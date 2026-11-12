#pragma once

#include "gui_forms/controls/breadcrumb_trail_values.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/breadcrumb_trail_capabilities.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// One retained location instrument. Logical segments are virtual semantic
// children; the exact-path TextBox is the sole retained child and occupies the
// same row only while editing.
class BreadcrumbTrail final : public Panel, public BreadcrumbTrailCapabilities<BreadcrumbTrail> {
public:
    static constexpr bool initialize_tree_after_construction = true;

    explicit BreadcrumbTrail(StableId stable_id,
                             std::string editor_stable_id = {});
    void initialize_control_tree();

    [[nodiscard]] std::span<const BreadcrumbSegment> segments() const& noexcept {
        return segments_;
    }
    std::span<const BreadcrumbSegment> segments() const&& = delete;
    void set_segments(std::vector<BreadcrumbSegment> segments);
    [[nodiscard]] std::span<const std::string> hidden_segment_ids() const& noexcept {
        return hidden_segment_ids_;
    }
    std::span<const std::string> hidden_segment_ids() const&& = delete;

    void begin_edit(std::string text, bool select_all = true);
    [[nodiscard]] std::shared_ptr<TextBox> editor() const noexcept {
        return editor_;
    }
    [[nodiscard]] std::string_view overflow_stable_id() const& noexcept {
        return overflow_stable_id_;
    }
    std::string_view overflow_stable_id() const&& = delete;
    [[nodiscard]] std::string_view edit_stable_id() const& noexcept {
        return edit_stable_id_;
    }
    std::string_view edit_stable_id() const&& = delete;

    [[nodiscard]] Event<const std::string&>& segment_activated() noexcept {
        return segment_activated_;
    }
    [[nodiscard]] Event<>& overflow_activated() noexcept {
        return overflow_activated_;
    }
    [[nodiscard]] Event<const std::string&>& edit_committed() noexcept {
        return edit_committed_;
    }
    [[nodiscard]] Event<const std::string&>& edit_started() noexcept {
        return edit_started_;
    }
    [[nodiscard]] Event<>& edit_cancelled() noexcept {
        return edit_cancelled_;
    }
    [[nodiscard]] Event<>& edit_completion_requested() noexcept {
        return edit_completion_requested_;
    }

    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key_preview(KeyEvent& event) override;
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
    // Stage T1 anatomy, as dot members.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
    };
    [[nodiscard]] Parts parts() const;
    struct ItemParts final {
        PartGeometry segment;
        PartGeometry segment_label;
        PartGeometry segment_cap;
        PartGeometry segment_focus;
    };
    struct ItemStates final {
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

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void publish_realized_item_geometry(std::size_t item,
                                        std::span<PartGeometry> geometry) const override;

private:
    enum class VisibleKind : std::uint8_t { segment, overflow, edit };
    struct VisibleItem final {
        VisibleKind kind{VisibleKind::segment};
        std::size_t segment_index{};
        Rect bounds{};
    };

    struct EditorCommitCallback final {
        std::weak_ptr<BreadcrumbTrail> target{};
        void operator()(const std::string& text) const;
    };
    struct EditorCancelCallback final {
        std::weak_ptr<BreadcrumbTrail> target{};
        void operator()() const;
    };

    void editor_committed(std::string text);
    void editor_cancelled();
    void rebuild_layout(double width, double height);
    [[nodiscard]] double natural_width(const BreadcrumbSegment& segment) const;
    [[nodiscard]] std::optional<std::size_t> segment_index(
        std::string_view stable_id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> visible_index_at(Point absolute) const noexcept;
    [[nodiscard]] std::string_view visible_stable_id(const VisibleItem& item) const noexcept;
    void activate_visible(std::size_t index);
    void normalize_active();
    void paint_raised(Painter& painter);
    [[nodiscard]] double raised_point(Rect bounds) const noexcept;

    std::vector<BreadcrumbSegment> segments_{};
    std::vector<VisibleItem> visible_items_{};
    std::vector<std::string> hidden_segment_ids_{};
    std::string editor_stable_id_{};
    std::string overflow_stable_id_{};
    std::string edit_stable_id_{};
    std::string active_id_{};
    std::shared_ptr<TextBox> editor_{};
    SubscriptionToken editor_commit_{};
    SubscriptionToken editor_cancel_{};
    std::optional<std::size_t> hovered_visible_{};
    std::optional<std::size_t> pressed_visible_{};
    bool focused_{};
    Event<const std::string&> segment_activated_{};
    Event<> overflow_activated_{};
    Event<const std::string&> edit_committed_{};
    Event<const std::string&> edit_started_{};
    Event<> edit_cancelled_{};
    Event<> edit_completion_requested_{};

    static constexpr double edge_overlap_ = 9.0;
    static constexpr double edit_width_ = 30.0;
    // property_codemod begin BreadcrumbTrail (tools/property_families/breadcrumb_trail.json; generated, do not edit)
protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_font_changed();

private:
    friend struct FontCapability<BreadcrumbTrail>;
    friend struct BreadcrumbTrailAppearanceCapability<BreadcrumbTrail>;
    friend struct BreadcrumbTrailActiveIdCapability<BreadcrumbTrail>;
    friend struct BreadcrumbTrailEditingCapability<BreadcrumbTrail>;
    friend struct BreadcrumbTrailTabCompletionAvailableCapability<BreadcrumbTrail>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    void prepare_write(const decltype(appearance)& cell, BreadcrumbAppearance& incoming);
    void written(const decltype(appearance)& cell);
    void write_view(const decltype(active_id)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(active_id)& cell) const;
    void prepare_write(const decltype(editing)& cell, bool& incoming);
    void written(const decltype(editing)& cell);
    void prepare_write(const decltype(tab_completion_available)& cell, bool& incoming);
    void written(const decltype(tab_completion_available)& cell);
    // property_codemod end BreadcrumbTrail
};

} // namespace gui_forms
