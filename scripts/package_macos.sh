#!/usr/bin/env bash
# ==============================================================================
# package_macos.sh — build the VioraEDA macOS standalone app bundle, DMG & tarball
# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0
# ==============================================================================

set -euo pipefail

C_RED=$'\033[31m'; C_GRN=$'\033[32m'; C_YLW=$'\033[33m'; C_CYN=$'\033[36m'; C_END=$'\033[0m'
info() { printf "${C_CYN}==>${C_END} %s\n" "$*"; }
ok()   { printf "${C_GRN}==>${C_END} %s\n" "$*"; }
warn() { printf "${C_YLW}!! ${C_END} %s\n" "$*" >&2; }
die()  { printf "${C_RED}ERROR: %s${C_END}\n" "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
VioraEDA macOS installer & packaging build script.

Usage:
  ./scripts/package_macos.sh                 build .app bundle, .dmg image & .tar.gz
  ./scripts/package_macos.sh --dmg-only      build only the standalone .dmg installer image
  ./scripts/package_macos.sh --tar-only      build only the portable .tar.gz bundle
  ./scripts/package_macos.sh --lib <dir>     bundle custom component library from <dir>
  ./scripts/package_macos.sh --no-lib        build package without bundling library
  ./scripts/package_macos.sh --version <ver> specify explicit version string
  ./scripts/package_macos.sh --skip-build    skip compilation (package existing build)
  ./scripts/package_macos.sh --jobs 8        limit parallel build jobs
  ./scripts/package_macos.sh --help          this help dialog

Output: build/installer/VioraEDA-<ver>-macos-<arch>.{dmg,tar.gz}
EOF
    exit 0
}

SELF="${BASH_SOURCE[0]}"
ROOT_DIR="$(cd "$(dirname "$SELF")/.." && pwd)"

BUILD_DIR="$ROOT_DIR/build"
OUT_DIR="$BUILD_DIR/installer"
VERSION="${VERSION:-0.2.0-beta}"
LIB_SRC=""
DMG_ONLY=0
TAR_ONLY=0
SKIP_BUILD=0
JOBS="$(sysctl -n hw.logicalcpu 2>/dev/null || echo 8)"
ARCH="$(uname -m)"
case "$ARCH" in
    arm64|aarch64) ARCH="arm64" ;;
    x86_64|amd64)  ARCH="x86_64" ;;
esac

while [ $# -gt 0 ]; do
    case "$1" in
        --help|-h)        usage ;;
        --dmg-only)       DMG_ONLY=1 ;;
        --tar-only)       TAR_ONLY=1 ;;
        --no-lib)         LIB_SRC="__none__" ;;
        --skip-build)     SKIP_BUILD=1 ;;
        --lib)
            shift; [ -d "${1:-}" ] || die "--lib needs an existing directory"
            LIB_SRC="$(cd "$1" && pwd)" ;;
        --version)
            shift; [ -n "${1:-}" ] || die "--version needs an argument"
            VERSION="$1" ;;
        --build-dir)
            shift; [ -n "${1:-}" ] || die "--build-dir needs an argument"
            BUILD_DIR="$(cd "$1" && pwd)" ;;
        --out-dir)
            shift; [ -n "${1:-}" ] || die "--out-dir needs an argument"
            OUT_DIR="$1" ;;
        --jobs|-j)
            shift; case "${1:-}" in ""|*[!0-9]*) die "--jobs needs a number" ;; *) JOBS="$1" ;; esac ;;
        *) die "unknown option: $1 (try --help)" ;;
    esac
    shift
done

VERSION="${VERSION#v}"
PKG_NAME="VioraEDA-${VERSION}-macos-${ARCH}"
STAGE_DIR="$BUILD_DIR/stage_macos"
APP_DIR="$STAGE_DIR/VioraEDA.app"

info "=========================================================="
info " VioraEDA macOS Standalone Packaging Tool"
info " Version    : $VERSION"
info " Arch       : $ARCH"
info " Root Dir   : $ROOT_DIR"
info " Build Dir  : $BUILD_DIR"
info " Output Dir : $OUT_DIR"
info " Jobs       : $JOBS"
info "=========================================================="

