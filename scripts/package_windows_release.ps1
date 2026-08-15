$ErrorActionPreference = "Stop"
$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:PATH

$staging = "C:\VioraEDA\staging"
if (Test-Path $staging) {
    Remove-Item -Recurse -Force $staging
}

New-Item -ItemType Directory -Path "$staging\bin" -Force | Out-Null
New-Item -ItemType Directory -Path "$staging\cm" -Force | Out-Null

Write-Host "Copying core executables..."
Copy-Item "C:\VioraEDA\build\VioraEDA.exe" "$staging\bin\"
Copy-Item "C:\VioraEDA\build\viora.exe" "$staging\bin\"
Copy-Item "C:\VioraEDA\build\VioraEDA_Setup.exe" "$staging\bin\"
Copy-Item "C:\VioraEDA\build\VioraEDA_Setup.exe" "$staging\VioraEDA_Setup.exe"
if (Test-Path "C:\VioraEDA\build\flux_runner.exe") {
    Copy-Item "C:\VioraEDA\build\flux_runner.exe" "$staging\bin\"
}

$vioavr = Get-ChildItem -Path "C:\VioraEDA\build" -Filter "vioavr.exe" -Recurse | Select-Object -First 1
if ($vioavr) {
    Copy-Item $vioavr.FullName "$staging\bin\vioavr.exe"
    Write-Host "Found vioavr: $($vioavr.FullName)"
}

Write-Host "Copying code models..."
Get-ChildItem -Path "C:\VioraEDA\build\cm\*.cm" | ForEach-Object {
    Copy-Item $_.FullName "$staging\cm\"
}
Get-ChildItem -Path "C:\VioraEDA\build\viomatrixc-prebuilt\lib\ngspice\*.cm" -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item $_.FullName "$staging\cm\"
}

Write-Host "Copying engine DLLs..."
Get-ChildItem -Path "C:\VioraEDA\build\viomatrixc-prebuilt\bin\*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item $_.FullName "$staging\bin\"
}

Write-Host "Running windeployqt..."
& windeployqt --dir "$staging\bin" "$staging\bin\VioraEDA.exe"

Write-Host "Copying MinGW & runtime DLLs..."
$mingwBins = @(
    "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll",
    "zlib1.dll", "libzstd.dll", "python3.dll", "libLLVM-21.dll", "libLLVM.dll"
)
foreach ($dll in $mingwBins) {
    $src = "C:\msys64\mingw64\bin\$dll"
    if (Test-Path $src) {
        Copy-Item $src "$staging\bin\" -Force
    }
}

Get-ChildItem -Path "C:\msys64\mingw64\bin\LLVM*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item $_.FullName "$staging\bin\" -Force
}

$pyDll = Get-ChildItem -Path "C:\msys64\mingw64\bin" -Filter "libpython3.*.dll" | Select-Object -First 1
if ($pyDll) {
    Copy-Item $pyDll.FullName "$staging\bin\" -Force
}

Write-Host "Copying templates..."
Copy-Item "C:\VioraEDA\templates" "$staging\templates" -Recurse -Force

Write-Host "Copying ViospiceLib..."
if (Test-Path "C:\Users\rdpuser\ViospiceLib") {
    Copy-Item "C:\Users\rdpuser\ViospiceLib" "$staging\ViospiceLib" -Recurse -Force
}

Write-Host "Creating Release ZIP archive..."
$zipPath = "C:\VioraEDA\VioraEDA-v0.2.0-beta-windows-x86_64.zip"
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}
if (Test-Path "C:\msys64\usr\bin\bsdtar.exe") {
    & "C:\msys64\usr\bin\bsdtar.exe" -a -cf "$zipPath" -C "$staging" .
} else {
    Compress-Archive -Path "$staging\*" -DestinationPath $zipPath -CompressionLevel Optimal
}

if (Test-Path "C:\msys64\mingw64\bin\makensis.exe") {
    Write-Host "Compiling standalone NSIS installer..."
    & "C:\msys64\mingw64\bin\makensis.exe" -DOUTFILE="C:\VioraEDA\VioraEDA-v0.2.0-beta-windows-x86_64-Setup.exe" -DVERSION="0.2.0-beta" "C:\VioraEDA\tools\installer\installer.nsi"
} else {
    Copy-Item "C:\VioraEDA\build\VioraEDA_Setup.exe" "C:\VioraEDA\VioraEDA-v0.2.0-beta-windows-x86_64-Setup.exe" -Force
}

Write-Host "Release packaging completed successfully!"
Write-Host "Artifacts:"
Write-Host "  - ZIP: $zipPath ($( [math]::round((Get-Item $zipPath).Length / 1MB, 2) ) MB)"
Write-Host "  - Setup: C:\VioraEDA\VioraEDA-v0.2.0-beta-windows-x86_64-Setup.exe ($( [math]::round((Get-Item "C:\VioraEDA\VioraEDA-v0.2.0-beta-windows-x86_64-Setup.exe").Length / 1MB, 2) ) MB)"
