/**
 * @file  ab_uart.h
 * @brief USART1 (PA9/PA10) polling driver plus 1 ms time base.
 */
#ifndef AB_UART_H
#define AB_UART_H

#include <stdint.h>

void ab_clock_init(void);
void ab_time_init(void);
uint32_t ab_millis(void);
void ab_delay_ms(uint32_t ms);

void ab_uart_init(uint32_t baud);
void ab_uart_putc(uint8_t c);
void ab_uart_write(const void *data, uint32_t len);
void ab_uart_write_str(const char *s);
int  ab_uart_getc(uint8_t *c, uint32_t timeout_ms);
void ab_uart_rx_flush(void);

#endif /* AB_UART_H */
