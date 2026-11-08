#pragma once

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/panel/object_view/object_view_values/object_view_values.hpp"
#include "gui_forms/scheduler.hpp"
#include "gui_forms/controls/panel/object_view/object_view_capabilities/object_view_capabilities.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace gui_forms {

enum class ObjectGlyph : std::uint8_t {
    folder,
    document,
    image,
    archive,
    audio,
    code,
};

enum class ObjectCellAvailability : std::uint8_t { available, unavailable, not_applicable };
enum class ObjectColumnAlignment : std::uint8_t { left, right };

struct ObjectDetailsColumn final {
    ObjectColumnId id{};
    std::string label{};
    double width{160.0};
    double minimum_width{40.0};
    double maximum_width{4096.0};
    ObjectColumnAlignment alignment{ObjectColumnAlignment::left};
    bool sortable{true};
};

struct ObjectDetailsCell final {
    ObjectColumnId column{};
    std::string text{};
    ObjectCellAvailability availability{ObjectCellAvailability::available};
};

struct ObjectDetailsPaintWork final {
    std::size_t rows{};
    std::size_t cells{};
    std::size_t text_preparations{};
};

struct ObjectViewItem final {
    std::string stable_id{};
    std::string name{};
    std::string secondary_text{};
    std::string description{};
    ObjectGlyph glyph{ObjectGlyph::document};
    bool enabled{true};
    std::string image_key{};
    std::vector<ObjectDetailsCell> cells{};
};

struct ObjectSelectionChange final {
    std::string previous_id{};
    std::string current_id{};
    std::vector<std::string> previous_ids{};
    std::vector<std::string> current_ids{};
};

struct ObjectContextRequest final {
    std::string stable_id{};
    Point screen_position{};
};

// A stable-ID virtual object collection with icon and details projections.
// It owns no per-item controls: paint, hit testing, keyboard spatial movement,
// and semantic realization operate on a bounded visible window.
class ObjectView final : public Panel, public ObjectViewCapabilities<ObjectView> {
public:
    explicit ObjectView(StableId stable_id);
    // Provisional development admission guard, not measured product capacity.
    static constexpr std::size_t maximum_details_text_bytes = 64U * 1024U * 1024U;

    [[nodiscard]] std::span<const ObjectViewItem> items() const noexcept {
        return items_;
    }
    // Maps a root-client pointer position back to the retained stable item.
    // This is presentation identity only; applications retain all domain and
    // transfer authority.
    [[nodiscard]] std::string_view item_id_at(Point absolute) const noexcept;
    void set_items(std::vector<ObjectViewItem> items);
    // UI-thread complete replacement. See notes:docs-notes/OBJECT_VIEW_DETAILS_DEVELOPMENT_001.md.
    // Validation/preparation failure preserves the prior model. Spans borrow
    // this control and expire at replacement/disposal or column mutation.
    void set_details_model(std::vector<ObjectDetailsColumn> columns,
                           std::vector<ObjectViewItem> items);
    // Publishes the caller's actual row order and accepted indicator together.
    // Invalid explicit sort rejects before commit; empty ID clears the indicator.
    void set_details_model(std::vector<ObjectDetailsColumn> columns,
                           std::vector<ObjectViewItem> items,
                           ObjectDetailsSort accepted_sort);
    [[nodiscard]] std::span<const ObjectDetailsColumn> details_columns() const noexcept {
        return details_columns_;
    }
    void set_details_column_width(const ObjectColumnId& column, double width);
    [[nodiscard]] Event<const ObjectDetailsSort&>& sort_requested() noexcept { return sort_requested_; }
    void set_horizontal_offset(double offset);
    [[nodiscard]] double horizontal_offset() const noexcept { return horizontal_offset_; }
    [[nodiscard]] ObjectDetailsPaintWork details_paint_work() const noexcept { return details_paint_work_; }
    [[nodiscard]] std::span<const std::string> selected_ids() const noexcept {
        return selected_ids_;
    }
    void set_selected_ids(std::vector<std::string> stable_ids,
                          std::string_view primary_id = {});
    void clear_selection();
    void select_all();
    [[nodiscard]] std::string_view selection_anchor_id() const noexcept {
        return selection_anchor_id_;
    }
    [[nodiscard]] std::string_view focused_id() const noexcept {
        return focused_id_;
    }
    [[nodiscard]] std::size_t top_row() const noexcept { return top_row_; }
    void set_top_row(std::size_t row);

