/**
 * @file  main.c
 * @brief A/B serial IAP bootloader for STM32F407VE.
 *
 * Boot policy:
 *   1. newest metadata record decides the active slot and its state
 *   2. TRIAL: increment the attempt counter and boot; after AB_MAX_ATTEMPTS
 *      consecutive unconfirmed boots the image is marked BAD and the other
 *      slot is booted instead (no copy, both slots are runnable in place)
 *   3. CONFIRMED: boot directly
 *   4. update request (RAM magic / key / metadata flag) keeps the bootloader
 *      resident; otherwise a short UART window allows the host to catch it
 */
#include "stm32f4xx.h"
#include "ab_api.h"
#include "ab_config.h"
#include "ab_image.h"
#include "ab_meta.h"
#include "ab_proto.h"
#include "ab_uart.h"
#include <string.h>

#define AB_UART_WINDOW_MS   400u

static void print_str(const char *s)
{
    ab_uart_write_str(s);
}

static void print_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];
    int i;

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        buf[2 + i] = hex[(v >> (28 - 4 * i)) & 0xFu];
    }
    buf[10] = '\0';
    print_str(buf);
}

static void print_dec(uint32_t v)
{
    char buf[11];
    int i = 10;

    buf[10] = '\0';
    do {
        buf[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u && i > 0);
    print_str(&buf[i]);
}

static void boot_gpio_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
    GPIOE->MODER &= ~(3u << 6);            /* PE3 input */
    GPIOE->PUPDR = (GPIOE->PUPDR & ~(3u << 6)) | (1u << 6);
}

static int button_pressed(void)
{
    return (GPIOE->IDR & (1u << 3)) == 0u;
}

static void jump_to(uint8_t slot)
{
    uint32_t base = ab_slot_base(slot);
    uint32_t sp = *(volatile uint32_t *)(uintptr_t)base;
    uint32_t rv = *(volatile uint32_t *)(uintptr_t)(base + 4u);
    uint32_t i;

    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL = 0u;

    __disable_irq();
    for (i = 0; i < 8u; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
    SCB->VTOR = base;
    __DSB();
    __ISB();
    __set_MSP(sp);
    __DSB();
    __ISB();
    /* The application expects the reset state: PRIMASK cleared.  Without this
     * its SysTick never fires and delay loops hang until the watchdog bites. */
    __enable_irq();
    ((void (*)(void))rv)();

    for (;;) {
    }
}

/* Boot the other slot after the trial image failed.  Returns 1 on success. */
static int rollback(uint8_t failed_slot)
{
    uint8_t other = ab_slot_other(failed_slot);
    ab_meta_rec_t rec;

    if (!ab_meta_find(other, &rec)) {
        return 0;
    }
    if (rec.img_size != 0u) {
        if (!ab_image_verify(&rec, 0)) {
            return 0;
        }
    } else if (!ab_image_vectors_ok(other)) {
        return 0;
    }

    rec.state = AB_ST_CONFIRMED;
    rec.attempts = 0u;
    rec.flags = 0u;
    rec.min_version = ab_meta_min_version();   /* never lower the floor */
    rec.sec_flags |= AB_SEC_SIGNED;
    if (ab_meta_append(&rec, 1) != 0) {
        return 0;
    }
    return 1;
}

int main(void)
{
    ab_meta_rec_t latest;
    uint8_t active;
    uint8_t state;
    int have;
    int request;

    ab_clock_init();
    ab_time_init();
    ab_uart_init(AB_DEFAULT_BAUD);
    boot_gpio_init();

    print_str("\r\nAB-IAP boot v1.1\r\n");

    ab_api_publish();

    request = (*(volatile uint32_t *)(uintptr_t)AB_REQ_ADDR) == AB_REQ_MAGIC;
    *(volatile uint32_t *)(uintptr_t)AB_REQ_ADDR = 0u;

    have = ab_meta_load(&latest);
    active = have ? latest.slot : AB_SLOT_A;
    state = have ? latest.state : AB_ST_CONFIRMED;

    if (!have && !ab_image_vectors_ok(AB_SLOT_A)) {
        print_str("meta: empty, slot A blank\r\n");
        goto recovery;
    }
    if (!have) {
        /* Factory provisioning: remember the image we are about to trust so a
         * later rollback has a slot to go back to.  Size/CRC are unknown here,
         * so only the vector table is checked for this record. */
        ab_meta_rec_t boot_rec;

        memset(&boot_rec, 0, sizeof(boot_rec));
        boot_rec.slot = AB_SLOT_A;
        boot_rec.state = AB_ST_CONFIRMED;
        boot_rec.version = 0u;
        boot_rec.img_size = 0u;
        boot_rec.attempts = 0u;
        (void)ab_meta_append(&boot_rec, 1);
    }

    if (state == AB_ST_TRIAL) {
        if (latest.version < ab_meta_min_version()) {
            print_str("trial below version floor, rolling back\r\n");
            if (!rollback(active)) {
                goto recovery;
            }
        } else if (!ab_image_verify(&latest, 1)) {
            print_str("trial image invalid, rolling back\r\n");
            if (!rollback(active)) {
                goto recovery;
            }
        } else if (latest.attempts >= AB_MAX_ATTEMPTS) {
            print_str("trial attempts exhausted, rolling back\r\n");
            if (!rollback(active)) {
                goto recovery;
            }
        } else {
            latest.attempts++;
            (void)ab_meta_append(&latest, 1);
            print_str("trial boot ");
            print_dec(latest.attempts);
            print_str("/");
            print_dec(AB_MAX_ATTEMPTS);
            print_str("\r\n");
        }
    } else if (state == AB_ST_BAD) {
        if (!rollback(active)) {
            goto recovery;
        }
    }

reload:
    have = ab_meta_load(&latest);
    active = have ? latest.slot : AB_SLOT_A;
    state = have ? latest.state : AB_ST_CONFIRMED;

    print_str("meta: slot=");
    print_str(active == AB_SLOT_A ? "A" : "B");
    print_str(" state=");
    print_dec(state);
    print_str(" attempts=");
    print_dec(have ? latest.attempts : 0u);
    print_str("\r\n");

    if (have && (latest.flags & AB_FLAG_UPDATE_REQ) != 0u) {
        latest.flags = 0u;
        (void)ab_meta_append(&latest, 1);
        request = 1;
    }

    if (request || button_pressed()) {
        print_str("update mode requested\r\n");
        ab_proto_run();
    }

    if (ab_proto_listen(AB_UART_WINDOW_MS)) {
        print_str("update mode\r\n");
        ab_proto_run();
    }

    print_str("boot ");
    print_str(active == AB_SLOT_A ? "A" : "B");
    print_str(" @");
    print_hex32(ab_slot_base(active));
    print_str("\r\n");
    jump_to(active);

recovery:
    print_str("recovery: waiting for firmware\r\n");
    ab_proto_run();
    return 0;
}
