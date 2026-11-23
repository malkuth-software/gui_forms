#pragma once

// GUI.Forms' file operations on STXlib (stxlib/files.hpp; the shield,
// 2026-11-22): every file the library reads, writes, lists or inspects goes
// through STXlib, not std::filesystem's operations or the standard file
// streams. A path arrives as GUI.Forms' FilePath (std::filesystem::path,
// still the public vocabulary) and converts through the bridge
// (stxlib_path, stxlib_bridge.hpp).
//
// Header-only. Each function converts its path first; a path STXlib cannot
// carry (a NUL, a reserved ':' on Windows) fails as the operation would on a
// name that cannot be opened. The conversion's Windows copy is the standard
// library's and throws std::bad_alloc, so these are not noexcept; nothing
// else here throws, except list_directory_entries' standard copies
// (std::bad_alloc).

#include "stxlib_bridge.hpp"

#include <stxlib/buffer.hpp>
#include <stxlib/error.hpp>
#include <stxlib/files.hpp>
#include <stxlib/path.hpp>
#include <stxlib/result.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace gui_forms::platform {

// What `path` names, following links or not as `links` says; absent when
// nothing has that name, the path cannot be carried or the host refuses.
[[nodiscard]] inline std::optional<stxlib::FileInformation> file_information(const std::filesystem::path& path,
                                                                             const stxlib::LinkMode links) {
    const stxlib::OperationResult<stxlib::Path> converted = stxlib_path(path);
    if (converted.failed()) return std::nullopt;
    const stxlib::OperationResult<stxlib::FileInformation> inspected = stxlib::inspect(*converted.value(), links);
    if (inspected.failed()) return std::nullopt;
    const std::optional<stxlib::FileInformation> result(*inspected.value());
    return result;
}

// True when `path` names a regular file, through links (as
// std::filesystem::is_regular_file was).
[[nodiscard]] inline bool names_regular_file(const std::filesystem::path& path) {
    const std::optional<stxlib::FileInformation> information = file_information(path, stxlib::LinkMode::follow);
    return information.has_value() && (*information).kind == stxlib::FileKind::regular;
}

// True when `path` names a directory, through links (as
// std::filesystem::is_directory was).
[[nodiscard]] inline bool names_directory(const std::filesystem::path& path) {
    const std::optional<stxlib::FileInformation> information = file_information(path, stxlib::LinkMode::follow);
    return information.has_value() && (*information).kind == stxlib::FileKind::directory;
}

// The size of the regular file `path` names, through links; absent for
// anything else, as std::filesystem::file_size failed for it.
[[nodiscard]] inline std::optional<std::uint64_t> regular_file_bytes(const std::filesystem::path& path) {
    const std::optional<stxlib::FileInformation> information = file_information(path, stxlib::LinkMode::follow);
    if (!information.has_value() || (*information).kind != stxlib::FileKind::regular) return std::nullopt;
    const std::optional<std::uint64_t> result(static_cast<std::uint64_t>((*information).size));
    return result;
}

// `path` opened as `mode` says (FileMode), or the refusal: the conversion's
// or File::open's.
[[nodiscard]] inline stxlib::OperationResult<stxlib::File> open_file(const std::filesystem::path& path,
                                                                     const stxlib::FileMode mode) {
    const stxlib::OperationResult<stxlib::Path> converted = stxlib_path(path);
    if (converted.failed()) {
        stxlib::OperationResult<stxlib::File> refused = stxlib::OperationResult<stxlib::File>::failure(*converted.error());
        return refused;
    }
    stxlib::OperationResult<stxlib::File> result = stxlib::File::open(*converted.value(), mode);
    return result;
}

// The whole file `path` names, as stxlib::read_file reads it: refused
// (limit_exceeded) when it holds more than `limit` bytes, before anything is
// allocated for it, or grows past `limit` while it is read.
[[nodiscard]] inline stxlib::OperationResult<stxlib::Buffer<stxlib::Byte>> read_whole_file(
    const std::filesystem::path& path, const std::size_t limit) {
    const stxlib::OperationResult<stxlib::Path> converted = stxlib_path(path);
    if (converted.failed()) {
        stxlib::OperationResult<stxlib::Buffer<stxlib::Byte>> refused =
            stxlib::OperationResult<stxlib::Buffer<stxlib::Byte>>::failure(*converted.error());
        return refused;
    }
    stxlib::OperationResult<stxlib::Buffer<stxlib::Byte>> result =
        stxlib::read_file(*converted.value(), static_cast<stxlib::usize>(limit));
    return result;
}

// Reads exactly `count` bytes at `offset` into `destination`, as many reads
// as the host needs; false on a failure or when the file ends first (a short
// stream read, which set the stream's failbit).
[[nodiscard]] inline bool read_exactly_at(stxlib::File& file, const std::uint64_t offset, void* const destination,
                                          const std::size_t count) noexcept {
    stxlib::Byte* const bytes = static_cast<stxlib::Byte*>(destination);
    std::size_t done = 0;
    while (done < count) {
        const stxlib::MutableView<stxlib::Byte> rest(bytes + done, static_cast<stxlib::usize>(count - done));
        const stxlib::OperationResult<stxlib::usize> read = file.read_at(offset + done, rest);
        if (read.failed()) return false;
        const std::size_t got = static_cast<std::size_t>(*read.value());
        if (got == 0) return false;
        done += got;
    }
    return true;
}

