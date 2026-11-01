#pragma once

#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

enum class ListSelectionMode : std::uint8_t {
    one,
    multiple_extended,
};

struct ListSelectionChange final {
    std::vector<std::size_t> previous;
    std::vector<std::size_t> current;
    std::optional<std::size_t> active_index;
};

class ListBox : public Panel {
public:
    explicit ListBox(StableId stable_id);

    [[nodiscard]] std::span<const std::string> items() const noexcept {
        return items_;
    }
    virtual void set_items(GUI_FORMS_LOCALIZABLE(items) std::vector<std::string> items);
    virtual void add_item(GUI_FORMS_LOCALIZABLE(items) std::string item);
    virtual void remove_item(std::size_t index);
    virtual void clear_items();
    void set_item_stable_ids(std::vector<std::string> stable_ids);
    [[nodiscard]] std::string item_stable_id(std::size_t index) const;
    [[nodiscard]] ListSelectionMode selection_mode() const noexcept {
        return selection_mode_;
    }
    void set_selection_mode(ListSelectionMode mode);
    [[nodiscard]] std::span<const std::size_t> selected_indices() const noexcept {
        return selected_;
    }
    [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept;
    void select_index(std::size_t index, bool extend = false, bool toggle = false);
    void clear_selection();
    [[nodiscard]] std::size_t top_index() const noexcept { return top_index_; }
    void set_top_index(std::size_t index);
    [[nodiscard]] double item_height() const noexcept { return item_height_; }
    void set_item_height(double height);
    [[nodiscard]] FontSpec font() const noexcept { return font_; }
    void set_font(FontSpec font);

    [[nodiscard]] Event<const ListSelectionChange&>& selection_changed() noexcept {
        return selection_changed_;
    }
    [[nodiscard]] Event<std::size_t>& item_activated() noexcept {
        return item_activated_;
    }

    void on_paint(Painter& painter, Rect local_damage) override;
    void arrange(Rect final_bounds) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id,
                                  SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(
        std::size_t item) const override;
    // Stage T1 anatomy, as dot members. Rows are an item system: each
    // realized row is a value record, not a control (owner decision 5).
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry well;
        PartGeometry content;
    };
    struct ItemParts final {
        PartGeometry row;
        PartGeometry label;
        PartGeometry row_focus;
    };
    struct ItemStates final {
        bool selected{};
        bool hot{};
        bool current{};
    };
    struct Item final {
        std::size_t index{};
        std::string stable_id;
        ItemParts parts;
        ItemStates states;
    };
    [[nodiscard]] Parts parts() const;
    // The rows currently realized, top to bottom; `index` is the row's model
    // position and `stable_id` its semantic virtual child's identity.
    [[nodiscard]] std::vector<Item> realized_items() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    [[nodiscard]] virtual double row_text_left() const noexcept;
    virtual void paint_row_adornment(Painter& painter, std::size_t index,
                                     Rect row_bounds, bool selected,
                                     bool focused) const;
    [[nodiscard]] std::optional<std::size_t> active_index_for_extension()
        const noexcept { return active_index_; }
    [[nodiscard]] bool focused_for_extension() const noexcept { return focused_; }
    [[nodiscard]] std::optional<std::size_t> index_at(
        Point absolute) const noexcept;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void publish_realized_item_geometry(std::size_t item,
                                        std::span<PartGeometry> geometry) const override;
    // The model position of a realized row.
    [[nodiscard]] std::size_t realized_item_index(std::size_t item) const noexcept;
    // A realized row's own parts in local coordinates, clipped to the list's
    // content.
    [[nodiscard]] ItemParts list_item_parts(std::size_t item) const;
    // The row's states the list itself tracks.
    [[nodiscard]] AnatomyStateSet list_item_states(std::size_t index) const;

private:
    void apply_selection(std::vector<std::size_t> selection,
                         std::optional<std::size_t> active);
    void ensure_visible(std::size_t index);
    [[nodiscard]] std::size_t visible_row_count() const noexcept;

    std::vector<std::string> items_;
    std::vector<std::string> item_stable_ids_;
    std::vector<std::size_t> selected_;
    std::optional<std::size_t> active_index_;
    std::optional<std::size_t> anchor_index_;
    std::optional<std::size_t> hovered_index_;
    std::size_t top_index_{};
    double item_height_{26.0};
    FontSpec font_{FontRole::content, 12.0, 400, false};
    ListSelectionMode selection_mode_{ListSelectionMode::one};
    bool focused_{};
    Event<const ListSelectionChange&> selection_changed_;
    Event<std::size_t> item_activated_;
};

} // namespace gui_forms
