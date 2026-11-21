# Desktop Native Messaging host installation

The desktop Bridge host name is `com.vortexdm.native_host`. Firefox uses the stable add-on ID `vortex-browser-bridge@vortexdm.local`. Chromium browsers require the installed extension's 32-character ID.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVORTEX_BUILD_UI=OFF
cmake --build build --config Release --target vortex-native-host
cmake --install build --prefix "$HOME/.local"
```

The installed executable is `$HOME/.local/bin/vortex-native-host` on Linux. On Windows use the executable path from the Release build output.

## Linux

```sh
./scripts/install_native_host_linux.sh "$HOME/.local/bin/vortex-native-host"
```

For Chrome/Chromium/Edge also pass the extension ID:

```sh
./scripts/install_native_host_linux.sh "$HOME/.local/bin/vortex-native-host" <chrome-extension-id>
```

The script registers Firefox automatically and registers Chromium-family browsers only when a valid extension ID is supplied. It writes user-scoped manifests; no root access is required.

## Windows PowerShell

```powershell
.\scripts\install_native_host_windows.ps1 -BinaryPath "C:\path\to\vortex-native-host.exe"
```

For Chrome/Edge/Chromium:

```powershell
.\scripts\install_native_host_windows.ps1 -BinaryPath "C:\path\to\vortex-native-host.exe" -ChromeExtensionId "<chrome-extension-id>"
```

Uninstall with `scripts/uninstall_native_host_linux.sh` or `scripts/uninstall_native_host_windows.ps1`.

## Android

Android does not use Native Messaging. Firefox for Android uses VortexDM's authenticated loopback HTTP server. Enter the ephemeral port and per-launch API key shown in VortexDM's Android Bridge panel in the extension options after each app restart.

macOS is intentionally unsupported.
