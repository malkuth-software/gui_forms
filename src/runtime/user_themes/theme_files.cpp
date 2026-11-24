#include "theme_files.hpp"

#include "../../platform/library_files.hpp"
#include "../../platform/stxlib_bridge.hpp"

#include <stxlib/desktop.hpp>
#include <stxlib/environment.hpp>
#include <stxlib/error.hpp>
#include <stxlib/files.hpp>
#include <stxlib/path.hpp>
#include <stxlib/process.hpp>
#include <stxlib/result.hpp>
#include <stxlib/text.hpp>
#include <stxlib/text_view.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms::detail::theme_files {
namespace {

// UTF-8 text as a path: through char8_t, so that a Windows path is not
// read in the process's code page. A listed entry's relative path is its
// names' Path text joined by '/' (list_folder), as the generic form was.
std::filesystem::path path_of(std::string_view text) {
    const std::u8string units(reinterpret_cast<const char8_t*>(text.data()), text.size());
    std::filesystem::path made(units);
    return made;
}

// A listed entry's kind: a link, to anything, is `other` and never entered.
EntryKind kind_of(const stxlib::FileKind kind) noexcept {
    switch (kind) {
    case stxlib::FileKind::regular: return EntryKind::file;
    case stxlib::FileKind::directory: return EntryKind::folder;
    case stxlib::FileKind::symbolic_link: return EntryKind::other;
    case stxlib::FileKind::other: return EntryKind::other;
    default: return EntryKind::other;
    }
}

// A folder still to be listed: its path and its path relative to the root
// ('/' separating, empty for the root itself).
struct PendingFolder final {
    stxlib::Path path;
    std::string relative;
};

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
    const stxlib::OperationResult<stxlib::TextView> checked = platform::validated_text(name);
    if (checked.failed()) return std::string{};
    const stxlib::OperationResult<stxlib::Utf8String> value =
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
    // The root itself, not what a link names: a link to a folder is not
    // listed.
    const std::optional<stxlib::FileInformation> root_information =
        platform::file_information(root, stxlib::LinkMode::no_follow);
    if (!root_information.has_value() || (*root_information).kind != stxlib::FileKind::directory) {
        return FileOutcome::missing;
    }
    stxlib::OperationResult<stxlib::Path> root_path = platform::stxlib_path(root);
    if (root_path.failed()) return FileOutcome::failed;
    // One folder at a time (STXlib lists one level); the order does not
    // matter, since the entries are sorted at the end.
    std::vector<PendingFolder> pending;
    pending.push_back(PendingFolder{.path = static_cast<stxlib::OperationResult<stxlib::Path>&&>(root_path).take_value(),
                                    .relative = std::string{}});
    while (!pending.empty()) {
        PendingFolder current = std::move(pending.back());
        pending.pop_back();
        // More entries than are left is over the limit; STXlib refuses the
        // listing before holding them.
        const std::size_t room = maximum_entries - entries.size();
        const stxlib::OperationResult<stxlib::DirectoryListing> listed =
            stxlib::list_directory(current.path, static_cast<stxlib::usize>(room));
        if (listed.failed()) {
            if ((*listed.error()).kind() == stxlib::ErrorKind::limit_exceeded) {
                entries.clear();
                return FileOutcome::over_limit;
            }
            return FileOutcome::failed;
        }
        const stxlib::DirectoryListing& listing = *listed.value();
        for (stxlib::usize index = 0; index < listing.count(); ++index) {
            const stxlib::PathText name = listing.name(index);
            const stxlib::OperationResult<stxlib::Path> name_path = stxlib::Path::parse_wtf8(name.data(), name.count());
            if (name_path.failed()) return FileOutcome::failed;
            stxlib::OperationResult<stxlib::Path> entry_path = current.path.join(*name_path.value());
            if (entry_path.failed()) return FileOutcome::failed;
            FolderEntry entry{};
            entry.path = current.relative;
            if (!entry.path.empty()) entry.path.push_back('/');
            entry.path.append(name.data(), static_cast<std::size_t>(name.count()));
            entry.kind = kind_of(listing.kind(index));
            if (entry.kind == EntryKind::file) {
                const stxlib::OperationResult<stxlib::FileInformation> information =
                    stxlib::inspect(*entry_path.value(), stxlib::LinkMode::no_follow);
                if (information.failed()) return FileOutcome::failed;
                entry.bytes = static_cast<std::uint64_t>((*information.value()).size);
            }
            if (recursive && entry.kind == EntryKind::folder) {
                pending.push_back(PendingFolder{
                    .path = static_cast<stxlib::OperationResult<stxlib::Path>&&>(entry_path).take_value(),
                    .relative = entry.path});
            }
            entries.push_back(std::move(entry));
        }
    }
    std::sort(entries.begin(), entries.end(), EntryPathOrder{});
    return FileOutcome::done;
}

