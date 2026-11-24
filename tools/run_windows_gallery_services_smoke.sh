#!/bin/sh
set -eu

build_directory=${1:-"$(pwd)/build-windows-x64"}
wine_binary=${WINE_BINARY:-/opt/homebrew/bin/wine}
gallery="$build_directory/GUI.Forms Gallery.exe"
script="$build_directory/wine-gallery-services-script.jsonl"
log="$build_directory/wine-gallery-services-smoke.jsonl"

test -x "$wine_binary"
test -f "$gallery"

# The Gallery runs an automation script (the library's MCP automation,
# --mcp-script). The owned native dialogs the retired channel dismissed are
# not driven: a model never answers an operating system dialog (study 9.2),
# so the script asks for none.
press() {
    echo "{\"tool\":\"act\",\"action\":\"press\",\"target\":\"$1\"}"
}
dispatcher() {
    echo '{"tool":"inspect","what":["dispatcher"]}'
}
{
    echo '{"tool":"wait","until":"idle","timeout_ms":20000}'
    press gallery.page.animation-laboratory
    press gallery.animation.reduced
    echo '{"tool":"snapshot","limit":50}'
    press gallery.animation.pause
    echo '{"tool":"snapshot","limit":50}'
    press gallery.animation.pause
    echo '{"tool":"snapshot","limit":50}'
    press gallery.animation.reduced
    echo '{"tool":"snapshot","limit":50}'
    press gallery.page.states-and-diagnostics
    press gallery.dispatcher.post
    echo '{"tool":"wait","until":"ms","ms":200}'
    dispatcher
    press gallery.dispatcher.cancel
    echo '{"tool":"wait","until":"ms","ms":100}'
    dispatcher
    press gallery.dispatcher.invoke
    echo '{"tool":"wait","until":"ms","ms":200}'
    dispatcher
    press gallery.page.dialogs-and-host-services
    press gallery.host.service.2
    press gallery.host.service.3
    press gallery.page.images-and-drawing
    echo '{"tool":"capture"}'
    echo '{"tool":"snapshot","limit":50}'
} >"$script"
WINEDEBUG=-all "$wine_binary" "$gallery" --mcp-script "$script" >"$log" 2>&1

for stable_id in \
    gallery.page.dialogs-and-host-services \
    gallery.page.animation-laboratory \
    gallery.page.images-and-drawing \
    gallery.animation.reduced \
    gallery.animation.pause \
    gallery.dispatcher.post \
    gallery.dispatcher.cancel \
    gallery.dispatcher.invoke \
    gallery.host.service.2 \
    gallery.host.service.3
do
    rg -Fq "ok: press [$stable_id]" "$log"
done
# Animation runs on control-owned UI timers (ADR-016); the snapshots carry
# no lease count, so the run only has to have taken them.
test "$(rg -c '^step [0-9]+ snapshot: ' "$log")" -ge 3
test "$(rg -c '^step [0-9]+ inspect: ' "$log")" -eq 3
rg -q '"platform":"win32-dib"' "$log"
rg -q '"monitor_geometry"' "$log"
rg -q '"clipboard"' "$log"
rg -q '"sound_cues"' "$log"
rg -q '"monitor_queries":1' "$log"
rg -q '"sound_requests":1' "$log"
rg -q '"sound_playbacks":1' "$log"
rg -q '"modal_depth":0' "$log"
rg -q '"events_rejected":0' "$log"
test -s "$script.1.png"
rg -q '"close_requests":1' "$log"
rg -q '"closed":true' "$log"
rg -q '"shutdown":true' "$log"

echo "Wine Gallery host-services smoke passed: $log"
