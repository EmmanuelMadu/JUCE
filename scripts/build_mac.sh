#!/bin/bash
# Builds Hearth Keys on your own Mac and installs it for the current user.
# Needs Apple's command-line tools (xcode-select --install) and CMake (cmake.org or "brew install cmake").
# Run from Terminal:   bash scripts/build_mac.sh
set -e
cd "$(dirname "$0")/.."
xcode-select -p >/dev/null 2>&1 || { echo "Apple's command-line tools are missing. Run:  xcode-select --install"; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo "CMake is missing. Install it from https://cmake.org/download/ or with:  brew install cmake"; exit 1; }
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel --target HearthKeys_VST3 HearthKeys_AU HearthKeys_Standalone
A="build/HearthKeys_artefacts/Release"
rm -rf "dist/Hearth Keys"
mkdir -p "dist/Hearth Keys"
cp -R "$A/VST3/Hearth Keys.vst3" "$A/AU/Hearth Keys.component" "$A/Standalone/Hearth Keys.app" "dist/Hearth Keys/"
cp scripts/install_mac.sh "dist/Hearth Keys/"
bash "dist/Hearth Keys/install_mac.sh"
