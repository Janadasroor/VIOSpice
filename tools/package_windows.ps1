<#
.SYNOPSIS
    Packages VioraEDA for Windows, creating the exact same staging directory
    and NSIS installer as GitHub Actions CI.

.DESCRIPTION
    This script bundles:
      - Core executables: VioraEDA.exe, viora.exe, flux_runner.exe, vioavr.exe
      - SPICE engine and code models (analog.cm, digital.cm, etc.)
      - LLVM JIT runtime (libLLVM-22.dll)
      - Embedded Python 3.14 (python314.zip + python314._pth)
      - Qt6 runtime & platform plugins (qwindows, qoffscreen, qminimal)
      - All transitive MinGW DLL dependencies
      - Templates (FluxScript logic templates and circuit templates)
      - NSIS Setup Installer (VioraEDA-<version>-windows-x86_64-Setup.exe)

.PARAMETER BuildDir
    Path to CMake build directory (default: "build")

.PARAMETER Version
    Version string (default: "0.2.0-beta")

.PARAMETER OutDir
    Output directory for the final installer (default: root repo directory)
#>

[CmdletBinding()]
param(
    [string]$BuildDir = "build",
    [string]$Version = "0.2.0-beta",
    [string]$OutDir = "."
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path "$RepoRoot\CMakeLists.txt")) {
    $RepoRoot = (Get-Location).Path
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " VioraEDA Windows Packaging Tool (Local CI Mirror) " -ForegroundColor Cyan
Write-Host " Version : $Version" -ForegroundColor Cyan
Write-Host " BuildDir: $BuildDir" -ForegroundColor Cyan
Write-Host " RepoRoot: $RepoRoot" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Environment & Paths
$MinGWBin = "C:\msys64\mingw64\bin"
if (Test-Path $MinGWBin) {
    $env:PATH = "$MinGWBin;C:\msys64\usr\bin;$env:PATH"
}

$StagingDir = "$RepoRoot\staging"
$StagingBin = "$StagingDir\bin"
$StagingCm  = "$StagingDir\cm"
$StagingLib = "$StagingDir\lib"

# Clean previous staging
if (Test-Path $StagingDir) {
    Write-Host "[1/8] Cleaning previous staging directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $StagingDir
}

New-Item -ItemType Directory -Path $StagingBin -Force | Out-Null
New-Item -ItemType Directory -Path $StagingCm  -Force | Out-Null
New-Item -ItemType Directory -Path $StagingLib -Force | Out-Null
New-Item -ItemType Directory -Path "$StagingDir\python\templates" -Force | Out-Null
New-Item -ItemType Directory -Path "$StagingDir\templates\flux" -Force | Out-Null

# 2. Copy templates
Write-Host "[2/8] Copying templates and resources..." -ForegroundColor Yellow
if (Test-Path "$RepoRoot\templates") {
    Copy-Item -Recurse -Force "$RepoRoot\templates\*" "$StagingDir\templates\" -ErrorAction SilentlyContinue
}
if (Test-Path "$RepoRoot\python\templates") {
    Copy-Item -Recurse -Force "$RepoRoot\python\templates\*" "$StagingDir\python\templates\" -ErrorAction SilentlyContinue
    Copy-Item -Force "$RepoRoot\python\templates\*.flux" "$StagingDir\templates\flux\" -ErrorAction SilentlyContinue
}

# 3. Copy Executables
Write-Host "[3/8] Copying core executables..." -ForegroundColor Yellow
$Exes = @(
    @{ Name = "VioraEDA.exe"; Path = "$RepoRoot\$BuildDir\VioraEDA.exe"; Required = $true },
    @{ Name = "viora.exe"; Path = "$RepoRoot\$BuildDir\viora.exe"; Required = $true },
    @{ Name = "flux_runner.exe"; Path = "$RepoRoot\$BuildDir\flux_runner.exe"; Required = $true }
)

foreach ($item in $Exes) {
    if (Test-Path $item.Path) {
        Copy-Item -Force $item.Path $StagingBin
        Write-Host "  + $($item.Name)" -ForegroundColor Green
    } elseif ($item.Required) {
        throw "Required binary not found: $($item.Path). Build the project first!"
    }
}

# Copy vioavr.exe
$VioavrFound = $false
$VioavrCandidates = Get-ChildItem -Path "$RepoRoot\$BuildDir" -Filter "vioavr.exe" -Recurse -ErrorAction SilentlyContinue
if ($VioavrCandidates.Count -gt 0) {
    Copy-Item -Force $VioavrCandidates[0].FullName $StagingBin
    Write-Host "  + vioavr.exe ($($VioavrCandidates[0].FullName))" -ForegroundColor Green
    $VioavrFound = $true
} else {
    Write-Warning "vioavr.exe not found in $BuildDir"
}

# 4. Copy Code Models & Engine Libraries
Write-Host "[4/8] Packaging SPICE code models and engine libraries..." -ForegroundColor Yellow
if (Test-Path "$RepoRoot\$BuildDir\cm") {
    Get-ChildItem "$RepoRoot\$BuildDir\cm\*.cm" -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item -Force $_.FullName $StagingCm
        Write-Host "  + CM: $($_.Name)" -ForegroundColor Green
    }
}
# Fallback for code models from prebuilt
Get-ChildItem "$RepoRoot\$BuildDir\viomatrixc-prebuilt\lib\ngspice\*.cm" -ErrorAction SilentlyContinue | ForEach-Object {
    if (-not (Test-Path "$StagingCm\$($_.Name)")) {
        Copy-Item -Force $_.FullName $StagingCm
        Write-Host "  + CM (prebuilt): $($_.Name)" -ForegroundColor Green
    }
}

