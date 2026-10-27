/**
 * @file  ab_meta.h
 * @brief Append-only metadata log: slot, version, CRC32, SHA-256, state.
 *
 * One 96-byte record per event.  A record is valid only if magic and its own
 * CRC32 are correct, so a power loss while writing simply leaves a torn record
 * that is ignored and later overwritten by a compaction.
 */
#ifndef AB_META_H
#define AB_META_H

#include "ab_config.h"

/* Load the newest valid record.  Returns 1 when found, 0 when the log is empty. */
int ab_meta_load(ab_meta_rec_t *latest);

/* Newest record for a given slot.  Returns 1 when found. */
int ab_meta_find(uint8_t slot, ab_meta_rec_t *out);

/* Append one record (seq/CRC filled in).  allow_compact lets the caller erase
 * the sector when the log is full or torn; only the bootloader should pass 1. */
int ab_meta_append(ab_meta_rec_t *rec, int allow_compact);

/* Erase the log and keep `latest` as the only record. */
int ab_meta_compact(const ab_meta_rec_t *latest);

/* Number of records still writable before a compaction is required. */
uint32_t ab_meta_free_records(void);

/* Highest min_version seen in the log: the anti-rollback floor. */
uint32_t ab_meta_min_version(void);

#endif /* AB_META_H */
