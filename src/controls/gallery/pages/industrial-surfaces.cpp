#include "pages.hpp"

#include "gost_paints_theme_package.hpp"
#include "industrial-surfaces_form.hpp"
#include "industrial_theme_package.hpp"

#include "gui_forms/controls/indicator_lamp.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace gui_forms::gallery::pages {

namespace {

// -- the page's visual choices ------------------------------------------------------------
//
// The lamps the page shows, each an IndicatorLamp lit from a bake of the
// industrial-surfaces study's baker (lamp_keys.cpp: `lamps <dir> 240
// dialog` and `lamps <dir> 112 indicator`), named by its stem without
// `_up`, with its lamp's colour and its place in gallery.industrial.keys.
// The colours are the study's (engine_light::lamp_green, lamp_amber and
// lamp_red) encoded as sRGB and rounded, since a lamp's colour is an
// ordinary colour property. These are the page's choices, not rulings; the
// owner's references are still open. A lamp is an indicator (owner,
// 2026-11-12): it shows a state and is never clicked on or off, so the
// page switches them from an ordinary check box. The dialog keys are shown
// as lamps too, as before: a lit Button face (a pressed key mixing its up
// and down bakes) is not part of this page.
struct LampChoice final {
    std::string_view stem{};
    std::string_view accessible_name{};
    Color colour{};
    Rect bounds{};
};

constexpr Color lamp_green_srgb = Color::rgba(111, 255, 137);
constexpr Color lamp_amber_srgb = Color::rgba(255, 188, 80);
constexpr Color lamp_red_srgb = Color::rgba(255, 97, 56);

constexpr std::array<LampChoice, 4> lamp_choices{{
    {.stem = "dialog_ok", .accessible_name = "OK lamp", .colour = lamp_green_srgb,
     .bounds = {0.0, 0.0, 180.0, 120.0}},
    {.stem = "dialog_cancel", .accessible_name = "CANCEL lamp", .colour = lamp_amber_srgb,
     .bounds = {196.0, 0.0, 180.0, 120.0}},
    {.stem = "indicator_backed", .accessible_name = "Backed indicator", .colour = lamp_red_srgb,
     .bounds = {0.0, 132.0, 56.0, 56.0}},
    {.stem = "indicator_clear", .accessible_name = "Clear indicator", .colour = lamp_amber_srgb,
     .bounds = {72.0, 132.0, 56.0, 56.0}},
}};

// Where the bakes are read from: GUI_FORMS_LIGHT_PLANES when it is set (a
// preview of the integrator's bakes), else the source tree's
// assets/light_planes, which the build names (GUI_FORMS_LIGHT_PLANE_DIR).
std::filesystem::path light_plane_directory() {
    const char* const chosen = std::getenv("GUI_FORMS_LIGHT_PLANES");
    if (chosen != nullptr && *chosen != '\0') return std::filesystem::path(chosen);
    return std::filesystem::path(GUI_FORMS_LIGHT_PLANE_DIR);
}

// Whether the bake `name` is present in `directory`; whether it reads is
// the lamp's to find (a defective bake shows the theme's lens).
bool bake_present(const std::filesystem::path& directory, const std::string& name) noexcept {
    std::error_code error{};
    const bool present = std::filesystem::is_regular_file(directory / (name + ".f32"), error);
    return present && !error;
}

}  // namespace

// Owns the industrial-surfaces page (skeudoindustrial surfaces, phase 5):
// the controls demo/pages/industrial-surfaces.form describes, the lamps it
// places (lamp_choices), lit from the shipped bakes where they are present,
// the Lamps check box that switches them, and the check box that puts the
// industrial theme (demo/themes/industrial, over the GOST paints) on the
// preview group.
class IndustrialSurfacesPage final : public gui_forms::Component {
public:
    explicit IndustrialSurfacesPage(const std::shared_ptr<Surface>& body);
    // Points the Gallery window's resources at the bakes, so the lamps'
    // lenses are composited from them (the resources read each on first
    // use).
    static void install_bakes(const PageContext& context);

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void place_lamps();
    void on_lamps_changed(bool on);
    void on_theme_changed(bool on);
    // Start and Stop switch the Running lamp; the Coolant check box its
    // flow lamp. The lamps are never clicked themselves.
    void on_start(ButtonBase& button);
    void on_stop(ButtonBase& button);
    void on_coolant_changed(bool on);
    [[nodiscard]] std::shared_ptr<const Theme> build_industrial_theme(Window& window);

    // Owns the document's controls; the shell's page body holds them too
    // once mount_document() has moved them there.
    IndustrialSurfacesDocument<IndustrialSurfacesPage> document_{};
    std::vector<std::shared_ptr<IndicatorLamp>> lamps_{};
    // Compiled on first use and kept.
    std::shared_ptr<const Theme> industrial_{};
};

IndustrialSurfacesPage::IndustrialSurfacesPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    mount_document(body);
    place_lamps();
    on((*document_.gallery_industrial_lamps).checked_changed(), *this, &IndustrialSurfacesPage::on_lamps_changed);
    on((*document_.gallery_industrial_theme).checked_changed(), *this, &IndustrialSurfacesPage::on_theme_changed);
    on((*document_.gallery_industrial_preview_start).clicked(), *this, &IndustrialSurfacesPage::on_start);
    on((*document_.gallery_industrial_preview_stop).clicked(), *this, &IndustrialSurfacesPage::on_stop);
    on((*document_.gallery_industrial_preview_coolant).checked_changed(), *this,
       &IndustrialSurfacesPage::on_coolant_changed);
}

