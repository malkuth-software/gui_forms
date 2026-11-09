#!/bin/sh
# Drives gui_forms_macos_open_files_probe_app with LaunchServices (Mac handoff
# 2026-11-09, item 2d). `open -a` sends the open-documents Apple Event: first
# at launch (a.txt), then to the running application (b.txt and c.txt, then
# quit.txt, which closes it). The application logs what reached
# Application (tests/macos_open_files_probe_app.mm); this script checks the
# log: a.txt queued until the late subscriber took it, b.txt and c.txt in one
# open_files_requested event, in order, then quit.txt.
#
# Usage: run_macos_open_files_probe.sh BUNDLE OUTPUT_DIRECTORY
set -eu
bundle=$1
output=$2
inputs="$output/handoff_open_files_inputs"
rm -rf "$inputs"
mkdir -p "$inputs"
inputs=$(cd "$inputs" && pwd -P)
for name in a.txt b.txt c.txt quit.txt; do printf 'probe\n' > "$inputs/$name"; done
log="$(dirname "$bundle")/handoff_open_files/log.txt"
rm -f "$log"
executable="$bundle/Contents/MacOS/"
pkill -f "$executable" 2>/dev/null || true

open -a "$bundle" "$inputs/a.txt"
sleep 4
open -a "$bundle" "$inputs/b.txt" "$inputs/c.txt"
sleep 2
open -a "$bundle" "$inputs/quit.txt"
waited=0
while pgrep -f "$executable" >/dev/null 2>&1; do
    waited=$((waited + 1))
    if [ "$waited" -gt 40 ]; then
        echo "FAIL the probe application did not exit"
        pkill -f "$executable" || true
        break
    fi
    sleep 0.5
done

failures=0
if [ ! -f "$log" ]; then
    echo "FAIL no log at $log"
    echo "failures=1"
    exit 1
fi
echo "--- log"
cat "$log"
echo "---"
expect() {
    if grep -Fqx -- "$2" "$log"; then
        echo "PASS $1: $2"
    else
        echo "FAIL $1: expected the line '$2'"
        failures=$((failures + 1))
    fi
}
expect "launch request queued for the late subscriber" "queued: $inputs/a.txt"
expect "request during the run raised open_files_requested, in order" "event: $inputs/b.txt $inputs/c.txt"
expect "quit request during the run" "event: $inputs/quit.txt"
expect "the window closed" "close: accepted"
expect "the run ended" "run: accepted"
echo "failures=$failures"
[ "$failures" -eq 0 ]
