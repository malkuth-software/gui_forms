#pragma once

#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace gui_forms {

// The familiar themes (stage T4, phase 7; owner decisions of 2026-10-13
// and 2026-11-08): looks a Windows or macOS user recognises, recreated in
// the theme language, never the vendors' bitmaps. Literal Windows
// rendering (raised buttons, sunken cells) lives only in these themes.
// Each is a floor sheet of `:family()` blocks (as the default theme's),
// evaluated once into its recipes over a structure-only recipe; none has a
// parts sheet, so parts the engine paints draw from those recipes. Each
// follows its visual reference where the engine can express it: 7.css for
// windows7-style, XP.css for xp-style, Windows 2000's standard scheme and
// frame controls for classic, Puppertino for macos-style. A reference value an enabled
// control would draw below the contrast gate (4.5:1 text, 3:1 boundaries,
// glyphs and focus) is changed to meet it, and the sheet says so; disabled
// controls are exempt (WCAG's inactive components), so they keep the
// references' values. The Windows-familiar themes are light only (a dark
// colour scheme resolves their light records); macos-style has a dark
// floor sheet too, from Puppertino's dark appearance. High contrast is
// derived from the role model, as for every theme. `windows-professional`
// (windows_professional_theme) stays the Windows-familiar theme it was.
enum class FamiliarTheme : std::uint8_t {
    // `classic`: the Windows 2000 look, its system colours and bevels on #D4D0C8.
    classic,
    // `modern-flat`: a flat modern look whose boundaries, hover, focus and
    // input stay unequivocal (M076).
    modern_flat,
    // `xp-style`: a Luna-like look, white-to-beige plates with navy frames.
    xp_style,
    // `windows7-style`: an Aero-like look; its window glass waits for T5,
    // and until then its windows are opaque.
    windows7_style,
    // `macos-style`: a macOS-like look after Puppertino, translucent plates
    // with round corners, light and dark.
    macos_style,
    count,
};

inline constexpr std::size_t familiar_theme_count = static_cast<std::size_t>(FamiliarTheme::count);

// The theme's id (`classic`, `modern-flat`, `xp-style`, `windows7-style`,
// `macos-style`); empty for `count`.
[[nodiscard]] std::string_view familiar_theme_id(FamiliarTheme theme) noexcept;

// The theme `id` names, or none.
[[nodiscard]] std::optional<FamiliarTheme> familiar_theme_named(std::string_view id) noexcept;

// The theme's floor sheet (`<id>/floor.css` in diagnostics). Static
// storage; empty for `count`.
[[nodiscard]] std::string_view familiar_theme_floor_sheet(FamiliarTheme theme) noexcept;

// The theme's dark floor sheet (`<id>/floor-dark.css`), whose records the
// theme's dark colour scheme resolves (ThemeDarkVariant::authored).
// Static storage; empty for a light-only theme and for `count`.
[[nodiscard]] std::string_view familiar_theme_dark_floor_sheet(FamiliarTheme theme) noexcept;

// A built theme and what building it measured. When the floor sheet is
// refused, `theme` keeps windows-professional's recipes under the theme's
// id and `diagnostics` says why; the stock sheets compile with no
// diagnostics (tests/familiar_themes_tests).
struct FamiliarThemeBuild final {
    std::shared_ptr<const Theme> theme{};
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    // The compiler's work for the floor sheet (ThemeCompileWork::
    // operations), a count independent of the machine.
    std::uint64_t compile_operations{};
    // Wall time of the build, on the first call's thread.
    std::chrono::nanoseconds build_time{};
};

// The theme, built on its first call (thread-safe; later calls return the
// same build) and kept for the life of the program. `theme` is below
// familiar_theme_count.
[[nodiscard]] const FamiliarThemeBuild& familiar_theme_build(FamiliarTheme theme);
[[nodiscard]] std::shared_ptr<const Theme> familiar_theme(FamiliarTheme theme);

} // namespace gui_forms
