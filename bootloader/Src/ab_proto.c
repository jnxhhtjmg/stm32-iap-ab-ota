/**
 * @file  ab_proto.c
 * @brief Frame protocol: HELLO / START / DATA / END / STATUS / CONFIRM /
 *        REBOOT / SET_BAUD / ABORT.
 *
 * Frame: AA 55 | ver | type | seq(2) | len(2) | payload | crc32(4)
 * CRC32 covers ver..payload.  Every response starts with a status byte.
 */
#include "ab_proto.h"
#include "ab_config.h"
#include "ab_flash.h"
#include "ab_image.h"
#include "ab_meta.h"
#include "ab_sig.h"
#include "ab_uart.h"
#include "ab_util.h"
#include "stm32f4xx.h"
#include <string.h>

#define AB_BOOT_VERSION   0x01010000u
#define AB_IDLE_RESET_MS  600000u
#define AB_DL_BUF         256u

typedef struct {
    uint8_t  type;
    uint16_t seq;
    uint16_t len;
    uint8_t  payload[AB_MAX_PAYLOAD];
} ab_frame_t;

typedef struct {
    uint8_t  active;
    uint8_t  slot;
    uint32_t version;
    uint32_t size;
    uint32_t written;
    uint32_t addr;
    uint32_t crc_run;
    ab_sha256_ctx sha;
    uint8_t  buf[AB_DL_BUF];
    uint32_t buf_len;
} dl_ctx_t;

static dl_ctx_t s_dl;

/* --------------------------------------------------------------- helpers */
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void send_frame(uint8_t type, uint16_t seq, const void *payload, uint16_t len)
{
    uint8_t hdr[8];
    uint8_t tail[4];
    uint32_t crc;

    if (len > AB_MAX_PAYLOAD) {
        return;
    }
    hdr[0] = AB_SYNC0;
    hdr[1] = AB_SYNC1;
    hdr[2] = AB_PROTO_VERSION;
    hdr[3] = type;
    hdr[4] = (uint8_t)seq;
    hdr[5] = (uint8_t)(seq >> 8);
    hdr[6] = (uint8_t)len;
    hdr[7] = (uint8_t)(len >> 8);

    crc = ab_crc32_start();
    crc = ab_crc32_update(crc, &hdr[2], 6u);
    if (len != 0u) {
        crc = ab_crc32_update(crc, payload, len);
    }
    crc = ab_crc32_final(crc);
    wr32(tail, crc);

    ab_uart_write(hdr, 8u);
    if (len != 0u) {
        ab_uart_write(payload, len);
    }
    ab_uart_write(tail, 4u);
}

static void reply(const ab_frame_t *req, const uint8_t *payload, uint16_t len)
{
    send_frame((uint8_t)(req->type | AB_RSP_BIT), req->seq, payload, len);
}

static void reply_status(const ab_frame_t *req, uint8_t status)
{
    reply(req, &status, 1u);
}

static int recv_frame(ab_frame_t *f, uint32_t timeout_ms)
{
    uint8_t c;
    uint8_t hdr[6];
    uint8_t crc_buf[4];
    uint32_t crc;
    uint16_t i;

    /* Sync: 0xAA 0x55. */
    for (;;) {
        if (ab_uart_getc(&c, timeout_ms) != 0) {
            return 0;
        }
        if (c != AB_SYNC0) {
            continue;
        }
        if (ab_uart_getc(&c, 50u) != 0) {
            return 0;
        }
        if (c == AB_SYNC1) {
            break;
        }
        if (c != AB_SYNC0) {
            return -1;
        }
    }

    for (i = 0; i < 6u; i++) {
        if (ab_uart_getc(&hdr[i], 200u) != 0) {
            return -1;
        }
    }
    if (hdr[0] != AB_PROTO_VERSION) {
        return -1;
    }
    f->type = hdr[1];
    f->seq = (uint16_t)(hdr[2] | ((uint16_t)hdr[3] << 8));
    f->len = (uint16_t)(hdr[4] | ((uint16_t)hdr[5] << 8));
    if (f->len > AB_MAX_PAYLOAD) {
        return -1;
    }
    for (i = 0; i < f->len; i++) {
        if (ab_uart_getc(&f->payload[i], 200u) != 0) {
            return -1;
        }
    }
    for (i = 0; i < 4u; i++) {
        if (ab_uart_getc(&crc_buf[i], 200u) != 0) {
            return -1;
        }
    }

    crc = ab_crc32_start();
    crc = ab_crc32_update(crc, hdr, 6u);
    crc = ab_crc32_update(crc, f->payload, f->len);
    crc = ab_crc32_final(crc);
    if (crc != rd32(crc_buf)) {
        return -1;
    }
    return 1;
}

