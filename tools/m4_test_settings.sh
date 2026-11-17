#!/bin/sh
# The macOS instrumentation host's settings for the scripted Mac checks
# (cp33: Cmd-Q, live resize, scroll phases, normative traces). Run on the
# M4 in the console user's session, not as root:
#
#   sh tools/m4_test_settings.sh apply    # write the settings, saving the old values
#   sh tools/m4_test_settings.sh check    # print each setting; exit 1 if any differs
#   sh tools/m4_test_settings.sh revert   # restore the saved values
#
# Only user defaults are written: no permission (TCC) grant, no sudo, no
# per-application privacy entry. The checks inject their input inside
# their own process (NSApp postEvent, XSendEvent), which needs no grant.
#
# Settings:
#   per test bundle (local.guiforms.*): no App Nap, so a window behind
#     another keeps its timers and draws; no window restoration, so every
#     launch starts from the program's own state;
#   global: no window open/close animations (the resize probe's frames
#     then follow the posted steps); no press-and-hold accent popup (a held
#     key repeats, as the key tests expect); no screen saver while a run is
#     in progress.
# Saved values live in ~/Library/Application Support/gui_forms_test_settings.

set -eu

bundles="local.guiforms.gallery local.guiforms.live-resize-probe local.guiforms.termination-fixture local.guiforms.normative-trace"
saved_dir="$HOME/Library/Application Support/gui_forms_test_settings"
saved_file="$saved_dir/saved.txt"

# One global setting per line: domain, key, type, value. "currentHost" as
# the domain prefix writes the by-host domain.
globals() {
    cat <<'EOF'
NSGlobalDomain NSAutomaticWindowAnimationsEnabled bool false
NSGlobalDomain ApplePressAndHoldEnabled bool false
currentHost:com.apple.screensaver idleTime int 0
EOF
}

# Reads a default; prints "(unset)" when there is none.
read_default() {
    domain=$1
    key=$2
    case $domain in
    currentHost:*) defaults -currentHost read "${domain#currentHost:}" "$key" 2>/dev/null || echo "(unset)" ;;
    *) defaults read "$domain" "$key" 2>/dev/null || echo "(unset)" ;;
    esac
}

write_default() {
    domain=$1
    key=$2
    kind=$3
    value=$4
    case $domain in
    currentHost:*) defaults -currentHost write "${domain#currentHost:}" "$key" "-$kind" "$value" ;;
    *) defaults write "$domain" "$key" "-$kind" "$value" ;;
    esac
}

delete_default() {
    domain=$1
    key=$2
    case $domain in
    currentHost:*) defaults -currentHost delete "${domain#currentHost:}" "$key" 2>/dev/null || true ;;
    *) defaults delete "$domain" "$key" 2>/dev/null || true ;;
    esac
}

# The expected value as `defaults read` prints it.
printed() {
    kind=$1
    value=$2
    case $kind:$value in
    bool:true) echo 1 ;;
    bool:false) echo 0 ;;
    *) echo "$value" ;;
    esac
}

# Every setting, one per line: domain key type value.
settings() {
    for bundle in $bundles; do
        echo "$bundle NSAppSleepDisabled bool true"
        echo "$bundle ApplePersistenceIgnoreState bool true"
        echo "$bundle NSQuitAlwaysKeepsWindows bool false"
    done
    globals
}

apply() {
    mkdir -p "$saved_dir"
    if [ ! -f "$saved_file" ]; then
        settings | while read -r domain key kind value; do
            echo "$domain $key $kind $(read_default "$domain" "$key")"
        done >"$saved_file"
    fi
    settings | while read -r domain key kind value; do
        write_default "$domain" "$key" "$kind" "$value"
    done
    echo "m4_test_settings: applied (old values in $saved_file)"
}

check() {
    differs=0
    settings >"${TMPDIR:-/tmp}/gf_test_settings.$$"
    while read -r domain key kind value; do
        want=$(printed "$kind" "$value")
        got=$(read_default "$domain" "$key")
        state=ok
        if [ "$want" != "$got" ]; then
            state=DIFFERS
            differs=1
        fi
        echo "$state $domain $key = $got (want $want)"
    done <"${TMPDIR:-/tmp}/gf_test_settings.$$"
    rm -f "${TMPDIR:-/tmp}/gf_test_settings.$$"
    return $differs
}

revert() {
    if [ ! -f "$saved_file" ]; then
        echo "m4_test_settings: nothing saved; nothing to revert"
        return 0
    fi
    while read -r domain key kind old; do
        if [ "$old" = "(unset)" ]; then
            delete_default "$domain" "$key"
        else
            case $kind:$old in
            bool:1) write_default "$domain" "$key" bool true ;;
            bool:0) write_default "$domain" "$key" bool false ;;
            *) write_default "$domain" "$key" "$kind" "$old" ;;
            esac
        fi
    done <"$saved_file"
    rm -f "$saved_file"
    echo "m4_test_settings: reverted"
}

case ${1:-} in
apply) apply ;;
check) check ;;
revert) revert ;;
*)
    echo "usage: sh tools/m4_test_settings.sh apply|check|revert" >&2
    exit 2
    ;;
esac
