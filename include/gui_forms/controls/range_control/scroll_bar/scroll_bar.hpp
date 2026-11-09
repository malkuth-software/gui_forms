#pragma once

#include "gui_forms/controls/range_control/range_control.hpp"
#include "gui_forms/controls/range_control/scroll_bar/scroll_bar_capabilities/scroll_bar_capabilities.hpp"

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

class ScrollBar : public RangeControl, public ScrollBarCapabilities<ScrollBar> {
public:
    explicit ScrollBar(StableId stable_id,
                       Orientation initial_orientation = Orientation::vertical);

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
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action,
                            std::string_view value) override;

protected:
    void on_detached_from_window() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The press-and-hold repeat timer's target (ADR-016: a UI timer the bar
    // stops on release). Observes the bar.
    struct RepeatTick final {
        std::weak_ptr<ScrollBar> bar;
        void operator()(FrameTime now) const;
    };

    // One repeat tick: steps the pressed part again, and stops the repeat
    // when the track no longer lies under the pointer or the step is refused.
    void repeat_step();

    // One arrow button: its actuator part, pressed while it is the pressed
    // part, and its arrow mark.
    void paint_button(Painter& painter, Rect button, ScrollBarPart part,
                      bool incrementing, AnatomyStateSet states);
    [[nodiscard]] double axis_coordinate(Point local_point) const noexcept;
    [[nodiscard]] double value_from_thumb_coordinate(double coordinate) const noexcept;
    bool apply_part(ScrollBarPart part);
    void begin_repeat(ScrollBarPart part, Point pointer);
    void stop_interaction() noexcept;

    ScrollBarPart pressed_part_{ScrollBarPart::none};
    Point repeat_pointer_{};
    double thumb_drag_offset_{};
    // Repeats the pressed part: first after initial_repeat_delay, then
    // every repeat_interval, until release. Each repeat's value change
    // damages the bar.
    FrameRequestToken repeat_frames_;
    bool focused_{};
    bool tracking_thumb_{};
    // property_codemod begin ScrollBar (tools/property_families/scroll_bar.json; generated, do not edit)
public:
    // ScrollBar's property table: RangeControl's entries, then its own.
    static constexpr std::size_t scroll_bar_property_count = RangeControl::range_control_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, scroll_bar_property_count>& scroll_bar_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    // The arrow buttons' extent on a bar `axis` long (the theme may draw none).
    [[nodiscard]] double arrow_extent(double axis) const noexcept;
    friend struct ScrollBarButtonExtentCapability<ScrollBar>;
    friend struct ScrollBarMinimumThumbExtentCapability<ScrollBar>;
    friend struct ScrollBarInitialRepeatDelayCapability<ScrollBar>;
    friend struct ScrollBarRepeatIntervalCapability<ScrollBar>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(button_extent)& cell, double& incoming);
    void written(const decltype(button_extent)& cell);
    void prepare_write(const decltype(minimum_thumb_extent)& cell, double& incoming);
    void written(const decltype(minimum_thumb_extent)& cell);
    void prepare_write(const decltype(initial_repeat_delay)& cell, FrameInterval& incoming);
    void written(const decltype(initial_repeat_delay)& cell);
    void prepare_write(const decltype(repeat_interval)& cell, FrameInterval& incoming);
    void written(const decltype(repeat_interval)& cell);
    // property_codemod end ScrollBar
};

} // namespace gui_forms
