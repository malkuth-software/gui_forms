#!/bin/sh
# Drives gui_forms_macos_termination_fixture through the Cmd-Q path (cp29
# row 14): the quit Apple Event, which Cmd-Q sends too, without a session
# reason. With /tmp/gf_term_secondary_refuse present, the secondary window
# refuses: the quit is cancelled before the primary window is asked, and
# the process keeps running. Without it, both windows accept, the
# secondary first, and the process exits. Logout and restart (reason
# session_end) are checked by hand (tests/macos_termination_fixture.mm).
#
# osascript sends the event; on a host where the caller lacks the
# Automation grant for it, the first send fails and the script reports so.
#
# Usage: run_macos_termination_fixture.sh BUNDLE
set -eu
bundle=$1
identifier=local.guiforms.termination-fixture
log=/tmp/gf_term.log
executable="$bundle/Contents/MacOS/"
pkill -f "$executable" 2>/dev/null || true
rm -f "$log" /tmp/gf_term_refuse
touch /tmp/gf_term_secondary_refuse

wait_for_line() {
    waited=0
    while ! grep -Fq -- "$1" "$log" 2>/dev/null; do
        waited=$((waited + 1))
        if [ "$waited" -gt 40 ]; then return 1; fi
        sleep 0.5
    done
    return 0
}

failures=0
fail() {
    echo "FAIL $1"
    failures=$((failures + 1))
}

open -n -a "$bundle"
wait_for_line "main ready" || fail "the primary window was not ready"
wait_for_line "secondary ready" || fail "the secondary window was not ready"

# A refused quit answers the event with userCanceledErr; osascript exits 1.
quit_output=$(osascript -e "tell application id \"$identifier\" to quit" 2>&1) || true
case "$quit_output" in
    *"Not authorized"*|*"-1743"*) fail "osascript lacks the Automation grant: $quit_output" ;;
esac
wait_for_line "secondary refused" || fail "the secondary window did not refuse the first quit"
sleep 1
pgrep -f "$executable" >/dev/null 2>&1 || fail "a refused quit ended the process"

rm -f /tmp/gf_term_secondary_refuse
osascript -e "tell application id \"$identifier\" to quit" >/dev/null 2>&1 || true
waited=0
while pgrep -f "$executable" >/dev/null 2>&1; do
    waited=$((waited + 1))
    if [ "$waited" -gt 40 ]; then
        fail "the fixture did not exit after the second quit"
        pkill -f "$executable" || true
        break
    fi
    sleep 0.5
done

if [ ! -f "$log" ]; then
    echo "FAIL no log at $log"
    echo "failures=$((failures + 1))"
    exit 1
fi
echo "--- log"
cat "$log"
echo "---"
# The log's lines carry a timestamp and pid; compare what follows them.
# The closing questions and answers must come in exactly this order; the
# closed lines (the run's end may also close the secondary window) and the
# process exit only need to be there.
events=$(sed 's/^[^ ]* [^ ]* pid=[0-9]* //' "$log")
asked=$(printf '%s\n' "$events" | grep -E '^(main|secondary) (closing asked|refused|accepted)' || true)
expected="secondary closing asked reason=quit
secondary refused (flag file present)
secondary closing asked reason=quit
secondary accepted
main closing asked reason=quit
main accepted"
if [ "$asked" = "$expected" ]; then
    echo "PASS the secondary window refused the quit first, then both accepted in order"
else
    fail "the closing questions differ from the expected sequence:
$expected"
fi
for line in "main closed" "process exit"; do
    if printf '%s\n' "$events" | grep -Fqx -- "$line"; then
        echo "PASS $line"
    else
        fail "expected the line '$line'"
    fi
done
echo "failures=$failures"
[ "$failures" -eq 0 ]
