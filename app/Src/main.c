/**
 * @file  main.c
 * @brief Demo application for the A/B IAP bootloader.
 *
 * What it demonstrates:
 *   - runs in place from either slot (link address differs per target)
 *   - asks the bootloader to confirm a trial image after a self-test
 *   - IWDG based crash detection: "fail" stops feeding the dog, the reset is
 *     counted by the bootloader and after AB_MAX_ATTEMPTS the old slot is back
 *   - "boot" jumps back to the bootloader on request, no reset button needed
 */
#include "stm32f4xx.h"
#include "ab_config.h"
#include "app_config.h"
#include "oled.h"
#include <stdint.h>
#include <string.h>

#define UART_BAUD  115200u

#ifndef APP_NEVER_CONFIRM
#define APP_NEVER_CONFIRM 0
#endif

static volatile uint32_t s_ticks;
static char s_cmd[32];
static uint32_t s_cmd_len;
static uint32_t s_heartbeat;
static uint32_t s_boot_ms;

int main(void);

/* --------------------------------------------------------------- helpers */
void SysTick_Handler(void)
{
    s_ticks++;
}

static uint32_t millis(void)
{
    return s_ticks;
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = millis();

    while ((millis() - start) < ms) {
        __NOP();
    }
}

#define RX_RING 64
static volatile uint8_t s_rx[RX_RING];
static volatile uint8_t s_rx_head;
static volatile uint8_t s_rx_tail;

void USART1_IRQHandler(void)
{
    uint32_t sr = USART1->SR;

    if ((sr & (USART_SR_RXNE | USART_SR_ORE)) != 0u) {
        uint8_t b = (uint8_t)(USART1->DR & 0xFFu);
        uint8_t next = (uint8_t)((s_rx_head + 1u) % RX_RING);

        if (next != s_rx_tail) {
            s_rx[s_rx_head] = b;
            s_rx_head = next;
        }
    }
}

static void uart_init(uint32_t baud)
{


    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    GPIOA->MODER = (GPIOA->MODER & ~((3u << 18) | (3u << 20))) |
                   (2u << 18) | (2u << 20);
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~((0xFu << 4) | (0xFu << 8))) |
                    (7u << 4) | (7u << 8);
    GPIOA->OSPEEDR |= (3u << 18) | (3u << 20);
    GPIOA->PUPDR = (GPIOA->PUPDR & ~((3u << 18) | (3u << 20))) | (1u << 20);
    /* Calibrated on this board: USART1 sees an effective 5.25 MHz,
     * so BRR = 84e6 / baud (115200 -> 729 = 0x2D9, matching the
     * vendor demo that works on this wiring). */
    USART1->BRR = (uint16_t)((84000000u + baud / 2u) / baud);
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_EnableIRQ(USART1_IRQn);
}

static void putc_(char c)
{
    while ((USART1->SR & USART_SR_TXE) == 0u) {
    }
    USART1->DR = (uint8_t)c;
}

static void print(const char *s)
{
    while (*s != '\0') {
        putc_(*s++);
    }
}

static void print_u32(uint32_t v)
{
    char buf[11];
    int i = 10;

    buf[10] = '\0';
    do {
        buf[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u && i > 0);
    print(&buf[i]);
}

static void print_hex(uint32_t v)
{
    static const char h[] = "0123456789ABCDEF";
    char buf[11];
    int i;

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        buf[2 + i] = h[(v >> (28 - 4 * i)) & 0xFu];
    }
    buf[10] = '\0';
    print(buf);
}

static int getc_(char *c)
{
    if (s_rx_tail == s_rx_head) {
        return 0;
    }
    *c = (char)s_rx[s_rx_tail];
    s_rx_tail = (uint8_t)((s_rx_tail + 1u) % RX_RING);
    return 1;
}

/* ------------------------------------------------------------ bootloader */
static ab_api_t *api(void)
{
    ab_api_t *p = (ab_api_t *)(uintptr_t)AB_API_ADDR;

    return (p->magic == AB_API_MAGIC) ? p : 0;
}

static uint8_t current_slot(void)
{
    return ((uint32_t)(uintptr_t)&main >= AB_SLOT_B_ADDR) ? AB_SLOT_B : AB_SLOT_A;
}

static void print_reset_cause(void)
{
    uint32_t csr = RCC->CSR;

    print("reset=");
    if ((csr & RCC_CSR_IWDGRSTF) != 0u) {
        print("iwdg");
    } else if ((csr & RCC_CSR_SFTRSTF) != 0u) {
        print("sw");
    } else if ((csr & RCC_CSR_PORRSTF) != 0u) {
        print("por");
    } else if ((csr & RCC_CSR_PINRSTF) != 0u) {
        print("pin");
    } else {
        print("other");
    }
    RCC->CSR |= RCC_CSR_RMVF;
}

