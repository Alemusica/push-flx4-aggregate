#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

PLUGIN_SRC="$BUILD_DIR/plugin/PushFLX4Aggregate.driver"
HELPER_SRC="$BUILD_DIR/helper/pushflx4-helper"
PLUGIN_DST="/Library/Audio/Plug-Ins/HAL/PushFLX4Aggregate.driver"
HELPER_DST="/usr/local/bin/pushflx4-helper"
PLIST_SRC="$PROJECT_DIR/com.pushflx4.helper.plist"
PLIST_DST="$HOME/Library/LaunchAgents/com.pushflx4.helper.plist"
DOMAIN="gui/$(id -u)"

echo "==> Building..."
mkdir -p "$BUILD_DIR"
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"

echo "==> Installing plugin..."
sudo rm -rf "$PLUGIN_DST"
sudo cp -R "$PLUGIN_SRC" "$PLUGIN_DST"
sudo chown -R root:wheel "$PLUGIN_DST"
codesign --force --sign - "$PLUGIN_DST"

echo "==> Installing helper..."
sudo cp "$HELPER_SRC" "$HELPER_DST"
sudo chown root:wheel "$HELPER_DST"
codesign --force --sign - "$HELPER_DST"

echo "==> Installing LaunchAgent..."
launchctl bootout "$DOMAIN/com.pushflx4.helper" 2>/dev/null || true
cp "$PLIST_SRC" "$PLIST_DST"
launchctl bootstrap "$DOMAIN" "$PLIST_DST"

echo "==> Restarting coreaudiod..."
sudo killall coreaudiod || true

echo "==> Done. Check with: system_profiler SPAudioDataType | grep -A5 'Push+FLX4'"
