// gui_forms::AtomicThreadPool and gui_forms::Worker (gui_forms/threading.hpp)
// over STXlib's standard augmentation (owner, 2026-11-16: worker threads and
// the atomic pool live in STXlib): stxlib::AtomicPool, the owner's
// threadpool_atomic_fast algorithm with its claim sizes, memory orders and
// lock order (stxlib/thread_pool.hpp), and stxlib::Thread
// (stxlib/threads.hpp). The C copy GUI.Forms kept under
// src/core/threading/atomic_pool/ is gone; this file keeps GUI.Forms'
// throwing interface over STXlib's results.
//
// Thread records: every STXlib thread, pool workers included, ends its
// STXlib thread records on the thread when its body ends (threads.hpp,
// "Thread records"), outside the loader lock a Windows thread's exit runs
// under, as gui_forms_atomic_pool_thread_ending did here before.

#include "gui_forms/threading.hpp"

#include <stxlib/error.hpp>
#include <stxlib/result.hpp>
#include <stxlib/text_view.hpp>
#include <stxlib/thread_pool.hpp>
#include <stxlib/threads.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>

#include <cstddef>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {

void CancellationFlag::request() noexcept {
    requested_.store(true, std::memory_order_release);
}

bool CancellationFlag::requested() const noexcept {
    const bool result = requested_.load(std::memory_order_acquire);
    return result;
}

namespace {

// STXlib's refusal as GUI.Forms' exception: exhaustion as std::bad_alloc,
// anything else as std::runtime_error naming the operation and the record's
// code. A null record throws nothing.
void throw_failure(const stxlib::ErrorRecord* const record) {
    if (record == nullptr) return;
    const stxlib::ErrorRecord& failure = *record;
    if (failure.kind() == stxlib::ErrorKind::out_of_memory) throw std::bad_alloc();
    const stxlib::TextView operation = failure.operation().name();
    std::string message = "atomic pool operation failed: ";
    message.append(operation.data(), static_cast<std::size_t>(operation.count()));
    message.append(" (code ");
    message.append(std::to_string(failure.code()));
    message.append(")");
    throw std::runtime_error(message);
}

void check_status(const stxlib::Status& status) {
    if (!status.failed()) return;
    throw_failure(status.error());
}

} // namespace

// The pool and the reusable task table run hands it. `running` is true while
// run is inside the pool on the owner's thread, so a task run on the caller
// that calls run again is refused here (logic_error) before STXlib would end
// the process for it (PoolFailure::batch_still_open).
struct AtomicThreadPool::State final {
    stxlib::AtomicPool pool{};
    std::vector<stxlib::PoolTask> tasks{};
    bool running{false};
};

AtomicThreadPool::AtomicThreadPool(const std::uint32_t threads) : state_(std::make_unique<State>()) {
    stxlib::Result<stxlib::AtomicPool> created = stxlib::AtomicPool::create(threads);
    if (created.failed()) throw_failure(created.error());
    (*state_).pool = static_cast<stxlib::Result<stxlib::AtomicPool>&&>(created).take_value();
}

AtomicThreadPool::~AtomicThreadPool() = default;

std::uint32_t AtomicThreadPool::thread_count() const noexcept {
    const State& state = *state_;
    const std::uint32_t result = state.pool.thread_count();
    return result;
}

void AtomicThreadPool::run(const std::span<const AtomicTask> tasks) {
    if (tasks.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("atomic pool batch exceeds uint32_t");
    for (const AtomicTask& task : tasks) {
        if (task.entry == nullptr) throw std::invalid_argument("atomic pool entry is null");
    }
    State& state = *state_;
    if (state.running || state.pool.is_worker())
        throw std::logic_error("an atomic pool task cannot reenter its pool");
    state.tasks.resize(tasks.size());
    for (std::size_t index = 0; index < tasks.size(); ++index) {
        state.tasks[index] = stxlib::PoolTask{.function = tasks[index].entry, .argument = tasks[index].context};
    }
    const stxlib::View<stxlib::PoolTask> batch(state.tasks.data(), static_cast<stxlib::usize>(state.tasks.size()));
    state.running = true;
    const stxlib::Status result = state.pool.run(batch);
    state.running = false;
    check_status(result);
}

// One STXlib thread running the entry. The thread borrows this state until
// it is joined; the Thread is the last member, so it is destroyed (and
// joined, if wait never ran) before what it borrows. An entry's exception
// stays on the thread and is rethrown by join on the owner's.
struct Worker::State final {
    State(const Entry function, void* const address) : entry(function), context(address) {
        if (entry == nullptr) throw std::invalid_argument("Worker requires a named entry");
        stxlib::Result<stxlib::Thread> started = stxlib::Thread::start(State::run, this);
        if (started.failed()) throw_failure(started.error());
        thread = static_cast<stxlib::Result<stxlib::Thread>&&>(started).take_value();
    }
    ~State() {
        cancellation.request();
        try {
            wait();
        } catch (...) {
            std::terminate();
        }
    }
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&&) = delete;
    State& operator=(State&&) = delete;

    void wait() {
        if (joined) return;
        const stxlib::JoinResult result = thread.join();
        if (result == stxlib::JoinResult::self_join) throw std::logic_error("Worker cannot join itself");
        joined = true;
    }

    // The thread's body. STXlib's stop signal is not used: the entry reads
    // the worker's own CancellationFlag.
    static void run(const stxlib::StopSignal& stop, void* const address) noexcept {
        static_cast<void>(stop);
        State& state = *static_cast<State*>(address);
        try {
            state.entry(state.cancellation, state.context);
        } catch (...) {
            state.failure = std::current_exception();
        }
    }

    CancellationFlag cancellation{};
    Entry entry{nullptr};
    void* context{nullptr};
    std::exception_ptr failure{};
    bool joined{false};
    // Declared last (see above).
    stxlib::Thread thread{};
};

Worker::Worker(const Entry entry, void* const context) : state_(std::make_unique<State>(entry, context)) {}
Worker::~Worker() = default;

void Worker::request_cancel() noexcept {
    State& state = *state_;
    state.cancellation.request();
}

void Worker::join() {
    State& state = *state_;
    state.wait();
    const std::exception_ptr failure = std::exchange(state.failure, {});
    if (failure) std::rethrow_exception(failure);
}

} // namespace gui_forms
