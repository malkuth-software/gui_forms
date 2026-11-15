// The drawing library's per-thread last error as an STXlib thread record
// (src/abi/drawing_c_api.cpp, DrawingThreadRecord): a thread's failures
// set its own last error, a thread that never failed reports none, and a
// thread whose records have ended still gets every result while
// gd_last_error quietly reports no error.

#include "gui_forms/drawing_c_api.h"

#include <stxlib/thread_records.hpp>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Bitmaps are BGRA8 only: format 1 is refused as an argument fault, which
// sets the calling thread's last error.
constexpr std::uint32_t refused_format = 1U;

struct ThreadReport final {
    gd_result refused{GD_OK};
    std::uint32_t code{};
    std::uint64_t message_size{};
    gd_result refused_after_end{GD_OK};
    std::uint32_t code_after_end{};
    std::uint64_t message_size_after_end{};
    gd_result last_error_after_end{GD_ERROR_INTERNAL};
};

struct FailingThread final {
    const gd_api_v0* api{};
    ThreadReport* report{};

    void operator()() const {
        gd_handle bitmap{};
        gd_error_view error{};
        (*report).refused = (*api).bitmap_create(2U, 1U, refused_format, &bitmap);
        static_cast<void>((*api).last_error(&error));
        (*report).code = error.code;
        (*report).message_size = error.message.size;
        stxlib::end_thread_records();
        (*report).refused_after_end = (*api).bitmap_create(2U, 1U, refused_format, &bitmap);
        error = {};
        (*report).last_error_after_end = (*api).last_error(&error);
        (*report).code_after_end = error.code;
        (*report).message_size_after_end = error.message.size;
    }
};

struct CleanThread final {
    const gd_api_v0* api{};
    std::uint32_t* code{};
    std::uint64_t* message_size{};

    void operator()() const {
        gd_error_view error{};
        static_cast<void>((*api).last_error(&error));
        *code = error.code;
        *message_size = error.message.size;
    }
};

} // namespace

int main() {
    try {
        gd_api_v0 api{};
        api.struct_size = sizeof(api);
        require(gd_get_api_v0(GD_ABI_VERSION_0_1, &api) == GD_OK, "the drawing table loads");

        ThreadReport report{};
        const FailingThread failing{&api, &report};
        std::thread failing_thread(failing);
        failing_thread.join();
        require(report.refused == GD_ERROR_INVALID_ARGUMENT, "the refused format fails");
        require(report.code == static_cast<std::uint32_t>(GD_ERROR_INVALID_ARGUMENT) && report.message_size != 0U,
                "the failure is the thread's last error, with its text");
        require(report.refused_after_end == GD_ERROR_INVALID_ARGUMENT,
                "an ended thread still gets the result");
        require(report.last_error_after_end == GD_OK &&
                    report.code_after_end == static_cast<std::uint32_t>(GD_OK) &&
                    report.message_size_after_end == 0U,
                "an ended thread's last error is quietly none");

        std::uint32_t clean_code = UINT32_MAX;
        std::uint64_t clean_size = UINT64_MAX;
        const CleanThread clean{&api, &clean_code, &clean_size};
        std::thread clean_thread(clean);
        clean_thread.join();
        require(clean_code == static_cast<std::uint32_t>(GD_OK) && clean_size == 0U,
                "a thread that never failed reports no error");
    } catch (const std::exception& error) {
        std::cerr << "drawing thread records test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "drawing thread records tests passed\n";
    return 0;
}
