#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace gui_forms::host::detail {

// The program's name as the application menu shows it, resolved as AppKit
// resolves it: the bundle's CFBundleDisplayName (localized when the bundle
// localizes it), then its CFBundleName, then the process name. An empty
// `bundle_path` reads the main bundle; an unbundled executable has no names
// and so gets its process name.
[[nodiscard]] std::string application_menu_name(const std::filesystem::path& bundle_path = {});

// The application menu's quit item title for `program_name`.
[[nodiscard]] std::string quit_menu_title(std::string_view program_name);

// Installs the menu bar: one application menu whose Quit item (Cmd+Q) names
// this program. Main thread only.
void install_application_menu();

} // namespace gui_forms::host::detail
