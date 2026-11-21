# Android uBlock Origin integration decision

## User requirement
Android browsing must use the original uBlock Origin (uBO) by Raymond Hill, not a VortexDM reimplementation or third-party substitute, wherever the upstream-supported route is available.

## Supported route
- Target browser: Firefox for Android.
- Install original uBO from Mozilla's official listing: https://addons.mozilla.org/android/addon/ublock-origin/ . Verify publisher Raymond Hill and GPL-3.0-only license.
- Vortex Browser Bridge handles download detection and explicit handoff to VortexDM. It is not the Android ad blocker and must not claim uBO parity.
- Do not silently bundle a repackaged/forked copy of uBO into the downloader APK. Let Mozilla deliver and update the signed official add-on in Firefox.
- Google Chrome for Android is not an extension target. macOS remains excluded.

## Embedded browser option (experimental, not a release claim)
GeckoView can install bundled WebExtensions using WebExtensionController.installBuiltIn and APK assets. This makes an embedded Vortex browser technically plausible, but Mozilla's general WebExtension support documentation does not guarantee every uBO API dependency works in an arbitrary GeckoView embedder. Before claiming embedded uBO support, create a GeckoView proof-of-concept, load the unmodified upstream Firefox build of uBO, and pass the compatibility checklist below. If any required API is missing, keep Firefox for Android as the supported browser rather than patching uBO or substituting a clone.

## Compatibility and release gates
- [ ] GeckoView version pinned and Android ABI/build verified
- [ ] Original upstream uBO provenance and GPL-3.0 compliance documented if redistributed
- [ ] No uBO source modifications or branding changes
- [ ] Extension installs and starts using documented GeckoView API
- [ ] Network filtering, cosmetic filtering, exceptions, logger/dashboard, filter-list updates and restart persistence tested on device
- [ ] No duplicate Vortex Bridge blocker in the same Android browser profile
- [ ] Android 9+ arm64 device and emulator E2E pass

## Important distinction
uBO is a browser extension, not a device-wide Android VPN blocker or an APK that filters every other Android app. It filters supported web content in the browser where it is installed.
