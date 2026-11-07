#include <string.h>

#define TCOM_INLINE __attribute__((always_inline))

static TCOM_INLINE unsigned read_le(const unsigned char *bytes, unsigned count) {
    unsigned value = 0;
    memcpy(&value, bytes, count);
    if (!*(unsigned char *)&(unsigned){1})
        value = __builtin_bswap32(value);
    return value;
}

static TCOM_INLINE void write_le(unsigned char *bytes, unsigned count, unsigned value) {
    if (!*(unsigned char *)&(unsigned){1})
        value = __builtin_bswap32(value);
    memcpy(bytes, &value, count);
}

static __attribute__((noinline, cold)) __attribute__((preserve_most)) unsigned
read_tail(const unsigned char *bytes, unsigned count) {
    unsigned value = 0;
    while (count)
        value = value << 8 | bytes[--count];
    return value;
}

static TCOM_INLINE unsigned read_pixel(const unsigned char *bytes, unsigned count, size_t remaining,
                                       unsigned channel_mask) {
    if (remaining >= 4) {
        unsigned value;
        memcpy(&value, bytes, 4);
        if (!*(unsigned char *)&(unsigned){1})
            value = __builtin_bswap32(value);
        return value & channel_mask;
    }
    return read_tail(bytes, count);
}

static __attribute__((noinline, cold)) __attribute__((preserve_most)) void
write_tail(unsigned char *bytes, unsigned value, unsigned count) {
    while (count--)
        *bytes++ = value, value >>= 8;
}

static TCOM_INLINE void write_pixel(unsigned char *bytes, unsigned count, unsigned value,
                                    size_t remaining) {
    if (remaining >= 4) {
        if (!*(unsigned char *)&(unsigned){1})
            value = __builtin_bswap32(value);
        memcpy(bytes, &value, 4);
    } else
        write_tail(bytes, value, count);
}

static TCOM_INLINE size_t process_pixels(const unsigned char *input, size_t input_size_or_capacity,
                                         unsigned char *output, size_t row_bytes, size_t raw_size,
                                         unsigned channels, int decode, unsigned model,
                                         unsigned previous) {
    const unsigned char *input_end = input + (decode ? input_size_or_capacity : 0);
    unsigned char *output_end = output + (decode ? raw_size : input_size_or_capacity),
                  *output_begin = output;
    size_t remaining = raw_size, after_first_row = row_bytes ? raw_size - row_bytes : 0;
    unsigned channel_mask = ~0u >> ((4 - channels) * 8);
    if (decode)
        input += 16;
    else
        output += 16;
    const unsigned char *pixels = decode ? output : input;
    while (remaining) {
        unsigned count = 1, payload_size = 0, tag = 255, pixel, payload;
        unsigned above =
            remaining <= after_first_row
                ? read_pixel(pixels - row_bytes, channels, remaining + row_bytes, channel_mask)
                : previous;
        unsigned prediction = (previous | above) - ((previous ^ above) & 0xfefefefe) / 2 -
                              ((previous ^ above) & model * 128 & 256);
        if (decode) {
            if (input == input_end)
                return 0;
            tag = *input++;
            payload_size = tag & 3;
            pixel = tag & 128 ? above : previous;
            if (payload_size == 3 && tag < 255) {
                count = tag / 4 % 32 + 1;
                if (count * channels > remaining)
                    return 0;
            } else {
                payload_size = tag == 255 ? channels : payload_size;
                if (payload_size > (size_t)(input_end - input))
                    return 0;
                if (tag == 255)
                    pixel = read_pixel(input, channels, input_end - input, channel_mask);
                else {
                    unsigned bits =
                                 0x78461422u >> (8 * (payload_size ? payload_size + 1 : model & 1)),
                             green_bits = bits & 15, color_bits = bits >> 4 & 15;
                    unsigned packed = tag / 4 + (read_pixel(input, payload_size, input_end - input,
                                                            (1u << (8 * payload_size)) - 1)
                                                 << 6);
                    unsigned green_range = 1u << green_bits, color_range = 1u << color_bits,
                             green_delta = (packed & (green_range - 1)) + 256 - green_range / 2;
                    packed >>= green_bits;
                    unsigned red_delta = (packed & (color_range - 1)) - color_range / 2,
                             blue_delta = (packed >> color_bits) - color_range / 2;
                    pixel = (prediction & 0xff000000u) |
                            ((prediction + green_delta * 256) & 0xff00) |
                            (((prediction & 0xff00ff) + (green_delta + red_delta) +
                              (green_delta + blue_delta) * 65536) &
                             0xff00ff);
                }
                input += payload_size;
            }
            unsigned repeat = count;
            do {
                write_pixel(output, channels, pixel, output_end - output);
                output += channels;
            } while (--repeat);
        } else {
            pixel = payload = read_pixel(input, channels, remaining, channel_mask);
            if (pixel == previous || pixel == above) {
                tag = pixel != previous;
                while (count < 32 - tag && count * channels < remaining &&
                       read_pixel(input + count * channels, channels, remaining - count * channels,
                                  channel_mask) == pixel)
                    count++;
                tag = count * 4 - 1 + 128 * tag;
            } else {
                payload_size = channels;
                int green_delta = (signed char)((pixel >> (channels == 1 ? 0 : 8)) -
                                                (prediction >> (channels == 1 ? 0 : 8)));
                if (channels == 1 && green_delta < -8)
                    green_delta = -8;
                int red_delta = (signed char)(pixel - prediction - green_delta),
                    blue_delta = (signed char)(channels < 3 ? -green_delta * (model >> 2)
                                                            : pixel / 65536 - prediction / 65536 -
                                                                  green_delta);
                if (!((pixel ^ prediction) >> 24)) {
                    unsigned green_range = model & 1 ? 16 : 4, color_range = model & 1 ? 2 : 4,
                             green = green_delta + green_range / 2,
                             red = red_delta + color_range / 2, blue = blue_delta + color_range / 2;
                    if ((green | ((red | blue) << (model & 1 ? 3 : 0))) < green_range) {
                        tag = (green + (red + blue * color_range) * green_range) * 4;
                        payload_size = 0;
                    } else if (channels > 1) {
                        green = green_delta + 32;
                        red = red_delta + 8;
                        blue = blue_delta + 8;
                        if ((green | ((red | blue) << 2)) < 64) {
                            tag = green * 4 + 1;
                            payload = red | blue << 4;
                            payload_size = 1;
                        } else if (channels > 2) {
                            green = green_delta + 128;
                            red = red_delta + 64;
                            blue = blue_delta + 64;
                            if ((red | blue) < 128) {
                                tag = green * 4 + 2;
                                payload = green >> 6 | red << 2 | blue << 9;
                                payload_size = 2;
                            }
                        }
                    }
                }
            }
            if ((size_t)(output_end - output) <= payload_size)
                return 0;
            *output++ = tag;
            write_pixel(output, payload_size, payload, output_end - output);
            output += payload_size;
            input += count * channels;
        }
        previous = pixel;
        remaining -= count * channels;
        pixels += count * channels;
    }
    return (!decode || input == input_end) ? (size_t)(output - output_begin) : 0;
}

