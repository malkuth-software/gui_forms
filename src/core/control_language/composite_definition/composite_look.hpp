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
// of the theme in effect (detail::compile_theme_look) on its first use
// under that theme, once per theme serial, and every instance under that
// theme shares the result. Up to maximum_composite_look_entries themes'
// compilations are kept; the least recently used goes first, so switching
// back to a theme still kept reuses its compilation, and one evicted is
// compiled again. A look refused under a theme is kept as refused (no
// rules, one log line), so it is not compiled again under that theme.
//
// Ownership and threads: the definition owns its look; a control holds it
// through an alias of the definition (CompositeControl), so the look, and
// the anatomies it borrows from the definition, live while any control it
// styles does. The cache changes on the UI thread only, from the cascade's
// const paths; a borrowed rule set stays valid until the next rules_for
// call that compiles under another theme (eviction).

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

    // The rules for parts under `theme`: the theme-independent set, or the
    // set compiled for `theme`'s serial, compiled here on its first use;
    // null where the look is refused under `theme`. Borrowed (see above).
    // An allocation failure while compiling ends the program, as on the
    // cascade's other paths.
    [[nodiscard]] const CompiledThemeRules* rules_for(const Theme& theme) noexcept;

    [[nodiscard]] bool theme_bound() const noexcept { return theme_bound_; }
    // Compilations under a theme (theme-bound only), those refused, and
    // uses of a kept compilation after another theme's.
    [[nodiscard]] std::uint64_t theme_compiles() const noexcept { return theme_compiles_; }
    [[nodiscard]] std::uint64_t theme_refusals() const noexcept { return theme_refusals_; }
    [[nodiscard]] std::uint64_t theme_reuses() const noexcept { return theme_reuses_; }
    // How many themes' compilations are kept.
    [[nodiscard]] std::size_t entry_count() const noexcept { return entries_.size(); }

private:
    struct ThemeEntry final {
        std::uint64_t theme_serial{};
        // Null where the look is refused under the theme.
        std::shared_ptr<const CompiledThemeRules> rules{};
        // When the entry was last used (use_clock_).
        std::uint64_t used{};
    };

    std::string text_{};
    std::string file_{};
    std::vector<ControlAnatomy> controls_{};
    bool theme_bound_{};
    std::shared_ptr<const CompiledThemeRules> independent_{};
    // At most maximum_composite_look_entries, reserved at construction.
    std::vector<ThemeEntry> entries_{};
    // The entry rules_for returned last, by theme serial: 0 for none.
    std::uint64_t last_serial_{};
    std::uint64_t use_clock_{};
    std::uint64_t theme_compiles_{};
    std::uint64_t theme_refusals_{};
    std::uint64_t theme_reuses_{};
};

} // namespace detail
} // namespace gui_forms
