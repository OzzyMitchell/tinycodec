#include "fpng_adapter.h"
#include "fpng.h"
#include <memory>

using Bytes = std::vector<uint8_t>;

int fpng_bench_init(void) {
    fpng::fpng_init();
    return fpng::fpng_cpu_supports_sse41();
}

FpngResult fpng_bench_encode(const void *raw, unsigned width, unsigned height, unsigned channels) {
    auto out = std::make_unique<Bytes>();
    if (!fpng::fpng_encode_image_to_memory(raw, width, height, channels, *out, 0))
        return {};
    return {out->data(), out->size(), out.release()};
}

FpngResult fpng_bench_decode(const void *encoded, size_t size, unsigned width, unsigned height, unsigned channels) {
    if (size > UINT32_MAX)
        return {};
    auto out = std::make_unique<Bytes>();
    uint32_t actual_width = 0, actual_height = 0, actual_channels = 0;
    int status = fpng::fpng_decode_memory(encoded, static_cast<uint32_t>(size), *out,
                                         actual_width, actual_height, actual_channels, channels);
    if (status != fpng::FPNG_DECODE_SUCCESS || actual_width != width ||
        actual_height != height || actual_channels != channels)
        return {};
    return {out->data(), out->size(), out.release()};
}

void fpng_bench_free(void *owner) {
    delete static_cast<Bytes *>(owner);
}
