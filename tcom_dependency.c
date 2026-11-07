#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <wchar.h>
#include <errno.h>
#include <locale.h>
#include <ctype.h>
#include <limits.h>
#include <png.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 1000000
#include <stb/stb_image.h>

#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include <qoi/qoi.h>

#ifdef TCOM_READABLE
#include "tcom_readable.c"
#else
#include "tcom.c"
#define read_le G
#define write_le W
#endif

enum OutputFormat {
    OUTPUT_UNKNOWN,
    OUTPUT_PNG,
    OUTPUT_BMP,
    OUTPUT_PNM,
    OUTPUT_PGM,
    OUTPUT_PPM,
    OUTPUT_PBM,
    OUTPUT_PAM,
    OUTPUT_QOI,
};

static void usage(void) {
    puts("tcom pack input [output.tcom]\ntcom unpack input.tcom [output]");
}

static wchar_t *output_name(const wchar_t *input, int decode) {
    const wchar_t *base = input, *dot = NULL;
    for (const wchar_t *cursor = input; *cursor; ++cursor) {
        if (*cursor == L'/' || *cursor == L'\\') {
            base = cursor + 1;
            dot = NULL;
        } else if (*cursor == L'.' && cursor != base) {
            dot = cursor;
        }
    }
    size_t stem = dot ? (size_t)(dot - input) : wcslen(input);
    const wchar_t *suffix = decode ? L".unpacked.png" : L".tcom";
    size_t extra = wcslen(suffix) + 1;
    if (stem > SIZE_MAX / sizeof(wchar_t) - extra) {
        return NULL;
    }
    wchar_t *name = malloc((stem + extra) * sizeof *name);
    if (name) {
        wmemcpy(name, input, stem);
        wcscpy(name + stem, suffix);
    }
    return name;
}

