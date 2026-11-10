#include "windows_message_queue.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

void require(const bool condition, const char* const message) {
    if (!condition) { throw std::runtime_error(message); }
}

void post(const UINT message, const WPARAM value = 0U) {
    const BOOL posted = PostThreadMessageW(GetCurrentThreadId(), message, value, 0);
    require(posted != FALSE, "post test queue message");
}

struct QueueEntry final {
    UINT message{0U};
    WPARAM value{0U};
    bool hardware{false};
    bool consumed{false};
};

struct QueueCategories final {
    // The earlier character was posted by translation; Backspace is later
    // hardware input. Private rendering work precedes both in the posted lane.
    std::array<QueueEntry, 4> entries{{
        {WM_APP + 0x42U, 1U, false, false},
        {WM_APP + 0x42U, 2U, false, false},
        {WM_CHAR, 'a', false, false},
        {WM_KEYDOWN, VK_BACK, true, false}}};
};

// This bounded reference queue follows documented Win32 category precedence:
// matching posted messages precede hardware unless QS_INPUT is requested.
// It does not simulate the desktop or prove physical device delivery.
bool read_categories(void* const context, MSG& message,
                     const UINT first, const UINT last, const UINT flags) {
    QueueCategories& queue = *static_cast<QueueCategories*>(context);
    const bool removing = (flags & PM_REMOVE) != 0U;
    const bool hardware_only = (flags & PM_QS_INPUT) != 0U;
    const bool posted_only = (flags & PM_QS_POSTMESSAGE) != 0U;
    const std::size_t last_category = posted_only ? 1U : 2U;
    for (std::size_t category = hardware_only ? 1U : 0U; category < last_category; ++category) {
        for (QueueEntry& entry : queue.entries) {
            const bool in_range = (first == 0U && last == 0U) ||
                (entry.message >= first && entry.message <= last);
            if (!entry.consumed && in_range && entry.hardware == (category == 1U)) {
                message = {};
                message.message = entry.message;
                message.wParam = entry.value;
                if (removing) entry.consumed = true;
                return true;
            }
        }
    }
    return false;
}

void test_translated_character_order() {
    QueueCategories queue{};
    MSG message{};
    require(gui_forms::host::detail::take_window_message_with_reader(
                message, true, read_categories, &queue), "translated character available");
    require(message.message == WM_CHAR && message.wParam == 'a',
            "earlier translated character must precede later hardware Backspace");
    require(gui_forms::host::detail::take_window_message_with_reader(
                message, false, read_categories, &queue), "ordinary work available");
    require(message.message == WM_APP + 0x42U && message.wParam == 1U,
            "ordinary work receives its paired turn");
    require(gui_forms::host::detail::take_window_message_with_reader(
                message, true, read_categories, &queue), "later hardware key available");
    require(message.message == WM_KEYDOWN && message.wParam == VK_BACK,
            "hardware key still overtakes unrelated posted rendering work");

    QueueCategories outside_range{};
    outside_range.entries[2].consumed = true;
    outside_range.entries[3].message = WM_INPUT;
    require(gui_forms::host::detail::take_window_message_with_reader(
                message, true, read_categories, &outside_range), "input-category fallback available");
    require(message.message == WM_INPUT, "input outside the key/mouse range is not starved");
}

