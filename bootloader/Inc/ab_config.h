/**
 * @file    ab_config.h
 * @brief   A/B bootloader + serial IAP: flash map, metadata record, public API.
 *
 * Layout for STM32F407VE (512 KB flash, sectors 16/16/16/16/64/128/128/128 KB):
 *
 *   0x08000000  sector 0-1 32 KB   bootloader (signed-image verify)
 *   0x08008000  sector 2   16 KB   metadata log (append only)
 *   0x0800C000  sector 3-5 208 KB  slot A  (16 + 64 + 128)
 *   0x08040000  sector 6-7 256 KB  slot B  (128 + 128)
 *
 * Both slots hold a raw application image: vector table first, no header.
 * The metadata log is the single source of truth (slot + version + CRC + SHA).
 */
#ifndef AB_CONFIG_H
#define AB_CONFIG_H

#include <stdint.h>

/* ------------------------------------------------------------------ flash */
#define AB_FLASH_BASE       0x08000000u
#define AB_BOOT_ADDR        0x08000000u
#define AB_BOOT_SIZE        0x00008000u   /* sector 0-1, 32 KB */

#define AB_META_ADDR        0x08008000u   /* sector 2, 16 KB */
#define AB_META_SIZE        0x00004000u

#define AB_SLOT_A_ADDR      0x0800C000u
#define AB_SLOT_A_SIZE      0x00034000u   /* 208 KB */
#define AB_SLOT_B_ADDR      0x08040000u
#define AB_SLOT_B_SIZE      0x00040000u   /* 256 KB */

/* Largest image accepted by either slot. */
#define AB_IMAGE_MAX        AB_SLOT_A_SIZE

/* ---------------------------------------------------------------- records */
#define AB_META_MAGIC       0x41424946u   /* "ABIF" */
#define AB_REC_SIZE         96u
#define AB_META_SLOTS_MAX   (AB_META_SIZE / AB_REC_SIZE)   /* 170 */

/* Record states. */
#define AB_ST_NONE          0u
#define AB_ST_TRIAL         1u
#define AB_ST_CONFIRMED     2u
#define AB_ST_BAD           3u

/* Record flags. */
#define AB_FLAG_UPDATE_REQ  0x0001u

/* sec_flags: image passed Ed25519 verification before being installed. */
#define AB_SEC_SIGNED       0x0001u

/* Attempts allowed before a trial image is rolled back. */
#define AB_MAX_ATTEMPTS     3u

/* --------------------------------------------------- cross-reset handshake */
/* CCM RAM is used because the application leaves it untouched.  Keep the
 * top 512 bytes of CCM reserved for the bootloader. */
#define AB_API_ADDR         0x1000FF00u
#define AB_REQ_ADDR         0x1000FE00u
#define AB_REQ_MAGIC        0xB00710ADu
#define AB_API_MAGIC        0x41424950u   /* "ABIP" */
#define AB_API_VERSION      2u

/* --------------------------------------------------------------- security */
/* Ed25519 signature over the 32-byte SHA-256 image digest.  The public key
 * is compiled in; the private key never touches the device. */
#define AB_SIG_BYTES        64u
extern const uint8_t ab_pubkey[32];

/* --------------------------------------------------------------- protocol */
#define AB_PROTO_VERSION    2u
#define AB_SYNC0            0xAAu
#define AB_SYNC1            0x55u
#define AB_MAX_PAYLOAD      512u
#define AB_DEFAULT_BAUD     115200u

/* Request types (response = type | 0x80). */
#define AB_CMD_HELLO        0x01u
#define AB_CMD_START        0x02u
#define AB_CMD_DATA         0x03u
#define AB_CMD_END          0x04u
#define AB_CMD_STATUS       0x05u
#define AB_CMD_CONFIRM      0x06u
#define AB_CMD_REBOOT       0x07u
#define AB_CMD_SET_BAUD     0x08u
#define AB_CMD_ABORT        0x09u
#define AB_CMD_INFO         0x0Au
#define AB_RSP_BIT          0x80u

/* Status codes carried in response payloads. */
#define AB_OK               0x00u
#define AB_ERR_CRC          0x01u
#define AB_ERR_LEN          0x02u
#define AB_ERR_STATE        0x03u
#define AB_ERR_SIZE         0x04u
#define AB_ERR_FLASH        0x05u
#define AB_ERR_VERIFY       0x06u
#define AB_ERR_VERSION      0x07u
#define AB_ERR_SIGNATURE    0x08u
#define AB_ERR_ROLLBACK     0x09u

/* ---------------------------------------------------------------- utility */
#define AB_SLOT_A           0u
#define AB_SLOT_B           1u
#define AB_SLOT_NONE        0xFFu

static inline uint32_t ab_slot_base(uint8_t slot)
{
    return (slot == AB_SLOT_B) ? AB_SLOT_B_ADDR : AB_SLOT_A_ADDR;
}

static inline uint32_t ab_slot_size(uint8_t slot)
{
    return (slot == AB_SLOT_B) ? AB_SLOT_B_SIZE : AB_SLOT_A_SIZE;
}

static inline uint8_t ab_slot_other(uint8_t slot)
{
    return (slot == AB_SLOT_B) ? AB_SLOT_A : AB_SLOT_B;
}

/* Metadata record, 96 bytes, append-only. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t rec_size;
    uint8_t  slot;          /* 0 = A, 1 = B */
    uint8_t  state;         /* AB_ST_* */
    uint32_t seq;           /* strictly increasing */
    uint32_t version;       /* 0xMMmmppbb */
    uint32_t img_size;
    uint32_t img_crc32;
    uint16_t attempts;
    uint16_t flags;
    uint8_t  sha256[32];
    uint32_t min_version;   /* anti-rollback floor, monotonic */
    uint32_t sec_flags;     /* bit0: image was signature-checked */
    uint8_t  reserved[24];
    uint32_t crc32;         /* over bytes [0 .. 92) */
} ab_meta_rec_t;

#define AB_REC_CRC_LEN      92u

/* API published by the bootloader in CCM before every jump. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t (*confirm)(void);          /* mark running trial image good */
    uint32_t (*state)(void);            /* AB_ST_* of the running slot */
    uint32_t (*active_slot)(void);
    uint32_t (*request_update)(void);   /* set boot-request magic + reset */
    uint32_t (*running_version)(void);  /* version of the active slot */
} ab_api_t;

#endif /* AB_CONFIG_H */
