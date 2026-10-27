/**
 * @file  ab_image.h
 * @brief Boot-time image sanity checks.
 */
#ifndef AB_IMAGE_H
#define AB_IMAGE_H

#include "ab_config.h"

/* Full check: vector table sanity + CRC32, optionally SHA-256. */
int ab_image_verify(const ab_meta_rec_t *rec, int full_sha);

/* Vector sanity within an explicit image length (used by ab_image_verify). */
int ab_image_vectors_ok_size(uint8_t slot, uint32_t limit);

/* Cheap check used when no metadata exists yet (whole slot). */
int ab_image_vectors_ok(uint8_t slot);

#endif /* AB_IMAGE_H */
