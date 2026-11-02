#!/bin/sh
set -eu
spacesmart_tests=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
spacesmart_root=$(CDPATH= cd -- "$spacesmart_tests/../.." && pwd)
spacesmart_build="$spacesmart_root/build/arduino-tests"
mkdir -p "$spacesmart_build"
spacesmart_cxx=${CXX:-c++}
python3 "$spacesmart_tests/check_wiring.py"
for spacesmart_test in app_logic radar_frame dashboard; do
  "$spacesmart_cxx" -std=c++17 "$spacesmart_tests/${spacesmart_test}_test.cpp" -o "$spacesmart_build/$spacesmart_test"
  "$spacesmart_build/$spacesmart_test"
done
"$spacesmart_cxx" -std=c++17 -I "$spacesmart_tests/lightbar_stubs" "$spacesmart_tests/lightbar_test.cpp" "$spacesmart_tests/../SpaceSmart/LightbarService.cpp" -o "$spacesmart_build/lightbar"
"$spacesmart_build/lightbar"
"$spacesmart_cxx" -std=c++17 "$spacesmart_tests/../tools/render_dashboard.cpp" "$spacesmart_tests/../SpaceSmart/DashboardRenderer.cpp" "$spacesmart_tests/../SpaceSmart/DashboardFonts.cpp" -o "$spacesmart_build/render"
"$spacesmart_build/render" "$spacesmart_root/build/dashboard"
