# Parser fuzzing

The default test suite includes deterministic byte-mutation smoke tests for all
four formats. For longer sanitizer-backed fuzzing, configure with Clang:

```console
cmake -S . -B build/fuzz \
  -DEQMDSK_BUILD_PYTHON=OFF \
  -DEQMDSK_BUILD_TESTS=OFF \
  -DEQMDSK_BUILD_FUZZERS=ON \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build build/fuzz
build/fuzz/eqmdsk_fuzz_g -max_total_time=60 corpus/g
```

Targets `eqmdsk_fuzz_a`, `eqmdsk_fuzz_k`, and `eqmdsk_fuzz_s` exercise the
other parsers. Corpus and artifact directories should remain outside source
control unless a minimized input becomes a permanent regression fixture.
