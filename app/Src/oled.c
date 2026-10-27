/**
 * @file  oled.c
 * @brief SSD1306 128x64 on PB8 (SCL) / PB9 (SDA), bit-banged I2C.
 *
 * The hardware I2C1 peripheral wedged in BUSY on this board, so the bus is
 * driven manually.  Open-drain pins + the module's pull-ups do the level
 * shifting; roughly 250 kHz, which is plenty for a 1 KB frame buffer.
 */
#include "oled.h"
#include "oled_font.h"
#include "stm32f4xx.h"
#include <string.h>

#define OLED_ADDR   0x3Cu
#define OLED_W      128u
#define OLED_H      64u

static uint8_t s_fb[OLED_W * OLED_H / 8u];
static int s_ok;
static uint8_t s_addr = OLED_ADDR;

/* ------------------------------------------------------------ bit-banged I2C */
static void dly(void)
{
    volatile uint32_t i;
    for (i = 0; i < 250u; i++) {
        __NOP();
    }
}

static void scl(uint8_t v)
{
    GPIOB->BSRR = (v != 0u) ? (1u << 8) : (1u << (8 + 16));
}

static void sda(uint8_t v)
{
    GPIOB->BSRR = (v != 0u) ? (1u << 9) : (1u << (9 + 16));
}

static void bb_start(void)
{
    sda(1); dly();
    scl(1); dly();
    sda(0); dly();
    scl(0); dly();
}

static void bb_stop(void)
{
    sda(0); dly();
    scl(1); dly();
    sda(1); dly();
}

/* Returns 0 when the byte was acknowledged. */
static int bb_write(uint8_t b)
{
    uint8_t i;
    int ack;

    for (i = 0; i < 8u; i++) {
        sda((uint8_t)((b & 0x80u) != 0u));
        dly();
        scl(1); dly();
        scl(0); dly();
        b = (uint8_t)(b << 1);
    }
    sda(1); dly();
    scl(1); dly();
    ack = (GPIOB->IDR & (1u << 9)) == 0u;
    scl(0); dly();
    return ack ? 0 : -1;
}

static void i2c_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    GPIOB->MODER = (GPIOB->MODER & ~((3u << 16) | (3u << 18))) |
                   (1u << 16) | (1u << 18);           /* output */
    GPIOB->OTYPER |= (1u << 8) | (1u << 9);           /* open drain */
    GPIOB->OSPEEDR |= (3u << 16) | (3u << 18);
    GPIOB->PUPDR = (GPIOB->PUPDR & ~((3u << 16) | (3u << 18))) |
                   (1u << 16) | (1u << 18);           /* pull-up */
    scl(1);
    sda(1);
}

/* data[0] is the control byte (0x00 command / 0x40 data). */
static int i2c_write(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    bb_start();
    if (bb_write((uint8_t)(s_addr << 1)) != 0) {
        bb_stop();
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (bb_write(data[i]) != 0) {
            bb_stop();
            return -1;
        }
    }
    bb_stop();
    return 0;
}

static int i2c_probe(uint8_t addr)
{
    int ack;

    bb_start();
    ack = bb_write((uint8_t)(addr << 1));
    bb_stop();
    return ack;
}

static int oled_cmds(const uint8_t *cmds, uint32_t n)
{
    uint8_t buf[32];

    if (n > sizeof(buf) - 1u) {
        return -1;
    }
    buf[0] = 0x00u;
    memcpy(&buf[1], cmds, n);
    return i2c_write(buf, n + 1u);
}

/* ------------------------------------------------------------------ panel */
static const uint8_t s_init[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14,
    0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1,
    0xDB, 0x40, 0xA4, 0xA6, 0x2E, 0xAF,
};

void oled_init(void)
{
    i2c_init();
    memset(s_fb, 0, sizeof(s_fb));

    if (i2c_probe(0x3Cu) == 0) {
        s_addr = 0x3Cu;
    } else if (i2c_probe(0x3Du) == 0) {
        s_addr = 0x3Du;
    } else {
        s_ok = 0;
        return;
    }

    if (oled_cmds(s_init, sizeof(s_init)) != 0) {
        s_ok = 0;
        return;
    }
    s_ok = 1;
    oled_flush();
}

