// stx_fontpack tools and tests: an Executor over std::thread.
//
// The library starts no threads; a program that wants workers lends them.
// This one starts worker_count - 1 threads for each fork-join step, runs
// worker 0 on the calling thread and joins the rest, which is all the packer
// tool and the tests need. A product passes its own thread pool instead.
// std::thread's constructor throws std::system_error when the system cannot
// start a thread; the tool and the tests let that end the program.
#pragma once

#include "stx_fontpack/runtime.hpp"

#include <cstdint>
#include <thread>
#include <vector>

namespace stx_fontpack_tools {

inline void thread_run_one(const stx_fontpack::WorkerTask* task, std::uint32_t worker) {
    (*task).run((*task).context, worker);
}

inline void thread_run_workers(void* context, const stx_fontpack::WorkerTask& task) {
    static_cast<void>(context);
    if (task.worker_count == 0) {
        return;
    }
    std::vector<std::thread> threads;
    threads.reserve(task.worker_count - 1);
    for (std::uint32_t worker = 1; worker < task.worker_count; worker += 1) {
        threads.push_back(std::thread(thread_run_one, &task, worker));
    }
    thread_run_one(&task, 0);
    for (std::size_t k = 0; k < threads.size(); k += 1) {
        threads[k].join();
    }
}

// worker_count 0 means the hardware's thread count (at least 1, at most kMaxWorkers).
inline stx_fontpack::Executor thread_executor(std::uint32_t worker_count) {
    if (worker_count == 0) {
        worker_count = std::thread::hardware_concurrency();
    }
    if (worker_count == 0) {
        worker_count = 1;
    }
    if (worker_count > stx_fontpack::kMaxWorkers) {
        worker_count = stx_fontpack::kMaxWorkers;
    }
    stx_fontpack::Executor executor;
    executor.run_workers = thread_run_workers;
    executor.context = nullptr;
    executor.worker_count = worker_count;
    return executor;
}

}  // namespace stx_fontpack_tools
