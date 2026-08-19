#!/usr/bin/env python3
"""Repeatable whole-file I/O benchmark for eqmdsk G/A/K/S files."""

from __future__ import annotations

import argparse
import datetime as dt
import gc
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import tempfile
import time
from typing import Any, Callable


FORMAT_CLASSES = {
    "G": "GFile",
    "A": "AFile",
    "K": "KFile",
    "S": "SFile",
}


def _fortran_real(value: float) -> str:
    return f"{value:16.9E}"


def _real_records(values: list[float]) -> str:
    return "".join(
        "".join(_fortran_real(value) for value in values[start : start + 5])
        + "\n"
        for start in range(0, len(values), 5)
    )


def _make_gfile(path: Path, grid: int) -> None:
    nw = grid
    nh = grid
    header = f"{'EQMDSK SYNTHETIC BENCHMARK':<48}{0:4d}{nw:4d}{nh:4d}\n"
    scalars = [
        1.8,
        3.2,
        1.7,
        0.8,
        0.0,
        1.7,
        0.0,
        -0.8,
        0.2,
        2.1,
        1.0e6,
        -0.8,
        0.0,
        1.7,
        0.0,
        0.0,
        0.0,
        0.2,
        0.0,
        0.0,
    ]
    radial = [index / max(nw - 1, 1) for index in range(nw)]
    fpol = [2.1 - 0.1 * value for value in radial]
    pressure = [1.0e5 * (1.0 - value * value) for value in radial]
    ffprim = [-0.2 * value for value in radial]
    pprime = [-2.0e5 * value for value in radial]
    psirz = [
        -0.8
        + 0.01 * math.sin(row * 0.1)
        + 0.2 * column / max(nw - 1, 1)
        for row in range(nh)
        for column in range(nw)
    ]
    qpsi = [1.0 + 3.0 * value * value for value in radial]
    boundary = [1.2, -1.2, 2.2, -1.2, 2.2, 1.2, 1.2, 1.2]
    limiter = [0.9, -1.5, 2.5, -1.5, 2.5, 1.5, 0.9, 1.5]
    body = _real_records(
        scalars + fpol + pressure + ffprim + pprime + psirz + qpsi
    )
    body += f"{4:5d}{4:5d}\n"
    body += _real_records(boundary + limiter)
    path.write_bytes((header + body).encode("ascii"))


def _make_afile(path: Path) -> None:
    lines = [
        " 01-Jan-00 00/00/0000",
        "      1               1",
        " " + _fortran_real(1.0),
        (
            "*"
            + f"{1.0:8.3f}"
            + " " * 9
            + f"{1:5d}"
            + " " * 11
            + f"{0:5d} LIM {3:3d} {1:3d} QMF {0:5d}{0:5d}"
        ),
    ]
    for record in range(6):
        lines.append(
            "".join(_fortran_real(record + offset) for offset in range(1, 5))
        )
    lines.extend(
        [
            _fortran_real(10.0)
            + _fortran_real(11.0)
            + _fortran_real(12.0)
            + _fortran_real(999.0),
            _fortran_real(13.0)
            + _fortran_real(14.0)
            + _fortran_real(15.0)
            + _fortran_real(999.0),
            _fortran_real(14.0),
            _fortran_real(15.0),
        ]
    )
    for record in range(6, 17):
        lines.append(
            "".join(_fortran_real(record + offset) for offset in range(1, 5))
        )
    lines.append(f" {1:5d}{1:5d}{0:5d}{0:5d}")
    lines.append(
        _fortran_real(21.0)
        + _fortran_real(22.0)
        + _fortran_real(999.0)
        + _fortran_real(999.0)
    )
    for record in range(15):
        lines.append(
            "".join(
                _fortran_real(100.0 + record * 4 + offset)
                for offset in range(4)
            )
        )
    path.write_bytes(("\n".join(lines) + "\n").encode("ascii"))


def _make_kfile(path: Path) -> None:
    values = ", ".join(f"{index / 16:.9E}" for index in range(4096))
    content = (
        "&IN1\n"
        "  SHOT = 1\n"
        "  TIME = 1.0D+00\n"
        "  FITDELZ = .TRUE.\n"
        "  DESCRIPTION = 'synthetic benchmark'\n"
        f"  PROFILE = {values}\n"
        "/\n"
    )
    path.write_bytes(content.encode("ascii"))


