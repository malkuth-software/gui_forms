// The drawing library's per-thread last error (src/abi/drawing_c_api.cpp,
// LastError): plain thread_local data (owner rule 2), so a thread's
// failures set only its own last error, a thread that never failed reports
// none, and a failure on one thread leaves another's error as it was.

#include "gui_forms/drawing_c_api.h"

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

        // This thread's own failure, kept across the other threads' work.
        gd_error_view own{};
        require(api.last_error(nullptr) == GD_ERROR_INVALID_ARGUMENT, "last_error without output fails");
        require(api.last_error(&own) == GD_OK, "last_error reads");
        require(own.code == static_cast<std::uint32_t>(GD_ERROR_INVALID_ARGUMENT) && own.message.size != 0U,
                "the failure is this thread's last error, with its text");

        ThreadReport report{};
        const FailingThread failing{&api, &report};
        std::thread failing_thread(failing);
        failing_thread.join();
        require(report.refused == GD_ERROR_INVALID_ARGUMENT, "the refused format fails");
        require(report.code == static_cast<std::uint32_t>(GD_ERROR_INVALID_ARGUMENT) && report.message_size != 0U,
                "the failure is the other thread's last error, with its text");

        std::uint32_t clean_code = UINT32_MAX;
        std::uint64_t clean_size = UINT64_MAX;
        const CleanThread clean{&api, &clean_code, &clean_size};
        std::thread clean_thread(clean);
        clean_thread.join();
        require(clean_code == static_cast<std::uint32_t>(GD_OK) && clean_size == 0U,
                "a thread that never failed reports no error");

        gd_error_view after{};
        require(api.last_error(&after) == GD_OK, "last_error reads again");
        require(after.code == own.code && after.message.size == own.message.size,
                "other threads' failures leave this thread's last error");
    } catch (const std::exception& error) {
        std::cerr << "drawing last error test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "drawing last error tests passed\n";
    return 0;
}
