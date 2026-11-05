#pragma once

#include "gui_forms/commands/gallery_command/gallery_command.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/panel/anchored_overlay_layer/anchored_overlay_layer.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/window.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// The two forms of a ribbon gallery (RIBBON_STUDY.md §5.2). In the ribbon a
// gallery shows a fixed number of rows with scroll buttons and a "more"
// button that opens the full gallery as an overlay over the ribbon (owner
// decision RB-23). The drop-down form is the full gallery, grouped by
// category with captions, as the content of an overlay.
enum class RibbonGalleryForm : std::uint8_t { in_ribbon, drop_down };

// One category: its items show together under the caption in the drop-down
// form.
struct RibbonGalleryCategory final {
    std::string stable_id{};
    std::string caption{};
};

// One item: an image with an optional label shown under it. `label` is also
// the item's accessible name. `category` names a category's stable ID, or is
// empty for an item shown before every category.
struct RibbonGalleryItem final {
    std::string stable_id{};
    std::string label{};
    ImageId image{};
    std::string category{};
    bool enabled{true};
};

// A gallery of the ribbon framework (RIBBON_STUDY.md §4.4, §5.2; owner
// decisions RB-2, RB-8, RB-13, RB-23): a grid of items in categories, in two
// forms (RibbonGalleryForm). Items are records, not controls: only the rows in
// view are realized, as the "gallery_item" and "gallery_category" item
// systems, and every part is painted through the by-role paint path. The
// selection follows the value of a GalleryCommand when one is set; a hover
// of 300 ms, or keyboard focus resting on an item as long, asks the command
// for a live preview, and leaving, Escape or closing the overlay cancels it.
class RibbonGallery final : public Control {
public:
    // How long the pointer or keyboard focus rests on an item before it is
    // previewed (owner decision RB-13, E1-1).
    static constexpr std::chrono::milliseconds preview_dwell{300};

    explicit RibbonGallery(StableId stable_id, RibbonGalleryForm form = RibbonGalleryForm::in_ribbon);
    ~RibbonGallery() override;

    [[nodiscard]] RibbonGalleryForm form() const noexcept { return form_; }

    // -- model -----------------------------------------------------------
    [[nodiscard]] std::span<const RibbonGalleryCategory> categories() const noexcept { return categories_; }
    [[nodiscard]] std::span<const RibbonGalleryItem> items() const noexcept { return items_; }
    // Replaces the categories and items. Items show without a category
    // first, then category by category in category order, each in the order
    // given. Throws std::invalid_argument for an empty or repeated ID, an
    // item naming an unknown category, or invalid UTF-8. The full gallery
    // the more button opens shows the same categories and items.
    void set_items(std::vector<RibbonGalleryCategory> categories, std::vector<RibbonGalleryItem> items);

    // The value command whose value the selection follows, which a click or
    // Enter commits and which receives previews. Without one the gallery
    // keeps its own selection and offers no preview.
    [[nodiscard]] std::shared_ptr<GalleryCommand> command() const noexcept { return command_; }
    void set_command(std::shared_ptr<GalleryCommand> command);
    // The selected item's ID: the command's value, or the gallery's own.
    [[nodiscard]] std::optional<std::string> selected_id() const;
    // Selects an item without committing (the command's set_value). Throws
    // std::invalid_argument for an ID the gallery does not have.
    void set_selected_id(std::optional<std::string> item_id);

