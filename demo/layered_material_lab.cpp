#include "layered_material_lab.hpp"

#include "gui_forms/gui_forms.hpp"
#include "visual_inspector_poll.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms::layered_material_lab {
namespace {

constexpr Color ink = Color::rgba(29, 45, 75);
constexpr Color muted = Color::rgba(94, 111, 128);
constexpr Color paper = Color::rgba(246, 248, 249);

class LabRoot final : public Panel {
public:
    explicit LabRoot(StableId id) : Panel(std::move(id)) {}

    void retain(SubscriptionToken token) {
        subscriptions_.push_back(std::move(token));
    }
    void retain(FrameRequestToken token) {
        frames_.push_back(std::move(token));
    }

private:
    std::vector<SubscriptionToken> subscriptions_;
    std::vector<FrameRequestToken> frames_;
};

template <typename Parent, typename Child>
void add_at(const std::shared_ptr<Parent>& parent,
            const std::shared_ptr<Child>& child, Rect bounds,
            AnchorStyles anchor = AnchorStyles::top | AnchorStyles::left) {
    (*child).set_requested_bounds(bounds);
    (*child).set_anchor(anchor);
    (*parent).add_child(child);
}

std::shared_ptr<Label> label(std::string id, std::string text,
                             FontSpec font, Color color) {
    std::shared_ptr<Label> result = make_control<Label>(
        StableId(std::move(id)), std::move(text));
    (*result).set_font(font);
    (*result).set_foreground(color);
    return result;
}

SurfaceMaterial office_hot_material() {
    SurfaceMaterial result = office_pearl_material();
    result.fills.insert(result.fills.begin() + 1,
        MaterialFillLayer::radial({0.5, -0.1}, {0.8, 0.9}, {
            {0.0, Color::rgba(133, 206, 247, 92)},
            {1.0, Color::rgba(133, 206, 247, 0)},
        }));
    result.shadows = {
        {{0.0, 2.0}, 3.0, 0.0, Color::rgba(36, 69, 94, 68), false},
        {{0.0, 1.0}, 1.0, 0.0, Color::rgba(255, 255, 255, 210), true},
    };
    result.border = MaterialBorder{Color::rgba(83, 126, 161), 1.0};
    return result;
}

SurfaceMaterial office_pressed_material() {
    SurfaceMaterial result;
    result.fills = {
        MaterialFillLayer::linear_css_angle(180.0, {
            {0.0, Color::rgba(174, 198, 217)},
            {0.44, Color::rgba(207, 223, 235)},
            {1.0, Color::rgba(250, 253, 255)},
        }),
    };
    result.shadows = {
        {{0.0, 2.0}, 3.0, 0.0, Color::rgba(42, 70, 92, 112), true},
        {{0.0, -1.0}, 1.0, 0.0, Color::rgba(255, 255, 255, 118), true},
    };
    result.border = MaterialBorder{Color::rgba(61, 94, 121), 1.0};
    result.keylines = {
        {MaterialEdge::bottom, Color::rgba(255, 255, 255, 156), 1.0, 1.0},
    };
    result.corner_radius = 3.0;
    return result;
}

SurfaceMaterial office_disabled_material() {
    SurfaceMaterial result;
    result.fills = {
        MaterialFillLayer::linear_css_angle(180.0, {
            {0.0, Color::rgba(243, 245, 247)},
            {1.0, Color::rgba(207, 213, 218)},
        }),
    };
    result.border = MaterialBorder{Color::rgba(150, 159, 166), 1.0};
    result.keylines = {
        {MaterialEdge::top, Color::rgba(255, 255, 255, 150), 1.0, 1.0},
    };
    result.corner_radius = 3.0;
    return result;
}

SurfaceMaterial focused_pearl_material() {
    SurfaceMaterial result = office_pearl_material();
    result.keylines.push_back(
        {MaterialEdge::top, Color::rgba(41, 79, 145), 2.0, 2.0});
    result.keylines.push_back(
        {MaterialEdge::bottom, Color::rgba(41, 79, 145), 2.0, 2.0});
    return result;
}

std::vector<std::byte> studio_patch_pixels() {
    constexpr std::size_t extent = 12U;
    std::vector<std::byte> pixels(extent * extent * 4U);
    for (std::size_t y = 0U; y < extent; ++y) {
        for (std::size_t x = 0U; x < extent; ++x) {
            Color color = Color::rgba(232, 235, 231);
            if (x == 0U || y == 0U) color = Color::rgba(255, 255, 250);
            if (x == extent - 1U || y == extent - 1U) {
                color = Color::rgba(119, 137, 149);
            } else if (x <= 2U || y <= 2U) {
                color = Color::rgba(247, 244, 232);
            } else if (x >= extent - 3U || y >= extent - 3U) {
                color = Color::rgba(198, 210, 216);
            }
            const std::size_t offset = (y * extent + x) * 4U;
            pixels[offset] = static_cast<std::byte>(color.blue);
            pixels[offset + 1U] = static_cast<std::byte>(color.green);
            pixels[offset + 2U] = static_cast<std::byte>(color.red);
            pixels[offset + 3U] = static_cast<std::byte>(color.alpha);
        }
    }
    return pixels;
}

struct SelectTarget final {
    std::weak_ptr<VisualInspectorView> view;
    std::weak_ptr<VisualInspectorOverlay> overlay;
    std::string target;

