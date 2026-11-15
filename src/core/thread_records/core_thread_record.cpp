#include "core_thread_record.hpp"

#include <stxlib/result.hpp>
#include <stxlib/thread_records.hpp>

#include <utility>

namespace gui_forms::detail {

namespace {

using CoreThreadRecordType = stxlib::ThreadRecord<CoreThreadRecord>;
using CoreThreadRecordRegistration = stxlib::Result<CoreThreadRecordType>;

// Registered once per process, by the first thread that asks; permanent, as
// the core stays loaded for the process's life. A failed registration
// (STXlib's 16 types taken) leaves every thread without the record.
const CoreThreadRecordRegistration& core_thread_record_registration() noexcept {
    static const CoreThreadRecordRegistration registration =
        CoreThreadRecordType::register_type<&initialize_core_thread_record, &release_core_thread_record>();
    return registration;
}

} // namespace

CoreThreadRecord* core_thread_record() noexcept {
    const CoreThreadRecordType* const type = core_thread_record_registration().value();
    if (type == nullptr) return nullptr;
    stxlib::Result<CoreThreadRecord*> found = (*type).get();
    if (found.failed()) return nullptr;
    CoreThreadRecord* const result = std::move(found).take_value();
    return result;
}

CoreThreadRecord* existing_core_thread_record() noexcept {
    const CoreThreadRecordType* const type = core_thread_record_registration().value();
    if (type == nullptr) return nullptr;
    CoreThreadRecord* const result = (*type).existing();
    return result;
}

ThreadRecordsEnd::~ThreadRecordsEnd() {
    stxlib::end_thread_records();
}

} // namespace gui_forms::detail
