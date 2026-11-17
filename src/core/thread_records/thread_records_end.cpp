#include "thread_records_end.hpp"

#include <stxlib/thread_records.hpp>

namespace gui_forms::detail {

ThreadRecordsEnd::~ThreadRecordsEnd() {
    stxlib::end_thread_records();
}

} // namespace gui_forms::detail
