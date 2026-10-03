#include <string.h>

#define TCOM_INLINE __attribute__((always_inline))
#define TCOM_RGB_MASK 0x00ffffffu

enum {
    TCOM_LUMA_SMALL = 64,
    TCOM_INDEX = 192,
    TCOM_RUN_SHORT = 224,
    TCOM_RGB = 240,
    TCOM_RGBA = 241,
    TCOM_RUN_LEFT = 242,
    TCOM_RUN_ABOVE = 243,
    TCOM_LUMA_LARGE = 244
};

static TCOM_INLINE unsigned tcom_read_pixel(const unsigned char *bytes, unsigned channels) {
    unsigned pixel = channels < 4 ? ~TCOM_RGB_MASK : 0;

    for (unsigned channel = 0; channel < channels; channel++) {
        pixel |= (unsigned)bytes[channel] << (channel * 8);
    }

    return pixel;
}

static TCOM_INLINE void tcom_write_pixel(unsigned char *bytes, unsigned channels,
                                         unsigned pixel) {
    for (unsigned channel = 0; channel < channels; channel++) {
        bytes[channel] = (unsigned char)(pixel >> (channel * 8));
    }
}

static unsigned tcom_predict(unsigned left, unsigned above) {
    unsigned rgb = (left & above & TCOM_RGB_MASK) + ((left ^ above) & 0x00fefefeu) / 2;
    return rgb | (left & ~TCOM_RGB_MASK);
}

