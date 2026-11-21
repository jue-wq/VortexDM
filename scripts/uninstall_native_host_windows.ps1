$ErrorActionPreference = "SilentlyContinue"
foreach ($Root in @("Mozilla","Google\Chrome","Microsoft\Edge","Chromium")) {
  $Key = "HKCU:\Software\$Root\NativeMessagingHosts\com.vortexdm.bridge"
  Remove-Item -Recurse -Force $Key
}
Remove-Item -Recurse -Force (Join-Path $env:LOCALAPPDATA "VortexDM\NativeMessagingHosts")
Write-Host "VortexDM Native Messaging host registrations removed."
