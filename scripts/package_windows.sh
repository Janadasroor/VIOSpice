#!/usr/bin/env bash
# package_windows.sh — build the VioraEDA Windows installer (NSIS) locally.
#
# Produces build/installer/VioraEDA-<version>-windows-x86_64.exe plus the
# matching .zip archive. Bundles the ViospiceLib library (from ~/ViospiceLib
# unless --lib <dir> is given), all Qt/MinGW runtime DLLs, the ngspice engine
# DLL, python, LLVM DLLs, code models, templates, examples and python assets.
#
# Usage (run from the 'MSYS2 MINGW64' shell):
#   ./scripts/package_windows.sh
#   ./scripts/package_windows.sh --lib /path/to/ViospiceLib
#   ./scripts/package_windows.sh --skip-deps     # assume deps already installed
#   ./scripts/package_windows.sh --no-lib        # installer without the library
#   ./scripts/package_windows.sh --jobs 8
#   ./scripts/package_windows.sh --help

set -euo pipefail

C_RED=$'\033[31m'; C_GRN=$'\033[32m'; C_YLW=$'\033[33m'; C_END=$'\033[0m'
info() { printf "${C_YLW}==>${C_END} %s\n" "$*"; }
ok()   { printf "${C_GRN}==>${C_END} %s\n" "$*"; }
warn() { printf "${C_YLW}!! ${C_END} %s\n" "$*" >&2; }
die()  { printf "${C_RED}ERROR: %s${C_END}\n" "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
VioraEDA Windows installer build script (NSIS via CPack).

Usage:
  ./scripts/package_windows.sh                 build installer with ~/ViospiceLib
  ./scripts/package_windows.sh --lib <dir>     bundle library from <dir>
  ./scripts/package_windows.sh --no-lib        installer without the library
  ./scripts/package_windows.sh --skip-deps     skip installing MSYS2 packages
  ./scripts/package_windows.sh --jobs 8        limit parallel build jobs
  ./scripts/package_windows.sh --help          this help

Output: build/installer/VioraEDA-<ver>-windows-x86_64.{exe,zip}
EOF
    exit 0
}

SELF="${BASH_SOURCE[0]}"
ROOT="${SELF%/*}"; case "$ROOT" in /*) ;; *) ROOT="$(cd "$ROOT" && pwd)" ;; esac
ROOT="$(cd "$ROOT/.." && pwd)"

LIB_SRC=""
SKIP_DEPS=0
JOBS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --help|-h)     usage ;;
        --skip-deps)   SKIP_DEPS=1 ;;
        --no-lib)      LIB_SRC="__none__" ;;
        --lib)
            shift; [ -d "${1:-}" ] || die "--lib needs an existing directory"
            LIB_SRC="$(cd "$1" && pwd)" ;;
        --jobs)
            shift; case "${1:-}" in ''|*[!0-9]*) die "--jobs needs a number" ;; *) JOBS="$1" ;; esac ;;
        *) die "unknown option: $1 (try --help)" ;;
    esac
    shift
done

case "$(uname -s)" in MINGW*|MSYS*) ;; *) die "This script must run from the MSYS2 MINGW64 shell on Windows." ;; esac
[ -n "${MINGW_PREFIX:-}" ] || die "Run from the 'MSYS2 MINGW64' shell, not MSYS2."

[ -z "$JOBS" ] && JOBS="$(nproc 2>/dev/null || echo 4)"
info "Project root : $ROOT"
info "Jobs         : $JOBS"

# ---------------------------------------------------------------------------
# 1) Dependencies
# ---------------------------------------------------------------------------
if [ "$SKIP_DEPS" = "0" ]; then
    info "Checking/installing MSYS2 MinGW packages ..."
    PKGS=(base-devel
          mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-cmake
          mingw-w64-x86_64-curl mingw-w64-x86_64-python mingw-w64-x86_64-python-pip
          mingw-w64-x86_64-pkg-config mingw-w64-x86_64-qt6-base mingw-w64-x86_64-qt6-charts
          mingw-w64-x86_64-qt6-websockets mingw-w64-x86_64-qt6-multimedia
          mingw-w64-x86_64-qt6-svg mingw-w64-x86_64-qt6-declarative
          mingw-w64-x86_64-qt6-5compat mingw-w64-x86_64-qt6-shadertools
          mingw-w64-x86_64-llvm mingw-w64-x86_64-eigen3
          mingw-w64-x86_64-nsis
          flex bison autoconf automake libtool gettext gettext-devel zip)
    missing=()
    for p in "${PKGS[@]}"; do pacman -Q "$p" >/dev/null 2>&1 || missing+=("$p"); done
    if [ "${#missing[@]}" -gt 0 ]; then
        pacman -S --needed --noconfirm "${missing[@]}"
    else
        ok "MSYS2 MinGW packages already installed."
    fi
fi

command -v makensis >/dev/null 2>&1 || die "makensis not found (install mingw-w64-x86_64-nsis)."
command -v windeployqt >/dev/null 2>&1 || die "windeployqt not found (install mingw-w64-x86_64-qt6-base)."
command -v cmake >/dev/null 2>&1 || die "cmake not found."

# ---------------------------------------------------------------------------
# 2) Resolve bundled library
# ---------------------------------------------------------------------------
CMAKE_EXTRA=(-DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER)
if [ "${LIB_SRC:-}" = "__none__" ]; then
    warn "Building installer WITHOUT the ViospiceLib library."
elif [ -n "${LIB_SRC:-}" ]; then
    info "Bundling ViospiceLib from: $LIB_SRC"
    CMAKE_EXTRA+=("-DVIOSPICE_BUNDLED_LIB=$LIB_SRC")
else
    # MSYS2 $HOME maps to C:\msys64\home\<user>, not the Windows profile. The
    # library the app reads lives in the user's real home directory.
    if [ -d "$USERPROFILE/ViospiceLib" ]; then
        CMAKE_EXTRA+=("-DVIOSPICE_BUNDLED_LIB=$USERPROFILE/ViospiceLib")
    else
        warn "No ViospiceLib found; building installer without the bundled library."
        CMAKE_EXTRA+=("-DVIOSPICE_BUNDLED_LIB=")
    fi
fi

# ---------------------------------------------------------------------------
# 3) Configure + build
# ---------------------------------------------------------------------------
info "Configuring (msys2-mingw64 preset) ..."
Qt6_DIR="${Qt6_DIR:-$MINGW_PREFIX/lib/cmake/Qt6}" cmake --preset msys2-mingw64 "${CMAKE_EXTRA[@]}"

info "Building VioraEDA, viora, flux_runner, flux-lsp ..."
cmake --build --preset msys2-mingw64 --parallel "$JOBS" \
    --target VioraEDA viora flux_runner flux-lsp viospice-merge

# ---------------------------------------------------------------------------
# 4) Package (NSIS + ZIP)
# ---------------------------------------------------------------------------
info "Generating installer (this bundles Qt runtime + library; may take a while) ..."
mkdir -p "$ROOT/build/installer"
cd "$ROOT/build"
cpack -G "NSIS;ZIP" -C Release -B "$ROOT/build/installer" 2>&1

ok "Packaging complete."
echo
echo "Installer : $(cygpath -w "$ROOT/build/installer" 2>/dev/null || echo "$ROOT/build/installer")"
ls -lh "$ROOT/build/installer" | sed 's/^/  /'
exit 0
