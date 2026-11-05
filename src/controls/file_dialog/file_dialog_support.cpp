#include "file_dialog_support.hpp"

#include "gui_forms/components/file_dialog_types/file_dialog_types.hpp"

#include <atomic>
#include <string>
#include <utility>

namespace gui_forms::detail {
namespace {

// Characters an extension may not hold: wildcards, path separators, and the
// characters Windows forbids in a file name.
bool is_extension_character(char character) noexcept {
    switch (character) {
    case '*':
    case '?':
    case '/':
    case '\\':
    case ':':
    case '<':
    case '>':
    case '"':
    case '|':
        return false;
    default:
        return true;
    }
}

} // namespace

bool window_alive(const std::weak_ptr<WindowLifetime>& lifetime) {
    const std::shared_ptr<WindowLifetime> locked = lifetime.lock();
    const bool result = locked != nullptr && (*locked).window != nullptr;
    return result;
}

bool file_dialog_extension_valid(std::string_view extension) noexcept {
    if (extension.empty()) return false;
    for (const char character : extension) {
        if (!is_extension_character(character)) return false;
    }
    return true;
}

bool file_dialog_has_extension(std::string_view path) {
    const std::string name = file_dialog_name_part(path);
    const std::size_t dot = name.rfind('.');
    const bool result = dot != std::string::npos && dot + 1U < name.size();
    return result;
}

ShowingScope::ShowingScope(bool& showing, std::weak_ptr<void> presence) noexcept
    : showing_(showing), presence_(std::move(presence)) {
    showing_ = true;
}

ShowingScope::~ShowingScope() {
    if (!presence_.expired()) showing_ = false;
}

} // namespace gui_forms::detail
