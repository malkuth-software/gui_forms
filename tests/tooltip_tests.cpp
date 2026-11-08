#include "gui_forms/gui_forms.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

namespace {

using namespace gui_forms;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const SemanticNode* find_role(const std::vector<SemanticNode>& nodes,
                              SemanticRole role) {
    for (const gui_forms::SemanticNode& node : nodes) {
        if (node.role == role) return &node;
        if (const SemanticNode* found = find_role(node.children, role)) return found;
    }
    return nullptr;
}
const SemanticNode* find_role(std::vector<SemanticNode>&& nodes, SemanticRole role) = delete;

const SemanticNode* find_id(const std::vector<SemanticNode>& nodes, std::string_view id) {
    for (const gui_forms::SemanticNode& node : nodes) {
        if (node.stable_id == id) return &node;
        if (const SemanticNode* found = find_id(node.children, id)) return found;
    }
    return nullptr;
}
const SemanticNode* find_id(std::vector<SemanticNode>&& nodes, std::string_view id) = delete;

std::size_t count_role(const std::vector<SemanticNode>& nodes,
                       SemanticRole role) {
    std::size_t count = 0U;
    for (const gui_forms::SemanticNode& node : nodes) {
        count += node.role == role ? 1U : 0U;
        count += count_role(node.children, role);
    }
    return count;
}

// Runs every wake due within the next second (tooltip delays and caret
// blinks alike).
void run_due(Window& window) {
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + std::chrono::seconds(1)));
}

// Paints nothing: the window records each control's display operations.
class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void clip_rounded_rect(Rect, double) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override {}
    void fill_radial_gradient(Rect, Point, Size, std::span<const GradientStop>) override {}
    void draw_box_shadow(Rect, double, Point, double, double, Color) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

// Whether the control `stable_id` drew `text` when the window last painted
// everything.
bool painted_text(Window& window, std::string_view stable_id, std::string_view text) {
    window.perform_layout();
    NullPainter painter;
    const Size client = window.client_size();
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, client.width, client.height}));
    VisualInspectionOptions options{};
    options.include_text = true;
    const VisualInspectionSnapshot snapshot = window.visual_inspection_snapshot(options);
    for (const VisualControlInspection& control : snapshot.controls) {
        if (control.stable_id != stable_id) continue;
        for (const VisualPaintOperationSnapshot& operation : control.display_operations) {
            if (operation.operation == VisualPaintOperation::draw_text && operation.text == text) return true;
        }
    }
    return false;
}

struct Fixture final {
    Fixture() : window(root, {420.0, 240.0}) {
        (*root).set_requested_bounds({0.0, 0.0, 420.0, 240.0});
        (*button).set_requested_bounds({40.0, 45.0, 130.0, 34.0});
        (*other).set_requested_bounds({220.0, 45.0, 130.0, 34.0});
        (*root).add_child(button);
        (*root).add_child(other);
        window.perform_layout();
        static_cast<void>(window.take_damage());
    }

    Control::Ptr root = make_control<Control>(StableId("tooltip.root"));
    std::shared_ptr<Button> button =
        make_control<Button>(StableId("tooltip.target"), "Hover target");
    std::shared_ptr<Button> other =
        make_control<Button>(StableId("tooltip.other"), "Other target");
    Window window;
};

void hover(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer(
        {PointerAction::move, PointerButton::none, point}));
}

void press_key(Window& window, std::uint32_t key) {
    KeyEvent event{};
    event.action = KeyAction::down;
    event.physical_key = key;
    static_cast<void>(window.dispatch_key(event));
}

void fire_next(Window& window) {
    const std::optional<FrameTime> wake = window.next_wake();
    require(wake.has_value(), "test expected a scheduled UI deadline");
    static_cast<void>(window.poll_frame_schedule(*wake));
}

std::size_t tool_tip_nodes(Window& window) {
    window.perform_layout();
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const std::size_t count = count_role(snapshot.roots, SemanticRole::tool_tip);
    return count;
}