static TCOM_INLINE size_t tcom_process(const unsigned char *input, size_t input_size,
                                       unsigned char *output, size_t output_capacity,
                                       unsigned width, unsigned height, unsigned channels,
                                       int decode) {
    unsigned pixel_cache[32] = {0};
    unsigned previous_pixel = ~TCOM_RGB_MASK;
    size_t stream_offset = 16;
    size_t pixel_bytes = (size_t)width * height * channels;

    if (decode) {
        if (output_capacity < pixel_bytes) {
            return 0;
        }
    } else {
        if (input_size != pixel_bytes || output_capacity < 16) {
            return 0;
        }
        memcpy(output, "TCOM", 4);
        tcom_write_pixel(output + 4, 4, width);
        tcom_write_pixel(output + 8, 4, height);
        tcom_write_pixel(output + 12, 4, channels);
    }

    const unsigned char *pixels = decode ? output : input;

    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width;) {
            size_t pixel_offset = ((size_t)y * width + x) * channels;
            unsigned run_length = 0;
            unsigned pixel, opcode;

            if (decode) {
                if (stream_offset >= input_size) {
                    return 0;
                }
                opcode = input[stream_offset++];

                if ((opcode >= TCOM_RUN_SHORT && opcode < TCOM_RGB) ||
                    opcode == TCOM_RUN_LEFT || opcode == TCOM_RUN_ABOVE) {
                    if (opcode < TCOM_RGB) {
                        run_length = opcode - TCOM_RUN_SHORT + 1;
                    } else {
                        if (stream_offset == input_size) {
                            return 0;
                        }
                        run_length = input[stream_offset++] + 1;
                    }
                    if (run_length > width - x || (opcode == TCOM_RUN_ABOVE && y == 0)) {
                        return 0;
                    }
                } else if (opcode == TCOM_RGB || opcode == TCOM_RGBA) {
                    unsigned literal_channels = opcode == TCOM_RGB ? 3 : 4;
                    if (literal_channels > input_size - stream_offset) {
                        return 0;
                    }
                    pixel = tcom_read_pixel(input + stream_offset, literal_channels);
                    if (literal_channels == 3) {
                        pixel = (pixel & TCOM_RGB_MASK) | (previous_pixel & ~TCOM_RGB_MASK);
                    }
                    stream_offset += literal_channels;
                } else if (opcode >= TCOM_INDEX && opcode < TCOM_RUN_SHORT) {
                    pixel = pixel_cache[opcode - TCOM_INDEX];
                } else {
                    unsigned above =
                        y ? tcom_read_pixel(pixels + pixel_offset - width * channels,
                                            channels)
                          : ~TCOM_RGB_MASK;
                    unsigned prediction = x ? tcom_predict(previous_pixel, above) : above;
                    int red_delta, green_delta, blue_delta;

                    if (opcode < TCOM_LUMA_SMALL) {
                        red_delta = (int)(opcode & 3) - 2;
                        green_delta = (int)((opcode >> 2) & 3) - 2;
                        blue_delta = (int)(opcode >> 4) - 2;
                    } else if (opcode < TCOM_INDEX) {
                        if (stream_offset == input_size) {
                            return 0;
                        }
                        int packed =
                            (opcode - TCOM_LUMA_SMALL) << 8 | input[stream_offset++];
                        green_delta = (packed & 31) - 16;
                        red_delta = green_delta + ((packed >> 5) & 31) - 16;
                        blue_delta = green_delta + (packed >> 10) - 16;
                    } else {
                        if (opcode < TCOM_LUMA_LARGE || opcode >= TCOM_LUMA_LARGE + 4 ||
                            input_size - stream_offset < 2) {
                            return 0;
                        }
                        int packed = (opcode - TCOM_LUMA_LARGE) << 16 |
                                     input[stream_offset] | input[stream_offset + 1] << 8;
                        stream_offset += 2;
                        green_delta = (packed & 63) - 32;
                        red_delta = green_delta + ((packed >> 6) & 63) - 32;
                        blue_delta = green_delta + (packed >> 12) - 32;
                    }

                    pixel = ((prediction + red_delta) & 255) |
                            (((prediction >> 8) + green_delta) & 255) << 8 |
                            (((prediction >> 16) + blue_delta) & 255) << 16 |
                            (prediction & ~TCOM_RGB_MASK);
                }
            } else {
                pixel = tcom_read_pixel(input + pixel_offset, channels);
                unsigned cache_index = (pixel * 0x9e3779b1u) >> 27;
                unsigned above =
                    y ? tcom_read_pixel(pixels + pixel_offset - width * channels, channels)
                      : ~TCOM_RGB_MASK;

                if (pixel == previous_pixel || (y && pixel == above)) {
                    opcode = pixel == previous_pixel ? TCOM_RUN_LEFT : TCOM_RUN_ABOVE;
                    run_length = 1;

                    if (opcode == TCOM_RUN_LEFT) {
                        while (run_length < 256 && run_length < width - x &&
                               tcom_read_pixel(input + pixel_offset + run_length * channels,
                                               channels) == previous_pixel) {
                            run_length++;
                        }
                    } else {
                        while (run_length < 256 && run_length < width - x &&
                               tcom_read_pixel(input + pixel_offset + run_length * channels,
                                               channels) ==
                                   tcom_read_pixel(input + pixel_offset - width * channels +
                                                       run_length * channels,
                                                   channels)) {
                            run_length++;
                        }
                    }

                    if (output_capacity - stream_offset < 2) {
                        return 0;
                    }
                    if (opcode == TCOM_RUN_LEFT && run_length <= 16) {
                        output[stream_offset++] = TCOM_RUN_SHORT + run_length - 1;
                    } else {
                        output[stream_offset++] = opcode;
                        output[stream_offset++] = run_length - 1;
                    }
                } else {
                    if (output_capacity - stream_offset < 5) {
                        return 0;
                    }
                    if (pixel_cache[cache_index] == pixel) {
                        output[stream_offset++] = TCOM_INDEX + cache_index;
                    } else {
                        unsigned prediction =
                            x ? tcom_predict(previous_pixel, above) : above;
                        int red_delta = (signed char)(pixel - prediction);
                        int green_delta = (signed char)((pixel >> 8) - (prediction >> 8));
                        int blue_delta = (signed char)((pixel >> 16) - (prediction >> 16));
                        int same_alpha = (pixel ^ prediction) <= TCOM_RGB_MASK;

                        if (same_alpha && (unsigned)((red_delta + 2) | (green_delta + 2) |
                                                     (blue_delta + 2)) < 4) {
                            output[stream_offset++] = (red_delta + 2) |
                                                      (green_delta + 2) << 2 |
                                                      (blue_delta + 2) << 4;
                        } else if (same_alpha &&
                                   (unsigned)((green_delta + 16) |
                                              (red_delta - green_delta + 16) |
                                              (blue_delta - green_delta + 16)) < 32) {
                            unsigned packed = (green_delta + 16) |
                                              (red_delta - green_delta + 16) << 5 |
                                              (blue_delta - green_delta + 16) << 10;
                            output[stream_offset++] = TCOM_LUMA_SMALL + (packed >> 8);
                            output[stream_offset++] = packed;
                        } else if (same_alpha &&
                                   (unsigned)((green_delta + 32) |
                                              (red_delta - green_delta + 32) |
                                              (blue_delta - green_delta + 32)) < 64) {
                            unsigned packed = (green_delta + 32) |
                                              (red_delta - green_delta + 32) << 6 |
                                              (blue_delta - green_delta + 32) << 12;
                            output[stream_offset++] = TCOM_LUMA_LARGE + (packed >> 16);
                            output[stream_offset++] = packed;
                            output[stream_offset++] = packed >> 8;
                        } else {
                            unsigned literal_channels =
                                (pixel ^ previous_pixel) <= TCOM_RGB_MASK ? 3 : 4;
                            output[stream_offset++] =
                                literal_channels == 3 ? TCOM_RGB : TCOM_RGBA;
                            tcom_write_pixel(output + stream_offset, literal_channels,
                                             pixel);
                            stream_offset += literal_channels;
                        }
                    }
                }
            }

            if (run_length) {
                if (decode) {
                    if (opcode == TCOM_RUN_ABOVE) {
                        memcpy(output + pixel_offset,
                               output + pixel_offset - width * channels,
                               run_length * channels);
                    } else {
                        tcom_write_pixel(output + pixel_offset, channels, previous_pixel);
                        for (unsigned copied = 1; copied < run_length;) {
                            unsigned remaining = run_length - copied;
                            unsigned chunk = copied < remaining ? copied : remaining;
                            memcpy(output + pixel_offset + copied * channels,
                                   output + pixel_offset, chunk * channels);
                            copied += chunk;
                        }
                    }
                }
                previous_pixel = tcom_read_pixel(
                    pixels + pixel_offset + (run_length - 1) * channels, channels);
            } else {
                if (decode) {
                    tcom_write_pixel(output + pixel_offset, channels, pixel);
                }
                pixel_cache[(pixel * 0x9e3779b1u) >> 27] = pixel;
                previous_pixel = pixel;
                run_length = 1;
            }

            x += run_length;
        }
    }

    return decode ? (stream_offset == input_size ? pixel_bytes : 0) : stream_offset;
}

