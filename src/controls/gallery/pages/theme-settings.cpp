#include "pages.hpp"

#include "gallery_theme_package.hpp"
#include "theme-settings_form.hpp"

#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/controls/list_box.hpp"
#include "gui_forms/controls/scaled_group_box.hpp"

#include <stdexcept>

namespace gui_forms::gallery::pages {

namespace {

// The themes the list offers, in its order (demo/pages/theme-settings.form).
// The first two compile the Gallery's own theme package onto their base;
// the rest are the familiar themes, which carry no Gallery roles.
enum class ThemeChoice : std::uint8_t {
    gui_forms,
    windows_professional,
    classic,
    modern_flat,
    xp_style,
    windows7_style,
    macos_style,
    count,
};

constexpr std::size_t theme_choice_count = static_cast<std::size_t>(ThemeChoice::count);

// Where the selected theme shows.
enum class ThemeScope : std::uint8_t {
    // The resources' theme: every window sharing them.
    every_window,
    // This window alone (GalleryContext::show_window_theme, Part K).
    this_window,
    // The preview group alone (Control::set_theme_override).
    preview,
};

// The scheme radios' choice.
enum class SchemeChoice : std::uint8_t { light, dark, high_contrast };

std::string_view theme_choice_id(const ThemeChoice choice) noexcept {
    switch (choice) {
    case ThemeChoice::gui_forms: return "gui-forms";
    case ThemeChoice::windows_professional: return "windows-professional";
    case ThemeChoice::classic: return "classic";
    case ThemeChoice::modern_flat: return "modern-flat";
    case ThemeChoice::xp_style: return "xp-style";
    case ThemeChoice::windows7_style: return "windows7-style";
    case ThemeChoice::macos_style: return "macos-style";
    case ThemeChoice::count: return {};
    default: return {};
    }
}

// The familiar theme a choice names; none for the two Gallery bases.
std::optional<FamiliarTheme> familiar_for(const ThemeChoice choice) noexcept {
    switch (choice) {
    case ThemeChoice::classic: return FamiliarTheme::classic;
    case ThemeChoice::modern_flat: return FamiliarTheme::modern_flat;
    case ThemeChoice::xp_style: return FamiliarTheme::xp_style;
    case ThemeChoice::windows7_style: return FamiliarTheme::windows7_style;
    case ThemeChoice::macos_style: return FamiliarTheme::macos_style;
    case ThemeChoice::gui_forms:
    case ThemeChoice::windows_professional:
    case ThemeChoice::count:
        return std::nullopt;
    default: return std::nullopt;
    }
}

} // namespace

// Owns the theme-settings page that demo/pages/theme-settings.form
// describes, and applies its choices to the running Gallery:
//   - a theme from the list, built on first use and kept (the two Gallery
//     bases are compiled into the window's resources, so their images live
//     with them; the familiar themes are process-wide builds);
//   - the scope: the resources' theme (every window sharing them), this
//     window's theme override (Part K), or the preview group's control
//     override. A wider scope clears the narrower overrides, so what it
//     sets is what shows; the preview scope changes nothing else;
//   - the colour scheme, through this window's presentation overrides:
//     light, dark (offered only while the selected theme has a dark
//     variant) or high contrast; or the host's preference, through the
//     bound Follow box (ApplicationResources::set_automatic_colour_scheme);
//   - Restore, a bound Command, which returns the resources to the theme
//     the Gallery started with and clears every override the page set.
// The window is reached through the page's controls once the Gallery is
// shown; a choice made before that changes nothing and says so. Every
// handler runs on the UI thread, which compile_theme_package requires.
class ThemeSettingsPage final : public gui_forms::Component {
public:
    ThemeSettingsPage(const std::shared_ptr<Surface>& body, const std::shared_ptr<PageContext>& context);

private:
    // The document binds the Command and the Value (FD-6).
    template <typename> friend class ThemeSettingsDocument;

    void mount_document(const std::shared_ptr<Surface>& body);
    [[nodiscard]] Window* window() const noexcept;
    [[nodiscard]] std::shared_ptr<const Theme> build_theme(Window& window, ThemeChoice choice);
    [[nodiscard]] ThemeScope scope() const noexcept;
    [[nodiscard]] SchemeChoice scheme() const noexcept;
    void apply_theme();
    void apply_scheme();
    void offer_dark(const Theme& theme);
    void refresh_state();
    void report(std::string text);
    void show_window_theme(Window& window, std::shared_ptr<const Theme> theme);

