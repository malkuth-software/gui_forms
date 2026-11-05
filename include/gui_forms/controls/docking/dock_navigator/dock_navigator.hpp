#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/docking/types/dock_types.hpp"
#include "gui_forms/event.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// The navigator's two lists (owner decision DK-30): the tool panels and the
// documents, each in most-recently-active order.
enum class DockNavigatorList : std::uint8_t { tools, documents };

// One row: a panel and the title shown for it.
struct DockNavigatorEntry final {
    DockPanelId panel{};
    std::string title{};
};

// The row the user chose, raised by `chosen`.
struct DockNavigatorChoice final {
    DockPanelId panel{};
    DockNavigatorList list{DockNavigatorList::documents};
};

// The two-list Ctrl+Tab switcher of docking (owner decision DK-30; docking
// study 5.7): "Active Tool Windows" on the left, "Active Files" on the
// right, each a list of rows with one current row in the current list. A
// DockSite opens it in an overlay on Ctrl+Tab (documents) or Alt+F7 (tool
// panels) and activates the chosen panel; it can also stand on its own, as
// in the Gallery.
//
// Keys while it has focus: Tab and Down step to the next row and Shift+Tab
// and Up to the previous, wrapping within the list (with or without the
// held modifier); Left and Right switch lists, keeping the row index where
// the other list has it; Enter chooses; Escape cancels. Releasing the
// release modifier (Ctrl after Ctrl+Tab) chooses the current row: a key-up
// whose modifiers no longer hold it, which is how every host reports the
// release (the release key itself has no usage on some hosts). A press on a
// row chooses it.
//
// Every part is painted through paint_part: rows in the menu_item family,
// flat at rest with a plate when current or hot, on a panel face.
class DockNavigator : public Control {
public:
    explicit DockNavigator(StableId stable_id);
    ~DockNavigator() override;

    // Replaces both lists and makes the first row of `list` current (or of
    // the other list when `list` is empty). An empty panel identity throws
    // std::invalid_argument, changing nothing.
    void set_entries(std::vector<DockNavigatorEntry> tools, std::vector<DockNavigatorEntry> documents,
                     DockNavigatorList list = DockNavigatorList::documents);
    [[nodiscard]] std::span<const DockNavigatorEntry> entries(DockNavigatorList list) const noexcept;
    [[nodiscard]] DockNavigatorList current_list() const noexcept { return list_; }
    // The current row's index in the current list, or none when both lists
    // are empty.
    [[nodiscard]] std::optional<std::size_t> current_index() const noexcept { return current_; }
    // The current row's panel, or none.
    [[nodiscard]] std::optional<DockPanelId> current_panel() const;
    // Makes a row current. False, changing nothing, for an index past the
    // list's end.
    bool select(DockNavigatorList list, std::size_t index);
    // Steps the current row by `direction` (+1 or -1) within its list,
    // wrapping. False when the list is empty.
    bool step(int direction);
    // Moves to the other list, keeping the index where it exists there (else
    // its last row). False when the other list is empty.
    bool switch_list();

    // The modifier whose release chooses the current row (Ctrl after
    // Ctrl+Tab, Alt after Alt+F7), or none to choose only with Enter or a
    // press. Only control, alt and meta are accepted; others throw
    // std::invalid_argument.
    [[nodiscard]] Modifier release_modifier() const noexcept { return release_modifier_; }
    void set_release_modifier(Modifier modifier);

    // Raises `chosen` with the current row; false when there is none.
    bool choose();
    // Raises `cancelled`.
    void cancel();

    [[nodiscard]] Event<const DockNavigatorChoice&>& chosen() noexcept { return chosen_; }
    [[nodiscard]] Event<>& cancelled() noexcept { return cancelled_; }

    // The size that shows every row of the longer list beside the other.
    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
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
        PartGeometry tool_heading;
        PartGeometry document_heading;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;
    // Item system entry: one row.
    struct ItemParts final {
        PartGeometry entry_face;
        PartGeometry entry_label;
    };
    struct ItemStates final {
        bool hot{};
        bool selected{};
    };
    struct Item final {
        DockNavigatorList list{DockNavigatorList::tools};
        std::size_t index{};
        DockPanelId panel{};
        std::string stable_id;
        ItemParts parts;
        ItemStates states;
    };
    // Every row, the tool list's first.
    [[nodiscard]] std::vector<Item> realized_items() const;

    // The semantic identity of a row: `<id>.tools.<panel>` or
    // `<id>.documents.<panel>`.
    [[nodiscard]] std::string entry_identity(DockNavigatorList list, const DockPanelId& panel) const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;

private:
    struct Row final {
        DockNavigatorList list{DockNavigatorList::tools};
        std::size_t index{};
    };
    struct Layout final {
        Rect face{};
        Rect tool_heading{};
        Rect document_heading{};
        // The first row of each column; rows follow at the row height.
        Rect tool_rows{};
        Rect document_rows{};
        double row_height{};
    };

    [[nodiscard]] Layout layout_for(Size surface) const;
    [[nodiscard]] Rect row_bounds(const Layout& placed, DockNavigatorList list, std::size_t index) const;
    // A row's label: the row less the label inset at each end.
    [[nodiscard]] Rect entry_label_bounds(Rect row) const;
    [[nodiscard]] std::optional<Row> row_at(Point local) const;
    [[nodiscard]] Row realized_row(std::size_t item) const;
    [[nodiscard]] AnatomyStateSet row_states(DockNavigatorList list, std::size_t index) const;
    [[nodiscard]] const std::vector<DockNavigatorEntry>& list_entries(DockNavigatorList list) const noexcept;
    [[nodiscard]] FontSpec row_font() const;
    [[nodiscard]] FontSpec heading_font() const;
    void set_hot(std::optional<Row> row);

    std::vector<DockNavigatorEntry> tools_{};
    std::vector<DockNavigatorEntry> documents_{};
    DockNavigatorList list_{DockNavigatorList::documents};
    std::optional<std::size_t> current_{};
    std::optional<Row> hot_{};
    Modifier release_modifier_{Modifier::none};
    Event<const DockNavigatorChoice&> chosen_{};
    Event<> cancelled_{};
};

} // namespace gui_forms
