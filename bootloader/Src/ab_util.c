/**
 * @file  ab_util.c
 * @brief CRC32 (nibble table, small) and SHA-256 (FIPS 180-4).
 */
#include "ab_util.h"

/* ------------------------------------------------------------------ CRC32 */
static const uint32_t crc32_nibble[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

uint32_t ab_crc32_start(void)
{
    return 0xFFFFFFFFu;
}

uint32_t ab_crc32_update(uint32_t crc, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;

    while (len-- != 0u) {
        crc ^= *p++;
        crc = (crc >> 4) ^ crc32_nibble[crc & 0x0Fu];
        crc = (crc >> 4) ^ crc32_nibble[crc & 0x0Fu];
    }
    return crc;
}

uint32_t ab_crc32_final(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

uint32_t ab_crc32(const void *data, uint32_t len)
{
    return ab_crc32_final(ab_crc32_update(ab_crc32_start(), data, len));
}

/* ----------------------------------------------------------------- SHA-256 */
#define ROR(x, n) (((x) >> (n)) | ((x) << (32u - (n))))

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static void sha256_block(ab_sha256_ctx *ctx, const uint8_t *p)
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t i;

    for (i = 0; i < 16u; i++) {
        w[i] = ((uint32_t)p[i * 4u] << 24) |
               ((uint32_t)p[i * 4u + 1u] << 16) |
               ((uint32_t)p[i * 4u + 2u] << 8) |
               ((uint32_t)p[i * 4u + 3u]);
    }
    for (i = 16; i < 64u; i++) {
        uint32_t s0 = ROR(w[i - 15u], 7) ^ ROR(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
        uint32_t s1 = ROR(w[i - 2u], 17) ^ ROR(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    a = ctx->h[0]; b = ctx->h[1]; c = ctx->h[2]; d = ctx->h[3];
    e = ctx->h[4]; f = ctx->h[5]; g = ctx->h[6]; h = ctx->h[7];

    for (i = 0; i < 64u; i++) {
        uint32_t s1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + s1 + ch + K[i] + w[i];
        uint32_t s0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
    ctx->h[4] += e; ctx->h[5] += f; ctx->h[6] += g; ctx->h[7] += h;
}

void ab_sha256_init(ab_sha256_ctx *ctx)
{
    ctx->h[0] = 0x6a09e667u; ctx->h[1] = 0xbb67ae85u;
    ctx->h[2] = 0x3c6ef372u; ctx->h[3] = 0xa54ff53au;
    ctx->h[4] = 0x510e527fu; ctx->h[5] = 0x9b05688cu;
    ctx->h[6] = 0x1f83d9abu; ctx->h[7] = 0x5be0cd19u;
    ctx->total = 0u;
    ctx->used = 0u;
}

void ab_sha256_update(ab_sha256_ctx *ctx, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;

    ctx->total += len;

    while (len-- != 0u) {
        ctx->buf[ctx->used++] = *p++;
        if (ctx->used == 64u) {
            sha256_block(ctx, ctx->buf);
            ctx->used = 0u;
        }
    }
}

void ab_sha256_final(ab_sha256_ctx *ctx, uint8_t out[32])
{
    uint64_t bits = ctx->total * 8u;
    uint32_t i;

    ctx->buf[ctx->used++] = 0x80u;
    if (ctx->used > 56u) {
        while (ctx->used < 64u) {
            ctx->buf[ctx->used++] = 0u;
        }
        sha256_block(ctx, ctx->buf);
        ctx->used = 0u;
    }
    while (ctx->used < 56u) {
        ctx->buf[ctx->used++] = 0u;
    }
    for (i = 0; i < 8u; i++) {
        ctx->buf[56u + i] = (uint8_t)(bits >> (56u - 8u * i));
    }
    sha256_block(ctx, ctx->buf);

    for (i = 0; i < 8u; i++) {
        out[i * 4u]      = (uint8_t)(ctx->h[i] >> 24);
        out[i * 4u + 1u] = (uint8_t)(ctx->h[i] >> 16);
        out[i * 4u + 2u] = (uint8_t)(ctx->h[i] >> 8);
        out[i * 4u + 3u] = (uint8_t)(ctx->h[i]);
    }
}

void ab_sha256(const void *data, uint32_t len, uint8_t out[32])
{
    ab_sha256_ctx ctx;

    ab_sha256_init(&ctx);
    ab_sha256_update(&ctx, data, len);
    ab_sha256_final(&ctx, out);
}
