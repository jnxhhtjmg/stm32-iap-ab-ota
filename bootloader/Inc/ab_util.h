/**
 * @file  ab_util.h
 * @brief CRC32 (IEEE 802.3) and SHA-256, dependency free.
 */
#ifndef AB_UTIL_H
#define AB_UTIL_H

#include <stdint.h>

/* CRC32: start with 0xFFFFFFFF, xor the result with 0xFFFFFFFF at the end. */
uint32_t ab_crc32_start(void);
uint32_t ab_crc32_update(uint32_t crc, const void *data, uint32_t len);
uint32_t ab_crc32_final(uint32_t crc);
uint32_t ab_crc32(const void *data, uint32_t len);

typedef struct {
    uint32_t h[8];
    uint64_t total;
    uint8_t  buf[64];
    uint32_t used;
} ab_sha256_ctx;

void ab_sha256_init(ab_sha256_ctx *ctx);
void ab_sha256_update(ab_sha256_ctx *ctx, const void *data, uint32_t len);
void ab_sha256_final(ab_sha256_ctx *ctx, uint8_t out[32]);
void ab_sha256(const void *data, uint32_t len, uint8_t out[32]);

#endif /* AB_UTIL_H */