# Copy VioMATRIXC DLLs
Get-ChildItem "$RepoRoot\$BuildDir\viomatrixc-prebuilt\bin\*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item -Force $_.FullName $StagingBin
    Write-Host "  + DLL: $($_.Name)" -ForegroundColor Green
}

# 5. Copy LLVM & MinGW Core Runtimes
Write-Host "[5/8] Copying LLVM and MinGW runtimes..." -ForegroundColor Yellow
$MinGWDlls = @(
    "libLLVM*.dll",
    "LLVM*.dll",
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll",
    "libpython3*.dll"
)
foreach ($pattern in $MinGWDlls) {
    Get-ChildItem -Path $MinGWBin -Filter $pattern -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item -Force $_.FullName $StagingBin
        Write-Host "  + Runtime: $($_.Name)" -ForegroundColor Green
    }
}

# 6. Bundle Python 3.14 Isolated Standard Library
Write-Host "[6/8] Packaging embedded Python standard library..." -ForegroundColor Yellow
$PyDirs = Get-ChildItem -Path "C:\msys64\mingw64\lib" -Filter "python3.*" -Directory -ErrorAction SilentlyContinue
if ($PyDirs.Count -gt 0) {
    $PyDir = $PyDirs[0].FullName
    $PyVer = $PyDirs[0].Name.Replace(".", "")
    $ZipFile = "$StagingLib\$PyVer.zip"
    
    Write-Host "  Compressing Python library from $PyDir -> $ZipFile"
    & zip -r -q $ZipFile $PyDir -x "*.pyc" "__pycache__/*" "test/*" "tests/*" "idlelib/*" "tkinter/*" "turtledemo/*"
    Copy-Item -Force $ZipFile $StagingBin
    
    # Create ._pth file
    $PthContent = "$PyVer.zip`n.`nimport site`n"
    Set-Content -Path "$StagingBin\$PyVer._pth" -Value $PthContent -Encoding ASCII
    Write-Host "  + Created $PyVer._pth and bundled $PyVer.zip" -ForegroundColor Green
}

# 7. Run windeployqt and Transitive DLL Resolution
Write-Host "[7/8] Running windeployqt and resolving transitive DLLs..." -ForegroundColor Yellow
& windeployqt --dir $StagingBin "$StagingBin\VioraEDA.exe"

