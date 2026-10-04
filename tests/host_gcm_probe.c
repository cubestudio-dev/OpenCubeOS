#include <stdio.h>
#include "types.h"
#include "crypto_aead.h"
void *memset(void*,int,unsigned long);
void *memcpy(void*,const void*,unsigned long);
int   memcmp(const void*,const void*,unsigned long);
int main(void) {
    u8 key[16], nonce[12], tag[16];
    const char *kh = "feffe9928665731c6d6a8f9467308308";
    const char *nh = "cafebabefacedbaddecaf888";
    for (int i = 0; i < 16; i++) { unsigned v; sscanf(kh+2*i, "%2x", &v); key[i] = v; }
    for (int i = 0; i < 12; i++) { unsigned v; sscanf(nh+2*i, "%2x", &v); nonce[i] = v; }
    u8 pt[64], ct[64];
    const char *ph = "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
                     "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255";
    for (int i = 0; i < 64; i++) { unsigned v; sscanf(ph+2*i, "%2x", &v); pt[i] = v; }
    crypto_aes128_gcm_seal(key, nonce, NULL, 0, pt, 64, ct, tag);
    printf("tag: ");
    for (int i = 0; i < 16; i++) printf("%02x", tag[i]);
    printf("\nwant 4d5c2af327cd64a62cf35abd2ba6fab4\n");
    printf("ct : ");
    for (int i = 0; i < 64; i++) printf("%02x", ct[i]);
    printf("\nwant 42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985\n");
    /* chacha20 probe: RFC 8439 2.3.2 first block */
    u8 ck[32], cn[12], stream[64];
    const char *ckh = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    const char *cnh = "000000090000004a00000000";
    for (int i = 0; i < 32; i++) { unsigned v; sscanf(ckh+2*i, "%2x", &v); ck[i] = v; }
    for (int i = 0; i < 12; i++) { unsigned v; sscanf(cnh+2*i, "%2x", &v); cn[i] = v; }
    crypto_chacha20_xor(ck, cn, 1, stream, 64, stream);
    printf("cc20 keystream[0..15]: ");
    for (int i = 0; i < 16; i++) printf("%02x", stream[i]);
    printf("\nwant                 : 10f1e7e4d13b5915500fdd1fa32071c4\n");
    return 0;
}
/* appended probe: full block dump */
int probe2(void);