    [[nodiscard]] Event<const ObjectSelectionChange&>& selection_changed() noexcept {
        return selection_changed_;
    }
    [[nodiscard]] Event<const std::string&>& item_activated() noexcept {
        return item_activated_;
    }
    [[nodiscard]] Event<const ObjectContextRequest&>& context_requested() noexcept {
        return context_requested_;
    }

    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_context_menu_requested(ContextMenuRequest& request) override;
    void on_text_input(TextInputEvent& event) override;
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
    // Stage T1 anatomy, as dot members.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry header;
    };
    [[nodiscard]] Parts parts() const;
    struct ItemParts final {
        PartGeometry cell;
        PartGeometry glyph;
        PartGeometry name;
        PartGeometry secondary;
        PartGeometry cell_focus;
    };
    struct ItemStates final {
        bool selected{};
        bool hot{};
        bool current{};
        bool disabled{};
    };
    struct Item final {
        std::size_t index{};
        std::string stable_id;
        ItemParts parts;
        ItemStates states;
    };
    // The items currently realized, in paint order (owner decision 5).
    [[nodiscard]] std::vector<Item> realized_items() const;
    // The details view's column headers are a second item system,
    // "column". Realized columns are those the viewport shows; in the
    // generic realized-item path they follow the realized items. A column's
    // identity is the view's stable ID, ".column.", and the column's id.
    struct ColumnParts final {
        PartGeometry column_header;
        PartGeometry column_label;
        PartGeometry sort_glyph;
        PartGeometry column_divider;
        PartGeometry column_grip;
        PartGeometry column_focus;
    };
    struct ColumnStates final {
        bool pressed{};
        bool captured{};
        bool current{};
        // Application-defined column states.
        bool sorted_ascending{};
        bool sorted_descending{};
    };
    struct Column final {
        // The column's position in the details model.
        std::size_t index{};
        std::string stable_id;
        ColumnParts parts;
        ColumnStates states;
    };
    [[nodiscard]] std::vector<Column> realized_columns() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_attached_to_window() override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] ItemSystemIndex publish_realized_item_system(std::size_t item) const override;
    void publish_realized_item_geometry(std::size_t item,
                                        std::span<PartGeometry> geometry) const override;

