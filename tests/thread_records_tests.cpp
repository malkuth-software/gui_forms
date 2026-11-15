// ThreadRecordsEnd (src/core/thread_records/thread_records_end.hpp): the
// guard each thread entry GUI.Forms owns declares first. Closing it releases
// the thread's STXlib records on the thread, once, after which a lookup on
// that thread fails as disposed (the callers' quiet degradation); a thread
// that returns without one has its records released by STXlib's exit hook.

#include "../src/core/thread_records/thread_records_end.hpp"

#include <stxlib/error.hpp>
#include <stxlib/result.hpp>
#include <stxlib/thread_records.hpp>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {

using gui_forms::detail::ThreadRecordsEnd;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Every release of a test record, on any thread.
std::atomic<std::uint32_t> test_record_releases{0U};

struct TestRecord final {
    std::uint32_t value{};
};

void initialize_test_record(TestRecord* record) noexcept {
    ::new (static_cast<void*>(record)) TestRecord();
}

void release_test_record(TestRecord* record) noexcept {
    (*record).~TestRecord();
    test_record_releases.fetch_add(1U, std::memory_order_acq_rel);
}

using TestRecordType = stxlib::ThreadRecord<TestRecord>;

struct ThreadReport final {
    bool created{};
    std::uint32_t releases_inside{};
    std::uint32_t releases_after_end{};
    bool disposed_after_end{};
    bool existing_after_end{};
};

// Creates its record inside a ThreadRecordsEnd scope, then looks again.
struct EndingThread final {
    const TestRecordType* type{};
    ThreadReport* report{};

    void operator()() const {
        {
            const ThreadRecordsEnd thread_records_end;
            stxlib::Result<TestRecord*> found = (*type).get();
            (*report).created = !found.failed();
            (*report).releases_inside = test_record_releases.load(std::memory_order_acquire);
        }
        (*report).releases_after_end = test_record_releases.load(std::memory_order_acquire);
        const stxlib::Result<TestRecord*> again = (*type).get();
        (*report).disposed_after_end =
            again.failed() && (*again.error()).kind() == stxlib::ErrorKind::disposed;
        (*report).existing_after_end = (*type).existing() != nullptr;
    }
};

// Creates its record and returns without ending it.
struct ExitingThread final {
    const TestRecordType* type{};
    bool* created{};

    void operator()() const {
        const stxlib::Result<TestRecord*> found = (*type).get();
        *created = !found.failed();
    }
};

} // namespace

int main() {
    try {
        stxlib::Result<TestRecordType> registered =
            TestRecordType::register_type<&initialize_test_record, &release_test_record>();
        require(!registered.failed(), "the test record type registers");
        const TestRecordType type = std::move(registered).take_value();

        ThreadReport report{};
        const EndingThread ending{&type, &report};
        std::thread ending_thread(ending);
        ending_thread.join();
        require(report.created, "the thread's record is created");
        require(report.releases_inside == 0U, "the record lives until the guard closes");
        require(report.releases_after_end == 1U, "closing the guard releases the record once, on the thread");
        require(report.disposed_after_end, "after the guard a lookup fails as disposed");
        require(!report.existing_after_end, "after the guard no record is found");
        require(test_record_releases.load(std::memory_order_acquire) == 1U,
                "the thread's exit releases nothing more");

        bool created = false;
        const ExitingThread exiting{&type, &created};
        std::thread exiting_thread(exiting);
        exiting_thread.join();
        require(created, "the second thread's record is created");
        require(test_record_releases.load(std::memory_order_acquire) == 2U,
                "a thread without the guard has its record released at exit");
    } catch (const std::exception& error) {
        std::cerr << "thread records test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "thread records tests passed\n";
    return 0;
}