// A press on each frame part and the caption, then the release over the
// client: the press is taken before the release, so DefWindowProc's size or
// move loop sees its release (owner, 2026-11-10: a resize that took a second
// click). The same order holds for the main window and every owned window:
// the queue is the thread's.
void test_frame_press_precedes_release() {
    const std::array<WPARAM, 9> parts{{HTCAPTION, HTLEFT, HTRIGHT, HTTOP, HTTOPLEFT, HTTOPRIGHT, HTBOTTOM,
                                       HTBOTTOMLEFT, HTBOTTOMRIGHT}};
    for (const WPARAM part : parts) {
        QueueCategories queue{};
        queue.entries[0] = {WM_APP + 0x42U, 1U, false, false};
        queue.entries[1] = {WM_NCLBUTTONDOWN, part, true, false};
        queue.entries[2] = {WM_MOUSEMOVE, 0U, true, false};
        queue.entries[3] = {WM_LBUTTONUP, 0U, true, false};
        MSG message{};
        require(gui_forms::host::detail::take_window_message_with_reader(message, true, read_categories, &queue),
                "the frame press is available");
        require(message.message == WM_NCLBUTTONDOWN && message.wParam == part,
                "a frame press is taken before the later release over the client");
        require(gui_forms::host::detail::take_window_message_with_reader(message, true, read_categories, &queue) &&
                    message.message == WM_MOUSEMOVE,
                "then the motion");
        require(gui_forms::host::detail::take_window_message_with_reader(message, true, read_categories, &queue) &&
                    message.message == WM_LBUTTONUP,
                "then the one release that ends the drag");
        require(gui_forms::host::detail::frame_press_may_drag(part, true),
                "a frame press with the button down drags");
        require(!gui_forms::host::detail::frame_press_may_drag(part, false),
                "a frame press whose button is already up starts no loop that would wait for a second click");
    }
    require(gui_forms::host::detail::frame_press_may_drag(HTCLOSE, false) &&
                gui_forms::host::detail::frame_press_may_drag(HTMINBUTTON, false) &&
                gui_forms::host::detail::frame_press_may_drag(HTSYSMENU, false),
            "a press on the system buttons is always passed on");
    QueueCategories typed{};
    typed.entries[0] = {WM_CHAR, 'a', false, false};
    typed.entries[1] = {WM_NCLBUTTONDOWN, HTTOP, true, false};
    typed.entries[2] = {WM_LBUTTONUP, 0U, true, false};
    typed.entries[3] = {WM_APP + 0x42U, 1U, false, false};
    MSG message{};
    require(gui_forms::host::detail::take_window_message_with_reader(message, true, read_categories, &typed) &&
                message.message == WM_CHAR,
            "a translated character still comes first");
    require(gui_forms::host::detail::take_window_message_with_reader(message, true, read_categories, &typed) &&
                message.message == WM_NCLBUTTONDOWN,
            "then the frame press, before the release");
}

void test_queue() {
    MSG message{};
    // Establish this thread's queue; all fixtures stay on the test thread.
    PeekMessageW(&message, nullptr, 0U, 0U, PM_NOREMOVE);
    constexpr UINT render_wake = WM_APP + 0x42U;
    for (WPARAM index = 0U; index < 64U; ++index) { post(render_wake, index); }
    post(WM_KEYDOWN, 'H');
    post(WM_CHAR, 'h');
    post(WM_LBUTTONDOWN);
    post(WM_LBUTTONUP);
    require(gui_forms::host::detail::take_window_message(message, true), "queued key available");
    require(message.message == WM_KEYDOWN, "posted input precedes render backlog");
    require(gui_forms::host::detail::take_window_message(message, false), "ordinary turn available");
    require(message.message == render_wake && message.wParam == 0U, "ordinary work retains FIFO");
    require(gui_forms::host::detail::take_window_message(message, true), "translated character available");
    require(message.message == WM_CHAR && message.wParam == 'h', "translated character ordering preserved");
    require(gui_forms::host::detail::take_window_message(message, true), "mouse press available");
    require(message.message == WM_LBUTTONDOWN, "mouse press precedes release");
    require(gui_forms::host::detail::take_window_message(message, true), "mouse release available");
    require(message.message == WM_LBUTTONUP, "mouse release ordering preserved");
    for (WPARAM index = 1U; index < 64U; ++index) {
        require(gui_forms::host::detail::take_window_message(message, true), "no-input fallback available");
        require(message.message == render_wake && message.wParam == index, "fallback preserves posted work");
    }
    require(!gui_forms::host::detail::take_window_message(message, true), "empty queue does not fabricate work");
    PostQuitMessage(17);
    require(gui_forms::host::detail::take_window_message(message, true), "quit available during input turn");
    require(message.message == WM_QUIT && message.wParam == 17U, "quit exit code preserved");
    PostQuitMessage(23);
    require(gui_forms::host::detail::take_window_message(message, false), "quit available during ordinary turn");
    require(message.message == WM_QUIT && message.wParam == 23U, "ordinary quit exit code preserved");
}

} // namespace

int main() {
    try {
        test_translated_character_order();
        test_frame_press_precedes_release();
        test_queue();
        std::cout << "Windows queue fixtures passed; physical device delivery requires native dogfood\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
