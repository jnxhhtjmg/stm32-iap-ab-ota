/**
 * @file  ab_flash.c
 * @brief STM32F407 flash: 16/16/16/16/64/128/128/128 KB sectors, x32 programming.
 */
#include "ab_flash.h"
#include "ab_config.h"
#include "stm32f4xx.h"

#define FLASH_TIMEOUT   0xFFFFFFFFu

void ab_wdg_feed(void)
{
    IWDG->KR = 0xAAAAu;
}

static int flash_wait(void)
{
    uint32_t guard = FLASH_TIMEOUT;

    while ((FLASH->SR & FLASH_SR_BSY) != 0u) {
        /* The application may have started the independent watchdog before
         * requesting an update; a 128 KB erase can outlive its 4 s timeout.
         * Reloading a stopped IWDG is harmless, so just feed it here. */
        ab_wdg_feed();
        if (--guard == 0u) {
            return AB_FLASH_ERR;
        }
    }
    return AB_FLASH_OK;
}

static void flash_clear_flags(void)
{
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_OPERR | FLASH_SR_WRPERR |
                FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;
}

static int flash_has_error(void)
{
    return (FLASH->SR & (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR |
                         FLASH_SR_PGPERR | FLASH_SR_PGSERR)) != 0u;
}

static void flash_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u) {
        FLASH->KEYR = 0x45670123u;
        FLASH->KEYR = 0xCDEF89ABu;
    }
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

uint32_t ab_flash_sector_of(uint32_t addr)
{
    if (addr < 0x08004000u) return 0u;
    if (addr < 0x08008000u) return 1u;
    if (addr < 0x0800C000u) return 2u;
    if (addr < 0x08010000u) return 3u;
    if (addr < 0x08020000u) return 4u;
    if (addr < 0x08040000u) return 5u;
    if (addr < 0x08060000u) return 6u;
    return 7u;
}

int ab_flash_erase_sector(uint32_t sector)
{
    int rc;

    if (sector > 7u) {
        return AB_FLASH_ERR;
    }

    flash_unlock();
    flash_wait();
    flash_clear_flags();

    FLASH->CR = FLASH_CR_SER | FLASH_CR_PSIZE_1 | (sector << FLASH_CR_SNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;

    rc = flash_wait();
    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_STRT);
    if (flash_has_error()) {
        rc = AB_FLASH_ERR;
    }
    flash_clear_flags();
    flash_lock();

    return rc;
}

int ab_flash_erase_range(uint32_t addr, uint32_t len)
{
    uint32_t first;
    uint32_t last;
    uint32_t s;

    if (len == 0u) {
        return AB_FLASH_OK;
    }
    if (addr < AB_FLASH_BASE || (addr + len) > (AB_FLASH_BASE + 0x00080000u)) {
        return AB_FLASH_ERR;
    }

    first = ab_flash_sector_of(addr);
    last  = ab_flash_sector_of(addr + len - 1u);

    for (s = first; s <= last; s++) {
        if (ab_flash_erase_sector(s) != AB_FLASH_OK) {
            return AB_FLASH_ERR;
        }
    }
    return AB_FLASH_OK;
}

int ab_flash_program(uint32_t addr, const void *data, uint32_t len)
{
    const uint32_t *src = (const uint32_t *)data;
    uint32_t words = len / 4u;
    uint32_t i;
    int rc = AB_FLASH_OK;

    if (addr < AB_FLASH_BASE || (len & 3u) != 0u) {
        return AB_FLASH_ERR;
    }

    flash_unlock();
    flash_wait();
    flash_clear_flags();

    FLASH->CR = FLASH_CR_PG | FLASH_CR_PSIZE_1;

    for (i = 0; i < words; i++) {
        if (flash_wait() != AB_FLASH_OK) {
            rc = AB_FLASH_ERR;
            break;
        }
        if (flash_has_error()) {
            rc = AB_FLASH_ERR;
            break;
        }
        *(volatile uint32_t *)(addr + i * 4u) = src[i];
    }

    if (flash_wait() != AB_FLASH_OK) {
        rc = AB_FLASH_ERR;
    }
    if (flash_has_error()) {
        rc = AB_FLASH_ERR;
    }

    FLASH->CR &= ~FLASH_CR_PG;
    flash_clear_flags();
    flash_lock();

    return rc;
}

int ab_flash_blank(uint32_t addr, uint32_t len)
{
    const uint32_t *p = (const uint32_t *)addr;
    uint32_t words = len / 4u;
    uint32_t i;

    for (i = 0; i < words; i++) {
        if (p[i] != 0xFFFFFFFFu) {
            return 0;
        }
    }
    return 1;
}
