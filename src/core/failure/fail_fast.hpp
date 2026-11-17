#pragma once

#include "gui_forms/export/application_export.hpp"

#include <stxlib/error.hpp>
#include <stxlib/gate.hpp>

#include <string_view>

// The fail-fast boundary (owner, 2026-11-11; STXlib swap-in A2-3). Where
// foreign code calls GUI.Forms (a window procedure, a COM or AT-SPI method,
// an AppKit delegate) or where housekeeping calls application code (a UI
// timer, a motion listener, a host event's callbacks), the call goes through
// call_failing_fast, which is STXlib's fail-fast gate
// (stxlib::invoke_callback_classified). An exception reaching it is a real
// problem: STXlib records it (kind, the whole message, in a development
// build the exception's type and the native trace at the throw, and the
// shadow frames it was thrown under), and GUI.Forms' reporter writes the
// whole record to standard error through STXlib's console, after taking the
// thread's failure snapshot (failure_snapshot.hpp) so a dump of the process
// holds it too; STXlib then ends the process. Nothing is counted and nothing
// continues; no exception unwinds into foreign frames.
//
// Housekeeping itself (the scheduler wake, paint ticks, the motion clock,
// damage collection, live-surface drains) does not catch: it is no-throw by
// construction, and its anomalies are counted as missed expectations
// (Metrics::record_expectation_miss).
//
// The target is named: a free function with its arguments, or a member
// function with its object and arguments. `site` names the entry as the
// report prints it, "<host>.<entry>" (for example
// "windows.window_procedure"): an stxlib::Operation, so it is a string
// literal, checked when it is compiled. The gate's frame is a gate frame
// named by the site (ShadowFrameKind::gate).
//
// tools/check_native_boundaries.py checks that every native callback entry
// goes through call_failing_fast.

namespace gui_forms::detail {

// Installs GUI.Forms' reporter as the process's STXlib fail-fast reporter,
// once; later calls do nothing. Every fail-fast gate below calls it first, so
// the reporter is in place before any of them can fail fast. When another
// reporter was installed first, it stays and reports. Any thread.
GUI_FORMS_API void ensure_fail_fast_reporter() noexcept;

// The fail-fast gates' classifier: it keeps STXlib's standard classification
// and copies the exception's whole what() (failure_snapshot_message_capacity
// bytes) into the calling thread's fail-fast message, which the reporter
// prints in place of the record's 200 bytes.
extern GUI_FORMS_API const stxlib::ExceptionClassifier fail_fast_classifier;

// Fails fast at site with message, without an exception: an internal
// invariant a caller found broken (a reserved capacity missing). The record
// is internal_failure with the whole message; the report and the end are as
// for an exception at a fail-fast gate. Never returns.
[[noreturn]] GUI_FORMS_API void fail_fast(stxlib::Operation site, std::string_view message) noexcept;

// The report's parts, shared with the borrow sanitizer's report
// (detail::fail_borrow, gui_forms/borrow/borrowed_view/borrowed_view.hpp):
// the current stack, one line per frame, and one code address as its module
// and offset (and symbol, where the platform names one), so a symbolizer
// can name the function after the fact. Both write to standard error only,
// through STXlib's console.
GUI_FORMS_API void write_fail_fast_stack() noexcept;
GUI_FORMS_API void write_code_address(const void* address) noexcept;

// The fail-fast gate's target for a free function: calls it with the gate's
// arguments.
template <typename Result, typename... Parameters>
struct FailingFastFunction final {
    Result (*target)(Parameters...);

    template <typename... Given>
    Result operator()(Given&... given) const {
        return target(given...);
    }
};

// The fail-fast gate's target for a member function and its object.
template <typename Result, typename Owner, typename... Parameters>
struct FailingFastMember final {
    Result (Owner::*target)(Parameters...);
    Owner* owner;

    template <typename... Given>
    Result operator()(Given&... given) const {
        return ((*owner).*target)(given...);
    }
};

// Calls `target(arguments...)` behind the fail-fast gate and returns its
// result; an exception ends the process. The arguments reach target as
// lvalues (the gate passes them by reference), so target takes them by value
// or by lvalue reference.
template <typename Result, typename... Parameters, typename... Arguments>
Result call_failing_fast(const stxlib::Operation site, Result (*target)(Parameters...),
                         Arguments&&... arguments) noexcept {
    ensure_fail_fast_reporter();
    const FailingFastFunction<Result, Parameters...> call{target};
    const stxlib::GateFrame frame{.operation = site};
    return stxlib::invoke_callback_classified(fail_fast_classifier, frame, call, arguments...);
}

// Calls `(owner.*target)(arguments...)` behind the fail-fast gate and
// returns its result; an exception ends the process.
template <typename Result, typename Owner, typename... Parameters, typename... Arguments>
Result call_failing_fast(const stxlib::Operation site, Result (Owner::*target)(Parameters...), Owner& owner,
                         Arguments&&... arguments) noexcept {
    ensure_fail_fast_reporter();
    const FailingFastMember<Result, Owner, Parameters...> call{target, &owner};
    const stxlib::GateFrame frame{.operation = site};
    return stxlib::invoke_callback_classified(fail_fast_classifier, frame, call, arguments...);
}

} // namespace gui_forms::detail
