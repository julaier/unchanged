/*
 * uart.c - USART1 实现 (中断接收 + 查询发送)
 *
 * 接收走环形缓冲，主循环里用 uart_getc() 取走；发送是短帧，直接查询等待。
 * 本文件使用 STM32F10x Standard Peripheral Library USART/GPIO/RCC API。
 */

#include "stm32f10x.h"
#include "uart.h"

#define UART_RX_SIZE   128u

static volatile uint8_t  s_rx[UART_RX_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
//中断服务函数，用于接收数据，放入环形缓冲，然后在主循环里用 uart_getc() 取走
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        uint8_t c = (uint8_t)USART_ReceiveData(USART1);
        uint16_t next = (uint16_t)((s_rx_head + 1u) % UART_RX_SIZE);

        if (next != s_rx_tail) {              /* 满了就丢弃新数据 */
            s_rx[s_rx_head] = c;
            s_rx_head = next;
        }
    }
}
//baud = 115200, 8N1, PA9 = TX, PA10 = RX
void uart_init(uint32_t baud)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    /* PA9 = 复用推挽输出, PA10 = 浮空输入 */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = GPIO_Pin_9;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin  = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    USART_StructInit(&usart);
    usart.USART_BaudRate            = baud;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART1, &usart);
    //串口收到一个字节，触发中断
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART1, ENABLE);

    nvic.NVIC_IRQChannel                   = USART1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2u;
    nvic.NVIC_IRQChannelSubPriority        = 0u;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);
}
//发送数据，等待发送完成
void uart_write(const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    for (i = 0; i < len; i++) {
        // 等待发送缓冲空
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {
        }
        USART_SendData(USART1, buf[i]);
    }
}
//发送字符串，遇到 '\0' 结束
void uart_puts(const char *s)
{
    while (*s != '\0') {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {
        }
        USART_SendData(USART1, (uint16_t)(uint8_t)(*s++));
    }
}
//上位机发数据就用这，从接收缓冲取一个字节, 返回 1 = 取到, 0 = 没有数据
int uart_getc(uint8_t *c)
{

    if (s_rx_head == s_rx_tail) {
        return 0;
    }

    *c = s_rx[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1u) % UART_RX_SIZE);
    return 1;
}