    void on_theme_selected(const ListSelectionChange& change);
    void on_scheme_checked(bool checked);
    void on_scope_checked(bool checked);
    void on_follow_host(bool follow);
    void on_restore();

    // The shell's page context, which owns this page (its components): an
    // observation, so no cycle. Its `compiled` GalleryContext shows a
    // theme in the Gallery window alone (show_window_theme).
    std::weak_ptr<PageContext> context_{};
    Command restore_{"gallery.theme-settings.restore.command", "Restore the Gallery theme"};
    Value<bool> follow_host_{false};
    // Declared after the Command and the Value its controls bind, so it is
    // released first.
    ThemeSettingsDocument<ThemeSettingsPage> document_{};
    // Built themes by ThemeChoice, null until first chosen.
    std::array<std::shared_ptr<const Theme>, theme_choice_count> themes_{};
    // The resources' theme before the page first changed it, for Restore.
    std::shared_ptr<const Theme> original_{};
    std::optional<ThemeChoice> selected_{};
    // Whether the page set this window's theme override.
    bool window_override_{};
    // Set while the page itself checks a scheme radio, so the radio's
    // change does not apply the scheme a second time.
    bool updating_scheme_{};
};

ThemeSettingsPage::ThemeSettingsPage(const std::shared_ptr<Surface>& body,
                                     const std::shared_ptr<PageContext>& context)
    : context_(context) {
    document_.initialize_component(*this);
    mount_document(body);
    on((*document_.gallery_theme_settings_themes).selection_changed(), *this,
       &ThemeSettingsPage::on_theme_selected);
    const std::array<std::shared_ptr<RadioButton>, 3> schemes{
        document_.gallery_theme_settings_scheme_light, document_.gallery_theme_settings_scheme_dark,
        document_.gallery_theme_settings_scheme_high_contrast};
    for (const std::shared_ptr<RadioButton>& radio : schemes) {
        on((*radio).checked_changed(), *this, &ThemeSettingsPage::on_scheme_checked);
    }
    const std::array<std::shared_ptr<RadioButton>, 3> scopes{
        document_.gallery_theme_settings_scope_every_window, document_.gallery_theme_settings_scope_this_window,
        document_.gallery_theme_settings_scope_preview};
    for (const std::shared_ptr<RadioButton>& radio : scopes) {
        on((*radio).checked_changed(), *this, &ThemeSettingsPage::on_scope_checked);
    }
    on(follow_host_.changed(), *this, &ThemeSettingsPage::on_follow_host);
    on(restore_.invoked(), *this, &ThemeSettingsPage::on_restore);
}

void ThemeSettingsPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move
    // into the shell's page body, a design surface of the same 1000 x 660
    // size. The copy of the child list keeps the iteration valid while the
    // root shrinks.
    Control& root = *document_.gallery_theme_settings_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
}

Window* ThemeSettingsPage::window() const noexcept {
    Window* const attached = (*document_.gallery_theme_settings_themes).attached_window();
    return attached;
}

// Builds the choice's theme on first use. A refused Gallery compile leaves
// the slot empty and returns null; the report names the first finding.
std::shared_ptr<const Theme> ThemeSettingsPage::build_theme(Window& window, const ThemeChoice choice) {
    const std::size_t slot = static_cast<std::size_t>(choice);
    if (themes_[slot]) return themes_[slot];
    const std::optional<FamiliarTheme> familiar = familiar_for(choice);
    if (familiar) {
        themes_[slot] = familiar_theme(*familiar);
        return themes_[slot];
    }
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    options.effects = &built_in_effect_registry();
    options.base = choice == ThemeChoice::gui_forms ? ThemeBase::gui_forms : ThemeBase::windows_professional;
    const ThemeLibrary library{};
    const std::shared_ptr<ApplicationResources> resources = window.resources();
    const ThemeCompileResult result = (*resources).compile_theme_package(gallery_theme_package(), library, options);
    if (!result.succeeded()) {
        std::string text = "The Gallery theme on " + std::string(theme_choice_id(choice)) + " was refused";
        if (!result.diagnostics().empty()) {
            const ThemeCompileDiagnostic& first = result.diagnostics().front();
            text += ": " + first.code + " " + first.name;
        }
        report(std::move(text));
        return nullptr;
    }
    themes_[slot] = result.theme();
    return themes_[slot];
}