TCOM_INLINE size_t tcom(const unsigned char *input, size_t input_size, unsigned char *output,
                        size_t capacity, unsigned *dimensions, int decode) {
    if (!input || !dimensions)
        return 0;
    unsigned selected_model = 0;
    if (decode) {
        if (input_size < 16 || read_le(input, 4) != 0x4d4f4354 ||
            (selected_model = read_le(input + 12, 4)) >> 5 != 16)
            return 0;
        *dimensions = read_le(input + 4, 4);
        dimensions[1] = read_le(input + 8, 4);
        dimensions[2] = selected_model & 7;
        selected_model = selected_model / 8 & 3;
    }
    unsigned width = *dimensions, height = dimensions[1], channels = dimensions[2];
    size_t row_bytes, raw_size;
    if (channels - 1 > 3 || __builtin_mul_overflow((size_t)width, (size_t)channels, &row_bytes) ||
        __builtin_mul_overflow(row_bytes, (size_t)height, &raw_size) || !raw_size)
        return 0;
    if (!output || (decode ? capacity < raw_size : input_size != raw_size || capacity < 16))
        return (decode && !output) * raw_size;
    if (!decode) {
        selected_model = channels == 1;
        write_le(output, 4, 0x4d4f4354);
        write_le(output + 4, 4, width);
        write_le(output + 8, 4, height);
        write_le(output + 12, 4, 512 + channels + selected_model * 8);
        size_t sample_step = selected_model ? 0 : (raw_size / channels - 1) / 127 * channels;
        unsigned best_score = (unsigned)-1;
        for (unsigned trial_model = 0; sample_step && trial_model < 4; trial_model++) {
            unsigned score = 3 * !!trial_model;
            size_t sample_offset = 0;
            for (unsigned sample = 0; sample < 128; sample++, sample_offset += sample_step)
                score += (unsigned)process_pixels(
                    input + sample_offset, capacity, output,
                    sample_offset >= row_bytes ? row_bytes : 0, channels, channels, 0,
                    trial_model | 4,
                    sample_offset ? read_tail(input + sample_offset - channels, channels) : 0);
            if (score < best_score)
                best_score = score, selected_model = trial_model;
        }
        output[12] = channels + selected_model * 8;
    }
    if (decode) {
        if (channels == 1)
            return process_pixels(input, input_size, output, row_bytes, raw_size, 1, 1,
                                  selected_model, 0);
        if (channels == 3)
            return process_pixels(input, input_size, output, row_bytes, raw_size, 3, 1,
                                  selected_model, 0);
        if (channels == 4)
            return process_pixels(input, input_size, output, row_bytes, raw_size, 4, 1,
                                  selected_model, 0);
        return process_pixels(input, input_size, output, row_bytes, raw_size, 2, 1, selected_model,
                              0);
    }
    if (channels == 1)
        return process_pixels(input, capacity, output, row_bytes, raw_size, 1, 0, selected_model,
                              0);
    if (channels == 3)
        return process_pixels(input, capacity, output, row_bytes, raw_size, 3, 0, selected_model,
                              0);
    if (channels == 4)
        return process_pixels(input, capacity, output, row_bytes, raw_size, 4, 0, selected_model,
                              0);
    return process_pixels(input, capacity, output, row_bytes, raw_size, 2, 0, selected_model, 0);
}

#undef TCOM_INLINE
