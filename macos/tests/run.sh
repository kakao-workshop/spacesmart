#!/bin/sh
set -eu
spacesmart_source=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
spacesmart_root=$(CDPATH= cd -- "$spacesmart_source/.." && pwd)
spacesmart_build="$spacesmart_root/build/macos-tests"
mkdir -p "$spacesmart_build"
xcrun swiftc -parse-as-library "$spacesmart_source/AgentStore.swift" "$spacesmart_source/HookInstaller.swift" "$spacesmart_source/CustomData.swift" "$spacesmart_source/Telemetry.swift" "$spacesmart_source/NativeTests.swift" -o "$spacesmart_build/native-tests"
"$spacesmart_build/native-tests"
xcrun swiftc -parse-as-library "$spacesmart_source/AgentStore.swift" "$spacesmart_source/HookInstaller.swift" "$spacesmart_source/LightLearning.swift" "$spacesmart_source/LightController.swift" "$spacesmart_source/LightTests.swift" -o "$spacesmart_build/light-tests"
"$spacesmart_build/light-tests"
