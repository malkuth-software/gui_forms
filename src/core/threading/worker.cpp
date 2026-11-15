#include "gui_forms/threading.hpp"
#include "atomic_pool/atomic_pool.h"

#include <stxlib/thread_records.hpp>

#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

// Each pool thread's last act (atomic_pool.h): its STXlib thread records
// (every library's) are released here, on the thread, rather than by the
// exit hook, which on Windows runs under the loader lock.
extern "C" void gui_forms_atomic_pool_thread_ending() {
    stxlib::end_thread_records();
}

namespace gui_forms {

void CancellationFlag::request() noexcept {
    requested_.store(true, std::memory_order_release);
}

bool CancellationFlag::requested() const noexcept {
    const bool result = requested_.load(std::memory_order_acquire);
    return result;
}

namespace {
void check_pool_result(const gui_forms_atomic_pool_result_t result) {
    if (result == THREADPOOL_OK) return;
    if (result == THREADPOOL_ALLOCATION_FAILED) throw std::bad_alloc();
    throw std::runtime_error("threadpool_atomic_fast operation failed: " +
        std::to_string(static_cast<int>(result)));
}
} // namespace

struct AtomicThreadPool::State final {
    explicit State(const std::uint32_t threads) {
        const gui_forms_atomic_pool_result_t result =
            gui_forms_atomic_pool_create_checked(threads, &pool);
        check_pool_result(result);
    }
    ~State() { gui_forms_atomic_pool_destroy(pool); }
    gui_forms_atomic_pool_t* pool{nullptr};
    std::vector<gui_forms_atomic_pool_task_t> tasks{};
    bool running{false};
};

AtomicThreadPool::AtomicThreadPool(const std::uint32_t threads)
    : state_(std::make_unique<State>(threads)) {}
AtomicThreadPool::~AtomicThreadPool() = default;

std::uint32_t AtomicThreadPool::thread_count() const noexcept {
    const State& state = *state_;
    const std::uint32_t result = gui_forms_atomic_pool_thread_count(state.pool);
    return result;
}

void AtomicThreadPool::run(const std::span<const AtomicTask> tasks) {
    if (tasks.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("atomic pool batch exceeds uint32_t");
    for (const AtomicTask& task : tasks) {
        if (task.entry == nullptr) throw std::invalid_argument("atomic pool entry is null");
    }
    State& state = *state_;
    if (state.running || gui_forms_atomic_pool_is_worker(state.pool) != 0)
        throw std::logic_error("an atomic pool task cannot reenter its pool");
    state.tasks.resize(tasks.size());
    for (std::size_t index = 0; index < tasks.size(); ++index) {
        state.tasks[index] = {tasks[index].entry, tasks[index].context};
    }
    state.running = true;
    const gui_forms_atomic_pool_result_t result = gui_forms_atomic_pool_run_checked(
        state.pool, state.tasks.data(), static_cast<std::uint32_t>(state.tasks.size()));
    state.running = false;
    check_pool_result(result);
}

struct Worker::State final {
    State(const Entry function, void* const address) : entry(function), context(address) {
        if (entry == nullptr) throw std::invalid_argument("Worker requires a named entry");
        gui_forms_atomic_pool_result_t result = gui_forms_atomic_pool_create_checked(1, &pool);
        check_pool_result(result);
        task = {State::run, this};
        result = gui_forms_atomic_pool_begin_checked(pool, &task, 1);
        if (result != THREADPOOL_OK) {
            gui_forms_atomic_pool_destroy(pool);
            pool = nullptr;
            check_pool_result(result);
        }
    }
    ~State() {
        cancellation.request();
        try { wait(); } catch (...) { std::terminate(); }
        gui_forms_atomic_pool_destroy(pool);
    }
    void wait() {
        if (joined) return;
        if (gui_forms_atomic_pool_is_worker(pool) != 0)
            throw std::logic_error("Worker cannot join itself");
        const gui_forms_atomic_pool_result_t result = gui_forms_atomic_pool_wait_checked(pool);
        check_pool_result(result);
        gui_forms_atomic_pool_destroy(pool);
        pool = nullptr;
        joined = true;
    }
    static void run(void* const address) noexcept {
        State& state = *static_cast<State*>(address);
        try { state.entry(state.cancellation, state.context); }
        catch (...) { state.failure = std::current_exception(); }
    }
    CancellationFlag cancellation{};
    Entry entry{nullptr};
    void* context{nullptr};
    std::exception_ptr failure{};
    gui_forms_atomic_pool_t* pool{nullptr};
    gui_forms_atomic_pool_task_t task{};
    bool joined{false};
};

Worker::Worker(const Entry entry, void* const context)
    : state_(std::make_unique<State>(entry, context)) {}
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