mkdir -p "$OUT_DIR"
rm -rf "$STAGE_DIR"
mkdir -p "$APP_DIR/Contents/MacOS"
mkdir -p "$APP_DIR/Contents/Resources"
mkdir -p "$APP_DIR/Contents/Frameworks"
mkdir -p "$APP_DIR/Contents/PlugIns"

# ---------------------------------------------------------------------------
# 1) Build binaries
# ---------------------------------------------------------------------------
if [ "$SKIP_BUILD" -eq 0 ]; then
    info "Building VioraEDA, CLI, and toolchain utilities..."
    for tgt in VioraEDA viora flux_runner VioraEDA_Setup; do
        cmake --build "$BUILD_DIR" -j"$JOBS" --target "$tgt" || true
    done
    cmake --build "$BUILD_DIR" -j"$JOBS" --target flux-lsp 2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# 2) Stage Executables into VioraEDA.app/Contents/MacOS
# ---------------------------------------------------------------------------
info "Staging executables into App Bundle..."
if [ -f "$BUILD_DIR/VioraEDA.app/Contents/MacOS/VioraEDA" ]; then
    cp -f "$BUILD_DIR/VioraEDA.app/Contents/MacOS/VioraEDA" "$APP_DIR/Contents/MacOS/"
elif [ -f "$BUILD_DIR/VioraEDA" ]; then
    cp -f "$BUILD_DIR/VioraEDA" "$APP_DIR/Contents/MacOS/"
else
    die "VioraEDA executable not found in $BUILD_DIR"
fi

[ -f "$BUILD_DIR/viora" ] && cp -f "$BUILD_DIR/viora" "$APP_DIR/Contents/MacOS/" || true
[ -f "$BUILD_DIR/flux_runner" ] && cp -f "$BUILD_DIR/flux_runner" "$APP_DIR/Contents/MacOS/" || true
[ -f "$BUILD_DIR/flux-lsp" ] && cp -f "$BUILD_DIR/flux-lsp" "$APP_DIR/Contents/MacOS/" || true
[ -f "$BUILD_DIR/VioraEDA_Setup" ] && cp -f "$BUILD_DIR/VioraEDA_Setup" "$APP_DIR/Contents/MacOS/" || true
find "$BUILD_DIR" -name "vioavr" -type f -exec cp -f {} "$APP_DIR/Contents/MacOS/" \; 2>/dev/null || true

# ---------------------------------------------------------------------------
# 3) Info.plist, PkgInfo & Icon
# ---------------------------------------------------------------------------
info "Configuring App Bundle metadata and icons..."
if [ -f "$ROOT_DIR/resources/installer/macos/Info.plist.in" ]; then
    sed "s|@PROJECT_VERSION@|$VERSION|g" "$ROOT_DIR/resources/installer/macos/Info.plist.in" > "$APP_DIR/Contents/Info.plist"
elif [ -f "$ROOT_DIR/resources/Info.plist" ]; then
    cp -f "$ROOT_DIR/resources/Info.plist" "$APP_DIR/Contents/Info.plist"
fi
echo -n "APPL????" > "$APP_DIR/Contents/PkgInfo"

if [ -f "$ROOT_DIR/resources/installer/macos/vioraeda.icns" ]; then
    cp -f "$ROOT_DIR/resources/installer/macos/vioraeda.icns" "$APP_DIR/Contents/Resources/vioraeda.icns"
elif [ -f "$ROOT_DIR/resources/icons/app_icon.icns" ]; then
    cp -f "$ROOT_DIR/resources/icons/app_icon.icns" "$APP_DIR/Contents/Resources/vioraeda.icns"
fi

# ---------------------------------------------------------------------------
# 4) Stage Resources (models, templates, python, cm, ViospiceLib)
# ---------------------------------------------------------------------------
info "Staging resources, templates, and code models..."
[ -d "$ROOT_DIR/models" ] && cp -rf "$ROOT_DIR/models" "$APP_DIR/Contents/Resources/" 2>/dev/null || true
[ -d "$ROOT_DIR/templates" ] && cp -rf "$ROOT_DIR/templates" "$APP_DIR/Contents/Resources/" 2>/dev/null || true
if [ -d "$ROOT_DIR/python" ]; then
    mkdir -p "$APP_DIR/Contents/Resources/python"
    cp -rf "$ROOT_DIR/python/"* "$APP_DIR/Contents/Resources/python/" 2>/dev/null || true
