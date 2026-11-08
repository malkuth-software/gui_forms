#include "file_manager_demoboard/demoboard.hpp"

#include "file_manager_demoboard/fixture_model.hpp"
#include "gui_forms/gui_forms.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace file_manager_demoboard {

namespace {

struct ControllerLifetime final {
    std::vector<gui_forms::SubscriptionToken> subscriptions;
};

static constexpr std::array<double, 5> text_scales{
    1.0, 1.25, 1.5, 2.0, 2.25};

struct SurfaceSelectionHandler final {
    gui_forms::Window* product{};

    void operator()(std::optional<std::size_t> index) const {
        if (!index) return;
        if (*index == 0U) static_cast<void>(set_product_surface(*product, "folder"));
        else if (*index == 1U) static_cast<void>(set_product_surface(*product, "search"));
        else if (*index == 2U) static_cast<void>(set_product_surface(*product, "criteria"));
        else if (*index == 3U) static_cast<void>(set_product_surface(*product, "palettes"));
        else if (*index == 6U) static_cast<void>(set_product_surface(*product, "dna"));
    }
};

struct TextScaleHandler final {
    gui_forms::Window* product{};
    std::weak_ptr<gui_forms::Label> density;

    void operator()(std::optional<std::size_t> index) const {
        if (!index || *index >= text_scales.size()) return;
        (*product).set_text_scale(text_scales[*index]);
        const std::shared_ptr<gui_forms::Label> status = density.lock();
        if (status) {
            (*status).set_text(
                "Display scale: host-derived · text scale: " +
                std::to_string(static_cast<int>(text_scales[*index] * 100.0)) + "%");
        }
    }
};

struct ResetHandler final {
    gui_forms::Window* product{};
    std::weak_ptr<gui_forms::ComboBox> surface;
    std::weak_ptr<gui_forms::ComboBox> text_scale;
    std::weak_ptr<gui_forms::Label> density;
    std::weak_ptr<gui_forms::CheckBox> sound;
    std::weak_ptr<gui_forms::CheckBox> reduced_motion;

    void operator()(gui_forms::ButtonBase&) const {
        (*product).set_presentation_settings({});
        static_cast<void>(set_product_surface(*product, "folder"));
        const std::shared_ptr<gui_forms::ComboBox> surface_choice = surface.lock();
        if (surface_choice) (*surface_choice).set_selected_index(0U);
        const std::shared_ptr<gui_forms::ComboBox> scale_choice = text_scale.lock();
        if (scale_choice) (*scale_choice).set_selected_index(0U);
        const std::shared_ptr<gui_forms::Label> status = density.lock();
        if (status) {
            (*status).set_text("Display scale: host-derived · text scale: 100%");
        }
        const std::shared_ptr<gui_forms::CheckBox> sound_toggle = sound.lock();
        if (sound_toggle) (*sound_toggle).set_checked(true);
        const std::shared_ptr<gui_forms::CheckBox> motion_toggle =
            reduced_motion.lock();
        if (motion_toggle) (*motion_toggle).set_checked(false);
    }
};

enum class PresentationSettingKind { sound, reduced_motion };

struct PresentationSettingHandler final {
    gui_forms::Window* product{};
    PresentationSettingKind kind{};

    void operator()(bool enabled) const {
        gui_forms::PresentationSettings settings =
            (*product).presentation_settings();
        if (kind == PresentationSettingKind::sound) {
            settings.sound_enabled = enabled;
        } else {
            settings.reduced_motion = enabled;
        }
        (*product).set_presentation_settings(settings);
    }
};

} // namespace

