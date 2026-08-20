#!/usr/bin/env bash
# ==============================================================================
# VioraEDA Linux Standalone Uninstaller Script
# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="$SCRIPT_DIR"

BOLD="\033[1m"
GREEN="\033[32m"
YELLOW="\033[33m"
RED="\033[31m"
RESET="\033[0m"

echo -e "${BOLD}VioraEDA Suite 2026 — Linux Uninstaller${RESET}\n"

if [ "$EUID" -eq 0 ]; then
    BIN_DIR="/usr/local/bin"
    DESKTOP_DIR="/usr/share/applications"
    MIME_DIR="/usr/share/mime/packages"
    ICONS_BASE="/usr/share/icons/hicolor"
else
    BIN_DIR="$HOME/.local/bin"
    DESKTOP_DIR="$HOME/.local/share/applications"
    MIME_DIR="$HOME/.local/share/mime/packages"
    ICONS_BASE="$HOME/.local/share/icons/hicolor"
fi

SILENT=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        -y|--silent|--yes)
            SILENT=true
            shift
            ;;
        *)
            shift
            ;;
    esac
done

if [ "$SILENT" = false ]; then
    read -p "Are you sure you want to uninstall VioraEDA from $PREFIX? [y/N] " -n 1 -r
    echo ""
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo -e "${YELLOW}Uninstallation cancelled.${RESET}"
        exit 0
    fi
fi

echo -e "${YELLOW}[1/4] Removing symlinks...${RESET}"
rm -f "$BIN_DIR/VioraEDA"
rm -f "$BIN_DIR/viora"
rm -f "$BIN_DIR/flux_runner"
rm -f "$BIN_DIR/vioavr"

echo -e "${YELLOW}[2/4] Removing desktop & MIME associations...${RESET}"
rm -f "$DESKTOP_DIR/vioraeda.desktop"
rm -f "$MIME_DIR/vioraeda-mime.xml"

# Remove icons
for s in 16x16 32x32 48x48 64x64 128x128 256x256 512x512 scalable; do
    rm -f "$ICONS_BASE/$s/apps/vioraeda.png" 2>/dev/null || true
    rm -f "$ICONS_BASE/$s/apps/vioraeda.svg" 2>/dev/null || true
done
for m in application-x-viora-schematic.png application-x-viora-pcb.png application-x-viora-symbol.png application-x-viora-extension.png; do
    rm -f "$ICONS_BASE/128x128/mimetypes/$m" 2>/dev/null || true
done

echo -e "${YELLOW}[3/4] Updating system caches...${RESET}"
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$DESKTOP_DIR" >/dev/null 2>&1 || true
fi
if command -v update-mime-database >/dev/null 2>&1; then
    update-mime-database "${MIME_DIR%/*}" >/dev/null 2>&1 || true
fi

echo -e "${YELLOW}[4/4] Removing installation directory...${RESET}"
cd "$HOME"
rm -rf "$PREFIX"

echo -e "\n${GREEN}${BOLD}✓ VioraEDA has been completely removed.${RESET}"
