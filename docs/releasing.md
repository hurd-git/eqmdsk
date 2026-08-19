# Release checklist

Publishing is intentionally separate from normal development. The current
project policy permits local Git commits and local package builds, but no remote
push or PyPI upload without explicit authorization.

1. Update the version in `CMakeLists.txt`, `pyproject.toml`, and
   `include/eqmdsk/version.hpp`; move the CHANGELOG entry out of Unreleased.
2. Confirm `git status --short` is empty and all intended files are tracked.
3. Run the GCC C++ suite, Python 3.9–3.14 suite, ASan/UBSan suite, and—where
   available—the Clang libFuzzer smoke run.
4. Install the C++ artifacts to an empty prefix and build
   `tests/consumer` using only `find_package(eqmdsk CONFIG REQUIRED)`.
5. Build a wheel and sdist from a tree with `extern/` unavailable. Build a
   second wheel from the sdist.
6. Inspect archives: no `extern`, `.venv`, build tree, downloaded fixtures, or
   cache; MIT and complete third-party notices must be present. Confirm that
   wheels contain no C++ SDK artifacts.
7. Install the wheel into a clean virtual environment and run import/version
   plus the packaged behavior smoke tests.
8. Run package metadata validation and verify `Requires-Python`, NumPy
   dependency, classifiers, and license expression.
9. Execute the supported OS/Python CI matrix and the cibuildwheel matrix. Linux
   release wheels must have repaired manylinux tags; build both Intel and ARM
   macOS wheels and the Windows x64 wheels.
10. Fetch and checksum the optional public compatibility corpus, then run its
    parse/write/parse tests. Archive an updated benchmark result when behavior
    or parsing performance changed materially.
11. Create a signed/tagged local release checkpoint. Remote upload remains a
    separate explicitly authorized action.

Recommended local commands:

```console
uv build --wheel --sdist
uv venv build/release-venv
uv pip install --python build/release-venv/bin/python dist/eqmdsk-0.9.0-*.whl
build/release-venv/bin/python -c "import eqmdsk; print(eqmdsk.__version__)"
```

The `uv build` wheel above verifies local package composition only. Build
publishable wheels with the checked-in cibuildwheel workflow; it performs the
platform repair/audit step and tests each resulting wheel. Neither workflow
uploads to PyPI.
