#!/bin/sh
# Builds WHALE.vst3 and WHALE.component (Universal: Apple Silicon + Intel) and installs them for the current user.
# Requirements: Xcode (or Command Line Tools), CMake 3.22+, Git. JUCE 8 is downloaded automatically.
set -e

cmake -S . -B build -G Xcode
cmake --build build --config Release --target WHALE_VST3 WHALE_AU

VST3=build/WHALE_artefacts/Release/VST3/WHALE.vst3
AU=build/WHALE_artefacts/Release/AU/WHALE.component

codesign --force --deep --sign - "$VST3"
codesign --force --deep --sign - "$AU"

mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
rm -rf ~/Library/Audio/Plug-Ins/VST3/WHALE.vst3 ~/Library/Audio/Plug-Ins/Components/WHALE.component
cp -R "$VST3" ~/Library/Audio/Plug-Ins/VST3/
cp -R "$AU" ~/Library/Audio/Plug-Ins/Components/

echo "Installed. Rescan plugins in FL Studio / Ableton Live."
