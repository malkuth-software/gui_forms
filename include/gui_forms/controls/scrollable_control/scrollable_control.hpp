#pragma once

#include "gui_forms/controls/scrollable_control/scroll_properties/scroll_properties.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms {

class ScrollableControl : public Control {
public:
    static constexpr std::uint32_t scroll_state_auto_scrolling = 1U;
    static constexpr std::uint32_t scroll_state_hscroll_visible = 2U;
    static constexpr std::uint32_t scroll_state_vscroll_visible = 4U;
    static constexpr std::uint32_t scroll_state_user_has_scrolled = 8U;
    static constexpr std::uint32_t scroll_state_full_drag = 16U;

    explicit ScrollableControl(StableId stable_id);

    [[nodiscard]] bool auto_scroll() const noexcept { return auto_scroll_; }
    // With auto scroll, a size change can show or hide a scroll bar on
    // either axis, which moves the client area every child is placed in.
    [[nodiscard]] ResizeAxes layout_resize_response() const noexcept override;
    virtual void set_auto_scroll(bool enabled);
    [[nodiscard]] Size auto_scroll_margin() const noexcept {
        return auto_scroll_margin_;
    }
    void set_auto_scroll_margin(Size margin);
    void set_auto_scroll_margin(double x, double y);
    [[nodiscard]] Size auto_scroll_min_size() const noexcept {
        return auto_scroll_min_size_;
    }
    void set_auto_scroll_min_size(Size size);
    [[nodiscard]] Point auto_scroll_position() const noexcept {
        return {-scroll_position_.x, -scroll_position_.y};
    }
    void set_auto_scroll_position(Point position);
    [[nodiscard]] Point scroll_position() const noexcept {
        return scroll_position_;
    }
    [[nodiscard]] Rect display_rectangle() const noexcept override;
    [[nodiscard]] Rect viewport_rectangle() const noexcept {
        return effective_viewport_rectangle();
    }
    [[nodiscard]] const ScrollProperties& horizontal_scroll() const noexcept {
        return horizontal_scroll_;
    }
    [[nodiscard]] ScrollProperties& horizontal_scroll() noexcept {
        return horizontal_scroll_;
    }
    [[nodiscard]] const ScrollProperties& vertical_scroll() const noexcept {
        return vertical_scroll_;
    }
    [[nodiscard]] ScrollProperties& vertical_scroll() noexcept {
        return vertical_scroll_;
    }
    [[nodiscard]] bool hscroll() const noexcept {
        return horizontal_scroll_.visible_;
    }
    [[nodiscard]] bool vscroll() const noexcept {
        return vertical_scroll_.visible_;
    }
    void set_hscroll(bool visible);
    void set_vscroll(bool visible);
    [[nodiscard]] bool get_scroll_state(std::uint32_t bit) const noexcept;
    void set_scroll_state(std::uint32_t bit, bool value);
    [[nodiscard]] Event<ScrollEvent&>& scroll() noexcept { return scroll_event_; }
    [[nodiscard]] std::optional<ScrollEvent> last_scroll_event() const noexcept {
        return last_scroll_event_;
    }
    [[nodiscard]] std::uint64_t scroll_event_revision() const noexcept {
        return scroll_event_revision_;
    }
    [[nodiscard]] ScrollSnapshot scroll_snapshot() const noexcept;

    bool scroll_to(Point position,
                   ScrollEventType type = ScrollEventType::thumb_position,
                   bool notify = false);
    bool scroll_by(Point delta,
                   ScrollEventType horizontal_type =
                       ScrollEventType::small_increment,
                   ScrollEventType vertical_type =
                       ScrollEventType::small_increment,
                   bool notify = false);
    void scroll_control_into_view(const Control::Ptr& control);

