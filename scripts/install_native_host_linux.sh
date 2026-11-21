#!/usr/bin/env bash
set -euo pipefail
BIN_PATH="${1:-$(command -v vortex-native-host || true)}"
CHROME_EXTENSION_ID="${2:-}"
if [[ -z "$BIN_PATH" || ! -x "$BIN_PATH" ]]; then
  echo "Usage: $0 /absolute/path/to/vortex-native-host [32-character Chrome extension id]" >&2
  exit 2
fi
BIN_PATH="$(realpath "$BIN_PATH")"
FIREFOX_DIR="$HOME/.mozilla/native-messaging-hosts"
mkdir -p "$FIREFOX_DIR"
python3 - "$BIN_PATH" "$FIREFOX_DIR/com.vortexdm.native_host.json" <<'PY'
import json,sys
binary,path=sys.argv[1:]
with open(path,"w",encoding="utf-8") as f:
    json.dump({"name":"com.vortexdm.native_host","description":"VortexDM Native Messaging host","path":binary,"type":"stdio","allowed_extensions":["vortex-browser-bridge@vortexdm.local"]},f,indent=2)
    f.write("\n")
PY
if [[ -n "$CHROME_EXTENSION_ID" ]]; then
  if [[ ! "$CHROME_EXTENSION_ID" =~ ^[a-p]{32}$ ]]; then
    echo "Chrome extension id must be 32 characters from a-p." >&2
    exit 2
  fi
  for dir in "$HOME/.config/google-chrome/NativeMessagingHosts" "$HOME/.config/chromium/NativeMessagingHosts" "$HOME/.config/microsoft-edge/NativeMessagingHosts"; do
    mkdir -p "$dir"
    python3 - "$BIN_PATH" "$dir/com.vortexdm.native_host.json" "$CHROME_EXTENSION_ID" <<'PY'
import json,sys
binary,path,extension_id=sys.argv[1:]
with open(path,"w",encoding="utf-8") as f:
    json.dump({"name":"com.vortexdm.native_host","description":"VortexDM Native Messaging host","path":binary,"type":"stdio","allowed_origins":[f"chrome-extension://{extension_id}/"]},f,indent=2)
    f.write("\n")
PY
  done
fi
echo "Firefox Native Messaging host installed."
if [[ -n "$CHROME_EXTENSION_ID" ]]; then echo "Chromium/Chrome/Edge Native Messaging host installed."; fi
