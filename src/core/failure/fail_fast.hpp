#pragma once

#include <exception>
#include <string_view>
#include <utility>

// The fail-fast boundary (owner, 2026-11-11). Where foreign code calls
// GUI.Forms (a window procedure, a COM or AT-SPI method, an AppKit delegate)
// or where housekeeping calls application code (a UI timer, a motion
// listener, a host event's callbacks), the call goes through
// call_failing_fast. An exception reaching it is a real problem: the site,
// the exception's text, the shadow frames it was thrown under and a stack
// (where the platform gives one) are written to stderr, and the process ends
// with std::abort. The thread's failure snapshot (failure_snapshot.hpp) is
// taken first, so a dump of the process holds it too. Nothing is counted
// and nothing continues; no exception unwinds into foreign frames.
//
// Housekeeping itself (the scheduler wake, paint ticks, the motion clock,
// damage collection, live-surface drains) does not catch: it is no-throw by
// construction, and its anomalies are counted as missed expectations
// (Metrics::record_expectation_miss).
//
// The target is named: a free function with its arguments, or a member
// function with its object and arguments. `site` names the entry as the
// report prints it, "<host>.<entry>" (for example
// "windows.window_procedure"); it must outlive the call.
//
// tools/check_native_boundaries.py checks that every native callback entry
// goes through call_failing_fast.

namespace gui_forms::detail {

// Takes the thread's failure snapshot, writes the report for `failure` at
// `site` and ends the process. Called from the catch block, before anything
// opens a shadow frame. Never returns and never throws.
[[noreturn]] void fail_fast(std::string_view site, std::exception_ptr failure) noexcept;

// The report's parts, shared with the borrow sanitizer's report
// (detail::fail_borrow, gui_forms/borrow/borrowed_view/borrowed_view.hpp):
// the current stack, one line per frame, and one code address as its module
// and offset (and symbol, where the platform names one), so a symbolizer
// can name the function after the fact. Both write to stderr only.
void write_fail_fast_stack() noexcept;
void write_code_address(const void* address) noexcept;

// Calls `target(arguments...)` and returns its result; an exception ends the
// process through fail_fast.
template <typename Result, typename... Parameters, typename... Arguments>
Result call_failing_fast(std::string_view site, Result (*target)(Parameters...), Arguments&&... arguments) noexcept {
    try {
        return target(std::forward<Arguments>(arguments)...);
    } catch (...) {
        fail_fast(site, std::current_exception());
    }
}

// Calls `(owner.*target)(arguments...)` and returns its result; an exception
// ends the process through fail_fast.
template <typename Result, typename Owner, typename... Parameters, typename... Arguments>
Result call_failing_fast(std::string_view site, Result (Owner::*target)(Parameters...), Owner& owner,
                         Arguments&&... arguments) noexcept {
    try {
        return (owner.*target)(std::forward<Arguments>(arguments)...);
    } catch (...) {
        fail_fast(site, std::current_exception());
    }
}

} // namespace gui_forms::detail
