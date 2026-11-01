#pragma once

#include "gui_forms/commands.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/controls/panel/combo_box/combo_box.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/controls/range_control/progress_bar/progress_bar.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// The kinds of item a ToolStrip shows (Windows Forms' ToolStripButton,
// ToolStripLabel, ToolStripSeparator, ToolStripDropDownButton,
// ToolStripSplitButton, ToolStripComboBox, ToolStripTextBox and
// ToolStripProgressBar). The last three are hosted items: the strip owns a
// real ComboBox, TextBox or ProgressBar child control for each and places it
// at the item's bounds.
enum class ToolStripItemKind : std::uint8_t {
    button,
    label,
    separator,
    drop_down_button,
    split_button,
    combo_box,
    text_box,
    progress_bar,
};

// What an item shows: its text, its image, or both (the default; an item
// with no image shows only its text).
enum class ToolStripItemDisplay : std::uint8_t { text, image, image_and_text };

// Where an item sits: from the strip's start, or from its end (Windows Forms'
// ToolStripItemAlignment.Right on a left-to-right strip).
enum class ToolStripItemPlacement : std::uint8_t { start, end };

// Whether an item that does not fit moves to the overflow menu, or is cut off.
enum class ToolStripOverflow : std::uint8_t { as_needed, never };

// One item. `stable_id` is unique within the strip and names the item in
// events, semantics and the published anatomy.
//
// An item with a `command` takes its text (when `text` is empty), enabled,
// visible and checked state from it, and a click executes it. An item
// without one uses `text`, `enabled`, `visible` and the strip's own checked
// state. `drop_down` is the menu of a drop-down or split button and must be
// empty for other kinds. `spring` is honoured only by StatusStrip, on labels:
// spring labels share the width the other items leave.
//
// A hosted item (combo box, text box, progress bar) takes no command, menu or
// check. Its `text` is the hosted control's accessible name, `choices` are a
// combo box's items, and `width` its width in logical units (zero for the
// default: 120 for a combo box, 100 otherwise). Hosted items never move to the
// overflow menu, whatever their `overflow`: a menu row cannot hold a control.
// The application reaches the control through combo_box(), text_box() or
// progress_bar(); it keeps its state while set_items keeps an item of the
// same ID and kind.
//
// `tool_tip_text` is the item's plain tooltip (owner, 2026-10-28): the
// window's presenter shows it over the item, and keyboard focus on the item
// shows it below the item; empty shows none. It reaches the item's semantic
// node (SemanticNode::tool_tip), and a hosted item's control as that
// control's tooltip text.
struct ToolStripItemSpec final {
    std::string stable_id{};
    ToolStripItemKind kind{ToolStripItemKind::button};
    std::string text{};
    std::string tool_tip_text{};
    ImageId image{};
    ToolStripItemDisplay display{ToolStripItemDisplay::image_and_text};
    std::shared_ptr<Command> command{};
    std::vector<MenuItemSpec> drop_down{};
    ToolStripItemPlacement placement{ToolStripItemPlacement::start};
    ToolStripOverflow overflow{ToolStripOverflow::as_needed};
    bool enabled{true};
    bool visible{true};
    // A button that toggles its checked state on each click (Windows Forms'
    // CheckOnClick); with a command, the command's checked state toggles.
    bool check_on_click{};
    bool checked{};
    bool spring{};
    std::vector<std::string> choices{};
    double width{};
};

// A click on an item, after any command it carries has executed.
struct ToolStripItemClick final {
    std::string strip_id{};
    std::string item_id{};
};

// The item the keyboard is on changed while the strip has focus: the strip
// took focus, or an arrow key or a semantic focus action moved it.
struct ToolStripItemFocus final {
    std::string strip_id{};
    std::string item_id{};
};

// A choice from a drop-down or split button's menu, or from the overflow
// menu's copy of one.
struct ToolStripDropDownInvocation final {
    std::string strip_id{};
    std::string item_id{};
    MenuItemInvocation choice{};
};

// A retained command strip (Windows Forms' ToolStrip): a row of buttons,
// labels, separators, drop-down and split buttons. Items are records, not
// child controls; the strip lays them out as its "item" item system and
// paints every part through the by-role paint path. Items that do not fit
// move to an overflow menu opened from the overflow button at the strip's
// end. A strip is laid out horizontally, or vertically (top to bottom) for
// the left and right panels of a ToolStripContainer.
class ToolStrip : public Control {
public:
    explicit ToolStrip(StableId stable_id);
    ~ToolStrip() override;