class CountTooltipVisibility final {
public:
    CountTooltipVisibility(int& shown, int& hidden)
        : shown_(shown), hidden_(hidden) {}

    void operator()(const ToolTipEvent& event) const {
        event.shown ? ++shown_ : ++hidden_;
    }

private:
    int& shown_;
    int& hidden_;
};

class RecordTooltip final {
public:
    explicit RecordTooltip(ToolTipEvent& last) : last_(last) {}

    void operator()(const ToolTipEvent& event) const { last_ = event; }

private:
    ToolTipEvent& last_;
};

void test_hover_delay_cancel_and_accessible_overlay() {
    Fixture fixture;
    ToolTip tips(fixture.window);
    tips.initial_delay = 30ms;
    tips.auto_pop_delay = 200ms;
    tips.set_tool_tip(fixture.button, "Opens the calibrated input selector");
    require((*fixture.button).tool_tip_text.read() == "Opens the calibrated input selector",
            "ToolTip writes the target's own tooltip text");
    int shown = 0;
    int hidden = 0;
    SubscriptionToken changes = tips.visibility_changed().subscribe(
        CountTooltipVisibility(shown, hidden));

    hover(fixture.window, {60.0, 60.0});
    require(!tips.visible() && fixture.window.next_wake().has_value(),
            "hover must arm the initial delay without opening synchronously");
    hover(fixture.window, {390.0, 210.0});
    require(!tips.visible() && !fixture.window.next_wake().has_value(),
            "leaving before the delay must cancel the pending overlay and wake");

    hover(fixture.window, {60.0, 60.0});
    fire_next(fixture.window);
    fixture.window.perform_layout();
    const SemanticSnapshot snapshot = fixture.window.semantic_snapshot();
    const SemanticNode* tooltip = find_role(snapshot.roots, SemanticRole::tool_tip);
    require(tips.visible() && fixture.window.tool_tip_visible() && shown == 1 && hidden == 0 && tooltip &&
                (*tooltip).name == "Opens the calibrated input selector" &&
                fixture.window.hit_test({65.0, 60.0}) == fixture.button,
            "a due hover must open a named semantic, input-transparent overlay");
    const SemanticNode* target_node = find_id(snapshot.roots, "tooltip.target");
    require(target_node && (*target_node).tool_tip == "Opens the calibrated input selector",
            "the control's semantic node carries its tooltip");

    hover(fixture.window, {390.0, 210.0});
    const SemanticSnapshot after_leave = fixture.window.semantic_snapshot();
    require(!tips.visible() && shown == 1 && hidden == 1 &&
                !find_role(after_leave.roots,
                           SemanticRole::tool_tip),
            "leave must remove both the retained overlay and its semantic node");
}

// The pointer must rest for the whole delay: a motion over the target before
// the deadline moves the deadline, and nothing shows until it comes.
void test_motion_restarts_the_rest() {
    Fixture fixture;
    ToolTipSettings settings = fixture.window.tool_tip_settings();
    settings.initial_delay = 40ms;
    fixture.window.set_tool_tip_settings(settings);
    (*fixture.button).tool_tip_text = "Resting help";
    hover(fixture.window, {60.0, 60.0});
    const std::optional<FrameTime> first = fixture.window.next_wake();
    require(first.has_value(), "hover arms a wake");
    hover(fixture.window, {62.0, 61.0});
    // Woken at the first deadline, the presenter sees the later motion.
    static_cast<void>(fixture.window.poll_frame_schedule(*first));
    if (fixture.window.tool_tip_visible()) {
        // The second motion came within the clock's resolution of the first:
        // the deadline did not move. Nothing else to check.
        return;
    }
    const std::optional<FrameTime> second = fixture.window.next_wake();
    require(second.has_value() && *second > *first, "a later motion moves the deadline");
    static_cast<void>(fixture.window.poll_frame_schedule(*second));
    require(fixture.window.tool_tip_visible() && fixture.window.tool_tip_text() == "Resting help",
            "the tooltip shows once the pointer has rested");
}

