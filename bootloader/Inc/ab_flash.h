/**
 * @file  ab_flash.h
 * @brief STM32F4 internal flash driver (register level, no HAL).
 */
#ifndef AB_FLASH_H
#define AB_FLASH_H

#include <stdint.h>

#define AB_FLASH_OK         0
#define AB_FLASH_ERR        (-1)

uint32_t ab_flash_sector_of(uint32_t addr);
int ab_flash_erase_sector(uint32_t sector);
int ab_flash_erase_range(uint32_t addr, uint32_t len);
int ab_flash_program(uint32_t addr, const void *data, uint32_t len);
int ab_flash_blank(uint32_t addr, uint32_t len);

/* Extend the IWDG to ~8 s and reload it.  Safe to call even when the
 * application never started the watchdog. */
void ab_wdg_init(void);

/* Reload the IWDG if it is running. */
void ab_wdg_feed(void);

#endif /* AB_FLASH_H */
