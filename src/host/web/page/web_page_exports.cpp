// The page's entry points (web_host.js calls them on the browser's main
// thread): each builds one PageRecord and posts it to the page queue
// (web_page_queue.hpp). Strings are UTF-8 in the module's memory, read
// before the call returns; the page frees them afterwards.
//
// A record the queue refuses (it is full) is dropped and counted; nothing
// here blocks beyond the queue's short lock.
#include "web_page_queue.hpp"

#include <cstdint>
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

using gui_forms::host::web_detail::PageRecord;
using gui_forms::host::web_detail::PageRecordKind;
using gui_forms::host::web_detail::page_queue;

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

void post(PageRecord record) { static_cast<void>(page_queue().post(std::move(record))); }

} // namespace

extern "C" {

// A pointer event: action is a PagePointerAction, x and y CSS pixels of the
// canvas, button the DOM button, buttons and modifiers bit sets, time the
// event's timestamp in milliseconds.
GF_WEB_EXPORT void gf_web_post_pointer(int action, double x, double y, int button, int pointer,
                                       unsigned int modifiers, double time) {
    PageRecord record;
    record.kind = PageRecordKind::pointer;
    record.action = static_cast<std::uint8_t>(action);
    record.x = x;
    record.y = y;
    record.button = button;
    record.pointer = pointer;
    record.modifiers = modifiers;
    record.time = time;
    post(std::move(record));
}

// A wheel event: mode is the DOM deltaMode.
GF_WEB_EXPORT void gf_web_post_wheel(double x, double y, double delta_x, double delta_y, int mode,
                                     unsigned int modifiers) {
    PageRecord record;
    record.kind = PageRecordKind::wheel;
    record.x = x;
    record.y = y;
    record.delta_x = delta_x;
    record.delta_y = delta_y;
    record.mode = static_cast<std::uint8_t>(mode);
    record.modifiers = modifiers;
    post(std::move(record));
}

// A key: code is KeyboardEvent.code.
GF_WEB_EXPORT void gf_web_post_key(int down, int repeat, const char* code, unsigned int modifiers) {
    PageRecord record;
    record.kind = PageRecordKind::key;
    record.down = down != 0;
    record.repeat = repeat != 0;
    record.text = text_of(code);
    record.modifiers = modifiers;
    post(std::move(record));
}

// Committed text (composing 0) or the composition's marked text (1).
GF_WEB_EXPORT void gf_web_post_text(const char* text, int composing) {
    PageRecord record;
    record.kind = composing != 0 ? PageRecordKind::composition : PageRecordKind::text;
    record.text = text_of(text);
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_clipboard(const char* text) {
    PageRecord record;
    record.kind = PageRecordKind::clipboard;
    record.text = text_of(text);
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_resize(double width, double height, double scale) {
    PageRecord record;
    record.kind = PageRecordKind::resize;
    record.x = width;
    record.y = height;
    record.scale = scale;
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_focus(int focused) {
    PageRecord record;
    record.kind = PageRecordKind::focus;
    record.down = focused != 0;
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_visibility(int hidden) {
    PageRecord record;
    record.kind = PageRecordKind::visibility;
    record.down = hidden != 0;
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_presented() {
    PageRecord record;
    record.kind = PageRecordKind::presented;
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_frame(double time) {
    PageRecord record;
    record.kind = PageRecordKind::frame;
    record.time = time;
    post(std::move(record));
}

// A browser dialog's answer. paths is the written files, joined by '\n'
// (file names never hold one here: the page replaces it); text the chosen
// value.
GF_WEB_EXPORT void gf_web_post_dialog(double request, int outcome, const char* paths, const char* text) {
    PageRecord record;
    record.kind = PageRecordKind::dialog;
    record.request = static_cast<std::uint64_t>(request);
    record.mode = static_cast<std::uint8_t>(outcome);
    record.text = text_of(text);
    split_lines(paths, record.paths);
    post(std::move(record));
}

// Files dropped at x, y, already written to the program's file system.
GF_WEB_EXPORT void gf_web_post_drop(double x, double y, const char* paths, unsigned int modifiers) {
    PageRecord record;
    record.kind = PageRecordKind::drop;
    record.x = x;
    record.y = y;
    record.modifiers = modifiers;
    split_lines(paths, record.paths);
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_fonts(const char* group) {
    PageRecord record;
    record.kind = PageRecordKind::fonts;
    record.text = text_of(group);
    post(std::move(record));
}

GF_WEB_EXPORT void gf_web_post_close_page() {
    PageRecord record;
    record.kind = PageRecordKind::close_page;
    post(std::move(record));
}

} // extern "C"