std::unique_ptr<gui_forms::Window> make_controller_window(
    gui_forms::Window* product) {
    using namespace gui_forms;
    std::shared_ptr<gui_forms::FlowLayoutPanelCompat> root = std::make_shared<FlowLayoutPanelCompat>(StableId("demo.controller.window"));
    (*root).set_flow_direction(FlowDirection::top_down);
    (*root).set_wrap_contents(false);
    (*root).padding = {10,10,10,10};

    std::shared_ptr<gui_forms::Label> title = std::make_shared<Label>(StableId("demo.controller.title"),
                                         "DEMOboard controller · catalogue 001 / generation 86");
    (*title).set_requested_bounds({0,0,430,30});
    (*title).set_font({FontRole::control,12,700,false,.25});
    (*root).add_child(title);

    std::shared_ptr<gui_forms::ComboBox> surface = std::make_shared<ComboBox>(StableId("demo.surface.choice"));
    (*surface).set_requested_bounds({0,0,430,30});
    (*surface).items = {"Folder", "Search", "Criteria", "Palettes", "Icons", "Styles", "DNA"};
    (*surface).set_selected_index(0);
    (*root).add_child(surface);

    std::shared_ptr<gui_forms::ComboBox> condition = std::make_shared<ComboBox>(StableId("demo.fixture.condition"));
    (*condition).set_requested_bounds({0,0,430,30});
    (*condition).items = {"Normal", "Offline volume", "Stale index", "Preview unavailable",
                          "Transfer running", "Conflict", "Empty folder", "Million items"};
    (*condition).set_selected_index(0);
    (*root).add_child(condition);

    std::shared_ptr<gui_forms::Label> accommodation = std::make_shared<Label>(
        StableId("demo.accommodation.title"),
        "ACCOMMODATION · LOGICAL TEXT / HOST DENSITY SEPARATE");
    (*accommodation).set_requested_bounds({0,0,430,24});
    (*accommodation).set_font({FontRole::control,9,700,false,.28});
    (*root).add_child(accommodation);

    std::shared_ptr<gui_forms::ComboBox> text_scale = std::make_shared<ComboBox>(
        StableId("demo.accommodation.text-scale"));
    (*text_scale).set_requested_bounds({0,0,430,30});
    (*text_scale).accessible_name = "Product text size";
    (*text_scale).accessible_description =
        "Changes logical text measurement and large-text accommodation without "
        "changing the host display scale";
    (*text_scale).items = {"100% text", "125% text", "150% text",
                           "200% text", "225% text"};
    (*text_scale).set_selected_index(0);
    (*root).add_child(text_scale);

    std::shared_ptr<gui_forms::Label> density = std::make_shared<Label>(
        StableId("demo.accommodation.display-scale"),
        "Display scale: host-derived · text scale: 100%");
    (*density).set_requested_bounds({0,0,430,24});
    (*density).set_font({FontRole::content,10,400,false});
    (*density).accessible_name = "Presentation scale status";
    (*root).add_child(density);

    std::shared_ptr<gui_forms::CheckBox> sound = std::make_shared<CheckBox>(
        StableId("demo.accommodation.sound"), "Semantic state sounds");
    (*sound).set_requested_bounds({0,0,430,28});
    (*sound).set_checked(true);
    (*sound).accessible_description =
        "Mutes presentation only; visual state, semantics, and feedback trace remain identical";
    (*root).add_child(sound);

    std::shared_ptr<gui_forms::CheckBox> reduced_motion = std::make_shared<CheckBox>(
        StableId("demo.accommodation.reduced-motion"), "Reduced motion");
    (*reduced_motion).set_requested_bounds({0,0,430,28});
    (*reduced_motion).accessible_description =
        "Keeps live animation active at a calmer cadence and reduced excursion";
    (*root).add_child(reduced_motion);

    std::shared_ptr<gui_forms::ListBox> report = std::make_shared<ListBox>(StableId("demo.diagnostics"));
    (*report).set_requested_bounds({0,0,430,330});
    std::vector<std::string> rows;
    for (const CapabilityEntry& capability : initial_capability_report()) {
        rows.push_back(capability.id + " · " + capability.state + " · " + capability.detail);
    }
    (*report).items = std::move(rows);
    (*report).set_item_height(32);
    (*root).add_child(report);

    std::shared_ptr<gui_forms::Button> reset = std::make_shared<Button>(StableId("demo.reset"), "Reset to reference");
    (*reset).set_requested_bounds({0,0,180,30});
    (*reset).set_default_button(true);
    (*root).add_child(reset);

    std::shared_ptr<ControllerLifetime> lifetime =
        std::make_shared<ControllerLifetime>();
    if (product != nullptr) {
        (*lifetime).subscriptions.push_back(
            (*surface).selected_index_changed().subscribe(
                *root, SurfaceSelectionHandler{product}));
        (*lifetime).subscriptions.push_back(
            (*text_scale).selected_index_changed().subscribe(
                *root, TextScaleHandler{product, density}));
        (*lifetime).subscriptions.push_back((*reset).clicked().subscribe(
            *root, ResetHandler{product, surface, text_scale, density, sound,
                                reduced_motion}));
        (*lifetime).subscriptions.push_back((*sound).checked_changed().subscribe(
            *root, PresentationSettingHandler{
                product, PresentationSettingKind::sound}));
        (*lifetime).subscriptions.push_back(
            (*reduced_motion).checked_changed().subscribe(
                *root, PresentationSettingHandler{
                    product, PresentationSettingKind::reduced_motion}));
    } else {
        (*text_scale).enabled = false;
        (*text_scale).accessible_description =
            "Attach a product window to exercise text accommodation";
        (*density).set_text("Display scale: host-derived · product not attached");
        (*sound).enabled = false;
        (*reduced_motion).enabled = false;
    }

    std::unique_ptr<gui_forms::Window> window = std::make_unique<Window>(root, Size{470,700});
    (*window).perform_layout();
    (*root).set_tag(std::move(lifetime));
    return window;
}

} // namespace file_manager_demoboard
