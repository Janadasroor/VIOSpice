# Contributing to VioraEDA

Thank you for your interest in contributing to VioraEDA! This document provides guidelines and instructions for contributing to this open-source EDA (Electronic Design Automation) project.

## Build Requirements

| Tool       | Minimum Version | Notes                          |
|------------|-----------------|--------------------------------|
| GCC        | 13+             | C++20 support required         |
| Clang      | 17+             | Alternative to GCC             |
| Qt         | 6.10+           | Core framework                 |
| LLVM       | 19+             | FluxScript JIT backend         |
| CMake      | 3.21+           | Build system                   |
| Python     | 3.10+           | Optional, for scripting/tests  |
| ccache     | Latest           | Recommended for faster builds  |
| mold       | Latest           | Recommended linker             |

## Build Instructions

### Clone and Configure

```bash
git clone https://github.com/VioraEDA/VioraEDA.git
cd VioraEDA
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=mold"
```

### Build

```bash
cmake --build build -j8
```

### Run Tests

```bash
ctest --test-dir build -j8 --output-on-failure
```

### Debug Build

```bash
cmake -B build-debug -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=mold"
cmake --build build-debug -j8
```

## Code Style

VioraEDA follows a consistent code style enforced by `.clang-format` at the repository root. Run `clang-format` before submitting code:

```bash
clang-format -i src/**/*.cpp src/**/*.h
```

### Summary of Conventions

- **Indentation**: Tabs (width 4)
- **Column limit**: 120 characters
- **Braces**: Allman style (opening brace on its own line)
- **Classes / Structs / Enums**: `CamelCase` — e.g. `CircuitNode`, `SimulationEngine`
- **Functions / Methods**: `camelBack` — e.g. `runSimulation()`, `parseNetlist()`
- **Member variables**: `m_` prefix — e.g. `m_nodeCount`, `m_frequency`
- **Pointers and references**: Left-aligned — `int* ptr`, `const QString& name`
- **Includes**: Sorted case-insensitively, existing grouping preserved

See [`.clang-format`](.clang-format) for the full machine-readable specification.

### Additional Guidelines

- Prefer `auto` only when the type is obvious from context.
- Use `nullptr` instead of `NULL` or `0`.
- Prefer range-based `for` loops.
- Use C++20 features (concepts, ranges, `std::format`, designated initializers) where they improve clarity.
- Mark classes as `final` unless they are explicitly designed for inheritance.

## Pull Request Process

1. **Fork and branch**: Create a feature branch from `main` with a descriptive name:
   - `feature/add-transient-analysis`
   - `fix/netlist-parser-crash`
   - `docs/update-api-reference`

2. **Keep commits focused**: Each commit should represent a single logical change. Write clear, imperative-mood commit messages:
   ```
   Add frequency sweep to AC analysis engine

   Implement logarithmic and linear frequency sweep modes for the AC
   analysis engine. Adds UI controls for sweep configuration.
   ```

3. **Test your changes**: Ensure all existing tests pass and add new tests for new functionality.

4. **Update documentation**: If your change affects public APIs or user-facing behavior, update the relevant documentation.

5. **Open a PR**: Submit your pull request against `main`. Fill out the PR template completely. Link any related issues.

6. **Code review**: At least one maintainer must approve the PR before merging. Address all review feedback.

7. **CI must pass**: All CI checks (build, tests, linting) must be green before merge.

## Bug Reporting

When filing a bug report, please include:

- **VioraEDA version** (commit hash or release tag)
- **Operating system** and version
- **Qt version** and compiler version
- **Steps to reproduce** the issue
- **Expected behavior** vs. **actual behavior**
- **Minimal netlist or project file** that triggers the bug, if applicable
- **Console output / stack trace** if available

Use the GitHub issue tracker and apply the `bug` label. Check existing issues before filing a duplicate.

## Architecture Documentation

For understanding the codebase before contributing, refer to:

- [`simulator/ARCHITECTURE.md`](simulator/ARCHITECTURE.md) — Overview of the simulation engine architecture, SPICE solver pipeline, and device model integration.
- [`docs/EXTENSION_API.md`](docs/EXTENSION_API.md) — API reference for writing FluxScript extensions and plugins.

## License

VioraEDA is licensed under the [Apache License 2.0](LICENSE). By contributing, you agree that your contributions will be licensed under the same license.

All new source files must include the standard Apache 2.0 header comment. See existing files for the exact format.
