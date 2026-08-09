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
# Detect OS
# ---------------------------------------------------------------------------
case "$(uname -s)" in
    Linux*)          OS="linux" ;;
    Darwin*)         OS="macos" ;;
    MINGW*|MSYS*)    OS="windows" ;;
    *)               die "unsupported OS: $(uname -s)" ;;
esac

if [ "$OS" = "windows" ] && [ -z "${MINGW_PREFIX:-}" ]; then
    die "Please run this script from the 'MSYS2 MINGW64' shell (not MSYS/MSYS2)."
fi

[ "$JOBS" -eq 0 ] && JOBS="$(nproc 2>/dev/null || echo 4)"

info "Platform        : $OS"
info "Jobs            : $JOBS"
info "Project root    : $ROOT"
echo

# ---------------------------------------------------------------------------
# 1) Dependencies
# ---------------------------------------------------------------------------
if [ "$SKIP_DEPS" = "0" ]; then
    case "$OS" in
        windows)
            need() { command -v "$1" >/dev/null 2>&1; }
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
            for p in "${PKGS}"; do
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
            need_cmd brew || die "Install Homebrew first: https://brew.sh"
            info "Installing Homebrew packages ..."
            brew install cmake python@3.12 ccache autoconf automake libtool \
                bison flex zstd llvm libomp pkg-config gettext \
                qt || true
            ;;
        linux)
            info "Installing Linux packages (requires sudo) ..."
            if command -v apt-get >/dev/null 2>&1; then
                sudo apt-get update
                sudo apt-get install -y \
                    build-essential cmake git \
                    qt6-base-dev qt6-charts-dev qt6-svg-dev qt6-tools-dev \
                    qt6-l10n-tools qt6-translations-l10n libgl1-mesa-dev \
                    llvm-18-dev libclang-18-dev libcurl4-openssl-dev \
                    autoconf automake libtool bison flex libnghttp2-dev gettext \
                    python3 python3-pip python3-venv python3-dev
            elif command -v dnf >/dev/null 2>&1; then
                sudo dnf install -y \
                    cmake gcc-c++ qt6-qtbase-devel qt6-qtcharts-devel qt6-qtsvg-devel \
                    qt6-tools-devel qt6-declarative-devel llvm-devel clang-devel \
                    libcurl-devel python3 python3-devel autoconf automake libtool \
                    flex bison libnghttp2-devel gettext
            elif command -v pacman >/dev/null 2>&1; then
                sudo pacman -S --noconfirm --needed \
                    base-devel cmake qt6-base qt6-charts qt6-svg qt6-declarative \
                    llvm clang curl python python-pip autoconf automake libtool \
                    flex bison libnghttp2 gettext
            fi
            ;;
    esac
fi

# Verify core tools are now present
for t in cmake ${DEV_MODE:+autoconf automake libtool}; do
    command -v "$t" >/dev/null 2>&1 || die "missing required tool: $t"
done

# ---------------------------------------------------------------------------
# 2) Configure
# ---------------------------------------------------------------------------
if [ "$DO_CLEAN" = "1" ]; then
    info "Cleaning build/ ..."
    rm -rf "$ROOT/build"
fi

CMAKE_ARGS=(-DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER)
BUILD_PRESET="release"
CONFIGURE_PRESET="release"
if [ "$DEV_MODE" = "1" ]; then
    CMAKE_ARGS+=(-DVIOSPICE_DEV_MODE=ON)
fi

if [ "$OS" = "windows" ]; then
    CONFIGURE_PRESET="msys2-mingw64"
    BUILD_PRESET="msys2-mingw64"
fi

info "Configuring (CMake preset: ${CONFIGURE_PRESET}) ..."

if [ "$OS" = "windows" ]; then
    Qt6_DIR="${Qt6_DIR:-$MINGW_PREFIX/lib/cmake/Qt6}" cmake --preset "$CONFIGURE_PRESET" "${CMAKE_ARGS[@]}"
else
    cmake --preset "$CONFIGURE_PRESET" "${CMAKE_ARGS[@]}"
fi

# ---------------------------------------------------------------------------
# 3) Build
# ---------------------------------------------------------------------------
ok "Building (target: VioraEDA + viora CLI) ..."
cmake --build --preset "${BUILD_PRESET}" --parallel "$JOBS" \
    --target VioraEDA viora

# ---------------------------------------------------------------------------
# 4) Tests
# ---------------------------------------------------------------------------
if [ "$SKIP_TESTS" = "0" ]; then
    if command -v ctest >/dev/null 2>&1; then
        info "Running test suite ..."
        set +e
        ctest --preset unit --output-on-failure
        CTEST_RC=$?
        set -e
        if [ "$CTEST_RC" = "0" ]; then
            ok "All tests passed."
        else
            warn "ctest returned $CTEST_RC — see output above."
        fi
    else
        warn "ctest not found; skipping tests."
    fi
fi

# ---------------------------------------------------------------------------
# 5) Done
# ---------------------------------------------------------------------------
BIN_DIR="$ROOT/build"
APP=""
for p in "$BIN_DIR/VioraEDA" "$BIN_DIR/VioraEDA.exe"; do
    [ -x "$p" ] && APP="$p" && break
done

if [ -n "$APP" ]; then
    ok "Build finished successfully!"
    echo
    if [ "$OS" = "windows" ]; then
        echo "To launch the app:   $(cygpath -w "$APP")"
    else
        echo "To launch the app:   $APP"
    fi
    echo "CLI tool:            $BIN_DIR/viora"
else
    warn "Build finished but no VioraEDA executable found in build/."
fi

exit 0