/* -------------------------------------------------------------- watchdog */
static void iwdg_start(uint32_t timeout_ms)
{
    /* LSI 32 kHz, /64 -> 500 Hz, reload = timeout_ms / 2 */
    IWDG->KR = 0x5555u;
    IWDG->PR = 4u;
    IWDG->RLR = timeout_ms / 2u;
    IWDG->KR = 0xAAAAu;
    IWDG->KR = 0xCCCCu;
}

static void iwdg_feed(void)
{
    IWDG->KR = 0xAAAAu;
}

/* --------------------------------------------------------------- commands */
static void cmd_info(void)
{
    ab_api_t *p = api();

    print("info slot=");
    print(current_slot() == AB_SLOT_A ? "A" : "B");
    print(" base=");
    print_hex(ab_slot_base(current_slot()));
    print(" ver=1.0.1 state=");
    if (p != 0) {
        print_u32(p->state());
    } else {
        print("noapi");
    }
    print(" uptime=");
    print_u32((millis() - s_boot_ms) / 1000u);
    print("s\r\n");
}

static void cmd_boot(void)
{
    ab_api_t *p = api();

    print("rebooting to bootloader\r\n");
    delay_ms(50);
    if (p != 0) {
        p->request_update();
    }
    NVIC_SystemReset();
}

static void cmd_fail(void)
{
    print("watchdog armed, stopping feed (rollback in ~3 boots)\r\n");
    for (;;) {
        /* deliberately stop feeding IWDG */
    }
}

static void dispatch(const char *line)
{
    if (line[0] == '\0') {
        return;
    }
    if (line[0] == 'b' && line[1] == 'o' && line[2] == 'o' && line[3] == 't') {
        cmd_boot();
    } else if (line[0] == 'f' && line[1] == 'a' && line[2] == 'i' && line[3] == 'l') {
        cmd_fail();
    } else if (line[0] == 'i' && line[1] == 'n' && line[2] == 'f' && line[3] == 'o') {
        cmd_info();
    } else {
        print("commands: info | boot | fail\r\n");
    }
}

/* ------------------------------------------------------------------- main */
/* HSE 8 MHz -> 168 MHz, identical to the original application firmware. */
static void clock_init(void)
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

    /* Do not call SystemCoreClockUpdate(): HSE_VALUE is not defined in this
     * project, so CMSIS would assume 25 MHz and report 525 MHz. */
    SystemCoreClock = 168000000u;
}

static void format_version(char *out)
{
    uint32_t v = 0u;
    ab_api_t *p = api();

    if (p != 0 && p->running_version != 0) {
        v = p->running_version();
    }
    if (v == 0u) {
        v = APP_VERSION;
    }
    out[0] = 'v';
    out[1] = (char)('0' + ((v >> 24) & 0xFu));
    out[2] = '.';
    out[3] = (char)('0' + ((v >> 16) & 0xFu));
    out[4] = '.';
    out[5] = (char)('0' + ((v >> 8) & 0xFu));
    out[6] = '\0';
}

/* One frame of the OLED animation.  Slot A sweeps an equalizer, slot B bounces
 * a ball, so a glance at the panel tells which slot is running. */
