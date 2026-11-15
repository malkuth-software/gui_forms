#pragma once

// Hosted content (owner decisions 2026-11-13, "The two topologies": GUI.Forms
// as host around a sub-surface program). The program runs in a process of
// its own; its frames arrive through shared memory and its input goes back
// through a channel (hosted_protocol.hpp), behind the same interface as
// in-process content: make_hosted_content returns a ContentProgram for
// ContentSurface::start_program, and the surface's status, watchdog,
// termination and stale veil apply unchanged.
// ContentSurface::start_hosted_content does both and also forwards the
// surface's pointer, keys, text and focus to the process, in the owner's
// input order (content_surface.hpp).
//
// What the application is promised:
//   - A program that crashes ends its own process only. The surface's status
//     becomes `failed`, with the signal or exit status as its failure text.
//   - A program that hangs leaves the interface responsive: the watchdog
//     marks it stale and keeps its last frame, and terminate() ends it (a
//     stop message, then, after the stop grace, the process is killed).
//   - Closing the window, destroying the surface or shutting down ends the
//     process the same way, within the surface's termination grace.
//   - Everything the program sends is validated before use (frame records
//     against their slot, message sizes and kinds, text), and a program that
//     breaks the protocol is ended with status `failed`.
// It is an integrity boundary, not a security boundary: the process runs
// with the application's rights.
//
// Platforms: macOS and Linux (posix_spawn, shared memory, a socket pair) and
// Windows (CreateProcessW in a job object, a pagefile-backed section, an
// overlapped named pipe), reported as HostCapability::hosted_content. On
// Windows a crash's failure text carries the NTSTATUS the process ended
// with where POSIX names the signal.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/content/hosted/hosted_protocol.hpp"
#include "gui_forms/content/operation/content_operation.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types/platform_types/platform_types.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

namespace detail {
struct HostedInputState;
} // namespace detail

enum class HostedContentSupport : std::uint8_t {
    available,
    // This platform has no implementation (no supported platform at
    // present; kept for ports that have none yet).
    not_reported,
};

[[nodiscard]] GUI_FORMS_API HostedContentSupport hosted_content_support() noexcept;

inline constexpr std::chrono::milliseconds default_hosted_start_timeout{5000};
inline constexpr std::chrono::milliseconds default_hosted_stop_grace{100};

struct GUI_FORMS_API HostedContentOptions final {
    // The program to run, by absolute path; it is not looked up on PATH.
    FilePath executable{};
    // Its arguments after the program name.
    std::vector<std::string> arguments{};
    // Each frame slot's size in bytes (hosted_protocol.hpp); frames larger
    // than a slot are refused. At most maximum_hosted_slot_bytes.
    std::uint64_t slot_bytes{default_hosted_slot_bytes};
    // How long the program may take to send hello; it is ended as failed
    // after that.
    std::chrono::milliseconds start_timeout{default_hosted_start_timeout};
    // How long a stopped program may take to exit before it is killed. Keep
    // it below the surface's termination grace, so the process is ended
    // before the surface stops waiting.
    std::chrono::milliseconds stop_grace{default_hosted_stop_grace};
};

// The application's end of the input channel: forwards input to the
// program, from the UI thread or any other. A send never waits: when the
// channel is full or the program is not running, the event is dropped and
// counted. Copies share one channel.
class GUI_FORMS_API HostedInput final {
public:
    HostedInput() noexcept = default;
    explicit HostedInput(std::shared_ptr<detail::HostedInputState> state) noexcept;

    // A key, as the window delivered it.
    bool send_key(const KeyEvent& event);
    // Text input, an input method's composition included.
    bool send_text(std::string_view text, bool composing);
    // A pointer event; `device_position` is in the region's device pixels
    // from its top left (the local position times the window's scale).
    bool send_pointer(const PointerEvent& event, Point device_position);
    // The surface gained (true) or lost the keyboard focus.
    bool send_focus(bool focused);
    // Events dropped because the channel was full or closed.
    [[nodiscard]] std::uint64_t dropped() const noexcept;
    // Whether the program's process is running and connected.
    [[nodiscard]] bool connected() const noexcept;

private:
    // `droppable`: whether the event may be dropped while the channel's
    // backlog is over its limit (presses, moves, wheel and text); key and
    // button releases and focus changes are queued regardless while the
    // channel is open, so the program never keeps a key or button down, or
    // the focus, that the application has let go.
    bool send(HostedMessageKind kind, std::span<const std::byte> payload, bool droppable);

    std::shared_ptr<detail::HostedInputState> state_{};
};

// What make_hosted_content makes: the program to start on a ContentSurface,
// and the input channel to its process.
struct GUI_FORMS_API HostedContent final {
    std::unique_ptr<ContentProgram> program{};
    HostedInput input{};
};

// Checks `options` and makes the program; nothing starts until the surface
// runs it. Throws std::invalid_argument for an executable that is not an
// absolute path, a slot size of zero or over the limit, or a negative
// timeout; on Windows also for an argument that is not UTF-8 or holds a NUL,
// and for a command line over 32,767 characters. Throws std::runtime_error
// where hosted content is not supported.
[[nodiscard]] GUI_FORMS_API HostedContent make_hosted_content(HostedContentOptions options);

} // namespace gui_forms
