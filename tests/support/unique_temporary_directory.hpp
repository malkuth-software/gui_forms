#pragma once

// A directory under the system temporary directory that belongs to one run
// of one test. Fixed names there are shared by every process on the
// machine: two copies of a test running at once (the ordinary and the
// sanitizer tree, another workspace) would delete or rewrite each other's
// files. The name is the caller's prefix followed by a steady-clock stamp
// and an attempt number, and the directory is one this call created, never
// one that was already there (B-gf-tests-02-F1).

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace gui_forms::test_support {

// Creates and returns a new directory named `prefix` plus a unique suffix
// under std::filesystem::temp_directory_path(). Throws std::runtime_error
// when no new directory could be created; nothing is left behind then. The
// caller removes the directory (remove_temporary_directory) when done.
inline std::filesystem::path make_unique_temporary_directory(std::string_view prefix) {
    const std::filesystem::path base = std::filesystem::temp_directory_path();
    const std::chrono::steady_clock::duration stamp = std::chrono::steady_clock::now().time_since_epoch();
    const std::string stem = std::string(prefix) + "-" + std::to_string(stamp.count()) + "-";
    constexpr std::uint32_t attempts = 64U;
    for (std::uint32_t attempt = 0U; attempt < attempts; ++attempt) {
        const std::filesystem::path candidate = base / (stem + std::to_string(attempt));
        std::error_code status{};
        const bool created = std::filesystem::create_directory(candidate, status);
        if (created && !status) return candidate;
    }
    throw std::runtime_error("no unique temporary directory could be created for " + std::string(prefix));
}

// Removes a directory made by make_unique_temporary_directory and all it
// holds; a failure is ignored (the system cleans its temporary directory).
inline void remove_temporary_directory(const std::filesystem::path& directory) noexcept {
    std::error_code ignored{};
    std::filesystem::remove_all(directory, ignored);
}

} // namespace gui_forms::test_support