    [[nodiscard]] const std::vector<ToolStripItemSpec>& items() const noexcept { return items_; }
    // Replaces the items. Throws std::invalid_argument for an empty or
    // duplicate ID, invalid UTF-8, a drop-down menu on a kind that has none,
    // a drop-down or split button without one, an invalid menu, a hosted
    // item with a command or check, choices on an item that is not a combo
    // box, or a width that is negative, not finite or over 1024.
    void set_items(std::vector<ToolStripItemSpec> items);
    // The control a hosted item holds. Throws std::out_of_range for an ID
    // that names no item of that kind.
    [[nodiscard]] std::shared_ptr<ComboBox> combo_box(std::string_view item_id) const;
    [[nodiscard]] std::shared_ptr<TextBox> text_box(std::string_view item_id) const;
    [[nodiscard]] std::shared_ptr<ProgressBar> progress_bar(std::string_view item_id) const;
    // Replaces one item's text (a hosted item's accessible name). Throws
    // std::out_of_range for an unknown ID and std::invalid_argument for
    // invalid UTF-8.
    void set_item_text(std::string_view item_id, std::string text);
    // The checked state the strip shows for an item: its command's when it
    // has one. Throws std::out_of_range for an unknown ID.
    [[nodiscard]] bool item_checked(std::string_view item_id) const;
    void set_item_checked(std::string_view item_id, bool checked);
    // Whether the grip is shown at the strip's start.
    [[nodiscard]] bool grip_visible() const noexcept { return grip_visible_; }
    void set_grip_visible(bool visible);
    [[nodiscard]] FontSpec font() const noexcept { return font_; }
    void set_font(FontSpec font);
    // Horizontal strips run left to right; vertical strips run top to bottom
    // and are as wide as their widest item. A StatusStrip stays horizontal
    // and throws std::logic_error when set vertical.
    [[nodiscard]] Orientation orientation() const noexcept { return orientation_; }
    void set_orientation(Orientation orientation);

    // Opens a drop-down or split button's menu, or closes any open menu.
    bool show_drop_down(std::string_view item_id, MenuOpenMode mode = MenuOpenMode::keyboard);
    void close_drop_down() noexcept;
    [[nodiscard]] bool drop_down_open() const noexcept;
    // The IDs of the items currently in the overflow menu, in strip order.
    [[nodiscard]] std::vector<std::string> overflow_item_ids() const;
    bool show_overflow(MenuOpenMode mode = MenuOpenMode::keyboard);
    // The ID of the laid-out item under a point in window coordinates,
    // enabled or not; empty when no item is there. For providers that
    // describe items, such as RibbonToolTip.
    [[nodiscard]] std::string item_id_at(Point window_point) const;
    // The ID of the item the keyboard is on while the strip has focus;
    // empty otherwise.
    [[nodiscard]] std::string focused_item_id() const;

    [[nodiscard]] Event<const ToolStripItemClick&>& item_clicked() noexcept { return item_clicked_; }
    [[nodiscard]] Event<const ToolStripDropDownInvocation&>& drop_down_item_invoked() noexcept {
        return drop_down_item_invoked_;
    }
    [[nodiscard]] Event<const ToolStripItemFocus&>& item_focus_changed() noexcept {
        return item_focus_changed_;
    }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(std::size_t item) const override;

    // Stage T1 anatomy, as dot members; its states are Control's.
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry grip;
        PartGeometry overflow_button;
        PartGeometry overflow_glyph;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;
    // Item part names start with the word item (item_face): an item part may not repeat
    // one of the strip's own part names (face, focus).
    struct ItemParts final {
        PartGeometry item_face;
        PartGeometry item_icon;
        PartGeometry item_label;
        PartGeometry item_arrow;
        PartGeometry item_arrow_glyph;
        PartGeometry item_separator;
        PartGeometry item_focus;
        // The hosted control of a combo box, text box or progress bar item.
        PartGeometry item_combo_box;
        PartGeometry item_text_box;
        PartGeometry item_progress_bar;
    };
    struct ItemStates final {
        bool hot{};
        bool pressed{};
        bool checked{};
        bool open{};
        bool disabled{};
        bool focus_visible{};
    };
    struct Item final {
        std::size_t index{};
        std::string stable_id;
        ItemParts parts;
        ItemStates states;
    };
    // The items laid out on the strip (not those in the overflow menu), in
    // strip order.
    [[nodiscard]] std::vector<Item> realized_items() const;

protected:
    // For StatusStrip: a strip whose items never overflow, whose spring
    // labels share the free width, and which may show a sizing grip.
    struct StatusLayout final {};
    ToolStrip(StableId stable_id, StatusLayout);