void test_keyboard_focus_shows_below_and_follows_the_target() {
    Fixture fixture;
    ToolTip tips(fixture.window);
    tips.initial_delay = 1ms;
    tips.auto_pop_delay = 40ms;
    tips.set_tool_tip(fixture.button, "Keyboard help");
    // Focus by the program, with no keyboard cue, shows no bubble.
    require(fixture.window.request_focus(fixture.button), "fixture button must accept focus");
    require(!fixture.window.next_wake().has_value() && !tips.visible(),
            "focus without the keyboard cue shows nothing");
    require(fixture.window.request_focus(fixture.other), "fixture other button must accept focus");
    ToolTipEvent last{};
    SubscriptionToken record = fixture.window.tool_tip_visibility_changed().subscribe(RecordTooltip(last));
    press_key(fixture.window, PhysicalKey::tab);
    require(fixture.window.focused_control() == fixture.button, "Tab moves focus to the button");
    fire_next(fixture.window);
    fixture.window.perform_layout();
    const SemanticSnapshot before_snapshot = fixture.window.semantic_snapshot();
    const SemanticNode* before =
        find_role(before_snapshot.roots, SemanticRole::tool_tip);
    require(before && tips.visible() && last.shown && last.keyboard_initiated &&
                last.target_stable_id == "tooltip.target" && (*before).bounds.y >= 45.0 + 34.0,
            "keyboard focus shows the tooltip below its control");
    const Rect old_bounds = (*before).bounds;

    (*fixture.button).set_requested_bounds({250.0, 145.0, 130.0, 34.0});
    fixture.window.perform_layout();
    const SemanticSnapshot after_snapshot = fixture.window.semantic_snapshot();
    const SemanticNode* after =
        find_role(after_snapshot.roots, SemanticRole::tool_tip);
    require(after && (*after).bounds != old_bounds && (*after).bounds.x >= 250.0,
            "a visible tooltip must remain anchored when its target moves");

    fire_next(fixture.window);
    require(!tips.visible(), "auto-pop deadline must close a focused tooltip");
}

void test_repeated_focus_show_hide_reuses_no_stale_overlay_identity() {
    Fixture fixture;
    ToolTip tips(fixture.window);
    tips.initial_delay = 1ms;
    tips.reshow_delay = 1ms;
    tips.auto_pop_delay = 5s;
    tips.set_tool_tip(fixture.button, "Repeated keyboard help");
    // A key shows the keyboard cue, so the program's focus moves below count
    // as keyboard focus.
    press_key(fixture.window, PhysicalKey::escape);
    for (std::size_t cycle = 0U; cycle < 32U; ++cycle) {
        require(fixture.window.request_focus(fixture.button),
                "repeated tooltip target must accept focus");
        fire_next(fixture.window);
        const std::size_t shown = tool_tip_nodes(fixture.window);
        if (!tips.visible() || shown != 1U) {
            throw std::runtime_error(
                "tooltip focus cycle " + std::to_string(cycle) +
                " opened visible=" + (tips.visible() ? "true" : "false") +
                " count=" + std::to_string(shown) + " frame_faults=" +
                std::to_string(
                    fixture.window.metrics_snapshot().frame_callback_faults));
        }
        require(fixture.window.request_focus(fixture.other),
                "focus must leave the repeated tooltip target");
        require(!tips.visible() && tool_tip_nodes(fixture.window) == 0U,
                "focus departure must detach the tooltip subtree and identity");
    }
}

