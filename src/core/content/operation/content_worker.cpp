#include "gui_forms/content/operation/content_operation.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include "../channel/content_channel.hpp"

#include <stxlib/clocks.hpp>
#include <stxlib/error.hpp>
#include <stxlib/result.hpp>
#include <stxlib/stop_token.hpp>
#include <stxlib/threads.hpp>

#include <algorithm>
#include <atomic>
#include <exception>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

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
    mutable stxlib::Mutex mutex{};
    stxlib::ConditionVariable finished{};
    bool done{};
    bool abandoned{};
    // abandon() moved the thread's start block under abandoned_worker_root
    // (the thread then frees it through the root).
    bool start_in_root{};
    ContentWorkerState state{ContentWorkerState::running};
    std::string failure{};
};

// What a worker's thread starts from and owns: the worker's shared state.
// Made by ContentWorker's constructor and handed to the thread when it
// starts (a refused start frees it at once). From then on it belongs to the
// thread, which frees it when it returns, except while the worker is
// abandoned: abandon() moves it under abandoned_worker_root, and the
// returning thread takes it out of the root and frees it there.
struct ContentWorkerStart final {
    std::shared_ptr<detail::ContentWorkerShared> shared{};
};

// The thread a worker runs on: an STXlib thread (stxlib::Thread, the
// standard augmentation) on every host, so the only state the thread holds
// is ours and STXlib's (its start block and STXlib's thread state, both
// released when it returns; std::thread's runtime would add a per-thread
// record that only the thread's own storage refers to, which a leak checker
// reports for a thread abandoned until exit). STXlib ends the thread's
// records on the thread when its body ends (stxlib/threads.hpp, "Thread
// records"). `joinable` is true from a successful start until the thread is
// joined or abandoned. `start` observes the thread's start block, which the
// thread owns (or the root, once abandoned); read by the owner only under
// the shared state's mutex while the thread has not set done.
struct ContentWorkerThread final {
    stxlib::Thread thread{};
    ContentWorkerStart* start{};
    bool joinable{};
};

} // namespace detail

