// stx_fontpack: the two default capabilities (runtime.hpp).
#include "stx_fontpack/runtime.hpp"

#include <cstdlib>

namespace stx_fontpack {

namespace {

void* standard_allocate(void* context, std::size_t size) {
    static_cast<void>(context);
    return std::malloc(size);
}

void standard_release(void* context, void* memory, std::size_t size) {
    static_cast<void>(context);
    static_cast<void>(size);
    std::free(memory);
}

void serial_run_workers(void* context, const WorkerTask& task) {
    static_cast<void>(context);
    for (std::uint32_t worker = 0; worker < task.worker_count; worker += 1) {
        task.run(task.context, worker);
    }
}

}  // namespace

Allocator standard_allocator() noexcept {
    Allocator allocator;
    allocator.allocate = standard_allocate;
    allocator.release = standard_release;
    allocator.context = nullptr;
    return allocator;
}

Executor serial_executor() noexcept {
    Executor executor;
    executor.run_workers = serial_run_workers;
    executor.context = nullptr;
    executor.worker_count = 1;
    return executor;
}

}  // namespace stx_fontpack
