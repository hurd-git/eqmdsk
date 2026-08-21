# Release checklist

Publishing is intentionally separate from normal development. A release is
created from one semantic-version tag such as `v0.9.0`. The checked-in release
workflow builds the sdist and the complete cibuildwheel matrix, verifies that
every artifact has the same version, and uploads a combined artifact for review.
It does not publish anything by default. A future, explicit manual dispatch on
the tag with `publish=true` will publish those exact files to PyPI and then
create the matching GitHub Release with the same files. It never publishes from
`main` without a tag.

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
   wheels contain no C++ SDK artifacts and that the sdist, direct wheel, and
   sdist-rebuilt wheel all contain `__init__.pyi` and an empty `py.typed`.
7. Install the wheel into a clean virtual environment and run import/version
   plus the packaged behavior smoke tests. Run the checked-in strict mypy usage
   check and `mypy.stubtest` against that installed wheel.
8. Run package metadata validation and verify `Requires-Python`, NumPy
   dependency, classifiers, and license expression.
9. Execute the supported OS/Python CI matrix and the cibuildwheel matrix. Linux
   release wheels must have repaired manylinux tags; build both Intel and ARM
   macOS wheels and the Windows x64 wheels.
10. Fetch and checksum the optional public compatibility corpus, then run its
    parse/write/parse tests. Archive an updated benchmark result when behavior
    or parsing performance changed materially.
11. Create and push the release tag only after all checks pass. The tag workflow
    only prepares and uploads artifacts; it does not publish PyPI or create a
    Release.
12. Before the first real release, configure a PyPI Trusted Publisher for this
    repository and `.github/workflows/wheels.yml` with the
    `pypa/gh-action-pypi-publish` action. Then manually dispatch the workflow on
    the exact `vX.Y.Z` tag with `publish=true`. The publishing job requires
    `contents: write` and `id-token: write`; no long-lived PyPI token is stored
    in the repository.
13. Inspect the completed publishing workflow and confirm that the PyPI version
    and GitHub Release tag are identical. It publishes PyPI first, so a failed
    PyPI publication does not create a public GitHub Release.

Recommended local commands:

```console
uv build --wheel --sdist
uv venv build/release-venv
uv pip install --python build/release-venv/bin/python dist/eqmdsk-0.9.0-*.whl
build/release-venv/bin/python -c "import eqmdsk; print(eqmdsk.__version__)"
```

The `uv build` wheel above verifies local package composition only. Build
publishable wheels with the checked-in cibuildwheel workflow; it performs the
platform repair/audit step and tests each resulting wheel. A tag push invokes
the preparation workflow only. The PyPI and GitHub Release actions are gated by
the explicit `workflow_dispatch` `publish=true` input described above.
