/*
 * oled.c - 4 针 I2C OLED 驱动实现
 *
 * 显存按 SSD1306 的页格式排列:
 *   一页 8 行, 每列 1 字节, bit0 在上, bit7 在下。
 * 每次刷新分 8 页发送, 共用 LDC1614 的 I2C1。
 */

#include <string.h>
#include "oled.h"
#include "hard_i2c.h"

typedef struct {
    char    ch;
    uint8_t col[5];
} oled_glyph_t;

/* 只包含当前界面用到的字符, 节省 Flash。 */
static const oled_glyph_t s_font[] = {
    {' ', {0x00u, 0x00u, 0x00u, 0x00u, 0x00u}},
    {'-', {0x08u, 0x08u, 0x08u, 0x08u, 0x08u}},
    {'?', {0x02u, 0x01u, 0x51u, 0x09u, 0x06u}},
    {'.', {0x00u, 0x60u, 0x60u, 0x00u, 0x00u}},
    {'0', {0x3Eu, 0x51u, 0x49u, 0x45u, 0x3Eu}},
    {'1', {0x00u, 0x42u, 0x7Fu, 0x40u, 0x00u}},
    {'2', {0x42u, 0x61u, 0x51u, 0x49u, 0x46u}},
    {'3', {0x21u, 0x41u, 0x45u, 0x4Bu, 0x31u}},
    {'4', {0x18u, 0x14u, 0x12u, 0x7Fu, 0x10u}},
    {'5', {0x27u, 0x45u, 0x45u, 0x45u, 0x39u}},
    {'6', {0x3Cu, 0x4Au, 0x49u, 0x49u, 0x30u}},
    {'7', {0x01u, 0x71u, 0x09u, 0x05u, 0x03u}},
    {'8', {0x36u, 0x49u, 0x49u, 0x49u, 0x36u}},
    {'9', {0x06u, 0x49u, 0x49u, 0x29u, 0x1Eu}},
    {'A', {0x7Eu, 0x11u, 0x11u, 0x11u, 0x7Eu}},
    {'C', {0x3Eu, 0x41u, 0x41u, 0x41u, 0x22u}},
    {'E', {0x7Fu, 0x49u, 0x49u, 0x49u, 0x41u}},
    {'F', {0x7Fu, 0x09u, 0x09u, 0x09u, 0x01u}},
    {'G', {0x3Eu, 0x41u, 0x49u, 0x49u, 0x7Au}},
    {'H', {0x7Fu, 0x08u, 0x08u, 0x08u, 0x7Fu}},
    {'I', {0x00u, 0x41u, 0x7Fu, 0x41u, 0x00u}},
    {'L', {0x7Fu, 0x40u, 0x40u, 0x40u, 0x40u}},
    {'M', {0x7Fu, 0x02u, 0x0Cu, 0x02u, 0x7Fu}},
    {'N', {0x7Fu, 0x04u, 0x08u, 0x10u, 0x7Fu}},
    {'O', {0x3Eu, 0x41u, 0x41u, 0x41u, 0x3Eu}},
    {'P', {0x7Fu, 0x09u, 0x09u, 0x09u, 0x06u}},
    {'R', {0x7Fu, 0x09u, 0x19u, 0x29u, 0x46u}},
    {'S', {0x46u, 0x49u, 0x49u, 0x49u, 0x31u}},
    {'T', {0x01u, 0x01u, 0x7Fu, 0x01u, 0x01u}},
    {'U', {0x3Fu, 0x40u, 0x40u, 0x40u, 0x3Fu}},
    {'W', {0x7Fu, 0x20u, 0x18u, 0x20u, 0x7Fu}}
};

