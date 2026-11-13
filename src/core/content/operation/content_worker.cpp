#include "gui_forms/content/operation/content_operation.hpp"

#include "../channel/content_channel.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace gui_forms {

namespace detail {

// What a worker's thread shares with its owner. The thread holds it by
// shared ownership, so an abandoned thread keeps it (and the program and the
// channel in it) alive after the owner is gone. `program` is touched only by
// the worker's thread once it starts; everything below `mutex` is guarded by
// it, and `finished` is signalled when `done` is set.
struct ContentWorkerShared final {
    std::unique_ptr<ContentProgram> program{};
    ContentFrameSink sink{};
    std::shared_ptr<ContentChannel> channel{};
    mutable std::mutex mutex{};
    std::condition_variable finished{};
    bool done{};
    bool abandoned{};
    ContentWorkerState state{ContentWorkerState::running};
    std::string failure{};
};

} // namespace detail

namespace {

// Threads abandoned and still running, process-wide. A constant-initialized
// atomic of a trivial type: no destructor runs at exit, so an abandoned
// thread that ends during static destruction may still decrement it.
std::atomic<std::uint64_t> abandoned_worker_count{0U};

// The worker thread's body: runs the program, destroys it on this thread,
// then records the outcome and signals. An abandoned worker's outcome is not
// recorded (its owner already reported abandoned); it only leaves the count.
class ContentWorkerEntry final {
public:
    explicit ContentWorkerEntry(std::shared_ptr<detail::ContentWorkerShared> shared) noexcept
        : shared_(std::move(shared)) {}

    void operator()() const noexcept {
        detail::ContentWorkerShared& shared = *shared_;
        ContentWorkerState outcome = ContentWorkerState::finished;
        std::string failure{};
        // Exceptions end at this boundary: they never cross into the owner's
        // thread, and std::thread would terminate the process on one.
        try {
            ContentContext context(shared.sink);
            (*shared.program).run(context);
        } catch (const std::exception& error) {
            outcome = ContentWorkerState::failed;
            failure = error.what();
        } catch (...) {
            outcome = ContentWorkerState::failed;
            failure = "the content program threw a value that is not a std::exception";
        }
        // The program's destructor runs here, inside the bounded wait.
        shared.program.reset();
        bool abandoned = false;
        {
            const std::scoped_lock lock(shared.mutex);
            shared.done = true;
            abandoned = shared.abandoned;
            if (!abandoned) {
                shared.state = outcome;
                shared.failure = std::move(failure);
            }
        }
        shared.finished.notify_all();
        if (abandoned) {
            abandoned_worker_count.fetch_sub(1U, std::memory_order_acq_rel);
            return;
        }
        // Tells the surface without waiting for its next check. A notice
        // that cannot be posted is not an error: the check finds the state.
        try {
            detail::notify_content_listener(*shared.channel);
        } catch (...) {
            static_cast<void>(0);
        }
    }

private:
    std::shared_ptr<detail::ContentWorkerShared> shared_;
};

} // namespace

std::string_view content_worker_state_name(ContentWorkerState state) noexcept {
    switch (state) {
    case ContentWorkerState::running: return "running";
    case ContentWorkerState::finished: return "finished";
    case ContentWorkerState::failed: return "failed";
    case ContentWorkerState::abandoned: return "abandoned";
    }
    return "unknown";
}

std::uint64_t abandoned_content_workers() noexcept {
    const std::uint64_t count = abandoned_worker_count.load(std::memory_order_acquire);
    return count;
}

ContentContext::ContentContext(ContentFrameSink sink) noexcept : sink_(std::move(sink)) {}

std::stop_token ContentContext::stop_token() const noexcept {
    std::stop_token token = sink_.stop_token();
    return token;
}

bool ContentContext::stop_requested() const noexcept {
    const bool result = sink_.stop_requested();
    return result;
}

ContentRequest ContentContext::wait_for_request(std::uint64_t seen_generation, std::chrono::milliseconds timeout) const {
    const ContentRequest result = sink_.wait_for_request(seen_generation, timeout);
    return result;
}

ContentWorker::ContentWorker(std::unique_ptr<ContentProgram> program, ContentFrameSink sink) {
    if (!program) throw std::invalid_argument("ContentWorker needs a program");
    std::shared_ptr<detail::ContentChannel> channel = detail::ContentSinkAccess::channel(sink);
    if (!channel) throw std::invalid_argument("ContentWorker needs an open sink");
    shared_ = std::make_shared<detail::ContentWorkerShared>();
    (*shared_).program = std::move(program);
    (*shared_).sink = std::move(sink);
    (*shared_).channel = std::move(channel);
    const ContentWorkerEntry entry(shared_);
    // std::system_error escapes when the system refuses the thread; the
    // program is released with shared_ then.
    thread_ = std::thread(entry);
}

ContentWorker::~ContentWorker() {
    static_cast<void>(stop(default_content_termination_grace));
}

void ContentWorker::request_stop() noexcept {
    if (!shared_) return;
    detail::request_content_stop(*(*shared_).channel);
}

bool ContentWorker::done() const noexcept {
    if (!shared_) return true;
    const std::scoped_lock lock((*shared_).mutex);
    const bool result = (*shared_).done;
    return result;
}

bool ContentWorker::join_if_done() noexcept {
    if (!thread_.joinable()) return true;
    if (!done()) return false;
    // The thread has set done and returns without taking another lock, so
    // the join waits only for its return.
    try {
        thread_.join();
    } catch (const std::system_error&) {
        thread_.detach();
    }
    return true;
}

void ContentWorker::abandon() noexcept {
    if (!shared_ || !thread_.joinable()) return;
    {
        const std::scoped_lock lock((*shared_).mutex);
        if (!(*shared_).done) {
            (*shared_).abandoned = true;
            (*shared_).state = ContentWorkerState::abandoned;
            abandoned_worker_count.fetch_add(1U, std::memory_order_acq_rel);
        }
    }
    if (join_if_done()) return;
    // Nothing the thread does from now on reaches what the interface reads.
    detail::revoke_content_channel(*(*shared_).channel);
    thread_.detach();
}

ContentWorkerState ContentWorker::stop(std::chrono::milliseconds grace) noexcept {
    if (!shared_) return ContentWorkerState::finished;
    request_stop();
    if (thread_.joinable()) {
        const std::chrono::milliseconds bounded =
            std::clamp(grace, std::chrono::milliseconds(0), maximum_content_termination_grace);
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + bounded;
        {
            std::unique_lock<std::mutex> lock((*shared_).mutex);
            while (!(*shared_).done) {
                const std::cv_status status = (*shared_).finished.wait_until(lock, deadline);
                if (status == std::cv_status::timeout) break;
            }
        }
        if (!join_if_done()) abandon();
    }
    const ContentWorkerState result = state();
    return result;
}

ContentWorkerState ContentWorker::state() const noexcept {
    if (!shared_) return ContentWorkerState::finished;
    const std::scoped_lock lock((*shared_).mutex);
    const ContentWorkerState result = (*shared_).state;
    return result;
}

std::string ContentWorker::failure() const {
    if (!shared_) return std::string{};
    const std::scoped_lock lock((*shared_).mutex);
    std::string result = (*shared_).failure;
    return result;
}

} // namespace gui_forms
