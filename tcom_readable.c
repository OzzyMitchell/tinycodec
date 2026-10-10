#include <string.h>

#define TCOM_INLINE __attribute__((always_inline))

static TCOM_INLINE unsigned read_le(const unsigned char *bytes, unsigned count) {
    unsigned value = 0;
    while (count)
        value = value << 8 | bytes[--count];
    return value;
}

static TCOM_INLINE void write_le(unsigned char *bytes, unsigned count, unsigned value) {
    while (count--) {
        *bytes++ = (unsigned char)value;
        value >>= 8;
    }
}

static TCOM_INLINE unsigned decode_residual(unsigned prediction, unsigned packed,
                                           unsigned green_radix, unsigned chroma_radix) {
    unsigned green_delta = packed % green_radix + 256 - green_radix / 2;
    packed /= green_radix;
    unsigned alpha = prediction & 0xff000000u;
    unsigned green = (prediction + green_delta * 256) & 0xff00u;
    unsigned red_blue = ((prediction & 0xff00ffu) + (green_delta - chroma_radix / 2) * 65537 +
                         packed % chroma_radix + packed / chroma_radix * 65536) &
                        0xff00ffu;
    return alpha | green | red_blue;
}

static TCOM_INLINE size_t process_pixels(const unsigned char *input, size_t input_size_or_samples,
                                        unsigned char *output, size_t capacity_or_stride,
                                        size_t row_bytes, size_t raw_size, unsigned channels,
                                        int decode, unsigned model) {
    size_t offset = 16;
    unsigned previous = 0;
    for (size_t position = 0; output ? position < raw_size : input_size_or_samples--;) {
        if (!output)
            previous = position ? read_le(input + position - channels, channels) : 0;
        unsigned count = 1, payload_size = 0, tag = 255, pixel, value;
        unsigned above = position >= row_bytes
                             ? read_le((decode ? output : input) + position - row_bytes, channels)
                             : previous;
        unsigned prediction = (previous | above) - ((previous ^ above) & 0xfefefefeu) / 2 -
                              ((previous ^ above) & model * 128 & 256);
        if (decode) {
            if (offset == input_size_or_samples)
                return 0;
            tag = input[offset++];
            payload_size = tag & 3;
            pixel = tag & 128 ? above : previous;
            if (payload_size == 3 && tag < 255) {
                count = tag / 4 % 32 + 1;
                if (count * channels > raw_size - position)
                    return 0;
            } else {
                payload_size = tag == 255 ? channels : payload_size;
                if (payload_size > input_size_or_samples - offset)
                    return 0;
                pixel = read_le(input + offset, payload_size);
                offset += payload_size;
                if (tag < 255) {
                    unsigned packed = tag / 4 + pixel * 64;
                    pixel = payload_size == 0
                                ? (model & 1 ? decode_residual(prediction, packed, 16, 2)
                                             : decode_residual(prediction, packed, 4, 4))
                            : payload_size == 1 ? decode_residual(prediction, packed, 64, 16)
                                                : decode_residual(prediction, packed, 256, 128);
                }
            }
            for (unsigned repeat = count; repeat--;)
                write_le(output + position + repeat * channels, channels, pixel);
        } else {
            pixel = value = read_le(input + position, channels);
            if (pixel == previous || pixel == above) {
                tag = pixel != previous;
                while (output && count < 32 - tag && count * channels < raw_size - position &&
                       read_le(input + position + count * channels, channels) == pixel)
                    count++;
                tag = count * 4 - 1 + 128 * tag;
            } else {
                payload_size = channels;
                int green_delta = (signed char)(pixel / 256 - prediction / 256);
                int red_delta = (signed char)(pixel - prediction - green_delta);
                int blue_delta = (signed char)(pixel / 65536 - prediction / 65536 - green_delta);
                for (unsigned kind = 0, green_radix = model & 1 ? 16 : 4,
                              chroma_radix = model & 1 ? 2 : 4;
                     !((pixel ^ prediction) >> 24) && kind < 3;
                     green_radix = 64 << (2 * kind), chroma_radix = 16 << (3 * kind), kind++) {
                    unsigned green = green_delta + green_radix / 2;
                    unsigned red = red_delta + chroma_radix / 2;
                    unsigned blue = blue_delta + chroma_radix / 2;
                    if (green < green_radix && (red | blue) < chroma_radix) {
                        value = green + (red + blue * chroma_radix) * green_radix;
                        tag = value * 4 + kind;
                        value >>= 6;
                        payload_size = kind;
                        break;
                    }
                }
            }
            if (output) {
                if (capacity_or_stride - offset <= payload_size)
                    return 0;
                output[offset] = (unsigned char)tag;
                write_le(output + offset + 1, payload_size, value);
            }
            offset += payload_size + 1;
        }
        previous = pixel;
        position += output ? count * channels : capacity_or_stride;
    }
    return decode ? offset == input_size_or_samples ? raw_size : 0 : offset;
}

TCOM_INLINE size_t tcom(const unsigned char *input, size_t input_size, unsigned char *output,
                      size_t capacity, unsigned dimensions[3], int decode) {
    if (!input || !dimensions)
        return 0;
    unsigned model = 0;
    if (decode) {
        if (input_size < 16 || memcmp(input, "TCOM", 4) ||
            ((model = read_le(input + 12, 4)) >> 5) != 16)
            return 0;
        dimensions[0] = read_le(input + 4, 4);
        dimensions[1] = read_le(input + 8, 4);
        dimensions[2] = model & 7;
        model = (model >> 3) & 3;
    }
    unsigned width = dimensions[0], height = dimensions[1], channels = dimensions[2];
    if (!width || !height || channels - 1 > 3 || width > (size_t)-1 / height / channels)
        return 0;
    size_t row_bytes = (size_t)width * channels;
    size_t raw_size = row_bytes * height;
    if (!output || (decode ? capacity < raw_size : input_size != raw_size || capacity < 16))
        return decode && !output ? raw_size : 0;
    if (!decode) {
        size_t best_size = (size_t)-1;
        size_t sample_step = (raw_size / channels - 1) / 127 * channels;
        for (unsigned candidate = 0; sample_step && candidate < 4; candidate++) {
            size_t cost = process_pixels(input, 128, NULL, sample_step, row_bytes, raw_size,
                                         channels, 0, candidate) +
                          3 * (candidate != 0);
            if (cost < best_size) {
                best_size = cost;
                model = candidate;
            }
        }
        memcpy(output, "TCOM", 4);
        write_le(output + 12, 4, 512 + channels + model * 8);
        write_le(output + 4, 4, width);
        write_le(output + 8, 4, height);
    }
    if (channels == 3)
        return process_pixels(input, input_size, output, capacity, row_bytes, raw_size, 3, decode,
                              model);
    if (channels == 4)
        return process_pixels(input, input_size, output, capacity, row_bytes, raw_size, 4, decode,
                              model);
    return process_pixels(input, input_size, output, capacity, row_bytes, raw_size, channels,
                          decode, model);
}

#undef TCOM_INLINE
