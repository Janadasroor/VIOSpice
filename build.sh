#!/usr/bin/env bash
# build.sh — one-command build for VioraEDA (new-user friendly).
#
# Detects the OS/toolchain, installs missing dependencies, configures,
# builds, runs the test suite, and prints how to launch the app.
#
# Usage:
#   ./build.sh                  # deps + configure + build + test
#   ./build.sh --jobs 8         # limit parallel jobs
#   ./build.sh --skip-deps      # assume dependencies already installed
#   ./build.sh --skip-tests     # build only, skip ctest
#   ./build.sh --clean          # wipe the build dir first
#   ./build.sh --dev            # build VioMATRIXC/FluxScript from source
#   ./build.sh --help

set -euo pipefail

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
if [ -t 1 ]; then
    C_RED=$'\033[31m'; C_GRN=$'\033[32m'; C_YLW=$'\033[33m'
    C_END=$'\033[0m'
else
    C_RED=""; C_GRN=""; C_YLW=""; C_END=""
fi
info() { printf "${C_YLW}==>${C_END} %s\n" "$*"; }
ok()   { printf "${C_GRN}==>${C_END} %s\n" "$*"; }
warn() { printf "${C_YLW}!! ${C_END} %s\n" "$*" >&2; }
die()  { printf "${C_RED}ERROR: %s${C_END}\n" "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
VioraEDA build script — one command to install deps, configure, build,
run tests, and locate the app.

Usage:
  ./build.sh                  deps + configure + build + test
  ./build.sh --jobs 8         limit parallel jobs
  ./build.sh --skip-deps      assume dependencies already installed
  ./build.sh --skip-tests     build only, skip ctest
  ./build.sh --clean          wipe the build dir first
  ./build.sh --dev            build VioMATRIXC/FluxScript from source
  ./build.sh --help           this help

Notes:
  - On Windows, run from the 'MSYS2 MINGW64' shell.
  - The app and CLI binaries land in the build/ directory.
EOF
    exit 0
}

