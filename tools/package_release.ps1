<#
.SYNOPSIS
  Builds the player package dist\Baroque\ (work/reports/PACKAGING.md).

.DESCRIPTION
  Configures a separate release build (build\release_pkg) with BAROQUE_DEV_ASSET_FALLBACK=OFF, so the EXE only
  reads <exe>\assets and <exe>\disc and carries no path of this PC, builds Baroque-Native in Release, and copies
  into dist\Baroque\:
    Baroque.exe                 GUI-subsystem EXE (no console window), icon + version resource
    FFmpeg + dependency DLLs    the load-time closure listed in native/cmake/video.cmake
    README.txt, THIRD_PARTY_NOTICES.txt, raylib_LICENSE.txt
    widescreen\*.png           16:9 title / menu backdrop art
  No game data is copied: assets\ and disc\ are created on the player's PC by the first-start setup from their own
  disc image. controls.ini / baroque_port.ini / save\ / baroque.log are written by the game on first run.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\package_release.ps1
  powershell -ExecutionPolicy Bypass -File tools\package_release.ps1 -Version 0.2.0 -Zip
#>
param(
    [string]$Version = "0.1.0",
    [string]$CMake = "",
    [string]$BuildDir = "",
    [string]$OutDir = "",
    [switch]$NoBuild,
    [switch]$Zip
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ($CMake -eq "") {
    $CMake = Join-Path $root "..\Simple 2000s DX  DECOMP\PingPong\toolchain\python\cmake\data\bin\cmake.exe"
    if (-not (Test-Path $CMake)) { $CMake = "cmake" }
}
if ($BuildDir -eq "") { $BuildDir = Join-Path $root "build\release_pkg" }
if ($OutDir -eq "") { $OutDir = Join-Path $root "dist\Baroque" }

if (-not $NoBuild) {
    & $CMake -S (Join-Path $root "native") -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
        -DBAROQUE_DEV_ASSET_FALLBACK=OFF "-DBAROQUE_PORT_VERSION=$Version"
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
    & $CMake --build $BuildDir --config Release --target Baroque-Native -- "/m" "/v:minimal"
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}
$bin = Join-Path $BuildDir "Release"
$exe = Join-Path $bin "Baroque.exe"
if (-not (Test-Path $exe)) { throw "missing $exe (build Baroque-Native first)" }

# PE optional header Subsystem (offset 68 from the optional header): 2 = Windows GUI, 3 = console
$bytes = [System.IO.File]::ReadAllBytes($exe)
$pe = [BitConverter]::ToInt32($bytes, 0x3c)
$subsystem = [BitConverter]::ToUInt16($bytes, $pe + 24 + 68)
if ($subsystem -ne 2) { throw "Baroque.exe subsystem is $subsystem, expected 2 (GUI)" }

# Keep anything the player created if OutDir is an existing install (assets, disc, save, ini, log); replace program files.
New-Item -ItemType Directory -Force $OutDir | Out-Null
Copy-Item $exe (Join-Path $OutDir "Baroque.exe") -Force
$dlls = @("avcodec-61.dll", "avutil-59.dll", "swscale-8.dll", "swresample-5.dll",
          "libwebpmux.dll", "libwebp.dll", "libsharpyuv.dll",
          "jxl.dll", "jxl_cms.dll", "jxl_threads.dll", "brotlidec.dll", "brotlienc.dll", "brotlicommon.dll",
          "zlib1.dll")
foreach ($d in $dlls) {
    $src = Join-Path $bin $d
    if (-not (Test-Path $src)) { throw "missing runtime DLL $src" }
    Copy-Item $src (Join-Path $OutDir $d) -Force
}
# PORT: 21:9 title / menu backdrop art (read from <exe>\widescreen); older art files are replaced
$wideDir = Join-Path $OutDir "widescreen"
New-Item -ItemType Directory -Force $wideDir | Out-Null
Get-ChildItem $wideDir -Filter *.png | ForEach-Object { [IO.File]::Delete($_.FullName) }
Copy-Item (Join-Path $root "native\resources\widescreen\*.png") $wideDir -Force
$pkg = Join-Path $root "tools\package"
(Get-Content (Join-Path $pkg "README.txt") -Raw).Replace("@VERSION@", $Version) |
    Set-Content -Encoding ASCII (Join-Path $OutDir "README.txt")
Copy-Item (Join-Path $pkg "THIRD_PARTY_NOTICES.txt") $OutDir -Force
$raylibLicense = Join-Path $root "..\Simple 2000s DX  DECOMP\PingPong\build\toolchain\_deps\raylib-src\LICENSE"
if (Test-Path $raylibLicense) { Copy-Item $raylibLicense (Join-Path $OutDir "raylib_LICENSE.txt") -Force }

# Never ship game data or developer leftovers
foreach ($bad in @("assets", "disc")) {
    $p = Join-Path $OutDir $bad
    if ((Test-Path $p) -and -not ((Get-Item $p).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        Write-Warning "$p exists in the output folder (player data?) - it is not part of the package; do not distribute it"
    }
}
Write-Host "dist ready: $OutDir"
Get-ChildItem $OutDir -File | Format-Table Name, Length -AutoSize | Out-String | Write-Host

if ($Zip) {
    $zipPath = Join-Path (Split-Path -Parent $OutDir) "Baroque-$Version-win64.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    $files = @("Baroque.exe", "README.txt", "THIRD_PARTY_NOTICES.txt", "raylib_LICENSE.txt") + $dlls |
        ForEach-Object { Join-Path $OutDir $_ } | Where-Object { Test-Path $_ }
    $stage = Join-Path ([IO.Path]::GetTempPath()) ("baroque_zip_" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force (Join-Path $stage "Baroque") | Out-Null
    Copy-Item $files (Join-Path $stage "Baroque")
    Copy-Item $wideDir (Join-Path $stage "Baroque") -Recurse   # the widescreen art folder
    Compress-Archive -Path (Join-Path $stage "Baroque") -DestinationPath $zipPath
    Remove-Item $stage -Recurse -Force
    Write-Host "zip: $zipPath"
}
