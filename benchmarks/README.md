# TCOM benchmark

Use x64 Visual Studio Developer PowerShell with Python 3.11+ and LLVM's `clang-cl` on PATH. Install Pillow with `python -m pip install Pillow`.

Place the 42.11 GB of input files listed in [corpus.jsonl](corpus.jsonl) under `build/benchmark-corpus`, keeping the manifest paths. The image files are stored separately. Pass `-Corpus PATH` to use another copy.

```powershell
# Reference QOI and TCOM on 17,660 images
./benchmarks/reproduce.ps1 full

# All six configurations on 640 images
./benchmarks/reproduce.ps1 sample -PngDeps PATH
```

For the 640-image run, install Rust/Cargo and pass an x64 static libpng/zlib install with headers in `include` and `libpng16_static.lib` and `zs.lib` in `lib`. The recorded run used libpng 1.6.58, zlib 1.3.2, and Oxipng 10.2.1. The script builds the locked Oxipng preset 4 adapter.

Results go to `build/benchmark-full` or `build/benchmark-sample`. Add `-Smoke` to check two images or rerun the original command with `-Resume` to continue an interrupted run.