    void operator()(ButtonBase&) const {
        if (const std::shared_ptr<VisualInspectorView> strong = view.lock()) {
            (*strong).set_target_stable_id(target);
        }
        if (const std::shared_ptr<VisualInspectorOverlay> strong = overlay.lock()) {
            (*strong).set_target_stable_id(target);
        }
    }
};

struct ToggleContrast final {
    Window* window{};
    void operator()(ButtonBase&) const {
        PresentationSettings next = (*window).presentation_settings();
        next.high_contrast = !next.high_contrast;
        (*window).set_presentation_settings(next);
    }
};

struct CycleTextScale final {
    Window* window{};
    std::weak_ptr<Button> button;
    void operator()(ButtonBase&) const {
        const double current = (*window).presentation_settings().text_scale;
        const double next = current < 1.20 ? 1.25 : (current < 1.45 ? 1.5 : 1.0);
        (*window).set_text_scale(next);
        if (const std::shared_ptr<Button> strong = button.lock()) {
            (*strong).set_text("Text scale " + std::to_string(
                static_cast<int>(next * 100.0)) + "%");
        }
    }
};

void add_specimen_copy(const std::shared_ptr<Control>& specimen,
                       std::string prefix, std::string title,
                       std::string detail, Color title_color,
                       Color detail_color) {
    add_at(specimen,
        label(prefix + ".title", std::move(title),
              {FontRole::control, 15.0, 700, false, 0.12}, title_color),
        {18.0, 10.0, 330.0, 36.0});
    add_at(specimen,
        label(prefix + ".detail", std::move(detail),
              {FontRole::content, 10.0, 400, false}, detail_color),
        {18.0, 50.0, 330.0, 42.0});
}

} // namespace

SurfaceMaterial watercolor_fresco_material() {
    SurfaceMaterial result;
    result.fills = {
        MaterialFillLayer::linear_css_angle(92.0, {
            {0.0, Color::rgba(23, 52, 127)},
            {0.58, Color::rgba(58, 104, 203)},
            {1.0, Color::rgba(217, 104, 114)},
        }),
        MaterialFillLayer::radial({0.18, -0.08}, {0.45, 1.25}, {
            {0.0, Color::rgba(146, 217, 255, 136)},
            {1.0, Color::rgba(146, 217, 255, 0)},
        }),
        MaterialFillLayer::radial({0.62, 1.25}, {0.42, 1.0}, {
            {0.0, Color::rgba(214, 178, 255, 120)},
            {1.0, Color::rgba(214, 178, 255, 0)},
        }),
        MaterialFillLayer::radial({0.96, 0.92}, {0.28, 0.8}, {
            {0.0, Color::rgba(255, 195, 142, 132)},
            {1.0, Color::rgba(255, 195, 142, 0)},
        }),
    };
    result.shadows = {
        {{0.0, 4.0}, 7.0, 0.0, Color::rgba(19, 39, 91, 96), false},
        {{0.0, 1.0}, 1.0, 0.0, Color::rgba(255, 255, 255, 72), true},
    };
    result.keylines = {
        {MaterialEdge::bottom, Color::rgba(23, 45, 105), 1.0, 0.0},
        {MaterialEdge::bottom, Color::rgba(215, 242, 255, 196), 1.0, 2.0},
    };
    result.corner_radius = 5.0;
    return result;
}