    // Where each item and the strip's own parts sit, from one layout pass.
    struct Layout final {
        std::vector<Rect> items;       // per item; empty when hidden or overflowed
        std::vector<bool> overflowed;  // per item
        Rect grip{};
        Rect overflow_button{};
        Rect sizing_grip{};
        // The end reserve (set_end_reserve), at the strip's far end.
        Rect end_reserve{};
    };
    [[nodiscard]] Layout layout() const;
    [[nodiscard]] Layout layout_for(Rect surface) const;
    [[nodiscard]] bool status_layout() const noexcept { return status_layout_; }
    void set_sizing_grip_visible(bool visible);
    [[nodiscard]] bool sizing_grip_visible() const noexcept { return sizing_grip_visible_; }
    // For QuickAccessToolbar: logical units kept free at the strip's far end
    // for a part the derived control paints there (its customize button).
    // Items and the overflow button sit before it. Zero by default. Throws
    // std::invalid_argument for a width that is negative, not finite or
    // over 64.
    void set_end_reserve(double logical_width);
    // Checks items before set_items changes anything. ToolStrip accepts every
    // valid item; a derived strip that admits fewer kinds throws
    // std::invalid_argument for the others.
    virtual void validate_items(const std::vector<ToolStripItemSpec>& items) const;

    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;
    [[nodiscard]] std::size_t realized_item_index(std::size_t item) const;
    // The strip's own parts, as ToolStrip publishes them.
    [[nodiscard]] Parts strip_parts() const;

private:
    // Which part of an item a point or a press is on.
    enum class ItemZone : std::uint8_t { none, main, arrow };
    struct ItemState final {
        bool checked{};
        std::shared_ptr<Command> overflow_command{};
        // The child control of a hosted item; null for other kinds.
        Control::Ptr hosted{};
    };

    void construct();
    [[nodiscard]] bool vertical() const noexcept;
    [[nodiscard]] Control::Ptr hosted_control(std::string_view item_id, ToolStripItemKind kind) const;
    [[nodiscard]] Control::Ptr make_hosted(const ToolStripItemSpec& item) const;
    void configure_hosted(const ToolStripItemSpec& item, Control& hosted) const;
    [[nodiscard]] std::optional<std::size_t> index_of(std::string_view item_id) const;
    [[nodiscard]] Size item_size(const ToolStripItemSpec& item) const;
    [[nodiscard]] std::string item_text(const ToolStripItemSpec& item) const;
    [[nodiscard]] bool item_enabled(const ToolStripItemSpec& item) const;
    [[nodiscard]] bool item_visible(const ToolStripItemSpec& item) const;
    [[nodiscard]] bool item_interactive(std::size_t index) const;
    [[nodiscard]] ItemParts item_parts(std::size_t index, Rect bounds) const;
    [[nodiscard]] AnatomyStateSet item_states(std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> index_at(Point window_point, ItemZone& zone) const;
    [[nodiscard]] bool overflow_at(Point window_point) const;
    [[nodiscard]] std::optional<std::size_t> next_interactive(std::size_t start, int direction) const;
    void click(std::size_t index);
    // Flips a check-on-click item's checked state: its command's when it has
    // one, else the strip's own. Returns whether the strip's own state
    // changed (a command's change reaches the strip through on_command_state).
    [[nodiscard]] bool flip_checked(std::size_t index);
    void set_hot(std::optional<std::size_t> index, ItemZone zone);
    void focus_item(std::optional<std::size_t> index);
    void paint_item(Painter& painter, std::size_t index, Rect bounds) const;
    void on_popup_item_invoked(const MenuItemInvocation& invocation);
    void on_popup_open_changed(bool open);
    void on_command_state(const CommandState& state);
    [[nodiscard]] std::vector<MenuItemSpec> overflow_menu() const;

    std::vector<ToolStripItemSpec> items_{};
    std::vector<ItemState> item_states_{};
    std::unique_ptr<ContextMenu> popup_{};
    // The item whose menu is open; none with the overflow menu or no menu.
    std::optional<std::size_t> open_index_{};
    bool overflow_open_{};
    std::optional<std::size_t> hot_index_{};
    ItemZone hot_zone_{ItemZone::none};
    std::optional<std::size_t> pressed_index_{};
    ItemZone pressed_zone_{ItemZone::none};
    bool overflow_hot_{};
    bool focused_{};
    bool grip_visible_{true};
    Orientation orientation_{Orientation::horizontal};
    bool status_layout_{};
    bool sizing_grip_visible_{};
    FontSpec font_{FontRole::control, 10.5, 400, false, 0.12};
    double end_reserve_{};
    SubscriptionToken popup_invoked_{};
    SubscriptionToken popup_changed_{};
    // One per item command, so a command's enabled, visible, text or
    // checked change repaints the strip.
    std::vector<SubscriptionToken> command_tokens_{};
    Event<const ToolStripItemClick&> item_clicked_{};
    Event<const ToolStripDropDownInvocation&> drop_down_item_invoked_{};
    Event<const ToolStripItemFocus&> item_focus_changed_{};
};

} // namespace gui_forms
