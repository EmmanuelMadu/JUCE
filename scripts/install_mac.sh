#!/bin/bash
# Installs Hearth Keys for the current user: the plug-ins (VST3 + Audio Unit) and the
# recordings of the grand piano.  Run from Terminal:   bash install_mac.sh
# Running it again is harmless: what is already in place is kept.
cd "$(dirname "$0")" || exit 1

REPO="${HK_REPO:-EmmanuelMadu/JUCE}"
PACK="HearthKeys-Grand-v1"
PACK_URL="https://github.com/$REPO/releases/download/grand-pack-v1/$PACK.zip"
PACK_FILES=641
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
AU="$HOME/Library/Audio/Plug-Ins/Components"
GRAND="$HOME/Library/Application Support/Hearth Keys/Grand-v1"

# ---- 1. plug-ins -----------------------------------------------------------------
mkdir -p "$VST3" "$AU"
for item in "Hearth Keys.vst3:$VST3" "Hearth Keys.component:$AU"; do
    name="${item%%:*}"; dest="${item##*:}"
    if [ -d "$name" ]; then
        rm -rf "$dest/$name"
        cp -R "$name" "$dest/" || { echo "Could not copy $name"; exit 1; }
        xattr -dr com.apple.quarantine "$dest/$name" 2>/dev/null || true
        codesign --force --deep --sign - "$dest/$name" >/dev/null 2>&1 || true
        echo "Installed  $name"
    fi
done
if [ -d "Hearth Keys.app" ]; then
    xattr -dr com.apple.quarantine "Hearth Keys.app" 2>/dev/null || true
    codesign --force --deep --sign - "Hearth Keys.app" >/dev/null 2>&1 || true
fi
killall -9 AudioComponentRegistrar 2>/dev/null || true

# ---- 2. grand piano recordings ---------------------------------------------------
# One download of about 270 MB, unpacked to about 1.3 GB.  Kept between updates.
grand_ok() { [ -f "$GRAND/.complete" ] && [ "$(ls "$GRAND" | grep -c '\.wav$')" -eq "$PACK_FILES" ]; }

if grand_ok; then
    echo "Grand piano recordings are already installed."
else
    WORK="$HOME/Library/Caches/HearthKeysInstall"
    mkdir -p "$WORK"
    ZIP="$WORK/$PACK.zip"
    if [ -f "$PACK.zip" ]; then
        ZIP="$PWD/$PACK.zip"                       # a copy placed next to this script is used as it is
    else
        echo "Downloading the grand piano recordings (about 270 MB)..."
        if ! curl -L --fail --retry 3 -C - --progress-bar -o "$ZIP" "$PACK_URL"; then
            rm -f "$ZIP"
            echo "Starting the download again from the beginning..."
            curl -L --fail --retry 3 --progress-bar -o "$ZIP" "$PACK_URL"
        fi
    fi
    if [ ! -s "$ZIP" ]; then
        echo
        echo "The recordings could not be downloaded (no internet connection?)."
        echo "The tine piano is installed and works.  Run this script again later for the grand."
    else
        echo "Unpacking..."
        rm -rf "$WORK/unpacked" "$GRAND.partial"
        mkdir -p "$WORK/unpacked" "$GRAND.partial"
        if unzip -q -o "$ZIP" -d "$WORK/unpacked"; then
            echo "Preparing $PACK_FILES recordings (about half a minute)..."
            export HK_OUT="$GRAND.partial"
            find "$WORK/unpacked/$PACK" -name '*.flac' -print0 | xargs -0 -n 1 -P 6 sh -c \
                'afconvert -f WAVE -d LEI16 "$0" "$HK_OUT/$(basename "${0%.flac}").wav" || echo "failed: $0"'
            cp "$WORK/unpacked/$PACK/"*.txt "$GRAND.partial/" 2>/dev/null || true
        fi
        if [ "$(ls "$GRAND.partial" | grep -c '\.wav$')" -eq "$PACK_FILES" ]; then
            rm -rf "$GRAND"
            mv "$GRAND.partial" "$GRAND"
            touch "$GRAND/.complete"
            rm -rf "$WORK"
            echo "Grand piano recordings installed."
        else
            rm -rf "$GRAND.partial" "$WORK/unpacked"
            [ "$ZIP" = "$WORK/$PACK.zip" ] && rm -f "$ZIP"
            echo
            echo "Something went wrong while preparing the recordings."
            echo "The tine piano is installed and works.  Run this script again to retry the grand."
        fi
    fi
fi

echo
echo "Done.  Restart Logic (or your DAW).  Hearth Keys is under AU Instruments > Homemade."
