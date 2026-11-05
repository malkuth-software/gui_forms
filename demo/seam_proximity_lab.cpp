#include "seam_proximity_lab.hpp"

#include "gui_forms/gui_forms.hpp"
#include "visual_inspector_poll.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms::seam_proximity_lab {
namespace {

constexpr Color ink = Color::rgba(35, 48, 59);
constexpr Color muted = Color::rgba(91, 105, 116);
constexpr Color paper = Color::rgba(246, 248, 249);
constexpr Color graphite = Color::rgba(75, 84, 89);
constexpr Color graphite_dark = Color::rgba(51, 59, 64);
constexpr Color cool_well = Color::rgba(232, 238, 242);

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

std::shared_ptr<Label> make_label(std::string id, std::string text,
                                  FontSpec font, Color color = ink) {
    std::shared_ptr<Label> result = make_control<Label>(StableId(std::move(id)), std::move(text));
    (*result).set_font(font);
    (*result).set_foreground(color);
    return result;
}

void add_panel_copy(const std::shared_ptr<SplitterPanel>& panel,
                    std::string id, std::string heading,
                    std::string detail) {
    add_at(panel,
        make_label(id + ".heading", std::move(heading),
                   {FontRole::control, 10.0, 700, false, 0.08}),
        {12.0, 12.0, 150.0, 22.0});
    add_at(panel,
        make_label(id + ".detail", std::move(detail),
                   {FontRole::content, 9.0, 400, false}, muted),
        {12.0, 39.0, 150.0, 40.0});
}

std::shared_ptr<SplitContainer> make_split(
    std::string id, Orientation orientation, SplitSeamGeometry geometry,
    double distance, SplitFixedPanel collapse_panel = SplitFixedPanel::none) {
    std::shared_ptr<SplitContainer> split =
        make_control<SplitContainer>(StableId(std::move(id)));
    (*split).set_orientation(orientation);
    (*split).set_splitter_geometry(geometry);
    (*split).set_splitter_distance(distance);
    (*split).set_first_minimum(54.0);
    (*split).set_second_minimum(54.0);
    (*split).set_keyboard_increment(4.0);
    (*split).set_collapse_panel(collapse_panel);
    (*split).accessible_name = "Adjustable physical pane seam";
    (*(*split).first_panel()).set_background(paper);
    (*(*split).second_panel()).set_background(cool_well);
    return split;
}

struct RefreshReadout final {
    std::weak_ptr<SplitContainer> split;
    std::weak_ptr<Label> label;

    void refresh() const {
        const std::shared_ptr<SplitContainer> owner = split.lock();
        const std::shared_ptr<Label> output = label.lock();
        if (!owner || !output) return;
        const SplitSeamSnapshot snapshot = (*owner).splitter_seam_snapshot();
        (*output).set_text(
            std::string(seam_state_name(snapshot.state)) + " · visible " +
            std::to_string(snapshot.visible_bounds.width) + "L / " +
            std::to_string(snapshot.visible_device_pixels) + "px · hit " +
            std::to_string(snapshot.orientation == Orientation::vertical
                               ? snapshot.hit_bounds.width
                               : snapshot.hit_bounds.height) + "L");
    }

    template <typename... Arguments>
    void operator()(Arguments&&...) const { refresh(); }
};

struct SelectTarget final {
    std::weak_ptr<VisualInspectorView> inspector;
    std::weak_ptr<VisualInspectorOverlay> overlay;
    std::string stable_id;

