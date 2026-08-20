#!/usr/bin/env bash
# ==============================================================================
# test_docker_gui.sh — Test VioraEDA installer and GUI inside isolated Docker
# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INSTALLER_DIR="$ROOT/build/installer"

BOLD="\033[1m"
CYAN="\033[36m"
GREEN="\033[32m"
YELLOW="\033[33m"
RED="\033[31m"
RESET="\033[0m"

echo -e "${CYAN}${BOLD}=== VioraEDA Isolated Docker GUI Test Runner ===${RESET}\n"

if [ ! -d "$INSTALLER_DIR" ] || [ ! -f "$INSTALLER_DIR/VioraEDA-0.2.0-beta-linux-x86_64.tar.gz" ]; then
    echo -e "${YELLOW}Building packages first...${RESET}"
    "$SCRIPT_DIR/package_linux.sh"
fi

# Allow local root / container access to X11 display
if command -v xhost >/dev/null 2>&1; then
    echo -e "${CYAN}Authorizing local container access to X11 ($DISPLAY)...${RESET}"
    xhost +local:root >/dev/null 2>&1 || true
    xhost +local:docker >/dev/null 2>&1 || true
fi

XAUTH_ARGS=()
if [ -n "${XAUTHORITY:-}" ] && [ -f "$XAUTHORITY" ]; then
    XAUTH_ARGS+=("-v" "$XAUTHORITY:/root/.Xauthority:ro")
elif [ -f "$HOME/.Xauthority" ]; then
    XAUTH_ARGS+=("-v" "$HOME/.Xauthority:/root/.Xauthority:ro")
fi

MODE="${1:-setup}" # 'setup' (interactive wizard), 'deb' (deb install + VioraEDA), 'shell'
IMAGE="ubuntu:devel"

case "$MODE" in
    setup)
        echo -e "${GREEN}${BOLD}Launching VioraEDA_Setup (Qt GUI Installer) inside Ubuntu container...${RESET}"
        docker run -it --rm \
            --net=host \
            -e DISPLAY="${DISPLAY:-:0}" \
            -e QT_X11_NO_MITSHM=1 \
            -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
            "${XAUTH_ARGS[@]}" \
            -v "$INSTALLER_DIR":/pkg:ro \
            "$IMAGE" bash -c "
                set -e
                echo '[Container] Installing minimal runtime dependencies...'
                apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
                    libgl1 \
                    libegl1 \
                    libopengl0 \
                    libglx-mesa0 \
                    libgl1-mesa-dri \
                    libxkbcommon-x11-0 \
                    libfontconfig1 \
                    libfreetype6 \
                    libdbus-1-3 \
                    libxcb-cursor0 \
                    libxcb-icccm4 \
                    libxcb-image0 \
                    libxcb-keysyms1 \
                    libxcb-randr0 \
                    libxcb-render-util0 \
                    libxcb-shape0 \
                    libxcb-sync1 \
                    libxcb-xfixes0 \
                    libxcb-xinerama0 \
                    libxcb-xinput0 \
                    desktop-file-utils \
                    shared-mime-info \
                    hicolor-icon-theme >/dev/null 2>&1
                
                echo '[Container] Unpacking distribution bundle...'
                mkdir -p /tmp/viora_setup
                tar -xzf /pkg/VioraEDA-0.2.0-beta-linux-x86_64.tar.gz -C /tmp/viora_setup
                cd /tmp/viora_setup/VioraEDA-0.2.0-beta-linux-x86_64
                
                echo '[Container] Starting VioraEDA Setup Wizard GUI...'
                ./bin/VioraEDA_Setup
            "
        ;;
    deb)
        echo -e "${GREEN}${BOLD}Testing .deb installation and launching VioraEDA GUI in container...${RESET}"
        docker run -it --rm \
            --net=host \
            -e DISPLAY="${DISPLAY:-:0}" \
            -e QT_X11_NO_MITSHM=1 \
            -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
            "${XAUTH_ARGS[@]}" \
            -v "$INSTALLER_DIR":/pkg:ro \
            "$IMAGE" bash -c "
                set -e
                echo '[Container] Installing .deb package and dependencies...'
                apt-get update -qq
                DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
                    /pkg/VioraEDA-0.2.0-beta-linux-x86_64.deb \
                    libgl1 \
                    libegl1 \
                    libopengl0 \
                    libglx-mesa0 \
                    libgl1-mesa-dri \
                    libxkbcommon-x11-0 \
                    libfontconfig1 \
                    libfreetype6 \
                    libdbus-1-3 \
                    libxcb-cursor0 \
                    libxcb-icccm4 \
                    libxcb-image0 \
                    libxcb-keysyms1 \
                    libxcb-randr0 \
                    libxcb-render-util0 \
                    libxcb-shape0 \
                    libxcb-sync1 \
                    libxcb-xfixes0 \
                    libxcb-xinerama0 \
                    libxcb-xinput0 >/dev/null 2>&1
                
                echo '[Container] Launching installed VioraEDA...'
                VioraEDA
            "
        ;;
    shell)
        echo -e "${GREEN}${BOLD}Dropping into interactive container shell...${RESET}"
        docker run -it --rm \
            --net=host \
            -e DISPLAY="${DISPLAY:-:0}" \
            -e QT_X11_NO_MITSHM=1 \
            -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
            "${XAUTH_ARGS[@]}" \
            -v "$INSTALLER_DIR":/pkg:ro \
            "$IMAGE" bash
        ;;
    *)
        echo "Usage: $0 [setup|deb|shell]"
        exit 1
        ;;
esac
