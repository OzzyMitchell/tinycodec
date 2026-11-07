# TinyCodec

A very fast, tiny lossless image codec.

## CLI

Use `pack` to encode and `unpack` to decode. `unpack` decodes into a png by default.

```text
tcom pack image.png
tcom unpack image.tcom
```

The CLI supports PNG, BMP, PNM/PAM, and QOI. Give `unpack` an output filename to choose the format by extension.

The Windows CMake build uses `clang-cl`, libpng, and zlib. Set `-DTCOM_READABLE=ON` to build with the readable implementation instead.

## Benchmarks

- **Windows x64** — Drop an image or folder onto `benchmark.cmd`, or open it and paste the path.
- **Linux x64** (glibc 2.31+) — From the project folder, run `sh benchmarks/linux/benchmark.sh` and paste the path.

fpng requires SSE4.1.

Use the extracted 640-image ZIP or the full corpus folder, then press Enter through the prompts. [Download the ZIP (487 MB)](https://github.com/OzzyMitchell/tinycodec/releases/download/Benchmark-640-images/tinycodec-benchmark-640.zip)

Both runs used a 9950x3d with one thread and one repetition per image.

### 640 image set

There are 20 images in each of the 32 categories, covering photographs, textures, scanned maps, manuscripts, transparent icons, UI controls, and pixel art. This set totals 1.108 gigapixels.

| Codec | Total size (MB) | Encode (s) | Encode (MP/s) | Decode (s) | Decode (MP/s) |
|---|---:|---:|---:|---:|---:|
| Reference QOI (`qoi.h`) | 1685.70 | 6.07 | 182.47 | 5.31 | 208.71 |
| TCOM | 1534.76 | 5.45 | 203.34 | 4.40 | 251.74 |
| QOIR | 1565.47 | 5.64 | 196.66 | 5.15 | 215.24 |
| fpng single pass | 1850.09 | 3.27 | 339.27 | 6.13 | 180.87 |
| libpng level 1 | 1543.76 | 46.69 | 23.74 | 13.55 | 81.81 |

[Results and run details](benchmarks/results-640.json) · [Per-image CSV](benchmarks/results-640.csv) · [640 image manifest and licenses](benchmarks/640-manifest.jsonl)

### 17,660 images

The full corpus totals 21.63 gigapixels across 32 categories, including photos, textures, aerial imagery, UI, scans, and graphics.

| Codec | Total size (GB) | Encode (s) | Encode (MP/s) | Decode (s) | Decode (MP/s) |
|---|---:|---:|---:|---:|---:|
| TCOM | 16.488 | 65.98 | 327.76 | 56.53 | 382.57 |
| Reference QOI (`qoi.h`) | 17.605 | 80.66 | 268.14 | 64.37 | 335.97 |
| QOIR | 16.056 | 71.30 | 303.31 | 70.08 | 308.60 |
| fpng single pass | 19.643 | 55.29 | 391.15 | 75.24 | 287.46 |
| libpng level 1 | 15.628 | 622.04 | 34.77 | 168.15 | 128.62 |

[Results and run details](benchmarks/results.json) · [Per-image CSV](benchmarks/results-17660.csv) · [Corpus manifest](benchmarks/corpus.jsonl)

Copyright 2026 Ozzy M. Licensed under [Apache 2.0](LICENSE).
