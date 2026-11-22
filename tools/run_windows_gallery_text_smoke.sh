#!/bin/sh
set -eu

build_directory=${1:-"$(pwd)/build-windows-x64"}
build_directory=$(CDPATH= cd -- "$build_directory" && pwd)
wine_binary=${WINE_BINARY:-/opt/homebrew/bin/wine}
gallery="$build_directory/GUI.Forms Gallery.exe"
script="$evidence_directory/wine-gallery-text-script.jsonl"
evidence_directory=${GUI_FORMS_EVIDENCE_DIR:-"$build_directory"}
mkdir -p "$evidence_directory"
evidence_directory=$(CDPATH= cd -- "$evidence_directory" && pwd)
log="$evidence_directory/wine-gallery-text-smoke.jsonl"
capture="$script.1.png"

test -x "$wine_binary"
test -f "$gallery"
text_tests="$build_directory/gui_forms_windows_text_tests.exe"
test -f "$text_tests"
# The text tests read the faces from a directory; programs carry only the
# licences (the faces are in the embedded pack), so the checkout's own.
repository_directory=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
font_directory=${GUI_FORMS_FONT_DIR:-"$repository_directory/assets/fonts"}
# The shared-engine test replaces the removed GDI trace assertions: it checks
# script coverage, cached/cold pixels, weights, scales, and identity hashes.
(
    cd "$evidence_directory"
    WINEDEBUG=-all "$wine_binary" "$text_tests" "$font_directory"
) >"$evidence_directory/windows-text-tests.log" 2>&1
test "$(rg -c '^text-identity ' "$evidence_directory/windows-text-tests.log")" -eq 12

# The Gallery runs an automation script (--mcp-script): the former sixth
# board page ("Text & Input") is the Gallery page text-and-input; the script
# opens it, waits for its frame, captures the window and closes it.
{
    echo '{"tool":"wait","until":"idle","timeout_ms":20000}'
    echo '{"tool":"act","action":"press","target":"gallery.page.text-and-input"}'
    echo '{"tool":"capture"}'
} >"$script"
WINEDEBUG=-all "$wine_binary" "$gallery" --mcp-script "$script" >"$log" 2>&1

rg -q '^ok: 3 steps' "$log"
test -s "$capture"
rg -q '"renderer_name":"Win32 DIB · Skia CPU raster · STXlib text · owned PNG · bundled fonts"' "$log"
rg -q '"events_rejected":0' "$log"
rg -q '"close_requests":1' "$log"
rg -q '"closed":true' "$log"
rg -q '"shutdown":true' "$log"

echo "Wine Gallery text fallback smoke passed: $log"
