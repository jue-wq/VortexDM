# Vortex Shield extension

This bundled GeckoView WebExtension is the initial first-party network-blocking
bootstrap. It blocks a small conservative set of known advertising/tracking
hostnames using the WebExtensions webRequest API.

It is not uBlock Origin, does not parse EasyList/ABP syntax, does not implement
cosmetic filtering, and does not yet provide a user-facing rules dashboard.
Do not market this bootstrap as full uBlock compatibility.

Next implementation steps:
1. Add a tested ABP/uBO-compatible parser and rule compiler.
2. Add subscription downloads, ETag/Last-Modified handling, checksums, and
   atomic cache replacement.
3. Add first-party/third-party matching, request types, exceptions, and
   per-site allow rules.
4. Add cosmetic filters and an element picker through content scripts.
5. Add request logger, blocked counter, and a per-site shield switch.
6. Validate official uBlock Origin Firefox add-on compatibility independently.
