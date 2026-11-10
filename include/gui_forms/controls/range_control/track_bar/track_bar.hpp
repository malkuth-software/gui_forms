#pragma once

#include "gui_forms/controls/range_control/range_control.hpp"
#include "gui_forms/controls/range_control/track_bar/track_bar_values/track_bar_values.hpp"
#include "gui_forms/controls/range_control/track_bar/track_bar_capabilities/track_bar_capabilities.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms {

class TrackBar : public RangeControl, public TrackBarCapabilities<TrackBar> {
public:
    explicit TrackBar(StableId stable_id);

    // Public part geometry in local coordinates (stage T1). The value maps
    // linearly across value_span_bounds(): minimum at its left (horizontal)
    // or bottom (vertical) edge. track_bounds() is the painted groove, which
    // shares the value span's extent along the axis.
    [[nodiscard]] Rect value_span_bounds() const noexcept;
    [[nodiscard]] Rect track_bounds() const noexcept;
    // The value portion of the groove; empty in the classic style.
    [[nodiscard]] Rect fill_bounds() const noexcept;
    [[nodiscard]] Rect thumb_bounds() const noexcept;
    // The band the tick marks occupy; empty when ticks are not drawn.
    [[nodiscard]] Rect ticks_bounds() const noexcept;

    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry track;
        PartGeometry fill;
        PartGeometry ticks;
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
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    [[nodiscard]] bool ticks_drawn() const noexcept;
    [[nodiscard]] double value_from_window_point(Point point) const noexcept;

    bool tracking_{};
    bool focused_{};
    // property_codemod begin TrackBar (tools/property_families/track_bar.json; generated, do not edit)
public:
    // TrackBar's property table: RangeControl's entries, then its own.
    static constexpr std::size_t track_bar_property_count = RangeControl::range_control_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, track_bar_property_count>& track_bar_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct TrackBarTickFrequencyCapability<TrackBar>;
    friend struct TrackBarShowTicksCapability<TrackBar>;
    friend struct TrackBarVisualStyleChoiceCapability<TrackBar>;
    friend struct TrackBarThumbExtentCapability<TrackBar>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(tick_frequency)& cell, double& incoming);
    void written(const decltype(tick_frequency)& cell);
    void prepare_write(const decltype(show_ticks)& cell, bool& incoming);
    void written(const decltype(show_ticks)& cell);
    void prepare_write(const decltype(visual_style)& cell, TrackBarVisualStyle& incoming);
    void written(const decltype(visual_style)& cell);
    void prepare_write(const decltype(thumb_extent)& cell, double& incoming);
    void written(const decltype(thumb_extent)& cell);
    // property_codemod end TrackBar
};

} // namespace gui_forms