// One presenter: two ToolTips write the same thing's text, the last one
// holds, and one bubble shows.
void test_one_presenter_for_every_provider_and_owner_disposal() {
    Fixture fixture;
    ToolTip first(fixture.window);
    ToolTip second(fixture.window);
    first.set_tool_tip(fixture.button, "First provider");
    second.set_tool_tip(fixture.button, "Second provider");
    require((*fixture.button).tool_tip_text.read() == "Second provider", "the last text set holds");
    first.show(fixture.button, 0ms);
    second.show(fixture.button, 0ms);
    require(first.visible() && second.visible() && tool_tip_nodes(fixture.window) == 1U,
            "the window shows one tooltip whoever asked");
    const SemanticSnapshot snapshot = fixture.window.semantic_snapshot();
    const SemanticNode* tip = find_role(snapshot.roots, SemanticRole::tool_tip);
    require(tip && (*tip).name == "Second provider", "the bubble shows the thing's text");

    (*fixture.button).dispose();
    require(!first.visible() && !second.visible() && !first.active_control() &&
                !fixture.window.tool_tip_visible(),
            "disposing a target must synchronously revoke the overlay");
    require(tool_tip_nodes(fixture.window) == 0U,
            "owner disposal must leave no stale tooltip semantic nodes");
}

void test_mapping_removal_and_provider_disposal_are_quiescent() {
    Fixture fixture;
    std::unique_ptr<gui_forms::ToolTip> tips = std::make_unique<ToolTip>(fixture.window);
    (*tips).initial_delay = 20ms;
    (*tips).set_tool_tip(fixture.button, "Disposable mapping");
    hover(fixture.window, {60.0, 60.0});
    require((*tips).remove_tool_tip(*fixture.button) && (*fixture.button).tool_tip_text.read().empty(),
            "removing a mapping clears the target's text");
    hover(fixture.window, {61.0, 60.0});
    require(!fixture.window.next_wake(), "a target without text arms nothing");
    require(!(*tips).remove_tool_tip(*fixture.other), "a target the ToolTip never set is refused");
    (*tips).set_tool_tip(fixture.button, "Disposable provider");
    (*tips).show(fixture.button, 0ms);
    require((*tips).visible(), "explicit persistent tooltip must open");
    fixture.window.perform_layout();
    const SemanticSnapshot explicit_snapshot = fixture.window.semantic_snapshot();
    const SemanticNode* explicit_tip =
        find_role(explicit_snapshot.roots, SemanticRole::tool_tip);
    require(explicit_tip && (*explicit_tip).bounds.x >= (*fixture.button).absolute_bounds().x,
            "programmatic ToolTip show must anchor to its target, not the client origin");
    (*tips).dispose();
    require(!fixture.window.next_wake() && tool_tip_nodes(fixture.window) == 0U &&
                (*fixture.button).tool_tip_text.read().empty(),
            "provider disposal clears its text and removes overlay and scheduled work");
}

void test_show_always_supports_a_disabled_visible_owner() {
    Fixture fixture;
    ToolTip tips(fixture.window);
    tips.set_tool_tip(fixture.button, "Why this command is unavailable");
    (*fixture.button).enabled = false;
    tips.show(fixture.button, 0ms);
    require(!tips.visible(), "a disabled control shows no tooltip by default");
    tips.show_always = true;
    require(fixture.window.tool_tip_settings().show_when_disabled, "ShowAlways is the window's setting");
    tips.show(fixture.button, 0ms);
    fixture.window.perform_layout();
    const SemanticSnapshot snapshot = fixture.window.semantic_snapshot();
    const SemanticNode* tip = find_role(snapshot.roots, SemanticRole::tool_tip);
    require(tips.visible() && tip && (*tip).name == "Why this command is unavailable",
            "ShowAlways must allow passive help owned by a disabled visible control");
}

void test_maximum_width_is_bounded_and_shapes_overlay() {
    Fixture fixture;
    ToolTip tips(fixture.window);
    tips.maximum_width = 120.0;
    require(tips.maximum_width.read() == 120.0 && fixture.window.tool_tip_settings().maximum_width == 120.0,
            "ToolTip must retain an explicit bounded maximum width on its window");
    bool rejected = false;
    try {
        tips.maximum_width = 40.0;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && tips.maximum_width.read() == 120.0, "ToolTip must reject unusably narrow maximum widths");
    bool negative = false;
    try {
        tips.initial_delay = -1ms;
    } catch (const std::invalid_argument&) {
        negative = true;
    }
    require(negative, "a negative delay is refused");
    tips.set_tool_tip(
        fixture.button,
        "A deliberately long tooltip sentence that must wrap into multiple lines");
    tips.show(fixture.button, 0ms);
    fixture.window.perform_layout();
    const SemanticSnapshot snapshot = fixture.window.semantic_snapshot();
    const SemanticNode* tip = find_role(snapshot.roots, SemanticRole::tool_tip);
    require(tip && (*tip).bounds.width <= 140.0 && (*tip).bounds.height > 30.0,
            "ToolTip bubble must honor maximum text width plus chrome, wrapping onto more lines");
}