SELF="${BASH_SOURCE[0]}"
ROOT="${SELF%/*}"
case "$ROOT" in
    /*|[A-Za-z]:/*) : ;;                    # absolute (posix or msys c:/path)
    .)              ROOT="$(pwd)" ;;
    *)              ROOT="$(cd "$ROOT" && pwd)" ;;
esac
[ -z "$ROOT" ] && ROOT="$(pwd)"
PREFIX=""
JOBS=""
SKIP_DEPS=0
SKIP_TESTS=0
DO_CLEAN=0
DEV_MODE=0

while [ $# -gt 0 ]; do
    case "$1" in
        --help|-h)        usage ;;
        --skip-deps)      SKIP_DEPS=1 ;;
        --skip-tests)     SKIP_TESTS=1 ;;
        --clean)          DO_CLEAN=1 ;;
        --dev)            DEV_MODE=1 ;;
        --jobs|--parallel)
            shift
            case "${1:-}" in
                ''|*[!0-9]*) die "--jobs needs a positive number" ;;
                *) JOBS="$1" ;;
            esac ;;
        -j?*) JOBS="${1#-j}" ;;
        *) die "unknown option: $1 (try --help)" ;;
    esac
    shift
done

# ---------------------------------------------------------------------------
# Detect OS & Architecture
# ---------------------------------------------------------------------------
case "$(uname -s)" in
    Linux*)          OS="linux" ;;
    Darwin*)         OS="macos" ;;
    MINGW*|MSYS*)    OS="windows" ;;
    *)               die "unsupported OS: $(uname -s)" ;;
esac

ARCH="$(uname -m)"

if [ "$OS" = "windows" ] && [ -z "${MINGW_PREFIX:-}" ]; then
    die "Please run this script from the 'MSYS2 MINGW64' shell (not MSYS/MSYS2)."
fi

[ -z "$JOBS" ] && JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# On x86_64 macOS, default to dev mode to build native x86_64 ngspice & FluxScript
if [ "$OS" = "macos" ] && [ "$ARCH" = "x86_64" ]; then
    DEV_MODE=1
fi

info "Platform        : $OS ($ARCH)"
info "Jobs            : $JOBS"
info "Dev Mode (src)  : $DEV_MODE"
info "Project root    : $ROOT"
echo

# ---------------------------------------------------------------------------
# 1) Dependencies & Environment Setup
# ---------------------------------------------------------------------------
if [ "$SKIP_DEPS" = "0" ]; then
    case "$OS" in
        windows)
            PKGS=(base-devel
                  mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake
                  mingw-w64-x86_64-curl mingw-w64-x86_64-python
                  mingw-w64-x86_64-python-pip mingw-w64-x86_64-pkg-config
                  mingw-w64-x86_64-qt6-base mingw-w64-x86_64-qt6-charts
                  mingw-w64-x86_64-qt6-websockets mingw-w64-x86_64-qt6-multimedia
                  mingw-w64-x86_64-qt6-svg mingw-w64-x86_64-qt6-declarative
                  mingw-w64-x86_64-qt6-5compat mingw-w64-x86_64-qt6-shadertools
                  mingw-w64-x86_64-llvm mingw-w64-x86_64-eigen3
                  flex bison autoconf automake libtool gettext gettext-devel zip)
            missing=()
            for p in "${PKGS[@]}"; do
                pacman -Q "$p" >/dev/null 2>&1 || missing+=("$p")
            done
            if [ "${#missing[@]}" -gt 0 ]; then
                info "Installing MSYS2/MinGW packages (${#missing[@]}) ..."
                pacman -S --needed --noconfirm "${missing[@]}"
            else
                ok "MSYS2 MinGW packages already installed."
            fi
            ;;
        macos)
            if command -v brew >/dev/null 2>&1; then
                info "Checking Homebrew packages ..."
                brew install cmake autoconf automake libtool bison flex zstd llvm@15 eigen pkg-config || true
            fi
            ;;
        linux)
            info "Checking Linux packages ..."
            if command -v apt-get >/dev/null 2>&1; then
                sudo apt-get update
                sudo apt-get install -y \
                    build-essential cmake git \
                    qt6-base-dev qt6-charts-dev qt6-svg-dev qt6-tools-dev \
                    qt6-l10n-tools qt6-translations-l10n libgl1-mesa-dev \
                    llvm-dev clang-dev libcurl4-openssl-dev \
                    autoconf automake libtool bison flex libnghttp2-dev gettext \
                    python3 python3-pip python3-venv python3-dev libeigen3-dev
            elif command -v dnf >/dev/null 2>&1; then
                sudo dnf install -y \
                    cmake gcc-c++ qt6-qtbase-devel qt6-qtcharts-devel qt6-qtsvg-devel \
                    qt6-tools-devel qt6-declarative-devel llvm-devel clang-devel \
                    libcurl-devel python3 python3-devel autoconf automake libtool \
                    flex bison libnghttp2-devel gettext eigen3-devel
            elif command -v pacman >/dev/null 2>&1; then
                sudo pacman -S --noconfirm --needed \
                    base-devel cmake qt6-base qt6-charts qt6-svg qt6-declarative \
                    llvm clang curl python python-pip autoconf automake libtool \
                    flex bison libnghttp2 gettext eigen
            fi
            ;;
    esac
fi

# Discover macOS specific paths
QT_PREFIX_DIRS=()
LLVM_PREFIX_DIRS=()
EXTRA_CMAKE_ARGS=()

if [ "$OS" = "macos" ]; then
    # Bison & tools
    for bp in /usr/local/opt/bison/bin /opt/homebrew/opt/bison/bin; do
        [ -d "$bp" ] && export PATH="$bp:$PATH" && break
    done

    # LLVM
    for lp in /usr/local/opt/llvm@15 /opt/homebrew/opt/llvm@15 /usr/local/opt/llvm /opt/homebrew/opt/llvm; do
        if [ -d "$lp" ]; then
            export PATH="$lp/bin:$PATH"
            LLVM_PREFIX_DIRS+=("$lp")
            EXTRA_CMAKE_ARGS+=(-DLLVM_DIR="$lp/lib/cmake/llvm")
            break
        fi
    done

    # Qt 6
    for qp in "$HOME/Qt/6.6.3/macos" "$HOME/Qt/6.*/macos" /usr/local/opt/qt@6 /opt/homebrew/opt/qt@6 /usr/local/opt/qt /opt/homebrew/opt/qt; do
        if [ -d "$qp" ]; then
            QT_PREFIX_DIRS+=("$qp")
            export Qt6_DIR="$qp/lib/cmake/Qt6"
            break
        fi
    done

    # Eigen3
    for ep in /usr/local/include/eigen3 /opt/homebrew/include/eigen3 /usr/local/opt/eigen/include/eigen3; do
        if [ -d "$ep" ]; then
            EXTRA_CMAKE_ARGS+=(-DEIGEN3_INCLUDE_DIR="$ep")
            break
        fi
    done
fi

# Discover component libraries
if [ -d "$HOME/ViospiceLib" ]; then
    EXTRA_CMAKE_ARGS+=(-DVIOSPICE_BUNDLED_LIB="$HOME/ViospiceLib")
