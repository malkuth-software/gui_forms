#!/bin/sh
set -eu

build_directory=${1:-"$(pwd)/build-windows-x64"}
wine_binary=${WINE_BINARY:-/opt/homebrew/bin/wine}
gallery="$build_directory/GUI.Forms Gallery.exe"
probe="$build_directory/gui_forms_windows_probe.exe"
log="$build_directory/wine-gallery-services-smoke.jsonl"

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
    if test "$attempt" -ge 120; then
        echo "Gallery automation window did not appear" >&2
        exit 1
    fi
    sleep 0.1
done

for stable_id in \
    gallery.page.animation-laboratory \
    gallery.animation.reduced
do
    WINEDEBUG=-all "$wine_binary" "$probe" activate "$stable_id" >/dev/null
done
WINEDEBUG=-all "$wine_binary" "$probe" snapshot >/dev/null
WINEDEBUG=-all "$wine_binary" "$probe" activate gallery.animation.pause >/dev/null
WINEDEBUG=-all "$wine_binary" "$probe" snapshot >/dev/null
WINEDEBUG=-all "$wine_binary" "$probe" activate gallery.animation.pause >/dev/null
WINEDEBUG=-all "$wine_binary" "$probe" snapshot >/dev/null
WINEDEBUG=-all "$wine_binary" "$probe" activate gallery.animation.reduced >/dev/null
WINEDEBUG=-all "$wine_binary" "$probe" snapshot >/dev/null

WINEDEBUG=-all "$wine_binary" "$probe" activate gallery.page.states-and-diagnostics >/dev/null
WINEDEBUG=-all "$wine_binary" "$probe" activate gallery.dispatcher.post >/dev/null
sleep 0.2
WINEDEBUG=-all "$wine_binary" "$probe" snapshot >/dev/null
WINEDEBUG=-all "$wine_binary" "$probe" activate gallery.dispatcher.cancel >/dev/null
sleep 0.1
WINEDEBUG=-all "$wine_binary" "$probe" snapshot >/dev/null
WINEDEBUG=-all "$wine_binary" "$probe" activate gallery.dispatcher.invoke >/dev/null
sleep 0.2
WINEDEBUG=-all "$wine_binary" "$probe" snapshot >/dev/null

for stable_id in \
    gallery.page.dialogs-and-host-services \
    gallery.host.service.2 \
    gallery.host.service.3
do
    WINEDEBUG=-all "$wine_binary" "$probe" activate "$stable_id" >/dev/null
done

dismiss_native_dialog() {
    stable_id=$1
    WINEDEBUG=-all "$wine_binary" "$probe" activate "$stable_id" >/dev/null 2>&1 &
    activation_pid=$!
    attempt=0
    until WINEDEBUG=-all "$wine_binary" "$probe" dismiss-dialog >/dev/null 2>&1; do
        attempt=$((attempt + 1))
        if test "$attempt" -ge 50; then
            kill "$activation_pid" 2>/dev/null || true
            echo "Owned native dialog did not appear for $stable_id" >&2
            return 1
        fi
        sleep 0.1
    done
    wait "$activation_pid"
}

dismiss_native_dialog gallery.host.message.1
dismiss_native_dialog gallery.host.dialog.4

WINEDEBUG=-all "$wine_binary" "$probe" activate gallery.page.images-and-drawing >/dev/null
WINEDEBUG=-all "$wine_binary" "$probe" capture >/dev/null
WINEDEBUG=-all "$wine_binary" "$probe" snapshot >/dev/null
WINEDEBUG=-all "$wine_binary" "$probe" close >/dev/null
wait "$gallery_pid"
trap - EXIT INT TERM

rg -q '"automation":"activate","id":"gallery.page.dialogs-and-host-services"' "$log"
rg -q '"automation":"activate","id":"gallery.page.animation-laboratory"' "$log"
rg -q '"automation":"activate","id":"gallery.page.images-and-drawing"' "$log"
rg -q '"automation":"activate","id":"gallery.animation.reduced"' "$log"
rg -q '"automation":"activate","id":"gallery.animation.pause"' "$log"
# Animation runs on control-owned UI timers (ADR-016); the snapshots carry
# no lease count, so the run only has to have taken them.
motion_snapshots=$(rg -c '"automation":"snapshot"' "$log")
test "$motion_snapshots" -ge 3
rg -q '"automation":"activate","id":"gallery.dispatcher.post"' "$log"
rg -q '"automation":"activate","id":"gallery.dispatcher.cancel"' "$log"
rg -q '"automation":"activate","id":"gallery.dispatcher.invoke"' "$log"
rg -Fq '"dispatcher":{"posted":4,"invoked":4,"cancelled":0,"faulted":0,"synchronous_invocations":0,"inline_invocations":0,"marshalled_invocations":0,"pending":0,"maximum_pending":3,"accepting":true}' "$log"
rg -Fq '"dispatcher":{"posted":5,"invoked":4,"cancelled":1,"faulted":0,"synchronous_invocations":0,"inline_invocations":0,"marshalled_invocations":0,"pending":0,"maximum_pending":3,"accepting":true}' "$log"
rg -Fq '"dispatcher":{"posted":6,"invoked":5,"cancelled":1,"faulted":0,"synchronous_invocations":1,"inline_invocations":0,"marshalled_invocations":1,"pending":0,"maximum_pending":3,"accepting":true}' "$log"
rg -q '"automation":"activate","id":"gallery.host.service.2"' "$log"
rg -q '"automation":"activate","id":"gallery.host.service.3"' "$log"
rg -q '"platform":"win32-dib"' "$log"
rg -q '"monitor_geometry"' "$log"
rg -q '"clipboard"' "$log"
rg -q '"dialogs"' "$log"
rg -q '"sound_cues"' "$log"
rg -q '"monitor_queries":1' "$log"
rg -q '"sound_requests":1' "$log"
rg -q '"sound_playbacks":1' "$log"
rg -q '"dialog_requests":2' "$log"
rg -q '"dialog_completions":2' "$log"
rg -q '"dialog_cancellations":2' "$log"
rg -q '"maximum_modal_depth":1' "$log"
rg -q '"modal_depth":0' "$log"
rg -q '"events_rejected":0' "$log"
rg -q '"automation":"capture","saved":true' "$log"
test -s "$build_directory/gallery-automation.bmp"
rg -q '"close_requests":1' "$log"
rg -q '"closed":true' "$log"
rg -q '"shutdown":true' "$log"

echo "Wine Gallery host-services smoke passed: $log"
