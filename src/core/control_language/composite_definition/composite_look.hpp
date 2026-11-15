#pragma once

#include "gui_forms/anatomy/types/anatomy_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Internal: a composite definition's compiled <look> (cp34 phase 3), which
// the controls it styles reach through Control::look_rules.
//
// A look that reads only the engine inputs and the built-in colour roles
// is theme-independent: it is compiled once, with its definition, and that
// one rule set serves every theme. A look that reads a theme's application
// roles or custom properties is theme-bound: it is compiled with the text
// of a theme (detail::compile_theme_look), once per theme serial, and every
// instance under that theme shares the result. Compilation happens only at
// fixed points before resolution (prepare): when a window shows a theme or
// a control's theme override changes, for each control the look styles,
// and when such a control is attached (Control::prepare_composite_look).
// The cascade only reads finished compilations (rules_for): one missing
// there is a defect, counted (missing), and the look draws nothing for
// that part; nothing compiles inside the style pass. Up to
// maximum_composite_look_entries themes' compilations are kept; the least
// recently prepared goes first, so switching back to a theme still kept
// reuses its compilation, and one evicted is compiled again when it is
// next prepared. A look refused under a theme is kept as refused (no
// rules, one log line), so it is not compiled again under that theme.
//
// Ownership and threads: the definition owns its look; a control holds it
// through an alias of the definition (CompositeControl), so the look, and
// the anatomies it borrows from the definition, live while any control it
// styles does. UI thread only. A rule set rules_for borrows stays valid
// until a prepare call under another theme evicts it.

namespace gui_forms {

class CompiledThemeRules;
class Theme;

inline constexpr std::size_t maximum_composite_look_entries = 8U;

namespace detail {

class CompositeLook final {
public:
    // A theme-independent look: `rules`, compiled with the definition.
    CompositeLook(std::string text, std::string file, std::shared_ptr<const CompiledThemeRules> rules);
    // A theme-bound look: `text`, named `file`, compiled per theme against
    // `controls` (the stock anatomies and the composite's own, whose views
    // are into the owning definition).
    CompositeLook(std::string text, std::string file, std::vector<ControlAnatomy> controls);
    CompositeLook(const CompositeLook&) = delete;
    CompositeLook& operator=(const CompositeLook&) = delete;
    CompositeLook(CompositeLook&&) = delete;
    CompositeLook& operator=(CompositeLook&&) = delete;
    ~CompositeLook();

    // Makes the compilation for `theme` ready: none for a
    // theme-independent look; for a theme-bound one, the kept compilation
    // for `theme`'s serial (marked used), else a new one, in place of the
    // least recently used when all are taken. Called at the fixed points
    // above, never from the cascade.
    void prepare(const Theme& theme);
    // The rules for parts under `theme`: the theme-independent set, or the
    // set prepared for `theme`'s serial; null where the look is refused
    // under `theme`, or, counted as missing (a defect), where no
    // compilation was prepared. Compiles nothing. Borrowed (see above).
    [[nodiscard]] const CompiledThemeRules* rules_for(const Theme& theme) noexcept;

    [[nodiscard]] bool theme_bound() const noexcept { return theme_bound_; }
    // Compilations under a theme (theme-bound only), those refused,
    // preparations that reused a kept compilation after another theme's,
    // and resolutions that found no prepared compilation (defects).
    [[nodiscard]] std::uint64_t theme_compiles() const noexcept { return theme_compiles_; }
    [[nodiscard]] std::uint64_t theme_refusals() const noexcept { return theme_refusals_; }
    [[nodiscard]] std::uint64_t theme_reuses() const noexcept { return theme_reuses_; }
    [[nodiscard]] std::uint64_t missing() const noexcept { return missing_; }
    // How many themes' compilations are kept.
    [[nodiscard]] std::size_t entry_count() const noexcept { return entries_.size(); }

private:
    struct ThemeEntry final {
        std::uint64_t theme_serial{};
        // Null where the look is refused under the theme.
        std::shared_ptr<const CompiledThemeRules> rules{};
        // When the entry was last prepared (use_clock_).
        std::uint64_t used{};
    };

    std::string text_{};
    std::string file_{};
    std::vector<ControlAnatomy> controls_{};
    bool theme_bound_{};
    std::shared_ptr<const CompiledThemeRules> independent_{};
    // At most maximum_composite_look_entries, reserved at construction.
    std::vector<ThemeEntry> entries_{};
    // The theme serial prepare was called with last: 0 for none.
    std::uint64_t last_serial_{};
    std::uint64_t use_clock_{};
    std::uint64_t theme_compiles_{};
    std::uint64_t theme_refusals_{};
    std::uint64_t theme_reuses_{};
    std::uint64_t missing_{};
};

} // namespace detail
} // namespace gui_forms