int oled_ok(void)
{
    return s_ok;
}

void oled_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

static void oled_pixel(uint8_t x, uint8_t y)
{
    if (x >= OLED_W || y >= OLED_H) {
        return;
    }
    s_fb[(y / 8u) * OLED_W + x] |= (uint8_t)(1u << (y % 8u));
}

static void oled_pixel_color(uint8_t x, uint8_t y, uint8_t color)
{
    if (x >= OLED_W || y >= OLED_H) {
        return;
    }
    if (color != 0u) {
        s_fb[(y / 8u) * OLED_W + x] |= (uint8_t)(1u << (y % 8u));
    } else {
        s_fb[(y / 8u) * OLED_W + x] &= (uint8_t)~(1u << (y % 8u));
    }
}

static void oled_char_xy(uint8_t x, uint8_t y, char c, uint8_t scale, uint8_t color)
{
    uint8_t idx, col, bit, sx, sy;

    if (c < 32 || c > 127) {
        c = '?';
    }
    idx = (uint8_t)(c - 32);
    for (col = 0; col < 5u; col++) {
        uint8_t line = oled_font5x7[idx][col];

        for (bit = 0; bit < 7u; bit++) {
            if ((line & (1u << bit)) == 0u) {
                continue;
            }
            for (sx = 0; sx < scale; sx++) {
                for (sy = 0; sy < scale; sy++) {
                    oled_pixel_color((uint8_t)(x + col * scale + sx),
                                     (uint8_t)(y + bit * scale + sy), color);
                }
            }
        }
    }
}

/* Draw text at an absolute pixel position.  color 0 clears pixels, which is
 * how the filled B badge knocks its letter out of the block. */
void oled_text_at(uint8_t x, uint8_t y, const char *s, uint8_t scale, uint8_t color)
{
    while (*s != '\0') {
        oled_char_xy(x, y, *s++, scale, color);
        x = (uint8_t)(x + 6u * scale);
    }
}

void oled_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t fill)
{
    uint8_t i, j;

    for (i = 0; i < w; i++) {
        for (j = 0; j < h; j++) {
            if (fill != 0u || i == 0u || j == 0u || i == w - 1u || j == h - 1u) {
                oled_pixel((uint8_t)(x + i), (uint8_t)(y + j));
            }
        }
    }
}

static void oled_char(uint8_t x, uint8_t page, char c, uint8_t scale)
{
    uint8_t idx, col, bit, sx, sy;

    if (c < 32 || c > 127) {
        c = '?';
    }
    idx = (uint8_t)(c - 32);
    for (col = 0; col < 5u; col++) {
        uint8_t line = oled_font5x7[idx][col];

        for (bit = 0; bit < 7u; bit++) {
            if ((line & (1u << bit)) == 0u) {
                continue;
            }
            for (sx = 0; sx < scale; sx++) {
                for (sy = 0; sy < scale; sy++) {
                    oled_pixel((uint8_t)(x + col * scale + sx),
                               (uint8_t)(page * 8u + bit * scale + sy));
                }
            }
        }
    }
}

void oled_text(uint8_t x, uint8_t page, const char *s)
{
    while (*s != '\0') {
        oled_char(x, page, *s++, 1u);
        x = (uint8_t)(x + 6u);
    }
}

void oled_text2(uint8_t x, uint8_t page, const char *s)
{
    while (*s != '\0') {
        oled_char(x, page, *s++, 2u);
        x = (uint8_t)(x + 12u);
    }
}

void oled_flush(void)
{
    uint8_t buf[1 + OLED_W];

    if (s_ok == 0) {
        return;
    }

    {
        static const uint8_t win[] = {0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};
        if (oled_cmds(win, sizeof(win)) != 0) {
            s_ok = 0;
            return;
        }
    }

    buf[0] = 0x40u;
    for (uint32_t page = 0; page < 8u; page++) {
        memcpy(&buf[1], &s_fb[page * OLED_W], OLED_W);
        if (i2c_write(buf, sizeof(buf)) != 0) {
            s_ok = 0;
            return;
        }
    }
}
