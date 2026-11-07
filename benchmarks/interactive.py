"""Run a benchmark on a local image or folder."""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from time import perf_counter
import urllib.request

WINDOWS = os.name == "nt"
if WINDOWS:
    import msvcrt
else:
    import fcntl

try:
    from PIL import Image
except ModuleNotFoundError:
    raise SystemExit("Install Pillow with python -m pip install Pillow, then run the benchmark again.")

RUNNER = Path(__file__).resolve()
FROZEN = getattr(sys, "frozen", False)
HERE = Path(sys.executable).resolve().parents[1] if FROZEN else RUNNER.parent
WORKER_NAME = "compare.exe" if WINDOWS else "compare"
PLATFORM = "windows" if WINDOWS else "linux"
CPU = 1 if WINDOWS else min(os.sched_getaffinity(0))
REPO = HERE.parent
WORK = REPO / "interactive_benchmarks"
FLAGS = ["/nologo", "/O2", "/MT", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS"]
QOIR_COMMIT = "ce7b583beb123d897c4f20e436a2cb9d6d40e477"
QOIR_SHA256 = "a6d2a742484a1071cbccf02940454483671a175a0cb40c3cde24628a628df195"
QOIR_URL = f"https://raw.githubusercontent.com/nigeltao/qoir/{QOIR_COMMIT}/src/qoir.h"
FPNG_COMMIT = "925796543b9d26b8edfcdcecd94c1dac280f29fc"
FPNG_SOURCES = {"fpng.cpp": "b2b953f53f4b472b427bfb04a0ee1839ba66e8f32a02580e7a09d31313edd975",
                "fpng.h": "604c7e3757dba53b32f7e2523927c7e02ad90939d631a76f94f0690efa2c1fc9"}
WORKER_SOURCES = ("benchmarks/compare.c", "tcom.c", "third_party/qoi/qoi.h")
FPNG_ADAPTERS = ("benchmarks/fpng_adapter.cpp", "benchmarks/fpng_adapter.h")
CODECS = ("tcom", "qoi_reference", "qoir", *(f"libpng_level_{n}" for n in range(1, 10)), "fpng_single_pass")
LABELS = ("TCOM", "Reference QOI", "QOIR", *(f"libpng level {n}" for n in range(1, 10)), "fpng single pass")
IMAGE_EXTENSIONS = {".png", ".bmp", ".dib", ".pbm", ".pgm", ".ppm", ".pnm",
                    ".qoi", ".jpg", ".jpeg", ".webp", ".gif", ".tif", ".tiff"}


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def parse_choices(text, maximum):
    values = set()
    for part in text.replace(",", " ").split():
        match = re.fullmatch(r"(\d+)(?:-(\d+))?", part)
        if not match:
            raise ValueError("Use numbers or ranges separated by commas.")
        start = int(match[1])
        end = int(match[2]) if match[2] else start
        if not 1 <= start <= end <= maximum:
            raise ValueError(f"Choose numbers from 1 to {maximum}.")
        values.update(range(start, end + 1))
    if not values:
        raise ValueError("Choose at least one entry.")
    return sorted(values)


def ask_choices(prompt, maximum, default):
    while True:
        text = input(prompt).strip().lower() or default
        try:
            return parse_choices(f"1-{maximum}" if text == "all" else text, maximum)
        except ValueError as error:
            print(error)


def choose_measurement():
    print("\n1. Measure startup and file I/O (Total time)\n"
          "2. Measure in memory (More accurate for MP/s numbers)")
    while True:
        choice = ask_choices("Choose measurement [2]: ", 2, "2")
        if len(choice) == 1:
            return "total" if choice[0] == 1 else "memory"
        print("Choose one measurement mode.")


def ask_repetitions():
    while True:
        text = input("How many repetitions? (Slightly more accurate, but repeats the benchmark) [1]: ").strip()
        try:
            repetitions = int(text or "1")
            if 1 <= repetitions <= 20:
                return repetitions
        except ValueError:
            pass
        print("Enter a whole number from 1 to 20.")


def unquote(text):
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in ("'", '"'):
        return text[1:-1]
    return text


def choose_input(path):
    while path is None:
        entered = unquote(input("Drop or paste an image or folder path here: "))
        if entered:
            candidate = Path(entered).expanduser()
            if candidate.is_file() or candidate.is_dir():
                path = candidate
            else:
                print("That path could not be opened. Try another path.")
    path = path.resolve()
    if path.is_file():
        return path, [path]
    if not path.is_dir():
        raise ValueError(f"Image or folder does not exist: {path}")
    images = sorted(p for p in path.rglob("*") if p.suffix.lower() in IMAGE_EXTENSIONS and p.is_file())
    if not images:
        raise ValueError(f"No supported image files found in {path}")
    return path, images


def read_pixels(path):
    with Image.open(path) as image:
        if getattr(image, "n_frames", 1) != 1:
            raise ValueError("multiple frames")
        if image.mode not in {"1", "L", "LA", "P", "PA", "RGB", "RGBA"}:
            raise ValueError(f"unsupported pixel mode {image.mode}")
        if image.format == "PNG":
            with path.open("rb") as stream:
                header = stream.read(29)
            if len(header) != 29 or header[24] > 8:
                raise ValueError("PNG sample depth exceeds 8 bits")
        if image.format == "TIFF":
            bits = image.tag_v2.get(258, (8,))
            if any(n > 8 for n in ((bits,) if isinstance(bits, int) else bits)):
                raise ValueError("TIFF sample depth exceeds 8 bits")
        alpha = "A" in image.getbands() or "transparency" in image.info
        pixels = image.convert("RGBA" if alpha else "RGB")
        return pixels.width, pixels.height, len(pixels.getbands()), pixels.tobytes()


def compiler_environment(folder):
    if os.environ.get("INCLUDE") and os.environ.get("LIB") and shutil.which("link.exe"):
        return os.environ.copy()
    vswhere = (Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
               / "Microsoft Visual Studio/Installer/vswhere.exe")
    if not vswhere.is_file():
        raise RuntimeError("Open x64 Visual Studio Developer PowerShell or install the Visual C++ build tools.")
    install = subprocess.check_output(
        [str(vswhere), "-latest", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
        text=True,
    ).strip()
    vsdev = Path(install) / "Common7/Tools/VsDevCmd.bat"
    if not install or not vsdev.is_file():
        raise RuntimeError("Install the Visual C++ x64 build tools.")
    script = folder / "compiler-env.cmd"
    script.write_text('@echo off\ncall "%TCOM_BENCH_VSDEV%" -no_logo -arch=x64 -host_arch=x64 >nul\n'
                      'if errorlevel 1 exit /b 1\nset\n', encoding="ascii")
    env = os.environ.copy()
    env["TCOM_BENCH_VSDEV"] = str(vsdev)
    output = subprocess.check_output([os.environ.get("COMSPEC", "cmd.exe"), "/d", "/u", "/c", str(script)],
                                     env=env, cwd=folder).decode("utf-16-le")
    for line in output.splitlines():
        key, sep, value = line.partition("=")
        if sep and key.upper() in {"PATH", "INCLUDE", "LIB", "LIBPATH"}:
            env[key.upper()] = value
    if not env.get("INCLUDE") or not env.get("LIB"):
        raise RuntimeError("Could not load the Visual C++ x64 compiler environment.")
    return env


def get_qoir():
    path = WORK / "qoir.h"
    if path.is_file() and sha(path) == QOIR_SHA256:
        return path
    print("Fetching the pinned QOIR source for the benchmark worker.", flush=True)
    with urllib.request.urlopen(QOIR_URL, timeout=30) as response:
        source = response.read()
    if hashlib.sha256(source).hexdigest() != QOIR_SHA256:
        raise RuntimeError("The downloaded QOIR header did not match its expected SHA-256.")
    path.write_bytes(source)
    return path


def read_metadata(path):
    try:
        metadata = json.loads(path.read_text(encoding="utf-8"))
        return metadata if isinstance(metadata, dict) else {}
    except (OSError, ValueError):
        return {}


def get_fpng():
    folder = WORK / "fpng-source"
    folder.mkdir(exist_ok=True)
    for name, expected in FPNG_SOURCES.items():
        path = folder / name
        if path.is_file() and sha(path) == expected:
            continue
        print(f"Fetching the pinned {name} source for the benchmark worker.", flush=True)
        url = f"https://raw.githubusercontent.com/richgel999/fpng/{FPNG_COMMIT}/src/{name}"
        with urllib.request.urlopen(url, timeout=30) as response:
            source = response.read()
        if hashlib.sha256(source).hexdigest() != expected:
            raise RuntimeError(f"The downloaded {name} did not match its expected SHA-256.")
        path.write_bytes(source)
    return folder


def normalized_worker_sources():
    sources = {
        name: hashlib.sha256((REPO / name).read_bytes().replace(b"\r\n", b"\n")).hexdigest()
        for name in (*WORKER_SOURCES, *FPNG_ADAPTERS)
    }
    return {**sources, "qoir.h": QOIR_SHA256, **FPNG_SOURCES}


def ensure_worker(want_png, png_deps, want_fpng=False, use_bundle=True):
    if FROZEN and use_bundle and png_deps is None:
        folder = RUNNER.parent / "worker"
        exe = folder / WORKER_NAME
        bundled = read_metadata(folder / "build.json")
        if (exe.is_file() and bundled.get("executable_sha256") == sha(exe)
                and bundled.get("normalized_sources") == normalized_worker_sources()):
            return exe, bundled

    sources = {"benchmarks/interactive.py": sha(RUNNER)}
    sources.update({name: sha(REPO / name) for name in WORKER_SOURCES})
    sources["qoir.h"] = QOIR_SHA256
    if want_fpng:
        sources.update(FPNG_SOURCES)
        for name in FPNG_ADAPTERS:
            sources[name] = sha(REPO / name)
    if png_deps is not None:
        png_deps = png_deps.expanduser().resolve()
    for variant in (("png",) if want_png else ("core", "png")):
        if want_fpng:
            variant += "-fpng"
        folder = WORK / f"{PLATFORM}-{variant}"
        cached = read_metadata(folder / "build.json")
        exe = folder / WORKER_NAME
        if (exe.is_file() and cached.get("sources") == sources
                and cached.get("executable_sha256") == sha(exe)
                and (png_deps is None or cached.get("png_deps") == str(png_deps))):
            return exe, cached

    variant = ("png" if want_png else "core") + ("-fpng" if want_fpng else "")
    folder = WORK / f"{PLATFORM}-{variant}"
    folder.mkdir(exist_ok=True)
    if not WINDOWS:
        return build_linux_worker(folder, sources, want_png, png_deps, want_fpng)
    if want_png and png_deps is None:
        configured = os.environ.get("TCOM_BENCH_PNG_DEPS") or read_metadata(folder / "build.json").get("png_deps")
        if configured:
            png_deps = Path(configured)
        else:
            print("libpng needs an x64 static libpng/zlib folder with include and lib subfolders.")
            value = unquote(input("libpng/zlib folder: "))
            if not value:
                raise ValueError("Choose a libpng/zlib folder to benchmark PNG.")
            png_deps = Path(value)
        png_deps = png_deps.expanduser().resolve()
    libraries = []
    flags = [*FLAGS, "/DBENCH_INTERACTIVE", f"/I{get_qoir().parent}"]
    fpng_flags = []
    if want_fpng:
        fpng_source = get_fpng()
        flags += ["/DBENCH_FPNG"]
        fpng_flags = ["/nologo", "/O2", "/MT", "/std:c++17", "/EHsc", "/DNDEBUG",
                      "/D_CRT_SECURE_NO_WARNINGS", "/clang:-msse4.1", "/clang:-mpclmul",
                      "/clang:-fno-strict-aliasing", f"/I{fpng_source}"]
    if want_png:
        headers = [png_deps / "include/png.h", png_deps / "include/zlib.h"]
        libraries = [png_deps / "lib/libpng16_static.lib", png_deps / "lib/zs.lib"]
        for path in headers + libraries:
            if not path.is_file():
                raise ValueError(f"Missing dependency file: {path}")
        flags += ["/DBENCH_LIBPNG", f"/I{png_deps / 'include'}"]

    print("Building the benchmark worker.", flush=True)
    with tempfile.TemporaryDirectory(prefix="compile-", dir=folder) as temporary:
        scratch = Path(temporary)
        env = compiler_environment(scratch)
        compiler = shutil.which("clang-cl.exe", path=env.get("PATH"))
        if not compiler:
            installed = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM/bin/clang-cl.exe"
            if installed.is_file():
                compiler = str(installed)
            else:
                raise RuntimeError("Install LLVM clang-cl or put it on PATH.")
        objects = []
        if want_fpng:
            for source in (fpng_source / "fpng.cpp", HERE / "fpng_adapter.cpp"):
                obj = source.stem + ".obj"
                subprocess.run([compiler, *fpng_flags, "/c", str(source), f"/Fo{obj}"],
                               cwd=scratch, env=env, check=True)
                objects.append(obj)
        subprocess.run(
            [compiler, *flags, "/c", str(HERE / "compare.c"), "/Focompare.obj"],
            cwd=scratch, env=env, check=True,
        )
        subprocess.run(
            [compiler, "/nologo", "/MT", "compare.obj", *objects, "/Fecompare.exe",
             "/link", *map(str, libraries)],
            cwd=scratch, env=env, check=True,
        )
        exe = folder / "compare.exe"
        (scratch / "compare.exe").replace(exe)
        metadata = {
            "sources": sources,
            "executable_sha256": sha(exe),
            "compiler": subprocess.check_output([compiler, "--version"], env=env, text=True).strip(),
            "flags": flags,
            "png_deps": str(png_deps) if want_png else None,
            "libraries": {p.name: sha(p) for p in libraries},
            "qoir_commit": QOIR_COMMIT,
            "fpng_commit": FPNG_COMMIT if want_fpng else None,
            "fpng_flags": fpng_flags,
            "link_flags": ["/nologo", "/MT", "/link"],
        }
        if want_png:
            metadata["png_versions"] = subprocess.check_output([str(exe), "versions"], text=True).strip()
        if want_png and want_fpng:
            metadata["normalized_sources"] = normalized_worker_sources()
        (folder / "build.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return exe, metadata


def build_linux_worker(folder, sources, want_png, png_deps, want_fpng):
    flags = ["-O2", "-std=c11", "-DBENCH_INTERACTIVE", f"-I{get_qoir().parent}"]
    libraries = []
    link_flags = []
    if want_png:
        flags.append("-DBENCH_LIBPNG")
        if png_deps:
            flags.append(f"-I{png_deps / 'include'}")
            libraries = [png_deps / "lib/libpng16.a", png_deps / "lib/libz.a"]
            link_flags = [*map(str, libraries), "-lm"]
        else:
            flags += subprocess.check_output(["pkg-config", "--cflags", "libpng", "zlib"], text=True).split()
            link_flags = subprocess.check_output(["pkg-config", "--libs", "libpng", "zlib"], text=True).split()
    fpng_flags = []
    if want_fpng:
        fpng_source = get_fpng()
        flags.append("-DBENCH_FPNG")
        fpng_flags = ["-O2", "-std=c++17", "-DNDEBUG", "-msse4.1", "-mpclmul",
                      "-fno-strict-aliasing", f"-I{fpng_source}"]
    print("Building the benchmark worker.", flush=True)
    with tempfile.TemporaryDirectory(prefix="compile-", dir=folder) as temporary:
        scratch = Path(temporary)
        subprocess.run(["cc", *flags, "-c", str(HERE / "compare.c"), "-o", "compare.o"],
                       cwd=scratch, check=True)
        objects = ["compare.o"]
        if want_fpng:
            for source in (fpng_source / "fpng.cpp", HERE / "fpng_adapter.cpp"):
                obj = source.stem + ".o"
                subprocess.run(["c++", *fpng_flags, "-c", str(source), "-o", obj], cwd=scratch, check=True)
                objects.append(obj)
        linker = "c++" if want_fpng else "cc"
        subprocess.run([linker, *objects, *link_flags, "-o", WORKER_NAME], cwd=scratch, check=True)
        exe = folder / WORKER_NAME
        (scratch / WORKER_NAME).replace(exe)
    metadata = {
        "sources": sources,
        "executable_sha256": sha(exe),
        "compiler": subprocess.check_output(["cc", "--version"], text=True).splitlines()[0],
        "flags": flags,
        "png_deps": str(png_deps) if png_deps else None,
        "libraries": {p.name: sha(p) for p in libraries},
        "qoir_commit": QOIR_COMMIT,
        "fpng_commit": FPNG_COMMIT if want_fpng else None,
        "fpng_flags": fpng_flags,
        "link_flags": link_flags,
    }
    if want_png:
        metadata["png_versions"] = subprocess.check_output([str(exe), "versions"], text=True).strip()
    if want_png and want_fpng:
        metadata["normalized_sources"] = normalized_worker_sources()
    (folder / "build.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return exe, metadata


def report_table(stats, slots, images, pixels, skipped, measurement, repetitions):
    timing = ("Total time. Includes worker startup, codec work and buffered file I/O.\n"
              "Prepared RGB/RGBA files are used. Source image loading and pixel checks are outside timing."
              if measurement == "total" else
              "In memory. Includes allocation. Input loading, file I/O and pixel checks are outside timing.")
    lines = [f"{images:,} image(s), {pixels:,} pixels. Single threaded. {skipped} skipped.",
             timing, f"Repetitions per image {repetitions}. Times are averages. Bytes counted once.",
             "", f"{'Codec':<19} {'Bytes':>14} {'Encode s':>10} {'Encode MP/s':>12}"
                 f" {'Decode s':>10} {'Decode MP/s':>12}"]
    for slot in slots:
        result = stats[CODECS[slot]]
        encode = f"{result['encode_mpps']:.2f}" if result["encode_mpps"] is not None else "n/a"
        decode = f"{result['decode_mpps']:.2f}" if result["decode_mpps"] is not None else "n/a"
        lines.append(f"{LABELS[slot]:<19} {result['bytes']:>14,} {result['encode_sec']:>10.4g} "
                     f"{encode:>12} {result['decode_sec']:>10.4g} {decode:>12}")
    return "\n".join(lines)


def benchmark_files(exe, scratch, slots, width, height, channels, raw, order, repetitions):
    records = {slot: {"name": CODECS[slot], "bytes": 0, "encode_sec": 0.0, "decode_sec": 0.0,
                      "encode_reps": [], "decode_reps": [], "loops": 1, "checks": 0} for slot in slots}
    shape = [str(width), str(height), str(channels)]
    for repetition in range(repetitions):
        first = (order + repetition) % len(slots)
        for slot in slots[first:] + slots[:first]:
            record = records[slot]
            for operation, source, destination in (("encode", "pixels.raw", "encoded.bin"),
                                                    ("decode", "encoded.bin", "decoded.raw")):
                start = perf_counter()
                process = subprocess.run(
                    [str(exe), operation, str(slot), source, destination, *shape],
                    cwd=scratch, capture_output=True, text=True,
                )
                elapsed = perf_counter() - start
                if process.returncode:
                    raise RuntimeError(process.stderr.strip() or f"{LABELS[slot]} {operation} failed")
                record[operation + "_sec"] += elapsed
                record[operation + "_reps"].append(elapsed)
            size = (scratch / "encoded.bin").stat().st_size
            if size <= 0 or (record["bytes"] and record["bytes"] != size):
                raise RuntimeError(f"Unexpected encoded size for {LABELS[slot]}.")
            if (scratch / "decoded.raw").read_bytes() != raw:
                raise RuntimeError(f"Pixel verification failed for {LABELS[slot]}.")
            record["bytes"] = size
            record["checks"] += 1
    for record in records.values():
        record["encode_sec"] /= repetitions
        record["decode_sec"] /= repetitions
    return {"codecs": [records[slot] for slot in slots]}


def benchmark(path, images, slots, exe, metadata, measurement, repetitions):
    output = Path(tempfile.mkdtemp(prefix=datetime.now().strftime("%Y%m%d-%H%M%S-"), dir=WORK))
    print(f"Reports: {output}", flush=True)
    stats = {CODECS[slot]: {"bytes": 0, "encode_sec": 0.0, "decode_sec": 0.0, "checks": 0} for slot in slots}
    mask = sum(1 << slot for slot in slots)
    pixels = 0
    complete = 0
    skipped = []
    with tempfile.TemporaryDirectory(prefix="pixels-", dir=output) as temporary, \
            (output / "images.jsonl").open("w", encoding="utf-8", buffering=1) as log:
        scratch = Path(temporary)
        for index, image in enumerate(images, 1):
            try:
                width, height, channels, raw = read_pixels(image)
            except (OSError, ValueError, Image.DecompressionBombError) as error:
                if path.is_file():
                    raise ValueError(f"Could not read {image.name}: {error}") from error
                skipped.append({"path": str(image), "reason": str(error)})
                print(f"Skipped {image.name}: {error}", flush=True)
                continue
            (scratch / "pixels.raw").write_bytes(raw)
            if measurement == "total":
                row = benchmark_files(exe, scratch, slots, width, height, channels, raw,
                                      complete % len(slots), repetitions)
            else:
                process = subprocess.run(
                    [str(exe), "pixels.raw", str(width), str(height), str(channels), str(repetitions),
                     str(complete % len(slots)), str(mask), "--no-warmup"],
                    cwd=scratch, capture_output=True, text=True,
                )
                if process.returncode:
                    raise RuntimeError(f"{image.name}: {process.stderr.strip() or 'benchmark worker failed'}")
                row = json.loads(process.stdout)
            if [record["name"] for record in row["codecs"]] != list(stats):
                raise RuntimeError("The benchmark worker returned an unexpected codec list.")
            for record in row["codecs"]:
                if record["loops"] != 1 or record["checks"] != repetitions or record["bytes"] <= 0:
                    raise RuntimeError(f"Pixel verification failed for {image.name}.")
                for key in stats[record["name"]]:
                    stats[record["name"]][key] += record[key]
            pixels += width * height
            complete += 1
            log.write(json.dumps({"path": str(image), "width": width, "height": height, "channels": channels,
                                  "raw_sha256": hashlib.sha256(raw).hexdigest(),
                                  "measurement": measurement, **row}) + "\n")
            if len(images) <= 20 or index % 25 == 0 or index == len(images):
                print(f"{index}/{len(images)} checked", flush=True)
    if not complete:
        raise ValueError("No images could be benchmarked.")
    for result in stats.values():
        for op in ("encode", "decode"):
            seconds = result[op + "_sec"]
            result[op + "_mpps"] = pixels / seconds / 1e6 if seconds > 0 else None
    timing = ("mean time per fresh process; includes process startup and exit, input reads, "
              "codec calls and buffered output writes; excludes shared image loading, pixel preparation, "
              "build and exact pixel verification" if measurement == "total" else
              "mean time per in-memory call; allocation included; excludes input decoding, "
              "process startup, file I/O and exact pixel verification")
    summary = {"input": str(path), "images": complete, "pixels": pixels, "skipped": skipped,
               "host": platform.platform(), "cpu": platform.processor(),
               "python": platform.python_version(), "affinity": f"logical CPU {CPU}",
               "measurement": measurement, "repetitions": repetitions, "warmup": False,
               "runner_sha256": sha(RUNNER), "timing": timing,
               "input_layout": "shared RGB/RGBA pixels; grayscale expanded",
               "build": metadata, "codecs": stats}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    table = report_table(stats, slots, complete, pixels, len(skipped), measurement, repetitions)
    (output / "report.txt").write_text(table + "\n", encoding="utf-8")
    print("\n" + table + f"\n\nReport: {output / 'report.txt'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, nargs="?", help="Image or folder. Omit to enter it interactively.")
    parser.add_argument("--png-deps", type=Path, help="Folder containing static libpng and zlib.")
    parser.add_argument("--prepare", action="store_true", help="Build the worker without running a comparison.")
    parser.add_argument("--fpng", action="store_true", help="Include fpng when preparing the worker.")
    args = parser.parse_args()
    if not args.prepare:
        print("TinyCodec benchmark")
        path, images = choose_input(args.path)
        print(f"\nFound {len(images):,} image(s).\n1. TCOM   2. Reference QOI   3. QOIR   4. libpng   5. fpng")
        choices = ask_choices("Choose codecs [all]: ", 5, "all")
        slots = [choice - 1 for choice in choices if choice < 4]
        if 4 in choices:
            levels = ask_choices("libpng levels 1-9 (e.g. 1,2,3,4 or 1-9) [1]: ", 9, "1")
            slots.extend(level + 2 for level in levels)
        if 5 in choices:
            slots.append(12)
        measurement = choose_measurement()
        repetitions = ask_repetitions()
    WORK.mkdir(parents=True, exist_ok=True)
    with (WORK / ".run.lock").open("a+b") as lock:
        if not lock.tell():
            lock.write(b"0")
            lock.flush()
        lock.seek(0)
        try:
            if WINDOWS:
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise RuntimeError("Another interactive benchmark is already running.") from error
        if args.prepare:
            exe, _ = ensure_worker(args.png_deps is not None, args.png_deps, args.fpng, use_bundle=False)
            print(f"Ready: {exe}")
        else:
            exe, metadata = ensure_worker(4 in choices, args.png_deps, 5 in choices)
            benchmark(path, images, slots, exe, metadata, measurement, repetitions)


if __name__ == "__main__":
    try:
        main()
    except (EOFError, KeyboardInterrupt):
        raise SystemExit("\nBenchmark cancelled.")
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Benchmark stopped: {error}")
