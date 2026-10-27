#include <stdio.h>
#include <string.h>

#include "ab_sig.h"

static int read_all(const char *path, unsigned char *buf, int max)
{
    FILE *f = fopen(path, "rb");
    int n;
    if (!f) {
        return -1;
    }
    n = (int)fread(buf, 1, (size_t)max, f);
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    unsigned char digest[32];
    unsigned char sig[64];
    int nd, ns, ok;

    if (argc != 3) {
        fprintf(stderr, "usage: %s digest.bin sig.bin\n", argv[0]);
        return 2;
    }
    nd = read_all(argv[1], digest, 32);
    ns = read_all(argv[2], sig, 64);
    fprintf(stderr, "read %d/%d, digest[0]=%02X sig[0]=%02X\n", nd, ns, digest[0], sig[0]);
    ok = ab_sig_verify(digest, 32, sig);
    fprintf(stderr, "verify=%d\n", ok);
    return ok ? 0 : 1;
}
