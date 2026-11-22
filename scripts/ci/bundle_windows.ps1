$ErrorActionPreference = "Stop"
$stage = Join-Path $env:RUNNER_TEMP "vortexdm-windows"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$app = Join-Path $env:GITHUB_WORKSPACE "build\desktop\vortexdm.exe"
if (-not (Test-Path $app)) { throw "VortexDM executable missing: $app" }
Copy-Item $app $stage
$windeployqt = Join-Path $env:QT_ROOT_DIR "bin\windeployqt.exe"
if (-not (Test-Path $windeployqt)) { throw "windeployqt missing: $windeployqt" }
& $windeployqt --qmldir (Join-Path $env:GITHUB_WORKSPACE "src\ui") --release --compiler-runtime (Join-Path $stage "vortexdm.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

$vcpkgBin = Join-Path $env:GITHUB_WORKSPACE "build\desktop\vcpkg_installed\x64-windows-release\bin"
if (-not (Test-Path $vcpkgBin)) { throw "vcpkg runtime DLL directory missing: $vcpkgBin" }
Get-ChildItem $vcpkgBin -Filter *.dll | Copy-Item -Destination $stage -Force

$required = @("libcurl.dll", "sqlite3.dll", "libcrypto*.dll", "libssl*.dll", "tinyxml2.dll")
foreach ($pattern in $required) {
  if (-not (Get-ChildItem $stage -Filter $pattern -Recurse)) {
    throw "Required runtime DLL missing: $pattern"
  }
}
$archive = Join-Path $env:GITHUB_WORKSPACE "VortexDM-windows-x64.zip"
Remove-Item $archive -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $archive -Force