void test_window_and_program_switches() {
    Fixture fixture;
    ToolTipSettings settings = fixture.window.tool_tip_settings();
    settings.initial_delay = 1ms;
    fixture.window.set_tool_tip_settings(settings);
    (*fixture.button).tool_tip_text = "Switched help";
    require(!fixture.window.tool_tips_shown_setting() && fixture.window.tool_tips_shown(),
            "a window follows the program, which shows tooltips by default");
    hover(fixture.window, {60.0, 60.0});
    fire_next(fixture.window);
    require(fixture.window.tool_tip_visible(), "the tooltip shows");
    fixture.window.set_tool_tips_shown(false);
    require(!fixture.window.tool_tip_visible(), "turning the window's tooltips off closes the one that shows");
    hover(fixture.window, {390.0, 210.0});
    hover(fixture.window, {60.0, 60.0});
    require(!fixture.window.next_wake(), "a window with tooltips off waits for nothing");
    fixture.window.set_tool_tips_shown(std::nullopt);
    Application::set_tool_tips_shown(false);
    require(!fixture.window.tool_tips_shown(), "an unset window follows the program's switch");
    hover(fixture.window, {390.0, 210.0});
    hover(fixture.window, {60.0, 60.0});
    require(!fixture.window.next_wake(), "the program's switch stops the wait");
    fixture.window.set_tool_tips_shown(true);
    require(fixture.window.tool_tips_shown(), "the window's own switch wins over the program's");
    hover(fixture.window, {390.0, 210.0});
    hover(fixture.window, {60.0, 60.0});
    fire_next(fixture.window);
    require(fixture.window.tool_tip_visible(), "a window switched on shows its tooltips");
    Application::set_tool_tips_shown(true);
    fixture.window.show_tool_tip(fixture.button, 0ms);
    fixture.window.set_tool_tips_shown(false);
    require(fixture.window.tool_tip_visible(), "a tooltip the program showed stays whatever the switches");
    fixture.window.hide_tool_tip();
    require(!fixture.window.tool_tip_visible(), "hide closes it");
}

void test_press_hides_and_text_changes_follow() {
    Fixture fixture;
    ToolTipSettings settings = fixture.window.tool_tip_settings();
    settings.initial_delay = 1ms;
    settings.auto_pop_delay = 0ms;
    fixture.window.set_tool_tip_settings(settings);
    (*fixture.button).tool_tip_text = "Before";
    hover(fixture.window, {60.0, 60.0});
    fire_next(fixture.window);
    require(fixture.window.tool_tip_text() == "Before" && !fixture.window.next_wake(),
            "a zero auto-pop keeps the tooltip without a wake");
    (*fixture.button).tool_tip_text = "After";
    require(fixture.window.tool_tip_visible() && fixture.window.tool_tip_text() == "After",
            "a tooltip that shows shows its thing's new text");
    fixture.window.perform_layout();
    const SemanticSnapshot snapshot = fixture.window.semantic_snapshot();
    const SemanticNode* tip = find_role(snapshot.roots, SemanticRole::tool_tip);
    require(tip && (*tip).name == "After", "the bubble is rebuilt with the new text");
    static_cast<void>(fixture.window.dispatch_pointer({PointerAction::down, PointerButton::primary, {60.0, 60.0}}));
    static_cast<void>(fixture.window.dispatch_pointer({PointerAction::up, PointerButton::primary, {60.0, 60.0}}));
    require(!fixture.window.tool_tip_visible(), "a press closes the tooltip");
    hover(fixture.window, {61.0, 61.0});
    require(!fixture.window.next_wake(), "after a press nothing waits until the pointer leaves the thing");
    hover(fixture.window, {390.0, 210.0});
    hover(fixture.window, {60.0, 60.0});
    require(fixture.window.next_wake().has_value(), "back over the thing, its tooltip waits again");
    (*fixture.button).tool_tip_text = std::string{};
    require(!fixture.window.next_wake() && !fixture.window.tool_tip_visible(),
            "emptying the text of a waiting thing cancels the wait");
    bool refused = false;
    try {
        (*fixture.button).tool_tip_text = std::string("\xff", 1U);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*fixture.button).tool_tip_text.read().empty(), "invalid UTF-8 is refused, changing nothing");
    require((*fixture.button).has_bindable_property("tool_tip_text"), "the text is a bindable property");
    bool explicit_refused = false;
    try {
        fixture.window.show_tool_tip(fixture.button);
    } catch (const std::invalid_argument&) {
        explicit_refused = true;
    }
    require(explicit_refused, "showing a control without text is refused");
}