def _make_sfile(path: Path, rows: int) -> None:
    with path.open("w", encoding="ascii", newline="\n") as stream:
        stream.write("major radius\nvalue\nEQMDSK SYNTHETIC BENCHMARK\n")
        for index in range(rows):
            x = index / max(rows - 1, 1)
            stream.write(
                f"{x:.17g} {math.sin(x):.17g} "
                f"{1.0 / rows:.17g} {1.0e-6:.17g}\n"
            )


GENERATORS: dict[str, Callable[..., None]] = {
    "G": _make_gfile,
    "A": _make_afile,
    "K": _make_kfile,
    "S": _make_sfile,
}


def _find_fixture(format_name: str, data_dir: Path) -> Path | None:
    preferred = data_dir / f"{format_name.lower()}067590.03300"
    if preferred.is_file():
        return preferred
    candidates: list[Path] = []
    for pattern in (f"{format_name.lower()}*", f"{format_name.upper()}*"):
        candidates.extend(path for path in data_dir.glob(pattern) if path.is_file())
    return (
        min(set(candidates), key=lambda item: item.name) if candidates else None
    )


def _peak_rss_bytes() -> int | None:
    try:
        import resource

        maximum = int(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
        return maximum if sys.platform == "darwin" else maximum * 1024
    except (ImportError, OSError):
        pass

    if sys.platform == "win32":
        try:
            import ctypes
            from ctypes import wintypes

            class ProcessMemoryCounters(ctypes.Structure):
                _fields_ = [
                    ("cb", wintypes.DWORD),
                    ("PageFaultCount", wintypes.DWORD),
                    ("PeakWorkingSetSize", ctypes.c_size_t),
                    ("WorkingSetSize", ctypes.c_size_t),
                    ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                    ("PagefileUsage", ctypes.c_size_t),
                    ("PeakPagefileUsage", ctypes.c_size_t),
                ]

            get_current_process = ctypes.windll.kernel32.GetCurrentProcess
            get_current_process.restype = wintypes.HANDLE
            get_process_memory_info = ctypes.windll.psapi.GetProcessMemoryInfo
            get_process_memory_info.argtypes = [
                wintypes.HANDLE,
                ctypes.POINTER(ProcessMemoryCounters),
                wintypes.DWORD,
            ]
            get_process_memory_info.restype = wintypes.BOOL

            counters = ProcessMemoryCounters()
            counters.cb = ctypes.sizeof(counters)
            process = get_current_process()
            ok = get_process_memory_info(
                process, ctypes.byref(counters), counters.cb
            )
            return int(counters.PeakWorkingSetSize) if ok else None
        except (AttributeError, OSError):
            return None
    return None


def _summary(samples_ns: list[int]) -> dict[str, float]:
    values = [value / 1_000_000 for value in samples_ns]
    return {
        "min": round(min(values), 6),
        "median": round(statistics.median(values), 6),
        "mean": round(statistics.fmean(values), 6),
    }


def _run_worker(
    format_name: str,
    source: Path,
    target: Path,
    iterations: int,
    warmup: int,
) -> dict[str, Any]:
    import eqmdsk

    file_class = getattr(eqmdsk, FORMAT_CLASSES[format_name])
    measurements: dict[str, list[int]] = {
        "parse": [],
        "write": [],
        "reparse": [],
    }

    for iteration in range(warmup + iterations):
        started = time.perf_counter_ns()
        parsed = file_class(source)
        parsed_ns = time.perf_counter_ns() - started

        started = time.perf_counter_ns()
        parsed.write(target)
        write_ns = time.perf_counter_ns() - started

        started = time.perf_counter_ns()
        reparsed = file_class(target)
        reparsed_ns = time.perf_counter_ns() - started

        # Force access to the parsed mapping before releasing the objects.
        if not reparsed.keys():
            raise RuntimeError(f"{format_name}-file round-trip produced no fields")
        if iteration >= warmup:
            measurements["parse"].append(parsed_ns)
            measurements["write"].append(write_ns)
            measurements["reparse"].append(reparsed_ns)
        del parsed, reparsed
        gc.collect()

    peak = _peak_rss_bytes()
    return {
        "format": format_name,
        "bytes": source.stat().st_size,
        "iterations": iterations,
        "timings_ms": {
            operation: _summary(samples)
            for operation, samples in measurements.items()
        },
        "process_peak_rss_mib": (
            round(peak / (1024 * 1024), 3) if peak is not None else None
        ),
    }


def _positive(value: str) -> int:
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError("must be at least 1")
    return parsed


def _nonnegative(value: str) -> int:
    parsed = int(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError("must not be negative")
    return parsed


def _parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iterations", type=_positive, default=10)
    parser.add_argument("--warmup", type=_nonnegative, default=2)
    parser.add_argument("--synthetic-grid", type=_positive, default=129)
    parser.add_argument("--s-rows", type=_positive, default=10_000)
    parser.add_argument(
        "--data-dir",
        type=Path,
        help="fixture directory (default: the workspace-level ../data)",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        help="retain generated fixtures and round-trip files in this directory",
    )
    parser.add_argument(
        "--json", action="store_true", help="emit the complete result as JSON"
    )
    parser.add_argument("--_worker", nargs=5, help=argparse.SUPPRESS)
    return parser.parse_args()


def _print_text(report: dict[str, Any]) -> None:
    print(
        "format  source       bytes    parse ms   write ms  reparse ms  peak MiB"
    )
    for result in report["results"]:
        timings = result["timings_ms"]
        peak = result["process_peak_rss_mib"]
        peak_text = "n/a" if peak is None else f"{peak:.3f}"
        print(
            f"{result['format']:<7} {result['source_kind']:<11} "
            f"{result['bytes']:>8} "
            f"{timings['parse']['median']:>11.3f} "
            f"{timings['write']['median']:>10.3f} "
            f"{timings['reparse']['median']:>11.3f} "
            f"{peak_text:>9}"
        )
    print(
        f"iterations={report['config']['iterations']} "
        f"warmup={report['config']['warmup']} "
        f"python={report['environment']['python']} "
        f"eqmdsk={report['environment']['eqmdsk']}"
    )


def main() -> int:
    arguments = _parse_arguments()
    if arguments._worker:
        format_name, source, target, iterations, warmup = arguments._worker
        result = _run_worker(
            format_name,
            Path(source),
            Path(target),
            int(iterations),
            int(warmup),
        )
        print(json.dumps(result, sort_keys=True))
        return 0

    import eqmdsk

    repository = Path(__file__).resolve().parents[1]
    data_dir = (arguments.data_dir or repository.parent / "data").resolve()
    temporary: tempfile.TemporaryDirectory[str] | None = None
    if arguments.work_dir is None:
        scratch_root = repository / "build"
        scratch_root.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(
            prefix="eqmdsk-benchmark-", dir=scratch_root
        )
        work_dir = Path(temporary.name)
    else:
        work_dir = arguments.work_dir.resolve()
        work_dir.mkdir(parents=True, exist_ok=True)

    try:
        fixtures: dict[str, tuple[Path, str]] = {}
        for format_name in FORMAT_CLASSES:
            source = _find_fixture(format_name, data_dir)
            if source is not None:
                fixtures[format_name] = (source.resolve(), "workspace")
                continue
            source = work_dir / f"synthetic-{format_name.lower()}"
            if format_name == "G":
                _make_gfile(source, arguments.synthetic_grid)
            elif format_name == "S":
                _make_sfile(source, arguments.s_rows)
            else:
                GENERATORS[format_name](source)
            fixtures[format_name] = (source, "synthetic")

        results: list[dict[str, Any]] = []
        environment = os.environ.copy()
        environment["PYTHONHASHSEED"] = "0"
        for format_name, (source, source_kind) in fixtures.items():
            target = work_dir / f"roundtrip-{format_name.lower()}"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(Path(__file__).resolve()),
                    "--_worker",
                    format_name,
                    str(source),
                    str(target),
                    str(arguments.iterations),
                    str(arguments.warmup),
                ],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )
            if completed.returncode != 0:
                raise RuntimeError(
                    f"{format_name}-file benchmark failed:\n{completed.stderr}"
                )
            result = json.loads(completed.stdout)
            result["source"] = str(source)
            result["source_kind"] = source_kind
            results.append(result)

        report = {
            "schema_version": 1,
            "generated_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "environment": {
                "eqmdsk": eqmdsk.__version__,
                "python": platform.python_version(),
                "implementation": platform.python_implementation(),
                "platform": platform.platform(),
                "processor": platform.processor() or "unknown",
            },
            "config": {
                "iterations": arguments.iterations,
                "warmup": arguments.warmup,
                "synthetic_grid": arguments.synthetic_grid,
                "s_rows": arguments.s_rows,
                "data_dir": str(data_dir),
            },
            "results": results,
        }
        if arguments.json:
            print(json.dumps(report, indent=2, sort_keys=True))
        else:
            _print_text(report)
        return 0
    finally:
        if temporary is not None:
            temporary.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
