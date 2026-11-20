/*
 * Vortex Shield bootstrap blocker.
 * Small first-party domain-list MVP; not uBlock Origin and not an ABP parser.
 */
"use strict";

const blockedHosts = new Set([
  "doubleclick.net",
  "googlesyndication.com",
  "googleadservices.com",
  "adservice.google.com",
  "adnxs.com",
  "taboola.com",
  "outbrain.com",
  "scorecardresearch.com",
  "criteo.com",
  "zedo.com"
]);

function matchesBlockedHost(hostname) {
  const host = hostname.toLowerCase().replace(/\.$/, "");
  for (const blocked of blockedHosts) {
    if (host === blocked || host.endsWith("." + blocked)) return true;
  }
  return false;
}

browser.webRequest.onBeforeRequest.addListener(
  (details) => {
    try {
      return { cancel: matchesBlockedHost(new URL(details.url).hostname) };
    } catch (_error) {
      return { cancel: false };
    }
  },
  { urls: ["<all_urls>"] },
  ["blocking"]
);
