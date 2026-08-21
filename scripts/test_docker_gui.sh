#!/usr/bin/env bash
# ==============================================================================
# test_docker_gui.sh — Test VioraEDA installer, app, and CLI across Linux distros in Docker
# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INSTALLER_DIR="$ROOT/build/installer"

C_BLD=$'\033[1m'
C_CYN=$'\033[36m'
C_GRN=$'\033[32m'
C_YLW=$'\033[33m'
C_RED=$'\033[31m'
C_MAG=$'\033[35m'
C_END=$'\033[0m'

BOLD="$C_BLD"
CYAN="$C_CYN"
GREEN="$C_GRN"
YELLOW="$C_YLW"
RED="$C_RED"
MAGENTA="$C_MAG"
RESET="$C_END"

usage() {
    cat <<EOF
${C_CYN}${C_BLD}VioraEDA Multi-Distro Docker Test Runner${C_END}

${C_BLD}Usage:${C_END}
  $0 [mode] [distro] [options]

${C_BLD}Modes:${C_END}
  setup      Launch VioraEDA_Setup (Qt GUI Installer Wizard) [Default]
  app | run  Unpack distribution bundle and launch main VioraEDA GUI
  cli        Run headless CLI smoke tests (viora --version, help, etc.)
  deb        Test .deb package installation & app launch (Debian/Ubuntu)
  rpm        Test .rpm package installation & app launch (Fedora/openSUSE/RHEL)
  shell      Install dependencies and drop into interactive container shell

${C_BLD}Supported Distros:${C_END}
  ubuntu     ubuntu:24.04 [Default]
  debian     debian:bookworm
  fedora     fedora:latest
  arch       archlinux:latest
  opensuse   opensuse/tumbleweed
  rocky      rockylinux:9
  alpine     alpine:latest (cli / shell mode only)

${C_BLD}Options:${C_END}
  --image <tag>     Override container image (e.g. ubuntu:devel, fedora:40)
  --offscreen       Force headless Qt offscreen platform (QT_QPA_PLATFORM=offscreen)
  --help, -h        Show this help message

${C_BLD}Examples:${C_END}
  $0 setup fedora            # Test Setup Wizard GUI on Fedora
  $0 app arch                # Test VioraEDA GUI on Arch Linux
  $0 cli debian              # Run headless CLI checks on Debian 12
  $0 shell opensuse          # Interactive shell on openSUSE Tumbleweed
  $0 deb ubuntu              # Test .deb package installation on Ubuntu
EOF
    exit 0
}

MODE="setup"
DISTRO="ubuntu"
CUSTOM_IMAGE=""
OFFSCREEN=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --help|-h)
            usage
            ;;
        --image)
            CUSTOM_IMAGE="$2"
            shift 2
            ;;
        --offscreen)
            OFFSCREEN=1
            shift
            ;;
        setup|app|run|cli|deb|rpm|shell)
            MODE="$1"
            shift
            ;;
        ubuntu|debian|fedora|arch|archlinux|opensuse|suse|tumbleweed|rocky|almalinux|rhel|alpine)
            DISTRO="$1"
            shift
            ;;
        *)
            echo -e "${RED}Unknown argument: $1${RESET}"
            usage
            ;;
    esac
done

if [ "$MODE" = "run" ]; then
    MODE="app"
fi

echo -e "${CYAN}${BOLD}=== VioraEDA Multi-Distro Docker Test Runner ===${RESET}\n"

# Verify packages exist
TAR_PKG=$(find "$INSTALLER_DIR" -name "VioraEDA-*-linux-x86_64.tar.gz" 2>/dev/null | head -n 1 || true)
DEB_PKG=$(find "$INSTALLER_DIR" -name "VioraEDA-*-linux-x86_64.deb" 2>/dev/null | head -n 1 || true)
RPM_PKG=$(find "$INSTALLER_DIR" -name "VioraEDA-*-linux-x86_64.rpm" 2>/dev/null | head -n 1 || true)

if [ -z "$TAR_PKG" ]; then
    echo -e "${YELLOW}Distribution bundle not found in $INSTALLER_DIR. Building packages first...${RESET}"
    "$SCRIPT_DIR/package_linux.sh"
    TAR_PKG=$(find "$INSTALLER_DIR" -name "VioraEDA-*-linux-x86_64.tar.gz" 2>/dev/null | head -n 1 || true)
