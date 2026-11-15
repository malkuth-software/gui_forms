#pragma once
// The records the page's main thread posts to the program's thread
// (web_host.js through the gf_web_post_* exports in web_page_exports.cpp),
// and the queue that carries them. Plain C++ with no Emscripten, so the
// queue's order, waits and wakes are tested on every platform
// (tests/web_desk_tests.cpp).
//
// Threads: post and wake are called on any thread (the page's main thread,
// the engine's workers, a model's wake handler); take is called only on the
// program's thread. The page's main thread may lock the queue's mutex: a
// browser's main thread cannot wait on a futex, so Emscripten spins there,
// and each critical section here is a few moves long.

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace gui_forms::host::web_detail {

enum class PageRecordKind : std::uint8_t {
    // A DOM pointer event on the canvas: `action` is a PagePointerAction,
    // `x` and `y` the canvas position in CSS pixels, `button` the DOM
    // button, `pointer` the pointer id, `time` the event's timestamp.
    pointer,
    // A wheel event: `x` and `y` the position, `delta_x` and `delta_y` the
    // DOM deltas, `mode` the deltaMode.
    wheel,
    // A key on the input element: `down`, `repeat`, `text` the
    // KeyboardEvent.code.
    key,
    // Committed text from the input element (an input event or a
    // composition's end).
    text,
    // The composition's marked text; empty cancels it.
    composition,
    // The page's clipboard text as a paste or copy event saw it.
    clipboard,
    // The canvas's CSS size (`x`, `y`) and devicePixelRatio (`scale`).
    resize,
    // The canvas gained (`down` true) or lost the keyboard.
    focus,
    // The page was hidden (`down` true) or shown again.
    visibility,
    // The last frame was presented; painting may resume.
    presented,
    // An animation frame while the program asked for them (live surfaces).
    frame,
    // A browser dialog finished: `request` its identity, `mode` a
    // PageDialogOutcome, `paths` the files written or the chosen value in
    // `text`.
    dialog,
    // Files dropped on the canvas at (`x`, `y`), written under `paths`.
    drop,
    // A font group arrived in the file system (`text` its name).
    fonts,
    // The page is going away (pagehide): every window closes.
    close_page,
};

enum class PagePointerAction : std::uint8_t {
    move = 0,
    down = 1,
    up = 2,
    enter = 3,
    leave = 4,
    cancel = 5,
};

enum class PageDialogOutcome : std::uint8_t {
    cancelled = 0,
    accepted = 1,
    failed = 2,
};

struct PageRecord final {
    PageRecordKind kind{PageRecordKind::frame};
    std::uint8_t action{};
    std::uint8_t mode{};
    bool down{};
    bool repeat{};
    std::int32_t button{};
    std::uint32_t modifiers{};
    std::int32_t pointer{};
    double x{};
    double y{};
    double delta_x{};
    double delta_y{};
    double scale{1.0};
    double time{};
    std::uint64_t request{};
    std::string text{};
    std::vector<std::string> paths{};
};

class PageQueue final {
public:
    // Records beyond this many are dropped: a page that floods the queue
    // loses input rather than memory.
    static constexpr std::size_t maximum_records = 4096U;

    // Appends `record` and wakes the program's thread. Returns false, keeping
    // nothing, when the queue is full.
    bool post(PageRecord record);
    // Wakes the program's thread without a record (a model's wake handler).
    void wake() noexcept;
    // Moves every record into `taken` (appended, its storage reused) and
    // clears the wake. When there is neither a record nor a wake, first
    // waits until one arrives or `deadline` passes; no deadline waits without
    // limit, and a deadline in the past does not wait. Returns whether a
    // record or a wake was taken.
    bool take(std::deque<PageRecord>& taken,
              std::optional<std::chrono::steady_clock::time_point> deadline);
    // Records dropped because the queue was full.
    [[nodiscard]] std::uint64_t dropped() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<PageRecord> records_;
    bool woken_{};
    std::uint64_t dropped_{};
};

// The process's one queue: the page posts to it before the program's thread
// starts its run, and the run takes from it.
[[nodiscard]] PageQueue& page_queue() noexcept;

} // namespace gui_forms::host::web_detail
