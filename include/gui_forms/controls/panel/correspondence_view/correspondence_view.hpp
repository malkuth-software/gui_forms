#pragma once

#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/scheduler.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gui_forms/controls/panel/object_view/object_view.hpp"
#include "gui_forms/controls/panel/correspondence_view/correspondence_view_capabilities/correspondence_view_capabilities.hpp"

namespace gui_forms {

// A factual, variable-height virtual record suitable for correspondence,
// evidence, activity, and inspection ledgers. Compact and expanded
// presentations share one stable logical item; expanding a row never creates a
// retained child control per model item.
struct CorrespondenceItem final {
    std::string stable_id;
    std::string title;
    std::string secondary_text;
    std::string metadata;
    std::string excerpt;
    std::string metric;
    std::string information_heading;
    std::vector<std::string> information_tags;
    std::string information_detail;
    ObjectGlyph glyph{ObjectGlyph::document};
    bool enabled{true};
    bool unavailable{};
    bool stale{};
    // Case-insensitive ASCII terms whose matching excerpt ranges receive a
    // restrained evidence mark. Text remains one factual string; terms do not
    // become commands or ranking tokens.
    std::vector<std::string> emphasis_terms;
};

struct CorrespondenceSelectionChange final {
    std::string previous_id;
    std::string current_id;
};

struct CorrespondencePinChange final {
    std::string previous_id;
    std::string current_id;
};

enum class CorrespondenceExpansionReason : std::uint8_t {
    programmatic,
    hover_intent,
    keyboard_focus,
    pin,
};

struct CorrespondenceExpansionChange final {
    std::string stable_id;
    bool expanded{};
    bool pinned{};
    CorrespondenceExpansionReason reason{
        CorrespondenceExpansionReason::programmatic};
};

// Variable-height virtual list with three independent transient states:
// keyboard focus, delayed pointer inspection, and one persistent pinned item.
// Sparse expanded identities keep position lookup logarithmic without a
// per-item control or per-item height object. Height changes preserve a stable
// anchor row and local offset, preventing the pointer-target oscillation common
// to naive hover expansion.
class CorrespondenceView final : public Panel, public CorrespondenceViewCapabilities<CorrespondenceView> {
public:
    explicit CorrespondenceView(StableId stable_id);

    [[nodiscard]] BorrowedSpan<CorrespondenceItem> items() const& noexcept { return detail::lend_view(items_); }
    BorrowedSpan<CorrespondenceItem> items() const&& = delete;
    void set_items(std::vector<CorrespondenceItem> items);
    [[nodiscard]] std::string_view focused_id() const& noexcept {
        return focused_id_;
    }
    std::string_view focused_id() const&& = delete;
    [[nodiscard]] std::string_view hovered_id() const& noexcept {
        return hovered_id_;
    }
    std::string_view hovered_id() const&& = delete;
    [[nodiscard]] bool expanded(std::string_view stable_id) const;
    [[nodiscard]] double content_height() const noexcept;
    [[nodiscard]] std::size_t realized_count() const noexcept;
    [[nodiscard]] std::optional<Rect> item_bounds(
        std::string_view stable_id) const noexcept;

    [[nodiscard]] Event<const CorrespondenceSelectionChange&>&
    selection_changed() noexcept { return selection_changed_; }
    [[nodiscard]] Event<const CorrespondencePinChange&>& pin_changed() noexcept {
        return pin_changed_;
    }
    [[nodiscard]] Event<const CorrespondenceExpansionChange&>&
    expansion_changed() noexcept { return expansion_changed_; }
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
    };
    [[nodiscard]] Parts parts() const;
    struct ItemParts final {
        PartGeometry card;
        PartGeometry status_rail;
        PartGeometry glyph;
        PartGeometry title;
        PartGeometry secondary;
        PartGeometry metric;
        PartGeometry evidence;
        PartGeometry card_focus;
    };
    struct ItemStates final {
        bool selected{};
        bool hot{};
        bool current{};
        bool open{};
        bool unavailable{};
        bool disabled{};
        bool pinned{};
        bool stale{};
    };
    struct Item final {
        std::size_t index{};
        std::string stable_id;
        ItemParts parts;
        ItemStates states;
    };
    // The items currently realized, in paint order (owner decision 5).
    [[nodiscard]] std::vector<Item> realized_items() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_detached_from_window() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void publish_realized_item_geometry(std::size_t item,
                                        std::span<PartGeometry> geometry) const override;

private:
    void emit_item_activated(const std::string& stable_id);
    struct HoverIntentCallback;
    struct ScrollAnchor final {
        std::string stable_id;
        std::size_t index{};
        double viewport_y{};
        bool valid{};
    };