# Platforms plugins
$PlatformPluginDirs = @("C:\msys64\mingw64\share\qt6\plugins\platforms", "C:\msys64\mingw64\lib\qt6\plugins\platforms")
foreach ($pdir in $PlatformPluginDirs) {
    if (Test-Path $pdir) {
        New-Item -ItemType Directory -Path "$StagingBin\platforms" -Force | Out-Null
        Get-ChildItem -Path "$pdir\*.dll" | ForEach-Object {
            Copy-Item -Force $_.FullName "$StagingBin\platforms\"
        }
        break
    }
}

# Transitive DLL Resolver
$SystemDlls = @(
    "kernel32.dll","user32.dll","gdi32.dll","msvcrt.dll","advapi32.dll","shell32.dll",
    "ole32.dll","oleaut32.dll","ws2_32.dll","comdlg32.dll","imm32.dll","winmm.dll",
    "shlwapi.dll","version.dll","ntdll.dll","uxtheme.dll","dwmapi.dll","setupapi.dll",
    "wtsapi32.dll","iphlpapi.dll","bcrypt.dll","crypt32.dll","mpr.dll","secur32.dll",
    "userenv.dll","opengl32.dll","d3d11.dll","dxgi.dll","d2d1.dll","dwrite.dll","winspool.drv"
)

$Queue = [System.Collections.Generic.Queue[string]]::new()
$Scanned = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

Get-ChildItem -Path $StagingBin -Include *.exe, *.dll -Recurse | ForEach-Object {
    $Queue.Enqueue($_.FullName)
}

while ($Queue.Count -gt 0) {
    $current = $Queue.Dequeue()
    $base = [System.IO.Path]::GetFileName($current)
    if ($Scanned.Contains($base)) { continue }
    $null = $Scanned.Add($base)

    $dump = & objdump -p $current 2>$null | Select-String "DLL Name:\s*(.*)"
    foreach ($line in $dump) {
        $dll = $line.Matches[0].Groups[1].Value.Trim()
        if ($dll -match "^api-ms-" -or $dll -match "^ext-ms-" -or ($SystemDlls -contains $dll.ToLower())) {
            continue
        }
        if (-not (Test-Path "$StagingBin\$dll")) {
            $found = $false
            $searchDirs = @($MinGWBin, "C:\msys64\mingw64\lib", "$RepoRoot\$BuildDir\viomatrixc-prebuilt\bin")
            foreach ($sdir in $searchDirs) {
                $target = "$sdir\$dll"
                if (Test-Path $target) {
                    Copy-Item -Force $target $StagingBin
                    $Queue.Enqueue("$StagingBin\$dll")
                    Write-Host "  + Bundled Transitive DLL: $dll" -ForegroundColor DarkGreen
                    $found = $true
                    break
                }
            }
            if (-not $found) {
                Write-Warning "Unresolved transitive dependency: $dll (needed by $base)"
            }
        }
    }
}

# 8. Build NSIS Installer
Write-Host "[8/8] Building NSIS Installer..." -ForegroundColor Yellow
$OutFile = "$RepoRoot\VioraEDA-$Version-windows-x86_64-Setup.exe"
$NsiScript = "$RepoRoot\tools\installer\installer.nsi"

& makensis "-DOUTFILE=$OutFile" "-DVERSION=$Version" $NsiScript

if (Test-Path $OutFile) {
    $installerSize = (Get-Item $OutFile).Length / 1MB
    Write-Host "==========================================================" -ForegroundColor Green
    Write-Host " SUCCESS: Installer created successfully!" -ForegroundColor Green
    Write-Host " Path: $OutFile" -ForegroundColor Green
    Write-Host " Size: $([math]::Round($installerSize, 2)) MB" -ForegroundColor Green
    Write-Host "==========================================================" -ForegroundColor Green
} else {
    throw "makensis failed to generate $OutFile"
}
