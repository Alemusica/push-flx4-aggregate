#!/bin/bash
set -euo pipefail

PLUGIN_DST="/Library/Audio/Plug-Ins/HAL/PushFLX4Aggregate.driver"
HELPER_DST="/usr/local/bin/pushflx4-helper"
PLIST_DST="$HOME/Library/LaunchAgents/com.pushflx4.helper.plist"
DOMAIN="gui/$(id -u)"

echo "==> Stopping helper..."
launchctl bootout "$DOMAIN/com.pushflx4.helper" 2>/dev/null || true

echo "==> Removing LaunchAgent..."
rm -f "$PLIST_DST"

echo "==> Removing helper..."
sudo rm -f "$HELPER_DST"

echo "==> Removing plugin..."
sudo rm -rf "$PLUGIN_DST"

echo "==> Restarting coreaudiod..."
sudo killall coreaudiod || true

echo "==> Uninstalled."