// A strip's items carry their own tooltips; the presenter finds the one
// under the pointer and the one the keyboard is on.
void test_strip_item_tooltips() {
    Control::Ptr root = make_control<Control>(StableId("strip-tips.root"));
    (*root).set_requested_bounds({0.0, 0.0, 420.0, 200.0});
    std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(StableId("strip-tips.strip"));
    (*strip).set_requested_bounds({0.0, 0.0, 420.0, 30.0});
    std::vector<ToolStripItemSpec> items{};
    items.push_back({.stable_id = "open", .text = "Open", .tool_tip_text = "Open a file"});
    items.push_back({.stable_id = "save", .text = "Save", .tool_tip_text = "Save (Ctrl+S)"});
    items.push_back({.stable_id = "plain", .text = "Plain"});
    (*strip).set_items(items);
    (*root).add_child(strip);
    Window window(root, {420.0, 200.0});
    ToolTipSettings settings = window.tool_tip_settings();
    settings.initial_delay = 1ms;
    settings.reshow_delay = 1ms;
    window.set_tool_tip_settings(settings);
    window.perform_layout();
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* save = find_id(snapshot.roots, "save");
    const SemanticNode* plain = find_id(snapshot.roots, "plain");
    require(save && (*save).tool_tip == "Save (Ctrl+S)" && plain && (*plain).tool_tip.empty(),
            "item tooltips reach the items' semantic nodes");
    const Rect save_bounds = (*save).bounds;
    const Rect plain_bounds = (*plain).bounds;

    hover(window, {save_bounds.x + 4.0, save_bounds.y + 4.0});
    fire_next(window);
    require(window.tool_tip_visible() && window.tool_tip_item_id() == "save" &&
                window.tool_tip_text() == "Save (Ctrl+S)" && window.tool_tip_target() == strip,
            "resting over an item shows the item's tooltip");
    hover(window, {plain_bounds.x + 4.0, plain_bounds.y + 4.0});
    require(!window.tool_tip_visible(), "leaving the item closes its tooltip");
    if (window.next_wake()) fire_next(window);
    require(!window.tool_tip_visible(), "an item without a tooltip shows none");
    // An item's tooltip only overrides where it exists: over an item without
    // one, the strip's own text shows.
    hover(window, {390.0, 150.0});
    (*strip).tool_tip_text = "Commands";
    hover(window, {save_bounds.x + 4.0, save_bounds.y + 4.0});
    fire_next(window);
    require(window.tool_tip_text() == "Save (Ctrl+S)", "an item's tooltip overrides the strip's");
    hover(window, {plain_bounds.x + 4.0, plain_bounds.y + 4.0});
    if (window.next_wake()) fire_next(window);
    require(window.tool_tip_visible() && window.tool_tip_text() == "Commands" && window.tool_tip_item_id().empty(),
            "an item without a tooltip shows the strip's own");
    (*strip).tool_tip_text = "";
    hover(window, {390.0, 150.0});
    if (window.next_wake()) fire_next(window);

    // The keyboard: focus reaches the strip, and the arrows move between
    // items. The pointer leaves first: the strip's keyboard item starts at
    // the item under the pointer.
    hover(window, {390.0, 150.0});
    press_key(window, PhysicalKey::tab);
    require(window.focused_control() == strip, "Tab focuses the strip");
    fire_next(window);
    require(window.tool_tip_visible() && window.tool_tip_item_id() == "open",
            "keyboard focus shows the tooltip of the item it is on");
    press_key(window, PhysicalKey::right);
    require(!window.tool_tip_visible(), "moving to another item closes the tooltip");
    fire_next(window);
    require(window.tool_tip_visible() && window.tool_tip_item_id() == "save",
            "the next item's tooltip shows");
}

