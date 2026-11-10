#pragma once

#include "gui_forms/controls/range_control/range_control.hpp"
#include "gui_forms/controls/instruments/dial/dial_capabilities/dial_capabilities.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace gui_forms {

// A rotary knob (W-5b; the instruments package): RangeControl's value set by
// turning. The scale runs 270 degrees clockwise from the lower left (the
// minimum) over twelve o'clock to the lower right (the maximum); the knob's
// pointer faces the value, and with show_arc the scale lights from the
// minimum to the value.
//
// Input follows TrackBar: a primary press turns the knob to face the
// pointer and a drag keeps it turning, clamped to the scale (a drag through
// the gap at the bottom stays at the end it came from); the wheel, the arrow
// keys (small_change), Page Up and Page Down (large_change), Home and End
// move the value; every change raises scroll and value_changed with
// RangeAction as TrackBar does. To assistive technology it is a slider with
// increment, decrement and set-value actions.
//
// Parts (design language: the dial roles, row 2.5, owner option (a)): the
// scale `track`, an `arc` of the progress family; with show_arc the lit
// scale `value_arc`, an arc in the selected state; scale marks
// `graticule`; the knob `thumb` (button family, round) with its `needle`
// facing the value; and the focus cue on the knob. The theme sets the arcs'
// thickness and the needle's width and shape (stroke-width, needle-shape).
class Dial : public RangeControl, public DialCapabilities<Dial> {
public:
    explicit Dial(StableId stable_id);

    // The knob's square in local coordinates, and the pointer's angle in
    // radians clockwise from twelve o'clock (-3/4 pi at the minimum).
    [[nodiscard]] Rect knob_bounds() const noexcept;
    [[nodiscard]] double pointer_angle() const noexcept;

    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts). The arcs
    // publish the square their circle is inscribed in, the needle the
    // square inside the knob it turns in; `value_arc` is absent without
    // show_arc or at the minimum.
    struct Parts final {
        PartGeometry region;
        PartGeometry track;
        PartGeometry value_arc;
        PartGeometry graticule;
        PartGeometry thumb;
        PartGeometry needle;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The scale and the knob in local coordinates: `arc` the square the
    // scale arc is inscribed in, `mark_radius` the circle the marks, `mark`
    // units across, are centred on, and `knob` the knob's side.
    struct Face final {
        Point centre{};
        Rect arc{};
        double mark_radius{0.0};
        double mark{0.0};
        double knob{0.0};
    };
    [[nodiscard]] Face face() const noexcept;
    [[nodiscard]] static Rect mark_at(Point centre, double radius, double ratio, double side) noexcept;
    // The square inside the knob the needle turns in.
    [[nodiscard]] Rect needle_bounds() const noexcept;
    // The value whose angle faces a window point; `previous` decides which
    // end a point in the gap at the bottom of the scale belongs to.
    [[nodiscard]] double value_from_window_point(Point point, double previous) const noexcept;
    [[nodiscard]] double ratio() const noexcept;

    bool tracking_{false};
    bool focused_{false};
    // property_codemod begin Dial (tools/property_families/dial.json; generated, do not edit)
public:
    // Dial's property table: RangeControl's entries, then its own.
    static constexpr std::size_t dial_property_count = RangeControl::range_control_property_count + 2U;
    [[nodiscard]] static const std::array<PropertyEntry, dial_property_count>& dial_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct DialTickFrequencyCapability<Dial>;
    friend struct DialShowArcCapability<Dial>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(tick_frequency)& cell, double& incoming);
    void written(const decltype(tick_frequency)& cell);
    void prepare_write(const decltype(show_arc)& cell, bool& incoming);
    void written(const decltype(show_arc)& cell);
    // property_codemod end Dial
};

} // namespace gui_forms
