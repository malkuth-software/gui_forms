#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Internal: the user themes' file and folder operations (USER_THEMES.md
// sections 2 and 7), the one place src/runtime/user_themes reaches the file
// system or the environment. Every path is UTF-8 text with `/` between its
// segments (a Windows path may also hold `\`); no std::filesystem type
// crosses this interface, so the operations can move onto STXlib's
// owned UTF-8 paths and files without touching their callers.

namespace gui_forms::detail::theme_files {

enum class EntryKind : std::uint8_t { file, folder, other };

// One entry of a listing: its path relative to the listed folder, with `/`
// between segments, its kind (a symbolic link, device or socket is
// `other`, and a link is never followed) and, for a file, its size.
struct FolderEntry final {
    std::string path{};
    EntryKind kind{EntryKind::other};
    std::uint64_t bytes{};
};

enum class FileOutcome : std::uint8_t {
    done,
    // The folder or file is not there.
    missing,
    // A listing over its bound of entries, or a file over its bound of
    // bytes; nothing is returned.
    over_limit,
    // A file to write is already there; it is left as it was.
    exists,
    failed,
};

// Lists `folder` into `entries` (cleared first), sorted by path in byte
// order: its top level, or with `recursive` every entry below it (folders
// listed, and entered, but links not). Over `maximum_entries` entries is
// over_limit with `entries` cleared.
[[nodiscard]] FileOutcome list_folder(std::string_view folder, bool recursive, std::size_t maximum_entries,
                                      std::vector<FolderEntry>& entries);

// Reads the file at `path` into `bytes` (replaced): over_limit, with
// `bytes` cleared and nothing read, for a file over `maximum_bytes`.
[[nodiscard]] FileOutcome read_file(std::string_view path, std::size_t maximum_bytes, std::string& bytes);

// Reads the file a user named at `path`, following a link, into `bytes`
// (replaced): a block at a time, so a file whose size the platform does not
// report (Linux's /proc) reads whole; over_limit, with `bytes` cleared,
// past `maximum_bytes`; missing where nothing is there; failed for a folder
// or a read that fails.
[[nodiscard]] FileOutcome read_named_file(std::string_view path, std::size_t maximum_bytes, std::string& bytes);

// Writes `bytes` as a new file at `path`, making the folders above it; a
// file already there is `exists` and keeps its bytes (never overwritten).
// On failed, a partly written file may remain.
[[nodiscard]] FileOutcome write_new_file(std::string_view path, std::string_view bytes);

// `folder` and `relative` joined with one `/`.
[[nodiscard]] std::string join_path(std::string_view folder, std::string_view relative);

// The platform's configuration folder for `program` (macOS
// `~/Library/Application Support/<program>`, Windows `%APPDATA%\<program>`,
// elsewhere `$XDG_CONFIG_HOME/<program>` or `~/.config/<program>`), from
// the environment; empty where the environment names none.
[[nodiscard]] std::string program_configuration_folder(std::string_view program);

// The environment variable `name`'s value as UTF-8 (STXlib's
// read_variable); empty where it is unset, unreadable or not UTF-8.
[[nodiscard]] std::string environment_value(std::string_view name);

// The running program's own file as an absolute UTF-8 path (STXlib's
// current_executable: Windows, the process's module; Linux, /proc/self/exe;
// macOS, _NSGetExecutablePath); empty where the platform names none.
[[nodiscard]] std::string executable_path();

// The arguments the process was started with, after the program's own
// name, as the host gave them (STXlib's process_arguments: Windows, the
// process's command line split as the C runtime splits it; Linux,
// /proc/self/cmdline; macOS, the C runtime's argument vector); empty where
// the platform gives none.
[[nodiscard]] std::vector<std::string> process_arguments();

} // namespace gui_forms::detail::theme_files
