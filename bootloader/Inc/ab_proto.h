/**
 * @file  ab_proto.h
 * @brief Binary UART update protocol.
 */
#ifndef AB_PROTO_H
#define AB_PROTO_H

#include <stdint.h>

/* Handle one frame if it arrives inside the window.  Returns 1 when the
 * bootloader should stay in update mode (a HELLO was received). */
int ab_proto_listen(uint32_t window_ms);

/* Update loop; never returns in normal operation (resets on idle timeout). */
void ab_proto_run(void);

/* 1 while a START/DATA session is active (used to decide resume vs fallback). */
int ab_proto_download_active(void);

/* Stay in update mode for up to window_ms, dispatching HELLO/STATUS/etc.,
 * and enter the update loop if a START opens a download session. */
void ab_proto_resume_window(uint32_t window_ms);

#endif /* AB_PROTO_H */
