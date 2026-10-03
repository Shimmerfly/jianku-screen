#!/bin/sh
# Best-effort stable code signing so TCC permissions survive rebuilds.
# Unlocks the dedicated keychain first so codesign never blocks on a GUI prompt.
APP="$1"
[ -n "$APP" ] || exit 0
KC="$HOME/Library/Keychains/jianku-dev.keychain-db"
/usr/bin/security unlock-keychain -p jianku "$KC" >/dev/null 2>&1 || true
/usr/bin/codesign --force --deep --sign JiankuDev --identifier com.jianku.screen --keychain "$KC" "$APP" >/dev/null 2>&1 \
    || /usr/bin/codesign --force --deep --sign JiankuDev --identifier com.jianku.screen "$APP" >/dev/null 2>&1 \
    || true
exit 0