void IndustrialSurfacesPage::on_start(ButtonBase&) {
    (*document_.gallery_industrial_preview_running).state = IndicatorLampState::on;
}

void IndustrialSurfacesPage::on_stop(ButtonBase&) {
    (*document_.gallery_industrial_preview_running).state = IndicatorLampState::off;
}

void IndustrialSurfacesPage::on_coolant_changed(bool on) {
    (*document_.gallery_industrial_preview_coolant_lamp).state = on ? IndicatorLampState::on : IndicatorLampState::off;
}

void IndustrialSurfacesPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move
    // into the shell's page body, a design surface of the same 1000 x 660
    // size. The copy of the child list keeps the iteration valid while the
    // root shrinks.
    Control& root = *document_.gallery_industrial_surfaces_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
}

// One lamp for each choice, named after its bake; a lamp whose bake is
// missing shows the theme's lens. The status line says which bakes are
// missing and how they are made.
void IndustrialSurfacesPage::place_lamps() {
    Control& keys = *document_.gallery_industrial_keys;
    const std::filesystem::path directory = light_plane_directory();
    std::size_t missing = 0;
    for (const LampChoice& choice : lamp_choices) {
        const std::string bake = std::string(choice.stem) + "_up";
        if (!bake_present(directory, bake)) ++missing;
        // A stable ID joins its words by '-', so the stem's '_' becomes '-'.
        std::string id = "gallery.industrial.lamp." + std::string(choice.stem);
        std::replace(id.begin(), id.end(), '_', '-');
        std::shared_ptr<IndicatorLamp> lamp = make_control<IndicatorLamp>(StableId(id));
        (*lamp).accessible_name = std::string(choice.accessible_name);
        (*lamp).light_planes = bake;
        (*lamp).lamp_color = std::optional<Color>{choice.colour};
        (*lamp).set_bounds(choice.bounds);
        keys.add_child(lamp);
        lamps_.push_back(std::move(lamp));
    }
    std::string text{};
    if (missing == lamp_choices.size()) {
        text = "No light-plane bakes in " + directory.string() +
               ": the lamps show the theme's lens; the integrator bakes them with the study's baker (lamps <dir> 240 "
               "dialog; lamps <dir> 112 indicator).";
    } else if (missing != 0U) {
        text = std::to_string(missing) +
               " of the lamps' bakes are missing and show the theme's lens; the rest light their own lens and legend only.";
    } else {
        text = "Each lamp lights its own lens and legend only; the check box switches them, as a lamp is never clicked.";
    }
    (*document_.gallery_industrial_bakes).text = std::move(text);
}

void IndustrialSurfacesPage::on_lamps_changed(bool on) {
    for (const std::shared_ptr<IndicatorLamp>& lamp : lamps_) {
        (*lamp).state = on ? IndicatorLampState::on : IndicatorLampState::off;
    }
}

void IndustrialSurfacesPage::install_bakes(const PageContext& context) {
    if (!context.compiled) return;
    GalleryWindow* const window = (*context.compiled).window;
    if (window == nullptr) return;
    const std::shared_ptr<ApplicationResources> resources = (*window).resources();
    (*resources).set_light_plane_directory(light_plane_directory());
}

std::shared_ptr<const Theme> IndustrialSurfacesPage::build_industrial_theme(Window& window) {
    if (industrial_) return industrial_;
    ThemeLibrary library{};
    const std::vector<ThemeCompileDiagnostic> installed = library.install(gost_paints_theme_package());
    if (!installed.empty()) {
        (*document_.gallery_industrial_preview_state).text =
            "The GOST palette was refused: " + installed.front().code + " " + installed.front().name;
        return nullptr;
    }
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    options.effects = &built_in_effect_registry();
    options.base = ThemeBase::gui_forms;
    const std::shared_ptr<ApplicationResources> resources = window.resources();
    const ThemeCompileResult result = (*resources).compile_theme_package(industrial_theme_package(), library, options);
    if (!result.succeeded()) {
        std::string text = "The industrial theme was refused";
        if (!result.diagnostics().empty()) {
            const ThemeCompileDiagnostic& first = result.diagnostics().front();
            text += ": " + first.code + " " + first.name;
        }
        (*document_.gallery_industrial_preview_state).text = std::move(text);
        return nullptr;
    }
    industrial_ = result.theme();
    return industrial_;
}

void IndustrialSurfacesPage::on_theme_changed(bool on) {
    ScaledGroupBox& preview = *document_.gallery_industrial_preview;
    if (!on) {
        preview.clear_theme_override();
        (*document_.gallery_industrial_preview_state).text = "";
        return;
    }
    Window* const window = preview.attached_window();
    if (window == nullptr) return;
    std::shared_ptr<const Theme> theme = build_industrial_theme(*window);
    if (!theme) return;
    preview.set_theme_override(std::move(theme));
    (*document_.gallery_industrial_preview_state).text =
        "Pale mint keys in orange peel over grey-green, blue-green hammertone wells: GOST 319, 365 and 397.";
}

void add_industrial_surfaces(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    IndustrialSurfacesPage::install_bakes(*context);
    (*context).components.add(std::make_shared<IndustrialSurfacesPage>(page));
}

}  // namespace gui_forms::gallery::pages
