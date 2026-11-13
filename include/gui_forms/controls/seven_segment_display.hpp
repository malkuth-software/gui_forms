#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/seven_segment_display_capabilities.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

// One cell of a seven-segment reading: the lit segments, as bits from a
// (the top bar, bit 0) clockwise through f (the upper left, bit 5) to g
// (the middle bar, bit 6); whether its decimal point is lit; and whether it
// is a colon cell, which is half a digit wide and shows two dots instead.
struct GUI_FORMS_API SevenSegmentCell final {
    std::uint8_t segments{};
    bool point{};
    bool colon{};

    friend bool operator==(const SevenSegmentCell& left, const SevenSegmentCell& right) noexcept {
        return left.segments == right.segments && left.point == right.point && left.colon == right.colon;
    }
};

// The segments `character` lights (a Unicode scalar value): the digits, the
// hexadecimal letters and the other letters a seven-segment cell can form
// (in whichever case it can), '-', '_', '=', the degree sign (U+00B0) and
// space; zero for anything else.
[[nodiscard]] GUI_FORMS_API std::uint8_t seven_segment_pattern(char32_t character) noexcept;

// A segmented numeric readout (W-5b; the instruments package): a row of
// digit cells in which each character of `text` lights the bars of a
// seven-segment display, with decimal points and colons, as a meter, clock or
// instrument panel shows a reading. The reading is right-aligned in
// digit_count cells; with digit_count zero there are as many cells as the
// text needs. A reading longer than the cells keeps its last digits and
// reports overflowed(), as an LCD counter does.
//
// It takes no input or focus. To assistive technology it is static text,
// named by its accessible name, with the reading as its value.
//
// Theme: the window is the `plot` part (the instruments' field, drawn as a
// well); a lit bar is the `trace` part in the selected state and an unlit
// one, while ghost_segments is set, the `graticule` part, both of the
// progress family, so a theme styles them with `seven-segment-display::
// part(trace)` and `::part(graticule)`. The cells grow and shrink with the
// control's height, and shrink further to fit its width.
class GUI_FORMS_API SevenSegmentDisplay final : public Control, public SevenSegmentDisplayCapabilities<SevenSegmentDisplay> {
public:
    explicit SevenSegmentDisplay(StableId stable_id);

    // Raised after `text` changes, with the new reading.
    [[nodiscard]] Event<const std::string&>& text_changed() noexcept { return text_changed_; }
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

    // The cells shown now, left to right, padded with blank cells on the
    // left to digit_count digits; read from text and digit_count each call.
    [[nodiscard]] std::vector<SevenSegmentCell> cells() const;
    // Whether the reading has more digits than digit_count, so that its
    // first digits are not shown.
    [[nodiscard]] bool overflowed() const;

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `plot` is the
    // window, `trace` the box around the cells' lit bars and `graticule` the
    // box around the cells (present while ghost_segments is set).
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry plot;
        PartGeometry trace;
        PartGeometry graticule;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // Where the cells lie in a control of `size`: the cell height, the
    // digit width, the gap after a cell and the left edge of the first.
    struct CellMetrics final {
        double height{};
        double digit_width{};
        double colon_width{};
        double gap{};
        double thickness{};
        double left{};
        double top{};
    };
    [[nodiscard]] CellMetrics cell_metrics(Size size) const noexcept;
    // The cells' natural width at a cell height of `height`.
    [[nodiscard]] double natural_width(double height) const noexcept;
    // The reading split into cells, before digit_count pads or trims it.
    [[nodiscard]] std::vector<SevenSegmentCell> reading_cells() const;

    Event<const std::string&> text_changed_{};
    // property_codemod begin SevenSegmentDisplay (tools/property_families/seven_segment_display.json; generated, do not edit)
public:
    // SevenSegmentDisplay's property table: Control's entries, then its own.
    static constexpr std::size_t seven_segment_display_property_count = Control::control_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, seven_segment_display_property_count>& seven_segment_display_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct SevenSegmentDisplayTextCapability<SevenSegmentDisplay>;
    friend struct SevenSegmentDisplayDigitCountCapability<SevenSegmentDisplay>;
    friend struct SevenSegmentDisplayGhostSegmentsCapability<SevenSegmentDisplay>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(text)& cell, std::string& incoming);
    void written(const decltype(text)& cell);
    void prepare_write(const decltype(digit_count)& cell, std::uint32_t& incoming);
    void written(const decltype(digit_count)& cell);
    void prepare_write(const decltype(ghost_segments)& cell, bool& incoming);
    void written(const decltype(ghost_segments)& cell);
    // property_codemod end SevenSegmentDisplay
};

} // namespace gui_forms
