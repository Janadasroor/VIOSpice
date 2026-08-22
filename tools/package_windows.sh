#!/usr/bin/env bash
#
# Packages VioraEDA for Windows replicating the CI release workflow 1:1.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${1:-build}"
VERSION="${2:-0.2.0-beta}"
STAGING_DIR="$REPO_ROOT/staging"

echo "=========================================================="
echo " VioraEDA Windows Packaging Tool (Local CI Mirror) "
echo " Version : $VERSION"
echo " BuildDir: $BUILD_DIR"
echo " RepoRoot: $REPO_ROOT"
echo "=========================================================="

export PATH="/mingw64/bin:$PATH"

# 1. Clean & create staging
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR/bin" "$STAGING_DIR/cm" "$STAGING_DIR/lib" "$STAGING_DIR/python/templates" "$STAGING_DIR/templates/flux"

# 2. Copy templates
if [ -d "$REPO_ROOT/templates" ]; then
    cp -r "$REPO_ROOT/templates/"* "$STAGING_DIR/templates/" 2>/dev/null || true
fi
if [ -d "$REPO_ROOT/python/templates" ]; then
    cp -r "$REPO_ROOT/python/templates/"* "$STAGING_DIR/python/templates/" 2>/dev/null || true
    cp "$REPO_ROOT/python/templates/"*.flux "$STAGING_DIR/templates/flux/" 2>/dev/null || true
fi

# 3. Copy Executables
cp "$REPO_ROOT/$BUILD_DIR/VioraEDA.exe" "$STAGING_DIR/bin/"
cp "$REPO_ROOT/$BUILD_DIR/viora.exe" "$STAGING_DIR/bin/" 2>/dev/null || true
cp "$REPO_ROOT/$BUILD_DIR/flux_runner.exe" "$STAGING_DIR/bin/" 2>/dev/null || true
find "$REPO_ROOT/$BUILD_DIR" -name "vioavr.exe" -exec cp {} "$STAGING_DIR/bin/" \; 2>/dev/null || echo "no vioavr.exe"

# 4. Code Models & Engine Libraries
cp -r "$REPO_ROOT/$BUILD_DIR/cm/." "$STAGING_DIR/cm/" 2>/dev/null || true
if [ -z "$(ls -A "$STAGING_DIR/cm" 2>/dev/null)" ]; then
    for f in "$REPO_ROOT/$BUILD_DIR/viomatrixc-prebuilt/lib/ngspice/"*.cm; do
        [ -f "$f" ] && cp "$f" "$STAGING_DIR/cm/" || true
    done
fi
cp "$REPO_ROOT/$BUILD_DIR/viomatrixc-prebuilt/bin/"*.dll "$STAGING_DIR/bin/" 2>/dev/null || true

# 5. LLVM & MinGW Core Runtimes
cp /mingw64/bin/LLVM-C*.dll "$STAGING_DIR/bin/" 2>/dev/null || true
cp /mingw64/bin/libLLVM*.dll "$STAGING_DIR/bin/" 2>/dev/null || true
cp /mingw64/bin/LLVM*.dll "$STAGING_DIR/bin/" 2>/dev/null || true
cp /mingw64/bin/libgcc_s_seh-1.dll "$STAGING_DIR/bin/" 2>/dev/null || true
cp /mingw64/bin/libstdc++-6.dll "$STAGING_DIR/bin/" 2>/dev/null || true
cp /mingw64/bin/libwinpthread-1.dll "$STAGING_DIR/bin/" 2>/dev/null || true
cp /mingw64/bin/libpython3*.dll "$STAGING_DIR/bin/" 2>/dev/null || true

# 6. Python 3.14 Isolated Standard Library
for pydir in /mingw64/lib/python3.*; do
    if [ -d "$pydir" ]; then
        pyver=$(basename "$pydir" | tr -d '.')
        (cd "$pydir" && zip -r -q "$STAGING_DIR/lib/$pyver.zip" . -x "*.pyc" "__pycache__/*" "test/*" "tests/*" "idlelib/*" "tkinter/*" "turtledemo/*")
        cp "$STAGING_DIR/lib/$pyver.zip" "$STAGING_DIR/bin/" 2>/dev/null || true
        printf "%s.zip\n.\nimport site\n" "$pyver" > "$STAGING_DIR/bin/$pyver._pth"
        break
    fi
done

# 7. windeployqt and Transitive DLL Resolution
windeployqt --dir "$STAGING_DIR/bin" "$STAGING_DIR/bin/VioraEDA.exe"

for pdir in /mingw64/share/qt6/plugins /mingw64/plugins /mingw64/lib/qt6/plugins; do
    if [ -d "$pdir/platforms" ]; then
        mkdir -p "$STAGING_DIR/bin/platforms"
        cp "$pdir/platforms/"*.dll "$STAGING_DIR/bin/platforms/" 2>/dev/null || true
        break
    fi
done

# Transitive DLL Resolver
shopt -s nocasematch
unset SCANNED
declare -a QUEUE
while IFS= read -r f; do QUEUE+=("$f"); done < <(find "$STAGING_DIR/bin" -type f \( -name '*.exe' -o -name '*.dll' \) 2>/dev/null)
while [ ${#QUEUE[@]} -gt 0 ]; do
    f="${QUEUE[0]}"
    QUEUE=("${QUEUE[@]:1}")
    base=$(basename "$f")
    case " ${SCANNED[*]-} " in *" $base "*) continue ;; esac
    SCANNED+=("$base")
    for dll in $(objdump -p "$f" 2>/dev/null | grep "DLL Name" | awk '{print $3}'); do
        [ -f "$STAGING_DIR/bin/$dll" ] && continue
        case "$dll" in
            api-ms-*|ext-ms-*|kernel32.dll|user32.dll|gdi32.dll|msvcrt.dll|advapi32.dll|shell32.dll|ole32.dll|oleaut32.dll|ws2_32.dll|comdlg32.dll|imm32.dll|winmm.dll|shlwapi.dll|version.dll|ntdll.dll|uxtheme.dll|dwmapi.dll|setupapi.dll|wtsapi32.dll|iphlpapi.dll|bcrypt.dll|crypt32.dll|mpr.dll|secur32.dll|userenv.dll|opengl32.dll|d3d11.dll|dxgi.dll|d2d1.dll|dwrite.dll|winspool.drv)
                continue ;;
        esac
        found=0
        for srcdir in /mingw64/bin /mingw64/lib "$REPO_ROOT/$BUILD_DIR/viomatrixc-prebuilt/bin"; do
            if [ -f "$srcdir/$dll" ]; then
                echo "Bundling transitive DLL: $dll from $srcdir"
                cp "$srcdir/$dll" "$STAGING_DIR/bin/"
                QUEUE+=("$STAGING_DIR/bin/$dll")
                found=1
                break
            fi
        done
        if [ $found -eq 0 ]; then
            echo "Warning: unresolved dependency $dll for $base"
        fi
    done
done
shopt -u nocasematch

# 8. Build NSIS Installer
OUTFILE="$REPO_ROOT/VioraEDA-$VERSION-windows-x86_64-Setup.exe"
makensis -DOUTFILE="$OUTFILE" -DVERSION="$VERSION" "$REPO_ROOT/tools/installer/installer.nsi"

echo "=========================================================="
echo " SUCCESS: Installer created successfully!"
echo " Path: $OUTFILE"
echo "=========================================================="