    // -- layout ----------------------------------------------------------
    // Every item's image area, in logical units at text scale 1.
    [[nodiscard]] Size item_size() const noexcept { return item_size_; }
    void set_item_size(Size size);
    // Whether labels show under the images.
    [[nodiscard]] bool show_labels() const noexcept { return show_labels_; }
    void set_show_labels(bool show);
    // In-ribbon form: the rows shown (1 to 8).
    [[nodiscard]] std::size_t visible_rows() const noexcept { return visible_rows_; }
    void set_visible_rows(std::size_t rows);
    // In-ribbon form: the authored column range (owner decision RB-8: the
    // ribbon's scaling gives up one column at a time within it) and the
    // current column count, kept inside the range. Throws std::out_of_range
    // for an empty range, a bound of 0 or above 32, or a count outside it.
    [[nodiscard]] std::size_t minimum_columns() const noexcept { return minimum_columns_; }
    [[nodiscard]] std::size_t maximum_columns() const noexcept { return maximum_columns_; }
    void set_column_range(std::size_t minimum, std::size_t maximum);
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }
    void set_columns(std::size_t columns);
    // In-ribbon form: the width, in logical units at the current text scale,
    // the gallery measures at `columns` columns (the ribbon's scaling reads
    // it for each column step).
    [[nodiscard]] double width_for_columns(std::size_t columns) const noexcept;
    // Drop-down form: columns per row and the rows shown before it scrolls
    // (each 1 to 32). The full gallery the more button opens uses them.
    [[nodiscard]] std::size_t drop_down_columns() const noexcept { return drop_down_columns_; }
    [[nodiscard]] std::size_t drop_down_rows() const noexcept { return drop_down_rows_; }
    void set_drop_down_size(std::size_t columns, std::size_t rows);

    // -- scrolling -------------------------------------------------------
    // The first line shown: an item row, or in the drop-down form also a
    // category caption.
    [[nodiscard]] std::size_t first_visible_line() const noexcept { return first_line_; }
    [[nodiscard]] std::size_t line_count() const noexcept { return lines_.size(); }
    void scroll_to_line(std::size_t line);
    [[nodiscard]] bool can_scroll_up() const noexcept { return first_line_ > 0U; }
    [[nodiscard]] bool can_scroll_down() const noexcept;

    // -- overlays ----------------------------------------------------------
    // Drop-down form: shows this gallery in an overlay below `anchor` (a
    // drop-down or split button, or an in-ribbon gallery), with keyboard focus
    // in it. Returns false for the in-ribbon form, when already open, or when
    // `anchor` is not attached to a window.
    bool open_overlay(const Control::Ptr& anchor);
    // Cancels any preview, then closes the overlay and returns focus.
    void close_overlay();
    [[nodiscard]] bool overlay_open() const noexcept { return overlay_open_; }
    // In-ribbon form: opens the full gallery (the more button). Returns false
    // for the drop-down form or when it cannot open.
    bool show_more();
    // In-ribbon form: the drop-down gallery the more button opens, made on
    // first use; null before.
    [[nodiscard]] std::shared_ptr<RibbonGallery> more_gallery() const noexcept { return more_gallery_; }

    // -- focus -----------------------------------------------------------
    // The item keyboard focus is on, or none.
    [[nodiscard]] std::optional<std::string> focused_id() const;

    // -- events ----------------------------------------------------------
    [[nodiscard]] Event<const std::optional<std::string>&>& selection_changed() noexcept {
        return selection_changed_;
    }
    [[nodiscard]] Event<bool>& overlay_open_changed() noexcept { return overlay_open_changed_; }

    // -- Control ---------------------------------------------------------
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
        PartGeometry well;
        PartGeometry item_viewport;
        PartGeometry scroll_up;
        PartGeometry scroll_up_glyph;
        PartGeometry scroll_down;
        PartGeometry scroll_down_glyph;
        PartGeometry more_button;
        PartGeometry more_glyph;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

    // The "gallery_item" item system: one realized item per item in view.
    struct ItemParts final {
        PartGeometry item_face;
        PartGeometry item_selection;
        PartGeometry item_icon;
        PartGeometry item_label;
        PartGeometry item_focus;
    };
    struct ItemStates final {
        bool hot{};
        bool pressed{};
        bool selected{};
        bool current{};
        bool disabled{};
        bool focus_visible{};
    };
    struct Item final {
        std::size_t index{};
        std::string stable_id;
        ItemParts parts;
        ItemStates states;
    };
    [[nodiscard]] std::vector<Item> realized_items() const;

    // The "gallery_category" item system: one realized item per caption in
    // view (drop-down form only).
    struct CategoryParts final {
        PartGeometry category_caption;
        PartGeometry category_divider;
    };
    struct Category final {
        std::size_t index{};
        std::string stable_id;
        CategoryParts parts;
    };
    [[nodiscard]] std::vector<Category> realized_categories() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] ItemSystemIndex publish_realized_item_system(std::size_t item) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;