FileOutcome read_file(std::string_view path, std::size_t maximum_bytes, std::string& bytes) {
    bytes.clear();
    const std::filesystem::path file = path_of(path);
    // The name itself, not what a link names.
    const std::optional<stxlib::FileInformation> information =
        platform::file_information(file, stxlib::LinkMode::no_follow);
    if (!information.has_value() || (*information).kind != stxlib::FileKind::regular) return FileOutcome::missing;
    const std::uint64_t size = (*information).size;
    if (size > maximum_bytes) return FileOutcome::over_limit;
    stxlib::OperationResult<stxlib::File> opened = platform::open_file(file, stxlib::FileMode::read);
    if (opened.failed()) return FileOutcome::failed;
    stxlib::File& stream = *opened.value();
    bytes.resize(static_cast<std::size_t>(size));
    // A file that changed size while read is refused rather than cut.
    const bool all_read = platform::read_exactly_at(stream, 0, bytes.data(), bytes.size());
    const bool whole = all_read && platform::ends_at(stream, size);
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
    // Through links: a link to a regular file is read.
    const std::optional<stxlib::FileInformation> information =
        platform::file_information(file, stxlib::LinkMode::follow);
    if (!information.has_value()) return FileOutcome::missing;
    if ((*information).kind != stxlib::FileKind::regular) return FileOutcome::failed;
    stxlib::OperationResult<stxlib::File> opened = platform::open_file(file, stxlib::FileMode::read);
    if (opened.failed()) return FileOutcome::failed;
    stxlib::File& stream = *opened.value();
    while (true) {
        const std::size_t held = bytes.size();
        if (held > maximum_bytes) {
            bytes.clear();
            return FileOutcome::over_limit;
        }
        bytes.resize(held + block_bytes);
        // One block, as many reads as the host needs, ending early only at
        // the end of the file.
        std::size_t read = 0;
        while (read < block_bytes) {
            const stxlib::MutableView<stxlib::Byte> rest(reinterpret_cast<stxlib::Byte*>(bytes.data() + held + read),
                                                         static_cast<stxlib::usize>(block_bytes - read));
            const stxlib::OperationResult<stxlib::usize> got = stream.read(rest);
            if (got.failed()) {
                bytes.clear();
                return FileOutcome::failed;
            }
            if (*got.value() == 0U) break;
            read += static_cast<std::size_t>(*got.value());
        }
        bytes.resize(held + read);
        if (read < block_bytes) break;
    }
    if (bytes.size() > maximum_bytes) {
        bytes.clear();
        return FileOutcome::over_limit;
    }
    return FileOutcome::done;
}

FileOutcome write_new_file(std::string_view path, std::string_view bytes) {
    const std::filesystem::path file = path_of(path);
    if (!platform::create_directories(file.parent_path())) return FileOutcome::failed;
    // Created only where nothing has the name, atomically, on every host
    // (FileMode::create_new: O_EXCL; CREATE_NEW).
    stxlib::OperationResult<stxlib::File> opened = platform::open_file(file, stxlib::FileMode::create_new);
    if (opened.failed()) {
        const bool taken = platform::file_information(file, stxlib::LinkMode::no_follow).has_value();
        return taken ? FileOutcome::exists : FileOutcome::failed;
    }
    stxlib::File& stream = *opened.value();
    const stxlib::View<stxlib::Byte> content(reinterpret_cast<const stxlib::Byte*>(bytes.data()),
                                             static_cast<stxlib::usize>(bytes.size()));
    const stxlib::OperationStatus written = stream.write_all(content);
    const stxlib::OperationStatus closed = stream.close();
    if (written.failed() || closed.failed()) return FileOutcome::failed;
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
    const stxlib::OperationResult<stxlib::Path> found = stxlib::current_executable();
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
    const stxlib::OperationResult<stxlib::ProcessArguments> read = stxlib::process_arguments();
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
