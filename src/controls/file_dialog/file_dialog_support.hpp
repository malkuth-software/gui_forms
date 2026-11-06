#pragma once

// Private to the dialog components (OpenFileDialog, SaveFileDialog and the
// dialogs after them): what every one of them needs while it is shown.

#include "gui_forms/window.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace gui_forms::detail {

// A new dialog request identity, unique in the process: HostServices refuses
// an identity already on its modal stack, a host may run other callbacks
// (and so other dialogs) inside its modal loop, and windows may run on
// several UI threads. Never zero. Defined in the core (host_services.cpp),
// whose surface alerts share it.
[[nodiscard]] std::uint64_t take_dialog_request_id() noexcept;

// Whether the window a lifetime record observes still exists.
[[nodiscard]] bool window_alive(const std::weak_ptr<WindowLifetime>& lifetime);

// Whether `extension` (without its dot) is one a file name may end in: not
// empty, and free of wildcards, path separators and the characters Windows
// forbids in a name.
[[nodiscard]] bool file_dialog_extension_valid(std::string_view extension) noexcept;

// Whether the name part of `path` has an extension, as .NET's
// Path.HasExtension says: a dot in the name that is not its last character.
[[nodiscard]] bool file_dialog_has_extension(std::string_view path);

// Marks a dialog as showing for one show_dialog call. The mark is cleared on
// every exit unless the dialog itself is gone, which the presence observer
// tells: `showing` lives in the dialog.
class ShowingScope final {
public:
    ShowingScope(bool& showing, std::weak_ptr<void> presence) noexcept;
    ~ShowingScope();
    ShowingScope(const ShowingScope&) = delete;
    ShowingScope& operator=(const ShowingScope&) = delete;

private:
    bool& showing_;
    std::weak_ptr<void> presence_;
};

} // namespace gui_forms::detail