static void oled_tick(uint32_t t)
{
    uint8_t slot = current_slot();
    ab_api_t *p = api();
    uint8_t trial = (p != 0 && p->state() == AB_ST_TRIAL) ? 1u : 0u;
    char ver[8];
    uint8_t i;

    oled_clear();

    /* Accent rail: A on the left, B on the right.  A glance is enough. */
    oled_rect((slot == AB_SLOT_A) ? 0u : 126u, 0u, 2u, 64u, 1u);

    /* Top bar: name, heartbeat, running version. */
    oled_text_at(4u, 1u, "AB-IAP", 1u, 1u);
    if (((t / 500u) & 1u) != 0u) {
        oled_rect(62u, 2u, 4u, 4u, 1u);
    }
    format_version(ver);
    oled_text_at((uint8_t)(126u - 6u * (uint8_t)strlen(ver)), 1u, ver, 1u, 1u);
    oled_rect(0u, 9u, 128u, 1u, 1u);

    if (slot == AB_SLOT_A) {
        /* Outlined badge, letter drawn in white. */
        oled_rect(4u, 12u, 32u, 32u, 0u);
        oled_rect(5u, 13u, 30u, 30u, 0u);
        oled_text_at(15u, 21u, "A", 2u, 1u);

        if (trial != 0u) {
            oled_text_at(44u, 14u, "TRIAL", 1u, 1u);
            oled_text_at(44u, 26u, ((t / 300u) & 1u) ? "booting..." : "booting",
                         1u, 1u);
        } else {
            oled_text_at(44u, 14u, "CONFIRMED", 1u, 1u);
            oled_text_at(44u, 26u, "selftest ok", 1u, 1u);
        }

        /* Equalizer, 12 bands. */
        for (i = 0u; i < 12u; i++) {
            uint8_t h = (uint8_t)(4u + ((t / 90u + i * 5u) % 13u));
            oled_rect((uint8_t)(4u + i * 10u), (uint8_t)(62u - h), 7u, h, 1u);
        }
        oled_rect(0u, 63u, 128u, 1u, 1u);
    } else {
        /* Filled badge, letter knocked out (color 0). */
        oled_rect(92u, 12u, 32u, 32u, 1u);
        oled_text_at(103u, 21u, "B", 2u, 0u);

        if (trial != 0u) {
            oled_text_at(4u, 14u, "TRIAL", 1u, 1u);
            oled_text_at(4u, 26u, ((t / 300u) & 1u) ? "booting..." : "booting",
                         1u, 1u);
        } else {
            oled_text_at(4u, 14u, "CONFIRMED", 1u, 1u);
            oled_text_at(4u, 26u, "selftest ok", 1u, 1u);
        }

        /* Comet: three blocks sweeping right to left over a dashed track. */
        {
            uint8_t pos = (uint8_t)((t / 18u) % 150u);
            int16_t x0 = (int16_t)(-12 + pos);
            for (i = 0u; i < 3u; i++) {
                int16_t x = (int16_t)(x0 - (int16_t)(i * 14));
                if (x >= 0 && x < 120) {
                    oled_rect((uint8_t)x, 52u, 8u, 6u, 1u);
                }
            }
        }
        for (i = 0u; i < 128u; i = (uint8_t)(i + 10u)) {
            oled_rect(i, 63u, 5u, 1u, 1u);
        }
    }

    oled_flush();
}

int main(void)
{
    ab_api_t *p;
    uint32_t i;

    /* Defensive: a bootloader jump may leave PRIMASK set. */
    __enable_irq();

    clock_init();
    (void)SysTick_Config(SystemCoreClock / 1000u);

    uart_init(UART_BAUD);
    s_boot_ms = millis();

    oled_init();
    print(oled_ok() != 0 ? "oled ok\r\n" : "oled fail\r\n");
    if (oled_ok() != 0) {
        oled_tick(millis());
    }

    {
        char ver[8];
        format_version(ver);
        print("\r\napp ");
        print(ver);
        print(" slot=");
    }
    print(current_slot() == AB_SLOT_A ? "A" : "B");
    print(" ");
    print_reset_cause();
    print("\r\n");

    iwdg_start(4000u);

#if APP_NEVER_CONFIRM
    /* Rollback demo build: never confirm, never feed the dog.  The bootloader
     * counts three trial boots and switches back to the previous slot. */
    print("this build never confirms (rollback demo)\r\n");
    if (oled_ok() != 0) {
        oled_clear();
        oled_text2(6, 1, "BAD");
        oled_text2(6, 4, "FW");
        oled_text(0, 7, "no confirm");
        oled_flush();
    }
    for (;;) {
    }
#endif

    /* Self-test: keep it honest but short.  A real product would check the
     * sensor, the model CRC and the power rail here. */
    print("selftest");
    for (i = 0; i < (3000u / 500u); i++) {
        delay_ms(500u);
        iwdg_feed();
        print(".");
    }

    p = api();
    if (p == 0) {
        print(" no bootloader api, skipping confirm\r\n");
    } else if (p->state() == AB_ST_TRIAL) {
        if (p->confirm() == 0u) {
            print(" ok, trial confirmed\r\n");
        } else {
            print(" ok, confirm failed\r\n");
        }
    } else {
        print(" ok\r\n");
    }

    print("running, commands: info | boot | fail\r\n");
    s_cmd_len = 0u;
    s_heartbeat = millis();

    for (;;) {
        char c;
        static uint32_t s_oled_ms;

        iwdg_feed();

        while (getc_(&c) != 0) {
            if (c == '\r' || c == '\n') {
                s_cmd[s_cmd_len] = '\0';
                dispatch(s_cmd);
                s_cmd_len = 0u;
            } else if (s_cmd_len < (sizeof(s_cmd) - 1u)) {
                s_cmd[s_cmd_len++] = c;
            }
        }

        if ((millis() - s_oled_ms) >= 100u) {
            s_oled_ms = millis();
            if (oled_ok() != 0) {
                oled_tick(millis());
            }
        }

        if ((millis() - s_heartbeat) >= 1000u) {
            s_heartbeat = millis();
            print("hb,");
            print_u32((millis() - s_boot_ms) / 1000u);
            print("\r\n");
        }
    }
}
