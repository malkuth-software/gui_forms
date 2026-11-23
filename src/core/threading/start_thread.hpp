#ifndef GUI_FORMS_CORE_THREADING_START_THREAD_HPP
#define GUI_FORMS_CORE_THREADING_START_THREAD_HPP

// Starting a library thread on STXlib (stxlib/threads.hpp; the shield,
// 2026-11-22: every thread GUI.Forms starts is an stxlib::Thread). The
// thread ends its STXlib thread records itself when its entry returns
// (threads.hpp, "Thread records"), so an entry needs no ThreadRecordsEnd.
//
// start_library_thread fails as std::thread's constructor did, so callers
// keep their documented failures: std::bad_alloc when STXlib's allocator is
// exhausted, std::system_error (resource_unavailable_try_again) when the
// host refuses the thread. A started thread's entry must not throw (it is
// noexcept: an escaping exception ends the process, as it did from a
// std::thread's function).

#include <stxlib/error.hpp>
#include <stxlib/result.hpp>
#include <stxlib/threads.hpp>

#include <new>
#include <system_error>

namespace gui_forms::detail {

[[nodiscard]] inline stxlib::Thread start_library_thread(const stxlib::Thread::Entry entry, void* const context,
                                                         const char* const what) {
    stxlib::OperationResult<stxlib::Thread> started = stxlib::Thread::start(entry, context);
    if (started.failed()) {
        if ((*started.error()).kind() == stxlib::ErrorKind::out_of_memory) throw std::bad_alloc();
        throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again), what);
    }
    stxlib::Thread thread = static_cast<stxlib::OperationResult<stxlib::Thread>&&>(started).take_value();
    return thread;
}

// Joins `thread` when it is running; an empty or joined thread is left.
// Joining itself is a defect here, as std::thread::join's deadlock error was.
inline void join_library_thread(stxlib::Thread& thread) {
    if (!thread.identity().present()) return;
    const stxlib::JoinResult joined = thread.join();
    if (joined == stxlib::JoinResult::self_join) {
        throw std::system_error(std::make_error_code(std::errc::resource_deadlock_would_occur),
                                "a library thread cannot join itself");
    }
}

} // namespace gui_forms::detail

#endif
