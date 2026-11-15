// The page's entry points (web_host.js calls them on the browser's main
// thread): each turns one DOM event into the window manager's record
// (surface_input.hpp) and posts it to the web host's input queue
// (web_input_queue). Strings are UTF-8 in the module's memory, read before
// the call returns; the page frees them afterwards.
//
// A record the queue refuses (it is full) is dropped and counted; nothing
// here blocks beyond the queue's short lock.
#include "../application/web_surface_host.hpp"
#include "../input/web_input_rules.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define GF_WEB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define GF_WEB_EXPORT
#endif

namespace {

using gui_forms::host::surface_detail::SurfaceInputKind;
using gui_forms::host::surface_detail::SurfaceInputRecord;
using gui_forms::host::surface_detail::SurfacePointerAction;

std::string text_of(const char* text) {
    std::string copied = text == nullptr ? std::string{} : std::string(text);
    return copied;
}

// Appends each non-empty line of `joined` to `lines`.
void split_lines(const char* joined, std::vector<std::string>& lines) {
    const std::string text = text_of(joined);
    std::size_t start = 0U;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        if (end > start) lines.push_back(text.substr(start, end - start));
        start = end + 1U;
    }
}

void post(SurfaceInputRecord record) {
    static_cast<void>(gui_forms::host::web_detail::web_input_queue().post(std::move(record)));
}

} // namespace

extern "C" {

// A pointer event: action as web_pointer_action numbers it, x and y CSS
// pixels of the canvas, button the DOM button, modifiers a bit set, time
// the event's timestamp in milliseconds.
GF_WEB_EXPORT void gf_web_post_pointer(int action, double x, double y, int button, int pointer,
                                       unsigned int modifiers, double time) {
    static_cast<void>(pointer);
    const std::optional<SurfacePointerAction> known = gui_forms::host::web_detail::web_pointer_action(action);
    if (!known.has_value()) return;
    SurfaceInputRecord record;
    record.kind = SurfaceInputKind::pointer;
    record.action = *known;
    record.position = gui_forms::Point{x, y};
    record.button = gui_forms::host::web_detail::web_pointer_button(button);
    record.modifiers = gui_forms::host::web_detail::web_modifiers(modifiers);
    record.time = time;
    post(std::move(record));
}

// A wheel event: mode is the DOM deltaMode.
GF_WEB_EXPORT void gf_web_post_wheel(double x, double y, double delta_x, double delta_y, int mode,
                                     unsigned int modifiers) {
    SurfaceInputRecord record;
    record.kind = SurfaceInputKind::wheel;
    record.position = gui_forms::Point{x, y};
    record.wheel_delta = gui_forms::host::web_detail::web_wheel_delta(delta_x, delta_y);
    record.wheel_unit = gui_forms::host::web_detail::web_wheel_unit(mode);
    record.modifiers = gui_forms::host::web_detail::web_modifiers(modifiers);
    post(std::move(record));
}

// A key: code is KeyboardEvent.code.
GF_WEB_EXPORT void gf_web_post_key(int down, int repeat, const char* code, unsigned int modifiers) {
    SurfaceInputRecord record;
    record.kind = SurfaceInputKind::key;
    record.down = down != 0;
    record.repeat = repeat != 0;
    record.physical_key = gui_forms::host::web_detail::web_physical_key(text_of(code));
    record.modifiers = gui_forms::host::web_detail::web_modifiers(modifiers);
    post(std::move(record));
}

// Committed text (composing 0) or the composition's marked text (1).
GF_WEB_EXPORT void gf_web_post_text(const char* text, int composing) {
    SurfaceInputRecord record;
    record.kind = composing != 0 ? SurfaceInputKind::composition : SurfaceInputKind::text;
    record.text = text_of(text);
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_clipboard(const char* text) {
    SurfaceInputRecord record;
    record.kind = SurfaceInputKind::clipboard;
    record.text = text_of(text);
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_resize(double width, double height, double scale) {
    SurfaceInputRecord record;
    record.kind = SurfaceInputKind::resize;
    record.extent = gui_forms::Size{width, height};
    record.scale = scale;
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_focus(int focused) {
    SurfaceInputRecord record;
    record.kind = SurfaceInputKind::focus;
    record.down = focused != 0;
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_visibility(int hidden) {
    SurfaceInputRecord record;
    record.kind = SurfaceInputKind::visibility;
    record.down = hidden != 0;
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_presented() {
    SurfaceInputRecord record;
    record.kind = SurfaceInputKind::presented;
    post(std::move(record));
}

// The canvas lost its pixels (a restored WebGL context): every pixel is
// composed and uploaded again.
GF_WEB_EXPORT void gf_web_post_activation(int active) {
    SurfaceInputRecord record;
    record.kind = SurfaceInputKind::activation;
    record.down = active != 0;
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_frame(double time) {
    SurfaceInputRecord record;
    record.kind = SurfaceInputKind::frame;
    record.time = time;
    post(std::move(record));
}

// A browser dialog's answer. paths is the written files, joined by '\n'
// (file names never hold one here: the page replaces it); text the chosen
// value.
GF_WEB_EXPORT void gf_web_post_dialog(double request, int outcome, const char* paths, const char* text) {
    SurfaceInputRecord record;
    record.kind = SurfaceInputKind::dialog;
    record.request = static_cast<std::uint64_t>(request);
    record.outcome = gui_forms::host::web_detail::web_dialog_outcome(outcome);
    record.text = text_of(text);
    split_lines(paths, record.paths);
    post(std::move(record));
}

// Files dropped at x, y, already written to the program's file system.
GF_WEB_EXPORT void gf_web_post_drop(double x, double y, const char* paths, unsigned int modifiers) {
    SurfaceInputRecord record;
    record.kind = SurfaceInputKind::drop;
    record.position = gui_forms::Point{x, y};
    record.modifiers = gui_forms::host::web_detail::web_modifiers(modifiers);
    split_lines(paths, record.paths);
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_fonts(const char* group) {
    SurfaceInputRecord record;
    record.kind = SurfaceInputKind::fonts;
    record.text = text_of(group);
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_close_page() {
    SurfaceInputRecord record;
    record.kind = SurfaceInputKind::close;
    post(std::move(record));
}

} // extern "C"