/* ------------------------------------------------------------- download */
static int dl_flush(int pad)
{
    uint32_t n = s_dl.buf_len;

    if (n == 0u) {
        return 0;
    }
    if (pad) {
        while ((n & 3u) != 0u) {
            s_dl.buf[n++] = 0xFFu;
        }
    }
    if (ab_flash_program(s_dl.addr, s_dl.buf, n) != AB_FLASH_OK) {
        return -1;
    }
    s_dl.addr += n;
    s_dl.buf_len = 0u;
    return 0;
}

static int dl_feed(const uint8_t *p, uint32_t n)
{
    while (n-- != 0u) {
        s_dl.buf[s_dl.buf_len++] = *p++;
        if (s_dl.buf_len == AB_DL_BUF) {
            if (dl_flush(0) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

/* --------------------------------------------------------------- commands */
static void cmd_hello(const ab_frame_t *f)
{
    uint8_t out[18];
    ab_meta_rec_t latest;
    uint8_t active = AB_SLOT_A;
    uint8_t state = AB_ST_CONFIRMED;

    if (ab_meta_load(&latest)) {
        active = latest.slot;
        state = latest.state;
    }

    memset(out, 0, sizeof(out));
    out[0] = AB_OK;
    out[1] = (uint8_t)AB_PROTO_VERSION;
    wr32(&out[2], AB_BOOT_VERSION);
    out[6] = active;
    out[7] = state;
    out[8] = (uint8_t)AB_MAX_ATTEMPTS;
    out[9] = 0u;
    wr32(&out[10], AB_IMAGE_MAX);
    wr32(&out[14], 0u);
    reply(f, out, 18u);
}

static void cmd_start(const ab_frame_t *f)
{
    uint8_t out[8];
    uint8_t slot;
    uint32_t version;
    uint32_t size;
    uint8_t active = AB_SLOT_A;
    ab_meta_rec_t latest;
    ab_meta_rec_t bad;

    if (f->len < (48u + AB_SIG_BYTES)) {
        reply_status(f, AB_ERR_LEN);
        return;
    }
    if (ab_meta_load(&latest)) {
        active = latest.slot;
    }

    slot = f->payload[0];
    version = rd32(&f->payload[4]);
    size = rd32(&f->payload[8]);

    if (slot == AB_SLOT_NONE) {
        slot = ab_slot_other(active);
    }
    if (slot > AB_SLOT_B) {
        reply_status(f, AB_ERR_STATE);
        return;
    }
    if (size < 8u || size > AB_IMAGE_MAX || size > ab_slot_size(slot)) {
        reply_status(f, AB_ERR_SIZE);
        return;
    }
    ab_uart_write_str("start\r\n");
    if (version == 0u) {
        reply_status(f, AB_ERR_VERSION);
        return;
    }
    if (version < ab_meta_min_version()) {
        reply_status(f, AB_ERR_ROLLBACK);
        return;
    }
    if (slot == active) {
        reply_status(f, AB_ERR_STATE);
        return;
    }
    /* Signature covers the SHA-256 digest declared in this frame.  The
     * digest itself is re-checked against the written image in cmd_end(). */
    if (!ab_sig_verify(&f->payload[4], 44u, &f->payload[48])) {
        ab_uart_write_str("sig bad\r\n");
        reply_status(f, AB_ERR_SIGNATURE);
        return;
    }
    ab_uart_write_str("sig ok\r\n");

    /* Invalidate the target slot before erasing it.  If power is lost between
     * here and END, the log says BAD and the next boot rolls back instead of
     * trusting a slot that no longer holds a complete image. */
    memset(&bad, 0, sizeof(bad));
    bad.slot = slot;
    bad.state = AB_ST_BAD;
    bad.version = 0u;
    bad.min_version = ab_meta_min_version();
    bad.sec_flags = AB_SEC_SIGNED;
    if (ab_meta_append(&bad, 1) != 0) {
        reply_status(f, AB_ERR_FLASH);
        return;
    }

    if (ab_flash_erase_range(ab_slot_base(slot), size) != AB_FLASH_OK) {
        ab_uart_write_str("erase fail\r\n");
        reply_status(f, AB_ERR_FLASH);
        return;
    }
    ab_uart_write_str("erased\r\n");

    memset(&s_dl, 0, sizeof(s_dl));
    s_dl.active = 1u;
    s_dl.slot = slot;
    s_dl.version = version;
    s_dl.size = size;
    s_dl.addr = ab_slot_base(slot);
    s_dl.crc_run = ab_crc32_start();
    ab_sha256_init(&s_dl.sha);

    memset(out, 0, sizeof(out));
    out[0] = AB_OK;
    out[1] = slot;
    wr32(&out[2], 0u);
    reply(f, out, 6u);
}

static void cmd_data(const ab_frame_t *f)
{
    uint8_t out[6];
    uint32_t offset;
    uint32_t n;

    if (f->len < 8u) {
        reply_status(f, AB_ERR_LEN);
        return;
    }
    if (s_dl.active == 0u) {
        reply_status(f, AB_ERR_STATE);
        return;
    }

    offset = rd32(&f->payload[0]);
    n = (uint32_t)f->len - 4u;

    if (offset != s_dl.written || (s_dl.written + n) > s_dl.size) {
        reply_status(f, AB_ERR_STATE);
        return;
    }

    s_dl.crc_run = ab_crc32_update(s_dl.crc_run, &f->payload[4], n);
    ab_sha256_update(&s_dl.sha, &f->payload[4], n);

    if (dl_feed(&f->payload[4], n) != 0) {
        s_dl.active = 0u;
        reply_status(f, AB_ERR_FLASH);
        return;
    }
    s_dl.written += n;

    memset(out, 0, sizeof(out));
    out[0] = AB_OK;
    wr32(&out[1], s_dl.written);
    reply(f, out, 6u);
}

static void cmd_end(const ab_frame_t *f)
{
    uint8_t out[8];
    uint8_t sha[32];
    uint8_t status = AB_OK;
    ab_meta_rec_t rec;
    uint32_t crc_expect;

    if (f->len < 36u) {
        s_dl.active = 0u;
        reply_status(f, AB_ERR_LEN);
        return;
    }
    if (s_dl.active == 0u) {
        reply_status(f, AB_ERR_STATE);
        return;
    }
    if (s_dl.written != s_dl.size) {
        s_dl.active = 0u;
        reply_status(f, AB_ERR_SIZE);
        return;
    }
    if (dl_flush(1) != 0) {
        s_dl.active = 0u;
        reply_status(f, AB_ERR_FLASH);
        return;
    }
    crc_expect = rd32(&f->payload[0]);
    if (ab_crc32_final(s_dl.crc_run) != crc_expect) {
        status = AB_ERR_CRC;
    }
    ab_sha256_final(&s_dl.sha, sha);
    if (status == AB_OK && memcmp(sha, &f->payload[4], 32u) != 0) {
        status = AB_ERR_VERIFY;
    }
    if (status == AB_OK && !ab_image_vectors_ok(s_dl.slot)) {
        status = AB_ERR_VERIFY;
    }

    if (status == AB_OK) {
        memset(&rec, 0, sizeof(rec));
        rec.slot = s_dl.slot;
        rec.state = AB_ST_TRIAL;
        rec.version = s_dl.version;
        rec.img_size = s_dl.size;
        rec.img_crc32 = crc_expect;
        memcpy(rec.sha256, sha, 32u);
        rec.attempts = 0u;
        rec.min_version = ab_meta_min_version();
        rec.sec_flags = AB_SEC_SIGNED;
        if (ab_meta_append(&rec, 1) != 0) {
            status = AB_ERR_FLASH;
        }
    }

    s_dl.active = 0u;

    memset(out, 0, sizeof(out));
    out[0] = status;
    out[1] = s_dl.slot;
    wr32(&out[2], s_dl.version);
    reply(f, out, 6u);
}

static void cmd_status(const ab_frame_t *f)
{
    uint8_t out[4 + 12 * 2];
    ab_meta_rec_t latest;
    ab_meta_rec_t rec;
    uint8_t active = AB_SLOT_A;
    uint8_t state = AB_ST_NONE;
    uint8_t count = 0u;
    uint8_t slot;

    memset(out, 0, sizeof(out));
    if (ab_meta_load(&latest)) {
        active = latest.slot;
        state = latest.state;
    }
    out[0] = AB_OK;
    out[1] = active;
    out[2] = state;

    for (slot = 0u; slot <= 1u; slot++) {
        uint8_t *p = &out[4u + slot * 12u];
        if (ab_meta_find(slot, &rec)) {
            p[0] = 1u;
            p[1] = rec.state;
            p[2] = (uint8_t)(rec.attempts > 255u ? 255u : rec.attempts);
            p[3] = 0u;
            wr32(&p[4], rec.version);
            wr32(&p[8], rec.img_size);
            count++;
        }
    }
    out[3] = count;

    reply(f, out, (uint16_t)sizeof(out));
}

static void cmd_confirm(const ab_frame_t *f)
{
    ab_meta_rec_t latest;

    if (!ab_meta_load(&latest) || latest.state != AB_ST_TRIAL) {
        reply_status(f, AB_ERR_STATE);
        return;
    }
    latest.state = AB_ST_CONFIRMED;
    latest.attempts = 0u;
    latest.flags = 0u;
    if (latest.version > latest.min_version) {
        latest.min_version = latest.version;   /* raise the anti-rollback floor */
    }
    latest.sec_flags |= AB_SEC_SIGNED;
    if (ab_meta_append(&latest, 1) != 0) {
        reply_status(f, AB_ERR_FLASH);
        return;
    }
    reply_status(f, AB_OK);
}

static void cmd_info(const ab_frame_t *f)
{
    uint8_t out[24];
    ab_meta_rec_t latest;
    uint8_t active = AB_SLOT_A;
    uint8_t state = AB_ST_NONE;
    uint32_t version = 0u;
    uint32_t size = 0u;
    uint16_t attempts = 0u;

    if (ab_meta_load(&latest)) {
        active = latest.slot;
        state = latest.state;
        version = latest.version;
        size = latest.img_size;
        attempts = latest.attempts;
    }

    memset(out, 0, sizeof(out));
    out[0] = AB_OK;
    out[1] = (uint8_t)AB_PROTO_VERSION;
    wr32(&out[2], AB_BOOT_VERSION);
    out[6] = active;
    out[7] = state;
    wr32(&out[8], version);
    wr32(&out[12], ab_meta_min_version());
    wr32(&out[16], size);
    out[20] = (uint8_t)(attempts & 0xFFu);
    out[21] = (uint8_t)(attempts >> 8);
    reply(f, out, 22u);
}

static void cmd_set_baud(const ab_frame_t *f)
{
    uint32_t baud;

    if (f->len < 4u) {
        reply_status(f, AB_ERR_LEN);
        return;
    }
    baud = rd32(&f->payload[0]);
    if (baud != 115200u && baud != 230400u && baud != 460800u && baud != 921600u) {
        reply_status(f, AB_ERR_LEN);
        return;
    }
    reply_status(f, AB_OK);
    ab_delay_ms(10u);
    ab_uart_init(baud);
    ab_uart_rx_flush();
}

static void cmd_reboot(const ab_frame_t *f)
{
    reply_status(f, AB_OK);
    ab_delay_ms(20u);
    NVIC_SystemReset();
}

static void dispatch(const ab_frame_t *f)
{
    switch (f->type) {
    case AB_CMD_HELLO:   cmd_hello(f);   break;
    case AB_CMD_START:   cmd_start(f);   break;
    case AB_CMD_DATA:    cmd_data(f);    break;
    case AB_CMD_END:     cmd_end(f);     break;
    case AB_CMD_STATUS:  cmd_status(f);  break;
    case AB_CMD_CONFIRM: cmd_confirm(f); break;
    case AB_CMD_INFO:    cmd_info(f);    break;
    case AB_CMD_SET_BAUD: cmd_set_baud(f); break;
    case AB_CMD_REBOOT:  cmd_reboot(f);  break;
    case AB_CMD_ABORT:
        s_dl.active = 0u;
        reply_status(f, AB_OK);
        break;
    default:
        reply_status(f, AB_ERR_STATE);
        break;
    }
}

/* ------------------------------------------------------------------ loops */
int ab_proto_listen(uint32_t window_ms)
{
    uint32_t start = ab_millis();

    for (;;) {
        uint32_t elapsed = ab_millis() - start;
        ab_wdg_feed();
        static ab_frame_t f;
        int rc;

        if (elapsed >= window_ms) {
            return 0;
        }
        rc = recv_frame(&f, window_ms - elapsed);
        if (rc == 0) {
            return 0;
        }
        if (rc < 0) {
            continue;
        }
        dispatch(&f);
        return 1;
    }
}

void ab_proto_run(void)
{
    uint32_t last = ab_millis();

    for (;;) {
        ab_wdg_feed();
        static ab_frame_t f;
        int rc = recv_frame(&f, 1000u);

        if (rc > 0) {
            dispatch(&f);
            last = ab_millis();
        } else if (s_dl.active == 0u && (ab_millis() - last) > AB_IDLE_RESET_MS) {
            NVIC_SystemReset();
        }
    }
}