    void operator()(ButtonBase&) const {
        if (const std::shared_ptr<VisualInspectorView> target =
                inspector.lock()) {
            (*target).set_target_stable_id(stable_id);
        }
        if (const std::shared_ptr<VisualInspectorOverlay> target =
                overlay.lock()) {
            (*target).set_target_stable_id(stable_id);
        }
    }
};

struct ToggleCollapse final {
    std::weak_ptr<SplitContainer> split;
    RefreshReadout readout;
    void operator()(ButtonBase&) const {
        if (const std::shared_ptr<SplitContainer> target = split.lock()) {
            (*target).set_second_collapsed(!(*target).second_collapsed(),
                                           SplitCollapseOrigin::user);
            readout.refresh();
        }
    }
};

struct ToggleDisabled final {
    std::weak_ptr<SplitContainer> split;
    RefreshReadout readout;
    void operator()(ButtonBase&) const {
        if (const std::shared_ptr<SplitContainer> target = split.lock()) {
            (*target).enabled = !(*target).enabled.type();
            readout.refresh();
        }
    }
};

struct FocusSeam final {
    Window* window{};
    std::weak_ptr<SplitContainer> split;
    RefreshReadout readout;
    void operator()(ButtonBase&) const {
        if (const std::shared_ptr<SplitContainer> target = split.lock()) {
            static_cast<void>((*window).perform_semantic_action(
                (*target).stable_id().value(), SemanticAction::focus));
            readout.refresh();
        }
    }
};

struct CycleDeviceScale final {
    Window* window{};
    std::weak_ptr<Button> button;
    RefreshReadout readout;
    void operator()(ButtonBase&) const {
        const double next = (*window).scale() < 1.5 ? 2.0 : 1.0;
        (*window).set_scale(next);
        (*window).perform_layout();
        if (const std::shared_ptr<Button> target = button.lock()) {
            (*target).set_text(next == 2.0 ? "Device scale 2×"
                                           : "Device scale 1×");
        }
        readout.refresh();
    }
};

struct CycleTextScale final {
    Window* window{};
    std::weak_ptr<Button> button;
    void operator()(ButtonBase&) const {
        const double current = (*window).presentation_settings().text_scale;
        const double next = current < 1.1 ? 1.25 :
            (current < 1.4 ? 1.5 : 1.0);
        (*window).set_text_scale(next);
        if (const std::shared_ptr<Button> target = button.lock()) {
            (*target).set_text("Text scale " +
                std::to_string(static_cast<int>(next * 100.0)) + "%");
        }
    }
};

} // namespace

SplitSeamGeometry reference_seam_geometry() {
    return {3.0, SplitSeamThicknessPolicy::logical, 2.0, 7.0, 12.0};
}

SplitSeamGeometry physical_hairline_geometry() {
    return {1.0, SplitSeamThicknessPolicy::device_pixel_hairline,
            6.0, 12.0, 18.0};
}

bool invalid_geometry_is_rejected() {
    std::shared_ptr<SplitContainer> split =
        make_control<SplitContainer>(StableId("seam.invalid.probe"));
    const SplitSeamGeometry before = (*split).splitter_geometry();
    SplitSeamGeometry invalid = before;
    invalid.hit_after = SplitSeamGeometry::maximum_hit_extension + 1.0;
    try {
        (*split).set_splitter_geometry(invalid);
    } catch (const std::invalid_argument&) {
        return (*split).splitter_geometry() == before;
    }
    return false;
}

const char* seam_state_name(SplitSeamState state) noexcept {
    switch (state) {
    case SplitSeamState::idle: return "IDLE";
    case SplitSeamState::near: return "NEAR";
    case SplitSeamState::hot: return "HOT";
    case SplitSeamState::dragging: return "DRAGGING";
    case SplitSeamState::focused: return "FOCUSED";
    case SplitSeamState::disabled: return "DISABLED";
    case SplitSeamState::collapsed: return "COLLAPSED";
    }
    return "UNKNOWN";
}

