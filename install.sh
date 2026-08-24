#!/usr/bin/env bash
# install.sh — multiplatform installation script for VioraEDA & CLI
# Supports macOS, Linux, and Windows (MSYS2)

set -euo pipefail

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
if [ -t 1 ]; then
    C_RED=$'\033[31m'; C_GRN=$'\033[32m'; C_YLW=$'\033[33m'; C_CYN=$'\033[36m'
    C_END=$'\033[0m'
else
    C_RED=""; C_GRN=""; C_YLW=""; C_CYN=""; C_END=""
fi
info() { printf "${C_YLW}==>${C_END} %s\n" "$*"; }
ok()   { printf "${C_GRN}==>${C_END} %s\n" "$*"; }
warn() { printf "${C_YLW}!! ${C_END} %s\n" "$*" >&2; }
die()  { printf "${C_RED}ERROR: %s${C_END}\n" "$*" >&2; exit 1; }

SELF="${BASH_SOURCE[0]}"
ROOT="${SELF%/*}"
case "$ROOT" in
    /*|[A-Za-z]:/*) : ;;
    .)              ROOT="$(pwd)" ;;
    *)              ROOT="$(cd "$ROOT" && pwd)" ;;
esac
[ -z "$ROOT" ] && ROOT="$(pwd)"

case "$(uname -s)" in
    Linux*)          OS="linux" ;;
    Darwin*)         OS="macos" ;;
    MINGW*|MSYS*)    OS="windows" ;;
    *)               die "unsupported OS: $(uname -s)" ;;
esac

# Locate build directory
BUILD_DIR=""
for d in "$ROOT/build" "$ROOT/build-release"; do
    if [ -d "$d" ]; then
        BUILD_DIR="$d"
        break
    fi
done

[ -z "$BUILD_DIR" ] && die "Build directory not found. Please run ./build.sh first."

echo "${C_CYN}========================================${C_END}"
echo "${C_CYN}  Installing VioraEDA & Viora CLI       ${C_END}"
echo "${C_CYN}  Platform: $OS                         ${C_END}"
echo "${C_CYN}========================================${C_END}"
echo

# ---------------------------------------------------------------------------
# 1) Component Library Setup
# ---------------------------------------------------------------------------
LIB_DIR="$HOME/ViospiceLib"
mkdir -p "$LIB_DIR/sym" "$LIB_DIR/sub" "$LIB_DIR/cmp" "$LIB_DIR/lib"

if [ -d "$HOME/viora-libs" ] && [ ! -d "$LIB_DIR/sym/rcl" ]; then
    info "Populating component library from ~/viora-libs ..."
    cp -Rf "$HOME/viora-libs/"* "$LIB_DIR/" 2>/dev/null || true
    ok "Component libraries synchronized to $LIB_DIR."
fi

# ---------------------------------------------------------------------------
# 2) Platform-Specific Installation
# ---------------------------------------------------------------------------
case "$OS" in
    macos)
        # Choose application target directory
        TARGET_APP_DIR="/Applications"
        if [ ! -w "$TARGET_APP_DIR" ]; then
            TARGET_APP_DIR="$HOME/Applications"
            mkdir -p "$TARGET_APP_DIR"
        fi

        # Choose CLI binary directory
        CLI_BIN_DIR="/usr/local/bin"
        if [ ! -w "$CLI_BIN_DIR" ]; then
            CLI_BIN_DIR="$HOME/.local/bin"
            mkdir -p "$CLI_BIN_DIR"
        fi

        # Install GUI App Bundle
        if [ -d "$BUILD_DIR/VioraEDA.app" ]; then
            info "Installing VioraEDA.app to $TARGET_APP_DIR ..."
            rm -rf "$TARGET_APP_DIR/VioraEDA.app"
            cp -Rf "$BUILD_DIR/VioraEDA.app" "$TARGET_APP_DIR/"
            ok "Installed: $TARGET_APP_DIR/VioraEDA.app"
        else
            warn "VioraEDA.app not found in $BUILD_DIR."
        fi

        # Install CLI Tools
        for tool in viora flux_runner; do
            if [ -f "$BUILD_DIR/$tool" ]; then
                info "Installing $tool CLI to $CLI_BIN_DIR ..."
                rm -f "$CLI_BIN_DIR/$tool"
                cp -f "$BUILD_DIR/$tool" "$CLI_BIN_DIR/$tool"
                chmod +x "$CLI_BIN_DIR/$tool"
                ok "Installed: $CLI_BIN_DIR/$tool"
            fi
        done
        ;;

    linux)
        INSTALL_DIR="$HOME/.local/bin"
        ICON_DIR="$HOME/.local/share/icons/hicolor/256x256/apps"
        DESKTOP_DIR="$HOME/.local/share/applications"

        mkdir -p "$INSTALL_DIR" "$ICON_DIR" "$DESKTOP_DIR"

        # Install GUI Binary
        if [ -f "$BUILD_DIR/VioraEDA" ]; then
            info "Installing VioraEDA binary to $INSTALL_DIR ..."
            rm -f "$INSTALL_DIR/VioraEDA"
            cp -f "$BUILD_DIR/VioraEDA" "$INSTALL_DIR/VioraEDA"
            chmod +x "$INSTALL_DIR/VioraEDA"
            ok "Installed: $INSTALL_DIR/VioraEDA"
        fi

        # Install CLI Tools
        for tool in viora flux_runner; do
            if [ -f "$BUILD_DIR/$tool" ]; then
                info "Installing $tool CLI to $INSTALL_DIR ..."
                rm -f "$INSTALL_DIR/$tool"
                cp -f "$BUILD_DIR/$tool" "$INSTALL_DIR/$tool"
                chmod +x "$INSTALL_DIR/$tool"
                ok "Installed: $INSTALL_DIR/$tool"
            fi
        done

        # Install Desktop Icon & Entry
        if [ -f "$ROOT/resources/icons/logo_viospice.png" ]; then
            cp -f "$ROOT/resources/icons/logo_viospice.png" "$ICON_DIR/vioraeda.png"
        fi

        cat > "$DESKTOP_DIR/vioraeda.desktop" <<EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=VioraEDA
GenericName=AI-Assisted EDA & SPICE Simulator
Comment=Modern electronic design automation with Gemini AI integration
Exec=$INSTALL_DIR/VioraEDA
Icon=vioraeda
Terminal=false
Categories=Development;Engineering;Electronics;
Keywords=spice;eda;schematic;pcb;ai;gemini;
StartupNotify=true
EOF
        chmod +x "$DESKTOP_DIR/vioraeda.desktop"
        ok "Desktop entry created: $DESKTOP_DIR/vioraeda.desktop"
        ;;

    windows)
        INSTALL_DIR="$USERPROFILE/AppData/Local/VioraEDA"
        mkdir -p "$INSTALL_DIR"

        for f in "$BUILD_DIR"/*.exe "$BUILD_DIR"/*.dll; do
            [ -f "$f" ] && cp -f "$f" "$INSTALL_DIR/"
        done
        ok "Installed binaries to $INSTALL_DIR"
        ;;
esac

echo
ok "VioraEDA installation complete!"
echo "Make sure your PATH includes your local bin directory (e.g. ~/.local/bin or /usr/local/bin)."