private:
    void replace_details_model(std::vector<ObjectDetailsColumn> columns,
                               std::vector<ObjectViewItem> items,
                               std::optional<ObjectDetailsSort> accepted_sort);
    struct ItemIdHash final {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(const std::string_view id) const noexcept {
            const std::size_t result = std::hash<std::string_view>{}(id);
            return result;
        }
    };
    using ItemIndices = std::unordered_map<std::string, std::size_t, ItemIdHash, std::equal_to<>>;
    using SelectionIds = std::unordered_set<std::string, ItemIdHash, std::equal_to<>>;
    struct DetailsTextCache final {
        std::size_t item{};
        std::size_t column{};
        double width{};
        // The elided text's measurement in the details font.
        Size measured{};
        bool header{};
        bool valid{};
        std::string text{};
    };
    [[nodiscard]] bool has_details_columns() const noexcept;
    [[nodiscard]] double header_height() const noexcept;
    [[nodiscard]] double total_column_width() const noexcept;
    [[nodiscard]] std::optional<std::size_t> column_index(const ObjectColumnId& id) const noexcept;
    [[nodiscard]] Rect column_bounds(std::size_t column) const noexcept;
    // Realized rows and realized (visible) details columns. `position`
    // counts visible columns; the result is the column's model index.
    [[nodiscard]] std::size_t realized_row_count() const noexcept;
    [[nodiscard]] std::size_t realized_column_count() const noexcept;
    [[nodiscard]] std::size_t realized_column_index(std::size_t position) const noexcept;
    [[nodiscard]] ColumnParts column_parts(std::size_t column) const;
    [[nodiscard]] AnatomyStateSet column_states(std::size_t column) const;
    void clamp_horizontal_offset() noexcept;
    void reveal_header();
    void cancel_header_interaction();
    void request_header_sort(std::size_t column);
    bool press_column_header(std::string_view id, SemanticAction action);
    void append_column_headers(std::vector<SemanticNode>& nodes) const;
    void append_row_cells(std::size_t index, SemanticNode& row_node) const;
    struct DetailsPointerContext final {
        Window* attached{}; // Borrowed only across synchronous input delivery.
        std::uint64_t revision{};
        Rect bounds{};
        double scale{};
        double offset{};
        double header{};
    };
    [[nodiscard]] bool details_pointer_context_valid(const DetailsPointerContext& context) const;
    [[nodiscard]] bool details_pointer(PointerEvent& event);
    [[nodiscard]] bool details_key(KeyEvent& event);
    void paint_details(Painter& painter);
    void clear_details_cache() noexcept;
    [[nodiscard]] const std::string& details_text(Painter& painter,
        std::size_t slot, std::size_t item, std::size_t column, bool header,
        std::string_view text, double width, FontSpec text_face);
    std::vector<ObjectDetailsColumn> details_columns_{};
    std::uint64_t details_revision_{};
    ItemIndices item_indices_{};
    SelectionIds selected_lookup_{};
    ObjectDetailsSort details_sort_{};
    Event<const ObjectDetailsSort&> sort_requested_{};
    double horizontal_offset_{};
    bool header_focused_{};
    std::size_t focused_column_{};
    std::optional<std::size_t> pressed_column_{};
    std::optional<std::size_t> resizing_column_{};
    double resize_start_x_{};
    double resize_start_width_{};
    std::vector<DetailsTextCache> details_cache_{};
    FontSpec details_cache_font_{};
    ObjectDetailsPaintWork details_paint_work_{};
    [[nodiscard]] std::optional<std::size_t> item_index(
        std::string_view stable_id) const noexcept;
    [[nodiscard]] std::size_t columns() const noexcept;
    [[nodiscard]] double row_height() const noexcept;
    [[nodiscard]] std::size_t visible_row_count() const noexcept;
    [[nodiscard]] std::size_t fully_visible_row_count() const noexcept;
    [[nodiscard]] std::size_t maximum_top_row() const noexcept;
    [[nodiscard]] Rect item_bounds(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<std::size_t> index_at(Point absolute) const noexcept;
    void ensure_visible(std::size_t index);
    void select_index(std::size_t index, bool activate,
                      Modifier modifiers = Modifier::none);
    void focus_index(std::size_t index);
    void apply_selection(std::vector<std::string> stable_ids,
                         std::string primary_id,
                         std::string anchor_id,
                         bool move_focus);
    [[nodiscard]] bool is_selected(std::string_view stable_id) const noexcept;
    [[nodiscard]] std::vector<std::string> range_selection(
        std::size_t target_index, bool preserve_existing) const;
    void type_select(std::string_view text);
    void image_list_content_changed(const ImageListChange&);
    [[nodiscard]] bool paint_item_image(
        Painter& painter, const ObjectViewItem& item,
        std::size_t index, bool selected, Rect destination_bounds);
    void paint_glyph(Painter& painter, Rect bounds, ObjectGlyph glyph,
                     bool enabled) const;
    void paint_focus_ring_part(Painter& painter, Rect bounds) const;

    std::vector<ObjectViewItem> items_{};
    std::string selected_id_{};
    std::vector<std::string> selected_ids_{};
    std::string focused_id_{};
    std::string selection_anchor_id_{};
    std::optional<std::size_t> hovered_index_{};
    std::optional<std::size_t> pressed_index_{};
    std::uint32_t pressed_click_count_{1U};
    PointerButton pressed_button_{PointerButton::none};
    ObjectViewMode view_mode_{ObjectViewMode::icons};
    std::size_t top_row_{};
    std::string type_prefix_{};
    std::chrono::steady_clock::time_point last_type_time_{};
    bool focused_{};
    SubscriptionToken image_list_changed_{};
    Event<const ObjectSelectionChange&> selection_changed_{};
    Event<const std::string&> item_activated_{};
    Event<const ObjectContextRequest&> context_requested_{};
    // property_codemod begin ObjectView (tools/property_families/object_view.json; generated, do not edit)
protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_font_changed();

private:
    friend struct ObjectViewViewModeCapability<ObjectView>;
    friend struct ObjectViewSelectedIdCapability<ObjectView>;
    friend struct ObjectViewSelectionModeCapability<ObjectView>;
    friend struct ObjectViewIconCellSizeCapability<ObjectView>;
    friend struct ObjectViewDetailsRowHeightCapability<ObjectView>;
    friend struct ObjectViewShowSecondaryTextCapability<ObjectView>;
    friend struct FontCapability<ObjectView>;
    friend struct ObjectViewImageListCapability<ObjectView>;
    friend struct ObjectViewDetailsSortCapability<ObjectView>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(view_mode)& cell, ObjectViewMode incoming);
    [[nodiscard]] ObjectViewMode read_view(const decltype(view_mode)& cell) const;
    void write_view(const decltype(selected_id)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(selected_id)& cell) const;
    void prepare_write(const decltype(selection_mode)& cell, ObjectSelectionMode& incoming);
    void written(const decltype(selection_mode)& cell);
    void prepare_write(const decltype(icon_cell_size)& cell, Size& incoming);
    void written(const decltype(icon_cell_size)& cell);
    void prepare_write(const decltype(details_row_height)& cell, double& incoming);
    void written(const decltype(details_row_height)& cell);
    void prepare_write(const decltype(show_secondary_text)& cell, bool& incoming);
    void written(const decltype(show_secondary_text)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    void prepare_write(const decltype(image_list)& cell, std::shared_ptr<ImageList>& incoming);
    void written(const decltype(image_list)& cell);
    void write_view(const decltype(details_sort)& cell, ObjectDetailsSort incoming);
    [[nodiscard]] ObjectDetailsSort read_view(const decltype(details_sort)& cell) const;
    // property_codemod end ObjectView
};

} // namespace gui_forms
