/*
 * ui.c - OLED 显示层实现
 */

#include <stdio.h>
#include "ui.h"
#include "oled.h"

static const char *status_text(uint8_t status)
{
    switch (status) {
    case MEAS_ST_NORMAL: return "NORMAL";
    case MEAS_ST_WARN:   return "WARN";
    case MEAS_ST_ALARM:  return "ALARM";
    case MEAS_ST_FAULT:  return "FAULT";
    case MEAS_ST_UNCAL:  return "UNCAL";
    default:             return "?";
    }
}

static void format_fixed(char *out, float value, int frac)
{
    int32_t scale;
    int32_t scaled;
    int32_t integer;
    int32_t fraction;

    if (frac == 5) {
        scale = 100000;
    } else if (frac == 3) {
        scale = 1000;
    } else {
        scale = 10;
    }

    scaled = (int32_t)(value * (float)scale +
                       ((value >= 0.0f) ? 0.5f : -0.5f));
    integer = scaled / scale;
    fraction = scaled % scale;
    if (fraction < 0) {
        fraction = -fraction;
    }

    if (frac == 5) {
        (void)sprintf(out, "%ld.%05ld", (long)integer, (long)fraction);
    } else if (frac == 3) {
        (void)sprintf(out, "%ld.%03ld", (long)integer, (long)fraction);
    } else {
        (void)sprintf(out, "%ld.%01ld", (long)integer, (long)fraction);
    }
}

int ui_init(void)
{
    return oled_init();
}
/* 刷新一屏测量结果，返回 I2C 错误码。
*显示测量结果的格式如下：
*GAP   123.456 MM
*TEMP  25.0 C
*STAT  NORMAL
*CH0   12345678
*CH1   87654321
*RATIO 0.12345
*/
int ui_show_result(const measure_result_t *r)
{
    char line[32];
    char num[24];

    if (r == 0) {
        return -1;
    }

    oled_clear();

    format_fixed(num, r->dist_mm, 3);
    (void)sprintf(line, "GAP   %s MM", num);
    oled_draw_text(0u, 0u, line);

    format_fixed(num, r->temp_c, 1);
    (void)sprintf(line, "TEMP  %s C", num);
    oled_draw_text(0u, 8u, line);

    (void)sprintf(line, "STAT  %s", status_text(r->status));
    oled_draw_text(0u, 16u, line);

    (void)sprintf(line, "CH0   %lu", (unsigned long)r->ch0);
    oled_draw_text(0u, 24u, line);

    (void)sprintf(line, "CH1   %lu", (unsigned long)r->ch1);
    oled_draw_text(0u, 32u, line);

    format_fixed(num, r->ratio, 5);
    (void)sprintf(line, "RATIO %s", num);
    oled_draw_text(0u, 40u, line);

    return oled_update();
}