TCOM_INLINE size_t tcom(const unsigned char *input, size_t input_size,
                        unsigned char *output, size_t output_capacity,
                        unsigned dimensions[3], int decode) {
    if (!input || !dimensions) {
        return 0;
    }

    if (decode) {
        if (input_size < 16 || memcmp(input, "TCOM", 4)) {
            return 0;
        }
        dimensions[0] = tcom_read_pixel(input + 4, 4);
        dimensions[1] = tcom_read_pixel(input + 8, 4);
        dimensions[2] = tcom_read_pixel(input + 12, 4);
    }

    unsigned width = dimensions[0];
    unsigned height = dimensions[1];
    unsigned channels = dimensions[2];

    if (!width || width > 1000000 || !height || channels - 1 > 3 ||
        width > (size_t)-1 / height / channels) {
        return 0;
    }
    if (!output) {
        return decode ? (size_t)width * height * channels : 0;
    }

    if (channels == 3) {
        return tcom_process(input, input_size, output, output_capacity, width, height, 3,
                            decode);
    }
    if (channels == 4) {
        return tcom_process(input, input_size, output, output_capacity, width, height, 4,
                            decode);
    }
    return tcom_process(input, input_size, output, output_capacity, width, height, channels,
                        decode);
}

#undef TCOM_INLINE
#undef TCOM_RGB_MASK
