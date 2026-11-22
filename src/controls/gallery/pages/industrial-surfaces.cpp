#include "pages.hpp"

#include "gost_paints_theme_package.hpp"
#include "industrial-surfaces_form.hpp"
#include "industrial_theme_package.hpp"

#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/indicator_lamp.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <stxlib/optional.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::gallery::pages {

namespace {

// -- the page's visual choices ------------------------------------------------------------
//
// The study's lamp colours (demo.html: green [0.16, 1, 0.25], amber [1,
// 0.5, 0.08] and red [1, 0.12, 0.04], engine_light::lamp_green, lamp_amber
// and lamp_red) encoded as sRGB and rounded, since a lamp's colour is an
// ordinary colour property.
constexpr Color lamp_green_srgb = Color::rgba(111, 255, 137);
constexpr Color lamp_amber_srgb = Color::rgba(255, 188, 80);
constexpr Color lamp_red_srgb = Color::rgba(255, 97, 56);

// The class that puts a lit key's lamps out (demo/themes/gallery/
// gallery.theme.css, `.lit-key.lamp-off`).
constexpr std::string_view lamp_off_class = "lamp-off";

// Each of the dialog's status lamps: its bake (shipped with the library,
// engine_light::shipped_light_plane_store) and its colour, as the study's
// dialog has them: the jewel (indicator_backed_up, seen through a dot).
// FAULT is failing (IndicatorLampState::fault), which takes the red. The
// keys are buttons the gallery theme draws from the shipped key bakes
// (`primitive: light-map`): CANCEL white plastic over red lamps, always
// lit, and OK clear over a frosted back, lit by the Lamps check box with
// the status lamps (the `lamp-off` class puts it out).
struct DialogLamp final {
    std::string_view bake{};
    Color colour{};
};

}  // namespace

// Owns the industrial-surfaces page (skeudoindustrial surfaces, phase 5;
// owner, 2026-11-22): the controls demo/pages/industrial-surfaces.form
// describes, the dialog's lamps lit from the shipped bakes, the Lamps
// check box that switches them, and the check box that puts the
// industrial theme (demo/themes/industrial, over the GOST paints) on the
// preview group.
class IndustrialSurfacesPage final : public gui_forms::Component {
public:
    explicit IndustrialSurfacesPage(const std::shared_ptr<Surface>& body);

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void light_dialog();
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
    // Compiled on first use and kept.
    std::shared_ptr<const Theme> industrial_{};
};

IndustrialSurfacesPage::IndustrialSurfacesPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    mount_document(body);
    light_dialog();
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

// Each dialog lamp names its bake and takes its colour; the bakes ship
// with the library, so nothing is looked for on disk.
void IndustrialSurfacesPage::light_dialog() {
    const std::array<std::shared_ptr<IndicatorLamp>, 3> lamps{document_.gallery_industrial_status_pump_ready,
                                                              document_.gallery_industrial_status_heating,
                                                              document_.gallery_industrial_status_fault};
    constexpr std::array<DialogLamp, 3> choices{{
        {.bake = "indicator_backed_up", .colour = lamp_green_srgb},
        {.bake = "indicator_backed_up", .colour = lamp_amber_srgb},
        {.bake = "indicator_backed_up", .colour = lamp_red_srgb},
    }};
    for (std::size_t index = 0U; index < lamps.size(); ++index) {
        IndicatorLamp& lamp = *lamps[index];
        lamp.light_planes = std::string(choices[index].bake);
        lamp.lamp_color = stxlib::Optional<Color>::of(choices[index].colour);
    }
}

void IndustrialSurfacesPage::on_lamps_changed(bool on) {
    (*document_.gallery_industrial_status_pump_ready).state = on ? IndicatorLampState::on : IndicatorLampState::off;
    (*document_.gallery_industrial_status_heating).state = on ? IndicatorLampState::on : IndicatorLampState::off;
    (*document_.gallery_industrial_status_fault).state = on ? IndicatorLampState::fault : IndicatorLampState::off;
    Button& ok = *document_.gallery_industrial_key_ok;
    if (on) {
        static_cast<void>(ok.remove_class(lamp_off_class));
    } else {
        static_cast<void>(ok.add_class(lamp_off_class));
    }
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
    (*context).components.add(std::make_shared<IndustrialSurfacesPage>(page));
}

}  // namespace gui_forms::gallery::pages
