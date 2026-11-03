# TinyCodec

A lossless codec in 45 lines of C code.

# CLI

To use the CLI, use `pack` to encode and `unpack` to decode it (converting into a png)

```text
tcom pack image.png
tcom unpack image.tcom
```

The CLI in `tcom_dependency.c` supports PNG, BMP, PNM/PAM, QOI and WebP.

The codec itself is 45 lines. The CLI and format adapter is 164 lines, minus image libraries.

The readable version `tcom_readable.c` is 167 lines.

## Benchmark

### QOI and TCOM on 17,660 images

The full corpus totals 21.63 gigapixels and includes photos, textures, aerial imagery, ui, scans, and graphics.

| Codec | Total size (GB) | Encode (MP/s) | Decode (MP/s) |
|---|---:|---:|---:|
| Reference QOI (`qoi.h`) | 17.60 | 275.35 | 342.41 |
| TCOM | 16.49 | 290.12 | 360.75 |

TCOM produced 6.34% smaller files than QOI and encoded and decoded about 5.36% faster overall.

[Results by image type](benchmarks/results.json).

### TCOM, QOI, libpng, and Oxipng on 640 images

This one covers 32 categories and totals 816.39 megapixels.

| Codec | Total size (MB) | Encode (MP/s) | Decode (MP/s) |
|---|---:|---:|---:|
| Reference QOI (`qoi.h`) | 862.01 | 209.43 | 262.46 |
| TCOM | 784.04 | 226.45 | 242.86 |
| libpng level 1 | 769.37 | 27.29 | 100.73 |
| libpng level 3 | 741.91 | 18.70 | 104.22 |
| libpng level 6 | 713.30 | 6.89 | 97.91 |
| Oxipng preset 4 | 646.06 | 1.18 | 112.12 |

Both PNG implementations used libpng for the decoding. [Results by image type](benchmarks/results-640.json).

Single threaded on a 9950x3D.

[Benchmark instructions](benchmarks/README.md) · [Frozen corpus manifest](benchmarks/corpus.jsonl)

Copyright 2026 Ozzy M. Licensed under [Apache 2.0](LICENSE).
