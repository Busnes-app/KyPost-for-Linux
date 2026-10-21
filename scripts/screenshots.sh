#!/usr/bin/env bash
# Renders every screen offscreen with fixture data (tests/screenshots/) and
# rebuilds build-shots/compare.html against the Android store shots.
#
#   scripts/screenshots.sh [label]
#
# label defaults to "after"; shots land in build-shots/<label>/<layout>-<theme>/.
# Set ANDROID_REF to a folder of Android store shots to compare against.
# Override the root and screen script per layout with DESKTOP_ROOT, MOBILE_ROOT,
# DESKTOP_SCRIPT and MOBILE_SCRIPT (the "before" shots used the old roots).
# Every build and run is capped: the host shares one memory cgroup.
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
label=${1:-after}
desktop_root=${DESKTOP_ROOT:-qrc:/qml/AppShell.qml}
mobile_root=${MOBILE_ROOT:-$desktop_root}
desktop_script=${DESKTOP_SCRIPT:-$repo/tests/screenshots/screens-shell.qml}
mobile_script=${MOBILE_SCRIPT:-$desktop_script}
build=$repo/build
shots=$repo/build-shots
cap=(systemd-run --user --scope -q -p MemoryMax=6G -p MemorySwapMax=0 --)

if [[ ! -f $build/CMakeCache.txt ]]; then
    sqlcipher=()
    [[ -d $HOME/.local/opt/kypost-sqlcipher ]] && sqlcipher=(-DKYPOST_SQLCIPHER_ROOT="$HOME/.local/opt/kypost-sqlcipher")
    "${cap[@]}" cmake -B "$build" -S "$repo" -DCMAKE_BUILD_TYPE=RelWithDebInfo "${sqlcipher[@]}"
fi
"${cap[@]}" cmake --build "$build" --target KyPostScreens -j 4

run() { # layout size flag theme root script
    local out=$shots/$label/$1-$(tr ' A-Z' '-a-z' <<<"$4")
    rm -rf "$out"
    "${cap[@]}" env QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu \
        "$build/tests/KyPostScreens" --root "$5" --size "$2" $3 \
        --theme "$4" --script "$6" --out "$out" > "$out.log" 2>&1 \
        || { echo "FAILED: $out (see $out.log)"; tail -5 "$out.log"; return 1; }
    tail -1 "$out.log"
}

mkdir -p "$shots/$label"
status=0
for theme in "Busnes Light" "Busnes Dark"; do
    run mobile 412x732 --mobile "$theme" "$mobile_root" "$mobile_script" || status=1
    run desktop 1920x1080 "" "$theme" "$desktop_root" "$desktop_script" || status=1
done

# ANDROID_REF: a folder of Android store shots (phoneScreenshots-N.png,
# sevenInchScreenshots-N.png). Unset, compare.html has no Android column.
python3 -I "$repo/scripts/screenshots-compare.py" "$shots" ${ANDROID_REF:+"$ANDROID_REF"}
echo "compare: $shots/compare.html"
exit $status
