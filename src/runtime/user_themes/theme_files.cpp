#include "theme_files.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#endif

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

// The variable `name`'s value as UTF-8, or empty.
std::string environment(const char* name) {
#if defined(_WIN32)
    std::wstring wide{};
    for (const char* cursor = name; *cursor != '\0'; ++cursor) wide.push_back(static_cast<wchar_t>(*cursor));
    const wchar_t* const value = _wgetenv(wide.c_str());
    if (value == nullptr) return std::string{};
    const std::filesystem::path path(value);
    return text_of(path);
#else
    const char* const value = std::getenv(name);
    if (value == nullptr) return std::string{};
    std::string made(value);
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
    // be overwritten (an exclusive open replaces this with stx_auxlib).
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
    const std::string terminated(name);
    std::string value = environment(terminated.c_str());
    return value;
}

std::string executable_path() {
#if defined(_WIN32)
    // The longest path with long paths enabled, in UTF-16 units.
    constexpr DWORD maximum_path_units = 32768U;
    std::wstring path(maximum_path_units, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), maximum_path_units);
    if (count == 0U || count >= maximum_path_units) return std::string{};
    path.resize(count);
    const std::filesystem::path made(path);
    return text_of(made);
#elif defined(__APPLE__)
    std::uint32_t size = 0U;
    static_cast<void>(_NSGetExecutablePath(nullptr, &size));
    if (size == 0U) return std::string{};
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) return std::string{};
    path.resize(std::char_traits<char>::length(path.c_str()));
    return path;
#elif defined(__linux__)
    std::error_code error{};
    const std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) return std::string{};
    return text_of(path);
#else
    return std::string{};
#endif
}

std::vector<std::string> split_windows_command_line(std::string_view line) {
    std::vector<std::string> arguments{};
    std::size_t index = 0U;
    // The program's name: quotes toggle, backslashes are literal.
    std::string program{};
    bool quoted = false;
    while (index < line.size()) {
        const char character = line[index];
        if (!quoted && (character == ' ' || character == '\t')) break;
        if (character == '"') {
            quoted = !quoted;
        } else {
            program.push_back(character);
        }
        index = index + 1U;
    }
    arguments.push_back(std::move(program));
    while (true) {
        while (index < line.size() && (line[index] == ' ' || line[index] == '\t')) index = index + 1U;
        if (index >= line.size()) break;
        std::string argument{};
        quoted = false;
        while (index < line.size()) {
            const char character = line[index];
            if (!quoted && (character == ' ' || character == '\t')) break;
            if (character == '\\') {
                std::size_t backslashes = 0U;
                while (index < line.size() && line[index] == '\\') {
                    backslashes = backslashes + 1U;
                    index = index + 1U;
                }
                const bool before_quote = index < line.size() && line[index] == '"';
                if (!before_quote) {
                    argument.append(backslashes, '\\');
                    continue;
                }
                argument.append(backslashes / 2U, '\\');
                if (backslashes % 2U == 1U) {
                    argument.push_back('"');
                    index = index + 1U;
                }
                continue;
            }
            if (character == '"') {
                const bool doubled = quoted && index + 1U < line.size() && line[index + 1U] == '"';
                if (doubled) {
                    argument.push_back('"');
                    index = index + 2U;
                    continue;
                }
                quoted = !quoted;
                index = index + 1U;
                continue;
            }
            argument.push_back(character);
            index = index + 1U;
        }
        arguments.push_back(std::move(argument));
    }
    return arguments;
}

std::vector<std::string> process_arguments() {
    std::vector<std::string> arguments{};
#if defined(_WIN32)
    const wchar_t* const line = GetCommandLineW();
    if (line == nullptr) return arguments;
    const std::filesystem::path wide(line);
    const std::u8string units = wide.u8string();
    const std::string_view text(reinterpret_cast<const char*>(units.data()), units.size());
    arguments = split_windows_command_line(text);
#elif defined(__APPLE__)
    const int count = *_NSGetArgc();
    char** const vector = *_NSGetArgv();
    if (vector == nullptr) return arguments;
    for (int index = 0; index < count; ++index) {
        if (vector[index] == nullptr) break;
        arguments.emplace_back(vector[index]);
    }
#elif defined(__linux__)
    // NUL-separated, each argument ended by one; at most 1 MiB is read.
    constexpr std::size_t maximum_command_line_bytes = std::size_t{1024} * 1024U;
    std::string bytes{};
    if (read_named_file("/proc/self/cmdline", maximum_command_line_bytes, bytes) != FileOutcome::done) {
        return arguments;
    }
    std::size_t start = 0U;
    while (start < bytes.size()) {
        std::size_t end = bytes.find('\0', start);
        if (end == std::string::npos) end = bytes.size();
        arguments.emplace_back(bytes, start, end - start);
        start = end + 1U;
    }
#endif
    // The program's own name is not an argument.
    if (!arguments.empty()) arguments.erase(arguments.begin());
    return arguments;
}

} // namespace gui_forms::detail::theme_files
