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
