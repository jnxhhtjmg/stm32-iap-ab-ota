/**
 * @file  ab_sig.h
 * @brief Ed25519 verification of the image digest.
 */
#ifndef AB_SIG_H
#define AB_SIG_H

#include <stdint.h>

/* Returns 1 when `sig` is a valid Ed25519 signature of `msg` by ab_pubkey. */
int ab_sig_verify(const uint8_t *msg, uint32_t len, const uint8_t *sig);

#endif /* AB_SIG_H */
