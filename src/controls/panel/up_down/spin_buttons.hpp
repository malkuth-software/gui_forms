#pragma once

#include "gui_forms/anatomy.hpp"
#include "gui_forms/control.hpp"

#include <cstdint>
#include <string_view>

namespace gui_forms {

// The spin column of an up-down control (NumericUpDown, DomainUpDown): an
// upper and a lower actuator, each with an arrow glyph. The column takes the
// pointer; its owner paints the halves as its own parts (paint_spin_parts).
// Paint and the owning control's published anatomy both take their
// rectangles from spin_halves and spin_glyph, so they describe the same
// parts.
struct SpinHalves final {
    Rect upper{};
    Rect lower{};
};

// Splits `column` where its halves meet: the upper half takes the floor of
// half the height, the lower half the rest.
[[nodiscard]] SpinHalves spin_halves(Rect column) noexcept;

// The arrow glyph of one half: a square half as wide as the half's shorter
// side, centred in it, on whole units.
[[nodiscard]] Rect spin_glyph(Rect half) noexcept;

enum class SpinHalf : std::uint8_t { none, upper, lower };

class SpinButtons;

// The four parts an up-down control publishes for its spin column, in the
// owning control's coordinates: each half's actuator and its glyph, the glyph
// carrying its half's states.
struct SpinPartGeometry final {
    PartGeometry upper_arrow{};
    PartGeometry lower_arrow{};
    PartGeometry upper_glyph{};
    PartGeometry lower_glyph{};
};

// The published spin parts of `spin`, from its committed arranged bounds
// (its rectangle in the owning control) and the halves' current states.
[[nodiscard]] SpinPartGeometry spin_part_geometry(const SpinButtons& spin);

// The names under which an up-down control publishes its spin parts
// (NumericUpDown: increment_arrow, ...; DomainUpDown: up_arrow, ...).
struct SpinPartNames final {
    std::string_view upper_arrow{};
    std::string_view lower_arrow{};
    std::string_view upper_glyph{};
    std::string_view lower_glyph{};
};

// Paints `spin`'s halves as `owner`'s parts named `names`, in `owner`'s
// coordinates (its on_paint_overlay painter): each half's actuator, then its
// arrow glyph, in the half's states. The owner paints them, not the column,
// so that a theme's rules for the owner's parts
// (`numeric-up-down::part(increment-arrow)`) select them.
void paint_spin_parts(Painter& painter, const Control& owner, const SpinButtons& spin, const SpinPartNames& names);

class SpinButtons final : public Control {
public:
    explicit SpinButtons(StableId stable_id);
    // +1 when the upper half is pressed, -1 for the lower.
    [[nodiscard]] Event<int>& stepped() noexcept { return stepped_; }
    // The states one half shows: hot while the pointer is over it, pressed
    // while the primary button went down on it and is still down.
    [[nodiscard]] AnatomyStateSet half_states(SpinHalf half) const noexcept;
    void on_paint(Painter& painter, Rect local_damage) override;

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_wheel(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;

private:
    // The target phase in the routed form, from the mouse raisers.
    void handle_pointer(PointerEvent& event);
    [[nodiscard]] SpinHalf half_at(Point window_position) const;
    void show(SpinHalf hot, SpinHalf pressed);

    Event<int> stepped_;
    SpinHalf hot_{SpinHalf::none};
    SpinHalf pressed_{SpinHalf::none};
};

} // namespace gui_forms
