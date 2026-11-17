#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/color_picker_capabilities.hpp"

#include <cstdint>
#include <span>
#include <vector>

#include <stxlib/unique.hpp>

namespace gui_forms {

// The parts of a ColorPicker a pointer press or the keyboard edits.
enum class ColorPickerPart : std::uint8_t { none, field, hue, alpha };

// A colour in hue, saturation and brightness (HSV): hue in degrees [0, 360),
// saturation and brightness in [0, 1].
struct GUI_FORMS_API ColorPickerHsv final {
    double hue{};
    double saturation{};
    double brightness{};

    friend bool operator==(const ColorPickerHsv& left, const ColorPickerHsv& right) noexcept {
        const bool equal = left.hue == right.hue && left.saturation == right.saturation &&
                           left.brightness == right.brightness;
        return equal;
    }
};

// The stock colour picker (the ImGui-class demonstrator's I2): a
// saturation-brightness field for the current hue, a vertical hue track,
// an alpha track (alpha_enabled) and a swatch of the value over a
// checkerboard. ColorDialog's hosts and ColorValueEditor's drop-down use it.
//
// Input: a primary press on the field, the hue track or the alpha track
// picks there and drags that part until release (the window holds the
// pointer for the press). Each move writes `value` (value_changed); the
// release raises value_committed. Keys, while focused: Left and Right move
// the saturation and Up and Down the brightness by 1/100 (Shift: 1/10);
// Page Up and Page Down the hue by 5 degrees (Shift: 30); Shift with Home
// or End sets the alpha to 0 or 255, and Home or End alone the alpha by
// 1/16 down or up, when alpha_enabled. Each key edit writes the value and
// raises value_committed.
//
// The picker keeps its HSV position apart from the value, so a grey keeps
// the hue it was picked at; a program's write of `value` recomputes it,
// keeping the hue when the written colour is grey and the saturation when
// it is black. Every area is a theme part. The colour areas (field,
// hue_track, alpha_track, swatch) are wells carrying their content colours
// (PartPaint::content_color; design language 8.5, owner decision W-7), which
// the default and familiar themes' content rules draw as gradients over a
// checkerboard, the same in every theme and, framed by high contrast's
// edge, in forced high contrast; a theme without those rules draws plain
// wells.
//
// Threads: the UI thread.
class GUI_FORMS_API ColorPicker final : public Control, public ColorPickerCapabilities<ColorPicker> {
public:
    explicit ColorPicker(StableId stable_id, Color initial_value = Color::rgba(255, 255, 255));

    [[nodiscard]] Event<Color>& value_changed() noexcept { return value_changed_; }
    // A pick finished: a drag's release or a key edit. Not raised by a
    // program's write of `value`.
    [[nodiscard]] Event<Color>& value_committed() noexcept { return value_committed_; }

    // The thumbs' position. set_hsv moves them and writes the value (alpha
    // kept); it throws std::invalid_argument, changing nothing, for a
    // non-finite component or one outside its range (hue 360 is taken as 0).
    [[nodiscard]] ColorPickerHsv hsv() const noexcept { return hsv_; }
    void set_hsv(ColorPickerHsv incoming);
    // The part a pointer press is dragging, or none.
    [[nodiscard]] ColorPickerPart dragged_part() const noexcept { return dragged_; }

    // HSV and RGB conversions the picker uses. hsv_of keeps `previous`'s hue
    // for a grey and its saturation for black.
    [[nodiscard]] static Color color_of(ColorPickerHsv hsv, std::uint8_t alpha) noexcept;
    [[nodiscard]] static ColorPickerHsv hsv_of(Color colour, ColorPickerHsv previous) noexcept;

    // The layout's fixed extents in logical units: the tracks' width, the gap
    // between parts, the swatch's height and the inset around them.
    static constexpr double track_width = 14.0;
    static constexpr double part_gap = 6.0;
    static constexpr double swatch_height = 18.0;
    static constexpr double inset = 2.0;

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry field;
        PartGeometry field_thumb;
        PartGeometry hue_track;
        PartGeometry hue_thumb;
        PartGeometry alpha_track;
        PartGeometry alpha_thumb;
        PartGeometry swatch;
        PartGeometry focus;
    };
    struct GUI_FORMS_API States final {
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

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_key_down(KeyEventArgs& arguments) override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The parts' rectangles in local coordinates for the current bounds; the
    // alpha track is empty while alpha_enabled is false.
    struct Layout final {
        Rect field{};
        Rect hue_track{};
        Rect alpha_track{};
        Rect swatch{};
    };
    [[nodiscard]] Layout layout_parts() const noexcept;
    [[nodiscard]] Rect field_thumb_bounds(const Layout& layout) const noexcept;
    [[nodiscard]] Rect hue_thumb_bounds(const Layout& layout) const noexcept;
    [[nodiscard]] Rect alpha_thumb_bounds(const Layout& layout) const noexcept;
    // The part under `local`, or none.
    [[nodiscard]] ColorPickerPart part_at(const Layout& layout, Point local) const noexcept;
    // Picks on `part` at `local` (clamped into the part).
    void pick(ColorPickerPart part, Point local);
    // Moves the thumbs to `next` with `alpha` and writes the value.
    void apply(ColorPickerHsv next, std::uint8_t alpha);
    // The field's and the hue strip's pixels, from HSV (see on_paint).
    void paint_hsv_areas(Painter& painter, const Layout& layout, const AnatomyStateSet& states);

    ColorPickerHsv hsv_{};
    ColorPickerPart dragged_{ColorPickerPart::none};
    bool focused_{};
    // Set while apply writes the value, so written keeps the HSV position.
    bool picking_{};
    // The saturation-brightness field's and the hue strip's pixels, at the
    // display scale they were drawn for (defined in color_picker.cpp, since
    // the rasters are shared with the paint recordings that show them).
    // Empty until the first paint makes it.
    struct HsvPixels;
    stxlib::Unique<HsvPixels> hsv_pixels_;
    Event<Color> value_changed_;
    Event<Color> value_committed_;
    // property_codemod begin ColorPicker (tools/property_families/color_picker.json; generated, do not edit)
public:
    // ColorPicker's property table: Control's entries, then its own.
    static constexpr std::size_t color_picker_property_count = Control::control_property_count + 2U;
    [[nodiscard]] static const std::array<PropertyEntry, color_picker_property_count>& color_picker_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct ColorPickerValueCapability<ColorPicker>;
    friend struct ColorPickerAlphaEnabledCapability<ColorPicker>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(value)& cell, Color& incoming);
    void written(const decltype(value)& cell);
    void prepare_write(const decltype(alpha_enabled)& cell, bool& incoming);
    void written(const decltype(alpha_enabled)& cell);
    // property_codemod end ColorPicker
};

} // namespace gui_forms
