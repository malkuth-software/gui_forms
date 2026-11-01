#pragma once

#include "gui_forms/controls/range_control/range_control.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms {

enum class ScrollBarPart : std::uint8_t {
    none,
    decrement_button,
    increment_button,
    decrement_track,
    increment_track,
    thumb,
};

class ScrollBar : public RangeControl {
public:
    explicit ScrollBar(StableId stable_id,
                       Orientation orientation = Orientation::vertical);

    [[nodiscard]] double button_extent() const noexcept { return button_extent_; }
    void set_button_extent(double extent);
    [[nodiscard]] double minimum_thumb_extent() const noexcept {
        return minimum_thumb_extent_;
    }
    void set_minimum_thumb_extent(double extent);
    [[nodiscard]] FrameInterval initial_repeat_delay() const noexcept {
        return initial_repeat_delay_;
    }
    void set_initial_repeat_delay(FrameInterval delay);
    [[nodiscard]] FrameInterval repeat_interval() const noexcept {
        return repeat_interval_;
    }
    void set_repeat_interval(FrameInterval interval);

    [[nodiscard]] Rect decrement_button_bounds() const noexcept;
    [[nodiscard]] Rect increment_button_bounds() const noexcept;
    [[nodiscard]] Rect track_bounds() const noexcept;
    [[nodiscard]] Rect thumb_bounds() const noexcept;
    [[nodiscard]] ScrollBarPart part_at(Point local_point) const noexcept;

    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry decrement_arrow;
        PartGeometry increment_arrow;
        PartGeometry track;
        PartGeometry thumb;
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

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    void on_frame(FrameTime now) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action,
                            std::string_view value) override;

protected:
    void on_detached_from_window() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The press-and-hold repeat timer's target (ADR-016: a UI timer the bar
    // stops on release, not an active surface). Observes the bar.
    struct RepeatTick final {
        std::weak_ptr<ScrollBar> bar;
        void operator()(FrameTime now) const;
    };

    void paint_button(Painter& painter, Rect button, ScrollBarPart part,
                      bool incrementing, const BasicControlStyle& colors);
    [[nodiscard]] double axis_coordinate(Point local_point) const noexcept;
    [[nodiscard]] double value_from_thumb_coordinate(double coordinate) const noexcept;
    bool apply_part(ScrollBarPart part);
    void begin_repeat(ScrollBarPart part, Point pointer);
    void stop_interaction() noexcept;

    ScrollBarPart pressed_part_{ScrollBarPart::none};
    Point repeat_pointer_{};
    double thumb_drag_offset_{};
    double button_extent_{17.0};
    double minimum_thumb_extent_{18.0};
    FrameInterval initial_repeat_delay_{std::chrono::milliseconds(400)};
    FrameInterval repeat_interval_{std::chrono::milliseconds(60)};
    // Repeats the pressed part: first after initial_repeat_delay_, then
    // every repeat_interval_, until release. Each repeat's value change
    // damages the bar.
    FrameRequestToken repeat_frames_;
    bool focused_{};
    bool tracking_thumb_{};
};

} // namespace gui_forms
