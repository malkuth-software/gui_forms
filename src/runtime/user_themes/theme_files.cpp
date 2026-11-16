#include "theme_files.hpp"

#include "../../platform/stxlib_bridge.hpp"

#include <stxlib/desktop.hpp>
#include <stxlib/environment.hpp>
#include <stxlib/path.hpp>
#include <stxlib/process.hpp>
#include <stxlib/result.hpp>
#include <stxlib/text.hpp>
#include <stxlib/text_view.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <utility>

namespace gui_forms::detail::theme_files {
namespace {

// UTF-8 text as a path, and back: through char8_t, so that a Windows
// path is not read in the process's code page.
std::filesystem::path path_of(std::string_view text) {
    const std::u8string units(reinterpret_cast<const char8_t*>(text.data()), text.size());
    std::filesystem::path made(units);
    return made;
}

std::string text_of(const std::filesystem::path& path) {
    const std::u8string units = path.generic_u8string();
    std::string made(reinterpret_cast<const char*>(units.data()), units.size());
    return made;
}

EntryKind kind_of(const std::filesystem::file_status status) noexcept {
    if (std::filesystem::is_symlink(status)) return EntryKind::other;
    if (std::filesystem::is_regular_file(status)) return EntryKind::file;
    if (std::filesystem::is_directory(status)) return EntryKind::folder;
    return EntryKind::other;
}

struct EntryPathOrder final {
    bool operator()(const FolderEntry& left, const FolderEntry& right) const noexcept {
        return left.path < right.path;
    }
};

// The variable `name`'s value as UTF-8, or empty: STXlib's environment
// (stxlib/environment.hpp), which reads the wide block on Windows and
// serializes with every other reader. An unset, unreadable or non-UTF-8
// variable reads as empty. A high-integrity build reads no environment
// variable (cmake/HighIntegrity.cmake): always empty, so there is no
// configuration folder and no GUI_FORMS_THEME_SHEET request.
std::string environment(std::string_view name) {
#if defined(GUI_FORMS_HIGH_INTEGRITY)
    static_cast<void>(name);
    return std::string{};
#else
    const stxlib::Result<stxlib::TextView> checked = platform::validated_text(name);
    if (checked.failed()) return std::string{};
    const stxlib::Result<stxlib::Utf8String> value =
        stxlib::read_variable(*checked.value(), stxlib::maximum_environment_bytes);
    if (value.failed()) return std::string{};
    std::string made = platform::standard_text_copy((*value.value()).view());
    return made;
#endif
}

} // namespace

FileOutcome list_folder(std::string_view folder, bool recursive, std::size_t maximum_entries,
                        std::vector<FolderEntry>& entries) {
    entries.clear();
    const std::filesystem::path root = path_of(folder);
    std::error_code error{};
    const std::filesystem::file_status status = std::filesystem::symlink_status(root, error);
    if (error || !std::filesystem::is_directory(status)) return FileOutcome::missing;
    // One iterator form for both, so that a link to a folder is listed as
    // `other` and never entered.
    std::filesystem::recursive_directory_iterator walk(root, std::filesystem::directory_options::none, error);
    if (error) return FileOutcome::failed;
    const std::filesystem::recursive_directory_iterator end{};
    while (walk != end) {
        const std::filesystem::directory_entry& entry = *walk;
        const std::filesystem::file_status entry_status = entry.symlink_status(error);
        if (error) return FileOutcome::failed;
        if (entries.size() == maximum_entries) {
            entries.clear();
            return FileOutcome::over_limit;
        }
        FolderEntry listed{};
        listed.path = text_of(entry.path().lexically_relative(root));
        listed.kind = kind_of(entry_status);
        if (listed.kind == EntryKind::file) {
            listed.bytes = static_cast<std::uint64_t>(entry.file_size(error));
            if (error) return FileOutcome::failed;
        }
        entries.push_back(std::move(listed));
        if (!recursive || entries.back().kind != EntryKind::folder) walk.disable_recursion_pending();
        walk.increment(error);
        if (error) return FileOutcome::failed;
    }
    std::sort(entries.begin(), entries.end(), EntryPathOrder{});
    return FileOutcome::done;
}

FileOutcome read_file(std::string_view path, std::size_t maximum_bytes, std::string& bytes) {
    bytes.clear();
    const std::filesystem::path file = path_of(path);
    std::error_code error{};
    const std::filesystem::file_status status = std::filesystem::symlink_status(file, error);
    if (error || !std::filesystem::is_regular_file(status)) return FileOutcome::missing;
    const std::uintmax_t size = std::filesystem::file_size(file, error);
    if (error) return FileOutcome::failed;
    if (size > maximum_bytes) return FileOutcome::over_limit;
    std::ifstream stream(file, std::ios::binary);
    if (!stream) return FileOutcome::failed;
    bytes.resize(static_cast<std::size_t>(size));
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    // A file that changed size while read is refused rather than cut.
    const bool all_read = static_cast<std::size_t>(stream.gcount()) == bytes.size();
    const bool whole = all_read && stream.peek() == std::char_traits<char>::eof();
    if (!whole) {
        bytes.clear();
        return FileOutcome::failed;
    }
    return FileOutcome::done;
}

FileOutcome read_named_file(std::string_view path, std::size_t maximum_bytes, std::string& bytes) {
    // Read a block at a time: no size is taken first.
    constexpr std::size_t block_bytes = std::size_t{64} * 1024U;
    bytes.clear();
    const std::filesystem::path file = path_of(path);
    std::error_code error{};
    const std::filesystem::file_status status = std::filesystem::status(file, error);
    if (error || !std::filesystem::exists(status)) return FileOutcome::missing;
    if (!std::filesystem::is_regular_file(status)) return FileOutcome::failed;
    std::ifstream stream(file, std::ios::binary);
    if (!stream) return FileOutcome::failed;
    while (true) {
        const std::size_t held = bytes.size();
        if (held > maximum_bytes) {
            bytes.clear();
            return FileOutcome::over_limit;
        }
        bytes.resize(held + block_bytes);
        stream.read(bytes.data() + held, static_cast<std::streamsize>(block_bytes));
        const std::size_t read = static_cast<std::size_t>(stream.gcount());
        bytes.resize(held + read);
        if (read < block_bytes) break;
    }
    if (stream.bad()) {
        bytes.clear();
        return FileOutcome::failed;
    }
    if (bytes.size() > maximum_bytes) {
        bytes.clear();
        return FileOutcome::over_limit;
    }
    return FileOutcome::done;
}

FileOutcome write_new_file(std::string_view path, std::string_view bytes) {
    const std::filesystem::path file = path_of(path);
    std::error_code error{};
    std::filesystem::create_directories(file.parent_path(), error);
    if (error) return FileOutcome::failed;
#if defined(_WIN32)
    // Checked, then created: another writer between the two could still
    // be overwritten (an exclusive open replaces this with STXlib).
    if (std::filesystem::exists(std::filesystem::symlink_status(file, error))) return FileOutcome::exists;
    std::FILE* const stream = _wfopen(file.c_str(), L"wb");
#else
    // "x": created only where no file is, atomically.
    std::FILE* const stream = std::fopen(file.c_str(), "wbx");
    if (stream == nullptr && std::filesystem::exists(std::filesystem::symlink_status(file, error))) {
        return FileOutcome::exists;
    }
#endif
    if (stream == nullptr) return FileOutcome::failed;
    const std::size_t written = std::fwrite(bytes.data(), 1U, bytes.size(), stream);
    const int closed = std::fclose(stream);
    if (written != bytes.size() || closed != 0) return FileOutcome::failed;
    return FileOutcome::done;
}

std::string join_path(std::string_view folder, std::string_view relative) {
    std::string joined(folder);
    if (!joined.empty() && joined.back() != '/') joined.push_back('/');
    joined.append(relative);
    return joined;
}

std::string program_configuration_folder(std::string_view program) {
#if defined(__APPLE__)
    const std::string home = environment("HOME");
    if (home.empty()) return std::string{};
    std::string base = join_path(home, "Library/Application Support");
#elif defined(_WIN32)
    std::string base = environment("APPDATA");
    if (base.empty()) return std::string{};
#else
    std::string base = environment("XDG_CONFIG_HOME");
    if (base.empty()) {
        const std::string home = environment("HOME");
        if (home.empty()) return std::string{};
        base = join_path(home, ".config");
    }
#endif
    std::string folder = join_path(base, program);
    return folder;
}

std::string environment_value(std::string_view name) {
    std::string value = environment(name);
    return value;
}

// STXlib's current_executable (stxlib/desktop.hpp): GetModuleFileNameW on
// Windows, _NSGetExecutablePath on macOS, /proc/self/exe on Linux. Empty
// when the host cannot say or the name is not Unicode text.
std::string executable_path() {
    const stxlib::Result<stxlib::Path> found = stxlib::current_executable();
    if (found.failed()) return std::string{};
    const stxlib::PathText text = (*found.value()).text();
    if (!text.is_unicode()) return std::string{};
    std::string path = platform::standard_text_copy(text.unicode());
    return path;
}

// STXlib's process_arguments (stxlib/process.hpp): the C runtime's argv on
// macOS, /proc/self/cmdline on Linux, GetCommandLineW split by the C
// runtime's rules on Windows; each argument's bytes as the host gave them.
// Empty when the host cannot be read.
std::vector<std::string> process_arguments() {
    std::vector<std::string> arguments{};
    const stxlib::Result<stxlib::ProcessArguments> read = stxlib::process_arguments();
    if (read.failed()) return arguments;
    const stxlib::ProcessArguments& host = *read.value();
    // The program's own name (index 0) is not an argument.
    for (stxlib::usize index = 1; index < host.count(); ++index) {
        const stxlib::View<char> bytes = host.argument(index).bytes();
        arguments.emplace_back(bytes.data(), static_cast<std::size_t>(bytes.count()));
    }
    return arguments;
}

} // namespace gui_forms::detail::theme_files
