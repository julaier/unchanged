/*
 * ui.h - OLED 显示层接口
 *
 * 只负责把测量结果排版成屏幕内容, 不参与采样和 I2C 时序。
 */
#ifndef UI_H
#define UI_H

#include "measure.h"

/* 初始化 OLED，返回 I2C 错误码。 */
int ui_init(void);

/* 刷新一屏测量结果，返回 I2C 错误码。 */
int ui_show_result(const measure_result_t *r);

#endif /* UI_H */