fi

TAR_NAME="$(basename "$TAR_PKG")"
BASE_DIR="${TAR_NAME%.tar.gz}"

# Setup X11 display forwarding if not offscreen
X11_ARGS=()
if [ "$OFFSCREEN" -eq 1 ] || [ "$MODE" = "cli" ]; then
    X11_ARGS+=("-e" "QT_QPA_PLATFORM=offscreen")
else
    if command -v xhost >/dev/null 2>&1; then
        echo -e "${CYAN}Authorizing local container access to X11 (${DISPLAY:-:0})...${RESET}"
        xhost +local:root >/dev/null 2>&1 || true
        xhost +local:docker >/dev/null 2>&1 || true
    fi

    X11_ARGS+=(
        "-e" "DISPLAY=${DISPLAY:-:0}"
        "-e" "QT_X11_NO_MITSHM=1"
        "-v" "/tmp/.X11-unix:/tmp/.X11-unix:rw"
    )

    if [ -n "${XAUTHORITY:-}" ] && [ -f "$XAUTHORITY" ]; then
        X11_ARGS+=("-v" "$XAUTHORITY:/root/.Xauthority:ro")
    elif [ -f "$HOME/.Xauthority" ]; then
        X11_ARGS+=("-v" "$HOME/.Xauthority:/root/.Xauthority:ro")
    fi
fi

# Configure target image and dependency installation script per distro
IMAGE=""
INSTALL_DEPS=""

case "$DISTRO" in
    ubuntu)
        IMAGE="${CUSTOM_IMAGE:-ubuntu:devel}"
        INSTALL_DEPS="
            apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
                tar gzip ca-certificates \
                fonts-dejavu-core fonts-freefont-ttf \
                libgl1 libegl1 libopengl0 libglx-mesa0 libgl1-mesa-dri \
                libxkbcommon-x11-0 libfontconfig1 libfreetype6 libdbus-1-3 \
                libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
                libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-sync1 \
                libxcb-xfixes0 libxcb-xinerama0 libxcb-xinput0 \
                desktop-file-utils shared-mime-info hicolor-icon-theme >/dev/null 2>&1
        "
        ;;
    debian)
        IMAGE="${CUSTOM_IMAGE:-debian:testing}"
        INSTALL_DEPS="
            apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
                tar gzip ca-certificates \
                fonts-dejavu-core fonts-freefont-ttf \
                libgl1 libegl1 libopengl0 libglx-mesa0 libgl1-mesa-dri \
                libxkbcommon-x11-0 libfontconfig1 libfreetype6 libdbus-1-3 \
                libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
                libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-sync1 \
                libxcb-xfixes0 libxcb-xinerama0 libxcb-xinput0 \
                desktop-file-utils shared-mime-info hicolor-icon-theme >/dev/null 2>&1
        "
        ;;
    fedora)
        IMAGE="${CUSTOM_IMAGE:-fedora:latest}"
        INSTALL_DEPS="
            dnf install -y -q \
                tar gzip ca-certificates findutils \
                dejavu-sans-fonts dejavu-serif-fonts dejavu-sans-mono-fonts \
                mesa-libGL mesa-libEGL mesa-dri-drivers \
                libxkbcommon-x11 fontconfig freetype dbus-libs \
                xcb-util-cursor xcb-util-wm xcb-util-image xcb-util-keysyms xcb-util-renderutil \
                libX11-xcb libXrandr libXinerama libXi libXfixes \
                desktop-file-utils shared-mime-info hicolor-icon-theme >/dev/null 2>&1
        "
        ;;
    arch|archlinux)
        IMAGE="${CUSTOM_IMAGE:-archlinux:latest}"
        INSTALL_DEPS="
            pacman -Syu --noconfirm --quiet \
                tar gzip ca-certificates \
                ttf-dejavu ttf-liberation noto-fonts \
                mesa libglvnd \
                libxkbcommon-x11 fontconfig freetype2 dbus \
                xcb-util-cursor xcb-util-wm xcb-util-image xcb-util-keysyms xcb-util-renderutil \
                libx11 libxrandr libxinerama libxi libxfixes \
                desktop-file-utils shared-mime-info hicolor-icon-theme >/dev/null 2>&1
        "
        ;;
    opensuse|suse|tumbleweed)
        IMAGE="${CUSTOM_IMAGE:-opensuse/tumbleweed}"
        INSTALL_DEPS="
            zypper --non-interactive install --no-recommends -y \
                tar gzip ca-certificates \
                dejavu-fonts \
                Mesa-libGL1 libglvnd \
                libxkbcommon-x11-0 fontconfig libfreetype6 dbus-1 \
                libxcb-cursor0 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 \
                libX11-xcb1 libXrandr2 libXinerama1 libXi6 libXfixes3 \
                desktop-file-utils shared-mime-info hicolor-icon-theme >/dev/null 2>&1
        "
        ;;
    rocky|almalinux|rhel)
        IMAGE="${CUSTOM_IMAGE:-rockylinux:9}"
        INSTALL_DEPS="
            dnf install -y -q --allowerasing \
                tar gzip ca-certificates epel-release findutils >/dev/null 2>&1 || true
            dnf install -y -q --allowerasing \
                dejavu-sans-fonts \
                mesa-libGL mesa-libEGL mesa-dri-drivers \
                libxkbcommon-x11 fontconfig freetype dbus-libs \
                libX11-xcb libXrandr libXinerama libXi libXfixes \
                desktop-file-utils shared-mime-info hicolor-icon-theme >/dev/null 2>&1
        "
        ;;
    alpine)
        IMAGE="${CUSTOM_IMAGE:-alpine:latest}"
        INSTALL_DEPS="
            apk add --no-cache bash tar gzip ca-certificates \
                font-dejavu \
                mesa-gl mesa-egl libxkbcommon fontconfig freetype dbus \
                libxcb xcb-util-cursor xcb-util-wm xcb-util-image xcb-util-keysyms xcb-util-renderutil \
                libx11 libxrandr libxinerama libxi libxfixes >/dev/null 2>&1
        "
        ;;
