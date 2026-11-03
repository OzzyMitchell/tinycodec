# TinyCodec

A lossless codec in 45 lines of C code.

# CLI

To use the CLI, use `pack` to encode an image and `unpack` to decode it into a PNG.

```text
tcom pack image.png
tcom unpack image.tcom
```

The CLI in `tcom_dependency.c` supports PNG, BMP, PNM/PAM, QOI and WebP.

The codec itself is 45 lines. The CLI and format adapter is 164 lines, minus image libraries.

The readable version `tcom_readable.c` is 167 lines.

## Benchmark

17,660 images totaling 21.63 gigapixels, including photos, textures, aerial imagery, UI, scans, and graphics.

| Codec | Total size (GB) | Encode (MP/s) | Decode (MP/s) |
|---|---:|---:|---:|
| TCOM | 16.49 | 290.12 | 360.75 |
| Reference QOI (`qoi.h`) | 17.60 | 275.35 | 342.41 |

TCOM produced 6.34% smaller files and encoded and decoded 5.36% faster in the pooled results. On native RGB, TCOM encoded 1.55% faster and decoded 12.51% faster. On document scans expanded from grayscale to RGB, TCOM encoded 18.87% faster, decoded 16.40% slower, and produced 43.38% larger files.

Measured in memory on one logical CPU of a Ryzen 9 9950X3D with three repetitions. GB is decimal. Throughput divides the total pixels by the sum of the mean per-image times.

[Benchmark instructions](benchmarks/README.md) · [Frozen corpus manifest](benchmarks/corpus.jsonl) · [Full results by image type](benchmarks/results.json)

Copyright 2026 Ozzy M. Licensed under [Apache 2.0](LICENSE).
