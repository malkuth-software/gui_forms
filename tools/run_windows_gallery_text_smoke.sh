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
log="$evidence_directory/wine-gallery-text-smoke.jsonl"
capture="$evidence_directory/gallery-automation.bmp"

test -x "$wine_binary"
test -f "$gallery"
test -f "$probe"
text_tests="$build_directory/gui_forms_windows_text_tests.exe"
test -f "$text_tests"
font_directory=${GUI_FORMS_FONT_DIR:-"$build_directory/fonts"}
# The shared-engine test replaces the removed GDI trace assertions: it checks
# script coverage, cached/cold pixels, weights, scales, and identity hashes.
(
    cd "$evidence_directory"
    WINEDEBUG=-all "$wine_binary" "$text_tests" "$font_directory"
) >"$evidence_directory/windows-text-tests.log" 2>&1
test "$(rg -c '^text-identity ' "$evidence_directory/windows-text-tests.log")" -eq 12

WINEDEBUG=-all \
    "$wine_binary" "$gallery" --automation >"$log" 2>&1 &
gallery_pid=$!
cleanup() {
    kill "$gallery_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

attempt=0
until WINEDEBUG=-all "$wine_binary" "$probe" snapshot >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    if test "$attempt" -ge 120; then
        echo "Gallery automation window did not appear" >&2
        exit 1
    fi
    sleep 0.1
done

# The former sixth board page ("Text & Input") is the Gallery page text-and-input.
WINEDEBUG=-all "$wine_binary" "$probe" activate gallery.page.text-and-input >/dev/null
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
WINEDEBUG=-all "$wine_binary" "$probe" close >/dev/null
wait "$gallery_pid"
trap - EXIT INT TERM

test -s "$capture"
rg -q '"renderer_name":"Win32 DIB · Skia CPU raster · HarfBuzz/FreeType text · owned PNG · bundled fonts"' "$log"
rg -q '"events_rejected":0' "$log"
rg -q '"close_requests":1' "$log"
rg -q '"closed":true' "$log"
rg -q '"shutdown":true' "$log"

echo "Wine Gallery text fallback smoke passed: $log"
