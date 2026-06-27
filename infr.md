# VioraEDA Infrastructure Improvement Plan

> **Analysis date:** 2026-06-27  
> **Codebase:** ~200k C++ LOC, 1,050 source files, CMake 3.18 / C++20 / Qt6.10, 3-OS (Linux/macOS/Windows)  
> **Purpose:** Grounded, actionable recommendations to harden, speed, and professionalize the project infrastructure.

---

## Table of Contents

1. [Current State Assessment](#1-current-state-assessment)
2. [Build System](#2-build-system)
3. [CI/CD Pipelines](#3-cicd-pipelines)
4. [Testing Infrastructure](#4-testing-infrastructure)
5. [Packaging & Distribution](#5-packaging--distribution)
6. [Code Quality & Tooling](#6-code-quality--tooling)
7. [Repo Hygiene](#7-repo-hygiene)
8. [Rollout Roadmap](#8-rollout-roadmap)

---

## 1. Current State Assessment

### 1.1 Project Snapshot

| Metric | Value | Evidence |
|---|---|---|
| Code lines (C++) | ~200,000 | `scripts/count_loc.py` |
| Source files | 1,050 | Same |
| Major modules | schematic (421), core (97), ui (74), simulator (54), pcb (158), symbols (28), footprints (22) | `find` per subdirectory |
| Build system | CMake 3.18+, C++20, AUTOMOC/AUTOUIC/AUTORCC | Root `CMakeLists.txt` |
| Qt version | 6.10+ (6.5 min) | `CMakeLists.txt` line 67 |
| External deps | VioMATRIXC, FluxScript, Slang, nanobind, LLVM, Python | FetchContent + `ci/build-*.yml` |
| CI workflows | 3 active (`.github/workflows/`), 2 stranded (`ci/`) | `ls .github/workflows/` vs `ls ci/` |
| Test framework | QTest + custom `add_test` + pytest (unconfigured) | `tests/CMakeLists.txt`, grep `add_test` |
| Test count | ~8 `add_test` calls across 5 CMakeLists | grep `add_test` |
| Packaging | tar.gz/zip per-OS, manual AppDir | `release.yml` |
| Package format | None native (no deb/rpm/AppImage/dmg/msi automation) | — |

### 1.2 Key Strengths
- Multi-platform build matrix (ubuntu-24.04, macos-15, windows-2022) is well-structured.
- `FetchContent` for third-party deps provides reproducible builds.
- `ccache` integration speeds rebuilds on Linux/macOS.
- Prebuilt engine binary downloads save new developers from building VioMATRIXC from source.
- Well-organized RPATH handling via `vioraeda_setup_rpath()`.

### 1.3 Key Pain Points (summarized)
| # | Issue | Severity | Location |
|---|---|---|---|
| P1 | ~160 MB of build artifacts/tool binaries **committed in git** | 🔴 | `AppDir/`, `linuxdeploy`, `appimagetool` |
| P2 | Two CI workflows stranded in `ci/` — never run | 🔴 | `ci/build-fluxscript.yml`, `ci/build-viomatrixc.yml` |
| P3 | Root CMakeLists.txt is 25 KB — tests defined inline with monolithic boilerplate | 🟠 | `CMakeLists.txt` lines 463+ |
| P4 | Windows DLL resolution bash copy-pasted across 3 workflows | 🟠 | `build.yml`, `release.yml`, `test-release.yml` |
| P5 | No `CMakePresets.json` — CI flags passed ad-hoc | 🟠 | All CI workflows |
| P6 | Smoke tests use sleep+kill heuristic (exit>=128 treated as PASS) | 🟠 | `release.yml` lines 162-200 |
| P7 | No static analysis, sanitizers, or coverage in CI | 🟠 | No `.clang-tidy`, no sanitizer job |
| P8 | Nanobind has dual source (git submodule + FetchContent) | 🟠 | `.gitmodules` + `CMakeLists.txt` line 232 |
| P9 | Prebuilt engine/FluxScript downloads have no checksum verification | 🟠 | `file(DOWNLOAD ...)` in CMakeLists |
| P10 | No .clang-format, .clang-tidy, .editorconfig, CONTRIBUTING.md | 🟡 | Absent files |
| P11 | C++ tests gated behind `VIOSPICE_ENABLE_PYTHON` | 🟡 | `tests/CMakeLists.txt` line 3 |
| P12 | Python tests exist but no pytest configuration | 🟡 | No `pytest.ini`/`pyproject.toml` |
| P13 | No Dependabot, CODEOWNERS, or ISSUE_TEMPLATE | 🟡 | `.github/` |
| P14 | 80+ MB of .LIB test data committed | 🟡 | `tests/DATA/LIBRARY/` |

---

## 2. Build System

### 2.1 Action: Add `CMakePresets.json`

**Problem:** Each CI workflow passes configure flags manually and inconsistently.

| Workflow | Flags |
|---|---|
| Linux (build.yml) | `-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DMI_OVERRIDE=OFF` |
| macOS (build.yml) | same + `-DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm` |
| Windows (build.yml) | `-DVIOSPICE_BUILD_FLUXSCRIPT=ON -DSLANG_USE_MIMALLOC=OFF -G "MSYS Makefiles"` |
| Linux (release.yml) | same as build.yml Linux |

**Solution:** Create `CMakePresets.json` at repo root with named presets so CI becomes a one-liner.

**Presets to define:**
- `release` — default CI (ccache, Unix Makefiles, Release)
- `dev` — inherits release + `VIOSPICE_DEV_MODE=ON` (builds engine from source)
- `debug` — inherits release + Debug config
- `asan` — inherits release + ASan/UBSan flags for test hardening
- `msys2-mingw64` — Windows/MinGW generator + Windows-specific flags

**CI usage:**
```yaml
- name: Configure
  run: cmake --preset release
- name: Build
  run: cmake --build --preset release --parallel $(nproc)
- name: Test
  env: { QT_QPA_PLATFORM: offscreen }
  run: ctest --preset unit
```

This eliminates all ad-hoc flag repetition across `build.yml`, `release.yml`.

### 2.2 Action: Decompose monolithic root `CMakeLists.txt`

**Problem:** Root `CMakeLists.txt` (25 KB, 520+ lines) mixes:
- Engine acquisition logic (VioMATRIXC dev/prebuilt, lines 84–208)
- Nanobind bindings (lines 228–285)
- FluxScript download/build (lines 288–447)
- **Inline test executables** (lines 463–520+)
- Module `add_subdirectory` calls (lines 454–462)

**Solution:** Split into focused `cmake/` modules:
```
cmake/
├── policies.cmake                     ← CMP0111, other policy blocks
├── optimizations.cmake                ← -ffunction-sections, --gc-sections
├── dependencies/
│   ├── slang.cmake                    ← FetchContent slang
│   ├── viomatrixc.cmake               ← dev mode / prebuilt selection
│   ├── nanobind.cmake                 ← VioSolverCore + binding targets
│   └── fluxscript.cmake               ← download / build FluxScript
├── python.cmake                       ← find_package Python3 embedding
├── target_settings.cmake              ← existing (keep, add helper)
└── project_sources.cmake              ← existing (keep)
```

Each module's tests should live in that module's `CMakeLists.txt`:
```cmake
# schematic/CMakeLists.txt
if(BUILD_TESTING)
    vioraeda_add_test(test_spice_directive_netlist
        SOURCES tests/test_spice_directive_netlist.cpp
        LINK_LIBS VioSchematic VioSymbols VioSimulator VioCore VioUI
    )
endif()
```

### 2.3 Action: Add test-registration helper

**Problem:** Each test repeats ~12 lines of boilerplate in CMake.

**Solution:** Add `vioraeda_add_test()` to `cmake/target_settings.cmake`:

```cmake
function(vioraeda_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LINK_LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    set_target_properties(${name} PROPERTIES AUTOMOC ON)
    target_include_directories(${name} PRIVATE ${VIORAEDA_COMMON_INCLUDE_DIRS})
    target_link_libraries(${name} PRIVATE ${ARG_LINK_LIBS} Qt${QT_VERSION_MAJOR}::Test)
    vioraeda_setup_rpath(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()
```

### 2.4 Action: Resolve nanobind dual source

**Problem:** `ext/nanobind` git submodule (`.gitmodules`) AND `FetchContent_Declare(nanobind ...)` in CMake both reference the same library — they can diverge.

**Solution:** Remove the submodule, keep only FetchContent with a pinned tag:
```bash
git submodule deinit -f ext/nanobind
git rm -f ext/nanobind
rm -rf .git/modules/ext/nanobind
```
```cmake
FetchContent_Declare(nanobind
    GIT_REPOSITORY https://github.com/wjakob/nanobind.git
    GIT_TAG        v2.0.0
)
```

### 2.5 Action: Add checksum verification for prebuilt downloads

**Problem:** `file(DOWNLOAD ...)` for VioMATRIXC and FluxScript archives has no integrity check — a compromised artifact silently ships in every binary.

**Solution:** Add `EXPECTED_HASH` in release builds:
```cmake
file(DOWNLOAD "${VIOMATRIXC_DOWNLOAD_URL}"
     "${_vm_archive}"
     EXPECTED_HASH SHA256=<publish-per-release>
     STATUS _dl_status)
```
For dev mode (downloading from `main` branch), skip hash since the content is volatile. Document the hash in the release notes so users can verify.

---

## 3. CI/CD Pipelines

### 3.1 Action: Move stranded `ci/*.yml` into `.github/workflows/`

**Problem:** `ci/build-fluxscript.yml` and `ci/build-viomatrixc.yml` are **not** in `.github/workflows/` — GitHub Actions never executes them. The automated builds of prebuilt engine/FluxScript artifacts are dead.

**Solution:**
```bash
git mv ci/build-fluxscript.yml .github/workflows/build-fluxscript.yml
git mv ci/build-viomatrixc.yml .github/workflows/build-viomatrixc.yml
git rm -r ci/
```

Verify they trigger on pushes to `main` and tag pushes `v*`. The CI for VioMATRIXC already builds and packages the tarball via `softprops/action-gh-release` — confirm the release URL in `CMakeLists.txt` (`https://github.com/.../releases/download/v0.1.0/...`) matches what these workflows produce.

### 3.2 Action: DRY up Windows DLL resolution

**Problem:** The same ~50-line bash (recursive `objdump` → copy → verify loop) is duplicated verbatim in `build.yml` (lines ~214–242), `release.yml` (lines ~240–274), and `test-release.yml` (lines ~59–116). Any fix must touch 3 copies.

**Solution:** Extract into a reusable composite action `.github/actions/resolve-dlls/action.yml`:

```yaml
name: Resolve Windows DLLs
description: Recursively copy MinGW DLL dependencies
inputs:
  build-dir:
    required: true
    description: Path to CMake build output
runs:
  using: composite
  steps:
    - shell: msys2 {0}
      working-directory: ${{ inputs.build-dir }}
      run: |
        set -e; shopt -s nocasematch
        SCANNED=(); QUEUE=()
        # ... full recursive objdump/copy logic, parameterized
```

Usage:
```yaml
- name: Resolve DLLs
  uses: ./.github/actions/resolve-dlls
  with:
    build-dir: ${{ github.workspace }}/build
```

### 3.3 Action: Replace sleep-and-kill smoke tests with real CLI tests

**Current pattern (in release.yml lines 162–200):**
```bash
QT_QPA_PLATFORM=offscreen ./build/VioraEDA &
pid=$!; sleep 8; kill $pid 2>/dev/null; wait $pid; rc=$?
[ $rc -eq 0 ] || [ $rc -ge 128 ]  # "pass" on signal-kill
```
This masks crashes (signal-exit ≥128 is treated as PASS).

**Replacement:** Use the headless `viora` CLI which already exists:
```bash
./viora --version                                    # quick binary-load check
./viora --help                                       # exercises QApplication path
./viora netlist-run tests/circuits/test.cir --analysis op  # actual engine smoke
```
This pattern is already proven in `test-release.yml` (Test 5, lines 153–170). Promote it to `build.yml` and `release.yml`.

### 3.4 Action: Add sanitizer + clang-tidy CI jobs

**Problem:** No ASan, UBSan, TSan, or clang-tidy runs in CI despite local `build-asan/`/`build-tsan/` directories existing in `.gitignore`.

**Solution:** Add a Linux matrix entry in `build.yml`:

```yaml
sanitizer:
  runs-on: ubuntu-24.04
  steps:
    - uses: actions/checkout@v4
    - name: Install deps (Qt, LLVM-21, Python, ...)
      run: |  # copy dep installation from the main build job
    - name: Configure with ASan
      run: cmake --preset asan
    - name: Build
      run: cmake --build --preset asan --parallel $(nproc)
    - name: Test
      env: { QT_QPA_PLATFORM: offscreen }
      run: ctest --preset unit
```

Add a lightweight clang-tidy job (once `.clang-tidy` exists — see section 6):
```yaml
tidy:
  runs-on: ubuntu-24.04
  steps:
    - uses: actions/checkout@v4
    - name: Configure
      run: cmake --preset release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    - name: Run clang-tidy
      run: run-clang-tidy -p build -j$(nproc) -quiet
```

### 3.5 Action: Add code coverage reporting

**Solution:** Add a coverage job that builds with Clang's source-based coverage, runs tests, generates a report, and uploads to Codecov:

```yaml
coverage:
  runs-on: ubuntu-24.04
  steps:
    - name: Configure
      run: cmake --preset release -DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping" -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping"
    - name: Build + Test
      run: |
        cmake --build --preset release --parallel $(nproc)
        LLVM_PROFILE_FILE="coverage-%p.profraw" ctest --preset unit
    - name: Generate coverage report
      run: |
        llvm-profdata merge -sparse coverage-*.profraw -o coverage.profdata
        llvm-cov show --instr-profile=coverage.profdata build/VioraEDA > coverage.txt
        llvm-cov report --instr-profile=coverage.profdata build/VioraEDA
    - name: Upload to Codecov
      uses: codecov/codecov-action@v5
      with:
        files: ./coverage.txt
```

### 3.6 Action: Cache Qt installations across workflows

**Problem:** Every job runs `aqt install-qt` (~3–5 min). With 5+ jobs per push, this adds 15–25 min of wasted CI time.

**Solution:** Key the Qt cache on the Qt version + OS:

```yaml
- name: Cache Qt
  uses: actions/cache@v4
  id: cache-qt
  with:
    path: ${{ runner.temp }}/Qt
    key: ${{ runner.os }}-qt-6.10.0-${{ hashFiles('**/CMakeLists.txt') }}

- name: Install Qt (if not cached)
  if: steps.cache-qt.outputs.cache-hit != 'true'
  run: |
    python3 -m venv /tmp/qt-env
    /tmp/qt-env/bin/pip install aqtinstall
    /tmp/qt-env/bin/aqt install-qt ...
```

---

## 4. Testing Infrastructure

### 4.1 Action: Decouple C++ tests from Python gate

**Problem:** `tests/CMakeLists.txt` gates all testing behind `VIOSPICE_ENABLE_PYTHON`:
```cmake
if(VIOSPICE_ENABLE_TESTS AND VIOSPICE_ENABLE_PYTHON)
    add_subdirectory(unit/ux)
    add_subdirectory(integration)
    add_subdirectory(regression)
endif()
```
If Python dev headers are unavailable, **no tests run at all** — including pure C++ unit tests.

**Solution:** Only Python-dependent test suites should gate on Python:
```cmake
# tests/CMakeLists.txt
if(VIOSPICE_ENABLE_TESTS)
    add_subdirectory(unit/ux)
    add_subdirectory(integration)
    add_subdirectory(regression)
endif()
```
Within each subdirectory, conditionally include Python tests:
```cmake
# tests/regression/CMakeLists.txt
if(VIOSPICE_ENABLE_PYTHON)
    add_test(NAME regression.cli COMMAND ${Python3_EXECUTABLE} test_cli.py)
endif()
```

### 4.2 Action: Add pytest configuration

**Problem:** Python tests exist (`tests/regression/test_cli.py`, `tests/test_gui_remote.py`) but there's no `pytest.ini`/`pyproject.toml`. No markers, no min version, no test discovery rules.

**Solution:** Create `pyproject.toml` at repo root:

```toml
[build-system]
requires = ["setuptools>=64"]
build-backend = "setuptools.backends._legacy:_Backend"

[tool.pytest.ini_options]
minversion = "7.0"
testpaths = ["tests", "python"]
python_files = ["test_*.py"]
markers = [
    "slow: slow tests (batch simulations, etc.)",
    "regression: regression tests against known-good output",
    "gui: tests requiring Qt GUI (use QT_QPA_PLATFORM=offscreen)",
]
filterwarnings = ["ignore::DeprecationWarning"]
```

CI usage:
```yaml
- name: Run Python tests
  run: python3 -m pytest tests/regression/ -m "not slow" -v
```

### 4.3 Action: Add golden-trace regression tests for the simulator

**Problem:** The simulator (54 source files, core value proposition) has minimal coverage — only `tests/integration/test_ngspice_shared_lib.cpp` and `test_benchmark.cpp`.

**Solution:** Add golden-reference tests using `.raw` file comparison:

```cpp
// tests/regression/test_sim_golden.cpp
TEST(SimGolden, OpAmpFeedback) {
    auto netlist = loadNetlist("tests/circuits/opamp_feedback.cir");
    auto results = runSimulation(netlist, AnalysisType::OP);
    auto golden = loadGoldenRaw("tests/circuits/opamp_feedback.golden.raw");
    EXPECT_NEAR(results.getVoltage("OUT", 0), golden.getVoltage("OUT", 0), 1e-6);
}
```

Add golden `.raw` files (or compact JSON summaries) to `tests/circuits/` and register:
```cmake
vioraeda_add_test(test_sim_golden
    SOURCES regression/test_sim_golden.cpp
    LINK_LIBS VioSimulator VioCore
)
```

### 4.4 Action: Add a quick-test CMake target

```cmake
add_custom_target(quick-test
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R "^(schematic|core|unit_)\\."
    COMMENT "Running fast module unit tests only"
)
```
Usage: `cmake --build build --target quick-test`

---

## 5. Packaging & Distribution

### 5.1 Action: Remove vendored AppImage tools from git

**Problem:** ~40 MB of tool binaries and ~120 MB of AppDir build output are committed:

| File | Size |
|---|---|
| `linuxdeploy` | 19 MB |
| `linuxdeploy-plugin-qt` | 13 MB |
| `appimagetool` | 8.5 MB |
| `AppDir/` (entire directory) | ~200+ MB |

This bloats every clone and git history for all contributors.

**Solution:**
```bash
git rm linuxdeploy linuxdeploy-plugin-qt appimagetool
git rm -rf AppDir/
```
Update `.gitignore`:
```
# AppImage tooling — downloaded at build time
appimagetool
linuxdeploy*
AppDir/
```
Download tools in CI instead:
```yaml
- name: Setup AppImage tooling
  run: |
    wget -q https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage -O appimagetool
    wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -O linuxdeploy
    chmod +x appimagetool linuxdeploy linuxdeploy-plugin-qt
```

### 5.2 Action: Automate AppImage generation on release

Add an AppImage packaging step to `release.yml`:
```yaml
- name: Build AppImage
  if: runner.os == 'Linux'
  run: |
    export VERSION=${{ github.ref_name }}
    mkdir -p AppDir/usr/bin AppDir/usr/lib AppDir/cm
    cp build/VioraEDA AppDir/usr/bin/
    cp build/viomatrixc-prebuilt/lib/*.so* AppDir/usr/lib/
    cp build/cm/*.cm AppDir/cm/
    ./linuxdeploy --appdir AppDir --output appimage \
      --plugin qt \
      --desktop-file resources/viospice.desktop \
      --icon-file resources/icons/logo_viospice.png
    mv VioraEDA-*.AppImage VioraEDA-${{ github.ref_name }}-x86_64.AppImage
```

### 5.3 Action: Add native package formats via CPack

Add to root `CMakeLists.txt`:
```cmake
set(CPACK_PACKAGE_NAME "VioraEDA")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Janadasroor Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance open-source EDA/SPICE simulator")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
else()
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "electronics")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6 (>= 6.10), python3 (>= 3.10)")
endif()
include(CPack)
```

### 5.4 Action: Add version-check / auto-update mechanism

**Problem:** No update mechanism — users install once and never know about new releases.

**Solution:** Add an unobtrusive version check using Qt6::Network (already linked):

```cpp
// core/UpdateChecker.h
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    void checkAsync() {
        auto *reply = m_net.get(QNetworkRequest(
            QUrl("https://api.github.com/repos/Janadasroor/VioraEDA/releases/latest")));
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            auto json = QJsonDocument::fromJson(reply->readAll());
            QString latest = json["tag_name"].toString();
            if (compareVersions(latest, CURRENT_VERSION) > 0)
                emit updateAvailable(latest);
        });
    }
signals:
    void updateAvailable(const QString &version);
private:
    QNetworkAccessManager m_net;
};
```

Show a non-blocking notification bar: *"VioraEDA v0.2.0 is available (you have v0.1.0). Download"*.

---

## 6. Code Quality & Tooling

### 6.1 Action: Add `.clang-format` and `.clang-tidy`

**Problem:** No formatting or linting configuration exists. Code style is inconsistent across modules.

**Solution:** Create `.clang-format` (Qt style):
```yaml
BasedOnStyle: Qt
AccessModifierOffset: -4
BreakBeforeBraces: Allman
ColumnLimit: 100
IndentWidth: 4
UseTab: Always
AllowShortFunctionsOnASingleLine: None
```

Create `.clang-tidy`:
```yaml
Checks: >
    clang-analyzer-*,
    performance-*,
    modernize-*,
    bugprone-*,
    readability-identifier-naming,
    -modernize-use-trailing-return-type,
    -llvmlibc-*,
    -altera-*
CheckOptions:
  readability-identifier-naming.ClassCase: CamelCase
  readability-identifier-naming.FunctionCase: camelBack
  readability-identifier-naming.VariableCase: camelBack
  readability-identifier-naming.MemberCase: camelBack
  readability-identifier-naming.MemberSuffix: _
```

### 6.2 Action: Add `.editorconfig`

```ini
root = true

[*]
indent_style = tab
indent_size = 4
end_of_line = lf
charset = utf-8
trim_trailing_whitespace = true
insert_final_newline = true

[*.{yml,yaml,json}]
indent_size = 2
indent_style = space

[*.md]
trim_trailing_whitespace = false
```

### 6.3 Action: Add pre-commit hooks (optional)

```yaml
# .pre-commit-config.yaml
repos:
  - repo: https://github.com/pre-commit/mirrors-clang-format
    rev: v19.1.0
    hooks:
      - id: clang-format
        args: [--style=file]
  - repo: https://github.com/psf/black
    rev: 24.10.0
    hooks:
      - id: black
  - repo: https://github.com/pre-commit/pre-commit-hooks
    rev: v5.0.0
    hooks:
      - id: trailing-whitespace
      - id: end-of-file-fixer
      - id: check-added-large-files
        args: ["--maxkb=500"]
      - id: check-merge-conflict
```

### 6.4 Action: Add Dependabot and community files

**Problem:** No dependency auto-updates, no issue templates, no CODEOWNERS.

**Solution:**
- `.github/dependabot.yml` — weekly scans for GitHub Actions, pip, git submodules.
- `.github/CODEOWNERS` — route reviews per module.
- `.github/ISSUE_TEMPLATE/bug_report.md` + `feature_request.md`.
- `CONTRIBUTING.md` — build instructions, PR workflow, coding standards.
- `SECURITY.md` — vulnerability reporting process.

---

## 7. Repo Hygiene

### 7.1 Action: Purge committed binaries from git history

**Tracked artifacts that should not be in git:**

| File | Size | Category |
|---|---|---|
| `linuxdeploy` | 19 MB | Tool binary — download at build time |
| `linuxdeploy-plugin-qt` | 13 MB | Tool binary — download at build time |
| `appimagetool` | 8.5 MB | Tool binary — download at build time |
| `AppDir/` | ~200 MB | Full build output — never commit |
| `tests/circuits/boost_converter.raw` | 12 MB | Generated simulation output |
| `tests/DATA/LIBRARY/*.LIB` | ~80 MB | Large third-party data (some gitignored late) |

**Immediate fix (stop tracking forward):**
```bash
git rm --cached -r AppDir/ linuxdeploy linuxdeploy-plugin-qt appimagetool
```

**Full purge from history (for a maintenance release):**
```bash
# Using git-filter-repo
git filter-repo --path AppDir/ --path linuxdeploy --path linuxdeploy-plugin-qt \
    --path appimagetool --invert-paths
```

### 7.2 Action: Move large test data to Git LFS or release assets

Large `.LIB` files and simulation outputs should not bloat the default clone. Options:

**Option A — Git LFS** (requires LFS setup per contributor):
```
# .gitattributes
tests/DATA/LIBRARY/*.LIB filter=lfs diff=lfs merge=lfs -text
tests/circuits/*.raw filter=lfs diff=lfs merge=lfs -text
```

**Option B — Release asset** (cleaner for infrequently-changed data):
```cmake
option(VIOSPICE_DOWNLOAD_TEST_DATA "Download large test corpora" OFF)
if(VIOSPICE_DOWNLOAD_TEST_DATA)
    file(DOWNLOAD "https://github.com/Janadasroor/VioraEDA/releases/download/test-data-v1/lib-corpus.tar.gz"
         "${CMAKE_BINARY_DIR}/test-data/lib-corpus.tar.gz")
endif()
```
CI downloads only when needed, clones stay small.

---

## 8. Rollout Roadmap

### Phase 1 — Stop the bleeding (week 1)
| # | Task | Depends on |
|---|---|---|
| 1.1 | Remove AppDir/, linuxdeploy, appimagetool from tracking; update .gitignore | — |
| 1.2 | Move ci/*.yml into .github/workflows/ and verify trigger | — |
| 1.3 | Remove nanobind submodule, keep only FetchContent | — |
| 1.4 | Add checksum verification for prebuilt downloads | — |
| 1.5 | Confirm all three workflows (build, release, test-release) still pass | 1.1-1.4 |

### Phase 2 — Build ergonomics (week 2)
| # | Task | Depends on |
|---|---|---|
| 2.1 | Create CMakePresets.json for release/dev/debug/asan/msys2 | — |
| 2.2 | Refactor root CMakeLists.txt into cmake/*.cmake modules | 1.3 |
| 2.3 | Add vioraeda_add_test() helper to target_settings.cmake | 2.2 |
| 2.4 | Create .github/actions/resolve-dlls/action.yml | — |
| 2.5 | Update all CI workflows to use presets + reusable action | 2.1, 2.4 |

### Phase 3 — Quality gates (weeks 3-4)
| # | Task | Depends on |
|---|---|---|
| 3.1 | Replace sleep+kill smoke tests with real CLI smoke tests | — |
| 3.2 | Add ASan+UBSan CI job | 2.1 (asan preset) |
| 3.3 | Add clang-tidy CI job + .clang-tidy config | 6.1 |
| 3.4 | Add code coverage CI job | 2.1 |
| 3.5 | Add Qt cache across CI jobs | — |
| 3.6 | Decouple C++ tests from Python gate | — |
| 3.7 | Add pytest config (pyproject.toml) | — |
| 3.8 | Add golden-trace simulator regression tests | — |

### Phase 4 — Distribution & community (week 5)
| # | Task | Depends on |
|---|---|---|
| 4.1 | Automate AppImage build in release CI | 1.1 |
| 4.2 | Add CPack (deb, dmg, NSIS) in release CI | — |
| 4.3 | Add .clang-format, .editorconfig, .pre-commit-config.yaml | — |
| 4.4 | Add dependabot, CODEOWNERS, CONTRIBUTING.md, SECURITY.md, issue templates | — |
| 4.5 | Add version-check module in core/ | — |
| 4.6 | Move large test data to LFS or release assets | — |
| 4.7 | Purge large binaries from git history with git-filter-repo | 4.6 |

---

## Appendix: Quick Wins (< 30 min each)

1. **Add `.editorconfig`** — 10 lines, zero tooling dependencies, instantly improves editor consistency.
2. **Add `dependabot.yml`** — 15 lines, auto-patches CI action versions and pip dependencies.
3. **Move `ci/*.yml`** — `git mv`, zero code change, revives dead automation instantly.
4. **Remove nanobind submodule** — `git submodule deinit + git rm`, deduplicates dependency source.
5. **Add `vioraeda_add_test()` helper** — ~15 lines in `target_settings.cmake`, eliminates ~10 lines of boilerplate per test.
6. **Add `CMakePresets.json`** — ~80 lines, simplifies all local + CI development with no behavior change.

---

> **Next step:** Pick any phase or quick-win item above and I'll implement it immediately, verifying against the build.

