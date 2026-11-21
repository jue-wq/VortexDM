/*
 * Vortex Shield: conservative ABP host-rule subset.
 * Supports ||hostname^ block/exception rules and selected request-type options.
 * It deliberately ignores cosmetic, regex, wildcard, scriptlet, redirect,
 * CSP, and advanced uBO-specific syntax. It is not uBlock Origin parity.
 */
"use strict";

const subscriptions = [
  { id: "easylist", url: "https://easylist.to/easylist/easylist.txt" },
  { id: "easyprivacy", url: "https://easylist.to/easylist/easyprivacy.txt" }
];

const seededHosts = [
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
];

let blockRules = [];
let allowRules = [];
let blockedRequestCount = 0;

const typeMap = {
  script: "script",
  image: "image",
  stylesheet: "stylesheet",
  subdocument: "sub_frame",
  xmlhttprequest: "xmlhttprequest",
  media: "media",
  font: "font",
  other: "other",
  document: "main_frame"
};

function hostMatches(host, ruleHost) {
  return host === ruleHost || host.endsWith("." + ruleHost);
}

function parseHostRule(rawLine) {
  let line = rawLine.trim();
  if (!line || line.startsWith("!") || line.startsWith("#") ||
      line.startsWith("[") || line.includes("##") || line.includes("#@#")) return null;

  const exception = line.startsWith("@@");
  if (exception) line = line.slice(2);
  if (!line.startsWith("||")) return null;

  const body = line.slice(2);
  const splitAt = body.search(/[\^$]/);
  const host = (splitAt < 0 ? body : body.slice(0, splitAt)).toLowerCase();
  if (!host || !/^[a-z0-9.-]+$/.test(host) || host.startsWith(".") ||
      host.endsWith(".") || host.includes("..")) return null;

  const suffix = splitAt < 0 ? "" : body.slice(splitAt);
  const optionText = suffix.startsWith("^$") ? suffix.slice(2) :
    suffix.startsWith("$") ? suffix.slice(1) : "";
  const types = [];
  if (optionText) {
    const options = optionText.split(",");
    for (const option of options) {
      if (option.startsWith("~")) return null;
      if (option === "third-party" || option === "3p" ||
          option === "first-party" || option === "1p") return null;
      if (!(option in typeMap)) return null;
      types.push(typeMap[option]);
    }
  }
  return { host, exception, types };
}

async function readBoundedText(response, maximumBytes) {
  const declaredLength = Number(response.headers.get("content-length"));
  if (Number.isFinite(declaredLength) && declaredLength > maximumBytes) {
    throw new Error("Filter list exceeds the configured size limit");
  }
  if (!response.body || typeof response.body.getReader !== "function") {
    const text = await response.text();
    if (new TextEncoder().encode(text).byteLength > maximumBytes) {
      throw new Error("Filter list exceeds the configured size limit");
    }
    return text;
  }

  const reader = response.body.getReader();
  const chunks = [];
  let received = 0;
  try {
    while (true) {
      const result = await reader.read();
      if (result.done) break;
      received += result.value.byteLength;
      if (received > maximumBytes) {
        await reader.cancel("Filter list size limit exceeded");
        throw new Error("Filter list exceeds the configured size limit");
      }
      chunks.push(result.value);
    }
  } finally {
    reader.releaseLock();
  }

  const bytes = new Uint8Array(received);
  let offset = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return new TextDecoder("utf-8", { fatal: false }).decode(bytes);
}

function compileSubscriptions(texts) {
  const blocks = seededHosts.map(host => ({ host, exception: false, types: [] }));
  const allows = [];
  for (const text of texts) {
    for (const line of text.split(/\r?\n/)) {
      const rule = parseHostRule(line);
      if (!rule) continue;
      (rule.exception ? allows : blocks).push(rule);
    }
  }
  blockRules = blocks;
  allowRules = allows;
}

function matchesRule(rule, host, requestType) {
  return hostMatches(host, rule.host) &&
    (rule.types.length === 0 || rule.types.includes(requestType));
}

function shouldBlock(details) {
  let requestUrl;
  try {
    requestUrl = new URL(details.url);
  } catch (_error) {
    return false;
  }
  const host = requestUrl.hostname.toLowerCase().replace(/\.$/, "");
  const type = details.type || "other";

  if (allowRules.some(rule => matchesRule(rule, host, type))) return false;
  return blockRules.some(rule => matchesRule(rule, host, type));
}

async function loadCachedSubscriptions() {
  const keys = subscriptions.map(item => "filter:" + item.id);
  const stored = await browser.storage.local.get(keys);
  const texts = keys.map(key => stored[key] || "");
  compileSubscriptions(texts);
}

async function updateSubscriptions() {
  const texts = [];
  for (const item of subscriptions) {
    try {
      const response = await fetch(item.url, { cache: "no-store" });
      if (!response.ok) throw new Error("HTTP " + response.status);
      const text = await readBoundedText(response, 20 * 1024 * 1024);
      if (!text) throw new Error("Filter list is empty");
      await browser.storage.local.set({
        ["filter:" + item.id]: text,
        ["filterUpdated:" + item.id]: Date.now()
      });
      texts.push(text);
    } catch (error) {
      console.warn("Vortex Shield update failed for " + item.id, error);
      const cached = await browser.storage.local.get("filter:" + item.id);
      texts.push(cached["filter:" + item.id] || "");
    }
  }
  compileSubscriptions(texts);
}

browser.webRequest.onBeforeRequest.addListener(
  (details) => {
    if (!shouldBlock(details)) return { cancel: false };
    blockedRequestCount += 1;
    return { cancel: true };
  },
  { urls: ["<all_urls>"] },
  ["blocking"]
);

browser.alarms.onAlarm.addListener(alarm => {
  if (alarm.name === "vortex-shield-filter-update") updateSubscriptions();
});

browser.runtime.onInstalled.addListener(async () => {
  await loadCachedSubscriptions();
  await updateSubscriptions();
  browser.alarms.create("vortex-shield-filter-update", { periodInMinutes: 1440 });
});

browser.runtime.onStartup.addListener(async () => {
  await loadCachedSubscriptions();
  await updateSubscriptions();
  browser.alarms.create("vortex-shield-filter-update", { periodInMinutes: 1440 });
});

loadCachedSubscriptions().catch(error =>
  console.error("Vortex Shield cache load failed", error)
);
