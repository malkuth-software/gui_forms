#pragma once

#include <cstdint>
#include <string>

namespace gui_forms {

// BreadcrumbTrail's value types, apart from the control so its property
// capabilities can name them.

enum class BreadcrumbAppearance : std::uint8_t { plain, raised };

struct BreadcrumbSegment final {
    std::string stable_id{};
    std::string text{};
    std::string description{};
    bool enabled{true};
};

} // namespace gui_forms
