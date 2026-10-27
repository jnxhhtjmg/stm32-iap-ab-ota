/**
 * @file  oled.h
 * @brief Minimal SSD1306 128x64 driver over I2C1 (PB8/PB9), no HAL.
 */
#ifndef OLED_H
#define OLED_H

#include <stdint.h>

void oled_init(void);
int  oled_ok(void);
void oled_clear(void);
void oled_flush(void);
void oled_text(uint8_t x, uint8_t page, const char *s);
void oled_text2(uint8_t x, uint8_t page, const char *s);
void oled_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t fill);
void oled_text_at(uint8_t x, uint8_t y, const char *s, uint8_t scale, uint8_t color);

#endif /* OLED_H */
