#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *data;
    size_t size;
    void *owner;
} FpngResult;

int fpng_bench_init(void);
FpngResult fpng_bench_encode(const void *raw, unsigned width, unsigned height, unsigned channels);
FpngResult fpng_bench_decode(const void *encoded, size_t size, unsigned width, unsigned height, unsigned channels);
void fpng_bench_free(void *owner);

#ifdef __cplusplus
}
#endif
