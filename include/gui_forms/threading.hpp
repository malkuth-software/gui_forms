#pragma once

#include "gui_forms/export/threading_export.hpp"

#include <atomic>
#include <memory>
#include <span>
#include <cstdint>

namespace gui_forms {

// Cooperative cancellation. A request is permanent; observing it acquires
// writes preceding request(). It does not interrupt I/O or wake a condition.
class GUI_FORMS_THREADING_API CancellationFlag final {
public:
    CancellationFlag() noexcept = default;
    CancellationFlag(const CancellationFlag&) = delete;
    CancellationFlag& operator=(const CancellationFlag&) = delete;
    void request() noexcept;
    [[nodiscard]] bool requested() const noexcept;
private:
    std::atomic<bool> requested_{false};
};

struct AtomicTask final {
    void (*entry)(void* context) noexcept{nullptr};
    void* context{nullptr};
};

// The owner's threadpool_atomic_fast algorithm: synchronous batches, reusable
// task storage, native pthread workers. Small batches may run on the caller.
// Methods belong to one owner thread; entries borrow contexts until run returns.
class GUI_FORMS_THREADING_API AtomicThreadPool final {
public:
    explicit AtomicThreadPool(std::uint32_t threads);
    ~AtomicThreadPool();
    AtomicThreadPool(const AtomicThreadPool&) = delete;
    AtomicThreadPool& operator=(const AtomicThreadPool&) = delete;
    AtomicThreadPool(AtomicThreadPool&&) = delete;
    AtomicThreadPool& operator=(AtomicThreadPool&&) = delete;
    [[nodiscard]] std::uint32_t thread_count() const noexcept;
    void run(std::span<const AtomicTask> tasks);
private:
    struct State;
    std::unique_ptr<State> state_{};
};

// One asynchronous task on threadpool_atomic_fast. The named entry borrows
// context until join or destruction finishes. Declare the worker AFTER its
// context owner. Only request_cancel is concurrent-safe; join/destruction
// belong to the creating thread, never to the worker itself.
class GUI_FORMS_THREADING_API Worker final {
public:
    using Entry = void (*)(const CancellationFlag& cancellation, void* context);
    explicit Worker(Entry entry, void* context);
    ~Worker();
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    Worker(Worker&&) = delete;
    Worker& operator=(Worker&&) = delete;

    void request_cancel() noexcept;
    // Waits without requesting cancellation. Rethrows an entry failure once,
    // after joining. Repeated joins are harmless. Self-join throws logic_error.
    void join();
private:
    struct State;
    std::unique_ptr<State> state_{};
};

} // namespace gui_forms
