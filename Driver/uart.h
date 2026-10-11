/*
 * uart.h - USART1 串口 (接外部 CH340 模块)
 *
 * PA9  = USART1_TX -> CH340 的 RXD
 * PA10 = USART1_RX <- CH340 的 TXD
 * 3.3V TTL, 默认 115200 8N1。
 */
#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(uint32_t baud);
//发送数据，等待发送完成
void uart_write(const uint8_t *buf, uint16_t len);
//发送字符串，遇到 '\0' 结束
void uart_puts(const char *s);

//上位机发数据就用这，从接收缓冲取一个字节, 返回 1 = 取到, 0 = 没有数据
int uart_getc(uint8_t *c);

#endif /* UART_H */
