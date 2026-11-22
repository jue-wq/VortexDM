const browser = globalThis.browser || globalThis.chrome;
const HOST = "com.vortexdm.native_host";
const API = "http://127.0.0.1";
let nativePort = null;
let appInfo = null;
let pending = new Map();

function envelope(type, payload = {}) {
  return { protocol_version: 1, type, request_id: crypto.randomUUID(), timestamp: Math.floor(Date.now()/1000), payload };
}
function connectNative() {
  if (nativePort) return nativePort;
  try {
    nativePort = browser.runtime.connectNative(HOST);
    nativePort.onMessage.addListener(message => {
      const waiter = pending.get(message.request_id);
      if (waiter) { pending.delete(message.request_id); waiter(message); }
    });
    nativePort.onDisconnect.addListener(() => { nativePort = null; appInfo = null; });
  } catch (_) { nativePort = null; }
  return nativePort;
}
function nativeRequest(type, payload = {}, timeout = 1500) {
  return new Promise(resolve => {
    const port = connectNative();
    if (!port) return resolve(null);
    const message = envelope(type, payload);
    const timer = setTimeout(() => { pending.delete(message.request_id); resolve(null); }, timeout);
    pending.set(message.request_id, response => { clearTimeout(timer); resolve(response); });
    try { port.postMessage(message); } catch (_) { clearTimeout(timer); pending.delete(message.request_id); resolve(null); }
  });
}
async function androidRequest(type, payload = {}) {
  const cfg = await browser.storage.local.get(["androidPort","androidKey"]);
  if (!cfg.androidPort || !cfg.androidKey) return null;
  try {
    const response = await fetch(`${API}:${Number(cfg.androidPort)}/ipc`, {
      method:"POST", headers:{"Content-Type":"application/json","X-Vortex-Api-Key":cfg.androidKey},
      body:JSON.stringify(envelope(type,payload)), signal:AbortSignal.timeout(1500)
    });
    return response.ok ? await response.json() : null;
  } catch (_) { return null; }
}
async function request(type,payload={}) {
  const native = await nativeRequest(type,payload);
  if (native) return native;
  const android = await androidRequest(type,payload);
  return android?.payload ? android : android;
}
async function handshake() {
  const result = await request("handshake",{ext_version:browser.runtime.getManifest().version,browser:"firefox",capabilities:["download_capture","task_status"]});
  if (result?.payload?.accepted) appInfo = result.payload;
  return result;
}
browser.runtime.onInstalled.addListener(() => { handshake(); });
browser.runtime.onStartup.addListener(() => { handshake(); });

browser.downloads.onCreated.addListener(async item => {
  if (!/^https?:/i.test(item.url || "")) return;
  const result = await request("add_download",{url:item.url,filename:item.filename?.split(/[\\/]/).pop() || "",auto_start:true});
  if (result?.payload?.accepted) {
    try { await browser.downloads.cancel(item.id); } catch (_) {}
  }
});
browser.runtime.onMessage.addListener(async message => {
  if (message?.type === "status") {
    if (!appInfo) await handshake();
    const result = await request("task_status",{limit:20});
    return result?.payload ? {...result.payload, queues:appInfo?.queues || []} : {accepted:false,error:"VortexDM is not connected"};
  }
  if (message?.type === "addTab") {
    const result = await request("add_download",{url:message.url,filename:message.filename || "",auto_start:true,queue_id:message.queue_id || "default"});
    return result?.payload || {accepted:false,error:"VortexDM is not connected"};
  }
  if (message?.type === "control") {
    const result = await request("task_control",{task_id:message.task_id,action:message.action});
    return result?.payload || {accepted:false};
  }
  if (message?.type === "handshake") return (await handshake())?.payload || {accepted:false};
  return undefined;
});
