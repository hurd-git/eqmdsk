# Compatibility contract

## Toolchain and platform targets

| Component | Supported target | Locally verified for 0.9.0 |
| --- | --- | --- |
| C++ | C++17; GCC 11+, Clang 14+, AppleClang 14+, MSVC 2022 | GCC 13.3 |
| CMake | 3.18+ | 3.28 |
| Python | CPython 3.9–3.14 | CPython 3.9.25–3.14.6 on Linux |
| NumPy | 1.23+ | 2.5.2 |
| Eigen | 3.4+ or pinned 5.0.1 fallback | 5.0.1 |
| pybind11 | 2.12+ build dependency | 3.1.0 |
| OS | Linux, macOS, Windows | Linux x86-64 |

The non-Linux rows are portability targets exercised by the checked-in CI and
wheel matrices once the repository is hosted; they are not claims of local
execution in this local-only repository. The Python metadata intentionally
states the minimum (`>=3.9`) rather than rejecting future CPython versions;
versions newer than the tested range are not guaranteed until added to CI.
Expanding the matrix must not add format-specific compatibility layers.

## Stable 0.9 API surface

The compatibility focus is constructors, exact path writing, mapping access,
the error hierarchy, G-file COCOS behavior, ordered K-file inspection, and the
documented field names/shapes. Correctness fixes may still make small source
changes before 1.0. No cross-compiler or cross-standard-library C++ ABI promise
is made before 1.0; the installed static library and headers should be rebuilt
with the consuming toolchain. Because Eigen types occur in the public API, the
consumer must also use a compatible Eigen 3.4+ installation. Official Python
wheel builds use the checksum-pinned Eigen fallback and do not distribute the
C++ SDK artifacts.

## Distribution boundary

- Source archives contain everything needed to build the Python extension or
  the C++ SDK, but do not contain the local `extern/` dependency trees.
- Python wheels contain the Python package, extension, and license material;
  they intentionally omit headers, static libraries, and CMake package files.
- `cmake --install ... --component Development` installs the C++ headers,
  static library, CMake package, and notices from a source build.
- Cibuildwheel configuration produces repaired manylinux wheels and native
  macOS/Windows wheels for each supported CPython ABI. A plain local Linux
  wheel is a packaging test artifact, not a PyPI release artifact.

## Deliberate boundaries

- Files are read and written as complete byte sequences. There is no mmap,
  streaming, lazy loading, or cache.
- Serialization and schema validation finish before the destination is opened,
  and close-time I/O failures are reported. Replacement is not transactional:
  a device or filesystem failure during output can leave a partial file.
- Unknown input is preserved, but newly inventing unknown format syntax is not
  a public API. K-file `set()` modifies existing entries only.
- `raw_sections` is read-only. Format-specific private state is the authority
  used during serialization.
- Semantic parse/write/parse equivalence is required. Universal byte-for-byte
  identity is not; line endings and numeric formatting may be canonicalized.
- Python whole-array assignment preserves shape. C++ resize invalidates prior
  references/views in the normal C++ manner.
- Standard fields are case-sensitive and have no aliases. K-file identifiers
  remain case-insensitive according to Fortran namelist rules.
- COCOS detection/conversion applies only to G-files and never guesses an
  ambiguous source.
- There is no equilibrium analysis, derived coordinate/grid calculation,
  plotting, database, OMAS, or MDSplus integration.

## Resource behavior

Dimensions and counts are checked before multiplication and allocation and are
bounded by available file data. G-file output dimensions use four-character
fields; boundary counts use five. K-file convenience expansion is capped at ten
million effective values and 64 MiB of projected string storage per file,
while the compressed ordered representation remains available. File sizes must
fit both `size_t` and `streamsize`.
