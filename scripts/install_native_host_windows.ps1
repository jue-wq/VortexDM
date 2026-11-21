param(
  [Parameter(Mandatory=$true)][string]$BinaryPath,
  [string]$ChromeExtensionId = ""
)
$ErrorActionPreference = "Stop"
$BinaryPath = (Resolve-Path $BinaryPath).Path
if ($ChromeExtensionId -and $ChromeExtensionId -notmatch '^[a-p]{32}$') {
  throw "Chrome extension id must be 32 characters from a-p."
}
$Base = Join-Path $env:LOCALAPPDATA "VortexDM\NativeMessagingHosts"
New-Item -ItemType Directory -Force -Path $Base | Out-Null
$FirefoxManifest = Join-Path $Base "firefox-com.vortexdm.native_host.json"
@{
  name = "com.vortexdm.native_host"
  description = "VortexDM Native Messaging host"
  path = $BinaryPath
  type = "stdio"
  allowed_extensions = @("vortex-browser-bridge@vortexdm.local")
} | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 $FirefoxManifest
$FirefoxKey = "HKCU:\Software\Mozilla\NativeMessagingHosts\com.vortexdm.native_host"
New-Item -Force -Path $FirefoxKey | Out-Null
Set-Item -Path $FirefoxKey -Value $FirefoxManifest
if ($ChromeExtensionId) {
  $ChromeManifest = Join-Path $Base "chrome-com.vortexdm.native_host.json"
  @{
    name = "com.vortexdm.native_host"
    description = "VortexDM Native Messaging host"
    path = $BinaryPath
    type = "stdio"
    allowed_origins = @("chrome-extension://$ChromeExtensionId/")
  } | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 $ChromeManifest
  foreach ($Root in @("Google\Chrome","Microsoft\Edge","Chromium")) {
    $Key = "HKCU:\Software\$Root\NativeMessagingHosts\com.vortexdm.native_host"
    New-Item -Force -Path $Key | Out-Null
    Set-Item -Path $Key -Value $ChromeManifest
  }
}
Write-Host "Firefox Native Messaging host installed."
if ($ChromeExtensionId) { Write-Host "Chrome/Edge/Chromium Native Messaging host installed." }
