# Windows Firefox Native Messaging host registration

The Windows package installs the host executable, Firefox manifest, and two
per-user PowerShell scripts. Registration is intentionally scoped to
`HKEY_CURRENT_USER` and does not require a machine-wide registry change.

Configure with the final installation prefix before generating the manifest:

    cmake -S . -B build -DCMAKE_INSTALL_PREFIX="C:/Program Files/VortexDM"
    cmake --build build --config Release
    cmake --install build --config Release

Then register Firefox for the Windows user who runs the browser:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Program Files\VortexDM\share\vortexdm\registration\register-firefox-native-host.ps1"

To remove the registration:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Program Files\VortexDM\share\vortexdm\registration\unregister-firefox-native-host.ps1"

Restart Firefox after registration. The manifest allowlists only
`browser-bridge@vortexdm.app`. Chrome is intentionally unsupported until a
stable Chrome extension ID is pinned.

The registration scripts are packaged but are not automatically executed by
CMake installation. A signed product installer should invoke the registration
script in the installing user's context and invoke the unregistration script
during uninstall. The prefix used at configure time must match the final
installation prefix because the Native Messaging manifest contains an absolute
executable path.

This packaging does not make the bridge production-ready: the host still writes
to a separate inbox and returns a negative ACK until the actual application
download queue is connected.
