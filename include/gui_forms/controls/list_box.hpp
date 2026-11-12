#pragma once

#include "gui_forms/controls/list_box_values.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/text_items_capability.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/controls/list_box_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

struct ListSelectionChange final {
    std::vector<std::size_t> previous;
    std::vector<std::size_t> current;
    std::optional<std::size_t> active_index;
};

class ListBox : public Panel, public TextItemsCapability<ListBox>, public ListBoxCapabilities<ListBox> {
public:
    explicit ListBox(StableId stable_id);

    // The items are the `items` cell (TextItemsCapability): edits keep the
    // selection on its items (remapped through each edit, or cleared from
    // an item an edit removes) and repaint only the shown rows they reach,
    // at the next flush.
    void set_item_stable_ids(std::vector<std::string> stable_ids);
    [[nodiscard]] std::string item_stable_id(std::size_t index) const;
    [[nodiscard]] std::span<const std::size_t> selected_indices() const& noexcept {
        return selected_;
    }
    std::span<const std::size_t> selected_indices() const&& = delete;
    [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept;
    void select_index(std::size_t index, bool extend = false, bool toggle = false);
    void clear_selection();

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
    // Called by the items' prepare_edit after ListBox's own checks, before
    // anything changes: a subclass readies its per-item state (reserving)
    // or refuses the edit by throwing.
    virtual void on_items_changing(const CollectionEdit& edit);
    // Called by the items' written hook after the items are stored and the
    // selection remapped, before the selection change is raised: a
    // subclass brings its per-item state to the edit. It must not throw;
    // what it allocates it reserved in on_items_changing.
    virtual void on_items_changed(const CollectionEdit& edit) noexcept;
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
    friend struct TextItemsCapability<ListBox>;

    // The items' hooks: require_mutable, UTF-8 and the remapped selection
    // first; then the stable identities, selection, window and damage.
    void prepare_edit(const decltype(items)& cell, const CollectionEdit& edit, std::span<std::string> incoming);
    void written(const decltype(items)& cell, const CollectionEdit& edit);
    // Takes the items' pending damage (in arrange, at the flush) and
    // repaints the shown rows it reaches; the whole list when the shown
    // window moved.
    void settle_items_damage(bool window_moved);
    // The `items` property (text_items_property.hpp).
    [[nodiscard]] BindingValue items_property_value() const;
    void set_items_property(const BindingValue& value);
    void reset_items_property();
    [[nodiscard]] bool should_serialize_items_property() const noexcept;
    void apply_selection(std::vector<std::size_t> selection,
                         std::optional<std::size_t> active);
    void ensure_visible(std::size_t index);
    [[nodiscard]] std::size_t visible_row_count() const noexcept;
    // Repaints one shown row; nothing for a row outside the shown window.
    void invalidate_row(std::size_t index);
    // The index of an authored stable item ID, or nullopt.
    [[nodiscard]] std::optional<std::size_t> index_of_item_stable_id(std::string_view stable_id);

    // Orders indices into item_stable_ids_ by the identity they name.
    struct StableIdOrder {
        const std::vector<std::string>* ids{};
        [[nodiscard]] bool operator()(std::size_t left, std::size_t right) const noexcept {
            return (*ids)[left] < (*ids)[right];
        }
        [[nodiscard]] bool operator()(std::size_t left, std::string_view right) const noexcept {
            return std::string_view((*ids)[left]) < right;
        }
    };

    std::vector<std::string> item_stable_ids_;
    // Indices into item_stable_ids_ sorted by identity, for the semantic
    // action lookup; emptied whenever the identities change and rebuilt on
    // the next lookup.
    std::vector<std::size_t> stable_id_order_;
    std::vector<std::size_t> selected_;
    // The selection an edit leaves, made by prepare_edit before the edit
    // is stored; after the written hook swaps it in, the selection before
    // the edit, for the change event. Reused across edits.
    std::vector<std::size_t> edit_selection_;
    // The replaced rows taken at a settle; reused across flushes.
    std::vector<std::uint32_t> settled_rows_;
    std::optional<std::size_t> active_index_;
    std::optional<std::size_t> anchor_index_;
    std::optional<std::size_t> hovered_index_;
    std::size_t top_index_{};
    bool focused_{};
    Event<const ListSelectionChange&> selection_changed_;
    Event<std::size_t> item_activated_;
    // property_codemod begin ListBox (tools/property_families/list_box.json; generated, do not edit)
public:
    // ListBox's property table: Panel's entries, then its own.
    static constexpr std::size_t list_box_property_count = Panel::panel_property_count + 2U;
    [[nodiscard]] static const std::array<PropertyEntry, list_box_property_count>& list_box_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_font_changed();

private:
    friend struct ListBoxSelectionModeCapability<ListBox>;
    friend struct ListBoxTopIndexCapability<ListBox>;
    friend struct ListBoxItemHeightCapability<ListBox>;
    friend struct FontCapability<ListBox>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(selection_mode)& cell, ListSelectionMode& incoming);
    void written(const decltype(selection_mode)& cell);
    void write_view(const decltype(top_index)& cell, std::size_t incoming);
    [[nodiscard]] std::size_t read_view(const decltype(top_index)& cell) const;
    void prepare_write(const decltype(item_height)& cell, double& incoming);
    void written(const decltype(item_height)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    // property_codemod end ListBox
};

} // namespace gui_forms
