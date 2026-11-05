#include "custom_chrome_lab.hpp"

#include "gui_forms/gui_forms.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms::custom_chrome_lab {
namespace {

class LabRoot final : public Panel {
public:
    explicit LabRoot(StableId id) : Panel(std::move(id)) {}

    void retain(SubscriptionToken token) {
        subscriptions_.push_back(std::move(token));
    }

private:
    std::vector<SubscriptionToken> subscriptions_;
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

SurfaceMaterial title_material(bool active) {
    SurfaceMaterial result;
    if (active) {
        result.fills = {
            MaterialFillLayer::linear_css_angle(92.0, {
                {0.0, Color::rgba(23, 52, 127)},
                {0.58, Color::rgba(58, 104, 203)},
                {1.0, Color::rgba(217, 104, 114)},
            }),
            MaterialFillLayer::radial({0.18, -0.90}, {0.42, 1.35}, {
                {0.0, Color::rgba(146, 217, 255, 136)},
                {1.0, Color::rgba(146, 217, 255, 0)},
            }),
            MaterialFillLayer::radial({0.62, 1.60}, {0.34, 1.0}, {
                {0.0, Color::rgba(214, 178, 255, 136)},
                {1.0, Color::rgba(214, 178, 255, 0)},
            }),
            MaterialFillLayer::radial({0.95, 1.0}, {0.28, 0.8}, {
                {0.0, Color::rgba(255, 195, 142, 136)},
                {1.0, Color::rgba(255, 195, 142, 0)},
            }),
        };
        result.border_edges.bottom =
            MaterialBorder{Color::rgba(23, 45, 105, 235), 1.0};
        result.keylines = {
            {MaterialEdge::top, Color::rgba(255, 255, 255, 105), 1.0, 0.0},
            {MaterialEdge::bottom, Color::rgba(185, 236, 255, 210), 1.0,
             1.0},
        };
    } else {
        result.fills = {
            MaterialFillLayer::linear_css_angle(92.0, {
                {0.0, Color::rgba(66, 78, 111)},
                {0.58, Color::rgba(92, 105, 142)},
                {1.0, Color::rgba(143, 105, 116)},
            }),
            MaterialFillLayer::radial({0.18, -0.90}, {0.42, 1.35}, {
                {0.0, Color::rgba(212, 230, 240, 50)},
                {1.0, Color::rgba(212, 230, 240, 0)},
            }),
        };
        result.border_edges.bottom =
            MaterialBorder{Color::rgba(55, 65, 91), 1.0};
        result.keylines = {
            {MaterialEdge::top, Color::rgba(255, 255, 255, 44), 1.0, 0.0},
            {MaterialEdge::bottom, Color::rgba(210, 223, 244, 55), 1.0,
             1.0},
        };
    }
    result.shadows = {
        {{0.0, -1.0}, 0.0, 0.0, Color::rgba(23, 45, 105), true},
        {{0.0, 1.0}, 0.0, 0.0, Color::rgba(255, 255, 255, 82), true},
    };
    return result;
}

SurfaceMaterial glass_mark_material() {
    SurfaceMaterial result;
    result.fills = {
        MaterialFillLayer::linear_css_angle(180.0, {
            {0.0, Color::rgba(255, 255, 255, 210)},
            {0.45, Color::rgba(202, 235, 255, 155)},
            {1.0, Color::rgba(77, 112, 188, 190)},
        }),
        MaterialFillLayer::radial({0.28, 0.15}, {0.65, 0.75}, {
            {0.0, Color::rgba(255, 255, 255, 210)},
            {1.0, Color::rgba(255, 255, 255, 0)},
        }),
    };
    result.border = MaterialBorder{Color::rgba(255, 236, 164, 220), 1.0};
    result.corner_radius = 4.0;
    result.shadows = {
        {{0.0, 2.0}, 4.0, 0.0, Color::rgba(14, 28, 70, 120), false},
        {{0.0, 1.0}, 1.0, 0.0, Color::rgba(255, 255, 255, 170), true},
    };
    return result;
}

SurfaceMaterial paper_material() {
    SurfaceMaterial result;
    result.fills = {
        MaterialFillLayer::linear_css_angle(180.0, {
            {0.0, Color::rgba(250, 253, 255)},
            {1.0, Color::rgba(231, 237, 246)},
        }),
    };
    result.border = MaterialBorder{Color::rgba(119, 140, 171), 1.0};
    result.shadows = {
        {{0.0, 6.0}, 18.0, 0.0, Color::rgba(28, 46, 72, 58), false},
        {{0.0, 1.0}, 1.0, 0.0, Color::rgba(255, 255, 255, 210), true},
    };
    result.corner_radius = 5.0;
    return result;
}

struct ApplyActivationMaterial final {
    std::weak_ptr<Control> title;

    void operator()(bool active) const {
        const std::shared_ptr<Control> surface = title.lock();
        if (surface) (*surface).set_authored_surface_material(
            title_material(active));
    }
};

struct CountInteractiveClicks final {
    std::weak_ptr<Label> readout;
    std::shared_ptr<unsigned int> count;

