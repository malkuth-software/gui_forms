#pragma once
// CheckedComboBox: a drop-down field whose list has a tick box on each item,
// for choosing several items in the room of one field, such as the columns
// to show or the days a task repeats on. Windows Forms has no counterpart;
// the field looks like a drop-down-list ComboBox and its list is a
// CheckedListBox shown in an anchored overlay below it. The field's text is
// the checked items' texts, in item order, joined by `separator`, or
// `placeholder_text` while none is checked.
//
// Items are records kept by stable ID. A click on the field, F4, Alt+Down or
// Alt+Up, or the semantic expand opens the list, which takes the keyboard
// focus: Space or a click on an item toggles it, and each change applies at
// once and raises item_check_changed. Escape, a click away, the same keys on
// the field, or the semantic collapse close the list; the focus returns to
// the field.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/controls/checked_combo_box_capabilities.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <stxlib/optional.hpp>
#include <stxlib/unique.hpp>

namespace gui_forms {

class AnchoredOverlayLayer;
class CheckedListBox;
enum class CheckState : std::uint8_t;
enum class OverlayDismissReason : std::uint8_t;

// One item. `stable_id` is unique within the box and names the item in
// item_check_changed and checked_ids.
struct GUI_FORMS_API CheckedComboItem final {
    std::string stable_id{};
    GUI_FORMS_LOCALIZABLE(text) std::string text{};
    bool checked{};
};

// An item checked or unchecked, after it happened.
struct GUI_FORMS_API CheckedComboItemChange final {
    std::string item_id{};
    bool checked{};
};

class GUI_FORMS_API CheckedComboBox final : public Control, public CheckedComboBoxCapabilities<CheckedComboBox> {
public:
    // Development bound, not measured capacity.
    static constexpr std::size_t maximum_items = 1024U;

    explicit CheckedComboBox(StableId stable_id);
    ~CheckedComboBox() override;

    // -- items ---------------------------------------------------------------------
    [[nodiscard]] BorrowedSpan<CheckedComboItem> items() const& noexcept { return detail::lend_view(items_); }
    BorrowedSpan<CheckedComboItem> items() const&& = delete;
    // Replaces the items (an open list closes first). Throws
    // std::invalid_argument for an empty or duplicate ID or one that breaks
    // the StableId segment rule, invalid UTF-8, or more than maximum_items
    // items; the old items then stay. Raises no item_check_changed.
    void set_items(std::vector<CheckedComboItem> items);
    // Checks or unchecks one item, raising item_check_changed when it
    // changes. Throws std::out_of_range for an unknown item.
    void set_item_checked(std::string_view item_id, bool checked);
    // Whether an item is checked. Throws std::out_of_range for an unknown
    // item.
    [[nodiscard]] bool item_checked(std::string_view item_id) const;
    // Checks or unchecks every item, raising item_check_changed for each
    // that changes, in item order.
    void set_all_checked(bool checked);
    // The checked items' IDs, in item order.
    [[nodiscard]] std::vector<std::string> checked_ids() const;
    // The position of the item with this ID, or none.
    [[nodiscard]] stxlib::Optional<std::size_t> index_of(std::string_view item_id) const;
    // The field's text: the checked items' texts joined by the separator,
    // or empty while none is checked (the placeholder is shown then).
    [[nodiscard]] std::string text() const;

    // -- drop-down -----------------------------------------------------------------
    [[nodiscard]] bool dropped_down() const noexcept { return dropped_down_; }
    // Opens the list. False when it is open, the box is not in a window or
    // not enabled, or there are no items.
    bool show_drop_down();
    // Closes an open list; false when it is not open.
    bool close_drop_down();

    // Raised after an item is checked or unchecked.
    [[nodiscard]] Event<const CheckedComboItemChange&>& item_check_changed() noexcept {
        return item_check_changed_;
    }
    // Raised after the list opens (true) or closes (false).
    [[nodiscard]] Event<bool>& drop_down_changed() noexcept { return drop_down_changed_; }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;

    // Stage T1 anatomy, as dot members (see Control::Parts). `well` is the
    // field's ground, `label` its text, `actuator` the drop-down button and
    // `glyph` its arrow; the box is `open` while the list is.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry well;
        PartGeometry label;
        PartGeometry actuator;
        PartGeometry glyph;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

    // The open list, or null while it is closed.
    [[nodiscard]] std::shared_ptr<CheckedListBox> drop_down_list() const noexcept;

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;
    void on_key_down(KeyEventArgs& arguments) override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_detached_from_window() noexcept override;

private:
    struct Placement final {
        Rect well{};
        Rect label{};
        Rect actuator{};
        Rect glyph{};
    };
    [[nodiscard]] Placement placement() const;
    [[nodiscard]] FontSpec field_font() const noexcept;
    [[nodiscard]] bool focus_within_list() const;
    void apply_check(std::size_t item, bool checked);
    void on_list_check(std::size_t index, CheckState state);
    void on_dismiss(OverlayDismissReason reason);
    void on_overlay_revoked();
    void release_overlay() noexcept;

    detail::LendingVector<CheckedComboItem> items_{};
    // The private state, defined in checked_combo_box.cpp.
    struct State;
    [[nodiscard]] static stxlib::Unique<State> make_state();
    stxlib::Unique<State> state_{make_state()};
    OverlayToken overlay_{};
    SubscriptionToken overlay_revoked_{};
    SubscriptionToken dismissal_{};
    SubscriptionToken list_check_{};
    Event<const CheckedComboItemChange&> item_check_changed_;
    Event<bool> drop_down_changed_;
    bool dropped_down_{};
    bool hot_{};
    bool focused_{};
    // Set while the box writes the list's checks, so their echo is ignored.
    bool syncing_{};
    // property_codemod begin CheckedComboBox (tools/property_families/checked_combo_box.json; generated, do not edit)
public:
    // CheckedComboBox's property table: Control's entries, then its own.
    static constexpr std::size_t checked_combo_box_property_count = Control::control_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, checked_combo_box_property_count>& checked_combo_box_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct CheckedComboBoxSeparatorCapability<CheckedComboBox>;
    friend struct CheckedComboBoxPlaceholderTextCapability<CheckedComboBox>;
    friend struct CheckedComboBoxMaxDropDownItemsCapability<CheckedComboBox>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(separator)& cell, std::string& incoming);
    void written(const decltype(separator)& cell);
    void prepare_write(const decltype(placeholder_text)& cell, std::string& incoming);
    void written(const decltype(placeholder_text)& cell);
    void prepare_write(const decltype(max_drop_down_items)& cell, std::uint32_t& incoming);
    void written(const decltype(max_drop_down_items)& cell);
    // property_codemod end CheckedComboBox
};

} // namespace gui_forms
