#pragma once

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/controls/lookup_combo_box/lookup_combo_box_capabilities/lookup_combo_box_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class AnchoredOverlayLayer;
enum class OverlayDismissReason : std::uint8_t;

// One column of a LookupComboBox's list: its header and its width in
// logical units.
struct LookupColumn final {
    std::string header{};
    double width{100.0};

    friend bool operator==(const LookupColumn& left, const LookupColumn& right) noexcept {
        return left.header == right.header && left.width == right.width;
    }
};

// One row of a LookupComboBox: the key the application stores and the
// cells shown, one per column (missing cells show empty).
struct LookupRow final {
    std::string key{};
    std::vector<std::string> cells{};

    friend bool operator==(const LookupRow& left, const LookupRow& right) noexcept {
        return left.key == right.key && left.cells == right.cells;
    }
};

// A multi-column lookup (the superset's P1 rest; a lookup edit or
// multi-column combo): a drop-down field that chooses a row by its key and
// shows one of the row's cells (display_column), with a list of every
// column under headers when open. The field is not editable: while the list
// is open, typing filters it to the rows any of whose cells contain the
// typed text (ignoring ASCII case), Backspace removes the last typed
// character, and the filter shows in the field.
//
// Keys follow a WinForms ComboBox in DropDownList style: Alt+Down or F4
// opens and closes the list; with it closed, Up and Down choose the
// previous or next row at once; with it open, Up, Down, Page Up, Page Down,
// Home and End move the highlight, Enter or Alt+Up chooses the highlighted
// row and closes, and Escape closes without choosing. A press on the field
// toggles the list; a press on a row chooses it; a press elsewhere closes
// the list. Keyboard focus stays on the field throughout.
//
// Theme: the field is `well` and the drop-down `actuator` with its
// chevron `glyph` (choice family); the open list's `face` (menu_item
// family), its `header` captions, its rows (`item`, selected when
// highlighted) and their cells' `label`s. To assistive technology it is a
// combo box whose value is the shown cell, expanded while the list is open.
class LookupComboBox final : public Control, public LookupComboBoxCapabilities<LookupComboBox> {
public:
    explicit LookupComboBox(StableId stable_id);
    ~LookupComboBox() override;

    // The columns; a header must be valid UTF-8 and a width finite and
    // positive (std::invalid_argument, changing nothing).
    void set_columns(std::vector<LookupColumn> columns);
    [[nodiscard]] const std::vector<LookupColumn>& columns() const noexcept { return columns_; }
    // The rows; keys must be distinct and non-empty and every text valid
    // UTF-8 (std::invalid_argument, changing nothing). A selected key no
    // row has any more is cleared (raising selected_key_changed).
    void set_rows(std::vector<LookupRow> rows);
    [[nodiscard]] const std::vector<LookupRow>& rows() const noexcept { return rows_; }
    // The row with `key`, if any.
    [[nodiscard]] std::optional<std::size_t> row_of(std::string_view key) const noexcept;
    // The text the field shows: the filter while open and typed into, else
    // the chosen row's display cell.
    [[nodiscard]] std::string shown_text() const;

    void open_drop_down();
    void close_drop_down();
    [[nodiscard]] bool is_dropped_down() const noexcept { return dropped_down_; }
    // The typed filter while open (empty when closed).
    [[nodiscard]] const std::string& filter() const noexcept { return filter_; }
    // The rows the open list shows, as indices into rows(), in order.
    [[nodiscard]] const std::vector<std::size_t>& filtered_rows() const noexcept { return filtered_; }
    // The highlighted position in filtered_rows(), if any.
    [[nodiscard]] std::optional<std::size_t> highlighted() const noexcept { return highlighted_; }

    [[nodiscard]] Event<const std::string&>& selected_key_changed() noexcept { return selected_key_changed_; }
    [[nodiscard]] Event<bool>& drop_down_changed() noexcept { return drop_down_changed_; }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_text_input(TextInputEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry well;
        PartGeometry label;
        PartGeometry actuator;
        PartGeometry glyph;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

    // The open list's control (private to the source).
    class List;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_dispose() noexcept override;

private:
    friend class List;
    struct Placement final {
        Rect well{};
        Rect label{};
        Rect actuator{};
        Rect glyph{};
    };
    [[nodiscard]] Placement placement() const;
    [[nodiscard]] FontSpec field_font() const noexcept;
    void refilter();
    void choose_row(std::size_t row);
    void move_highlight(std::ptrdiff_t delta, bool to_edge);
    void step_selection(std::ptrdiff_t delta);
    void on_row_activated(std::size_t position);
    void on_dismiss(OverlayDismissReason reason);
    void on_overlay_revoked();
    void release_overlay() noexcept;

    std::vector<LookupColumn> columns_{};
    std::vector<LookupRow> rows_{};
    std::vector<std::size_t> filtered_{};
    std::optional<std::size_t> highlighted_{};
    std::string filter_{};
    std::shared_ptr<AnchoredOverlayLayer> layer_{};
    std::shared_ptr<List> list_{};
    OverlayToken overlay_{};
    SubscriptionToken overlay_revoked_{};
    SubscriptionToken dismissal_{};
    SubscriptionToken activation_{};
    bool dropped_down_{false};
    bool focused_{false};
    bool hot_{false};
    Event<const std::string&> selected_key_changed_;
    Event<bool> drop_down_changed_;
    // property_codemod begin LookupComboBox (tools/property_families/lookup_combo_box.json; generated, do not edit)
public:
    // LookupComboBox's property table: Control's entries, then its own.
    static constexpr std::size_t lookup_combo_box_property_count = Control::control_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, lookup_combo_box_property_count>& lookup_combo_box_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct LookupComboBoxSelectedKeyCapability<LookupComboBox>;
    friend struct LookupComboBoxDisplayColumnCapability<LookupComboBox>;
    friend struct LookupComboBoxMaxDropDownRowsCapability<LookupComboBox>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(selected_key)& cell, std::string& incoming);
    void written(const decltype(selected_key)& cell);
    void prepare_write(const decltype(display_column)& cell, std::uint32_t& incoming);
    void written(const decltype(display_column)& cell);
    void prepare_write(const decltype(max_drop_down_rows)& cell, std::uint32_t& incoming);
    void written(const decltype(max_drop_down_rows)& cell);
    // property_codemod end LookupComboBox
};

} // namespace gui_forms