    void operator()(ButtonBase&) const {
        ++*count;
        const std::shared_ptr<Label> target = readout.lock();
        if (target) {
            (*target).set_text("Interactive exclusion received " +
                               std::to_string(*count) + " click" +
                               (*count == 1U ? "" : "s"));
        }
    }
};

} // namespace

std::unique_ptr<Window> make_custom_chrome_lab() {
    std::shared_ptr<LabRoot> root = make_control<LabRoot>(
        StableId("custom-chrome.root"));
    (*root).set_background(Color::rgba(75, 84, 89));

    std::shared_ptr<Control> title = make_control<Control>(
        StableId("custom-chrome.title-drag"));
    (*title).set_authored_surface_material(title_material(true));
    add_at(root, title, {0.0, 0.0, 1120.0, 40.0},
           AnchorStyles::top | AnchorStyles::left | AnchorStyles::right);

    std::shared_ptr<Control> mark = make_control<Control>(
        StableId("custom-chrome.mark"));
    (*mark).set_authored_surface_material(glass_mark_material());
    (*mark).hit_test_transparent = true;
    add_at(title, mark, {88.0, 8.0, 24.0, 24.0});

    std::shared_ptr<Label> app_title = label(
        "custom-chrome.title", "File Manager",
        {FontRole::control, 13.0, 700, false, 0.15},
        Color::rgba(255, 255, 255));
    (*app_title).hit_test_transparent = true;
    add_at(title, app_title, {122.0, 7.0, 160.0, 24.0});
    std::shared_ptr<Label> location = label(
        "custom-chrome.location", "Projects — quentin",
        {FontRole::content, 10.0, 400, false},
        Color::rgba(230, 241, 255));
    (*location).hit_test_transparent = true;
    add_at(title, location, {236.0, 10.0, 240.0, 20.0});

    std::shared_ptr<Button> interactive = make_control<Button>(
        StableId("custom-chrome.interactive-exclusion"), "Interactive zone");
    (*interactive).set_visual_style(ButtonVisualStyle::command);
    (*interactive).font = {FontRole::control, 10.0, 700, false};
    add_at(title, interactive, {930.0, 6.0, 174.0, 28.0},
           AnchorStyles::top | AnchorStyles::right);

    std::shared_ptr<Control> paper = make_control<Control>(
        StableId("custom-chrome.paper"));
    (*paper).set_authored_surface_material(paper_material());
    add_at(root, paper, {28.0, 70.0, 550.0, 486.0},
           AnchorStyles::top | AnchorStyles::bottom | AnchorStyles::left);
    add_at(paper,
        label("custom-chrome.heading", "NATIVE WINDOW, AUTHORED IDENTITY",
              {FontRole::control, 15.0, 700, false, 0.12},
              Color::rgba(29, 45, 75)),
        {26.0, 24.0, 490.0, 28.0});
    add_at(paper,
        label("custom-chrome.explanation",
              "The Watercolor title plane starts at client y=0 and continues behind genuine host caption controls. Exact retained hit targets decide whether a primary press begins native window movement.",
              {FontRole::content, 12.0, 400, false},
              Color::rgba(61, 76, 101)),
        {26.0, 68.0, 490.0, 70.0});
    add_at(paper,
        label("custom-chrome.drag-rule", "DRAG BACKDROP",
              {FontRole::control, 9.0, 700, false, 0.16},
              Color::rgba(62, 92, 140)),
        {26.0, 166.0, 160.0, 20.0});
    add_at(paper,
        label("custom-chrome.drag-detail",
              "Click and drag the open sapphire title field. Decorative title children pass through to that backdrop.",
              {FontRole::content, 11.0, 400, false},
              Color::rgba(43, 57, 78)),
        {26.0, 190.0, 490.0, 48.0});
    add_at(paper,
        label("custom-chrome.exclude-rule", "INTERACTIVE EXCLUSION",
              {FontRole::control, 9.0, 700, false, 0.16},
              Color::rgba(150, 71, 84)),
        {26.0, 262.0, 190.0, 20.0});
    add_at(paper,
        label("custom-chrome.exclude-detail",
              "The title button wins hit testing, so it remains an ordinary GUI.Forms command rather than a magic platform rectangle.",
              {FontRole::content, 11.0, 400, false},
              Color::rgba(43, 57, 78)),
        {26.0, 286.0, 490.0, 48.0});
    std::shared_ptr<Label> click_readout = label(
        "custom-chrome.click-readout", "Interactive exclusion received 0 clicks",
        {FontRole::control, 10.0, 700, false},
        Color::rgba(61, 82, 119));
    add_at(paper, click_readout, {26.0, 366.0, 490.0, 28.0});
    add_at(paper,
        label("custom-chrome.host-truth",
              "Deactivate, resize, zoom/minimize, and close with the native host controls. The title material recedes; content remains enabled.",
              {FontRole::content, 10.0, 400, false},
              Color::rgba(98, 76, 70)),
        {26.0, 420.0, 490.0, 44.0});

    std::shared_ptr<VisualInspectorView> inspector =
        make_control<VisualInspectorView>(
            StableId("custom-chrome.inspector"),
            "custom-chrome.title-drag");
    add_at(root, inspector, {606.0, 70.0, 486.0, 486.0},
           AnchorStyles::top | AnchorStyles::bottom | AnchorStyles::left |
               AnchorStyles::right);

    const std::shared_ptr<unsigned int> click_count =
        std::make_shared<unsigned int>();
    (*root).retain((*interactive).clicked().subscribe(
        *root, CountInteractiveClicks{click_readout, click_count}));

    std::unique_ptr<Window> window = std::make_unique<Window>(
        root, Size{1120.0, 580.0});
    (*root).retain((*window).active_changed().subscribe(
        *root, ApplyActivationMaterial{title}));
    (*window).perform_layout();
    return window;
}

} // namespace gui_forms::custom_chrome_lab
