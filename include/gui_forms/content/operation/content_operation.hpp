#pragma once

// Termination of content (owner decisions 2026-11-13, "What this asks of
// GUI.Forms", item 2): a content operation is cancelled cooperatively, and
// the interface's own close and shutdown never join a hung content context.
// They ask for a stop, wait a bounded time, and then abandon the context:
// its channel is revoked, so nothing it does from then on reaches anything
// the interface reads, and its thread is detached.
//
// What abandoning leaves behind, stated plainly: the abandoned thread keeps
// running until its code returns or the process exits. It keeps alive, by
// shared ownership, the program object and the channel it holds; nothing of
// the window or the surface. When the process exits while it still runs,
// the operating system ends it with the process; static destructors may run
// meanwhile, so content that may hang must not use static state it shares
// with the rest of the program. Content that cannot be trusted to stop
// belongs in a hosted process (content/hosted), whose end GUI.Forms can
// force.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/content/frame_sink/content_frame_sink.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>

namespace gui_forms {

namespace detail {
struct ContentWorkerShared;
}

// The default bounded wait of a termination: 250 milliseconds.
inline constexpr std::chrono::milliseconds default_content_termination_grace{250};
// The longest bounded wait accepted: 10 seconds (longer ones are lowered).
inline constexpr std::chrono::milliseconds maximum_content_termination_grace{10000};

// What a content program sees on its own thread: the sink it publishes
// through and the stop it should honour. Valid for the duration of
// ContentProgram::run, on that thread only.
class GUI_FORMS_API ContentContext final {
public:
    explicit ContentContext(ContentFrameSink sink) noexcept;
    ContentContext(const ContentContext&) = delete;
    ContentContext& operator=(const ContentContext&) = delete;

    // The program publishes through the non-const sink; readers that only
    // inspect it take the const one.
    [[nodiscard]] ContentFrameSink& sink() noexcept { return sink_; }
    [[nodiscard]] const ContentFrameSink& sink() const noexcept { return sink_; }
    // Fires when the surface asks the program to stop (terminate, close,
    // leaving the window, the surface's destruction).
    [[nodiscard]] std::stop_token stop_token() const noexcept;
    [[nodiscard]] bool stop_requested() const noexcept;
    // ContentFrameSink::wait_for_request: also returns at once when a stop
    // is requested.
    [[nodiscard]] ContentRequest wait_for_request(std::uint64_t seen_generation,
                                                  std::chrono::milliseconds timeout) const;

private:
    ContentFrameSink sink_;
};

// A consumer's content operation, run on a thread GUI.Forms starts for it
// (ContentWorker). run returns when the content is done or a stop was
// requested; an exception it throws ends the operation as failed, with the
// exception's text. The object is destroyed on the worker's thread after run
// returns, so its destructor may release what run made there.
class GUI_FORMS_API ContentProgram {
public:
    virtual ~ContentProgram() = default;
    virtual void run(ContentContext& context) = 0;
};

// Where a worker is in its life.
enum class ContentWorkerState : std::uint8_t {
    running,
    // run returned and the program was destroyed.
    finished,
    // run threw; failure() says what.
    failed,
    // A stop did not finish within its bounded wait: the thread is detached
    // and the channel revoked.
    abandoned,
};

[[nodiscard]] GUI_FORMS_API std::string_view content_worker_state_name(ContentWorkerState state) noexcept;

// The threads abandoned in this process that have not yet finished: a
// diagnostic for shutdown reports. Any thread may read it.
[[nodiscard]] GUI_FORMS_API std::uint64_t abandoned_content_workers() noexcept;

// Owns one thread running one ContentProgram with one sink. Made, used and
// destroyed by one thread (the interface thread for a surface); the program
// runs on the worker's thread and shares nothing with its owner but the
// channel and the worker's completion state, both synchronized.
class GUI_FORMS_API ContentWorker final {
public:
    // Starts the thread. Null program or an empty sink is
    // std::invalid_argument; a thread the system refuses is
    // std::system_error, and the program is released then.
    ContentWorker(std::unique_ptr<ContentProgram> program, ContentFrameSink sink);
    // stop(default_content_termination_grace): never waits longer.
    ~ContentWorker();
    ContentWorker(const ContentWorker&) = delete;
    ContentWorker& operator=(const ContentWorker&) = delete;

    // Asks the program to stop (its context's stop token fires) and returns
    // at once. Idempotent.
    void request_stop() noexcept;
    // Whether the program's thread has ended (finished or failed). Never
    // blocks.
    [[nodiscard]] bool done() const noexcept;
    // Joins a thread that is done, without waiting for one that is not.
    // Returns whether it joined (or had already).
    bool join_if_done() noexcept;
    // Abandons at once, without waiting: revokes the channel and detaches
    // the thread. Nothing for a thread already done (it is joined instead).
    void abandon() noexcept;
    // request_stop, then waits at most `grace` (capped at
    // maximum_content_termination_grace) for the thread to end: joins it and
    // reports finished or failed, or abandons it and reports abandoned. The
    // only blocking member, and its wait is bounded. Idempotent: a later call
    // reports the same state.
    ContentWorkerState stop(std::chrono::milliseconds grace) noexcept;

    [[nodiscard]] ContentWorkerState state() const noexcept;
    // What run threw, for state failed; empty otherwise.
    [[nodiscard]] std::string failure() const;

private:
    std::shared_ptr<detail::ContentWorkerShared> shared_;
    std::thread thread_;
};

} // namespace gui_forms