elif [ -d "$HOME/viora-libs" ]; then
    EXTRA_CMAKE_ARGS+=(-DVIOSPICE_BUNDLED_LIB="$HOME/viora-libs")
fi

# ---------------------------------------------------------------------------
# 2) Configure
# ---------------------------------------------------------------------------
if [ "$DO_CLEAN" = "1" ]; then
    info "Cleaning build/ ..."
    rm -rf "$ROOT/build"
fi

CMAKE_ARGS=(
    -B build
    -DCMAKE_BUILD_TYPE=Release
    -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER
)

if [ "$DEV_MODE" = "1" ]; then
    CMAKE_ARGS+=(-DVIOSPICE_DEV_MODE=ON)
fi

PREFIX_PATH_LIST=""
[ "${#QT_PREFIX_DIRS[@]}" -gt 0 ] && PREFIX_PATH_LIST="${QT_PREFIX_DIRS[0]}"
[ "${#LLVM_PREFIX_DIRS[@]}" -gt 0 ] && PREFIX_PATH_LIST="${PREFIX_PATH_LIST:+${PREFIX_PATH_LIST};}${LLVM_PREFIX_DIRS[0]}"

if [ -n "$PREFIX_PATH_LIST" ]; then
    CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$PREFIX_PATH_LIST")
fi

CMAKE_ARGS+=("${EXTRA_CMAKE_ARGS[@]}")

info "Configuring CMake in build/ ..."
cmake "${CMAKE_ARGS[@]}"

# ---------------------------------------------------------------------------
# 3) Build
# ---------------------------------------------------------------------------
ok "Building targets: VioraEDA, viora, flux_runner, and test targets ..."
cmake --build build --parallel "$JOBS"

# Stage engine dylibs if in dev mode
if [ -f "build/_deps/viomatrixc-src/src/.libs/libngspice.dylib" ]; then
    mkdir -p build/viospice_engine/lib build/viomatrixc-prebuilt/lib
    cp -f build/_deps/viomatrixc-src/src/.libs/libngspice* build/viospice_engine/lib/ 2>/dev/null || true
    cp -f build/_deps/viomatrixc-src/src/.libs/libngspice* build/viomatrixc-prebuilt/lib/ 2>/dev/null || true
fi
if [ -f "build/_deps/fluxscript-build/libFluxScript.dylib" ]; then
    mkdir -p build/fluxscript-prebuilt/lib
    cp -f build/_deps/fluxscript-build/libFluxScript.dylib build/fluxscript-prebuilt/lib/ 2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# 4) Tests
# ---------------------------------------------------------------------------
if [ "$SKIP_TESTS" = "0" ]; then
    info "Running CTest test suite ..."
    set +e
    if [ "$OS" = "macos" ]; then
        DYLD_LIBRARY_PATH="$ROOT/build:$ROOT/build/_deps/viomatrixc-src/src/.libs:$ROOT/build/_deps/fluxscript-build:${LLVM_PREFIX_DIRS[0]:-}/lib" \
        QT_QPA_PLATFORM="offscreen" \
        ctest --test-dir build --output-on-failure -E "daemon"
    else
        QT_QPA_PLATFORM="offscreen" \
        ctest --test-dir build --output-on-failure -E "daemon"
    fi
    CTEST_RC=$?
    set -e
    if [ "$CTEST_RC" = "0" ]; then
        ok "All registered test suites passed!"
    else
        warn "CTest returned $CTEST_RC — see output above."
    fi
fi

# ---------------------------------------------------------------------------
# 5) Summary & Launch Instructions
# ---------------------------------------------------------------------------
BIN_DIR="$ROOT/build"
APP=""

if [ "$OS" = "macos" ] && [ -d "$BIN_DIR/VioraEDA.app" ]; then
    APP="$BIN_DIR/VioraEDA.app"
else
    for p in "$BIN_DIR/VioraEDA" "$BIN_DIR/VioraEDA.exe"; do
        [ -x "$p" ] && APP="$p" && break
    done
fi

if [ -n "$APP" ]; then
    ok "Build finished successfully!"
    echo
    if [ "$OS" = "windows" ]; then
        echo "GUI Application : $(cygpath -w "$APP" 2>/dev/null || echo "$APP")"
    elif [ "$OS" = "macos" ]; then
        echo "GUI Application : open $APP"
    else
        echo "GUI Application : $APP"
    fi
    echo "CLI Tool        : $BIN_DIR/viora"
    echo "Flux Runner     : $BIN_DIR/flux_runner"
    echo
    echo "To install system-wide, run: ./install.sh"
else
    warn "Build finished but no executable found in build/."
fi

exit 0