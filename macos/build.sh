#!/bin/zsh
set -euo pipefail
spacesmart_source=${0:A:h}
spacesmart_root=${spacesmart_source:h}
spacesmart_output=${1:-"$spacesmart_root/build/macos"}
spacesmart_build=$(mktemp -d "${TMPDIR:-/tmp}/spacesmart-build.XXXXXX")
trap 'rm -rf "$spacesmart_build"' EXIT
mkdir -p "$spacesmart_output"
spacesmart_app="$spacesmart_output/SpaceSmart.app"
if [[ -e "$spacesmart_app" && ! -f "$spacesmart_app/Contents/Info.plist" ]]; then
  print -u2 'Existing output is not an app bundle; refusing to replace it.'
  exit 1
fi
mkdir -p "$spacesmart_app/Contents/MacOS" "$spacesmart_app/Contents/Resources"
for spacesmart_arch in arm64 x86_64; do
  /usr/bin/xcrun swiftc -O -parse-as-library -target "$spacesmart_arch-apple-macos13.0" \
    "$spacesmart_source/AgentStore.swift" "$spacesmart_source/AgentMain.swift" \
    -o "$spacesmart_build/SpaceSmartAgent-$spacesmart_arch"
  /usr/bin/xcrun swiftc -O -parse-as-library -target "$spacesmart_arch-apple-macos13.0" \
    "$spacesmart_source/AgentStore.swift" "$spacesmart_source/HookInstaller.swift" \
    "$spacesmart_source/WorkspaceUI.swift" "$spacesmart_source/CustomData.swift" "$spacesmart_source/CustomWindow.swift" \
    "$spacesmart_source/LightLearning.swift" "$spacesmart_source/LightController.swift" "$spacesmart_source/LightWindow.swift" \
    "$spacesmart_source/Telemetry.swift" "$spacesmart_source/BluetoothBridge.swift" "$spacesmart_source/AppMain.swift" \
    -o "$spacesmart_build/SpaceSmart-$spacesmart_arch"
done
/usr/bin/lipo -create "$spacesmart_build/SpaceSmart-arm64" "$spacesmart_build/SpaceSmart-x86_64" -output "$spacesmart_app/Contents/MacOS/SpaceSmart"
/usr/bin/lipo -create "$spacesmart_build/SpaceSmartAgent-arm64" "$spacesmart_build/SpaceSmartAgent-x86_64" -output "$spacesmart_app/Contents/Resources/SpaceSmartAgent"
cp "$spacesmart_source/Info.plist" "$spacesmart_app/Contents/Info.plist"
if [[ -f "$spacesmart_source/SpaceSmart.icns" ]]; then cp "$spacesmart_source/SpaceSmart.icns" "$spacesmart_app/Contents/Resources/"; fi
if [[ -f "$spacesmart_root/docs/SpaceSmart-설치가이드.pdf" ]]; then cp "$spacesmart_root/docs/SpaceSmart-설치가이드.pdf" "$spacesmart_app/Contents/Resources/"; fi
/usr/bin/codesign --force --sign - "$spacesmart_app/Contents/Resources/SpaceSmartAgent"
/usr/bin/codesign --force --sign - "$spacesmart_app"
/usr/bin/codesign --verify --deep --strict "$spacesmart_app"
print "Built universal app: $spacesmart_app"
