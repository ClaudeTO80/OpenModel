# Contributing

Thanks for your interest in OpenModel.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOPENMODEL_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

C++ tests use `assert`; `NDEBUG` is disabled for them even in Release builds. The WebGL/GUI tests are optional, see
[`tests/webgl/README.md`](tests/webgl/README.md).

## Guidelines

- Language: C++17 (the code base does not use C++23 features yet). Source code and comments in English, Doxygen style (`/** ... */`, `@param`, `@return`).
- Public headers live in `include/OpenModel/`, implementations in `src/`. Optional backends stay behind their `OPENMODEL_BUILD_*_BACKEND` option.
- Every behaviour change comes with a test. When a test fails, first decide whether the test or the code is wrong, and say which in the pull request.
- Do not claim something works unless it was compiled and run; list in the pull request what was **not** verified.
- Keep MGMB/MGWV backward compatible (see `docs/MGMB_FORMAT.md`, `docs/MGWV_FORMAT.md`); new data goes in optional chunks.
