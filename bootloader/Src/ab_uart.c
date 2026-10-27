/**
 * @file  ab_uart.c
 * @brief USART1 polling driver, HSI 16 MHz clock tree.
 */
#include "ab_uart.h"
#include "stm32f4xx.h"

static volatile uint32_t s_ticks;

void SysTick_Handler(void)
{
    s_ticks++;
}

/* 168 MHz from HSE 8 MHz, same tree as the original application firmware.
 * Falls back to HSI 16 MHz / PLL if the crystal does not start. */
void ab_clock_init(void)
{
    uint32_t pllcfgr;
    uint32_t guard;

    RCC->CR |= RCC_CR_HSEON;
    for (guard = 0; guard < 200000u; guard++) {
        if ((RCC->CR & RCC_CR_HSERDY) != 0u) {
            break;
        }
    }

    if ((RCC->CR & RCC_CR_HSERDY) != 0u) {
        pllcfgr = (8u << 0) | (336u << 6) | (0u << 16) |
                  RCC_PLLCFGR_PLLSRC_HSE | (7u << 24);
    } else {
        pllcfgr = (16u << 0) | (336u << 6) | (0u << 16) | (7u << 24);
    }

    RCC->PLLCFGR = pllcfgr;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0u) {
    }

    FLASH->ACR = FLASH_ACR_LATENCY_5WS | FLASH_ACR_PRFTEN |
                 FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 |
                RCC_CFGR_PPRE2_DIV2 | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }

    /* Do not call SystemCoreClockUpdate(): this project does not define
     * HSE_VALUE, so CMSIS assumes 25 MHz and reports 525 MHz here. */
    SystemCoreClock = 168000000u;
}

void ab_time_init(void)
{
    /* 1 ms tick, lowest priority. */
    (void)SysTick_Config(SystemCoreClock / 1000u);
}

uint32_t ab_millis(void)
{
    return s_ticks;
}

void ab_delay_ms(uint32_t ms)
{
    uint32_t start = ab_millis();

    while ((ab_millis() - start) < ms) {
        __NOP();
    }
}

void ab_uart_init(uint32_t baud)
{


    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* PA9 = TX, PA10 = RX, alternate function 7. */
    GPIOA->MODER = (GPIOA->MODER & ~((3u << 18) | (3u << 20))) |
                   (2u << 18) | (2u << 20);
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~((0xFu << 4) | (0xFu << 8))) |
                    (7u << 4) | (7u << 8);
    GPIOA->OSPEEDR |= (3u << 18) | (3u << 20);
    GPIOA->PUPDR = (GPIOA->PUPDR & ~((3u << 18) | (3u << 20))) | (1u << 20);

    USART1->CR1 = 0u;
    USART1->CR2 = 0u;
    USART1->CR3 = 0u;
    /* Calibrated on this board: USART1 sees an effective 5.25 MHz,
     * so BRR = 84e6 / baud (115200 -> 729 = 0x2D9, matching the
     * vendor demo that works on this wiring). */
    USART1->BRR = (uint16_t)((84000000u + baud / 2u) / baud);

    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void ab_uart_putc(uint8_t c)
{
    while ((USART1->SR & USART_SR_TXE) == 0u) {
    }
    USART1->DR = c;
}

void ab_uart_write(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;

    while (len-- != 0u) {
        ab_uart_putc(*p++);
    }
    while ((USART1->SR & USART_SR_TC) == 0u) {
    }
}

void ab_uart_write_str(const char *s)
{
    while (*s != '\0') {
        ab_uart_putc((uint8_t)*s++);
    }
}

int ab_uart_getc(uint8_t *c, uint32_t timeout_ms)
{
    uint32_t start = ab_millis();

    for (;;) {
        uint32_t sr = USART1->SR;

        if ((sr & USART_SR_RXNE) != 0u) {
            *c = (uint8_t)(USART1->DR & 0xFFu);
            return 0;
        }
        if ((sr & USART_SR_ORE) != 0u) {
            (void)USART1->DR;
        }
        if ((ab_millis() - start) >= timeout_ms) {
            return -1;
        }
    }
}

void ab_uart_rx_flush(void)
{
    uint32_t guard = 64u;

    while (guard-- != 0u) {
        if ((USART1->SR & USART_SR_RXNE) == 0u) {
            break;
        }
        (void)USART1->DR;
    }
}
