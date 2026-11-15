// The core's per-thread state as an STXlib thread record
// (src/core/thread_records/core_thread_record.hpp): one record per thread,
// created on first write and never by a read; released on its thread by
// STXlib's exit hook or by ThreadRecordsEnd; and a thread whose records have
// ended degrades quietly (no record, nothing kept, nothing thrown).

#include "../src/core/thread_records/core_thread_record.hpp"
#include "../src/core/dispatcher/state/dispatcher_state.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

using gui_forms::detail::CoreThreadRecord;
using gui_forms::detail::RevocableInvokes;
using gui_forms::detail::ThreadRecordsEnd;
using gui_forms::detail::core_thread_record;
using gui_forms::detail::existing_core_thread_record;
using gui_forms::detail::set_thread_revocable_invokes;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// What one thread saw, read by the test's thread after the join.
struct ThreadReport final {
    bool existing_before_write{};
    bool created{};
    bool same_on_second_use{};
    bool existing_after_write{};
    bool invokes_kept{};
    bool ended_without_record{};
    bool existing_after_end{};
    bool set_after_end_kept{};
};

// A thread that writes its record, ends its records and checks that it
// then has none.
struct EndingThread final {
    ThreadReport* report{};
    std::shared_ptr<RevocableInvokes> invokes{};

    void operator()() const {
        {
            const ThreadRecordsEnd thread_records_end;
            (*report).existing_before_write = existing_core_thread_record() != nullptr;
            CoreThreadRecord* const record = core_thread_record();
            (*report).created = record != nullptr;
            (*report).same_on_second_use = core_thread_record() == record;
            (*report).existing_after_write = existing_core_thread_record() == record;
            set_thread_revocable_invokes(invokes);
            (*report).invokes_kept = record != nullptr && (*record).revocable_invokes == invokes;
        }
        // Ended: lookups fail (STXlib's disposed) and writes keep nothing.
        (*report).ended_without_record = core_thread_record() == nullptr;
        (*report).existing_after_end = existing_core_thread_record() != nullptr;
        set_thread_revocable_invokes(invokes);
        (*report).set_after_end_kept = existing_core_thread_record() != nullptr;
    }
};

// A thread that writes its record and returns without ending it: STXlib's
// exit hook releases it.
// Compared on the thread, while both records are live.
struct ExitingThread final {
    std::shared_ptr<RevocableInvokes> invokes{};
    const CoreThreadRecord* other{};
    bool* separate{};

    void operator()() const {
        set_thread_revocable_invokes(invokes);
        const CoreThreadRecord* const record = existing_core_thread_record();
        *separate = record != nullptr && record != other;
    }
};

void test_ended_thread_degrades() {
    std::shared_ptr<RevocableInvokes> invokes = std::make_shared<RevocableInvokes>();
    ThreadReport report{};
    const EndingThread body{&report, invokes};
    std::thread thread(body);
    thread.join();
    require(!report.existing_before_write, "a read creates no record");
    require(report.created, "the first write creates the record");
    require(report.same_on_second_use, "a thread keeps one record");
    require(report.existing_after_write, "the record is found once it exists");
    require(report.invokes_kept, "the record keeps the thread's revocable invokes");
    require(report.ended_without_record, "an ended thread gets no record");
    require(!report.existing_after_end, "an ended thread finds no record");
    require(!report.set_after_end_kept, "a write on an ended thread keeps nothing");
    // The release ran on the thread: the record's share of the invokes is
    // gone, and the late write kept none.
    require(invokes.use_count() == 1, "ending the records released the invokes");
}

void test_exit_releases_and_threads_are_separate() {
    CoreThreadRecord* const mine = core_thread_record();
    require(mine != nullptr, "the test's thread has a record");
    std::shared_ptr<RevocableInvokes> invokes = std::make_shared<RevocableInvokes>();
    bool separate = false;
    const ExitingThread body{invokes, mine, &separate};
    std::thread thread(body);
    thread.join();
    require(separate, "each thread has its own record");
    require(invokes.use_count() == 1, "the exit hook released the exited thread's record");
    require(core_thread_record() == mine, "another thread's exit leaves this thread's record");
    require((*mine).revocable_invokes == nullptr, "this thread's record never saw the other's invokes");
}

void test_clearing_creates_nothing() {
    struct ClearingThread final {
        bool* created{};
        void operator()() const {
            set_thread_revocable_invokes(nullptr);
            *created = existing_core_thread_record() != nullptr;
        }
    };
    bool created = true;
    const ClearingThread body{&created};
    std::thread thread(body);
    thread.join();
    require(!created, "clearing the revocable invokes creates no record");
}

} // namespace

int main() {
    try {
        test_ended_thread_degrades();
        test_exit_releases_and_threads_are_separate();
        test_clearing_creates_nothing();
    } catch (const std::exception& error) {
        std::cerr << "thread records test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "thread records tests passed\n";
    return 0;
}