namespace {

using detail::ContentWorkerStart;

// stop's wait predicate, under the worker's mutex: the thread set done.
bool worker_done(const void* const context) noexcept {
    const detail::ContentWorkerShared& shared = *static_cast<const detail::ContentWorkerShared*>(context);
    return shared.done;
}

// Threads abandoned and still running, process-wide. A constant-initialized
// atomic of a trivial type: no destructor runs at exit, so an abandoned
// thread that ends during static destruction may still decrement it.
std::atomic<std::uint64_t> abandoned_worker_count{0U};

// The abandoned workers' start blocks, their threads still running: a
// process-lifetime root, created on first use and deliberately never
// destroyed or freed. An abandoned thread is left to the operating system,
// which reclaims it, its start block and its program at process exit (the
// answer STXlib gives for a record never released). Holding each start
// block here keeps all the thread owns reachable from a named root, so a
// leak checker sees the abandonment as the deliberate ownership it is
// rather than lost memory. The vector and its mutex live in the leaked
// object, so threads that end during static destruction still find them.
struct AbandonedWorkerRoot final {
    stxlib::Mutex mutex{};
    std::vector<std::unique_ptr<ContentWorkerStart>> starts{};
};

AbandonedWorkerRoot& abandoned_worker_root() {
    static AbandonedWorkerRoot* const root = new AbandonedWorkerRoot();
    return *root;
}

// Moves an abandoned worker's start block under the root. Returns false,
// leaving it with its thread, when the root cannot grow; the block is never
// freed here.
bool keep_abandoned_start(ContentWorkerStart* start) noexcept {
    AbandonedWorkerRoot& root = abandoned_worker_root();
    const stxlib::ScopedLock lock(root.mutex);
    try {
        root.starts.reserve(root.starts.size() + 1U);
    } catch (...) {
        return false;
    }
    // Within the reserved capacity: no reallocation, nothing throws.
    root.starts.emplace_back(start);
    return true;
}

struct SameStart final {
    const ContentWorkerStart* start{};
    bool operator()(const std::unique_ptr<ContentWorkerStart>& held) const noexcept {
        return held.get() == start;
    }
};

// Takes a returning abandoned thread's start block out of the root and
// frees it.
void release_abandoned_start(const ContentWorkerStart* start) noexcept {
    std::unique_ptr<ContentWorkerStart> released{};
    {
        AbandonedWorkerRoot& root = abandoned_worker_root();
        const stxlib::ScopedLock lock(root.mutex);
        const std::vector<std::unique_ptr<ContentWorkerStart>>::iterator found =
            std::find_if(root.starts.begin(), root.starts.end(), SameStart{start});
        if (found != root.starts.end()) {
            released = std::move(*found);
            root.starts.erase(found);
        }
    }
    // Freed outside the root's lock.
    released.reset();
}

// The worker thread's body: runs the program, destroys it on this thread,
// then records the outcome and signals. An abandoned worker's outcome is not
// recorded (its owner already reported abandoned); it only leaves the count.
// Frees `start` last, through the root when the worker was abandoned and its
// block is there.
void run_content_worker(ContentWorkerStart* start) noexcept {
    // Kept by this frame: the start block may be freed before the end.
    const std::shared_ptr<detail::ContentWorkerShared> held = (*start).shared;
    detail::ContentWorkerShared& shared = *held;
    ContentWorkerState outcome = ContentWorkerState::finished;
    std::string failure{};
    // Exceptions end at this boundary: they never cross into the owner's
    // thread, and a thread's entry would terminate the process on one.
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
    bool start_in_root = false;
    {
        const stxlib::ScopedLock lock(shared.mutex);
        shared.done = true;
        abandoned = shared.abandoned;
        start_in_root = shared.start_in_root;
        if (!abandoned) {
            shared.state = outcome;
            shared.failure = std::move(failure);
        }
    }
    shared.finished.notify_all();
    if (start_in_root) {
        release_abandoned_start(start);
    } else {
        std::unique_ptr<ContentWorkerStart> owned(start);
        owned.reset();
    }
    if (abandoned) {
        abandoned_worker_count.fetch_sub(1U, std::memory_order_acq_rel);
        return;
    }
    // Tells the surface without waiting for its next check. A notice that
    // cannot be posted is not an error: the check finds the state.
    try {
        detail::notify_content_listener(*shared.channel);
    } catch (...) {
        static_cast<void>(0);
    }
}

// The STXlib thread's body: runs the worker on its start block (which it
// owns from here). STXlib's stop signal is not used: a stop reaches the
// program through its channel (request_content_stop).
void content_worker_thread_main(const stxlib::StopSignal& stop, void* argument) noexcept {
    static_cast<void>(stop);
    run_content_worker(static_cast<ContentWorkerStart*>(argument));
}

// Gives a started thread up: it runs until its code returns or the process
// exits, and STXlib releases its thread state when it returns.
void detach_thread(detail::ContentWorkerThread& thread) noexcept {
    if (!thread.joinable) return;
    thread.joinable = false;
    static_cast<void>(thread.thread.abandon());
}

// Joins a started thread. A join STXlib refuses (only a thread joining
// itself, which a content program cannot do through its own worker) gives
// the thread up instead.
void join_thread(detail::ContentWorkerThread& thread) noexcept {
    if (!thread.joinable) return;
    const stxlib::JoinResult joined = thread.thread.join();
    if (joined == stxlib::JoinResult::joined) {
        thread.joinable = false;
        return;
    }
    detach_thread(thread);
}

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

stxlib::StopToken ContentContext::stop_token() const noexcept {
    stxlib::StopToken token = sink_.stop_token();
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
    shared_ = gui_forms::detail::make_library_shared<detail::ContentWorkerShared>();
    (*shared_).program = std::move(program);
    (*shared_).sink = std::move(sink);
    (*shared_).channel = std::move(channel);
    thread_ = std::make_unique<detail::ContentWorkerThread>();
    // std::system_error escapes when the system refuses the thread
    // (std::bad_alloc when STXlib's allocator is exhausted); the program is
    // released with shared_ then.
    std::unique_ptr<ContentWorkerStart> start = std::make_unique<ContentWorkerStart>();
    (*start).shared = shared_;
    stxlib::OperationResult<stxlib::Thread> started = stxlib::Thread::start(&content_worker_thread_main, start.get());
    if (started.failed()) {
        // The thread never ran: the start block is still ours, freed here.
        if ((*started.error()).kind() == stxlib::ErrorKind::out_of_memory) throw std::bad_alloc();
        throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again),
                                "ContentWorker could not start its thread");
    }
    (*thread_).thread = static_cast<stxlib::OperationResult<stxlib::Thread>&&>(started).take_value();
    // The thread owns the start block from here (run_content_worker).
    (*thread_).start = start.release();
    (*thread_).joinable = true;
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
    const stxlib::ScopedLock lock((*shared_).mutex);
    const bool result = (*shared_).done;
    return result;
}