std::unique_ptr<Window> make_seam_proximity_lab() {
    std::shared_ptr<LabRoot> root = make_control<LabRoot>(StableId("seam-proximity.root"));
    (*root).set_background(Color::rgba(219, 225, 228));

    std::shared_ptr<Panel> header = make_control<Panel>(StableId("seam-proximity.header"));
    (*header).set_background(graphite);
    add_at(root, header, {0.0, 0.0, 1360.0, 62.0},
           AnchorStyles::top | AnchorStyles::left | AnchorStyles::right);
    add_at(header,
        make_label("seam-proximity.title", "GUI.Forms Physical Seam + Proximity Lab",
                   {FontRole::control, 18.0, 700, false, 0.12},
                   Color::rgba(248, 250, 251)),
        {22.0, 4.0, 620.0, 31.0});
    add_at(header,
        make_label("seam-proximity.subtitle",
                   "one-device-pixel hairlines · independent hit extents · retained capture / keyboard / semantics",
                   {FontRole::content, 9.0, 400, false},
                   Color::rgba(218, 226, 230)),
        {24.0, 36.0, 760.0, 20.0});

    add_at(root,
        make_label("seam-proximity.reference.label",
                   "REFERENCE · 3 LOGICAL PIXELS / ASYMMETRIC HIT",
                   {FontRole::control, 9.0, 700, false, 0.1}),
        {20.0, 76.0, 390.0, 20.0});
    std::shared_ptr<SplitContainer> reference = make_split("seam-proximity.reference",
        Orientation::vertical, reference_seam_geometry(), 150.0,
        SplitFixedPanel::second);
    add_panel_copy((*reference).first_panel(), "seam-proximity.reference.left",
                   "GRAPHITE CHASSIS", "2L before hit extent");
    add_panel_copy((*reference).second_panel(), "seam-proximity.reference.right",
                   "INFORMATION WELL", "7L after hit extent");
    add_at(root, reference, {20.0, 100.0, 390.0, 204.0});

    add_at(root,
        make_label("seam-proximity.hairline.label",
                   "PHYSICAL HAIRLINE · 1 PX AT 1× AND 2×",
                   {FontRole::control, 9.0, 700, false, 0.1}),
        {430.0, 76.0, 390.0, 20.0});
    std::shared_ptr<SplitContainer> hairline = make_split("seam-proximity.hairline",
        Orientation::vertical, physical_hairline_geometry(), 158.0,
        SplitFixedPanel::second);
    add_panel_copy((*hairline).first_panel(), "seam-proximity.hairline.left",
                   "QUIET SEAM", "move near / over center");
    add_panel_copy((*hairline).second_panel(), "seam-proximity.hairline.right",
                   "LIVE TARGET", "drag or use arrow keys");
    add_at(root, hairline, {430.0, 100.0, 390.0, 204.0});

    std::shared_ptr<Label> reference_readout = make_label("seam-proximity.reference.state", "",
        {FontRole::monospace, 9.0, 700, false}, graphite_dark);
    std::shared_ptr<Label> hairline_readout = make_label("seam-proximity.hairline.state", "",
        {FontRole::monospace, 9.0, 700, false}, graphite_dark);
    add_at(root, reference_readout, {20.0, 309.0, 390.0, 22.0});
    add_at(root, hairline_readout, {430.0, 309.0, 390.0, 22.0});
    const RefreshReadout reference_refresh{reference, reference_readout};
    const RefreshReadout hairline_refresh{hairline, hairline_readout};
    reference_refresh.refresh();
    hairline_refresh.refresh();

    add_at(root,
        make_label("seam-proximity.horizontal.label",
                   "HORIZONTAL HAIRLINE · SAME CONTRACT TRANSPOSED",
                   {FontRole::control, 9.0, 700, false, 0.1}),
        {20.0, 340.0, 390.0, 20.0});
    std::shared_ptr<SplitContainer> horizontal = make_split("seam-proximity.horizontal",
        Orientation::horizontal, physical_hairline_geometry(), 72.0,
        SplitFixedPanel::first);
    add_at(root, horizontal, {20.0, 364.0, 390.0, 178.0});

    add_at(root,
        make_label("seam-proximity.disabled.label", "DISABLED",
                   {FontRole::control, 9.0, 700, false, 0.1}),
        {430.0, 340.0, 180.0, 20.0});
    std::shared_ptr<SplitContainer> disabled = make_split("seam-proximity.disabled",
        Orientation::vertical, reference_seam_geometry(), 82.0,
        SplitFixedPanel::second);
    (*disabled).enabled = false;
    add_at(root, disabled, {430.0, 364.0, 180.0, 178.0});

    add_at(root,
        make_label("seam-proximity.collapsed.label", "COLLAPSED / MINIMUMS",
                   {FontRole::control, 9.0, 700, false, 0.1}),
        {630.0, 340.0, 190.0, 20.0});
    std::shared_ptr<SplitContainer> collapsed = make_split("seam-proximity.collapsed",
        Orientation::vertical, reference_seam_geometry(), 86.0,
        SplitFixedPanel::second);
    (*collapsed).set_second_collapsed(true, SplitCollapseOrigin::user);
    add_at(root, collapsed, {630.0, 364.0, 190.0, 178.0});

    std::shared_ptr<VisualInspectorView> inspector = make_control<VisualInspectorView>(
        StableId("seam-proximity.inspector"),
        "seam-proximity.hairline.splitter");
    add_at(root, inspector, {850.0, 78.0, 488.0, 650.0},
           AnchorStyles::top | AnchorStyles::bottom | AnchorStyles::left |
               AnchorStyles::right);
    std::shared_ptr<VisualInspectorOverlay> overlay = make_control<VisualInspectorOverlay>(
        StableId("seam-proximity.overlay"),
        "seam-proximity.hairline.splitter");

    const bool rejected = invalid_geometry_is_rejected();
    add_at(root,
        make_label("seam-proximity.rejection",
                   rejected
                       ? "REJECTED ATOMICALLY · hit extension 129L exceeds limit 128L"
                       : "ERROR · invalid seam geometry was accepted",
                   {FontRole::monospace, 9.0, 700, false},
                   rejected ? Color::rgba(45, 108, 68)
                            : Color::rgba(150, 38, 48)),
        {20.0, 554.0, 560.0, 22.0});

    std::shared_ptr<Button> focus = make_control<Button>(StableId("seam-proximity.focus"),
                                      "Focus hairline");
    std::shared_ptr<Button> collapse = make_control<Button>(StableId("seam-proximity.collapse"),
                                         "Collapse / restore");
    std::shared_ptr<Button> disable = make_control<Button>(StableId("seam-proximity.disable"),
                                        "Disable / enable");
    std::shared_ptr<Button> device_scale = make_control<Button>(
        StableId("seam-proximity.device-scale"), "Device scale 1×");
    std::shared_ptr<Button> text_scale = make_control<Button>(
        StableId("seam-proximity.text-scale"), "Text scale 100%");
    const std::vector<std::shared_ptr<Button>> actions{
        focus, collapse, disable, device_scale, text_scale};
    for (std::size_t index = 0; index < actions.size(); ++index) {
        add_at(root, actions[index],
               {20.0 + static_cast<double>(index) * 160.0, 584.0,
                148.0, 32.0});
    }

    const std::vector<std::pair<std::string, std::string>> targets{
        {"3L reference", "seam-proximity.reference.splitter"},
        {"1px hairline", "seam-proximity.hairline.splitter"},
        {"Horizontal", "seam-proximity.horizontal.splitter"},
        {"Disabled", "seam-proximity.disabled.splitter"},
        {"Collapsed", "seam-proximity.collapsed.splitter"},
    };
    for (std::size_t index = 0; index < targets.size(); ++index) {
        std::shared_ptr<Button> selector = make_control<Button>(
            StableId("seam-proximity.target." + std::to_string(index)),
            targets[index].first);
        (*selector).set_visual_style(ButtonVisualStyle::command);
        add_at(root, selector,
               {20.0 + static_cast<double>(index) * 160.0, 626.0,
                148.0, 30.0});
        (*root).retain((*selector).clicked().subscribe(
            *root, SelectTarget{inspector, overlay, targets[index].second}));
    }

    add_at(root,
        make_label("seam-proximity.help",
                   "Pointer: broad strip → near; centered tab → hot; drag anywhere on strip. Keyboard: Tab, arrows, Shift+arrows, Enter/Space. Escape cancels drag.",
                   {FontRole::content, 9.0, 400, false}, muted),
        {20.0, 670.0, 800.0, 40.0});

    add_at(root, overlay, {0.0, 0.0, 1360.0, 760.0},
           AnchorStyles::top | AnchorStyles::bottom | AnchorStyles::left |
               AnchorStyles::right);

    std::unique_ptr<Window> window =
        std::make_unique<Window>(root, Size{1360.0, 760.0});
    (*root).retain((*focus).clicked().subscribe(
        *root, FocusSeam{window.get(), hairline, hairline_refresh}));
    (*root).retain((*collapse).clicked().subscribe(
        *root, ToggleCollapse{hairline, hairline_refresh}));
    (*root).retain((*disable).clicked().subscribe(
        *root, ToggleDisabled{hairline, hairline_refresh}));
    (*root).retain((*device_scale).clicked().subscribe(
        *root, CycleDeviceScale{window.get(), device_scale,
                                hairline_refresh}));
    (*root).retain((*text_scale).clicked().subscribe(
        *root, CycleTextScale{window.get(), text_scale}));

    using ObservedSeam =
        std::pair<std::shared_ptr<SplitContainer>, RefreshReadout>;
    for (const ObservedSeam& pair : std::vector<ObservedSeam>{
             {reference, reference_refresh}, {hairline, hairline_refresh}}) {
        (*root).retain((*(*pair.first).splitter_control()).pointer_observed().subscribe(
            *root, pair.second));
        (*root).retain((*(*pair.first).splitter_control()).focus_observed().subscribe(
            *root, pair.second));
        (*root).retain((*pair.first).splitter_changed().subscribe(
            *root, pair.second));
    }
    (*root).retain((*window).pointer_capture_changed().subscribe(
        *root, hairline_refresh));
    (*window).perform_layout();
    // Polling belongs solely to the explicitly opened inspector. No seam or
    // proximity transition schedules a frame or an idle animation loop.
    gui_forms::demo::start_visual_inspector_poll(*inspector, *overlay);
    return window;
}

} // namespace gui_forms::seam_proximity_lab
