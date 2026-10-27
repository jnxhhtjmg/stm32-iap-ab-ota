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

#endif /* AB_PROTO_H */
