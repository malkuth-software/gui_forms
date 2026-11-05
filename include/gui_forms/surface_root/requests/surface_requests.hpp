#pragma once

#include "gui_forms/host/types/host_types.hpp"
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

// One alert: a message the user acknowledges by choosing one of its
// buttons, its content as data. The deciding window shows its alerts
// modally, one at a time, in the order they were raised.
struct AlertRequest final {
    // The message, not empty. It and the caption are valid UTF-8 without
    // NUL, of at most HostServices::maximum_dialog_text_bytes.
    std::string text{};
    // The title; empty for the host's own.
    std::string caption{};
    HostMessageButtons buttons{HostMessageButtons::ok};
    HostMessageIcon icon{HostMessageIcon::none};
    // The button Enter chooses, one of `buttons`; none for the first.
    HostDialogChoice default_choice{HostDialogChoice::none};
};

enum class SurfaceRequestResult : std::uint8_t {
    // It shows (tooltips; an alert is answered once, when it closes).
    shown,
    // The deciding root refused it (its policy, or an owner that cannot
    // show one now; for an alert, no host or a host that refused the
    // dialog); nothing is kept.
    declined,
    // It showed and was closed (auto-pop, a press, its owner left; an
    // alert, by the user's choice).
    closed,
    // It was replaced by a newer request, or revoked by the root that
    // decides (the pointer or focus left a sub-surface's host); an alert
    // withdrawn, or whose owner left its root, before it showed.
    revoked,
    // It arrived out of date at the deciding root and was dropped.
    stale,
};

// An answer to one request; `id` is the one the request API returned.
struct SurfaceRequestOutcome final {
    std::uint64_t id{};
    SurfaceRequestResult result{SurfaceRequestResult::shown};
    // For a closed alert, the button chosen: cancel (or ok for an OK box)
    // when the user closed it without choosing, none when the host closed
    // a box the user cannot dismiss (Yes/No; Abort, Retry, Ignore).
    HostDialogChoice choice{HostDialogChoice::none};
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