SurfaceMaterial office_pearl_material() {
    SurfaceMaterial result;
    result.fills = {
        MaterialFillLayer::linear_css_angle(180.0, {
            {0.0, Color::rgba(255, 255, 255)},
            {0.46, Color::rgba(250, 253, 255)},
            {0.50, Color::rgba(235, 242, 248)},
            {1.0, Color::rgba(201, 214, 229)},
        }),
        MaterialFillLayer::linear(
            {0.0, 0.0}, {0.0, 8.0}, {
                {0.0, Color::rgba(255, 255, 255, 20)},
                {1.0, Color::rgba(119, 140, 158, 8)},
            }, MaterialCoordinateSpace::logical,
            GradientSpreadMode::reflect),
    };
    result.shadows = {
        {{0.0, 2.0}, 3.0, 0.0, Color::rgba(52, 77, 99, 52), false},
        {{0.0, 1.0}, 1.0, 0.0, Color::rgba(255, 255, 255, 220), true},
    };
    result.border = MaterialBorder{Color::rgba(132, 156, 178), 1.0};
    result.keylines = {
        {MaterialEdge::top, Color::rgba(255, 255, 255, 226), 1.0, 1.0},
        {MaterialEdge::bottom, Color::rgba(116, 139, 160, 132), 1.0, 1.0},
    };
    result.corner_radius = 3.0;
    return result;
}

SurfaceMaterial workshop_graphite_material() {
    SurfaceMaterial result;
    result.fills = {
        MaterialFillLayer::linear_css_angle(180.0, {
            {0.0, Color::rgba(98, 108, 115)},
            {0.52, Color::rgba(75, 84, 90)},
            {1.0, Color::rgba(66, 74, 79)},
        }),
        MaterialFillLayer::repeating_linear(
            {0.0, 0.0}, {4.0, 4.0}, {
                {0.0, Color::rgba(255, 255, 255, 9)},
                {0.24, Color::rgba(255, 255, 255, 9)},
                {0.26, Color::rgba(25, 34, 40, 4)},
                {1.0, Color::rgba(25, 34, 40, 4)},
            }),
    };
    result.shadows = {
        {{0.0, 1.0}, 2.0, 0.0, Color::rgba(28, 39, 46, 112), true},
    };
    result.keylines = {
        {MaterialEdge::top, Color::rgba(146, 157, 164), 1.0, 0.0},
        {MaterialEdge::bottom, Color::rgba(43, 54, 61), 1.0, 0.0},
        {MaterialEdge::left, Color::rgba(128, 141, 148), 1.0, 1.0},
        {MaterialEdge::right, Color::rgba(48, 58, 64), 1.0, 1.0},
    };
    result.corner_radius = 2.0;
    return result;
}

SurfaceMaterial studio_caption_material(ImageId nine_patch_image) {
    SurfaceMaterial result;
    result.fills = {
        MaterialFillLayer::nine_patch(
            nine_patch_image, {12.0, 12.0}, {3.0, 3.0, 3.0, 3.0}),
        MaterialFillLayer::linear_css_angle(90.0, {
            {0.0, Color::rgba(229, 233, 231, 40)},
            {0.72, Color::rgba(248, 244, 233, 90)},
            {1.0, Color::rgba(248, 244, 233, 0)},
        }),
    };
    result.shadows = {
        {{0.0, 1.0}, 2.0, 0.0, Color::rgba(65, 81, 92, 68), true},
    };
    result.keylines = {
        {MaterialEdge::left, Color::rgba(58, 104, 203), 3.0, 4.0},
        {MaterialEdge::bottom, Color::rgba(126, 144, 156), 1.0, 0.0},
    };
    return result;
}

ControlStateRecipes office_pearl_state_recipes() {
    std::array<ControlVisualRecipe, control_surface_state_count> values;
    for (ControlVisualRecipe& value : values) {
        value.material = office_pearl_material();
        value.text = ink;
        value.muted_text = muted;
        value.glyph = ink;
        value.focus_ring = Color::rgba(41, 79, 145);
        value.focus_width = 2.0;
        value.focus_offset = 2.0;
        value.authored_focus_outline = true;
        value.default_width = 0.0;
        value.pressed_content_offset = {1.0, 1.0};
    }
    values[static_cast<std::size_t>(ControlSurfaceState::hot)].material =
        office_hot_material();
    values[static_cast<std::size_t>(ControlSurfaceState::pressed)].material =
        office_pressed_material();
    values[static_cast<std::size_t>(ControlSurfaceState::pending)].material =
        focused_pearl_material();
    values[static_cast<std::size_t>(ControlSurfaceState::invalid)].material =
        office_pressed_material();
    values[static_cast<std::size_t>(ControlSurfaceState::invalid)].text =
        Color::rgba(113, 48, 61);
    values[static_cast<std::size_t>(ControlSurfaceState::disabled)].material =
        office_disabled_material();
    values[static_cast<std::size_t>(ControlSurfaceState::disabled)].text =
        Color::rgba(117, 126, 134);
    values[static_cast<std::size_t>(ControlSurfaceState::deactivated)].material =
        office_disabled_material();
    return ControlStateRecipes::from_parts(values.data(), values.size());
}

