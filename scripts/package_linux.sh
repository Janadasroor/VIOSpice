#!/usr/bin/env bash
# ==============================================================================
# package_linux.sh — build the VioraEDA Linux installer and distribution packages
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
VioraEDA Linux installer & packaging build script.

Usage:
  ./scripts/package_linux.sh                 build .tar.gz bundle, .deb package & AppImage
  ./scripts/package_linux.sh --appimage      build standalone .AppImage bundle
  ./scripts/package_linux.sh --deb-only      build only the Debian .deb package
  ./scripts/package_linux.sh --tar-only      build only the portable .tar.gz bundle
  ./scripts/package_linux.sh --lib <dir>     bundle custom component library
  ./scripts/package_linux.sh --jobs 8        limit parallel build jobs
  ./scripts/package_linux.sh --help          this help dialog

Output: build/installer/VioraEDA-<ver>-linux-x86_64.{tar.gz,deb,AppImage}
EOF
    exit 0
}

SELF="${BASH_SOURCE[0]}"
ROOT="${SELF%/*}"; case "$ROOT" in /*) ;; *) ROOT="$(cd "$ROOT" && pwd)" ;; esac
ROOT="$(cd "$ROOT/.." && pwd)"

BUILD_DIR="$ROOT/build"
OUT_DIR="$BUILD_DIR/installer"
LIB_SRC=""
DEB_ONLY=0
TAR_ONLY=0
APPIMAGE_ONLY=0
JOBS="$(nproc 2>/dev/null || echo 8)"

while [ $# -gt 0 ]; do
    case "$1" in
        --help|-h)        usage ;;
        --deb-only)       DEB_ONLY=1 ;;
        --tar-only)       TAR_ONLY=1 ;;
        --appimage-only)  APPIMAGE_ONLY=1 ;;
        --appimage)       APPIMAGE_ONLY=1 ;;
        --lib)
            shift; [ -d "${1:-}" ] || die "--lib needs an existing directory"
            LIB_SRC="$(cd "$1" && pwd)" ;;
        --jobs)
            shift; case "${1:-}" in ''|*[!0-9]*) die "--jobs needs a number" ;; *) JOBS="$1" ;; esac ;;
        *) die "unknown option: $1 (try --help)" ;;
    esac
    shift
done

info "Project root : $ROOT"
info "Build jobs   : $JOBS"

# ---------------------------------------------------------------------------
# 1) Build binaries
# ---------------------------------------------------------------------------
info "Building VioraEDA, CLI, setup installer, and utilities..."
cmake --build "$BUILD_DIR" -j"$JOBS" --target VioraEDA viora flux_runner flux-lsp VioraEDA_Setup

VERSION="${VERSION:-0.2.0-beta}"
VERSION="${VERSION#v}"
PKG_NAME="VioraEDA-${VERSION}-linux-x86_64"
STAGE_DIR="$BUILD_DIR/stage/$PKG_NAME"

rm -rf "$BUILD_DIR/stage"
mkdir -p "$STAGE_DIR/bin"
mkdir -p "$STAGE_DIR/lib"
mkdir -p "$STAGE_DIR/models"
mkdir -p "$STAGE_DIR/resources/installer/linux"
mkdir -p "$OUT_DIR"

# ---------------------------------------------------------------------------
# 2) Stage files for packaging
# ---------------------------------------------------------------------------
info "Staging package assets..."

# Binaries
cp -f "$BUILD_DIR/VioraEDA" "$STAGE_DIR/bin/" 2>/dev/null || true
cp -f "$BUILD_DIR/viora" "$STAGE_DIR/bin/" 2>/dev/null || true
cp -f "$BUILD_DIR/flux_runner" "$STAGE_DIR/bin/" 2>/dev/null || true
cp -f "$BUILD_DIR/flux-lsp" "$STAGE_DIR/bin/" 2>/dev/null || true
cp -f "$BUILD_DIR/VioraEDA_Setup" "$STAGE_DIR/bin/" 2>/dev/null || true
find "$BUILD_DIR" -name "vioavr" -type f -exec cp -f {} "$STAGE_DIR/bin/" \; 2>/dev/null || true

# Copy library/model assets
if [ -d "$ROOT/models" ]; then
    cp -rf "$ROOT/models/"* "$STAGE_DIR/models/" 2>/dev/null || true
fi
if [ -d "$ROOT/python" ]; then
    mkdir -p "$STAGE_DIR/python"
    cp -rf "$ROOT/python/"* "$STAGE_DIR/python/" 2>/dev/null || true
fi
if [ -d "$ROOT/templates" ]; then
    mkdir -p "$STAGE_DIR/templates"
    cp -rf "$ROOT/templates/"* "$STAGE_DIR/templates/" 2>/dev/null || true
fi
if [ -d "$ROOT/python/templates" ]; then
    mkdir -p "$STAGE_DIR/templates/flux"
    cp -rf "$ROOT/python/templates/"*.flux "$STAGE_DIR/templates/flux/" 2>/dev/null || true
fi
if [ -n "$LIB_SRC" ] && [ -d "$LIB_SRC" ]; then
    info "Bundling ViospiceLib from custom source ($LIB_SRC)..."
    mkdir -p "$STAGE_DIR/ViospiceLib"
    cp -rf "$LIB_SRC/"* "$STAGE_DIR/ViospiceLib/"
elif [ -d "$HOME/ViospiceLib" ]; then
    info "Bundling ViospiceLib from $HOME/ViospiceLib..."
    mkdir -p "$STAGE_DIR/ViospiceLib"
    cp -rf "$HOME/ViospiceLib/"* "$STAGE_DIR/ViospiceLib/"
elif [ -d "$ROOT/ViospiceLib" ]; then
    info "Bundling ViospiceLib from $ROOT/ViospiceLib..."
    mkdir -p "$STAGE_DIR/ViospiceLib"
    cp -rf "$ROOT/ViospiceLib/"* "$STAGE_DIR/ViospiceLib/"
else
    info "Fetching ViospiceLib from remote repository (https://github.com/Janadasroor/viora-libs.git)..."
    LIB_CACHE="$BUILD_DIR/viora-libs"
    if [ ! -d "$LIB_CACHE" ]; then
        git clone --depth 1 https://github.com/Janadasroor/viora-libs.git "$LIB_CACHE" || warn "Could not clone viora-libs"
    fi
    if [ -d "$LIB_CACHE" ]; then
        mkdir -p "$STAGE_DIR/ViospiceLib"
        cp -rf "$LIB_CACHE/"* "$STAGE_DIR/ViospiceLib/"
        rm -rf "$STAGE_DIR/ViospiceLib/.git"
    fi
fi

# Bundle Qt runtime libraries & plugins for fully standalone offline execution
if [ -z "${QT_DIR:-}" ]; then
    if [ -n "${Qt6_DIR:-}" ] && [ -d "$Qt6_DIR/../../.." ]; then
        QT_DIR="$(cd "$Qt6_DIR/../../.." && pwd)"
    elif [ -d "/home/jnd/Qt/6.11.0/gcc_64" ]; then
        QT_DIR="/home/jnd/Qt/6.11.0/gcc_64"
    elif [ -d "/home/jnd/Qt/6.10.0/gcc_64" ]; then
        QT_DIR="/home/jnd/Qt/6.10.0/gcc_64"
    else
        QT_PREFIX="$(qmake6 -query QT_INSTALL_PREFIX 2>/dev/null || qmake -query QT_INSTALL_PREFIX 2>/dev/null || true)"
        if [ -n "$QT_PREFIX" ] && [ -d "$QT_PREFIX" ]; then
            QT_DIR="$QT_PREFIX"
        fi
    fi
fi

if [ -n "${QT_DIR:-}" ] && [ -d "$QT_DIR/lib" ]; then
    info "Bundling Qt6 ($QT_DIR) shared libraries, plugins, and QML modules for offline execution..."
    cp -P "$QT_DIR/lib/"libQt6*.so* "$STAGE_DIR/lib/" 2>/dev/null || true
    cp -P "$QT_DIR/lib/"libicu*.so* "$STAGE_DIR/lib/" 2>/dev/null || true
    # Platform, database, tls, and image format plugins
    mkdir -p "$STAGE_DIR/plugins/platforms"
    mkdir -p "$STAGE_DIR/plugins/imageformats"
    mkdir -p "$STAGE_DIR/plugins/iconengines"
    mkdir -p "$STAGE_DIR/plugins/xcbglintegrations"
    mkdir -p "$STAGE_DIR/plugins/sqldrivers"
    mkdir -p "$STAGE_DIR/plugins/tls"
    mkdir -p "$STAGE_DIR/plugins/networkinformation"
    [ -d "$QT_DIR/plugins/platforms" ] && cp -rf "$QT_DIR/plugins/platforms/"* "$STAGE_DIR/plugins/platforms/"
    [ -d "$QT_DIR/plugins/imageformats" ] && cp -rf "$QT_DIR/plugins/imageformats/"* "$STAGE_DIR/plugins/imageformats/"
    [ -d "$QT_DIR/plugins/iconengines" ] && cp -rf "$QT_DIR/plugins/iconengines/"* "$STAGE_DIR/plugins/iconengines/"
    [ -d "$QT_DIR/plugins/xcbglintegrations" ] && cp -rf "$QT_DIR/plugins/xcbglintegrations/"* "$STAGE_DIR/plugins/xcbglintegrations/"
    [ -d "$QT_DIR/plugins/sqldrivers" ] && cp -rf "$QT_DIR/plugins/sqldrivers/"* "$STAGE_DIR/plugins/sqldrivers/"
    [ -d "$QT_DIR/plugins/tls" ] && cp -rf "$QT_DIR/plugins/tls/"* "$STAGE_DIR/plugins/tls/"
    [ -d "$QT_DIR/plugins/networkinformation" ] && cp -rf "$QT_DIR/plugins/networkinformation/"* "$STAGE_DIR/plugins/networkinformation/"

    # Bundle QtQuick QML imports
    if [ -d "$QT_DIR/qml" ]; then
        info "Bundling Qt6 QML module imports..."
        mkdir -p "$STAGE_DIR/qml"
        for qmod in QtQuick Qt QML QtCore; do
            [ -d "$QT_DIR/qml/$qmod" ] && cp -rf "$QT_DIR/qml/$qmod" "$STAGE_DIR/qml/"
        done
    fi
fi

# Bundle all dynamic dependencies discovered via ldd (except glibc base libc/libm/libpthread/libdl)
info "Bundling complete standalone shared library closure..."
for b in "$STAGE_DIR/bin/"*.bin "$BUILD_DIR/VioraEDA" "$BUILD_DIR/viora" "$BUILD_DIR/VioraEDA_Setup" "$BUILD_DIR/flux_runner"; do
    if [ -f "$b" ]; then
        for dep in $(ldd "$b" 2>/dev/null | awk '{print $3}' | grep '^/' || true); do
            dep_base="$(basename "$dep")"
            case "$dep_base" in
                libc.so*|libm.so*|libpthread.so*|libdl.so*|libresolv.so*|/lib64/*)
                    # Keep glibc / core kernel interface native
                    ;;
                *)
                    if [ -f "$dep" ] && [ ! -f "$STAGE_DIR/lib/$dep_base" ]; then
                        cp -L "$dep" "$STAGE_DIR/lib/" 2>/dev/null || true
                    fi
                    ;;
            esac
        done
    fi
done

# Bundle VioMATRIXC / ngspice engine libraries & code models
if [ -d "$BUILD_DIR/viomatrixc-prebuilt/lib" ]; then
    info "Bundling VioMATRIXC simulation engine..."
    cp -P "$BUILD_DIR/viomatrixc-prebuilt/lib/"libngspice.so* "$STAGE_DIR/lib/" 2>/dev/null || true
fi
if [ -d "$BUILD_DIR/cm" ]; then
    mkdir -p "$STAGE_DIR/cm"
    cp -rf "$BUILD_DIR/cm/"* "$STAGE_DIR/cm/" 2>/dev/null || true
fi
if [ -z "$(ls -A "$STAGE_DIR/cm" 2>/dev/null)" ] && [ -d "$BUILD_DIR/viomatrixc-prebuilt/lib/ngspice" ]; then
    for f in "$BUILD_DIR"/viomatrixc-prebuilt/lib/ngspice/*.cm; do
        [ -f "$f" ] && cp "$f" "$STAGE_DIR/cm/" 2>/dev/null || true
    done
fi

# Bundle FluxScript engine library
if [ -f "$BUILD_DIR/FluxScript/libFluxScript.so" ]; then
    info "Bundling FluxScript engine library..."
    cp -P "$BUILD_DIR/FluxScript/libFluxScript.so"* "$STAGE_DIR/lib/" 2>/dev/null || true
elif [ -f "$BUILD_DIR/fluxscript-prebuilt/lib/libFluxScript.so" ]; then
    info "Bundling FluxScript prebuilt library..."
    cp -P "$BUILD_DIR/fluxscript-prebuilt/lib/libFluxScript.so"* "$STAGE_DIR/lib/" 2>/dev/null || true
fi

# Bundle LLVM shared libraries if present in build tree
if compgen -G "$BUILD_DIR/libLLVM*.so*" > /dev/null; then
    info "Bundling LLVM runtime libraries..."
    cp -P "$BUILD_DIR"/libLLVM*.so* "$STAGE_DIR/lib/" 2>/dev/null || true
fi

# Create launch wrapper script for VioraEDA and VioraEDA_Setup to set LD_LIBRARY_PATH and QT_PLUGIN_PATH
cat <<'EOF' > "$STAGE_DIR/bin/viora_env_wrapper.sh"
#!/usr/bin/env bash
SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do
  DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
  SOURCE="$(readlink "$SOURCE")"
  [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
done
SCRIPT_DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
BASE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

export LD_LIBRARY_PATH="$BASE_DIR/lib:$BASE_DIR/bin:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="$BASE_DIR/plugins:${QT_PLUGIN_PATH:-}"
export QML2_IMPORT_PATH="$BASE_DIR/qml:${QML2_IMPORT_PATH:-}"
export VIOSPICE_HOME="${VIOSPICE_HOME:-$BASE_DIR}"
export SPICE_LIB_DIR="${SPICE_LIB_DIR:-$BASE_DIR/models}"
EXE_NAME="$1"
shift
exec "$SCRIPT_DIR/$EXE_NAME" "$@"
EOF
chmod +x "$STAGE_DIR/bin/viora_env_wrapper.sh"

# Wrap entrypoints
for b in VioraEDA viora flux_runner VioraEDA_Setup vioavr; do
    if [ -f "$STAGE_DIR/bin/$b" ]; then
        mv "$STAGE_DIR/bin/$b" "$STAGE_DIR/bin/${b}.bin"
        cat <<EOF > "$STAGE_DIR/bin/$b"
#!/usr/bin/env bash
SOURCE="\${BASH_SOURCE[0]}"
while [ -h "\$SOURCE" ]; do
  DIR="\$(cd -P "\$(dirname "\$SOURCE")" && pwd)"
  SOURCE="\$(readlink "\$SOURCE")"
  [[ \$SOURCE != /* ]] && SOURCE="\$DIR/\$SOURCE"
done
SCRIPT_DIR="\$(cd -P "\$(dirname "\$SOURCE")" && pwd)"
exec "\$SCRIPT_DIR/viora_env_wrapper.sh" "${b}.bin" "\$@"
EOF
        chmod +x "$STAGE_DIR/bin/$b"
    fi
done

# Copy FreeDesktop Linux Assets
cp -rf "$ROOT/resources/installer/linux/"* "$STAGE_DIR/resources/installer/linux/"

# Copy install / uninstall scripts
cp -f "$ROOT/tools/installer/linux/install.sh" "$STAGE_DIR/install.sh"
cp -f "$ROOT/tools/installer/linux/uninstall.sh" "$STAGE_DIR/uninstall.sh"
chmod +x "$STAGE_DIR/install.sh" "$STAGE_DIR/uninstall.sh" "$STAGE_DIR/bin/"*

# ---------------------------------------------------------------------------
# 3) Create Tarball Distribution (.tar.gz)
# ---------------------------------------------------------------------------
if [ "$DEB_ONLY" = "0" ]; then
    info "Generating portable distribution tarball: ${PKG_NAME}.tar.gz..."
    tar -czf "$OUT_DIR/${PKG_NAME}.tar.gz" -C "$BUILD_DIR/stage" "$PKG_NAME"
    ok "Created $OUT_DIR/${PKG_NAME}.tar.gz"
fi

# ---------------------------------------------------------------------------
# 4) Create Debian Package (.deb) if dpkg-deb is available
# ---------------------------------------------------------------------------
if [ "$TAR_ONLY" = "0" ] && command -v dpkg-deb >/dev/null 2>&1; then
    info "Generating Debian / Ubuntu package (.deb)..."
    DEB_STAGE="$BUILD_DIR/deb_stage"
    rm -rf "$DEB_STAGE"
    mkdir -p "$DEB_STAGE/DEBIAN"
    mkdir -p "$DEB_STAGE/opt/VioraEDA"
    mkdir -p "$DEB_STAGE/usr/share/applications"
    mkdir -p "$DEB_STAGE/usr/share/mime/packages"
    mkdir -p "$DEB_STAGE/usr/share/metainfo"
    mkdir -p "$DEB_STAGE/usr/share/icons/hicolor"
    mkdir -p "$DEB_STAGE/usr/bin"

    # Copy files to /opt/VioraEDA
    cp -rf "$STAGE_DIR/"* "$DEB_STAGE/opt/VioraEDA/"

    # Symlink binaries in /usr/bin
    ln -sf "/opt/VioraEDA/bin/VioraEDA" "$DEB_STAGE/usr/bin/VioraEDA"
    ln -sf "/opt/VioraEDA/bin/viora" "$DEB_STAGE/usr/bin/viora"
    ln -sf "/opt/VioraEDA/bin/flux_runner" "$DEB_STAGE/usr/bin/flux_runner"
    ln -sf "/opt/VioraEDA/bin/VioraEDA_Setup" "$DEB_STAGE/usr/bin/VioraEDA_Setup"
    [ -f "$STAGE_DIR/bin/vioavr" ] && ln -sf "/opt/VioraEDA/bin/vioavr" "$DEB_STAGE/usr/bin/vioavr" || true

    # Install desktop, mime, metainfo, and icons
    cp -f "$ROOT/resources/installer/linux/vioraeda.desktop" "$DEB_STAGE/usr/share/applications/"
    cp -f "$ROOT/resources/installer/linux/vioraeda-mime.xml" "$DEB_STAGE/usr/share/mime/packages/"
    cp -f "$ROOT/resources/installer/linux/io.viora.VioraEDA.metainfo.xml" "$DEB_STAGE/usr/share/metainfo/"
    cp -rf "$ROOT/resources/installer/linux/icons/hicolor/"* "$DEB_STAGE/usr/share/icons/hicolor/"

    # Write DEBIAN/control
    cat <<EOF > "$DEB_STAGE/DEBIAN/control"
Package: vioraeda
Version: ${VERSION#v}
Section: electronics
Priority: optional
Architecture: amd64
Depends: libc6, libgl1, libxkbcommon-x11-0, fontconfig, fonts-dejavu-core | fonts-freefont-ttf
Maintainer: Janada Sroor <janada@vioraeda.io>
Description: Electronic Design Automation Suite & SPICE Simulator
 VioraEDA is a high-performance EDA suite integrating schematic capture,
 mixed-signal SPICE simulation, AVR co-simulation, and PCB layout.
EOF

    # Write DEBIAN/postinst
    cat <<'EOF' > "$DEB_STAGE/DEBIAN/postinst"
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications || true
fi
if command -v update-mime-database >/dev/null 2>&1; then
    update-mime-database /usr/share/mime || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor || true
fi
exit 0
EOF
    chmod 755 "$DEB_STAGE/DEBIAN/postinst"

    # Write DEBIAN/prerm
    cat <<'EOF' > "$DEB_STAGE/DEBIAN/prerm"
#!/bin/sh
set -e
rm -f /usr/bin/VioraEDA /usr/bin/viora /usr/bin/flux_runner /usr/bin/VioraEDA_Setup /usr/bin/vioavr
exit 0
EOF
    chmod 755 "$DEB_STAGE/DEBIAN/prerm"

    dpkg-deb --build "$DEB_STAGE" "$OUT_DIR/${PKG_NAME}.deb" >/dev/null
    ok "Created $OUT_DIR/${PKG_NAME}.deb"
fi

# ==============================================================================
# 9. AppImage Packaging
# ==============================================================================
if [ "$DEB_ONLY" -eq 0 ] && [ "$TAR_ONLY" -eq 0 ]; then
    info "Generating AppImage bundle..."
    APPDIR="$STAGE_DIR/AppDir"
    rm -rf "$APPDIR"
    mkdir -p "$APPDIR/usr"

    # Copy files into AppDir
    [ -d "$STAGE_DIR/bin" ] && cp -rf "$STAGE_DIR/bin" "$APPDIR/usr/"
    [ -d "$STAGE_DIR/lib" ] && cp -rf "$STAGE_DIR/lib" "$APPDIR/usr/"
    [ -d "$STAGE_DIR/plugins" ] && cp -rf "$STAGE_DIR/plugins" "$APPDIR/usr/"
    [ -d "$STAGE_DIR/resources" ] && cp -rf "$STAGE_DIR/resources" "$APPDIR/usr/"
    [ -d "$STAGE_DIR/share" ] && cp -rf "$STAGE_DIR/share" "$APPDIR/usr/"
    [ -d "$STAGE_DIR/qml" ] && cp -rf "$STAGE_DIR/qml" "$APPDIR/usr/"
    [ -d "$STAGE_DIR/models" ] && cp -rf "$STAGE_DIR/models" "$APPDIR/usr/"
    [ -d "$STAGE_DIR/templates" ] && cp -rf "$STAGE_DIR/templates" "$APPDIR/usr/"
    [ -d "$STAGE_DIR/python" ] && cp -rf "$STAGE_DIR/python" "$APPDIR/usr/"
    [ -d "$STAGE_DIR/cm" ] && cp -rf "$STAGE_DIR/cm" "$APPDIR/usr/"
    [ -d "$STAGE_DIR/ViospiceLib" ] && cp -rf "$STAGE_DIR/ViospiceLib" "$APPDIR/usr/"

    cp -f "$ROOT/resources/installer/linux/vioraeda.desktop" "$APPDIR/"
    cp -f "$ROOT/resources/installer/linux/icons/hicolor/scalable/apps/vioraeda.svg" "$APPDIR/vioraeda.svg"
    cp -f "$ROOT/resources/installer/linux/icons/hicolor/256x256/apps/vioraeda.png" "$APPDIR/vioraeda.png"
    ln -sf vioraeda.png "$APPDIR/.DirIcon"

    # Create AppRun script
    cat <<'EOF' > "$APPDIR/AppRun"
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${HERE}/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="${HERE}/usr/plugins:${QT_PLUGIN_PATH:-}"
export QML2_IMPORT_PATH="${HERE}/usr/qml:${QML2_IMPORT_PATH:-}"
export VIOSPICE_HOME="${VIOSPICE_HOME:-$HERE/usr}"
export SPICE_LIB_DIR="${SPICE_LIB_DIR:-$HERE/usr/models}"
exec "${HERE}/usr/bin/VioraEDA" "$@"
EOF
    chmod 755 "$APPDIR/AppRun"

    # Download appimagetool if not locally available
    AI_TOOL="$BUILD_DIR/appimagetool"
    if ! command -v appimagetool >/dev/null 2>&1; then
        if [ ! -f "$AI_TOOL" ]; then
            info "Fetching appimagetool..."
            curl -fsSL -o "$AI_TOOL" "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" 2>/dev/null || true
            [ -f "$AI_TOOL" ] && chmod +x "$AI_TOOL" || true
        fi
    else
        AI_TOOL="appimagetool"
    fi

    if [ -x "$AI_TOOL" ] || command -v appimagetool >/dev/null 2>&1; then
        ARCH=x86_64 "$AI_TOOL" --appimage-extract-and-run "$APPDIR" "$OUT_DIR/${PKG_NAME}.AppImage" >/dev/null 2>&1 || {
            warn "AppImage generation skipped (appimagetool could not package $APPDIR)"
        }
        [ -f "$OUT_DIR/${PKG_NAME}.AppImage" ] && ok "Created $OUT_DIR/${PKG_NAME}.AppImage"
    else
        warn "appimagetool not found; AppDir prepared at $APPDIR"
    fi
fi

echo ""
ok "Linux packaging complete."
echo "Installer packages generated in $OUT_DIR:"
ls -lh "$OUT_DIR" | grep "VioraEDA" | sed 's/^/  /'


