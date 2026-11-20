# VortexDM Android Architecture

## Product target

Android is a first-class product target, not a scaled-down desktop UI.
Supported platforms are Windows, Linux, and Android. macOS is not a supported
build or CI target.

## Application stack

- Native Android application shell: Kotlin, Android SDK, Gradle.
- Browser engine: Mozilla GeckoView, pinned and updated deliberately.
- Browser extension surface: GeckoView WebExtensions API.
- Download/business logic: shared C++23 core through a narrow JNI adapter.
- Persistence: SQLite; app-private storage by default, Storage Access
  Framework for user-selected destinations.
- Download lifecycle: user-visible ongoing notification and Android-compliant
  transfer scheduling. Do not assume an unlimited background execution window.

The Android UI is native and touch-first. Desktop Qt/QML is not reused as the
Android presentation layer.

## Vortex Shield content blocking

### Implemented bootstrap

The Android app now bundles a GeckoView WebExtension at
android/app/src/main/assets/vortex-shield. It is installed through
WebExtensionController.ensureBuiltIn. The initial background script uses
webRequest blocking against a deliberately small hostname set. This is a
development bootstrap only: it is not an ABP parser, does not load filter
subscriptions, and does not implement cosmetic filtering or uBlock Origin
feature parity. The README next to the extension records these limitations.

### Planned production behavior

The browser must provide a wide-spectrum blocker inspired by uBlock Origin:

1. Network filtering: block/allow rules, request types, third-party context,
   first/third-party domain matching, exceptions, and per-site controls.
2. Filter subscriptions: EasyList, EasyPrivacy, Peter Lowe's list, and malware
   domain lists. Subscription metadata, source URL, last update, checksum,
   enabled state, and update errors are persisted.
3. Cosmetic filtering: supported element-hiding rules, per-site exceptions,
   and a user element picker.
4. Advanced controls: temporary/permanent site allow rules, custom filter text,
   logger view, blocked-request counter, and disable-on-this-site action.
5. Privacy: no telemetry by default; filter updates use HTTPS; no browsing
   history is sent to filter providers.

## GeckoView integration decision

GeckoView is pinned to the 163.0.20261120154857 release line. Use GeckoView rather than Android WebView because GeckoView is designed for
embedded browser use and exposes WebExtensions. Its
WebExtensionController.installBuiltIn API can install app-bundled extensions.
The Android browser should expose an extension manager and validate extension
API compatibility before claiming full uBlock Origin support.

The browser shell now exposes an explicit uBlock button that asks GeckoView to
install the official Mozilla Add-ons signed XPI. Installation is user-initiated
and its success is reported in the browser status line. This is an integration
probe, not a guarantee that every uBO API or feature is supported by GeckoView.
The preferred compatibility path is to support the official uBlock Origin
Firefox add-on where GeckoView's supported WebExtension APIs permit it. Do not
silently redistribute a modified uBlock Origin build. If bundling or modifying
uBlock Origin is considered, complete GPL-3.0 compliance and source-offer review
first. Keep a Vortex Shield first-party extension as the controlled fallback.

A plain WebView request interceptor is not an equivalent substitute: Android's
documented interception callbacks have coverage limitations, including redirect
behavior and excluded URL schemes.

## Download lifecycle and Android restrictions

- Use a foreground service only while a user-visible transfer is actively
  running and declare the correct service type and permissions.
- Implement Android 15+ foreground-service timeout handling. A dataSync
  foreground service is subject to a six-hour total limit in a 24-hour period.
- Evaluate user-initiated data transfer jobs and WorkManager for eligible work;
  persist checkpoints before yielding to the OS.
- Recover downloads from persisted state after process death. Never promise
  uninterrupted background transfers when Android may stop the process.
- Use SAF document/tree URIs for user-selected destinations; do not assume
  unrestricted filesystem paths.

## Browser shell status

The current MainActivity provides a basic address/search field, Go, Back,
Forward, and Reload controls. It is a functional navigation scaffold, not a
finished tabbed browser. Tab lifecycle, session persistence, page-title and URL
delegates, permission prompts, downloads, bookmarks, history, and private
sessions remain to be implemented.

## JNI boundary

NativeBridge is the only initial Kotlin-to-C++ entry point. Keep JNI methods
small, versioned, and exception-safe. Pass immutable request/response payloads;
do not expose C++ object pointers to Kotlin. The current JNI method is only a
bootstrap stub and is not yet linked to the full vortex_core library.

## Build

From the repository root:

    gradle -p android assembleDebug

The Android CI job installs Android SDK 36, NDK, and CMake and assembles the
debug APK. The GeckoView artifact is fetched from Mozilla's Maven repository.
arm64-v8a is the primary ABI; x86_64 is included for emulator CI.
armeabi-v7a is intentionally not a launch target.
