"""Reproduce the TCOM / reference QOI benchmark on Windows."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import platform
import struct
import shutil
import subprocess
import tempfile
import time

from PIL import Image

BENCHMARK = Path(__file__).resolve().parent
REPO = BENCHMARK.parent
CODECS = ("tcom", "qoi_reference", "libpng_level_1", "libpng_level_3", "libpng_level_6", "oxipng_preset_4")
SUFFIXES = (".tcom", ".qoi", ".png1", ".png3", ".png6", ".oxi.png")
FLAGS = ["/nologo", "/O2", "/MT", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS"]


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def stratified_sample(images, limit):
    groups = defaultdict(list)
    for image in images:
        groups[image["Category"]].append(image)
    categories = sorted(groups)
    if not len(categories) <= limit <= len(images):
        raise ValueError(f"--sample must be between {len(categories)} and {len(images)}")
    quotas = dict.fromkeys(categories, 0)
    remaining = limit
    while remaining:
        for category in categories:
            if quotas[category] < len(groups[category]):
                quotas[category] += 1
                remaining -= 1
                if not remaining:
                    break
    selected = []
    for category in categories:
        group = sorted(groups[category], key=lambda im: (int(im["Pixels"]), im["RawSHA256"]))
        count = quotas[category]
        selected.extend(group[(2 * i + 1) * len(group) // (2 * count)] for i in range(count))
    return sorted(selected, key=lambda im: im["RawSHA256"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, default=REPO / "build/benchmark-corpus")
    parser.add_argument("--out", type=Path, default=REPO / "build/benchmark-run")
    parser.add_argument("--clang-cl", default="clang-cl")
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--sample", type=int, help="Sample this many images across all categories")
    selection.add_argument("--limit", type=int, help="Run only the first N images for a smoke check")
    parser.add_argument("--png-deps", type=Path, help="Include libpng levels 1, 3, and 6 using a static libpng/zlib install")
    parser.add_argument("--oxipng-dll", type=Path, help="Include the Oxipng preset 4 adapter")
    parser.add_argument("--reps", type=int, choices=range(1, 21), default=3,
                        help="Timed repetitions per image. One uses a single timed call.")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.limit is not None and args.limit < 1:
        parser.error("--limit must be positive")
    corpus = args.corpus.resolve()
    out = args.out.resolve()
    manifest = BENCHMARK / "corpus.jsonl"
    images = list(map(json.loads, manifest.read_text(encoding="utf-8").splitlines()))[:args.limit]
    if args.sample is not None:
        images = stratified_sample(images, args.sample)
    if not corpus.is_dir():
        parser.error(f"Missing corpus directory: {corpus}")
    missing = next((im["Path"] for im in images if not (corpus / im["Path"]).is_file()), None)
    if missing:
        parser.error(f"Missing corpus input: {missing}")

    header = REPO / "third_party/qoi/qoi.h"
    upstream = json.loads((BENCHMARK / "upstream.json").read_text())
    canonical_header = header.read_bytes().replace(b"\r\n", b"\n")
    if hashlib.sha256(canonical_header).hexdigest() != upstream["sha256"]:
        raise ValueError("Reference QOI header differs from upstream")

    flags = FLAGS.copy()
    libraries = []
    if args.png_deps:
        deps = args.png_deps.resolve()
        flags += ["/DBENCH_LIBPNG", f"/I{deps / 'include'}"]
        libraries = [deps / "lib/libpng16_static.lib", deps / "lib/zs.lib"]
    if args.oxipng_dll and not libraries:
        parser.error("--oxipng-dll requires --png-deps")
    oxi = args.oxipng_dll.resolve() if args.oxipng_dll else None
    if oxi:
        flags += ["/DBENCH_OXIPNG"]
    codec_count = 6 if oxi else 5 if libraries else 2

    sources = [Path(__file__), BENCHMARK / "compare.c", REPO / "tcom.c", header]
    meta = {
        "platform": platform.platform(),
        "cpu": subprocess.check_output(
            ["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_Processor).Name"],
            text=True,
        ).strip(),
        "compiler": subprocess.check_output([args.clang_cl, "--version"], text=True),
        "flags": flags,
        "libraries": {p.name: sha(p) for p in libraries},
        "oxipng_sha256": sha(oxi) if oxi else None,
        "manifest_sha256": sha(manifest),
        "images": len(images),
        "sample": args.sample,
        "source_sha256": {p.name: sha(p) for p in sources},
        "affinity": "logical CPU 1",
        "reps": args.reps,
        "upstream": upstream,
    }
    exe = out / "compare.exe"
    metadata = out / "metadata.json"
    result_log = out / "results.jsonl"
    rows = []
    if args.resume:
        if oxi and sha(out / "oxi_bench.dll") != meta["oxipng_sha256"]:
            raise ValueError("Oxipng adapter changed")
        recorded = json.loads(metadata.read_text())
        if recorded != {**meta, "executable_sha256": sha(exe)}:
            raise ValueError("Resume metadata does not match this run")
        if result_log.exists():
            rows = list(map(json.loads, result_log.read_text().splitlines()))
            if len(rows) > len(images) or any(row["path"] != im["Path"] for row, im in zip(rows, images)):
                raise ValueError("Resume results do not match the corpus order")
    else:
        out.mkdir(parents=True)
        if oxi:
            shutil.copyfile(oxi, out / "oxi_bench.dll")
        subprocess.run(
            [args.clang_cl, *flags, str(BENCHMARK / "compare.c"), "/Fecompare.exe", "/Focompare.obj",
             *(["/link", *map(str, libraries)] if libraries else [])],
            cwd=out, check=True,
        )
        (out / "selection.json").write_text(json.dumps(images, indent=2) + "\n")
        metadata.write_text(json.dumps({**meta, "executable_sha256": sha(exe)}, indent=2) + "\n")

    start = time.monotonic()
    with tempfile.TemporaryDirectory(dir=out) as scratch, result_log.open("a", buffering=1) as log:
        raw_path = Path(scratch) / "pixels.raw"
        encoded = Path(scratch) / "encoded"
        for index in range(len(rows), len(images)):
            im = images[index]
            path = corpus / im["Path"]
            if sha(path) != im["NormalizedSHA256"]:
                raise ValueError(f"File hash mismatch: {path}")
            with Image.open(path) as image:
                if image.mode != {1: "L", 3: "RGB", 4: "RGBA"}[im["source_channels"]] or image.size != (int(im["Width"]), int(im["Height"])):
                    raise ValueError(f"Unexpected image mode or dimensions: {path}")
                raw = (image.convert("RGB") if image.mode == "L" else image).tobytes()
                identity = struct.pack("<II", *image.size) + raw if im["group"] == "independent" else raw
                if hashlib.sha256(identity).hexdigest() != im["RawSHA256"]:
                    raise ValueError(f"Pixel hash mismatch: {path}")

            raw_path.write_bytes(raw)
            result = subprocess.run(
                [str(exe), str(raw_path), im["Width"], im["Height"], str(im["Channels"]),
                 str(args.reps), str(index % codec_count), str(encoded)],
                capture_output=True, text=True,
            )
            if result.returncode or result.stderr:
                raise RuntimeError(f"Benchmark failed on {path}: {result.returncode} {result.stderr}")
            row = {
                "path": im["Path"], "category": im["Category"], "group": im["group"],
                "source_channels": im["source_channels"], "pixels": int(im["Pixels"]),
                "channels": im["Channels"], **json.loads(result.stdout),
            }
            for codec, suffix in zip(row["codecs"], SUFFIXES):
                codec["sha256"] = sha(encoded.with_suffix(suffix))
            log.write(json.dumps(row) + "\n")
            rows.append(row)
            if index % 100 == 0 or index + 1 == len(images):
                print(f"{index+1}/{len(images)} elapsed={time.monotonic()-start:.1f}s", flush=True)

    summary = summarize(rows)
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary["TOTAL"], indent=2))


def summarize(rows):
    groups = {"TOTAL": rows}
    layouts = {1: "gray_as_rgb", 3: "native_rgb", 4: "native_rgba"}
    for row in rows:
        for key in (row["category"], row["group"], layouts[row["source_channels"]]):
            groups.setdefault(key, []).append(row)
    summary = {}
    for category, group in groups.items():
        pixels = sum(row["pixels"] for row in group)
        codecs = {}
        for index, name in enumerate(CODECS[:len(group[0]["codecs"])]):
            records = [row["codecs"][index] for row in group]
            totals = {key: sum(record[key] for record in records)
                      for key in ("bytes", "encode_sec", "decode_sec", "checks")}
            for operation in ("encode", "decode"):
                totals[f"{operation}_mpps"] = pixels / totals[f"{operation}_sec"] / 1e6
                totals[f"{operation}_repetition_mpps"] = [
                    pixels / sum(record[f"{operation}_reps"][repeat] for record in records) / 1e6
                    for repeat in range(len(records[0][f"{operation}_reps"]))
                ]
            codecs[name] = totals
        summary[category] = {"images": len(group), "pixels": pixels, "codecs": codecs}
    return summary


if __name__ == "__main__":
    main()
