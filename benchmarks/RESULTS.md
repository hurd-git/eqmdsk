# I/O benchmark reference

This is a short local reference run, not a cross-machine performance
guarantee. It was recorded on 2026-08-22 (Asia/Shanghai) with eqmdsk 0.9.0.

## Environment

- Linux 6.17.0-14-generic, x86-64, glibc 2.39
- Intel Core i5-14600KF, 14 cores / 20 logical CPUs
- CPython 3.12.13 and NumPy 2.5.2
- GCC 13.3.0; the installed extension was built in Release mode

## Method

The command below used five unmeasured warm-up rounds followed by thirty
measured rounds. Each format ran in a fresh child process. Times are medians for
complete parse, write, and reparse operations; peak RSS is the child process
peak and therefore includes CPython, NumPy, and the eqmdsk extension.
Filesystem caches were not cleared between rounds.

```console
.venv/bin/python benchmarks/benchmark_io.py \
  --iterations 30 --warmup 5 \
  --data-dir data
```

This run includes the strict, locale-independent stream parser used for
floating-point input. It supersedes the earlier reference recorded before
that portability change.

| Format | Input | Bytes | Parse (ms) | Write (ms) | Reparse (ms) | Peak RSS (MiB) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| G | `data/g067590.03300` | 668,454 | 27.940 | 22.031 | 25.955 | 36.664 |
| A | `data/a067590.03300` | 4,462 | 0.608 | 0.465 | 0.575 | 35.047 |
| K | `data/k067590.03300` | 3,416 | 0.943 | 0.309 | 1.335 | 34.895 |
| S | synthetic, 10,000 rows | 700,286 | 23.728 | 9.926 | 23.449 | 36.801 |

Use `--json` for the versioned, machine-readable result, including minimum,
median, and mean timings. When a G-, A-, K-, or S-file is absent from the data
directory, the benchmark creates a deterministic synthetic fixture for that
format.
