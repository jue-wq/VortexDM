#!/usr/bin/env bash
set -euo pipefail
rm -f "$HOME/.mozilla/native-messaging-hosts/com.vortexdm.bridge.json"
for dir in "$HOME/.config/google-chrome/NativeMessagingHosts" "$HOME/.config/chromium/NativeMessagingHosts" "$HOME/.config/microsoft-edge/NativeMessagingHosts"; do
  rm -f "$dir/com.vortexdm.bridge.json"
done
echo "VortexDM Native Messaging host registrations removed."
