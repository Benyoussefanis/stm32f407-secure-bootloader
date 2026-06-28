#include "aes_gcm.h"
#include "aes.h"
#include <string.h>

// this is the GCM multiplication step in GF(2^128)
// needed to compute the authentication tag
static void gcm_mul(uint8_t *X, const uint8_t *Y)
{
    uint8_t Z[16] = {0};
    uint8_t V[16];
    memcpy(V, Y, 16);

    for (int i = 0; i < 128; i++) {
        if (X[i / 8] & (0x80 >> (i % 8))) {
            for (int j = 0; j < 16; j++) Z[j] ^= V[j];
        }
        uint8_t lsb = V[15] & 1;
        for (int j = 15; j > 0; j--)
            V[j] = (V[j] >> 1) | (V[j-1] << 7);
        V[0] >>= 1;
        if (lsb) V[0] ^= 0xE1;  // 0xE1 is the reduction constant from the spec
    }
    memcpy(X, Z, 16);
}

// runs ghash over the ciphertext to produce the auth tag
static void ghash(const uint8_t *H,
                  const uint8_t *aad,  size_t aad_len,
                  const uint8_t *data, size_t data_len,
                  uint8_t *tag)
{
    uint8_t S[16] = {0};
    uint8_t block[16];

    size_t i = 0;
    while (i < aad_len) {
        size_t n = (aad_len - i) < 16 ? (aad_len - i) : 16;
        memset(block, 0, 16);
        memcpy(block, aad + i, n);
        for (int j = 0; j < 16; j++) S[j] ^= block[j];
        gcm_mul(S, H);
        i += n;
    }

    i = 0;
    while (i < data_len) {
        size_t n = (data_len - i) < 16 ? (data_len - i) : 16;
        memset(block, 0, 16);
        memcpy(block, data + i, n);
        for (int j = 0; j < 16; j++) S[j] ^= block[j];
        gcm_mul(S, H);
        i += n;
    }

    // last block has the lengths in bits
    uint64_t aad_bits  = (uint64_t)aad_len  * 8;
    uint64_t data_bits = (uint64_t)data_len * 8;
    block[0]  = (aad_bits  >> 56) & 0xFF;
    block[1]  = (aad_bits  >> 48) & 0xFF;
    block[2]  = (aad_bits  >> 40) & 0xFF;
    block[3]  = (aad_bits  >> 32) & 0xFF;
    block[4]  = (aad_bits  >> 24) & 0xFF;
    block[5]  = (aad_bits  >> 16) & 0xFF;
    block[6]  = (aad_bits  >>  8) & 0xFF;
    block[7]  = (aad_bits       ) & 0xFF;
    block[8]  = (data_bits >> 56) & 0xFF;
    block[9]  = (data_bits >> 48) & 0xFF;
    block[10] = (data_bits >> 40) & 0xFF;
    block[11] = (data_bits >> 32) & 0xFF;
    block[12] = (data_bits >> 24) & 0xFF;
    block[13] = (data_bits >> 16) & 0xFF;
    block[14] = (data_bits >>  8) & 0xFF;
    block[15] = (data_bits      ) & 0xFF;
    for (int j = 0; j < 16; j++) S[j] ^= block[j];
    gcm_mul(S, H);

    memcpy(tag, S, 16);
}

// builds the CTR block from IV + counter number
static void build_ctr(const uint8_t *iv, uint32_t counter, uint8_t *block)
{
    memcpy(block, iv, 12);
    block[12] = (counter >> 24) & 0xFF;
    block[13] = (counter >> 16) & 0xFF;
    block[14] = (counter >>  8) & 0xFF;
    block[15] = (counter      ) & 0xFF;
}

void aes_gcm_encrypt(const uint8_t *key,
                     const uint8_t *iv,
                     const uint8_t *aad, size_t aad_len,
                     uint8_t *data,      size_t data_len,
                     uint8_t *tag)
{
    struct AES_ctx ctx;
    uint8_t H[16] = {0};
    uint8_t ctr_block[16];
    uint8_t S0[16];

    // encrypt all-zeros to get the GHASH key H
    AES_init_ctx(&ctx, key);
    AES_ECB_encrypt(&ctx, H);

    // S0 is used to mask the tag at the end
    build_ctr(iv, 1, ctr_block);
    AES_init_ctx(&ctx, key);
    AES_ECB_encrypt(&ctx, ctr_block);
    memcpy(S0, ctr_block, 16);

    // actual encryption starts at counter 2
    build_ctr(iv, 2, ctr_block);
    AES_init_ctx_iv(&ctx, key, ctr_block);
    AES_CTR_xcrypt_buffer(&ctx, data, data_len);

    uint8_t ghash_out[16];
    ghash(H, aad, aad_len, data, data_len, ghash_out);

    for (int i = 0; i < 16; i++)
        tag[i] = ghash_out[i] ^ S0[i];
}

int aes_gcm_decrypt(const uint8_t *key,
                    const uint8_t *iv,
                    const uint8_t *aad, size_t aad_len,
                    uint8_t *data,      size_t data_len,
                    const uint8_t *tag)
{
    struct AES_ctx ctx;
    uint8_t H[16] = {0};
    uint8_t ctr_block[16];
    uint8_t S0[16];

    AES_init_ctx(&ctx, key);
    AES_ECB_encrypt(&ctx, H);

    build_ctr(iv, 1, ctr_block);
    AES_init_ctx(&ctx, key);
    AES_ECB_encrypt(&ctx, ctr_block);
    memcpy(S0, ctr_block, 16);

    // verify tag before doing anything else
    // if someone changed the data the tag wont match
    uint8_t ghash_out[16];
    ghash(H, aad, aad_len, data, data_len, ghash_out);
    uint8_t expected_tag[16];
    for (int i = 0; i < 16; i++)
        expected_tag[i] = ghash_out[i] ^ S0[i];

    // check all 16 bytes, dont stop early
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++)
        diff |= tag[i] ^ expected_tag[i];
    if (diff != 0) return -1;

    // tag is good, now decrypt
    build_ctr(iv, 2, ctr_block);
    AES_init_ctx_iv(&ctx, key, ctr_block);
    AES_CTR_xcrypt_buffer(&ctx, data, data_len);

    return 0;
}
