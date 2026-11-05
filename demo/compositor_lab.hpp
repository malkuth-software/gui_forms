#pragma once

#include "program_options.hpp"

#include "gui_forms/host/compositor/window_backdrop.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/window.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// The compositor lab (stage T5, phase C8; notes:planning/stages/T5-compositor/
// PLAN.md §5 C8 and §6): one window, declared non-opaque, that asks the host
// for a backdrop and shows what it was given. It carries
// - scenery drawn in the backplane plane, over the left part of the window;
// - a glass region straddling the scenery and the window's clear part;
// - a face reflecting itself and a face reflecting the backplane under it;
// - a status line naming the request, the grant and its reason.
// Each grant change prints one JSON line on stdout, the lab's part of the
// stage gate record (tools/t5_gate_record.py).
//
// Demo recipes over public GUI.Forms vocabulary: no host, renderer or
// platform type appears here.
namespace gui_forms::compositor_lab {

// The window's logical size.
inline constexpr Size lab_size{560.0, 360.0};

// What the lab asks for when the command line names nothing: the main
// window's material, falling back to transparent.
[[nodiscard]] WindowBackdropRequest default_request() noexcept;

// Parses a --backdrop value: one to three WindowBackdrop names
// (window_backdrop_name), comma-separated, preferred first. Nullopt for an
// unknown name or a request window_backdrop_request_valid refuses; an empty
// text is the default request.
[[nodiscard]] std::optional<WindowBackdropRequest> parse_backdrop_request(std::string_view text);

// The lab's theme: the professional theme with glass on regions, the
// material primitive on faces, a reflecting button face (its own face) and
// a reflecting choice face (the backplane), and a backplane base for each
// non-opaque backdrop: a partly clear tint over either material, clear over
// transparent.
[[nodiscard]] std::shared_ptr<const Theme> compositor_lab_theme();

// The JSON line the lab prints for `grant`: the host's capabilities (when
// a host is attached), the compositor state, the request, the grant and its
// reason. No trailing newline.
[[nodiscard]] std::string grant_record(const Window& window, const WindowBackdropGrant& grant);

// The lab window: themed, declared non-opaque and with `request` made, so
// that the host attaching it resolves the grant. With `print_records`, each
// grant change prints grant_record on stdout.
[[nodiscard]] std::unique_ptr<Window> make_compositor_lab(const WindowBackdropRequest& request,
                                                          bool print_records);

// Prints the record of the grant in force, unless it was the last one
// printed; nothing for a window make_compositor_lab did not make or made
// without records.
void record_grant_in_force(Window& window);

// A host_ready link that records the grant in force once the host is
// ready: a host whose first report leaves the grant as it was (raising no
// backdrop_changed) is still recorded. Then calls the previous link.
struct RecordWhenReady final {
    Window* window{};
    demo::HostReadyCallback previous;

    void operator()(std::function<void()> wake, std::function<void()> request_close,
                    std::function<HostDialogResult(const HostDialogRequest&)> show_dialog,
                    std::function<HostServiceStatus(const HostTooltipRequest&)> show_tooltip,
                    std::function<void()> hide_tooltip,
                    std::function<HostClipboardTextResult()> read_clipboard_text,
                    std::function<HostServiceStatus(std::string_view)> write_clipboard_text) const {
        if (window != nullptr) record_grant_in_force(*window);
        if (previous) {
            previous(std::move(wake), std::move(request_close), std::move(show_dialog),
                     std::move(show_tooltip), std::move(hide_tooltip),
                     std::move(read_clipboard_text), std::move(write_clipboard_text));
        }
    }
};

// Links RecordWhenReady into a host option struct (MacHostOptions,
// WindowsHostOptions, LinuxHostOptions) for `window`, which must outlive
// the host's run.
template <class HostOptions>
inline void record_when_ready(HostOptions& host, Window& window) {
    host.host_ready = RecordWhenReady{&window, std::move(host.host_ready)};
}

} // namespace gui_forms::compositor_lab
