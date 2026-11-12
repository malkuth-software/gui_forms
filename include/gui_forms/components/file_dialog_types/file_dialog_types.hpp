#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/host/types/host_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// Why a Windows Forms filter string was refused (parse_file_dialog_filter).
enum class FileDialogFilterError : std::uint8_t {
    none,
    // The string has an odd number of `|`-separated parts: a label without
    // its patterns.
    unpaired_label,
    // A pattern list is empty, or one of its `;`-separated patterns is.
    empty_pattern,
    // A pattern other than `*`, `*.*` or `*.extension`: the host dialogs
    // filter by extension only.
    unsupported_pattern,
    // Text that is not valid UTF-8, holds NUL, or is longer than
    // HostServices::maximum_dialog_text_bytes.
    invalid_text,
    // More filters than HostServices::maximum_dialog_filters, or more
    // patterns in one filter than HostServices::maximum_dialog_extensions.
    too_many,
};

// The result of parsing a filter string: the host filters in order, or the
// first error with no filters.
struct GUI_FORMS_API FileDialogFilterParse final {
    FileDialogFilterError error{FileDialogFilterError::none};
    std::vector<HostFileDialogFilter> filters{};
};

// Parses a filter in Windows Forms' syntax, `label|patterns|label|patterns`,
// where patterns are separated by `;` and spaces around a pattern are
// ignored: "Text files|*.txt;*.log|All files|*.*". `*.extension` becomes the
// host filter's extension (without the dot; `*.tar.gz` gives `tar.gz`), and
// `*` and `*.*` become `*`, which every host reads as any file. An empty
// string is no filter: the host shows every file.
[[nodiscard]] GUI_FORMS_API FileDialogFilterParse parse_file_dialog_filter(std::string_view filter);

// Whether a file name passes a host filter, as the portable hosts filter a
// directory listing: true when the filter has no extensions or holds `*`
// or `*.*`, or when the name ends in `.` followed by one of its extensions. Letters
// compare without regard to ASCII case, so `png` matches `photo.PNG` and
// `PNG` matches `photo.png`; a multi-dotted extension such as `tar.gz`
// matches `backup.tar.gz`. A name that is only the extension (`.png`) does
// not match. Leading `*` and `.` of an extension are ignored, so a hand-built
// `*.png` or `.png` reads as `png`. `file_name` is a name, not a path; pass file_dialog_name_part
// of a path.
[[nodiscard]] GUI_FORMS_API bool file_dialog_filter_matches(const HostFileDialogFilter& filter, std::string_view file_name);

// Whether `text` may be a dialog's title, directory or name: valid UTF-8
// without NUL, of at most HostServices::maximum_dialog_text_bytes.
[[nodiscard]] GUI_FORMS_API bool file_dialog_text_valid(std::string_view text);

// Raised by a file dialog after the user accepts and before the dialog
// takes the names (Windows Forms' FileOk). `file_names` holds the chosen
// paths and stays valid for the handler's call only. Setting `cancel` keeps
// the dialog's old names and shows the dialog again with the same settings,
// where Windows Forms keeps its dialog open.
struct GUI_FORMS_API FileDialogOk final {
    std::span<const std::string> file_names{};
    bool cancel{};
};

// The last component of a host path: the file's name without its
// directory, by the path rules of the platform the program runs on
// (Windows accepts `\` and `/`, the others `/` only). Empty for a path that
// ends in a separator.
[[nodiscard]] GUI_FORMS_API std::string file_dialog_name_part(std::string_view path);

} // namespace gui_forms
