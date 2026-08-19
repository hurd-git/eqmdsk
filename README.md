# eqmdsk

`eqmdsk` is a lightweight C++17 and Python library for reading, editing, and
writing EFIT G-, A-, K-, and S-files. It focuses on file compatibility and
preservation, without equilibrium analysis, plotting, lazy loading, database
connections, OMAS, or MDSplus integration.

The project is currently under active development toward version 0.9.0.

## Development build

Eigen and pybind11 may be provided in `extern/eigen` and `extern/pybind11` for
local development. These third-party source trees are intentionally excluded
from the project repository. System installations are also supported. Python
package builds may fetch the pinned Eigen 5.0.1 release when neither source is
available; C++-only builds require an explicit
`-DEQMDSK_FETCH_DEPENDENCIES=ON` to enable network fetching.

```console
uv venv --python 3.12
uv pip install --python .venv/bin/python -e '.[test]'
.venv/bin/python -m pytest
```

For a C++-only build:

```console
cmake -S . -B build -DEQMDSK_BUILD_PYTHON=OFF -DEQMDSK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

MIT
