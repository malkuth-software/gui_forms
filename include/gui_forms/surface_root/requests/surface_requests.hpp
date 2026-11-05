#pragma once

#include "gui_forms/types/geometry/geometry.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace gui_forms {

// What a control asks its root to show beyond itself (threaded sub-surfaces
// design note section 15). A control calls its surface_root()'s request API
// and never needs to know which kind of root it lives in: a window handles
// the request itself (its tooltip presenter); a sub-surface, which never
// shows anything beyond the region it borrows from its parent, forwards it
// to its parent, which decides whether and where it shows.

// One tooltip, its content as data.
struct ToolTipRequest final {
    // Plain text, valid UTF-8, not empty.
    std::string text{};
    // The semantic description of what the tooltip is about, when it has one.
    std::string description{};
    // The thing the tooltip is about, in the root's coordinates; the bubble
    // shows below it.
    Rect anchor{};
    // How long it shows: none, the root's auto-pop delay; zero, until it is
    // withdrawn or replaced.
    std::optional<std::chrono::milliseconds> duration{};
};

enum class SurfaceRequestResult : std::uint8_t {
    // It shows.
    shown,
    // The deciding root refused it (its policy, or an owner that cannot
    // show one now); nothing is kept.
    declined,
    // It showed and was closed (auto-pop, a press, its owner left).
    closed,
    // It was replaced by a newer request, or revoked by the root that
    // decides (the pointer or focus left a sub-surface's host).
    revoked,
    // It arrived out of date at the deciding root and was dropped.
    stale,
};

// An answer to one request; `id` is the one the request API returned.
struct SurfaceRequestOutcome final {
    std::uint64_t id{};
    SurfaceRequestResult result{SurfaceRequestResult::shown};
};

// Where a request's answers go: posted through the requesting root's
// dispatcher and called on its thread, in the order they were given. Held
// weakly; a target released before an answer arrives receives nothing.
class SurfaceRequestAnswer {
public:
    virtual void request_answered(const SurfaceRequestOutcome& outcome) = 0;

protected:
    ~SurfaceRequestAnswer() {}
};

} // namespace gui_forms