fi
if [ -d "$ROOT_DIR/python/templates" ]; then
    mkdir -p "$APP_DIR/Contents/Resources/templates/flux"
    cp -rf "$ROOT_DIR/python/templates/"*.flux "$APP_DIR/Contents/Resources/templates/flux/" 2>/dev/null || true
fi

# Code models (.cm)
mkdir -p "$APP_DIR/Contents/Resources/cm"
if [ -d "$BUILD_DIR/cm" ]; then
    cp -rf "$BUILD_DIR/cm/"* "$APP_DIR/Contents/Resources/cm/" 2>/dev/null || true
fi
if [ -d "$BUILD_DIR/viomatrixc-prebuilt/lib/ngspice" ]; then
    for f in "$BUILD_DIR/viomatrixc-prebuilt/lib/ngspice/"*.cm; do
        [ -f "$f" ] && cp -f "$f" "$APP_DIR/Contents/Resources/cm/" || true
    done
fi

# Bundle ViospiceLib
if [ "$LIB_SRC" = "__none__" ]; then
    info "Skipping ViospiceLib bundle (--no-lib requested)..."
elif [ -n "$LIB_SRC" ] && [ -d "$LIB_SRC" ]; then
    info "Bundling ViospiceLib from custom source ($LIB_SRC)..."
    mkdir -p "$APP_DIR/Contents/Resources/ViospiceLib"
    cp -rf "$LIB_SRC/"* "$APP_DIR/Contents/Resources/ViospiceLib/"
    rm -rf "$APP_DIR/Contents/Resources/ViospiceLib/.git"
elif [ -d "$HOME/viora-libs" ]; then
    info "Bundling ViospiceLib from $HOME/viora-libs..."
    mkdir -p "$APP_DIR/Contents/Resources/ViospiceLib"
    cp -rf "$HOME/viora-libs/"* "$APP_DIR/Contents/Resources/ViospiceLib/"
    rm -rf "$APP_DIR/Contents/Resources/ViospiceLib/.git"
elif [ -d "$HOME/ViospiceLib" ]; then
    info "Bundling ViospiceLib from $HOME/ViospiceLib..."
    mkdir -p "$APP_DIR/Contents/Resources/ViospiceLib"
    cp -rf "$HOME/ViospiceLib/"* "$APP_DIR/Contents/Resources/ViospiceLib/"
    rm -rf "$APP_DIR/Contents/Resources/ViospiceLib/.git"
elif [ -d "$ROOT_DIR/ViospiceLib" ]; then
    info "Bundling ViospiceLib from $ROOT_DIR/ViospiceLib..."
    mkdir -p "$APP_DIR/Contents/Resources/ViospiceLib"
    cp -rf "$ROOT_DIR/ViospiceLib/"* "$APP_DIR/Contents/Resources/ViospiceLib/"
    rm -rf "$APP_DIR/Contents/Resources/ViospiceLib/.git"
else
    info "Fetching ViospiceLib from remote repository..."
    LIB_CACHE="$BUILD_DIR/viora-libs"
    if [ ! -d "$LIB_CACHE" ]; then
        git clone --depth 1 https://github.com/Janadasroor/viora-libs.git "$LIB_CACHE" || warn "Could not clone viora-libs"
    fi
    if [ -d "$LIB_CACHE" ]; then
        mkdir -p "$APP_DIR/Contents/Resources/ViospiceLib"
        cp -rf "$LIB_CACHE/"* "$APP_DIR/Contents/Resources/ViospiceLib/"
        rm -rf "$APP_DIR/Contents/Resources/ViospiceLib/.git"
    fi
fi

