/*
 * oled.h - 4 针 I2C OLED 驱动 (SSD1306/SH1106)
 *
 * 接线:
 *   SCL = PB6
 *   SDA = PB7
 * 与 LDC1614 共用硬件 I2C1 总线, 从机地址不同即可。
 *
 * 常见 0.96/1.3 寸 128x64 模块:
 *   地址通常为 0x3C, 有些模块为 0x3D
 * 若使用 SH1106 屏且画面整体向右偏 2 列, 把 OLED_COL_OFFSET 改成 2。
 */
#ifndef OLED_H
#define OLED_H

#include <stdint.h>

#define OLED_I2C_ADDR      0x3Cu
#define OLED_WIDTH         128u
#define OLED_HEIGHT        64u
#define OLED_COL_OFFSET    0u

/* 成功返回 0, 失败返回 I2C 错误码。 */
int oled_init(void);/*发送 OLED 初始化命令。*/

/* 把内部显存发送到 OLED。 */
int oled_update(void);

/* 清空内部显存, 不立即刷新到屏幕。 */
void oled_clear(void);

/* 使用 5x7 点阵字体绘制一行文本, 字符间距 1 像素。 */
void oled_draw_text(uint8_t x, uint8_t y, const char *text);

#endif /* OLED_H */
