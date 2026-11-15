#ifndef GUI_FORMS_CORE_THREAD_RECORDS_CORE_THREAD_RECORD_HPP
#define GUI_FORMS_CORE_THREAD_RECORDS_CORE_THREAD_RECORD_HPP

// The core's per-thread state as one STXlib thread record (stxlib/
// thread_records.hpp): no thread_local with a destructor remains in the
// core or the C ABI. A record is created on its thread's first write and
// released on that thread, by STXlib's exit hook or earlier by
// ThreadRecordsEnd. Each library family keeps one record type: this one is
// the core's and the C ABI's (the C ABI module links the core); the drawing
// libraries keep their own, as they load separately.

#include <memory>
#include <new>
#include <vector>

namespace gui_forms::abi1 {
class EventFields;
} // namespace gui_forms::abi1

namespace gui_forms::detail {

struct RevocableInvokes;

struct CoreThreadRecord final {
    // The thread's revocable synchronous invokes, if it has any (a
    // sub-surface's thread; post_dispatch.cpp). Each thread reads and writes
    // only its own.
    std::shared_ptr<RevocableInvokes> revocable_invokes;
    // The C ABI's event field frames active on this thread, innermost last
    // (abi_event_fields.cpp). Observers: each frame lives in its caller's
    // scope and removes itself before it goes.
    std::vector<abi1::EventFields*> event_frames;
};

// The record type's callbacks, run by STXlib on the record's thread:
// initialize constructs the record in zero-filled memory (neither member's
// default construction allocates or throws); release destroys it, which
// frees memory only (the invokes are shared with their sub-surface channel,
// and the frames are observers), so it is safe under the Windows loader lock.
inline void initialize_core_thread_record(CoreThreadRecord* record) noexcept {
    ::new (static_cast<void*>(record)) CoreThreadRecord();
}

inline void release_core_thread_record(CoreThreadRecord* record) noexcept {
    (*record).~CoreThreadRecord();
}

// The calling thread's record, created on first use. Null when it cannot
// be: the thread's records are ending or have ended (STXlib's disposed),
// the allocation failed, or the type could not be registered. Callers then
// degrade quietly and keep nothing for the thread.
[[nodiscard]] CoreThreadRecord* core_thread_record() noexcept;

// The calling thread's record if it exists and is not being released;
// never creates one. For reads, and for clearing what a write left.
[[nodiscard]] CoreThreadRecord* existing_core_thread_record() noexcept;

// Ends the calling thread's thread records (every library's: STXlib's
// end_thread_records) when its scope closes. Declared first in every
// thread entry GUI.Forms owns, so it closes last: the records' release then
// runs on the thread before it returns, and on Windows outside the loader
// lock that the exit hook runs under, where GDI may not be called. After it
// a record lookup on the thread fails, and its callers degrade.
class ThreadRecordsEnd final {
public:
    ThreadRecordsEnd() noexcept {}
    ~ThreadRecordsEnd();
    ThreadRecordsEnd(const ThreadRecordsEnd&) = delete;
    ThreadRecordsEnd& operator=(const ThreadRecordsEnd&) = delete;
};

} // namespace gui_forms::detail

#endif
