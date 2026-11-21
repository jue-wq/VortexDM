#!/usr/bin/env bash
set -euo pipefail
rm -f "$HOME/.mozilla/native-messaging-hosts/com.vortexdm.native_host.json"
for dir in "$HOME/.config/google-chrome/NativeMessagingHosts" "$HOME/.config/chromium/NativeMessagingHosts" "$HOME/.config/microsoft-edge/NativeMessagingHosts"; do
  rm -f "$dir/com.vortexdm.native_host.json"
done
echo "VortexDM Native Messaging host registrations removed."
