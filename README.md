# TinyCodec

A lossless codec in 71 lines of C code.

# Important

This image codec is highly subject to change, as I do not believe this is the final version. Previous versions will remain available in this repo however, I believe there is plenty of room for improvement.

# CLI

To use the CLI, use `pack` to encode an image and `unpack` to decode it into a PNG.

```text
tcom pack image.png
tcom unpack image.tcom
```

The CLI in `tcom_dependency.c` supports PNG, BMP, PNM/PAM, QOI and WebP.

The codec itself is 71 lines. The CLI and format adapter is 158 lines, minus
the image libraries.

## Benchmark

3,167 images. Combined file sizes and encoding/decoding speeds:

| Codec | Total size (MB) | Encode (MP/s) | Decode (MP/s) |
|---|---:|---:|---:|
| TCOM | 1,968.04 | 250.54 | 304.60 |
| QOI | 2,125.45 | 238.71 | 298.07 |
| libpng level 6 | 1,792.59 | 7.30 | 100.16 |
| Oxipng effort 4 | 1,621.25 | 1.14 | 113.90 |

MB is decimal; MP/s is million pixels per second. Three repetitions on a
Ryzen 9 9950X3D, measured in memory without file I/O. All decoded pixels matched.

Copyright 2026 Ozzy M. Licensed under [Apache 2.0](LICENSE).

Third-party code retains their own licenses of course.
