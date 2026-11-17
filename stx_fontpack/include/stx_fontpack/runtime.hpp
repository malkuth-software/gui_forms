#pragma once

#include <cstddef>
#include <cstdint>

namespace stx_fontpack {

// The two capabilities a caller lends the package: memory and workers. The
// package keeps no global state, starts no threads and calls no global
// allocator on the load path; everything it holds comes from the Allocator
// it is given and goes back to the same one.

// Memory. allocate returns size bytes aligned to at least
// alignof(std::max_align_t), or null when it cannot; size is never 0.
// release takes back exactly what one allocate returned, with the same size;
// it is never called with null. Both may be called from several workers at
// once, so an implementation shared by workers must be safe for that.
struct Allocator {
    void* (*allocate)(void* context, std::size_t size);
    void (*release)(void* context, void* memory, std::size_t size);
    void* context;
};

// std::malloc and std::free, for tools and tests. Thread safe.
[[nodiscard]] Allocator standard_allocator() noexcept;

// One fork-join step. run is called once for each worker in
// [0, worker_count) with that worker's index; the index names the worker's
// private scratch, so no two calls with the same index may overlap.
struct WorkerTask {
    void (*run)(void* context, std::uint32_t worker);
    void* context;
    std::uint32_t worker_count;
};

// Workers. run_workers performs every call of task (see WorkerTask), on any
// threads, at the same time or one after another, and returns only after the
// last call has returned. No call waits for another, so an executor that runs
// them in turn on the calling thread is correct, only slower. worker_count
// says how many calls may usefully run at once (1 to kMaxWorkers). A task
// never calls back into its executor. Everything the caller wrote before
// run_workers happens before every call, and everything a call wrote happens
// before run_workers returns (a join, as std::thread::join gives).
struct Executor {
    void (*run_workers)(void* context, const WorkerTask& task);
    void* context;
    std::uint32_t worker_count;
};

// Runs every call in turn on the calling thread; worker_count 1.
[[nodiscard]] Executor serial_executor() noexcept;

}  // namespace stx_fontpack