ThemeScope ThemeSettingsPage::scope() const noexcept {
    if ((*document_.gallery_theme_settings_scope_this_window).checked.read()) return ThemeScope::this_window;
    if ((*document_.gallery_theme_settings_scope_preview).checked.read()) return ThemeScope::preview;
    return ThemeScope::every_window;
}

SchemeChoice ThemeSettingsPage::scheme() const noexcept {
    if ((*document_.gallery_theme_settings_scheme_dark).checked.read()) return SchemeChoice::dark;
    if ((*document_.gallery_theme_settings_scheme_high_contrast).checked.read()) return SchemeChoice::high_contrast;
    return SchemeChoice::light;
}

// Applies the selected theme at the selected scope. A theme the resources
// refuse (an inline style that does not compile against it) leaves every
// theme as it was and says why.
void ThemeSettingsPage::apply_theme() {
    Window* const target = window();
    if (target == nullptr || !selected_) {
        refresh_state();
        return;
    }
    const std::shared_ptr<const Theme> theme = build_theme(*target, *selected_);
    if (!theme) return;
    const std::shared_ptr<ApplicationResources> resources = (*target).resources();
    if (!original_) original_ = (*resources).program_theme_ptr();
    ScaledGroupBox& preview = *document_.gallery_theme_settings_preview;
    try {
        switch (scope()) {
        case ThemeScope::every_window:
            preview.clear_theme_override();
            show_window_theme(*target, nullptr);
            window_override_ = false;
            (*resources).set_theme(theme);
            break;
        case ThemeScope::this_window:
            preview.clear_theme_override();
            show_window_theme(*target, theme);
            window_override_ = true;
            break;
        case ThemeScope::preview:
            preview.set_theme_override(theme);
            break;
        default:
            preview.set_theme_override(theme);
            break;
        }
    } catch (const std::invalid_argument& refusal) {
        report(std::string("The theme was refused: ") + refusal.what());
        return;
    }
    offer_dark(*theme);
    refresh_state();
}

// Writes this window's colour-scheme and high-contrast overrides, keeping
// its other overrides. Following the host clears the scheme override, so
// the host's preference reaches the window.
void ThemeSettingsPage::apply_scheme() {
    Window* const target = window();
    if (target == nullptr) {
        refresh_state();
        return;
    }
    PresentationOverrides overrides{};
    if (const PresentationOverrides* const current = (*target).presentation_overrides()) overrides = *current;
    overrides.colour_scheme.reset();
    overrides.high_contrast.reset();
    const SchemeChoice choice = scheme();
    if (choice == SchemeChoice::high_contrast) {
        overrides.high_contrast = true;
    } else if (!follow_host_.get()) {
        overrides.colour_scheme = choice == SchemeChoice::dark ? ColourScheme::dark : ColourScheme::light;
    }
    (*target).set_presentation_overrides(overrides);
    refresh_state();
}

// Dark is offered only while the theme has a dark variant (a light-only
// theme resolves its light records in dark); a dark choice the new theme
// cannot show returns to light.
void ThemeSettingsPage::offer_dark(const Theme& theme) {
    const bool dark = theme.supports(ColourScheme::dark);
    RadioButton& dark_radio = *document_.gallery_theme_settings_scheme_dark;
    dark_radio.enabled = dark && !follow_host_.get();
    if (dark || !dark_radio.checked.read()) return;
    updating_scheme_ = true;
    (*document_.gallery_theme_settings_scheme_light).checked = true;
    updating_scheme_ = false;
    apply_scheme();
}

