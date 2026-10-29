/**
 * @file  ab_meta.c
 * @brief Append-only metadata log with torn-write tolerance.
 */
#include "ab_meta.h"
#include "ab_flash.h"
#include "ab_util.h"
#include <stddef.h>
#include <string.h>

#define REC_CRC_LEN   offsetof(ab_meta_rec_t, crc32)

typedef char ab_meta_size_check[(sizeof(ab_meta_rec_t) == AB_REC_SIZE) ? 1 : -1];

static const ab_meta_rec_t *rec_at(uint32_t offset)
{
    return (const ab_meta_rec_t *)(uintptr_t)(AB_META_ADDR + offset);
}

static int rec_valid(const ab_meta_rec_t *r)
{
    if (r->magic != AB_META_MAGIC || r->rec_size != AB_REC_SIZE) {
        return 0;
    }
    return r->crc32 == ab_crc32(r, (uint32_t)REC_CRC_LEN);
}

uint32_t ab_meta_min_version(void)
{
    uint32_t off = 0u;
    uint32_t floor = 0u;

    while ((off + AB_REC_SIZE) <= AB_META_SIZE) {
        const ab_meta_rec_t *r = rec_at(off);

        if (r->magic == 0xFFFFFFFFu || !rec_valid(r)) {
            break;
        }
        if (r->min_version > floor) {
            floor = r->min_version;
        }
        off += AB_REC_SIZE;
    }
    return floor;
}

/**
 * Walk the log from the start.
 * @param latest   newest valid record (may be untouched when empty)
 * @param next_off offset of the first non-record (append position)
 * @param torn     set when a written-but-invalid record was met
 * @return 1 when at least one valid record exists
 */
static int meta_walk(ab_meta_rec_t *latest, uint32_t *next_off, int *torn)
{
    uint32_t off = 0u;
    uint32_t last_seq = 0u;
    int have = 0;

    *torn = 0;

    while ((off + AB_REC_SIZE) <= AB_META_SIZE) {
        const ab_meta_rec_t *r = rec_at(off);

        if (r->magic == 0xFFFFFFFFu) {
            break;
        }
        if (!rec_valid(r)) {
            *torn = 1;
            break;
        }
        if (!have || r->seq > last_seq) {
            *latest = *r;
            last_seq = r->seq;
            have = 1;
        }
        off += AB_REC_SIZE;
    }

    *next_off = off;
    return have;
}

int ab_meta_load(ab_meta_rec_t *latest)
{
    uint32_t next;
    int torn;

    return meta_walk(latest, &next, &torn);
}

int ab_meta_find(uint8_t slot, ab_meta_rec_t *out)
{
    uint32_t off = 0u;
    uint32_t best_seq = 0u;
    int found = 0;

    while ((off + AB_REC_SIZE) <= AB_META_SIZE) {
        const ab_meta_rec_t *r = rec_at(off);

        if (r->magic == 0xFFFFFFFFu) {
            break;
        }
        if (!rec_valid(r)) {
            break;
        }
        if (r->slot == slot && (!found || r->seq > best_seq)) {
            *out = *r;
            best_seq = r->seq;
            found = 1;
        }
        off += AB_REC_SIZE;
    }
    return found;
}

int ab_meta_compact(const ab_meta_rec_t *latest)
{
    if (ab_flash_erase_sector(ab_flash_sector_of(AB_META_ADDR)) != AB_FLASH_OK) {
        return -1;
    }
    if (latest != NULL) {
        ab_meta_rec_t rec = *latest;

        rec.seq += 1u;
        rec.crc32 = ab_crc32(&rec, (uint32_t)REC_CRC_LEN);
        if (ab_flash_program(AB_META_ADDR, &rec, AB_REC_SIZE) != AB_FLASH_OK) {
            return -1;
        }
    }
    return 0;
}

int ab_meta_append(ab_meta_rec_t *rec, int allow_compact)
{
    ab_meta_rec_t latest;
    ab_meta_rec_t verify;
    uint32_t next;
    uint32_t seq;
    int torn;
    int have = meta_walk(&latest, &next, &torn);

    if (torn || (next + AB_REC_SIZE) > AB_META_SIZE) {
        if (!allow_compact) {
            return -1;
        }
        if (ab_meta_compact(have ? &latest : NULL) != 0) {
            return -1;
        }
        next = have ? AB_REC_SIZE : 0u;
        seq = have ? latest.seq + 2u : 1u;
    } else {
        seq = have ? latest.seq + 1u : 1u;
    }

    rec->magic = AB_META_MAGIC;
    rec->rec_size = (uint16_t)AB_REC_SIZE;
    rec->seq = seq;
    memset(rec->reserved, 0, sizeof(rec->reserved));
    rec->crc32 = ab_crc32(rec, (uint32_t)REC_CRC_LEN);

    if (ab_flash_program(AB_META_ADDR + next, rec, AB_REC_SIZE) != AB_FLASH_OK) {
        return -1;
    }
    memcpy(&verify, rec_at(next), sizeof(verify));
    if (memcmp(&verify, rec, sizeof(verify)) != 0) {
        return -1;
    }
    return 0;
}

uint32_t ab_meta_free_records(void)
{
    ab_meta_rec_t latest;
    uint32_t next;
    int torn;

    (void)meta_walk(&latest, &next, &torn);
    return (AB_META_SIZE - next) / AB_REC_SIZE;
}
