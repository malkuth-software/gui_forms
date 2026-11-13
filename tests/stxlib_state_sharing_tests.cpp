// libgui_forms_application and this executable share one stxlib_state
// (stx's cmake/Stxlib.cmake: STXLIB_STATE_SHARED ON). Each holds its own copy
// of STXlib's static code: the library through the controls' row height
// index, this program through stxlib::boundary. Blocks of STXlib's allocator
// cross between the copies both ways, and both copies read the same thread
// identities. A static second stxlib_state in either image (two allocators,
// two mimallocs, two identity issuers, two SIMD selections) fails here: a
// release of a foreign block ends the process in tag-checking builds (Debug
// and sanitizer builds, stxlib/memory.hpp), and the identity checks fail in
// every build. Tested, not proved: a build that shared the allocator but not
// the issuer would pass the first half only.

#include "../src/runtime/stxlib_state_probe.hpp"

#include <stxlib/memory.hpp>
#include <stxlib/threads.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void require(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool filled(const void* const block, const std::size_t size, const unsigned char fill) {
    const unsigned char* const bytes = static_cast<const unsigned char*>(block);
    bool result = true;
    for (std::size_t index = 0; index < size; ++index) {
        result = result && bytes[index] == fill;
    }
    return result;
}

// Sizes across mimalloc's small, medium and large classes, and zero (a unique
// non-null block).
constexpr std::size_t block_sizes[] = {0, 1, 64, 1000, 70000, 3000000};

void test_blocks_from_the_library_release_here() {
    for (const std::size_t size : block_sizes) {
        void* const block = gui_forms::runtime::stxlib_state_probe_allocate(size, 0xa5);
        require(block != nullptr, "the library's STXlib allocator refused a block");
        require(filled(block, size, 0xa5), "a block from the library lost its contents");
        stxlib::memory::release(block);
    }
}

void test_blocks_from_here_release_in_the_library() {
    for (const std::size_t size : block_sizes) {
        void* const block = stxlib::memory::allocate(size, 16);
        require(block != nullptr, "this program's STXlib allocator refused a block");
        if (size != 0) {
            std::memset(block, 0x3c, size);
        }
        gui_forms::runtime::stxlib_state_probe_release(block);
    }
}

// What the second thread reads, in this order: this program's copy first,
// then the library's.
struct SecondThreadIdentities final {
    std::uint64_t program{0};
    std::uint64_t library{0};
};

void read_second_thread(SecondThreadIdentities* const identities) {
    const stxlib::ThreadIdentity program = stxlib::current_thread();
    (*identities).program = program.value();
    (*identities).library = gui_forms::runtime::stxlib_state_probe_thread();
}

// The order makes separate issuers disagree whatever either issued before:
// the library issues the main thread's identity before this program's copy
// is asked on any thread, and on the second thread this program's copy is
// asked first. With one issuer every reading of a thread is one value and
// the two threads differ. With two, this program's issuer gives the second
// thread its first identity on that thread while the library's gives it its
// next, and the main thread's readings differ the same way.
void test_one_thread_identity_issuer() {
    const std::uint64_t library_main = gui_forms::runtime::stxlib_state_probe_thread();
    require(library_main != 0, "the library's copy issued no identity to the main thread");
    SecondThreadIdentities second{};
    std::thread worker(read_second_thread, &second);
    worker.join();
    require(second.program != 0, "this program's copy issued no identity to the second thread");
    require(second.program == second.library, "the library and this program read different identities of one thread");
    require(second.program != library_main, "two threads share an identity");
    const stxlib::ThreadIdentity program_main = stxlib::current_thread();
    require(program_main.value() == library_main, "the library and this program read different identities of the main thread");
}

} // namespace

int main() {
    try {
        test_blocks_from_the_library_release_here();
        test_blocks_from_here_release_in_the_library();
        test_one_thread_identity_issuer();
    } catch (const std::exception& error) {
        std::cerr << "stxlib_state_sharing_tests: " << error.what() << '\n';
        return 1;
    }
    std::cout << "stxlib_state_sharing_tests: one stxlib_state across libgui_forms_application and this program\n";
    return 0;
}
