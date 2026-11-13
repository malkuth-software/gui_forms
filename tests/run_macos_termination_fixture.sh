#!/bin/sh
# Drives gui_forms_macos_termination_fixture through Cmd-Q (cp29 row 14;
# cp33): close requests asked in order, a refusal honoured, no lost state.
#
# By default Cmd-Q is typed inside the fixture: the script creates
# /tmp/gf_term_cmd_q and the fixture posts Command-Q to its own event queue,
# which goes through the Quit menu item and -[NSApplication terminate:].
# No Automation or Accessibility grant is needed, so the instrumentation
# host runs it with no per-application permission. With DRIVER=apple-event
# the quit Apple Event is sent with osascript instead, which needs the
# Automation grant for the caller.
#
# Three quits:
#   1. /tmp/gf_term_secondary_refuse present: the secondary window refuses,
#      the primary is not asked, the process keeps running;
#   2. /tmp/gf_term_refuse present instead: the secondary accepts (and
#      saves), the primary refuses, the process keeps running;
#   3. no flag: both accept in order, the primary closes, Application::run
#      returns (the host cancels AppKit's termination for a plain quit), the
#      fixture finds both windows' saved state and exits with status 0.
# Logout and restart (reason session_end) are checked by hand
# (tests/macos_termination_fixture.mm).
#
# Usage: run_macos_termination_fixture.sh BUNDLE
set -eu
bundle=$1
driver=${DRIVER:-cmd-q}
identifier=local.guiforms.termination-fixture
log=/tmp/gf_term.log
executable="$bundle/Contents/MacOS/"
pkill -f "$executable" 2>/dev/null || true
rm -f "$log" /tmp/gf_term_refuse /tmp/gf_term_cmd_q
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

# The number of lines in the log holding $1.
count_lines() {
    grep -Fc -- "$1" "$log" 2>/dev/null || true
}

failures=0
fail() {
    echo "FAIL $1"
    failures=$((failures + 1))
}

quit() {
    case "$driver" in
        apple-event)
            # A refused quit answers the event with userCanceledErr, and so
            # does an accepted one now (the host cancels AppKit's
            # termination after the run ends); osascript then exits 1.
            quit_output=$(osascript -e "tell application id \"$identifier\" to quit" 2>&1) || true
            case "$quit_output" in
                *"Not authorized"*|*"-1743"*) fail "osascript lacks the Automation grant: $quit_output" ;;
            esac
            ;;
        *)
            posted_before=$(count_lines "cmd-q posted")
            touch /tmp/gf_term_cmd_q
            waited=0
            while [ "$(count_lines "cmd-q posted")" -le "$posted_before" ]; do
                waited=$((waited + 1))
                if [ "$waited" -gt 40 ]; then
                    fail "the fixture did not post Cmd-Q"
                    break
                fi
                sleep 0.25
            done
            ;;
    esac
}

open -n -a "$bundle"
wait_for_line "main ready" || fail "the primary window was not ready"
wait_for_line "secondary ready" || fail "the secondary window was not ready"

quit
wait_for_line "secondary refused" || fail "the secondary window did not refuse the first quit"
sleep 1
pgrep -f "$executable" >/dev/null 2>&1 || fail "a refused quit ended the process"

rm -f /tmp/gf_term_secondary_refuse
touch /tmp/gf_term_refuse
quit
wait_for_line "main refused" || fail "the primary window did not refuse the second quit"
sleep 1
pgrep -f "$executable" >/dev/null 2>&1 || fail "a quit the primary refused ended the process"

rm -f /tmp/gf_term_refuse
quit
waited=0
while pgrep -f "$executable" >/dev/null 2>&1; do
    waited=$((waited + 1))
    if [ "$waited" -gt 40 ]; then
        fail "the fixture did not exit after the third quit"
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
# closed lines (the run's end may also close the secondary window) only
# need to be there.
events=$(sed 's/^[^ ]* [^ ]* pid=[0-9]* //' "$log")
asked=$(printf '%s\n' "$events" | grep -E '^(main|secondary) (closing asked|refused|accepted)' || true)
expected="secondary closing asked reason=quit
secondary refused (flag file present)
secondary closing asked reason=quit
secondary accepted
main closing asked reason=quit
main refused (flag file present)
secondary closing asked reason=quit
secondary accepted
main closing asked reason=quit
main accepted"
if [ "$asked" = "$expected" ]; then
    echo "PASS the quits asked the windows in order and honoured each refusal"
else
    fail "the closing questions differ from the expected sequence:
$expected"
fi
# The run returned (the program's code after the run applies), each
# window's state was saved by the accepted quit, and main exited 0.
for line in "main closed" "run returned accepted" "state main=saved reason=quit" \
    "state secondary=saved reason=quit" "exit code 0" "process exit"; do
    if printf '%s\n' "$events" | grep -Fqx -- "$line"; then
        echo "PASS $line"
    else
        fail "expected the line '$line'"
    fi
done
if printf '%s\n' "$events" | grep -Fq -- "state not saved"; then
    fail "a window's state was not saved"
fi
echo "failures=$failures"
[ "$failures" -eq 0 ]
