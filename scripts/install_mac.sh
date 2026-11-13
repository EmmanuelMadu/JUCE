#!/bin/bash
# Installs Hearth Keys for the current user (VST3 + Audio Unit) and clears the download quarantine.
# Run from Terminal:   bash install_mac.sh
set -e
cd "$(dirname "$0")"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
AU="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3" "$AU"
for item in "Hearth Keys.vst3:$VST3" "Hearth Keys.component:$AU"; do
    name="${item%%:*}"; dest="${item##*:}"
    if [ -d "$name" ]; then
        rm -rf "$dest/$name"
        cp -R "$name" "$dest/"
        xattr -dr com.apple.quarantine "$dest/$name" 2>/dev/null || true
        codesign --force --deep --sign - "$dest/$name" >/dev/null 2>&1 || true
        echo "installed $dest/$name"
    fi
done
if [ -d "Hearth Keys.app" ]; then
    xattr -dr com.apple.quarantine "Hearth Keys.app" 2>/dev/null || true
    codesign --force --deep --sign - "Hearth Keys.app" >/dev/null 2>&1 || true
    echo "Hearth Keys.app (standalone) is ready to open from this folder"
fi
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Done. Restart your DAW and rescan plug-ins if Hearth Keys does not appear."
