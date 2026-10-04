# TinyCodec

A lossless codec in 45 lines of C code.

# CLI

To use the CLI, use `pack` to encode an image and `unpack` to decode it into a PNG.

```text
tcom pack image.png
tcom unpack image.tcom
```

The CLI in `tcom_dependency.c` supports PNG, BMP, PNM/PAM, QOI and WebP.

The codec itself is 45 lines. The CLI and format adapter is 164 lines, minus
the image libraries.

## Benchmark

3,167 images. Combined file sizes and encoding/decoding speeds:

| Codec | Total size (MB) | Encode (MP/s) | Decode (MP/s) |
|---|---:|---:|---:|
| TCOM | 1,954.35 | 284.09 | 368.18 |
| QOI | 2,125.45 | 238.71 | 298.07 |
| libpng level 6 | 1,792.59 | 7.30 | 100.16 |
| Oxipng effort 4 | 1,621.25 | 1.14 | 113.90 |

MB is decimal and MP/s is million pixels per second.

This was done on a 9950x3d, measured in memory without file I/O and all decoded pixels matched as they should.

Copyright 2026 Ozzy M. Licensed under [Apache 2.0](LICENSE).

Third-party code retains their own licenses of course.
