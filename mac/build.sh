#!/bin/bash
# Builds Nightshift Drums on a Mac and runs its tests. From the folder that holds nightshift/ and
# nightshift-drums/ (the NightShift kit), after NightShift's own build has run once (it fetches
# CMake if the Mac hasn't one, and JUCE):
#
#     bash nightshift-drums/mac/build.sh            VST3 + Audio Unit into ~/Library/Audio/Plug-Ins,
#                                                   the standalone app, drumstool; then every test
#     bash nightshift-drums/mac/build.sh --no-tests
#
# What happened goes in nightshift-drums/mac/report.txt (the whole log in build.log beside it).
set -u

MAC_DIR="$(cd "$(dirname "$0")" && pwd)"
DRUMS_DIR="$(cd "$MAC_DIR/.." && pwd)"
ROOT_DIR="$(cd "$DRUMS_DIR/.." && pwd)"
REPORT="$MAC_DIR/report.txt"
LOG="$MAC_DIR/build.log"
BUILD="$DRUMS_DIR/build/mac"
TESTS=1

for arg in "$@"; do
    case "$arg" in
        --no-tests) TESTS=0 ;;
    esac
done

: > "$REPORT"
: > "$LOG"
say()  { echo "$*" | tee -a "$REPORT"; }
fail() { say "STOPPED: $*"; exit 1; }

say "Nightshift Drums, Mac build, $(date '+%Y-%m-%d %H:%M')"
say "macOS $(sw_vers -productVersion), $(uname -m)"

# The JUCE NightShift uses, and the CMake its build found or fetched
[ -f "$ROOT_DIR/nightshift/external/tracktion_engine/modules/juce/CMakeLists.txt" ] \
    || fail "JUCE isn't in nightshift/external yet: run NightShift's build (bash nightshift/mac/build.sh) once first"

if command -v cmake >/dev/null 2>&1; then
    CMAKE="$(command -v cmake)"
else
    CMAKE="$ROOT_DIR/nightshift/mac/.tools/cmake/CMake.app/Contents/bin/cmake"
fi

[ -x "$CMAKE" ] || fail "no CMake: run NightShift's build once first (it fetches one)"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

say "Building (plugin, standalone, drumstool)..."
"$CMAKE" -S "$DRUMS_DIR" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 >> "$LOG" 2>&1 \
    || fail "CMake couldn't set the build up (see build.log)"

if ! "$CMAKE" --build "$BUILD" --config Release -j "$JOBS" >> "$LOG" 2>&1; then
    say "It didn't build. Its errors:"
    grep -E "error:|ld: " "$LOG" | sort -u | head -n 60 >> "$REPORT"
    fail "the build failed"
fi

APP="$(find "$BUILD" -maxdepth 6 -name "Nightshift Drums.app" -type d | head -n 1)"
TOOL="$(find "$BUILD" -maxdepth 5 -name drumstool -type f -perm -u+x | head -n 1)"
[ -n "$APP" ] && codesign --force --deep --sign - "$APP" >> "$LOG" 2>&1
say "Built: the VST3 and Audio Unit are in ~/Library/Audio/Plug-Ins; the app is $APP"

# Audio Units new to the system: macOS lists them after its plugin cache is refreshed
killall -9 AudioComponentRegistrar >/dev/null 2>&1

if [ "$TESTS" = "1" ] && [ -n "$TOOL" ]; then
    say ""
    say "drumstool --test:"
    "$TOOL" --test >> "$REPORT" 2>&1
    say ""
    say "The window's text (Helvetica Neue), each page:"

    for page in kit mixer grooves kits; do
        "$TOOL" --snapshot "/tmp/drums-$page.png" --kits test --kit testkit --page "$page" --groove "funk" --textaudit 2>&1 | grep -E "TEXT|SMALL|CUT|CLIPPED|SQUEEZED|FAINT" >> "$REPORT"
    done
fi

say ""
say "Done. Kits: open Nightshift Drums and get them on its Kits page (they go in ~/Library/Application Support/Nightshift Drums/Kits)."
