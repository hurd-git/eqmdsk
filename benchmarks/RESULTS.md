# I/O benchmark reference

This is a short local reference run, not a cross-machine performance
guarantee. It was recorded on 2026-08-22 (Asia/Shanghai) with eqmdsk 0.9.0.

## Environment

- Linux 6.17.0-14-generic, x86-64, glibc 2.39
- Intel Core i5-14600KF, 14 cores / 20 logical CPUs
- CPython 3.12.13 and NumPy 2.5.2
- GCC 13.3.0; the installed extension was built in Release mode

## Method

The Release extension was built in a clean CMake directory with the repository
virtual environment's Python interpreter. It was then loaded from a temporary
package directory so an editable Debug build could not affect the result. The
benchmark used five unmeasured warm-up rounds followed by thirty measured
rounds; each format ran in a fresh child process. Times are medians for complete
parse, write, and reparse operations. Peak RSS is the child process peak and
therefore includes CPython, NumPy, and the eqmdsk extension. Filesystem caches
were not cleared between rounds.

```console
cmake -S . -B build/benchmark-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DEQMDSK_BUILD_PYTHON=ON \
  -DEQMDSK_BUILD_TESTS=OFF \
  -DPython_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build/benchmark-release --parallel
bench_dir="$(mktemp -d)"
uv venv --python 3.12 "$bench_dir/venv"
uv pip install --python "$bench_dir/venv/bin/python" "numpy>=1.23"
site_packages="$("$bench_dir/venv/bin/python" -c \
  'import sysconfig; print(sysconfig.get_path("purelib"))')"
mkdir -p "$site_packages/eqmdsk"
cp python/eqmdsk/__init__.py python/eqmdsk/__init__.pyi \
  python/eqmdsk/py.typed "$site_packages/eqmdsk/"
cp build/benchmark-release/_core*.so "$site_packages/eqmdsk/"
"$bench_dir/venv/bin/python" benchmarks/benchmark_io.py \
  --iterations 30 --warmup 5 --data-dir data
rm -rf "$bench_dir"
```

This run includes the strict, locale-independent stream parser used for
floating-point input. It supersedes the earlier reference recorded before
that portability change.

| Format | Input | Bytes | Parse (ms) | Write (ms) | Reparse (ms) | Peak RSS (MiB) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| G | `data/g067590.03300` | 668,454 | 2.999 | 15.696 | 2.901 | 39.348 |
| A | `data/a067590.03300` | 4,462 | 0.135 | 0.315 | 0.105 | 33.105 |
| K | `data/k067590.03300` | 3,416 | 0.087 | 0.144 | 0.098 | 33.109 |
| S | synthetic, 10,000 rows | 700,286 | 3.520 | 7.537 | 3.287 | 35.340 |

Use `--json` for the versioned, machine-readable result, including minimum,
median, and mean timings. When a G-, A-, K-, or S-file is absent from the data
directory, the benchmark creates a deterministic synthetic fixture for that
format.
