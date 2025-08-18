// Minimal SHA1 implementation (compact) for bank file hashing.
// Public domain style (derivative of simplest SHA-1 refs). Not performance critical.
#ifndef MINIBAE_SHA1MINI_H
#define MINIBAE_SHA1MINI_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[5];
    uint64_t count; // bits processed
    unsigned char buffer[64];
} SHA1_CTX_MINI;

void sha1mini_init(SHA1_CTX_MINI *ctx);
void sha1mini_update(SHA1_CTX_MINI *ctx, const unsigned char *data, size_t len);
void sha1mini_final(unsigned char digest[20], SHA1_CTX_MINI *ctx);

static inline void sha1mini(const unsigned char *data, size_t len, unsigned char out[20]){
    SHA1_CTX_MINI c; sha1mini_init(&c); sha1mini_update(&c,data,len); sha1mini_final(out,&c);
}

int sha1mini_file(const char *path, unsigned char out[20]);
int sha1mini_file_hex(const char *path, char out_hex[41]); // out_hex gets NUL terminator

#endif // MINIBAE_SHA1MINI_H