bool ContentWorker::join_if_done() noexcept {
    if (!thread_ || !(*thread_).joinable) return true;
    if (!done()) return false;
    // The thread has set done and returns without taking another lock (a
    // worker abandoned meanwhile takes the root's, briefly), so the join
    // waits only for its return.
    join_thread(*thread_);
    return true;
}

void ContentWorker::abandon() noexcept {
    if (!shared_ || !thread_ || !(*thread_).joinable) return;
    {
        const stxlib::ScopedLock lock((*shared_).mutex);
        if (!(*shared_).done) {
            (*shared_).abandoned = true;
            (*shared_).state = ContentWorkerState::abandoned;
            abandoned_worker_count.fetch_add(1U, std::memory_order_acq_rel);
            // Left to the operating system under a named root: the thread's
            // start block moves under abandoned_worker_root. Done under the
            // worker's mutex, so the thread, which reads start_in_root under
            // it after setting done, frees the block through the root only
            // after it was put there, and itself otherwise. Lock order: the
            // worker's mutex, then the root's; the thread never holds both.
            ContentWorkerStart* const start = (*thread_).start;
            (*shared_).start_in_root = keep_abandoned_start(start);
        }
    }
    if (join_if_done()) return;
    // Nothing the thread does from now on reaches what the interface reads.
    detail::revoke_content_channel(*(*shared_).channel);
    detach_thread(*thread_);
}

ContentWorkerState ContentWorker::stop(std::chrono::milliseconds grace) noexcept {
    if (!shared_) return ContentWorkerState::finished;
    request_stop();
    if (thread_ && (*thread_).joinable) {
        const std::chrono::milliseconds bounded =
            std::clamp(grace, std::chrono::milliseconds(0), maximum_content_termination_grace);
        {
            stxlib::ScopedLock lock((*shared_).mutex);
            static_cast<void>((*shared_).finished.wait_for(lock, &worker_done, shared_.get(),
                                                           stxlib::Duration::milliseconds(bounded.count())));
        }
        if (!join_if_done()) abandon();
    }
    const ContentWorkerState result = state();
    return result;
}

ContentWorkerState ContentWorker::state() const noexcept {
    if (!shared_) return ContentWorkerState::finished;
    const stxlib::ScopedLock lock((*shared_).mutex);
    const ContentWorkerState result = (*shared_).state;
    return result;
}

std::string ContentWorker::failure() const {
    if (!shared_) return std::string{};
    const stxlib::ScopedLock lock((*shared_).mutex);
    std::string result = (*shared_).failure;
    return result;
}

} // namespace gui_forms
