#!/usr/bin/env bash
# ==============================================================================
# VioraEDA macOS Standalone Packaging Script (Mirroring Release CI)
# Builds standalone DMG and TAR.GZ packages containing complete Qt 6 runtimes,
# plugins, QML modules, simulation code models, and ViospiceLib component library.
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="$ROOT/build"
OUT_DIR="$BUILD_DIR/installer"
VERSION="${VERSION:-0.2.0-beta}"
LIB_SRC=""
JOBS=$(sysctl -n hw.logicalcpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# Logging helpers
info()  { echo -e "\033[1;34m[INFO]\033[0m $*"; }
ok()    { echo -e "\033[1;32m[OK]\033[0m $*"; }
warn()  { echo -e "\033[1;33m[WARN]\033[0m $*"; }
err()   { echo -e "\033[1;31m[ERROR]\033[0m $*" >&2; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --out-dir)   OUT_DIR="$2"; shift 2 ;;
        --version)   VERSION="$2"; shift 2 ;;
        --lib)       LIB_SRC="$2"; shift 2 ;;
        -j|--jobs)   JOBS="$2"; shift 2 ;;
        *) err "Unknown option: $1"; exit 1 ;;
    esac
done

VERSION="${VERSION#v}"
PKG_NAME="VioraEDA-${VERSION}-macos-arm64"
STAGE_DIR="$BUILD_DIR/stage_macos"
APP_DIR="$STAGE_DIR/VioraEDA.app"

info "=========================================================="
info " VioraEDA macOS Standalone Packaging Tool"
info " Version    : $VERSION"
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
info "Building VioraEDA, CLI, setup installer, and utilities..."
for tgt in VioraEDA viora flux_runner VioraEDA_Setup; do
    cmake --build "$BUILD_DIR" -j"$JOBS" --target "$tgt" || true
done
cmake --build "$BUILD_DIR" -j"$JOBS" --target flux-lsp 2>/dev/null || true

# ---------------------------------------------------------------------------
# 2) Stage Executables into VioraEDA.app/Contents/MacOS
# ---------------------------------------------------------------------------
info "Staging executables into App Bundle..."
if [ -f "$BUILD_DIR/VioraEDA.app/Contents/MacOS/VioraEDA" ]; then
    cp -f "$BUILD_DIR/VioraEDA.app/Contents/MacOS/VioraEDA" "$APP_DIR/Contents/MacOS/"
elif [ -f "$BUILD_DIR/VioraEDA" ]; then
    cp -f "$BUILD_DIR/VioraEDA" "$APP_DIR/Contents/MacOS/"
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
if [ -f "$ROOT/resources/installer/macos/Info.plist.in" ]; then
    sed "s|@PROJECT_VERSION@|$VERSION|g" "$ROOT/resources/installer/macos/Info.plist.in" > "$APP_DIR/Contents/Info.plist"
fi
echo -n "APPL????" > "$APP_DIR/Contents/PkgInfo"

if [ -f "$ROOT/resources/installer/macos/vioraeda.icns" ]; then
    cp -f "$ROOT/resources/installer/macos/vioraeda.icns" "$APP_DIR/Contents/Resources/vioraeda.icns"
fi

# ---------------------------------------------------------------------------
# 4) Stage Resources (models, templates, python, cm, ViospiceLib)
# ---------------------------------------------------------------------------
info "Staging resources, templates, and code models..."
[ -d "$ROOT/models" ] && cp -rf "$ROOT/models" "$APP_DIR/Contents/Resources/" 2>/dev/null || true
[ -d "$ROOT/templates" ] && cp -rf "$ROOT/templates" "$APP_DIR/Contents/Resources/" 2>/dev/null || true
if [ -d "$ROOT/python" ]; then
    mkdir -p "$APP_DIR/Contents/Resources/python"
    cp -rf "$ROOT/python/"* "$APP_DIR/Contents/Resources/python/" 2>/dev/null || true
fi
if [ -d "$ROOT/python/templates" ]; then
    mkdir -p "$APP_DIR/Contents/Resources/templates/flux"
    cp -rf "$ROOT/python/templates/"*.flux "$APP_DIR/Contents/Resources/templates/flux/" 2>/dev/null || true
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
if [ -n "$LIB_SRC" ] && [ -d "$LIB_SRC" ]; then
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
elif [ -d "$ROOT/ViospiceLib" ]; then
    info "Bundling ViospiceLib from $ROOT/ViospiceLib..."
    mkdir -p "$APP_DIR/Contents/Resources/ViospiceLib"
    cp -rf "$ROOT/ViospiceLib/"* "$APP_DIR/Contents/Resources/ViospiceLib/"
    rm -rf "$APP_DIR/Contents/Resources/ViospiceLib/.git"
else
    info "Fetching ViospiceLib from remote repository (https://github.com/Janadasroor/viora-libs.git)..."
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
find "$BUILD_DIR" -name "*.dylib" -exec cp -f {} "$APP_DIR/Contents/Frameworks/" \; 2>/dev/null || true

# Check Homebrew libomp / zstd / LLVM if present
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
if command -v macdeployqt >/dev/null 2>&1; then
    info "Running macdeployqt to deploy Qt runtime and plugins..."
    macdeployqt "$APP_DIR" -qmldir="$ROOT/ui" -always-overwrite 2>&1 || warn "macdeployqt finished with warnings"
elif [ -n "${Qt6_DIR:-}" ] && [ -x "$Qt6_DIR/../../../bin/macdeployqt" ]; then
    info "Running macdeployqt from Qt6_DIR..."
    "$Qt6_DIR/../../../bin/macdeployqt" "$APP_DIR" -qmldir="$ROOT/ui" -always-overwrite 2>&1 || warn "macdeployqt finished with warnings"
fi

# ---------------------------------------------------------------------------
# 7) Create Distribution Archives
# ---------------------------------------------------------------------------
info "Creating distribution archives..."

# Tarball
DMG_STAGE="$STAGE_DIR/dmg_stage"
mkdir -p "$DMG_STAGE"
cp -rf "$APP_DIR" "$DMG_STAGE/"
ln -sf /Applications "$DMG_STAGE/Applications"

# Standalone CLI tools archive alongside the App
TARBALL="$OUT_DIR/${PKG_NAME}.tar.gz"
info "Creating $TARBALL..."
tar -czf "$TARBALL" -C "$STAGE_DIR" VioraEDA.app
ok "Created $TARBALL ($(du -sh "$TARBALL" | cut -f1))"

# DMG Package
DMG_FILE="$OUT_DIR/VioraEDA-${VERSION}-macos-arm64.dmg"
if command -v hdiutil >/dev/null 2>&1; then
    info "Creating DMG using hdiutil..."
    rm -f "$DMG_FILE"
    hdiutil create -volname "VioraEDA" -srcfolder "$DMG_STAGE" -ov -format UDZO "$DMG_FILE"
    ok "Created $DMG_FILE ($(du -sh "$DMG_FILE" | cut -f1))"
elif command -v create-dmg >/dev/null 2>&1; then
    info "Creating DMG using create-dmg..."
    create-dmg --volname "VioraEDA" --window-pos 200 120 --window-size 800 400 --icon-size 100 \
        --app-drop-link 600 185 "$DMG_FILE" "$DMG_STAGE" || true
    ok "Created $DMG_FILE"
else
    warn "hdiutil/create-dmg not found, skipping DMG creation"
fi

info "=========================================================="
ok " macOS packaging complete!"
ls -lh "$OUT_DIR"
info "=========================================================="