    [[nodiscard]] std::optional<std::size_t> item_index(
        std::string_view stable_id) const noexcept;
    void retain_identity(std::string& stable_id) const;
    void insert_expanded_index(
        std::vector<std::size_t>& indices,
        std::string_view stable_id) const;
    static void add_semantic_text_child(
        SemanticNode& node, std::string suffix,
        std::string name, std::string value,
        SemanticRole role = SemanticRole::static_text);
    [[nodiscard]] std::vector<std::size_t> expanded_indices() const;
    [[nodiscard]] bool expanded_index(std::size_t index) const noexcept;
    [[nodiscard]] double scaled_compact_height() const noexcept;
    [[nodiscard]] double scaled_expanded_height() const noexcept;
    [[nodiscard]] double row_height(std::size_t index) const noexcept;
    [[nodiscard]] double row_top(std::size_t index) const noexcept;
    [[nodiscard]] double viewport_height() const noexcept;
    [[nodiscard]] double maximum_scroll_offset() const noexcept;
    [[nodiscard]] std::size_t first_visible_index() const noexcept;
    [[nodiscard]] std::pair<std::size_t, std::size_t> realized_range() const noexcept;
    [[nodiscard]] Rect item_bounds(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<std::size_t> index_at(Point absolute) const noexcept;
    [[nodiscard]] ScrollAnchor capture_anchor(std::size_t preferred) const noexcept;
    void restore_anchor(ScrollAnchor anchor);
    void clamp_scroll_offset() noexcept;
    void ensure_visible(std::size_t index);
    void focus_index(std::size_t index,
                     CorrespondenceExpansionReason reason);
    void set_hovered_index(std::optional<std::size_t> index);
    void schedule_hover_intent(std::size_t index);
    void clear_hover_intent(bool clear_expansion);
    void apply_hover_expansion(std::string stable_id);
    void emit_expansion_delta(std::string_view stable_id, bool before,
                              bool after,
                              CorrespondenceExpansionReason reason);
    void paint_glyph(Painter& painter, Rect bounds, ObjectGlyph glyph,
                     bool enabled) const;

    // The fonts of one card, from font_ and the effective text scale.
    struct CorrespondenceCardFonts final {
        FontSpec title{};
        FontSpec secondary{};
        FontSpec metric{};
        FontSpec evidence{};
        FontSpec evidence_bold{};
    };
    // Where one card's text and boxes go, in local coordinates: the one
    // geometry used by on_paint and publish_realized_item_geometry. Widths
    // are measured with the window's text metrics (estimated when
    // detached), so the title, state word and metric never overlap: the
    // metric is right-aligned, the state word sits left of it, and the
    // title and secondary line end before them (elided when painted).
    struct CorrespondenceCardLayout final {
        Rect card{};
        Rect status_rail{};
        Rect glyph{};
        Point title_origin{};
        double title_width{};
        Point secondary_origin{};
        double secondary_width{};
        Point metric_origin{};
        Rect metric_box{};
        // "PINNED", "OFFLINE" or "STALE"; empty when the card has none.
        std::string state_text;
        Point state_origin{};
        // The expanded evidence body (valid only when expanded is true).
        bool expanded{};
        double body_top{};
        double evidence_x{};
        bool wide{};
        double lane_width{};
        Rect excerpt_box{};
        // The narrow layout's information line under the excerpt box; not
        // drawn when the card is too short to hold it below the box.
        bool footer_shown{};
        Point footer_origin{};
    };
    [[nodiscard]] CorrespondenceCardFonts card_fonts() const;
    [[nodiscard]] CorrespondenceCardLayout card_layout(std::size_t index, const CorrespondenceCardFonts& fonts) const;
    [[nodiscard]] double measured_text_width(std::string_view text, FontSpec text_face) const;

    detail::LendingVector<CorrespondenceItem> items_;
    std::string selected_id_;
    std::string focused_id_;
    std::string hovered_id_;
    std::string hover_expanded_id_;
    std::string pending_hover_id_;
    std::string pinned_id_;
    std::optional<std::size_t> pressed_index_;
    PointerButton pressed_button_{PointerButton::none};
    std::uint32_t pressed_click_count_{1U};
    double compact_height_{45.0};
    double expanded_height_{126.0};
    double scroll_offset_{};
    FrameRequestToken hover_timer_;
    bool focused_{};
    Event<const CorrespondenceSelectionChange&> selection_changed_;
    Event<const CorrespondencePinChange&> pin_changed_;
    Event<const CorrespondenceExpansionChange&> expansion_changed_;
    Event<const std::string&> item_activated_;
    Event<const ObjectContextRequest&> context_requested_;
    // property_codemod begin CorrespondenceView (tools/property_families/correspondence_view.json; generated, do not edit)
protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_font_changed();

private:
    friend struct CorrespondenceViewSelectedIdCapability<CorrespondenceView>;
    friend struct CorrespondenceViewPinnedIdCapability<CorrespondenceView>;
    friend struct CorrespondenceViewCompactHeightCapability<CorrespondenceView>;
    friend struct CorrespondenceViewExpandedHeightCapability<CorrespondenceView>;
    friend struct CorrespondenceViewStatusRailWidthCapability<CorrespondenceView>;
    friend struct CorrespondenceViewHoverIntentDelayCapability<CorrespondenceView>;
    friend struct FontCapability<CorrespondenceView>;
    friend struct CorrespondenceViewScrollOffsetCapability<CorrespondenceView>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(selected_id)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(selected_id)& cell) const;
    void write_view(const decltype(pinned_id)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(pinned_id)& cell) const;
    void write_view(const decltype(compact_height)& cell, double incoming);
    [[nodiscard]] double read_view(const decltype(compact_height)& cell) const;
    void write_view(const decltype(expanded_height)& cell, double incoming);
    [[nodiscard]] double read_view(const decltype(expanded_height)& cell) const;
    void prepare_write(const decltype(status_rail_width)& cell, double& incoming);
    void written(const decltype(status_rail_width)& cell);
    void prepare_write(const decltype(hover_intent_delay)& cell, std::chrono::milliseconds& incoming);
    void written(const decltype(hover_intent_delay)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    void write_view(const decltype(scroll_offset)& cell, double incoming);
    [[nodiscard]] double read_view(const decltype(scroll_offset)& cell) const;
    // property_codemod end CorrespondenceView
};

} // namespace gui_forms
