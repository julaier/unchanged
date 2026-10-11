/*
 * app.c - 应用层实现
 */

#include "app.h"
#include "board.h"
#include "uart.h"
#include "adc_temp.h"
#include "ldc1614.h"
#include "measure.h"
#include "protocol.h"

/* ---- 可调参数 ---- */
#define APP_BAUD          115200u   /* 上位机串口波特率, 接 CH340 */
#define APP_REPORT_MS     100u      /* 周期上报, 100ms */
#define APP_TEMP_MS       1000u     /* 温度每秒读一次 */
#define APP_INTB_TIMEOUT  500u      /* 超过这么久没有 INTB 就判故障 */
#define APP_LINE_MAX      80u

static char s_line[APP_LINE_MAX];
static int  s_line_len;
static char s_out[192];
static uint32_t s_last_intb_ms;
static uint32_t s_last_temp_ms;
static float    s_temp_c;
/*用于发送数据*/
static void send(const char *buf, int len)
{
    if (len > 0) {
        uart_write((const uint8_t *)buf, (uint16_t)len);
    }
}

/* 初始化失败时发一个 "$E,<code>" 报文, 方便不看代码就知道卡在哪 */
static void report_boot_error(int code)
{
    int p = 0;
    char tmp[12];
    int n = 0;
    int v = -code;

    s_out[p++] = '$';
    s_out[p++] = 'E';
    s_out[p++] = ',';
    do {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v > 0);
    while (n > 0) {
        s_out[p++] = tmp[--n];
    }
    s_out[p++] = '\r';
    s_out[p++] = '\n';
    send(s_out, p);
}

void app_init(void)
{
    int rc;

    board_init();
    uart_init(APP_BAUD);

    ldc1614_init_driver();/*就是i2c硬件初始化*/
    adc_temp_init();

    measure_init();
    measure_load();

    protocol_init();/*初始化串口协议层,设置上报间隔为APP_REPORT_MS毫秒*/
    protocol_set_rate_ms(APP_REPORT_MS);/*设置上报间隔为APP_REPORT_MS毫秒，上面默认就是100这里可以不用*/

    rc = ldc1614_init(0, 0);/*初始化LDC1614，读取厂商ID,设备ID，这里为0也就是不读取*/
    if (rc != 0) {
        measure_fault();
        report_boot_error(rc);/*汇报错误*/
    }

    s_temp_c = (float)adc_temp_read_x10() / 10.0f;/*读取温度传感器的温度值，单位为摄氏度*/
    s_last_temp_ms = board_millis();/*记录上次读取温度的时间戳*/
    s_last_intb_ms = s_last_temp_ms;/*记录上次INTB中断的时间戳*/

    if (rc == 0) {
        uart_puts("$B,LDC1614 ready\r\n");
    }
}

void app_poll(void)
{
    uint32_t now = board_millis();/*现在的时间戳*/
    uint8_t c;

    /* ---- 1. 采样: INTB 到了就读一次 ---- */
    if (board_intb_take()) {
        uint32_t ch0 = 0u;
        uint32_t ch1 = 0u;
        uint16_t err = 0u;

        s_last_intb_ms = now;//更新上次INTB中断的时间戳

        if (ldc1614_read_raw(&ch0, &ch1, &err) == 0) {
            //如果距离上次读温度的时间大于APP_TEMP_MS，就再读一次温度，单位为摄氏度
            if ((uint32_t)(now - s_last_temp_ms) >= APP_TEMP_MS) {
                s_last_temp_ms = now;
                s_temp_c = (float)adc_temp_read_x10() / 10.0f;
            }
            measure_update(ch0, ch1, s_temp_c, now);
        } else {
            measure_fault();
        }
    }

    /* ---- 2. INTB 长时间不来 -> 故障 ---- */
    if ((uint32_t)(now - s_last_intb_ms) > APP_INTB_TIMEOUT) {
        measure_fault();
    }

    /* ---- 3. 上位机命令 ---- */
    while (uart_getc(&c)) {
        /*s_line_len用于记录接收到的字符数*/
        if (c == '\n' || c == '\r') {
            if (s_line_len > 0) {
                int n;
                s_line[s_line_len] = '\0';
                n = protocol_handle_line(s_line, s_out, (int)sizeof(s_out));
                send(s_out, n);/*发送命令是否成功*/
                s_line_len = 0;
            }
        } else if (s_line_len < ((int)sizeof(s_line) - 1)) {
            s_line[s_line_len++] = (char)c;
        }
    }

    /* ---- 4. 周期上报 ---- */
    send(s_out, protocol_tick(s_out, (int)sizeof(s_out), measure_latest(), now));

    board_feed_watchdog();
}
