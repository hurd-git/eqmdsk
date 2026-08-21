# Changelog

All notable changes follow Keep a Changelog conventions. Version numbers follow
Semantic Versioning, with the documented pre-1.0 compatibility allowance.

## [Unreleased]

- Continue compatibility validation with additional licensed real-world files
  and platform CI runs.
- Add per-format G/A/K/S usage guides and a shared Python API guide.
- Ship PEP 561 type information with field-specific G/A/S lookup overloads and
  complete K-file ordered-model signatures.
- Fix cross-platform CI coverage: wheel archive path checks, Windows test stream
  lifetime, and portable subnormal floating-point parsing.
- Provide the project README in Chinese while retaining the complete Python,
  C++, build, compatibility, and licensing guidance.

## [0.9.0] - 2026-08-20

### Added

- C++17 and Python APIs for EFIT G-, A-, K-, and S-file parse/edit/write.
- Canonical EFIT fields, writable zero-copy NumPy arrays, ordered raw sections,
  structured errors, and CMake install/export support.
- G-file COCOS detection, explicit source selection, and conversion across the
  16 standard numbered conventions.
- A-file fixed/dynamic records and all 15 optional record groups.
- Ordered K-file namelists with duplicates, comments, null/repeated/indexed/raw
  values, multiple sections, and block-external binary text.
- Deterministic mutation smoke tests, sanitizer tests, and optional libFuzzer
  targets.
- Linux/macOS/Windows CI, repaired multi-architecture wheel configuration,
  source/C++ consumer checks, and a whole-file I/O benchmark.
- A checksum-pinned, explicitly downloaded MIT-licensed public G/A
  compatibility corpus.

### Security

- Checked dimension/count multiplication, bounded K-file value expansion, and
  checked file-size conversions before allocation or stream operations.
- Separate Python-wheel and source C++ SDK distribution boundaries, including
  complete Eigen and pybind11 license material.
