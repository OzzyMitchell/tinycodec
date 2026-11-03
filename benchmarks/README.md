# TCOM benchmark

From the repository root, use x64 Visual Studio Developer PowerShell with Python 3.11+ and LLVM's `clang-cl` on PATH.

Place the 42.11 GB of image files listed in [corpus.jsonl](corpus.jsonl) under `build/benchmark-corpus`, keeping the manifest paths. The files are stored separately from the repository. Use `--corpus PATH` to point to another copy.

```powershell
python -m pip install Pillow
python benchmarks/run.py
```

Add `--limit 8 --out build/benchmark-smoke` for a quick check or `--resume` to continue an interrupted run.

Both codecs run on one logical CPU for three timed repetitions per image. Grayscale inputs are expanded to RGB before timing. Timings include allocation and exclude file I/O, verification, and freeing buffers.

Run output goes to `build/benchmark-run`. The [recorded results](results.json) include breakdowns by image type.

For a category-balanced comparison with libpng, run:

```powershell
python benchmarks/run.py --sample 2000 --png-deps PATH --out build/benchmark-png
```

`PATH` is an x64 static libpng/zlib install with headers in `include` and `libpng16_static.lib` and `zs.lib` in `lib`. PNG uses zlib levels 1, 3, and 6 with adaptive row filters at each level. Sampling distributes slots across categories and selects evenly across each category's image sizes. The run saves the exact selection alongside the results.

To include Oxipng preset 4, build the adapter with `cargo build --manifest-path benchmarks/oxipng-ffi/Cargo.toml --release --locked` and add `--oxipng-dll benchmarks/oxipng-ffi/target/release/oxi_bench.dll`. The adapter runs serially and preserves RGB beneath transparency. Both PNG implementations use libpng for decoding.