// True when `file` holds nothing at or after `offset` (a stream's peek
// reaching the end of the file); false when it holds more or cannot be read.
[[nodiscard]] inline bool ends_at(stxlib::File& file, const std::uint64_t offset) noexcept {
    stxlib::Byte probe{};
    const stxlib::MutableView<stxlib::Byte> one(&probe, 1U);
    const stxlib::OperationResult<stxlib::usize> read = file.read_at(offset, one);
    return read.succeeded() && *read.value() == 0U;
}

// Creates the directory `path` and its missing parents; true when it exists
// as a directory afterwards (std::filesystem::create_directories without
// an error). An empty path fails, as the standard call reported for it.
[[nodiscard]] inline bool create_directories(const std::filesystem::path& path) {
    if (path.empty()) return false;
    const stxlib::OperationResult<stxlib::Path> converted = stxlib_path(path);
    if (converted.failed()) return false;
    const stxlib::OperationStatus created = stxlib::create_directories(*converted.value());
    return created.succeeded();
}

// Removes the file, link or empty directory `path` names; true when it did
// (std::filesystem::remove, whose missing name STXlib reports as not_found).
[[nodiscard]] inline bool remove_file(const std::filesystem::path& path) {
    const stxlib::OperationResult<stxlib::Path> converted = stxlib_path(path);
    if (converted.failed()) return false;
    const stxlib::OperationStatus removed = stxlib::remove(*converted.value());
    return removed.succeeded();
}

// Copies the file `from` to `to`, replacing a file there (the standard
// copy_options::overwrite_existing); true when it did.
[[nodiscard]] inline bool copy_file_replacing(const std::filesystem::path& from, const std::filesystem::path& to) {
    const stxlib::OperationResult<stxlib::Path> source = stxlib_path(from);
    if (source.failed()) return false;
    const stxlib::OperationResult<stxlib::Path> target = stxlib_path(to);
    if (target.failed()) return false;
    const stxlib::OperationStatus copied =
        stxlib::copy_file(*source.value(), *target.value(), stxlib::CopyMode::replace);
    return copied.succeeded();
}

// The host's directory for temporary files (stxlib::temporary_directory),
// or an empty path when the host names none or it cannot be spelt.
[[nodiscard]] inline std::filesystem::path temporary_directory() {
    const stxlib::OperationResult<stxlib::Path> found = stxlib::temporary_directory();
    if (found.failed()) return std::filesystem::path{};
    std::filesystem::path result = standard_path(*found.value());
    return result;
}

// `path` made absolute against the working directory (stxlib::absolute, as
// std::filesystem::absolute), or an empty path when that fails.
[[nodiscard]] inline std::filesystem::path absolute_path(const std::filesystem::path& path) {
    const stxlib::OperationResult<stxlib::Path> converted = stxlib_path(path);
    if (converted.failed()) return std::filesystem::path{};
    const stxlib::OperationResult<stxlib::Path> made = stxlib::absolute(*converted.value());
    if (made.failed()) return std::filesystem::path{};
    std::filesystem::path result = standard_path(*made.value());
    return result;
}

// One entry of a listed directory: its whole path, and its kind through a
// link (what std::filesystem::directory_entry's is_directory and
// is_regular_file read); a link that names nothing stays symbolic_link.
struct ListedEntry final {
    std::filesystem::path path{};
    stxlib::FileKind kind{stxlib::FileKind::other};
};

// The entries of the directory `directory` names, in STXlib's order (by
// name); false, with nothing listed, when it cannot be listed (missing,
// not a directory, refused). An entry no host path can spell is left out.
[[nodiscard]] inline bool list_directory_entries(const std::filesystem::path& directory,
                                                 std::vector<ListedEntry>& entries) {
    entries.clear();
    const stxlib::OperationResult<stxlib::Path> converted = stxlib_path(directory);
    if (converted.failed()) return false;
    const stxlib::OperationResult<stxlib::DirectoryListing> listed =
        stxlib::list_directory(*converted.value(), std::numeric_limits<stxlib::usize>::max());
    if (listed.failed()) return false;
    const stxlib::DirectoryListing& listing = *listed.value();
    for (stxlib::usize index = 0; index < listing.count(); ++index) {
        const stxlib::PathText name = listing.name(index);
        const stxlib::OperationResult<stxlib::Path> name_path = stxlib::Path::parse_wtf8(name.data(), name.count());
        if (name_path.failed()) continue;
        const stxlib::OperationResult<stxlib::Path> whole = (*converted.value()).join(*name_path.value());
        if (whole.failed()) continue;
        ListedEntry entry{};
        entry.path = standard_path(*whole.value());
        if (entry.path.empty()) continue;
        entry.kind = listing.kind(index);
        if (entry.kind == stxlib::FileKind::symbolic_link) {
            const stxlib::OperationResult<stxlib::FileInformation> target =
                stxlib::inspect(*whole.value(), stxlib::LinkMode::follow);
            if (target.succeeded()) entry.kind = (*target.value()).kind;
        }
        entries.push_back(std::move(entry));
    }
    return true;
}

} // namespace gui_forms::platform
