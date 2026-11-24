#!/bin/sh
set -eu

build_directory=${1:-"$(pwd)/build-windows-x64"}
build_directory=$(CDPATH= cd -- "$build_directory" && pwd)
wine_binary=${WINE_BINARY:-/opt/homebrew/bin/wine}
gallery="$build_directory/GUI.Forms Gallery.exe"
evidence_directory=${GUI_FORMS_EVIDENCE_DIR:-"$build_directory"}
mkdir -p "$evidence_directory"
evidence_directory=$(CDPATH= cd -- "$evidence_directory" && pwd)
script="$evidence_directory/wine-gallery-smoke-script.jsonl"
log="$evidence_directory/wine-gallery-smoke.jsonl"

test -x "$wine_binary"
test -f "$gallery"

# The Gallery runs an automation script (the library's MCP automation,
# --mcp-script): it waits for the first frame, clicks three page rows and
# three controls by stable ID, captures the window and closes it.
# Pages are registry-driven; a click on a navigation row ("gallery.page.<id>")
# selects the page whose form-document controls the next clicks address. The
# last click goes to the collection list box itself: its rows are items, not
# controls.
{
    echo '{"tool":"wait","until":"idle","timeout_ms":20000}'
    for stable_id in \
        gallery.page.basics \
        gallery.checkbox \
        gallery.radio.quiet \
        gallery.page.values \
        gallery.page.retained-rows \
        gallery.collection
    do
        echo "{\"tool\":\"pointer\",\"action\":\"click\",\"target\":\"$stable_id\"}"
    done
    echo '{"tool":"capture"}'
} >"$script"
WINEDEBUG=-all "$wine_binary" "$gallery" --mcp-script "$script" >"$log" 2>&1

test "$(rg -c '^step [0-9]+ pointer: ok: click' "$log")" -eq 6
rg -q '^ok: 8 steps' "$log"
rg -q '"renderer_name":"Win32 DIB · Skia CPU raster · STXlib text · owned PNG · bundled fonts"' "$log"
rg -q '"events_rejected":0' "$log"
rg -q '"close_requests":1' "$log"
rg -q '"closed":true' "$log"
rg -q '"shutdown":true' "$log"
test -s "$script.1.png"

echo "Wine Gallery smoke passed: $log"
