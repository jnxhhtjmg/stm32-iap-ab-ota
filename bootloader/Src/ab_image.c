/**
 * @file  ab_image.c
 * @brief Validate an application image inside a slot.
 */
#include "ab_image.h"
#include "ab_util.h"

#define AB_SRAM_START   0x20000000u
#define AB_SRAM_END     0x20020000u

int ab_image_vectors_ok_size(uint8_t slot, uint32_t limit)
{
    uint32_t base = ab_slot_base(slot);
    uint32_t sp = *(const volatile uint32_t *)(uintptr_t)base;
    uint32_t rv = *(const volatile uint32_t *)(uintptr_t)(base + 4u);

    if (limit < 8u || limit > ab_slot_size(slot)) {
        return 0;
    }
    if (sp < AB_SRAM_START || sp > AB_SRAM_END || (sp & 3u) != 0u) {
        return 0;
    }
    if ((rv & 1u) == 0u) {
        return 0;
    }
    rv &= ~1u;
    if (rv < base || rv >= (base + limit)) {
        return 0;
    }
    return 1;
}

int ab_image_vectors_ok(uint8_t slot)
{
    return ab_image_vectors_ok_size(slot, ab_slot_size(slot));
}

int ab_image_verify(const ab_meta_rec_t *rec, int full_sha)
{
    uint32_t base;
    uint32_t size;
    uint32_t crc;
    uint8_t sha[32];
    uint32_t i;

    if (rec->slot > AB_SLOT_B) {
        return 0;
    }
    base = ab_slot_base(rec->slot);
    size = rec->img_size;

    if (size < 8u || size > ab_slot_size(rec->slot) || size > AB_IMAGE_MAX) {
        return 0;
    }
    if (!ab_image_vectors_ok_size(rec->slot, size)) {
        return 0;
    }

    crc = ab_crc32((const void *)(uintptr_t)base, size);
    if (crc != rec->img_crc32) {
        return 0;
    }

    if (full_sha) {
        ab_sha256((const void *)(uintptr_t)base, size, sha);
        for (i = 0; i < 32u; i++) {
            if (sha[i] != rec->sha256[i]) {
                return 0;
            }
        }
    }
    return 1;
}