esac

echo -e "Target Distro : ${MAGENTA}${BOLD}$DISTRO${RESET} (Image: ${CYAN}$IMAGE${RESET})"
echo -e "Target Mode   : ${GREEN}${BOLD}$MODE${RESET}"
echo -e "Bundle Target : ${YELLOW}$TAR_NAME${RESET}\n"

case "$MODE" in
    setup)
        echo -e "${GREEN}${BOLD}Launching VioraEDA_Setup (Qt GUI Installer) inside $IMAGE...${RESET}"
        docker run -it --rm \
            --net=host \
            "${X11_ARGS[@]}" \
            -v "$INSTALLER_DIR":/pkg:ro \
            "$IMAGE" bash -c "
                set -e
                echo '[Container] Installing minimal runtime dependencies for $DISTRO...'
                $INSTALL_DEPS
                
                echo '[Container] Unpacking distribution bundle ($TAR_NAME)...'
                mkdir -p /tmp/viora_setup
                tar -xzf /pkg/$TAR_NAME -C /tmp/viora_setup
                cd /tmp/viora_setup/$BASE_DIR
                
                echo '[Container] Starting VioraEDA Setup Wizard GUI...'
                ./bin/VioraEDA_Setup
            "
        ;;

    app)
        echo -e "${GREEN}${BOLD}Launching VioraEDA Application GUI inside $IMAGE...${RESET}"
        docker run -it --rm \
            --net=host \
            "${X11_ARGS[@]}" \
            -v "$INSTALLER_DIR":/pkg:ro \
            "$IMAGE" bash -c "
                set -e
                echo '[Container] Installing minimal runtime dependencies for $DISTRO...'
                $INSTALL_DEPS
                
                echo '[Container] Unpacking distribution bundle ($TAR_NAME)...'
                mkdir -p /tmp/viora_app
                tar -xzf /pkg/$TAR_NAME -C /tmp/viora_app
                cd /tmp/viora_app/$BASE_DIR
                
                echo '[Container] Starting VioraEDA main application...'
                ./bin/VioraEDA
            "
        ;;

    cli)
        echo -e "${GREEN}${BOLD}Running VioraEDA Headless CLI Smoke Tests inside $IMAGE...${RESET}"
        docker run -it --rm \
            -v "$INSTALLER_DIR":/pkg:ro \
            "$IMAGE" bash -c "
                set -e
                echo '[Container] Installing minimal runtime dependencies for $DISTRO...'
                $INSTALL_DEPS
                
                echo '[Container] Unpacking distribution bundle ($TAR_NAME)...'
                mkdir -p /tmp/viora_cli
                tar -xzf /pkg/$TAR_NAME -C /tmp/viora_cli
                cd /tmp/viora_cli/$BASE_DIR
                
                export PATH=\"\$(pwd)/bin:\$PATH\"
                
                echo '[Container] Testing viora --version...'
                ./bin/viora --version
                
                echo '[Container] Testing viora --help...'
                ./bin/viora --help | head -n 15
                
                echo '[Container] Testing viora schematic-netlist help...'
                ./bin/viora schematic-netlist --help | head -n 10
                
                echo '[Container] ✅ All CLI smoke tests passed successfully on $DISTRO!'
            "
        ;;

    deb)
        if [ "$DISTRO" != "ubuntu" ] && [ "$DISTRO" != "debian" ]; then
            echo -e "${RED}Error: .deb mode is only supported on Debian and Ubuntu distros.${RESET}"
            exit 1
        fi
        DEB_NAME="$(basename "$DEB_PKG")"
        echo -e "${GREEN}${BOLD}Testing .deb installation and app launch inside $IMAGE...${RESET}"
        docker run -it --rm \
            --net=host \
            "${X11_ARGS[@]}" \
            -v "$INSTALLER_DIR":/pkg:ro \
            "$IMAGE" bash -c "
                set -e
                echo '[Container] Installing .deb package ($DEB_NAME) and dependencies...'
                apt-get update -qq
                DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
                    /pkg/$DEB_NAME \
                    fonts-dejavu-core fonts-freefont-ttf \
                    libgl1 libegl1 libopengl0 libglx-mesa0 libgl1-mesa-dri \
                    libxkbcommon-x11-0 libfontconfig1 libfreetype6 libdbus-1-3 \
                    libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
                    libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-sync1 \
                    libxcb-xfixes0 libxcb-xinerama0 libxcb-xinput0 >/dev/null 2>&1
                
                echo '[Container] Launching installed VioraEDA...'
                VioraEDA
            "
        ;;

    rpm)
        if [ "$DISTRO" != "fedora" ] && [ "$DISTRO" != "opensuse" ] && [ "$DISTRO" != "suse" ] && [ "$DISTRO" != "tumbleweed" ] && [ "$DISTRO" != "rocky" ]; then
            echo -e "${RED}Error: .rpm mode is only supported on Fedora, openSUSE, and Rocky/RHEL distros.${RESET}"
            exit 1
        fi
        if [ -z "$RPM_PKG" ]; then
            echo -e "${RED}Error: No .rpm package found in $INSTALLER_DIR.${RESET}"
            exit 1
        fi
        RPM_NAME="$(basename "$RPM_PKG")"
        echo -e "${GREEN}${BOLD}Testing .rpm installation and app launch inside $IMAGE...${RESET}"
        docker run -it --rm \
            --net=host \
            "${X11_ARGS[@]}" \
            -v "$INSTALLER_DIR":/pkg:ro \
            "$IMAGE" bash -c "
                set -e
                $INSTALL_DEPS
                echo '[Container] Installing RPM package ($RPM_NAME)...'
                rpm -ivh --nodeps /pkg/$RPM_NAME
                
                echo '[Container] Launching installed VioraEDA...'
                VioraEDA
            "
        ;;

    shell)
        echo -e "${GREEN}${BOLD}Dropping into interactive container shell ($IMAGE)...${RESET}"
        docker run -it --rm \
            --net=host \
            "${X11_ARGS[@]}" \
            -v "$INSTALLER_DIR":/pkg:ro \
            "$IMAGE" bash -c "
                echo '[Container] Installing runtime dependencies for $DISTRO...'
                $INSTALL_DEPS
                echo '[Container] Environment ready. Distribution packages are mounted at /pkg'
                exec bash
            "
        ;;
esac