// A tab page's tooltip shows over its header, never over its body.
void test_tab_header_tooltips() {
    std::shared_ptr<TabControl> tabs = make_control<TabControl>(StableId("tab-tips"));
    (*tabs).set_requested_bounds({0.0, 0.0, 400.0, 240.0});
    std::shared_ptr<TabPage> first = make_control<TabPage>(StableId("tab-tips.first"), "General");
    std::shared_ptr<TabPage> second = make_control<TabPage>(StableId("tab-tips.second"), "Advanced");
    (*second).tool_tip_text = "Network and cache settings";
    (*tabs).add_page(first);
    (*tabs).add_page(second);
    Window window(tabs, {400.0, 240.0});
    ToolTipSettings settings = window.tool_tip_settings();
    settings.initial_delay = 1ms;
    window.set_tool_tip_settings(settings);
    window.perform_layout();
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* header = find_id(snapshot.roots, "tab-tips.second.tab");
    require(header && (*header).tool_tip == "Network and cache settings", "the header carries its page's tooltip");
    const Rect header_bounds = (*header).bounds;
    hover(window, {header_bounds.x + 6.0, header_bounds.y + 6.0});
    fire_next(window);
    require(window.tool_tip_visible() && window.tool_tip_item_id() == "tab-tips.second.tab" &&
                window.tool_tip_text() == "Network and cache settings",
            "resting over the header shows the page's tooltip");
    (*tabs).set_selected_index(1U);
    window.perform_layout();
    hover(window, {200.0, 160.0});
    require(!window.tool_tip_visible(), "leaving the header closes it");
    if (window.next_wake()) fire_next(window);
    require(!window.tool_tip_visible(), "the page's body shows no tooltip");
}

