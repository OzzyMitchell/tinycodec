"""Reproduce the TCOM / reference QOI benchmark on Windows."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess
import tempfile
import time

from PIL import Image

BENCHMARK = Path(__file__).resolve().parent
REPO = BENCHMARK.parent
FLAGS = ["/nologo", "/O2", "/MT", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS"]


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, default=REPO / "build/benchmark-corpus")
    parser.add_argument("--out", type=Path, default=REPO / "build/benchmark-run")
    parser.add_argument("--clang-cl", default="clang-cl")
    parser.add_argument("--limit", type=int, help="Run only the first N images for a smoke check")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.limit is not None and args.limit < 1:
        parser.error("--limit must be positive")
    corpus = args.corpus.resolve()
    out = args.out.resolve()
    manifest = BENCHMARK / "corpus.jsonl"
    images = list(map(json.loads, manifest.read_text(encoding="utf-8").splitlines()))[:args.limit]
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

    sources = [Path(__file__), BENCHMARK / "compare.c", REPO / "tcom.c", header]
    meta = {
        "platform": platform.platform(),
        "cpu": subprocess.check_output(
            ["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_Processor).Name"],
            text=True,
        ).strip(),
        "compiler": subprocess.check_output([args.clang_cl, "--version"], text=True),
        "flags": FLAGS,
        "manifest_sha256": sha(manifest),
        "images": len(images),
        "source_sha256": {p.name: sha(p) for p in sources},
        "affinity": "logical CPU 1",
        "reps": 3,
        "upstream": upstream,
    }
    exe = out / "compare.exe"
    metadata = out / "metadata.json"
    result_log = out / "results.jsonl"
    rows = []
    if args.resume:
        recorded = json.loads(metadata.read_text())
        if recorded != {**meta, "executable_sha256": sha(exe)}:
            raise ValueError("Resume metadata does not match this run")
        if result_log.exists():
            rows = list(map(json.loads, result_log.read_text().splitlines()))
            if len(rows) > len(images) or any(row["path"] != im["Path"] for row, im in zip(rows, images)):
                raise ValueError("Resume results do not match the corpus order")
    else:
        out.mkdir(parents=True)
        subprocess.run(
            [args.clang_cl, *FLAGS, str(BENCHMARK / "compare.c"), "/Fecompare.exe", "/Focompare.obj"],
            cwd=out, check=True,
        )
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
                 "3", str(index % 2), str(encoded)],
                capture_output=True, text=True,
            )
            if result.returncode or result.stderr:
                raise RuntimeError(f"Benchmark failed on {path}: {result.returncode} {result.stderr}")
            row = {
                "path": im["Path"], "category": im["Category"], "group": im["group"],
                "source_channels": im["source_channels"], "pixels": int(im["Pixels"]),
                "channels": im["Channels"], **json.loads(result.stdout),
            }
            for codec, suffix in zip(row["codecs"], (".tcom", ".qoi")):
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
        for index, name in enumerate(("tcom", "qoi_reference")):
            records = [row["codecs"][index] for row in group]
            totals = {key: sum(record[key] for record in records)
                      for key in ("bytes", "encode_sec", "decode_sec", "checks")}
            for operation in ("encode", "decode"):
                totals[f"{operation}_mpps"] = pixels / totals[f"{operation}_sec"] / 1e6
                totals[f"{operation}_repetition_mpps"] = [
                    pixels / sum(record[f"{operation}_reps"][repeat] for record in records) / 1e6
                    for repeat in range(3)
                ]
            codecs[name] = totals
        summary[category] = {"images": len(group), "pixels": pixels, "codecs": codecs}
    return summary


if __name__ == "__main__":
    main()