bool over_budget_recipe_is_rejected() {
    const MaterialFillLayer fill =
        MaterialFillLayer::solid(Color::rgba(255, 255, 255));
    std::array<MaterialKeyline, SurfaceMaterial::maximum_keylines + 1U>
        keylines{};
    try {
        static_cast<void>(SurfaceMaterial::from_parts(
            &fill, 1U, nullptr, 0U, nullptr, nullptr,
            keylines.data(), keylines.size(), 0.0));
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

std::unique_ptr<Window> make_layered_material_lab() {
    std::shared_ptr<LabRoot> root = make_control<LabRoot>(
        StableId("layered-material.root"));
    (*root).set_background(Color::rgba(218, 224, 228));

    std::shared_ptr<Control> header = make_control<Control>(
        StableId("layered-material.header"));
    (*header).set_authored_surface_material(watercolor_fresco_material());
    add_at(root, header, {0.0, 0.0, 1400.0, 64.0},
           AnchorStyles::top | AnchorStyles::left | AnchorStyles::right);
    add_at(header,
        label("layered-material.header.title",
              "GUI.Forms Layered Material Fidelity",
              {FontRole::control, 19.0, 700, false, 0.14},
              Color::rgba(255, 255, 255)),
        {22.0, 0.0, 560.0, 40.0});
    add_at(header,
        label("layered-material.header.subtitle",
              "Accepted House Composite recipes · retained CPU paint · inspectable order",
              {FontRole::content, 10.0, 400, false},
              Color::rgba(229, 242, 255)),
        {24.0, 40.0, 650.0, 22.0});

    struct Spec final {
        const char* id;
        const char* title;
        const char* detail;
        Rect bounds;
        SurfaceMaterial material;
        Color title_color;
        Color detail_color;
    };
    std::array<Spec, 3U> specs{{
        {"layered-material.watercolor", "WATERCOLOR FRESCO",
         "4 fills · 3 glows · edge + inset shine",
         {22.0, 82.0, 380.0, 166.0}, watercolor_fresco_material(),
         Color::rgba(255, 255, 255), Color::rgba(226, 240, 255)},
        {"layered-material.office", "OFFICE PEARL STOCK",
         "pad/reflect · inset/outset · edge keys",
         {418.0, 82.0, 380.0, 166.0}, office_pearl_material(), ink, muted},
        {"layered-material.graphite", "WORKSHOP GRAPHITE",
         "middle value · texture · 4 edge keys",
         {22.0, 264.0, 380.0, 166.0}, workshop_graphite_material(),
         Color::rgba(245, 248, 249), Color::rgba(216, 225, 229)},
    }};
    for (const Spec& spec : specs) {
        std::shared_ptr<Control> specimen = make_control<Control>(
            StableId(std::string(spec.id)));
        (*specimen).set_authored_surface_material(spec.material);
        add_at(root, specimen, spec.bounds);
        add_specimen_copy(specimen, spec.id, spec.title, spec.detail,
                          spec.title_color, spec.detail_color);
    }

    std::shared_ptr<Control> studio = make_control<Control>(
        StableId("layered-material.studio"));
    add_at(root, studio, {418.0, 264.0, 380.0, 166.0});
    add_specimen_copy(studio, "layered-material.studio",
                      "STUDIO 2003 CAPTION / WELL",
                      "12 px cap-inset · warm well · blue line",
                      ink, muted);

    add_at(root,
        label("layered-material.states.title", "OFFICE PEARL STATE MATRIX",
              {FontRole::control, 10.0, 700, false, 0.16}, ink),
        {22.0, 450.0, 300.0, 22.0});
    const std::array<std::pair<const char*, SurfaceMaterial>, 5U> states{{
        {"NORMAL", office_pearl_material()},
        {"HOT", office_hot_material()},
        {"PRESSED", office_pressed_material()},
        {"FOCUSED", focused_pearl_material()},
        {"DISABLED", office_disabled_material()},
    }};
    for (std::size_t index = 0U; index < states.size(); ++index) {
        const std::string id = "layered-material.state." +
                               std::to_string(index);
        std::shared_ptr<Control> swatch = make_control<Control>(StableId(id));
        (*swatch).set_authored_surface_material(states[index].second);
        add_at(root, swatch,
               {22.0 + static_cast<double>(index) * 154.0, 478.0,
                140.0, 62.0});
        add_at(swatch,
            label(id + ".label", states[index].first,
                  {FontRole::control, 9.0, 700, false, 0.12},
                  index == 4U ? Color::rgba(117, 126, 134) : ink),
            {14.0, 20.0, 112.0, 22.0});
    }

    std::shared_ptr<Button> live = make_control<Button>(
        StableId("layered-material.live-state"),
        "Live Pearl — hover / press / Tab");
    (*live).set_visual_recipes(office_pearl_state_recipes());
    (*live).set_font({FontRole::control, 11.0, 700, false, 0.1});
    add_at(root, live, {22.0, 560.0, 362.0, 42.0});

    std::shared_ptr<Button> disabled = make_control<Button>(
        StableId("layered-material.live-disabled"), "Actually disabled");
    (*disabled).set_visual_recipes(office_pearl_state_recipes());
    (*disabled).set_enabled(false);
    add_at(root, disabled, {400.0, 560.0, 190.0, 42.0});

    const bool rejected = over_budget_recipe_is_rejected();
    add_at(root,
        label("layered-material.rejection",
              rejected
                  ? "REJECTED AS REQUIRED · 9 keylines exceeds retained limit 8"
                  : "ERROR · over-budget material was accepted",
              {FontRole::monospace, 9.0, 700, false},
              rejected ? Color::rgba(53, 112, 73) : Color::rgba(145, 40, 54)),
        {22.0, 616.0, 520.0, 24.0});

    std::shared_ptr<VisualInspectorView> inspector =
        make_control<VisualInspectorView>(
            StableId("layered-material.inspector"),
            "layered-material.watercolor");
    add_at(root, inspector, {822.0, 76.0, 550.0, 630.0},
           AnchorStyles::top | AnchorStyles::bottom | AnchorStyles::left |
               AnchorStyles::right);

    std::shared_ptr<VisualInspectorOverlay> overlay =
        make_control<VisualInspectorOverlay>(
            StableId("layered-material.overlay"),
            "layered-material.watercolor");

    const std::array<std::pair<const char*, const char*>, 6U> targets{{
        {"Watercolor", "layered-material.watercolor"},
        {"Office", "layered-material.office"},
        {"Graphite", "layered-material.graphite"},
        {"Studio", "layered-material.studio"},
        {"Live state", "layered-material.live-state"},
        {"Focus", "layered-material.state.3"},
    }};
    for (std::size_t index = 0U; index < targets.size(); ++index) {
        std::shared_ptr<Button> selector = make_control<Button>(
            StableId("layered-material.target." + std::to_string(index)),
            targets[index].first);
        (*selector).set_visual_style(ButtonVisualStyle::command);
        add_at(root, selector,
               {22.0 + static_cast<double>(index) * 126.0, 660.0,
                116.0, 30.0});
        (*root).retain((*selector).clicked().subscribe(
            *root, SelectTarget{inspector, overlay, targets[index].second}));
    }

    std::shared_ptr<Button> contrast = make_control<Button>(
        StableId("layered-material.toggle-contrast"), "High contrast");
    add_at(root, contrast, {596.0, 560.0, 202.0, 30.0});
    std::shared_ptr<Button> scale = make_control<Button>(
        StableId("layered-material.cycle-scale"), "Text scale 100%");
    add_at(root, scale, {596.0, 598.0, 202.0, 30.0});

    add_at(root, overlay, {0.0, 0.0, 1400.0, 760.0},
           AnchorStyles::top | AnchorStyles::bottom | AnchorStyles::left |
               AnchorStyles::right);

    std::unique_ptr<Window> window = std::make_unique<Window>(
        root, Size{1400.0, 760.0});
    const std::vector<std::byte> pixels = studio_patch_pixels();
    const ImageLoadResult patch = (*window).load_bgra32_premultiplied(
        12U, 12U, 48U, pixels);
    if (!patch) {
        throw std::runtime_error("material lab could not load the deterministic nine-patch");
    }
    (*studio).set_authored_surface_material(
        studio_caption_material(patch.image));
    (*root).retain((*contrast).clicked().subscribe(
        *root, ToggleContrast{window.get()}));
    (*root).retain((*scale).clicked().subscribe(
        *root, CycleTextScale{window.get(), scale}));
    (*window).perform_layout();
    (*root).retain(gui_forms::demo::schedule_visual_inspector_poll(
        *window, *root, inspector, overlay));
    return window;
}

} // namespace gui_forms::layered_material_lab