static unsigned char *read_file(const wchar_t *path, size_t *size) {
    FILE *file = _wfopen(path, L"rb");
    if (!file) {
        _wperror(path);
        return NULL;
    }
    if (_fseeki64(file, 0, SEEK_END)) {
        fclose(file);
        return NULL;
    }
    __int64 length = _ftelli64(file);
    if (length < 0 || (uint64_t)length > SIZE_MAX || _fseeki64(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    *size = (size_t)length;
    unsigned char *data = malloc(*size ? *size : 1);
    int ok = data && fread(data, 1, *size, file) == *size && !ferror(file);
    if (fclose(file)) {
        ok = 0;
    }
    if (!ok) {
        free(data);
        return NULL;
    }
    return data;
}

static int reject_animation(png_structp png, png_unknown_chunkp chunk) {
    if (!memcmp(chunk->name, "acTL", 4)) {
        png_error(png, "Animated PNGs are not supported");
    }
    return 0;
}

static int read_png(const wchar_t *path, unsigned char **pixels, unsigned dimensions[3],
                    size_t *size) {
    FILE *file = _wfopen(path, L"rb");
    if (!file) {
        _wperror(path);
        return 0;
    }
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(file);
        return 0;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(file);
        return 0;
    }
    png_init_io(png, file);
    png_set_keep_unknown_chunks(png, PNG_HANDLE_CHUNK_ALWAYS, (png_const_bytep) "acTL", 1);
    png_set_read_user_chunk_fn(png, NULL, reject_animation);
    png_read_info(png, info);
    int color = png_get_color_type(png, info), depth = png_get_bit_depth(png, info);
    if (depth > 8) {
        png_error(png, "16-bit PNG is not supported");
    }
    if (color == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (color == PNG_COLOR_TYPE_GRAY && depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }
    int passes = png_set_interlace_handling(png);
    png_read_update_info(png, info);
    dimensions[0] = png_get_image_width(png, info);
    dimensions[1] = png_get_image_height(png, info);
    dimensions[2] = png_get_channels(png, info);
    if (!dimensions[0] || dimensions[0] > 1000000 || !dimensions[1] || dimensions[2] < 1 ||
        dimensions[2] > 4 || (size_t)dimensions[0] > SIZE_MAX / dimensions[1] / dimensions[2]) {
        png_error(png, "Unsupported image dimensions");
    }
    size_t stride = (size_t)dimensions[0] * dimensions[2];
    if (png_get_rowbytes(png, info) != stride) {
        png_error(png, "Unexpected pixel layout");
    }
    *size = stride * dimensions[1];
    *pixels = calloc(*size, 1);
    if (!*pixels) {
        png_error(png, "Not enough memory");
    }
    for (int pass = 0; pass < passes; ++pass) {
        for (unsigned y = 0; y < dimensions[1]; ++y) {
            png_read_row(png, *pixels + y * stride, NULL);
        }
    }
    png_read_end(png, info);
    png_destroy_read_struct(&png, &info, NULL);
    return fclose(file) == 0;
}

static int write_png(FILE *file, const unsigned char *pixels, const unsigned dimensions[3]) {
    const int colors[] = {PNG_COLOR_TYPE_GRAY, PNG_COLOR_TYPE_GRAY_ALPHA, PNG_COLOR_TYPE_RGB,
                          PNG_COLOR_TYPE_RGBA};
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info) {
        png_destroy_write_struct(&png, &info);
        return 0;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        return 0;
    }
    png_init_io(png, file);
    png_set_IHDR(png, info, dimensions[0], dimensions[1], 8, colors[dimensions[2] - 1],
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (unsigned y = 0; y < dimensions[1]; ++y) {
        png_write_row(png, (png_bytep)pixels + (size_t)y * dimensions[0] * dimensions[2]);
    }
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    return 1;
}

static size_t image_size(const unsigned dimensions[3]) {
    unsigned width = dimensions[0];
    unsigned height = dimensions[1];
    unsigned channels = dimensions[2];
    if (!width || width > 1000000 || !height || channels < 1 || channels > 4 ||
        (size_t)width > SIZE_MAX / height / channels) {
        return 0;
    }
    return (size_t)width * height * channels;
}

static void skip_pnm_space(const unsigned char **cursor, const unsigned char *end) {
    for (;;) {
        while (*cursor < end && isspace(**cursor)) {
            ++*cursor;
        }
        if (*cursor == end || **cursor != '#') {
            return;
        }
        while (*cursor < end && **cursor != '\r' && **cursor != '\n') {
            ++*cursor;
        }
    }
}

static int read_pnm_number(const unsigned char **cursor, const unsigned char *end,
                           unsigned *value) {
    skip_pnm_space(cursor, end);
    if (*cursor == end || !isdigit(**cursor)) {
        return 0;
    }
    unsigned number = 0;
    do {
        unsigned digit = *(*cursor)++ - '0';
        if (number > (UINT_MAX - digit) / 10) {
            return 0;
        }
        number = number * 10 + digit;
    } while (*cursor < end && isdigit(**cursor));
    if (*cursor < end && !isspace(**cursor) && **cursor != '#') {
        return 0;
    }
    *value = number;
    return 1;
}

static int read_pnm(const unsigned char *data, size_t length, unsigned char **pixels,
                    unsigned dimensions[3], size_t *size) {
    const unsigned char *cursor = data + 2, *end = data + length;
    unsigned max_value = 1;
    int pnm_type = data[1] - '0';
    if (pnm_type == 7) {
        unsigned seen_fields = 0;
        char tuple_type[64] = "";
        if (cursor == end || (*cursor != '\n' && *cursor != '\r')) {
            return 0;
        }
        while (cursor < end) {
            const unsigned char *next = memchr(cursor, '\n', (size_t)(end - cursor));
            if (!next || next - cursor >= 255) {
                return 0;
            }
            char line[256], key[32], value[128], extra;
            size_t line_size = (size_t)(next - cursor);
            memcpy(line, cursor, line_size);
            line[line_size] = 0;
            cursor = next + 1;
            char *field = line;
            while (isspace((unsigned char)*field)) {
                ++field;
            }
            if (!*field || *field == '#') {
                continue;
            }
            int fields = sscanf(field, "%31s %127s %c", key, value, &extra);
            if (!strcmp(key, "ENDHDR")) {
                if (fields != 1 || seen_fields != 15) {
                    return 0;
                }
                break;
            }
            if (fields != 2) {
                return 0;
            }
            if (!strcmp(key, "TUPLTYPE")) {
                if (*tuple_type || strlen(value) >= sizeof tuple_type) {
                    return 0;
                }
                strcpy(tuple_type, value);
                continue;
            }
            unsigned field_bit = 0, *destination = NULL;
            if (!strcmp(key, "WIDTH")) {
                field_bit = 1;
                destination = dimensions;
            } else if (!strcmp(key, "HEIGHT")) {
                field_bit = 2;
                destination = dimensions + 1;
            } else if (!strcmp(key, "DEPTH")) {
                field_bit = 4;
                destination = dimensions + 2;
            } else if (!strcmp(key, "MAXVAL")) {
                field_bit = 8;
                destination = &max_value;
            } else {
                return 0;
            }
            const unsigned char *a = (const unsigned char *)value, *b = a + strlen(value);
            if ((seen_fields & field_bit) || !read_pnm_number(&a, b, destination) || a != b) {
                return 0;
            }
            seen_fields |= field_bit;
        }
        const char *types[] = {"GRAYSCALE", "GRAYSCALE_ALPHA", "RGB", "RGB_ALPHA"};
        if (seen_fields != 15 || !image_size(dimensions) || (max_value != 255 && max_value != 1)) {
            return 0;
        }
        if (*tuple_type && strcmp(tuple_type, types[dimensions[2] - 1]) &&
            !(max_value == 1 && dimensions[2] <= 2 &&
              !strcmp(tuple_type, dimensions[2] == 1 ? "BLACKANDWHITE" : "BLACKANDWHITE_ALPHA"))) {
            return 0;
        }
    } else {
        if (!read_pnm_number(&cursor, end, dimensions) ||
            !read_pnm_number(&cursor, end, dimensions + 1)) {
            return 0;
        }
        dimensions[2] = (pnm_type == 3 || pnm_type == 6) ? 3 : 1;
        if (pnm_type != 1 && pnm_type != 4 &&
            (!read_pnm_number(&cursor, end, &max_value) || max_value != 255)) {
            return 0;
        }
    }
    *size = image_size(dimensions);
    if (!*size) {
        return 0;
    }
    size_t packed_row_bytes = ((size_t)dimensions[0] + 7) / 8;
    size_t raster_size = pnm_type == 4 ? packed_row_bytes * dimensions[1] : *size;
    if (pnm_type >= 4 && pnm_type != 7) {
        if (cursor < end && *cursor == '#') {
            while (cursor < end && *cursor != '\r' && *cursor != '\n') {
                ++cursor;
            }
        }
        if (cursor == end || !isspace(*cursor)) {
            return 0;
        }
        unsigned delimiter = *cursor++;
        if (delimiter == '\r' && cursor < end && *cursor == '\n' &&
            (size_t)(end - cursor) == raster_size + 1) {
            ++cursor;
        }
    }
    if (pnm_type >= 4 && (size_t)(end - cursor) != raster_size) {
        return 0;
    }
    *pixels = malloc(*size);
    if (!*pixels) {
        return 0;
    }
    for (size_t j = 0; j < *size; ++j) {
        unsigned sample;
        if (pnm_type == 1) {
            skip_pnm_space(&cursor, end);
            if (cursor == end || (*cursor != '0' && *cursor != '1')) {
                return 0;
            }
            sample = *cursor++ - '0';
        } else if (pnm_type < 4) {
            if (!read_pnm_number(&cursor, end, &sample) || sample > max_value) {
                return 0;
            }
        } else if (pnm_type == 4) {
            size_t row = j / dimensions[0];
            size_t column = j % dimensions[0];
            unsigned packed = cursor[row * packed_row_bytes + column / 8];
            sample = (packed >> (7 - column % 8)) & 1;
        } else {
            sample = *cursor++;
            if (sample > max_value) {
                return 0;
            }
        }
        if (pnm_type == 1 || pnm_type == 4) {
            sample = 255 - sample * 255;
        } else if (max_value == 1) {
            sample *= 255;
        }
        (*pixels)[j] = (unsigned char)sample;
    }
    if (pnm_type < 4) {
        skip_pnm_space(&cursor, end);
        if (cursor != end) {
            return 0;
        }
    }
    return 1;
}

static unsigned read_be32(const unsigned char *bytes) {
    return (unsigned)bytes[0] << 24 | (unsigned)bytes[1] << 16 | (unsigned)bytes[2] << 8 | bytes[3];
}

static int valid_qoi(const unsigned char *data, size_t length, unsigned dimensions[3]) {
    if (length < 22 || memcmp(data, "qoif", 4) || data[12] < 3 || data[12] > 4 || data[13] > 1) {
        return 0;
    }
    dimensions[0] = read_be32(data + 4);
    dimensions[1] = read_be32(data + 8);
    dimensions[2] = data[12];
    if (!image_size(dimensions) || memcmp(data + length - 8, "\0\0\0\0\0\0\0\1", 8)) {
        return 0;
    }
    size_t offset = 14, remaining_pixels = (size_t)dimensions[0] * dimensions[1];
    while (remaining_pixels) {
        if (offset >= length - 8) {
            return 0;
        }
        unsigned tag = data[offset++], payload_size = 0, run_length = 1;
        if (tag == 254) {
            payload_size = 3;
        } else if (tag == 255) {
            payload_size = 4;
        } else if ((tag & 192) == 128) {
            payload_size = 1;
        } else if ((tag & 192) == 192) {
            run_length = (tag & 63) + 1;
        }
        if (payload_size > length - 8 - offset || run_length > remaining_pixels) {
            return 0;
        }
        offset += payload_size;
        remaining_pixels -= run_length;
    }
    return offset == length - 8;
}

static int read_image(const wchar_t *path, unsigned char **pixels, unsigned dimensions[3],
                      size_t *size) {
    size_t length = 0;
    unsigned char *data = read_file(path, &length);
    if (!data) {
        return 0;
    }
    int valid = 0;
    if (length >= 8 && !memcmp(data, "\211PNG\r\n\032\n", 8)) {
        free(data);
        return read_png(path, pixels, dimensions, size);
    }
    if (length >= 3 && data[0] == 'P' && data[1] >= '1' && data[1] <= '7' && isspace(data[2])) {
        valid = read_pnm(data, length, pixels, dimensions, size);
    } else if (length >= 22 && !memcmp(data, "qoif", 4) && length <= INT_MAX &&
               valid_qoi(data, length, dimensions)) {
        qoi_desc description;
        *pixels = qoi_decode(data, (int)length, &description, 0);
        valid = *pixels != NULL;
    } else if (length >= 54 && data[0] == 'B' && data[1] == 'M' && length <= INT_MAX) {
        unsigned header_size = read_le(data + 14, 4), pixel_offset = read_le(data + 10, 4),
                 bits_per_pixel = data[28] | data[29] << 8, compression = read_le(data + 30, 4);
        if ((header_size != 40 && header_size != 108 && header_size != 124) ||
            (bits_per_pixel != 1 && bits_per_pixel != 4 && bits_per_pixel != 8 &&
             bits_per_pixel != 16 && bits_per_pixel != 24 && bits_per_pixel != 32) ||
            (compression != 0 &&
             (compression != 3 || (bits_per_pixel != 16 && bits_per_pixel != 32)))) {
            free(data);
            return 0;
        }
        int32_t signed_height = (int32_t)read_le(data + 22, 4);
        size_t width = read_le(data + 18, 4),
               height =
                   signed_height < 0 ? (size_t)(-(int64_t)signed_height) : (size_t)signed_height,
               stride = (width * bits_per_pixel + 31) / 32 * 4;
        unsigned pixel_start = 14 + header_size + (header_size == 40 && compression == 3 ? 12 : 0);
        if (width && width <= 1000000 && height && height <= 1000000 &&
            pixel_offset >= pixel_start && pixel_offset <= length &&
            height <= (length - pixel_offset) / stride) {
            if (bits_per_pixel < 16) {
                unsigned palette_size = (pixel_offset - pixel_start) / 4;
                if ((pixel_offset - pixel_start) % 4 || !palette_size ||
                    palette_size > (1u << bits_per_pixel)) {
                    free(data);
                    return 0;
                }
                for (size_t y = 0; y < height; ++y) {
                    for (size_t x = 0; x < width; ++x) {
                        if (((data[pixel_offset + y * stride + x * bits_per_pixel / 8] >>
                              (8 - bits_per_pixel - x * bits_per_pixel % 8)) &
                             ((1u << bits_per_pixel) - 1)) >= palette_size) {
                            free(data);
                            return 0;
                        }
                    }
                }
            } else if (pixel_offset != pixel_start) {
                memmove(data + pixel_start, data + pixel_offset, length - pixel_offset);
                length -= pixel_offset - pixel_start;
                write_le(data + 10, 4, pixel_start);
            }
            int x, y, channels;
            *pixels = stbi_load_from_memory(data, (int)length, &x, &y, &channels, 0);
            if (*pixels) {
                dimensions[0] = x;
                dimensions[1] = y;
                dimensions[2] = channels;
                valid = 1;
            }
        }
    }
    free(data);
    if (valid) {
        *size = image_size(dimensions);
        valid = *size != 0;
    }
    return valid;
}

static enum OutputFormat output_format(const wchar_t *path) {
    const wchar_t *dot = wcsrchr(path, L'.');
    if (!dot) {
        return OUTPUT_UNKNOWN;
    }
    const wchar_t *const extensions[] = {
        [OUTPUT_PNG] = L".png", [OUTPUT_BMP] = L".bmp", [OUTPUT_PNM] = L".pnm",
        [OUTPUT_PGM] = L".pgm", [OUTPUT_PPM] = L".ppm", [OUTPUT_PBM] = L".pbm",
        [OUTPUT_PAM] = L".pam", [OUTPUT_QOI] = L".qoi",
    };
    for (enum OutputFormat format = OUTPUT_PNG; format <= OUTPUT_QOI; ++format) {
        if (!_wcsicmp(dot, extensions[format])) {
            return format;
        }
    }
    return OUTPUT_UNKNOWN;
}

static int write_pnm(FILE *file, enum OutputFormat format, const unsigned char *pixels,
                     const unsigned dimensions[3]) {
    size_t size = image_size(dimensions);
    unsigned channels = dimensions[2];
    if (format == OUTPUT_PAM) {
        const char *types[] = {"GRAYSCALE", "GRAYSCALE_ALPHA", "RGB", "RGB_ALPHA"};
        fprintf(file, "P7\nWIDTH %u\nHEIGHT %u\nDEPTH %u\nMAXVAL 255\nTUPLTYPE %s\nENDHDR\n",
                dimensions[0], dimensions[1], channels, types[channels - 1]);
    } else if (format == OUTPUT_PBM) {
        if (channels != 1) {
            return 0;
        }
        for (size_t j = 0; j < size; ++j) {
            if (pixels[j] != 0 && pixels[j] != 255) {
                return 0;
            }
        }
        fprintf(file, "P4\n%u %u\n", dimensions[0], dimensions[1]);
        for (unsigned y = 0; y < dimensions[1]; ++y) {
            for (unsigned x = 0; x < dimensions[0]; x += 8) {
                unsigned packed = 0;
                for (unsigned k = 0; k < 8 && x + k < dimensions[0]; ++k) {
                    packed |= (pixels[(size_t)y * dimensions[0] + x + k] == 0) << (7 - k);
                }
                if (fputc((int)packed, file) == EOF) {
                    return 0;
                }
            }
        }
        return !ferror(file);
    } else {
        if ((channels != 1 && channels != 3) || (format == OUTPUT_PGM && channels != 1) ||
            (format == OUTPUT_PPM && channels != 3)) {
            return 0;
        }
        fprintf(file, "P%u\n%u %u\n255\n", channels == 1 ? 5 : 6, dimensions[0], dimensions[1]);
    }
    return fwrite(pixels, 1, size, file) == size && !ferror(file);
}

static int write_bmp(FILE *file, const unsigned char *pixels, const unsigned dimensions[3]) {
    unsigned channels = dimensions[2], header_size = channels == 4 ? 122 : 54;
    size_t stride = ((size_t)dimensions[0] * channels + 3) & ~(size_t)3;
    if (dimensions[1] > INT_MAX || stride > (UINT32_MAX - header_size) / dimensions[1]) {
        return 0;
    }
    unsigned char header[122] = {0};
    memcpy(header, "BM", 2);
    write_le(header + 2, 4, (unsigned)(header_size + stride * dimensions[1]));
    write_le(header + 10, 4, header_size);
    write_le(header + 14, 4, header_size - 14);
    write_le(header + 18, 4, dimensions[0]);
    write_le(header + 22, 4, dimensions[1]);
    write_le(header + 26, 2, 1);
    write_le(header + 28, 2, channels * 8);
    if (channels == 4) {
        write_le(header + 30, 4, 3);
        write_le(header + 54, 4, 0xff0000);
        write_le(header + 58, 4, 0xff00);
        write_le(header + 62, 4, 0xff);
        write_le(header + 66, 4, 0xff000000);
        write_le(header + 70, 4, 0x73524742);
    }
    unsigned char *row = calloc(stride, 1);
    if (!row) {
        return 0;
    }
    int written = fwrite(header, 1, header_size, file) == header_size;
    for (unsigned y = dimensions[1]; written && y-- > 0;) {
        const unsigned char *source_row = pixels + (size_t)y * dimensions[0] * channels;
        for (unsigned x = 0; x < dimensions[0]; ++x) {
            size_t j = (size_t)x * channels;
            row[j] = source_row[j + 2];
            row[j + 1] = source_row[j + 1];
            row[j + 2] = source_row[j];
            if (channels == 4) {
                row[j + 3] = source_row[j + 3];
            }
        }
        written = fwrite(row, 1, stride, file) == stride;
    }
    free(row);
    return written;
}

static int write_image(FILE *file, enum OutputFormat format, const unsigned char *pixels,
                       const unsigned source_dimensions[3]) {
    unsigned dimensions[3] = {source_dimensions[0], source_dimensions[1], source_dimensions[2]};
    if (format == OUTPUT_PNG) {
        return write_png(file, pixels, dimensions);
    }
    if (format >= OUTPUT_PNM && format <= OUTPUT_PAM) {
        return write_pnm(file, format, pixels, dimensions);
    }
    unsigned char *rgb = NULL;
    if (dimensions[2] < 3) {
        size_t count = (size_t)dimensions[0] * dimensions[1];
        unsigned rgb_channels = dimensions[2] + 2;
        if (count > SIZE_MAX / rgb_channels) {
            return 0;
        }
        rgb = malloc(count * rgb_channels);
        if (!rgb) {
            return 0;
        }
        for (size_t j = 0; j < count; ++j) {
            size_t offset = j * rgb_channels;
            unsigned char gray = pixels[j * dimensions[2]];
            rgb[offset] = gray;
            rgb[offset + 1] = gray;
            rgb[offset + 2] = gray;
            if (rgb_channels == 4) {
                rgb[offset + 3] = pixels[j * dimensions[2] + 1];
            }
        }
        pixels = rgb;
        dimensions[2] = rgb_channels;
    }
    int written = 0;
    if (format == OUTPUT_BMP) {
        written = write_bmp(file, pixels, dimensions);
    } else if (format == OUTPUT_QOI) {
        qoi_desc description = {dimensions[0], dimensions[1], (unsigned char)dimensions[2],
                                QOI_SRGB};
        int length = 0;
        void *encoded = qoi_encode(pixels, &description, &length);
        if (encoded) {
            written = fwrite(encoded, 1, (size_t)length, file) == (size_t)length;
            free(encoded);
        }
    }
    free(rgb);
    return written && !ferror(file);
}

int wmain(int argc, wchar_t **argv) {
    setlocale(LC_ALL, ".UTF8");
    if (argc == 1 || (argc == 2 && (!wcscmp(argv[1], L"--help") || !wcscmp(argv[1], L"-h") ||
                                    !wcscmp(argv[1], L"help")))) {
        usage();
        return 0;
    }
    int decode = !wcscmp(argv[1], L"unpack");
    if ((argc != 3 && argc != 4) || (!decode && wcscmp(argv[1], L"pack"))) {
        usage();
        return 1;
    }
    unsigned dimensions[3] = {0};
    unsigned char *input_data = NULL;
    unsigned char *result_data = NULL;
    wchar_t *default_output = argc == 3 ? output_name(argv[2], decode) : NULL;
    const wchar_t *output_path = argc == 4 ? argv[3] : default_output;
    const char *error = "Not enough memory";
    size_t input_size = 0;
    size_t capacity = 0;
    size_t result_size = 0;
    int result = 1;
    if (!output_path) {
        goto done;
    }
    if (decode) {
        error = "Could not read the TCOM file";
        input_data = read_file(argv[2], &input_size);
        if (!input_data) {
            goto done;
        }
        error = "Invalid TCOM header";
        capacity = tcom(input_data, input_size, NULL, 0, dimensions, 1);
        if (!capacity) {
            goto done;
        }
    } else {
        error = "Unsupported or invalid image";
        if (!read_image(argv[2], &input_data, dimensions, &input_size)) {
            goto done;
        }
        size_t pixels = input_size / dimensions[2];
        size_t max_bytes_per_pixel = dimensions[2] == 4 ? 5 : 4;
        error = "Image is too large";
        if (pixels > (SIZE_MAX - 32) / max_bytes_per_pixel) {
            goto done;
        }
        capacity = pixels * max_bytes_per_pixel + 32;
    }
    error = "Not enough memory";
    result_data = malloc(capacity);
    if (!result_data) {
        goto done;
    }
    error = "Invalid or truncated TCOM data";
    result_size = tcom(input_data, input_size, result_data, capacity, dimensions, decode);
    if (!result_size) {
        if (!decode) {
            error = "Could not encode the image";
        }
        goto done;
    }
    free(input_data);
    input_data = NULL;
    enum OutputFormat format = decode ? output_format(output_path) : OUTPUT_UNKNOWN;
    error = "Unsupported output extension";
    if (decode && format == OUTPUT_UNKNOWN) {
        goto done;
    }
    FILE *file = _wfopen(output_path, L"wbx");
    if (!file) {
        if (errno == EEXIST) {
            fprintf(stderr, "Output already exists.\n");
        } else {
            _wperror(output_path);
        }
        error = NULL;
        goto done;
    }
    int written = decode ? write_image(file, format, result_data, dimensions)
                         : fwrite(result_data, 1, result_size, file) == result_size;
    if (fclose(file)) {
        written = 0;
    }
    error = "Cannot write this image in the requested format";
    if (!written) {
        _wremove(output_path);
        goto done;
    }
    printf("%ls\n", output_path);
    result = 0;
done:
    if (result && error) {
        fprintf(stderr, "%s.\n", error);
    }
    free(input_data);
    free(result_data);
    free(default_output);
    return result;
}
