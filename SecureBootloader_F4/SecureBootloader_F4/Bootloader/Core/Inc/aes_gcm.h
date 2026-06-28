#ifndef AES_GCM_H
#define AES_GCM_H

#include <stdint.h>
#include <stddef.h>

// encrypt: fills tag with 16 byte auth tag, encrypts data in place
// decrypt: returns 0 if tag matches, -1 if not (data not touched if tag fails)
void aes_gcm_encrypt(const uint8_t *key,
                     const uint8_t *iv,
                     const uint8_t *aad, size_t aad_len,
                     uint8_t *data,      size_t data_len,
                     uint8_t *tag);

int  aes_gcm_decrypt(const uint8_t *key,
                     const uint8_t *iv,
                     const uint8_t *aad, size_t aad_len,
                     uint8_t *data,      size_t data_len,
                     const uint8_t *tag);

#endif