static uint8_t s_buffer[OLED_WIDTH * OLED_HEIGHT / 8u];
static uint8_t s_tx[OLED_WIDTH + 1u];
static uint8_t s_ready;
/* 查找字符的点阵数据, 返回 5 字节列数据指针, 找不到返回空格的点阵。 */
static const uint8_t *oled_glyph(char ch)
{
    unsigned int i;

    for (i = 0u; i < (sizeof(s_font) / sizeof(s_font[0])); i++) {
        if (s_font[i].ch == ch) {
            return s_font[i].col;
        }
    }
    return s_font[0].col;
}
/* 设置显存里某个像素点, 不立即刷新到屏幕。
*高度超了就不设置, 宽度超了也不设置。
*/
static void oled_set_pixel(uint8_t x, uint8_t y)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) {
        return;
    }
    s_buffer[((uint16_t)(y / 8u) * OLED_WIDTH) + x] |=
        (uint8_t)(1u << (y % 8u));
}
/* 发送一组命令, count=命令字节数, 1~31
*成功返回 0, 失败返回 <0 错误码,-3 = 参数错误, -4 = I2C 写失败
*/
static int oled_send_cmds(const uint8_t *cmd, uint8_t count)
{
    uint8_t tx[32];

    if (cmd == 0 || count == 0u || count > 31u) {
        return -3;
    }

    tx[0] = 0x00u;
    memcpy(&tx[1], cmd, count);
    return hard_i2c_write(OLED_I2C_ADDR, tx, (uint16_t)count + 1u);
}
/* OLED 初始化, 成功返回 0, 失败返回 <0 错误码,-3 = 参数错误, -4 = I2C 写失败
*/
int oled_init(void)
{
    static const uint8_t init_cmds[] = {
        0xAEu,              /* display off 中文：关闭显示 */
        0xD5u, 0x80u,       /* clock divide 中文：时钟分频 */
        0xA8u, 0x3Fu,       /* multiplex = 64 中文：复用 */
        0xD3u, 0x00u,       /* display offset = 0 中文：显示偏移 */
        0x40u,              /* start line = 0 中文：起始行 */
        0x8Du, 0x14u,       /* charge pump on 中文：电荷泵开启 */
        0x20u, 0x00u,       /* horizontal addressing mode 中文：水平寻址模式 */
        0xA1u,              /* segment remap 中文：段重映射 */
        0xC8u,              /* COM scan direction 中文：COM 扫描方向 */
        0xDAu, 0x12u,       /* COM pins 中文：COM 引脚 */
        0x81u, 0xCFu,       /* contrast 中文：对比度 */
        0xD9u, 0xF1u,       /* pre-charge 中文：预充电 */
        0xDBu, 0x40u,       /* VCOMH 中文：VCOMH */
        0xA4u,              /* resume RAM display 中文：恢复 RAM 显示 */
        0xA6u,              /* normal display 中文：正常显示 */
        0x2Eu,              /* deactivate scroll 中文：停用滚动 */
        0xAFu               /* display on 中文：开启显示 */
    };
    int rc;

    s_ready = 0u;
    oled_clear();/*显存清零*/

    rc = oled_send_cmds(init_cmds, (uint8_t)sizeof(init_cmds));
    if (rc != 0) {
        return rc;
    }

    s_ready = 1u;
    return 0;
}
/*显存清零*/
void oled_clear(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
}
/* 使用 5x7 点阵字体绘制一行文本, 字符间距 1 像素。 */
void oled_draw_text(uint8_t x, uint8_t y, const char *text)
{
    const uint8_t *glyph;
    uint8_t col;
    uint8_t row;

    if (text == 0) {
        return;
    }

    while (*text != '\0' && x < OLED_WIDTH) {
        glyph = oled_glyph(*text);
        for (col = 0u; col < 5u; col++) {
            for (row = 0u; row < 7u; row++) {
                if ((glyph[col] & (uint8_t)(1u << row)) != 0u) {
                    oled_set_pixel((uint8_t)(x + col), (uint8_t)(y + row));
                }
            }
        }
        x = (uint8_t)(x + 6u);
        text++;
    }
}
/*oled刷新*/
int oled_update(void)
{
    uint8_t page;
    uint8_t cmd[3];
    int rc;

    if (s_ready == 0u) {
        return -1;
    }

    for (page = 0u; page < (OLED_HEIGHT / 8u); page++) {
        cmd[0] = (uint8_t)(0xB0u + page);
        cmd[1] = (uint8_t)(OLED_COL_OFFSET & 0x0Fu);
        cmd[2] = (uint8_t)(0x10u | ((OLED_COL_OFFSET >> 4u) & 0x0Fu));

        rc = oled_send_cmds(cmd, 3u);
        if (rc != 0) {
            return rc;
        }

        s_tx[0] = 0x40u;
        memcpy(&s_tx[1], &s_buffer[(uint16_t)page * OLED_WIDTH], OLED_WIDTH);
        rc = hard_i2c_write(OLED_I2C_ADDR, s_tx, (uint16_t)(OLED_WIDTH + 1u));
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}
