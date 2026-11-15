#ifndef GUI_FORMS_CORE_THREAD_RECORDS_THREAD_RECORDS_END_HPP
#define GUI_FORMS_CORE_THREAD_RECORDS_THREAD_RECORDS_END_HPP

// Ends the calling thread's STXlib thread records (stxlib/
// thread_records.hpp; every library's, through end_thread_records) when its
// scope closes. Declared first in every thread entry GUI.Forms owns, so it
// closes last: the records' release then runs on the thread before it
// returns, and on Windows outside the loader lock that the exit hook runs
// under. After it, a record lookup on the thread fails (STXlib's disposed)
// and its callers degrade quietly. The core's own per-thread state is plain
// thread_local data (owner rule 2) and keeps no record.

namespace gui_forms::detail {

class ThreadRecordsEnd final {
public:
    ThreadRecordsEnd() noexcept {}
    ~ThreadRecordsEnd();
    ThreadRecordsEnd(const ThreadRecordsEnd&) = delete;
    ThreadRecordsEnd& operator=(const ThreadRecordsEnd&) = delete;
};

} // namespace gui_forms::detail

#endif
