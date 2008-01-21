#ifndef REPREPRO_SHA1_H
#define REPREPRO_SHA1_H

struct SHA1_Context {
    uint32_t state[5];
    uint64_t count;
    uint8_t  buffer[64];
};
#define SHA1_DIGEST_SIZE 20

void SHA1Init(/*@out@*/struct SHA1_Context *context);
void SHA1Update(struct SHA1_Context *context, const uint8_t *data, const size_t len);
void SHA1Final(struct SHA1_Context *context, /*@out@*/uint8_t digest[SHA1_DIGEST_SIZE]);

#endif
