#include "gui_forms/control/stable_id/stable_id.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace gui_forms {

namespace detail {
void stable_id_literal_is_not_dot_separated_lowercase_dash_segments() noexcept {}
} // namespace detail

void StableId::require_valid() const {
    if (valid_stable_id(value_)) return;
    // Quote at most 80 bytes, so a long runtime string cannot flood the message.
    constexpr std::size_t quoted_bytes = 80U;
    std::string message("GUI.Forms stable ID \"");
    message.append(value_, 0U, quoted_bytes);
    if (value_.size() > quoted_bytes) message.append("...");
    message.append("\" is not '.'-separated segments of lowercase ASCII letters and digits joined by single '-' "
                   "(such as \"settings.ok-button\"; a Windows Forms name such as \"textBox1\" is spelled "
                   "\"text-box1\")");
    throw std::invalid_argument(message);
}

} // namespace gui_forms
