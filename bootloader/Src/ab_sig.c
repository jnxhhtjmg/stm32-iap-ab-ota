/**
 * @file  ab_sig.c
 * @brief Thin wrapper around TweetNaCl's crypto_sign_open().
 */
#include "ab_sig.h"
#include "ab_config.h"
#include "tweetnacl.h"

#include <string.h>

/* TweetNaCl references randombytes from the keypair/sign paths.  The
 * bootloader only verifies, so a stub keeps the linker happy. */
void randombytes(unsigned char *p, unsigned long long n)
{
    (void)p;
    (void)n;
}

int ab_sig_verify(const uint8_t *msg, uint32_t len, const uint8_t *sig)
{
    unsigned char sm[AB_SIG_BYTES + 64u];
    /* TweetNaCl copies the whole signed blob (n bytes) into the output
     * buffer before verifying, so it must be at least n bytes. */
    unsigned char out[AB_SIG_BYTES + 64u];
    unsigned long long mlen = 0u;

    if (len > 64u) {
        return 0;
    }
    memcpy(sm, sig, AB_SIG_BYTES);
    memcpy(sm + AB_SIG_BYTES, msg, len);

    return crypto_sign_open(out, &mlen, sm,
                            (unsigned long long)(AB_SIG_BYTES + len),
                            ab_pubkey) == 0;
}
