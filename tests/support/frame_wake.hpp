#pragma once

// Reading a window's or a poll's next wake in tests. SurfaceRoot::next_wake
// and FramePollResult::next_wake are stxlib::Optional<FrameTime>, whose
// value() is null when no wake is armed. armed_wake checks presence before
// it reads, and fails the test naming the caller's line instead of reading
// an absent wake; wake_at spells the expected wake a comparison needs.

#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/surface_root/surface_root.hpp"

#include <source_location>
#include <stdexcept>
#include <string>

#include <stxlib/optional.hpp>

namespace gui_forms::test_support {

// The wake `wake` holds. When it holds none, throws std::runtime_error with
// "<file>:<line>: no wake is armed" for `where`, the failure the tests'
// require reports; nothing else changes.
inline FrameTime armed_wake(const stxlib::Optional<FrameTime>& wake,
                            const std::source_location where = std::source_location::current()) {
    const FrameTime* const armed = wake.value();
    if (armed == nullptr) {
        std::string message(where.file_name());
        message += ":";
        message += std::to_string(where.line());
        message += ": no wake is armed";
        throw std::runtime_error(message);
    }
    const FrameTime result = *armed;
    return result;
}

// The wake `window` has armed, read as above.
inline FrameTime armed_wake(const SurfaceRoot& window,
                            const std::source_location where = std::source_location::current()) {
    const stxlib::Optional<FrameTime> wake = window.next_wake();
    const FrameTime result = armed_wake(wake, where);
    return result;
}

// The armed wake at `time`, for comparing with next_wake.
inline stxlib::Optional<FrameTime> wake_at(const FrameTime time) noexcept {
    const stxlib::Optional<FrameTime> result = stxlib::Optional<FrameTime>::of(time);
    return result;
}

} // namespace gui_forms::test_support
