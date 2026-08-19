# eqmdsk

`eqmdsk` is a lightweight C++17 and Python library for reading, editing, and
writing EFIT G-, A-, K-, and S-files. It keeps the compatibility-focused file
I/O parts of larger EFIT toolkits without equilibrium analysis, plotting, lazy
loading, databases, OMAS, MDSplus, SciPy, or other framework integrations.

Version `0.9.0` provides one C++ implementation shared by both public APIs.

## Python quick start

```python
import eqmdsk

gfile = eqmdsk.GFile("g123456.01234")  # reads immediately
print(gfile.keys())
print(gfile["PSIRZ"].shape)             # (NH, NW): rows are Z, columns are R

gfile["CURRENT"] = 1.2e6
gfile["PSIRZ"][0, 0] = -0.25           # writable zero-copy NumPy view
gfile.write()                            # exact original path
gfile.write("result.with-any-suffix")   # exact caller-provided path
```

There is no public `load()` or `read()` wrapper. Constructing `GFile`, `AFile`,
`KFile`, or `SFile` reads the complete file; `write()` serializes it in one
operation.

Standard field names use their canonical uppercase EFIT spelling and are
case-sensitive. K-file direct mapping, variable, and section lookup is the sole
exception, because Fortran namelist identifiers are case-insensitive. Its
generic `fields` property still exposes the canonical uppercase `FieldMap` and
therefore uses exact uppercase keys:

```python
kfile = eqmdsk.KFile("k123456.01234")
assert kfile["xlim"] is not None
print([section.name for section in kfile.sections])
print(kfile.entry("IN1", "XLIM").values)
```

G-files do not contain enough metadata to distinguish all COCOS conventions.
Detection therefore normally returns several candidates. Select only a
detected source before conversion:

```python
print(gfile.cocos.candidates)
gfile.select_cocos(5)
converted = gfile.to_cocos(11, inplace=False)
```

`to_cocos()` never guesses an ambiguous source. A `CocosError` carries the
original result in `error.result`.

## Array ownership

Numeric arrays are writable NumPy views of C++-owned Eigen storage. They have
no hidden conversion or copy, and the view keeps its file or `FieldMap` owner
alive. Whole-array assignment copies values into the existing storage and must
preserve shape and length. Resize in C++ follows normal C++ reference
invalidation rules; obtain a fresh NumPy view after any C++ resize.

```python
view = gfile["PSIRZ"]
assert view.flags.c_contiguous and view.flags.writeable
```

## Errors and preservation

The public exception hierarchy is `Error`, `IOError`, `ParseError`,
`ValidationError`, `FieldError`, and `CocosError`. Python `ParseError` instances
also expose `filename`, `line`, and `column`.

Unknown or non-schema input is retained without guessing its meaning:

- G-file preamble, header suffix, and extension tail;
- A-file header and footer;
- K-file ordering, repeated entries, comments, indexed/raw values, section
  spelling, terminators, and text outside namelists;
- S-file interstitial and trailing text.

Opaque Python properties such as `extra_header`, `extension_tail`, `header`,
`footer`, and `RawSection.data` are `bytes`, so embedded NULs and non-UTF-8
content are not decoded or lost.

`raw_sections` is a read-only view of those preserved regions. Unmodified
output is guaranteed to remain semantically equivalent after reparsing; exact
byte identity is intentionally required only where the implementation can
preserve it without making raw text a second editable state.

See [format and field details](docs/formats.md) and the
[compatibility contract](docs/compatibility.md).

## C++ use

```cpp
#include <eqmdsk/eqmdsk.hpp>

eqmdsk::GFile file("g123456.01234");
auto& psi = std::get<eqmdsk::DoubleMatrix>(file.at("PSIRZ"));
psi(0, 0) = -0.25;
file.write("result");
```

After installation, consume the exported static library with CMake:

```cmake
find_package(eqmdsk 0.9 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE eqmdsk::eqmdsk)
```

Eigen is part of the public header interface and must be discoverable as
`Eigen3::Eigen` by the consuming CMake project. The static library is a source
SDK artifact: build it with the consuming toolchain and a compatible Eigen
3.4+ installation. No cross-toolchain or cross-Eigen binary ABI is promised.
Python wheels deliberately do not install the C++ static library, headers, or
CMake package.

## Development build

Eigen and pybind11 may be provided in `extern/eigen` and `extern/pybind11` for
local development. Their source trees are intentionally excluded from Git and
source distributions. System installations are supported. Python builds may
fetch the pinned Eigen release when neither is present; C++-only builds require
an explicit opt-in to network fetching.

```console
uv venv --python 3.12
uv pip install --python .venv/bin/python -e '.[test]'
.venv/bin/python -m pytest
```

For C++ only:

```console
cmake -S . -B build \
  -DEQMDSK_BUILD_PYTHON=OFF \
  -DEQMDSK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix build/prefix --component Development
```

Ordinary CMake configuration defaults to a C++-only build. A scikit-build-core
Python package build enables the extension automatically. Missing Eigen may be
downloaded only when `EQMDSK_FETCH_DEPENDENCIES=ON`; the fallback is pinned and
checksum-verified.

The default tests are offline. An additional checksum-pinned MIT-licensed G/A
compatibility corpus can be fetched explicitly:

```console
.venv/bin/python tests/public_data/fetch.py --output build/public-data
EQMDSK_PUBLIC_FIXTURE_DIR=build/public-data \
  .venv/bin/python -m pytest tests/python/test_public_fixtures.py
```

Run `benchmarks/benchmark_io.py` for repeatable whole-file parse/write/reparse
timings and process peak RSS. A reference run is recorded in
[benchmarks/RESULTS.md](benchmarks/RESULTS.md).

Long-running parser fuzzing is documented in
[tests/fuzz/README.md](tests/fuzz/README.md). Release verification is described
in [docs/releasing.md](docs/releasing.md).

## License

eqmdsk is MIT licensed. Build-time and public-header dependencies retain their
own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