    void arrange(Rect final_bounds) override;
    void on_pointer(PointerEvent& event) override;
    void on_pointer_bubble(PointerEvent& event) override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id,
                                  SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry viewport;
        PartGeometry horizontal_decrement_arrow;
        PartGeometry horizontal_track;
        PartGeometry horizontal_thumb;
        PartGeometry horizontal_increment_arrow;
        PartGeometry vertical_decrement_arrow;
        PartGeometry vertical_track;
        PartGeometry vertical_thumb;
        PartGeometry vertical_increment_arrow;
        PartGeometry scroll_corner;
        PartGeometry focus;
    };
    struct States final {
        bool normal{};
        bool pressed{};
        bool captured{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    // Called after the retained position changes, including silent API writes
    // and layout clamping. This does not publish a public Scroll event.
    virtual void on_scroll_position_changed() {}
    // Layout containers supply their measured content rather than authored child
    // coordinates. This keeps viewport, bars and hit-test clipping in one owner.
    void arrange_scroll_viewport(Size client_size, Size measured_content);
    virtual void adjust_scrollbars(bool display_scrollbars);
    [[nodiscard]] virtual Point scroll_to_control(const Control& control) const;
    void set_display_rect_location(Point location);
    void on_paint_overlay(Painter& painter, Rect local_damage) override;
    [[nodiscard]] Rect child_viewport_rectangle() const noexcept override {
        return effective_viewport_rectangle();
    }
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    // The region, viewport, scroll axes and focus as currently laid out.
    [[nodiscard]] Parts scrollable_parts() const;

private:
    friend class ScrollProperties;
    enum class ScrollPart : std::uint8_t {
        none,
        horizontal_first,
        horizontal_track_before,
        horizontal_thumb,
        horizontal_track_after,
        horizontal_last,
        vertical_first,
        vertical_track_before,
        vertical_thumb,
        vertical_track_after,
        vertical_last,
    };

    struct AxisGeometry final {
        Rect bar{};
        Rect first_button{};
        Rect last_button{};
        Rect track{};
        Rect thumb{};
    };

    void recompute_scroll_layout(Size client_size);
    [[nodiscard]] Rect effective_viewport_rectangle() const noexcept;
    [[nodiscard]] Size content_extent();
    [[nodiscard]] double maximum_offset(ScrollOrientation orientation) const noexcept;
    [[nodiscard]] AxisGeometry axis_geometry(ScrollOrientation orientation) const noexcept;
    [[nodiscard]] ScrollPart part_at(Point local) const noexcept;
    bool handle_wheel(PointerEvent& event);
    bool handle_scroll_pointer(PointerEvent& event);
    bool apply_axis_value(ScrollOrientation orientation, double value,
                          ScrollEventType type, bool notify);
    void notify_scroll(ScrollOrientation orientation, ScrollEventType type,
                       double old_value, double new_value);
    void paint_axis(Painter& painter, ScrollOrientation orientation,
                    const AxisGeometry& geometry) const;
    void append_semantic_axis(std::vector<SemanticNode>& nodes,
                              Rect absolute, ScrollOrientation orientation,
                              const ScrollProperties& axis) const;
    void axis_properties_changed(ScrollOrientation orientation,
                                 bool position_changed);
    static void validate_size(Size size, const char* message);
    static void validate_axis_value(double value, const char* message);
    [[nodiscard]] static double normalize_wheel_delta(double delta) noexcept;

    ScrollProperties horizontal_scroll_;
    ScrollProperties vertical_scroll_;
    Event<ScrollEvent&> scroll_event_;
    std::optional<ScrollEvent> last_scroll_event_;
    std::uint64_t scroll_event_revision_{};
    Size auto_scroll_margin_{};
    Size auto_scroll_min_size_{};
    Size content_extent_{};
    Point scroll_position_{};
    Rect viewport_rectangle_{};
    std::uint32_t scroll_state_{scroll_state_full_drag};
    std::optional<ScrollOrientation> dragging_orientation_;
    std::uint64_t dragging_pointer_id_{};
    Point dragging_origin_{};
    double dragging_start_value_{};
    double scrollbar_thickness_{16.0};
    double scrollbar_button_extent_{16.0};
    double minimum_thumb_extent_{18.0};
    bool auto_scroll_{};
    bool arranging_scroll_{};
};

} // namespace gui_forms