void ThemeSettingsPage::refresh_state() {
    Window* const target = window();
    if (target == nullptr) {
        report("Show the Gallery to apply a theme");
        return;
    }
    const std::shared_ptr<ApplicationResources> resources = (*target).resources();
    const std::string resources_id((*resources).program_theme().id());
    (*document_.gallery_theme_settings_state_resources).text = "Resources' theme: " + resources_id;
    const std::string window_id((*target).theme().id());
    (*document_.gallery_theme_settings_state_window)
        .text = window_override_ ? "This window: " + window_id + " (window override)"
                                   : "This window: " + window_id + " (follows the resources)";
    const std::shared_ptr<const Theme> preview = (*document_.gallery_theme_settings_preview).theme_override();
    (*document_.gallery_theme_settings_state_preview)
        .text = preview ? "Preview: " + std::string((*preview).id()) + " (control override)"
                          : std::string("Preview: follows the window");
    std::string scheme_text = "Scheme: ";
    const PresentationOverrides* const overrides = (*target).presentation_overrides();
    if (overrides != nullptr && (*overrides).high_contrast.value_or(false)) {
        scheme_text += "high contrast";
    } else if (overrides != nullptr && (*overrides).colour_scheme) {
        scheme_text += std::string(colour_scheme_name(*(*overrides).colour_scheme));
    } else {
        scheme_text += follow_host_.get() ? "the host's preference" : "the window's own";
    }
    (*document_.gallery_theme_settings_state_scheme).text = std::move(scheme_text);
}

void ThemeSettingsPage::report(std::string text) {
    (*document_.gallery_theme_settings_state_resources).text = std::move(text);
}

// The Gallery's own hook for a window theme (GalleryContext::
// show_window_theme, Part K); a page context without the Gallery's
// context (a page built alone) sets the window's override directly.
void ThemeSettingsPage::show_window_theme(Window& window, std::shared_ptr<const Theme> theme) {
    const std::shared_ptr<PageContext> context = context_.lock();
    if (context && (*context).compiled) {
        (*(*context).compiled).show_window_theme(std::move(theme));
        return;
    }
    window.set_theme_override(std::move(theme));
}

void ThemeSettingsPage::on_theme_selected(const ListSelectionChange& change) {
    static_cast<void>(change);
    const std::optional<std::size_t> index = (*document_.gallery_theme_settings_themes).selected_index();
    if (!index || *index >= theme_choice_count) return;
    selected_ = static_cast<ThemeChoice>(*index);
    apply_theme();
}

// A radio's change reaches both the radio unchecked and the one checked;
// only the checked one applies.
void ThemeSettingsPage::on_scheme_checked(const bool checked) {
    if (!checked || updating_scheme_) return;
    apply_scheme();
}

void ThemeSettingsPage::on_scope_checked(const bool checked) {
    if (!checked) return;
    apply_theme();
}

void ThemeSettingsPage::on_follow_host(const bool follow) {
    Window* const target = window();
    if (target == nullptr) {
        refresh_state();
        return;
    }
    (*(*target).resources()).set_automatic_colour_scheme(follow);
    (*document_.gallery_theme_settings_scheme_light).enabled = !follow;
    // Without a chosen theme the Gallery's own is shown, and dark is
    // offered as the window's own scheme would be.
    bool dark = true;
    if (selected_) {
        const std::shared_ptr<const Theme>& theme = themes_[static_cast<std::size_t>(*selected_)];
        if (theme) dark = (*theme).supports(ColourScheme::dark);
    }
    (*document_.gallery_theme_settings_scheme_dark).enabled = !follow && dark;
    apply_scheme();
}

// Returns every window sharing the resources to the theme the Gallery
// started with and clears the overrides the page set: the window's theme,
// the preview's theme, the scheme and high contrast.
void ThemeSettingsPage::on_restore() {
    Window* const target = window();
    if (target == nullptr) {
        refresh_state();
        return;
    }
    (*document_.gallery_theme_settings_preview).clear_theme_override();
    show_window_theme(*target, nullptr);
    window_override_ = false;
    if (original_) (*(*target).resources()).set_theme(original_);
    selected_.reset();
    (*document_.gallery_theme_settings_themes).clear_selection();
    updating_scheme_ = true;
    (*document_.gallery_theme_settings_scheme_light).checked = true;
    (*document_.gallery_theme_settings_scope_every_window).checked = true;
    updating_scheme_ = false;
    (*document_.gallery_theme_settings_scheme_dark).enabled = !follow_host_.get();
    PresentationOverrides overrides{};
    if (const PresentationOverrides* const current = (*target).presentation_overrides()) overrides = *current;
    overrides.colour_scheme.reset();
    overrides.high_contrast.reset();
    (*target).set_presentation_overrides(overrides);
    refresh_state();
}

void add_theme_settings(const std::shared_ptr<Surface>& page,
                        const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<ThemeSettingsPage>(page, context));
}

} // namespace gui_forms::gallery::pages
