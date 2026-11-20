# Vortex Shield extension

Vortex Shield is a first-party GeckoView WebExtension. It currently supports a
conservative ABP-compatible subset:

- Seeded advertising/tracking hostnames.
- Host blocking rules in the `||hostname^` form.
- Host exceptions in the `@@||hostname^` form.
- A limited set of request-type options (script, image, stylesheet, subdocument,
  XHR, media, font, document, and other).
- EasyList and EasyPrivacy HTTPS subscriptions, cached in extension storage.
- Daily scheduled updates with cached-list fallback on network errors.
- A per-process blocked-request counter.

## Important compatibility limits

This is not uBlock Origin and must not be advertised as equivalent. The current
parser intentionally skips cosmetic selectors, regex and wildcard rules,
third-party/first-party options, domain-scoped options, redirects, scriptlets,
CSP rules, and other advanced ABP/uBO syntax. The parser is deliberately
conservative to avoid broad accidental blocking. No full compatibility or
coverage claim is made.

## Security and reliability work still required

- Add checksum/signature metadata validation and atomic subscription versioning.
- Add maximum line count, parser budgets, and regression fixtures for upstream lists.
- Add first/third-party matching using a Public Suffix List implementation.
- Add per-site switch, logger, blocked counters in UI, and custom rule editor.
- Add cosmetic filtering and element picker.
- Add filter engine unit tests and Android instrumentation tests.
- Independently validate official uBlock Origin add-on installation and API coverage.

Filter subscriptions are fetched over HTTPS. Browsing URLs and filter decisions
are not sent to filter-list publishers by this extension.
