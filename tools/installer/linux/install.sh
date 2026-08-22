#!/usr/bin/env bash
# ==============================================================================
# VioraEDA Linux Standalone Installation Script
# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# ANSI Color codes
BOLD="\033[1m"
GREEN="\033[32m"
CYAN="\033[36m"
YELLOW="\033[33m"
RED="\033[31m"
RESET="\033[0m"

echo -e "${CYAN}${BOLD}"
echo "  __     ___                 ______ _____        "
echo "  \ \   / (_)               |  ____|  __ \   /\  "
echo "   \ \_/ / _  ___  _ __ __ _| |__  | |  | | /  \ "
echo "    \   / | |/ _ \| '__/ _\` |  __| | |  | |/ /\ \ "
echo "     | |  | | (_) | | | (_| | |____| |__| / ____ \\"
echo "     |_|  |_|\___/|_|  \__,_|______|_____/_/    \_\\"
echo -e "${RESET}"
echo -e "${BOLD}VioraEDA Suite 2026 — Linux Installer${RESET}\n"

# Default installation directory
if [ "$EUID" -eq 0 ]; then
    DEFAULT_PREFIX="/opt/VioraEDA"
    BIN_DIR="/usr/local/bin"
    DESKTOP_DIR="/usr/share/applications"
    MIME_DIR="/usr/share/mime/packages"
    ICONS_BASE="/usr/share/icons/hicolor"
    SYSTEM_MODE=true
else
    DEFAULT_PREFIX="$HOME/.local/share/VioraEDA"
    BIN_DIR="$HOME/.local/bin"
    DESKTOP_DIR="$HOME/.local/share/applications"
    MIME_DIR="$HOME/.local/share/mime/packages"
    ICONS_BASE="$HOME/.local/share/icons/hicolor"
    SYSTEM_MODE=false
fi

PREFIX="$DEFAULT_PREFIX"
SILENT=false

# Argument parsing
while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix=*)
            PREFIX="${1#*=}"
            shift
            ;;
        --prefix)
            PREFIX="$2"
            shift 2
            ;;
        -y|--silent|--yes)
            SILENT=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --prefix=PATH    Set installation target directory (Default: $DEFAULT_PREFIX)"
            echo "  -y, --silent     Non-interactive silent mode (auto-confirm prompts)"
            echo "  -h, --help       Show this help dialog"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${RESET}"
            exit 1
            ;;
    esac
done

if [ "$SILENT" = false ]; then
    echo -e "Installation Destination: ${BOLD}$PREFIX${RESET}"
    echo -e "Mode: $(if [ "$SYSTEM_MODE" = true ]; then echo "${GREEN}System-wide (root)${RESET}"; else echo "${YELLOW}User Mode ($USER)${RESET}"; fi)"
    echo ""
    read -p "Proceed with installation? [Y/n] " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Nn]$ ]]; then
        echo -e "${YELLOW}Installation cancelled.${RESET}"
        exit 0
    fi
fi

echo -e "\n${CYAN}[1/5] Creating directories...${RESET}"
mkdir -p "$PREFIX/bin"
mkdir -p "$PREFIX/lib"
mkdir -p "$PREFIX/models"
mkdir -p "$PREFIX/ViospiceLib"
mkdir -p "$PREFIX/resources"
mkdir -p "$BIN_DIR"
mkdir -p "$DESKTOP_DIR"
mkdir -p "$MIME_DIR"

echo -e "${CYAN}[2/5] Copying binaries and runtime assets...${RESET}"
if [ -d "$PACKAGE_DIR/bin" ]; then
    cp -rf "$PACKAGE_DIR/bin/"* "$PREFIX/bin/"
elif [ -f "$SCRIPT_DIR/VioraEDA" ]; then
    cp -f "$SCRIPT_DIR/VioraEDA" "$PREFIX/bin/"
    [ -f "$SCRIPT_DIR/viora" ] && cp -f "$SCRIPT_DIR/viora" "$PREFIX/bin/"
fi

# Copy libraries / models if present
[ -d "$PACKAGE_DIR/lib" ] && cp -rf "$PACKAGE_DIR/lib/"* "$PREFIX/lib/"
[ -d "$PACKAGE_DIR/models" ] && cp -rf "$PACKAGE_DIR/models/"* "$PREFIX/models/"
[ -d "$PACKAGE_DIR/ViospiceLib" ] && cp -rf "$PACKAGE_DIR/ViospiceLib/"* "$PREFIX/ViospiceLib/"

