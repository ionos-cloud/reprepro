#ifndef REPREPRO_SHA256_H
#define REPREPRO_SHA256_H

/* Structure to save state of computation between the single steps.  */
struct SHA256_Context
{
  uint32_t H[8];

  uint64_t total;
  uint32_t buflen;
  char buffer[128]; /* NB: always correctly aligned for uint32_t.  */
};

#define SHA256_DIGEST_SIZE 32

void SHA256Init(/*@out@*/struct SHA256_Context *context);
void SHA256Update(struct SHA256_Context *context, const uint8_t *data, size_t len);
void SHA256Final(struct SHA256_Context *context, /*@out@*/uint8_t digest[SHA256_DIGEST_SIZE]);

#endif
