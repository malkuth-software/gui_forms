#pragma once

#include "gui_forms/theme/theme/theme.hpp"

#include <memory>
#include <vector>

// Internal: how the theme package compiler makes a theme that owns the
// images it loaded (src/core/theme/theme_compiler).

namespace gui_forms::detail {

struct ThemeAccess final {
    // As Theme::create, the theme also holding `images` until it is
    // released.
    [[nodiscard]] static std::shared_ptr<const Theme> create_holding(
        const ThemeDefinition& definition, std::vector<std::shared_ptr<const ThemeImageLease>> images,
        std::shared_ptr<const ThemeSourceRecord> source = nullptr);
    // The source the theme keeps (P12b): a compiled package's, or for a
    // theme made from a definition alone (`source` null above, or
    // Theme::create) the empty sheet's.
    [[nodiscard]] static const ThemeSourceRecord& source(const Theme& theme) noexcept;
};

} // namespace gui_forms::detail
