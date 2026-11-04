#!/bin/bash
# Install the DAS Kai VST3, AU and OBS plug-in for the current user.
# Usage: scripts/install.sh [build-dir]   (default: build/release)
#
# Follows the plugins_dev install rule: remove, copy, then re-sign. Copying
# over an existing bundle keeps a stale signature cache and the host kills
# the plug-in on load (Killed: 9).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build/release}"
artefacts="$build/plugins/send-vst3/DasKai_artefacts/Release"

vst3_src="$artefacts/VST3/DAS Kai.vst3"
au_src="$artefacts/AU/DAS Kai.component"
obs_src="$build/plugins/obs-source/das-kai-obs.plugin"

vst3_dst="$HOME/Library/Audio/Plug-Ins/VST3/DAS Kai.vst3"
au_dst="$HOME/Library/Audio/Plug-Ins/Components/DAS Kai.component"
obs_dst="$HOME/Library/Application Support/obs-studio/plugins/das-kai-obs.plugin"

for source in "$vst3_src" "$au_src" "$obs_src"; do
  [ -d "$source" ] || { echo "missing build output: $source" >&2; exit 1; }
done

# Bundles from the working name "Kinu Stream" carry the same plug-in IDs and
# OBS source ID; two copies confuse hosts. Move them to the Trash (recoverable).
for legacy in "$HOME/Library/Audio/Plug-Ins/VST3/Kinu Stream.vst3" \
              "$HOME/Library/Audio/Plug-Ins/Components/Kinu Stream.component" \
              "$HOME/Library/Application Support/obs-studio/plugins/kinu-stream-obs.plugin"; do
  if [ -e "$legacy" ]; then
    trashed="$HOME/.Trash/$(basename "$legacy").$(date +%Y%m%d-%H%M%S)"
    mv "$legacy" "$trashed"
    echo "moved to Trash: $legacy"
  fi
done

install_bundle() {
  local source="$1" destination="$2"
  # Sign the build output first so the copy is byte-identical and the hash
  # comparison below means "same build", not "same signature state".
  codesign --force --deep -s - "$source"
  mkdir -p "$(dirname "$destination")"
  rm -rf "$destination"
  cp -R "$source" "$destination"
  codesign --force --deep -s - "$destination"
  codesign --verify --deep --strict "$destination"
  echo "installed: $destination"
}

install_bundle "$vst3_src" "$vst3_dst"
install_bundle "$au_src" "$au_dst"
install_bundle "$obs_src" "$obs_dst"

# Record what was installed so a host test can be tied to an exact build.
echo "--- SHA-256 (build vs installed)"
shasum -a 256 "$vst3_src/Contents/MacOS/DAS Kai" "$vst3_dst/Contents/MacOS/DAS Kai" \
  "$au_src/Contents/MacOS/DAS Kai" "$au_dst/Contents/MacOS/DAS Kai" \
  "$obs_src/Contents/MacOS/das-kai-obs" "$obs_dst/Contents/MacOS/das-kai-obs"
