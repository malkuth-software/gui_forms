#!/bin/sh
set -eu

build_directory=${1:-"$(pwd)/build-windows-x64"}
build_directory=$(CDPATH= cd -- "$build_directory" && pwd)
wine_binary=${WINE_BINARY:-/opt/homebrew/bin/wine}
gallery="$build_directory/GUI.Forms Gallery.exe"
probe="$build_directory/gui_forms_windows_probe.exe"
evidence_directory=${GUI_FORMS_EVIDENCE_DIR:-"$build_directory"}
mkdir -p "$evidence_directory"
evidence_directory=$(CDPATH= cd -- "$evidence_directory" && pwd)
log="$evidence_directory/wine-gallery-smoke.jsonl"

test -x "$wine_binary"
test -f "$gallery"
test -f "$probe"

WINEDEBUG=-all "$wine_binary" "$gallery" --automation >"$log" 2>&1 &
gallery_pid=$!
cleanup() {
    kill "$gallery_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

attempt=0
until WINEDEBUG=-all "$wine_binary" "$probe" snapshot >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    if test "$attempt" -ge 100; then
        echo "Gallery automation window did not appear" >&2
        exit 1
    fi
    sleep 0.1
done

# Pages are registry-driven; a click on a navigation row ("gallery.page.<id>")
# selects the page whose form-document controls the next clicks address. The last click
# goes to the collection list box itself: its rows are items, not controls,
# so no row has an automation target of its own.
for stable_id in \
    gallery.page.basics \
    gallery.checkbox \
    gallery.radio.quiet \
    gallery.page.values \
    gallery.page.retained-rows \
    gallery.collection
do
    WINEDEBUG=-all "$wine_binary" "$probe" click "$stable_id" >/dev/null
done
# An automation endpoint can exist before the first transactional frame commits.
attempt=0
until WINEDEBUG=-all "$wine_binary" "$probe" capture "$evidence_directory/gallery-automation.bmp" >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    if test "$attempt" -ge 50; then
        echo "No committed frame was available for capture" >&2
        exit 1
    fi
    sleep 0.1
done
WINEDEBUG=-all "$wine_binary" "$probe" snapshot >/dev/null
WINEDEBUG=-all "$wine_binary" "$probe" close >/dev/null
wait "$gallery_pid"
trap - EXIT INT TERM

test "$(rg -c '"automation":"click".*"handled":true' "$log")" -eq 6
rg -q '"automation":"capture","saved":true' "$log"
rg -q '"renderer_name":"Win32 DIB · Skia CPU raster · HarfBuzz/FreeType text · owned PNG · bundled fonts"' "$log"
rg -q '"events_rejected":0' "$log"
rg -q '"close_requests":1' "$log"
rg -q '"closed":true' "$log"
rg -q '"shutdown":true' "$log"
test -s "$evidence_directory/gallery-automation.bmp"

echo "Wine Gallery smoke passed: $log"
