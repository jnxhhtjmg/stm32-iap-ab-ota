#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tweetnacl.h"

void randombytes(unsigned char *p, unsigned long long n)
{
    memset(p, 0, (size_t)n);
}

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
    unsigned char sm[1024];
    unsigned char pk[32];
    unsigned char msg[1024];
    unsigned long long mlen = 0;
    int n;

    if (argc != 3) {
        fprintf(stderr, "usage: %s signed.bin public_key.bin\n", argv[0]);
        return 2;
    }
    n = read_all(argv[1], sm, (int)sizeof(sm));
    if (n <= 64) {
        return 2;
    }
    if (read_all(argv[2], pk, 32) != 32) {
        return 2;
    }

    if (crypto_sign_open(msg, &mlen, sm, (unsigned long long)n, pk) != 0) {
        printf("FAIL\n");
        return 1;
    }
    fwrite(msg, 1, (size_t)mlen, stdout);
    return 0;
}