private:
    // One line of the grid: a category caption, or a row of items. `first`
    // and `count` index the display order (order_) for an item row.
    enum class LineKind : std::uint8_t { caption, items };
    struct Line final {
        LineKind kind{LineKind::items};
        std::size_t category{};
        std::size_t first{};
        std::size_t count{};
        // Top in logical units at the current scale, from the first line.
        double top{};
        double height{};
    };
    // What a local point is over.
    enum class HitZone : std::uint8_t { none, item, scroll_up, scroll_down, more };
    struct HitResult final {
        HitZone zone{HitZone::none};
        // A display position (into order_) for an item.
        std::size_t position{};
    };
    struct Geometry final {
        Rect client{};
        Rect viewport{};
        Rect scroll_up{};
        Rect scroll_down{};
        Rect more{};
        Size cell{};
        double scale{1.0};
    };
    struct DwellCallback;

    [[nodiscard]] std::size_t active_columns() const noexcept;
    void rebuild_lines();
    void model_changed();
    [[nodiscard]] Geometry geometry() const;
    [[nodiscard]] std::size_t visible_line_end(const Geometry& layout) const noexcept;
    [[nodiscard]] std::size_t lines_per_page(const Geometry& layout) const noexcept;
    [[nodiscard]] Rect cell_bounds(std::size_t position, const Geometry& layout) const noexcept;
    [[nodiscard]] std::optional<std::size_t> line_of(std::size_t position) const noexcept;
    [[nodiscard]] ItemParts item_parts(std::size_t position, const Geometry& layout) const;
    [[nodiscard]] CategoryParts category_parts(std::size_t line, const Geometry& layout) const;
    [[nodiscard]] AnatomyStateSet item_states(std::size_t position) const;
    [[nodiscard]] Parts gallery_parts(const Geometry& layout) const;
    [[nodiscard]] HitResult hit_test(Point local) const;
    // Realized items in view: item positions, then caption lines.
    struct Realized final {
        std::vector<std::size_t> positions;
        std::vector<std::size_t> captions;
    };
    [[nodiscard]] Realized realized(const Geometry& layout) const;

    [[nodiscard]] std::optional<std::size_t> position_of(std::string_view item_id) const noexcept;
    [[nodiscard]] bool item_enabled(std::size_t position) const noexcept;
    void commit(std::size_t position);
    void focus_position(std::size_t position);
    void scroll_into_view(std::size_t position);
    void start_dwell(std::size_t position);
    void stop_dwell() noexcept;
    void dwell_elapsed(const std::string& item_id);
    void cancel_preview();
    void on_command_value(const std::optional<std::string>& value);
    void on_command_state(const CommandState& state);
    void on_layer_dismissed(OverlayDismissReason reason);
    void on_overlay_revoked();
    void release_layer_content() noexcept;
    void on_more_open_changed(bool open);
    [[nodiscard]] bool navigate(std::uint32_t key);

    RibbonGalleryForm form_{RibbonGalleryForm::in_ribbon};
    std::vector<RibbonGalleryCategory> categories_{};
    std::vector<RibbonGalleryItem> items_{};
    // Display order: indexes into items_.
    std::vector<std::size_t> order_{};
    std::vector<Line> lines_{};
    std::shared_ptr<GalleryCommand> command_{};
    SubscriptionToken command_value_{};
    SubscriptionToken command_state_{};
    std::optional<std::string> own_selection_{};

    Size item_size_{48.0, 48.0};
    bool show_labels_{};
    std::size_t visible_rows_{3U};
    std::size_t minimum_columns_{1U};
    std::size_t maximum_columns_{8U};
    std::size_t columns_{8U};
    std::size_t drop_down_columns_{6U};
    std::size_t drop_down_rows_{5U};
    std::size_t first_line_{};

    // Interaction state, as display positions.
    std::optional<std::size_t> hot_{};
    std::optional<std::size_t> pressed_{};
    std::optional<std::size_t> focused_{};
    HitZone hot_zone_{HitZone::none};
    HitZone pressed_zone_{HitZone::none};
    bool has_focus_{};
    // The item a dwell timer is running for, and its timer.
    std::string dwell_item_{};
    FrameRequestToken dwell_timer_{};

    // Overlay state of a drop-down gallery: the layer it sits in while open
    // (owned by the gallery; the layer holds the gallery as its content only
    // while open), its overlay lease and focus scope.
    std::shared_ptr<AnchoredOverlayLayer> layer_{};
    OverlayToken overlay_{};
    FocusScopeId scope_{};
    SubscriptionToken layer_dismissed_{};
    SubscriptionToken overlay_revoked_{};
    bool overlay_open_{};
    // The full gallery an in-ribbon gallery's more button opens.
    std::shared_ptr<RibbonGallery> more_gallery_{};
    SubscriptionToken more_open_changed_{};

    Event<const std::optional<std::string>&> selection_changed_{};
    Event<bool> overlay_open_changed_{};
};

} // namespace gui_forms
