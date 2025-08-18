#include "sha1mini.h"
#include <string.h>
#include <stdio.h>

// Rotate left
static inline uint32_t ROL(uint32_t v, unsigned b){ return (v<<b) | (v>>(32-b)); }

void sha1mini_init(SHA1_CTX_MINI *c){
    c->state[0]=0x67452301UL; c->state[1]=0xEFCDAB89UL; c->state[2]=0x98BADCFEUL; c->state[3]=0x10325476UL; c->state[4]=0xC3D2E1F0UL; c->count=0; }

static void sha1mini_transform(uint32_t st[5], const unsigned char block[64]){
    uint32_t w[80];
    for(int i=0;i<16;i++){ w[i] = (uint32_t)block[i*4+0]<<24 | (uint32_t)block[i*4+1]<<16 | (uint32_t)block[i*4+2]<<8 | (uint32_t)block[i*4+3]; }
    for(int i=16;i<80;i++){ w[i] = ROL(w[i-3]^w[i-8]^w[i-14]^w[i-16],1); }
    uint32_t a=st[0],b=st[1],c=st[2],d=st[3],e=st[4];
    for(int i=0;i<80;i++){
        uint32_t f,k;
        if(i<20){ f=(b & c) | ((~b)&d); k=0x5A827999; }
        else if(i<40){ f=b^c^d; k=0x6ED9EBA1; }
        else if(i<60){ f=(b & c) | (b & d) | (c & d); k=0x8F1BBCDC; }
        else { f=b^c^d; k=0xCA62C1D6; }
        uint32_t temp = ROL(a,5) + f + e + k + w[i];
        e=d; d=c; c=ROL(b,30); b=a; a=temp;
    }
    st[0]+=a; st[1]+=b; st[2]+=c; st[3]+=d; st[4]+=e; }

void sha1mini_update(SHA1_CTX_MINI *c, const unsigned char *data, size_t len){
    size_t idx = (size_t)((c->count >> 3) & 63); c->count += (uint64_t)len << 3; size_t part = 64 - idx; size_t i=0;
    if(len >= part){ memcpy(&c->buffer[idx], data, part); sha1mini_transform(c->state, c->buffer); i=part; for(; i+63 < len; i+=64) sha1mini_transform(c->state, &data[i]); idx=0; }
    else { i=0; }
    memcpy(&c->buffer[idx], &data[i], len - i);
}

void sha1mini_final(unsigned char digest[20], SHA1_CTX_MINI *c){
    unsigned char pad[64]; pad[0]=0x80; memset(pad+1,0,63);
    unsigned char lenb[8]; for(int i=0;i<8;i++){ lenb[7-i] = (unsigned char)((c->count >> (i*8)) & 0xFF); }
    size_t idx = (size_t)((c->count >> 3) & 63); size_t padlen = (idx < 56)? (56-idx):(120-idx);
    sha1mini_update(c,pad,padlen); sha1mini_update(c,lenb,8);
    for(int i=0;i<5;i++){ digest[i*4+0]=(unsigned char)(c->state[i]>>24); digest[i*4+1]=(unsigned char)(c->state[i]>>16); digest[i*4+2]=(unsigned char)(c->state[i]>>8); digest[i*4+3]=(unsigned char)(c->state[i]); }
}

int sha1mini_file(const char *path, unsigned char out[20]){
    FILE *f = fopen(path,"rb"); if(!f) return 0; unsigned char buf[4096]; size_t n; SHA1_CTX_MINI c; sha1mini_init(&c);
    while((n=fread(buf,1,sizeof(buf),f))>0){ sha1mini_update(&c,buf,n);} fclose(f); sha1mini_final(out,&c); return 1; }

static void to_hex(const unsigned char in[20], char out[41]){
    static const char* h="0123456789abcdef"; for(int i=0;i<20;i++){ out[i*2]=h[in[i]>>4]; out[i*2+1]=h[in[i]&15]; } out[40]='\0'; }

int sha1mini_file_hex(const char *path, char out_hex[41]){ unsigned char d[20]; if(!sha1mini_file(path,d)) return 0; to_hex(d,out_hex); return 1; }