# ---------------------------------------------------------------------------
# 5) Copy Shared Libraries & Frameworks
# ---------------------------------------------------------------------------
info "Bundling dynamic libraries into Contents/Frameworks..."
find "$BUILD_DIR" -maxdepth 3 -name "*.dylib" -exec cp -f {} "$APP_DIR/Contents/Frameworks/" \; 2>/dev/null || true

for brew_pkg in libomp zstd llvm; do
    if command -v brew >/dev/null 2>&1; then
        BREW_LIB="$(brew --prefix $brew_pkg 2>/dev/null)/lib"
        if [ -d "$BREW_LIB" ]; then
            find "$BREW_LIB" -maxdepth 1 -name "*.dylib" -exec cp -f {} "$APP_DIR/Contents/Frameworks/" \; 2>/dev/null || true
        fi
    fi
done

# ---------------------------------------------------------------------------
# 6) Deploy Qt Frameworks & Plugins via macdeployqt
# ---------------------------------------------------------------------------
MACDEPLOYQT=""
if command -v macdeployqt >/dev/null 2>&1; then
    MACDEPLOYQT="$(command -v macdeployqt)"
elif [ -n "${Qt6_DIR:-}" ] && [ -x "$Qt6_DIR/../../../bin/macdeployqt" ]; then
    MACDEPLOYQT="$Qt6_DIR/../../../bin/macdeployqt"
elif [ -n "${QT_DIR:-}" ] && [ -x "$QT_DIR/bin/macdeployqt" ]; then
    MACDEPLOYQT="$QT_DIR/bin/macdeployqt"
fi

if [ -n "$MACDEPLOYQT" ]; then
    info "Running macdeployqt ($MACDEPLOYQT)..."
    "$MACDEPLOYQT" "$APP_DIR" -qmldir="$ROOT_DIR/ui" -always-overwrite 2>&1 || warn "macdeployqt finished with non-fatal warnings"
else
    warn "macdeployqt not found; bundle will rely on system Qt libraries"
fi

# ---------------------------------------------------------------------------
# 7) Ad-hoc Code Signing for macOS Gatekeeper
# ---------------------------------------------------------------------------
if command -v codesign >/dev/null 2>&1; then
    info "Applying ad-hoc code signature..."
    codesign --force --deep --sign - "$APP_DIR" 2>/dev/null || warn "codesign exited with warnings"
fi

# ---------------------------------------------------------------------------
# 8) Create Distribution Archives
# ---------------------------------------------------------------------------
info "Creating distribution archives..."

DMG_STAGE="$STAGE_DIR/dmg_stage"
mkdir -p "$DMG_STAGE"
cp -rf "$APP_DIR" "$DMG_STAGE/"
ln -sf /Applications "$DMG_STAGE/Applications"

# Tarball
if [ "$DMG_ONLY" -eq 0 ]; then
    TARBALL="$OUT_DIR/${PKG_NAME}.tar.gz"
    info "Creating $TARBALL..."
    tar -czf "$TARBALL" -C "$STAGE_DIR" VioraEDA.app
    ok "Created $TARBALL ($(du -sh "$TARBALL" | cut -f1))"
fi

# DMG Package
if [ "$TAR_ONLY" -eq 0 ]; then
    DMG_FILE="$OUT_DIR/${PKG_NAME}.dmg"
    if command -v hdiutil >/dev/null 2>&1; then
        info "Creating DMG using hdiutil..."
        rm -f "$DMG_FILE"
        hdiutil create -volname "VioraEDA" -srcfolder "$DMG_STAGE" -ov -format UDZO "$DMG_FILE"
        ok "Created $DMG_FILE ($(du -sh "$DMG_FILE" | cut -f1))"
    elif command -v create-dmg >/dev/null 2>&1; then
        info "Creating DMG using create-dmg..."
        create-dmg --volname "VioraEDA" --window-pos 200 120 --window-size 800 400 --icon-size 100             --app-drop-link 600 185 "$DMG_FILE" "$DMG_STAGE" || true
        ok "Created $DMG_FILE"
    else
        warn "hdiutil not found, skipping DMG creation"
    fi
fi

info "=========================================================="
ok " macOS packaging complete!"
ls -lh "$OUT_DIR"
info "=========================================================="