# Ensure executable permissions
chmod +x "$PREFIX/bin/"* 2>/dev/null || true

echo -e "${CYAN}[3/5] Setting up PATH symlinks...${RESET}"
ln -sf "$PREFIX/bin/VioraEDA" "$BIN_DIR/VioraEDA" 2>/dev/null || true
ln -sf "$PREFIX/bin/viora" "$BIN_DIR/viora" 2>/dev/null || true
[ -f "$PREFIX/bin/flux_runner" ] && ln -sf "$PREFIX/bin/flux_runner" "$BIN_DIR/flux_runner" 2>/dev/null || true
[ -f "$PREFIX/bin/vioavr" ] && ln -sf "$PREFIX/bin/vioavr" "$BIN_DIR/vioavr" 2>/dev/null || true

echo -e "${CYAN}[4/5] Installing icons and desktop integration...${RESET}"
ASSETS_DIR="$PACKAGE_DIR/resources/installer/linux"
if [ ! -d "$ASSETS_DIR" ]; then
    ASSETS_DIR="$SCRIPT_DIR/resources/installer/linux"
fi

# Clean up legacy desktop launchers
rm -f "$DESKTOP_DIR/viospice.desktop" "$DESKTOP_DIR/viora-eda.desktop" "$DESKTOP_DIR/viora.desktop"

if [ -d "$ASSETS_DIR/icons/hicolor" ]; then
    cp -rf "$ASSETS_DIR/icons/hicolor/"* "$ICONS_BASE/" 2>/dev/null || true
    # Also install to pixmaps for legacy/alternative window managers
    PIXMAPS_DIR="${ICONS_BASE%/*}/pixmaps"
    mkdir -p "$PIXMAPS_DIR"
    [ -f "$ASSETS_DIR/icons/hicolor/256x256/apps/vioraeda.png" ] && cp -f "$ASSETS_DIR/icons/hicolor/256x256/apps/vioraeda.png" "$PIXMAPS_DIR/vioraeda.png" 2>/dev/null || true
    [ -f "$ASSETS_DIR/icons/hicolor/scalable/apps/vioraeda.svg" ] && cp -f "$ASSETS_DIR/icons/hicolor/scalable/apps/vioraeda.svg" "$PIXMAPS_DIR/vioraeda.svg" 2>/dev/null || true
fi

# Install desktop file with replaced prefix
if [ -f "$ASSETS_DIR/vioraeda.desktop" ]; then
    sed "s|Exec=VioraEDA|Exec=$PREFIX/bin/VioraEDA|g; s|Icon=vioraeda|Icon=vioraeda|g" "$ASSETS_DIR/vioraeda.desktop" > "$DESKTOP_DIR/vioraeda.desktop"
    chmod +x "$DESKTOP_DIR/vioraeda.desktop"
fi

# Install MIME definition
if [ -f "$ASSETS_DIR/vioraeda-mime.xml" ]; then
    cp -f "$ASSETS_DIR/vioraeda-mime.xml" "$MIME_DIR/vioraeda-mime.xml"
fi

# Install Uninstaller script
cp -f "$SCRIPT_DIR/uninstall.sh" "$PREFIX/uninstall.sh" 2>/dev/null || true
chmod +x "$PREFIX/uninstall.sh" 2>/dev/null || true

echo -e "${CYAN}[5/5] Updating system databases and caches...${RESET}"
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$DESKTOP_DIR" >/dev/null 2>&1 || true
fi
if command -v update-mime-database >/dev/null 2>&1; then
    update-mime-database "${MIME_DIR%/*}" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$ICONS_BASE" >/dev/null 2>&1 || true
fi

echo -e "\n${GREEN}${BOLD}✓ VioraEDA has been successfully installed!${RESET}"
echo -e "Target Directory : $PREFIX"
echo -e "CLI Executable   : $BIN_DIR/viora"
echo -e "Desktop Entry    : $DESKTOP_DIR/vioraeda.desktop"
echo -e "Uninstaller      : $PREFIX/uninstall.sh"
echo ""