// Typed-text fields show their tooltip as light grey hint text in the empty
// field while they have focus, never as a bubble; a placeholder wins, and
// hover still shows the bubble.
void test_typed_text_fields_show_hints() {
    Control::Ptr root = make_control<Control>(StableId("hint.root"));
    (*root).set_requested_bounds({0.0, 0.0, 420.0, 320.0});
    std::shared_ptr<TextBox> name = make_control<TextBox>(StableId("hint.name"));
    std::shared_ptr<TextBox> search = make_control<TextBox>(StableId("hint.search"));
    std::shared_ptr<NumericUpDown> copies = make_control<NumericUpDown>(StableId("hint.copies"));
    std::shared_ptr<RichTextBox> notes = make_control<RichTextBox>(StableId("hint.notes"));
    (*name).set_requested_bounds({20.0, 20.0, 200.0, 28.0});
    (*search).set_requested_bounds({20.0, 60.0, 200.0, 28.0});
    (*copies).set_requested_bounds({20.0, 100.0, 120.0, 28.0});
    (*notes).set_requested_bounds({20.0, 140.0, 300.0, 160.0});
    (*name).tool_tip_text = "Your full name";
    (*search).tool_tip_text = "Searches every folder";
    (*search).placeholder_text = "Search";
    (*copies).tool_tip_text = "How many copies";
    (*notes).tool_tip_text = "Notes for the reviewer";
    (*root).add_child(name);
    (*root).add_child(search);
    (*root).add_child(copies);
    (*root).add_child(notes);
    Window window(root, {420.0, 320.0});
    ToolTipSettings settings = window.tool_tip_settings();
    settings.initial_delay = 1ms;
    window.set_tool_tip_settings(settings);
    window.perform_layout();
    require(!painted_text(window, "hint.name", "Your full name"), "a field without focus shows no hint");

    press_key(window, PhysicalKey::tab);
    require(window.focused_control() == name && window.focus_cue_visible(), "Tab focuses the first field");
    // A field's caret blinks on its own wake; whatever is due runs, and no
    // bubble opens.
    run_due(window);
    require(!window.tool_tip_visible(), "keyboard focus on a typed-text field shows no bubble");
    require(painted_text(window, "hint.name", "Your full name"), "the focused empty field shows its hint");
    (*name).text = "Ada";
    require(!painted_text(window, "hint.name", "Your full name"), "typed text replaces the hint");

    static_cast<void>(window.request_focus(search));
    require(painted_text(window, "hint.search", "Search") && !painted_text(window, "hint.search", "Searches every folder"),
            "a placeholder that has been set wins");

    const std::shared_ptr<TextBox> copies_editor = (*copies).editor();
    static_cast<void>(window.request_focus(copies_editor));
    run_due(window);
    require(!window.tool_tip_visible(), "an up-down's edit field shows no bubble either");
    (*copies_editor).text = std::string{};
    const std::string editor_id((*copies_editor).stable_id().value());
    require(painted_text(window, editor_id, "How many copies"), "an emptied up-down field shows the up-down's hint");
    (*copies).tool_tip_text = "Copies to print";
    require(painted_text(window, editor_id, "Copies to print"), "the edit field follows its owner's new text");

    // An editable combo box: its hint shows in the empty edit field, and a
    // placeholder set on the combo box wins there too.
    std::shared_ptr<ComboBox> city = make_control<ComboBox>(StableId("hint.city"));
    (*city).drop_down_style = ComboBoxStyle::drop_down;
    (*city).set_requested_bounds({240.0, 20.0, 160.0, 28.0});
    (*city).tool_tip_text = "The city you live in";
    (*root).add_child(city);
    window.perform_layout();
    const std::shared_ptr<TextBox> city_editor = (*city).editor();
    require(city_editor != nullptr, "an editable combo box has an edit field");
    const std::string city_editor_id((*city_editor).stable_id().value());
    static_cast<void>(window.request_focus(city_editor));
    run_due(window);
    require(!window.tool_tip_visible() && painted_text(window, city_editor_id, "The city you live in"),
            "an editable combo box shows its hint in the edit field");
    (*city).placeholder_text = "City";
    require(painted_text(window, city_editor_id, "City") &&
                !painted_text(window, city_editor_id, "The city you live in"),
            "the combo box's placeholder wins in its edit field");

    static_cast<void>(window.request_focus(notes));
    run_due(window);
    require(!window.tool_tip_visible() && painted_text(window, "hint.notes", "Notes for the reviewer"),
            "an empty rich text box shows its hint");

    hover(window, {30.0, 30.0});
    run_due(window);
    require(window.tool_tip_visible() && window.tool_tip_text() == "Your full name",
            "resting the pointer on a field still shows its bubble");
}

} // namespace

int main() {
    try {
        test_hover_delay_cancel_and_accessible_overlay();
        test_motion_restarts_the_rest();
        test_keyboard_focus_shows_below_and_follows_the_target();
        test_repeated_focus_show_hide_reuses_no_stale_overlay_identity();
        test_one_presenter_for_every_provider_and_owner_disposal();
        test_mapping_removal_and_provider_disposal_are_quiescent();
        test_show_always_supports_a_disabled_visible_owner();
        test_maximum_width_is_bounded_and_shapes_overlay();
        test_window_and_program_switches();
        test_press_hides_and_text_changes_follow();
        test_strip_item_tooltips();
        test_tab_header_tooltips();
        test_typed_text_fields_show_hints();
        std::cout << "tooltip tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tooltip tests failed: " << error.what() << '\n';
        return 1;
    }
}
