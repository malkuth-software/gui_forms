#pragma once

#include "gui_forms/theme/theme/theme.hpp"

#include <memory>

namespace gui_forms::test_support {

// The Windows-familiar `windows-professional` theme, the test fixture for
// tests whose expectations (exact colours, recipes, digests, metrics) were
// written against it. The built-in default is `gui-forms` since stage T4
// phase 6 (owner decision 2026-11-08); a test that depends on the
// professional look pins it with this, through Window::set_theme,
// ApplicationResources::set_theme or Control::set_theme_override, and a
// theme-agnostic test keeps the default. Built once per process on the
// first call (thread-safe); every call returns the same theme.
[[nodiscard]] inline std::shared_ptr<const Theme> professional_theme() {
    static const std::shared_ptr<const Theme> theme = Theme::create(windows_professional_theme_definition());
    return theme;
}

} // namespace gui_forms::test_support
