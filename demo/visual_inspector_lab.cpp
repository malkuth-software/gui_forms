#include "visual_inspector_lab.hpp"

#include "gui_forms/gui_forms.hpp"
#include "visual_inspector_poll.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms::visual_inspector_lab {
namespace {

using namespace std::chrono_literals;

class LabRoot final : public Panel {
public:
    explicit LabRoot(StableId id) : Panel(std::move(id)) {}

    void retain(SubscriptionToken token) {
        subscriptions_.push_back(std::move(token));
    }

private:
    std::vector<SubscriptionToken> subscriptions_;
};

class ClippingPanel final : public Panel {
public:
    explicit ClippingPanel(StableId id) : Panel(std::move(id)) {}

protected:
    [[nodiscard]] Rect child_viewport_rectangle() const noexcept override {
        const Rect client = client_rectangle();
        return {14.0, 14.0, std::max(0.0, client.width - 28.0),
                std::max(0.0, client.height - 28.0)};
    }
};

template <typename Parent, typename Child>
void add_at(const std::shared_ptr<Parent>& parent,
            const std::shared_ptr<Child>& child, Rect bounds,
            AnchorStyles anchor = AnchorStyles::top | AnchorStyles::left) {
    (*child).set_requested_bounds(bounds);
    (*child).anchor = anchor;
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

SurfaceMaterial title_material() {
    SurfaceMaterial result;
    result.fills = {
        MaterialFillLayer::linear_css_angle(92.0, {
            {0.0, Color::rgba(22, 80, 151)},
            {0.52, Color::rgba(52, 132, 204)},
            {1.0, Color::rgba(214, 92, 150)},
        }),
        MaterialFillLayer::radial({0.16, 0.16}, {0.40, 1.0}, {
            {0.0, Color::rgba(255, 255, 255, 88)},
            {1.0, Color::rgba(255, 255, 255, 0)},
        }),
        MaterialFillLayer::radial({0.82, 0.2}, {0.36, 1.0}, {
            {0.0, Color::rgba(255, 205, 232, 68)},
            {1.0, Color::rgba(255, 205, 232, 0)},
        }),
    };
    result.shadows = {
        {{0.0, 3.0}, 8.0, 0.0, Color::rgba(15, 28, 46, 90), false},
        {{0.0, 1.0}, 2.0, 0.0, Color::rgba(255, 255, 255, 82), true},
    };
    result.border_edges.bottom = MaterialBorder{Color::rgba(18, 42, 77), 2.0};
    return result;
}

SurfaceMaterial specimen_material() {
    SurfaceMaterial result;
    result.fills = {
        MaterialFillLayer::linear_css_angle(180.0, {
            {0.0, Color::rgba(255, 255, 255)},
            {0.48, Color::rgba(238, 244, 248)},
            {1.0, Color::rgba(204, 215, 222)},
        }),
        MaterialFillLayer::radial({0.2, 0.0}, {0.7, 0.9}, {
            {0.0, Color::rgba(120, 194, 237, 70)},
            {1.0, Color::rgba(120, 194, 237, 0)},
        }),
        MaterialFillLayer::repeating_linear({0.0, 0.0}, {0.0, 4.0}, {
            {0.0, Color::rgba(255, 255, 255, 18)},
            {0.49, Color::rgba(255, 255, 255, 18)},
            {0.51, Color::rgba(30, 52, 66, 12)},
            {1.0, Color::rgba(30, 52, 66, 12)},
        }),
    };
    result.shadows = {
        {{0.0, 5.0}, 14.0, 1.0, Color::rgba(9, 25, 36, 90), false},
        {{0.0, 1.0}, 2.0, 0.0, Color::rgba(255, 255, 255, 180), true},
    };
    result.border = MaterialBorder{Color::rgba(92, 117, 132), 1.0};
    result.corner_radius = 10.0;
    return result;
}

struct SelectTarget final {
    std::weak_ptr<VisualInspectorView> view;
    std::weak_ptr<VisualInspectorOverlay> overlay;
    std::string target;

    void operator()(ButtonBase&) const {
        const std::shared_ptr<VisualInspectorView> strong_view = view.lock();
        const std::shared_ptr<VisualInspectorOverlay> strong_overlay =
            overlay.lock();
        if (strong_view) (*strong_view).set_target_stable_id(target);
        if (strong_overlay) (*strong_overlay).set_target_stable_id(target);
    }
};

} // namespace

std::unique_ptr<Window> make_visual_inspector_lab() {
    std::shared_ptr<LabRoot> root = make_control<LabRoot>(
        StableId("visual-inspector.root"));
    (*root).set_background(Color::rgba(215, 223, 228));

    std::shared_ptr<Control> title = make_control<Control>(
        StableId("visual-inspector.title-plane"));
    (*title).set_authored_surface_material(title_material());
    add_at(root, title, {0.0, 0.0, 1160.0, 58.0},
           AnchorStyles::top | AnchorStyles::left | AnchorStyles::right);
    add_at(title,
        label("visual-inspector.title", "GUI.Forms Visual-State Inspection",
              {FontRole::control, 20.0, 700, false, 0.12},
              Color::rgba(255, 255, 255)),
        {24.0, 6.0, 650.0, 30.0});
    add_at(title,
        label("visual-inspector.subtitle",
              "Committed geometry · clips · materials · fonts · state",
              {FontRole::content, 10.0, 400, false},
              Color::rgba(225, 241, 255)),
        {26.0, 34.0, 620.0, 18.0});

    std::shared_ptr<Control> card = make_control<Control>(
        StableId("visual-inspector.specimen-card"));
    (*card).set_authored_surface_material(specimen_material());
    (*card).padding = {22.0, 18.0, 22.0, 18.0};
    add_at(root, card, {28.0, 84.0, 516.0, 270.0},
           AnchorStyles::top | AnchorStyles::left);

    std::shared_ptr<Label> card_title = label(
        "visual-inspector.specimen-title", "One retained material, fully inspectable",
        {FontRole::control, 17.0, 700, false, 0.18},
        Color::rgba(25, 52, 70));
    add_at(card, card_title, {24.0, 20.0, 450.0, 34.0});
    add_at(card,
        label("visual-inspector.specimen-body",
              "The panel owns three ordered fills, two shadows, a border and a corner radius. The text and button retain their own resolved draw fonts and state.",
              {FontRole::content, 12.0, 400, false},
              Color::rgba(50, 70, 82)),
        {24.0, 62.0, 450.0, 54.0});

    std::shared_ptr<Button> primary = make_control<Button>(
        StableId("visual-inspector.specimen-button"), "Focus, hover, and press me");
    (*primary).set_visual_style(ButtonVisualStyle::accent);
    (*primary).font = {FontRole::control, 12.0, 700, false, 0.12};
    add_at(card, primary, {24.0, 138.0, 230.0, 42.0});

    std::shared_ptr<ClippingPanel> clipping = make_control<ClippingPanel>(
        StableId("visual-inspector.clip-host"));
    (*clipping).set_background(Color::rgba(55, 68, 77));
    (*clipping).set_border_style(BorderStyle::sunken);
    add_at(card, clipping, {280.0, 126.0, 202.0, 112.0});
    std::shared_ptr<Panel> clipped = make_control<Panel>(
        StableId("visual-inspector.clipped-child"));
    (*clipped).set_background(Color::rgba(238, 102, 184));
    (*clipped).set_border_style(BorderStyle::raised);
    add_at(clipping, clipped, {-18.0, 24.0, 178.0, 62.0});
    add_at(clipped,
        label("visual-inspector.clipped-label", "ancestor clipped",
              {FontRole::control, 10.0, 700, false},
              Color::rgba(255, 255, 255)),
        {28.0, 18.0, 130.0, 24.0});

    std::shared_ptr<VisualInspectorView> inspector =
        make_control<VisualInspectorView>(
            StableId("visual-inspector.readout"),
            "visual-inspector.specimen-card");
    add_at(root, inspector, {572.0, 76.0, 560.0, 584.0},
           AnchorStyles::top | AnchorStyles::bottom | AnchorStyles::left |
               AnchorStyles::right);

    add_at(root,
        label("visual-inspector.target-caption", "INSPECT TARGET",
              {FontRole::control, 10.0, 700, false},
              Color::rgba(54, 72, 84)),
        {28.0, 380.0, 180.0, 24.0});

    const std::vector<std::pair<std::string, std::string>> target_specs{
        {"Material card", "visual-inspector.specimen-card"},
        {"Resolved text", "visual-inspector.specimen-title"},
        {"Live state", "visual-inspector.specimen-button"},
        {"Ancestor clip", "visual-inspector.clipped-child"},
    };

    std::shared_ptr<VisualInspectorOverlay> overlay =
        make_control<VisualInspectorOverlay>(
            StableId("visual-inspector.overlay"),
            "visual-inspector.specimen-card");
    for (std::size_t index = 0U; index < target_specs.size(); ++index) {
        std::shared_ptr<Button> selector = make_control<Button>(
            StableId("visual-inspector.target." + std::to_string(index)),
            target_specs[index].first);
        (*selector).set_visual_style(ButtonVisualStyle::command);
        add_at(root, selector,
               {28.0 + static_cast<double>(index % 2U) * 250.0,
                412.0 + static_cast<double>(index / 2U) * 48.0,
                226.0, 34.0});
        (*root).retain((*selector).clicked().subscribe(
            *root, SelectTarget{inspector, overlay, target_specs[index].second}));
    }

    add_at(root,
        label("visual-inspector.legend",
              "The overlay is pointer-transparent. Blue is the arranged box, magenta includes visual outsets, and gold is the effective ancestor clip.",
              {FontRole::content, 10.0, 400, false},
              Color::rgba(57, 72, 81)),
        {28.0, 522.0, 516.0, 52.0});
    add_at(root,
        label("visual-inspector.truth",
              "A stale display chunk is reported as stale; inspection never paints to manufacture evidence.",
              {FontRole::content, 10.0, 700, false},
              Color::rgba(111, 63, 34)),
        {28.0, 588.0, 516.0, 40.0});

    add_at(root, overlay, {0.0, 0.0, 1160.0, 700.0},
           AnchorStyles::top | AnchorStyles::bottom | AnchorStyles::left |
               AnchorStyles::right);

    std::unique_ptr<Window> window = std::make_unique<Window>(root,
                                                               Size{1160.0, 700.0});
    (*window).perform_layout();
    gui_forms::demo::start_visual_inspector_poll(*inspector, *overlay);
    return window;
}

} // namespace gui_forms::visual_inspector_lab
