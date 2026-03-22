#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_DIGEST_SIZE 32

typedef struct {
  uint8_t data[64];
  uint32_t datalen;
  uint64_t bitlen;
  uint32_t state[8];
} SHA256_CTX;

void SHA256_Init(SHA256_CTX* ctx);
void SHA256_Update(SHA256_CTX* ctx, const void* data, size_t len);
void SHA256_Final(uint8_t digest[SHA256_DIGEST_SIZE], SHA256_CTX* ctx);

#ifdef __cplusplus
}
#endif

#endif
