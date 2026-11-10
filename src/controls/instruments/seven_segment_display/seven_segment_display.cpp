#include "gui_forms/controls/instruments/seven_segment_display/seven_segment_display.hpp"

#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

// The segment bits, from a (top) clockwise to f (upper left), then g.
constexpr std::uint8_t segment_a = 1U << 0U;
constexpr std::uint8_t segment_b = 1U << 1U;
constexpr std::uint8_t segment_c = 1U << 2U;
constexpr std::uint8_t segment_d = 1U << 3U;
constexpr std::uint8_t segment_e = 1U << 4U;
constexpr std::uint8_t segment_f = 1U << 5U;
constexpr std::uint8_t segment_g = 1U << 6U;
constexpr std::size_t segments_per_cell = 7U;

// A character and the segments it lights.
struct SegmentGlyph final {
    char32_t character{};
    std::uint8_t segments{};
};

// Sorted by character, so the lookup is an exact-membership search.
constexpr std::array<SegmentGlyph, 52> segment_glyphs{{
    {U' ', 0x00U}, {U'-', 0x40U}, {U'0', 0x3FU}, {U'1', 0x06U}, {U'2', 0x5BU}, {U'3', 0x4FU},
    {U'4', 0x66U}, {U'5', 0x6DU}, {U'6', 0x7DU}, {U'7', 0x07U}, {U'8', 0x7FU}, {U'9', 0x6FU},
    {U'=', 0x48U}, {U'A', 0x77U}, {U'B', 0x7CU}, {U'C', 0x39U}, {U'D', 0x5EU}, {U'E', 0x79U},
    {U'F', 0x71U}, {U'G', 0x3DU}, {U'H', 0x76U}, {U'I', 0x30U}, {U'J', 0x1EU}, {U'L', 0x38U},
    {U'N', 0x54U}, {U'O', 0x3FU}, {U'P', 0x73U}, {U'Q', 0x67U}, {U'R', 0x50U}, {U'S', 0x6DU},
    {U'T', 0x78U}, {U'U', 0x3EU}, {U'Y', 0x6EU}, {U'_', 0x08U}, {U'a', 0x77U}, {U'b', 0x7CU},
    {U'c', 0x58U}, {U'd', 0x5EU}, {U'e', 0x79U}, {U'f', 0x71U}, {U'g', 0x6FU}, {U'h', 0x74U},
    {U'i', 0x10U}, {U'j', 0x1EU}, {U'l', 0x30U}, {U'n', 0x54U}, {U'o', 0x5CU}, {U'r', 0x50U},
    {U't', 0x78U}, {U'u', 0x1CU}, {U'y', 0x6EU}, {U'°', 0x63U},
}};

[[nodiscard]] bool glyph_before(const SegmentGlyph& glyph, char32_t character) noexcept {
    return glyph.character < character;
}

// The cell's natural proportions, in cell heights: a digit is half as wide
// as it is tall, a colon a quarter; the gap after a cell leaves room for
// the decimal point; a bar is a tenth of the height thick; the window's
// padding is an eighth of the control's height.
constexpr double digit_aspect = 0.5;
constexpr double colon_aspect = 0.25;
constexpr double gap_share = 0.16;
constexpr double thickness_share = 0.1;
constexpr double padding_share = 0.125;
constexpr double natural_height = 32.0;

constexpr std::array<PartField<SevenSegmentDisplay::Parts>, 4> seven_segment_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &SevenSegmentDisplay::Parts::region},
    {.declaration = {.name = "plot", .role = PartRole::plot, .composed_from = {}},
     .member = &SevenSegmentDisplay::Parts::plot},
    {.declaration = {.name = "trace", .role = PartRole::trace, .composed_from = {}},
     .member = &SevenSegmentDisplay::Parts::trace},
    {.declaration = {.name = "graticule", .role = PartRole::graticule, .composed_from = {}},
     .member = &SevenSegmentDisplay::Parts::graticule},
}};

constexpr std::array<PartDeclaration, 4> seven_segment_parts = part_declarations(seven_segment_part_fields);

constexpr std::array<AnatomyState, 4> seven_segment_states{
    AnatomyState::normal, AnatomyState::selected, AnatomyState::enabled, AnatomyState::disabled,
};

constexpr ControlAnatomy seven_segment_anatomy{
    .control_id = "seven_segment_display",
    .parts = seven_segment_parts,
    .states = seven_segment_states,
    .custom_states = {},
};

// The bars of a digit cell at `cell` with bars `thickness` thick, in the
// order of the segment bits (a to g). Each bar stops short of its
// neighbours by a fifth of a bar, so the cell reads as separate bars.
std::array<Rect, segments_per_cell> segment_bars(Rect cell, double thickness) noexcept {
    const double inset = thickness * 0.2;
    const double half = cell.height * 0.5;
    const double horizontal_x = cell.x + thickness * 0.5 + inset;
    const double horizontal_width = std::max(0.0, cell.width - thickness - inset * 2.0);
    const double vertical_height = std::max(0.0, half - thickness * 0.5 - inset * 2.0);
    const double upper_y = cell.y + thickness * 0.5 + inset;
    const double lower_y = cell.y + half + inset;
    const double right_x = cell.x + cell.width - thickness;
    std::array<Rect, segments_per_cell> bars{};
    bars[0] = Rect{horizontal_x, cell.y, horizontal_width, thickness};
    bars[1] = Rect{right_x, upper_y, thickness, vertical_height};
    bars[2] = Rect{right_x, lower_y, thickness, vertical_height};
    bars[3] = Rect{horizontal_x, cell.y + cell.height - thickness, horizontal_width, thickness};
    bars[4] = Rect{cell.x, lower_y, thickness, vertical_height};
    bars[5] = Rect{cell.x, upper_y, thickness, vertical_height};
    bars[6] = Rect{horizontal_x, cell.y + half - thickness * 0.5, horizontal_width, thickness};
    return bars;
}

// The union of two rectangles, where an empty one adds nothing.
Rect joined(Rect left, Rect right) noexcept {
    if (left.empty()) return right;
    if (right.empty()) return left;
    const double x = std::min(left.x, right.x);
    const double y = std::min(left.y, right.y);
    const double right_edge = std::max(left.x + left.width, right.x + right.width);
    const double bottom = std::max(left.y + left.height, right.y + right.height);
    const Rect result{x, y, right_edge - x, bottom - y};
    return result;
}

// Reads the scalar value that starts at `index` of `text` (valid UTF-8) and
// moves `index` past it.
char32_t next_scalar(std::string_view text, std::size_t& index) noexcept {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    std::size_t length = 1U;
    char32_t scalar = lead;
    if (lead >= 0xF0U) {
        length = 4U;
        scalar = lead & 0x07U;
    } else if (lead >= 0xE0U) {
        length = 3U;
        scalar = lead & 0x0FU;
    } else if (lead >= 0xC0U) {
        length = 2U;
        scalar = lead & 0x1FU;
    }
    const std::size_t end = std::min(text.size(), index + length);
    for (std::size_t position = index + 1U; position < end; ++position) {
        const unsigned char continuation = static_cast<unsigned char>(text[position]);
        scalar = (scalar << 6U) | (continuation & 0x3FU);
    }
    index = end;
    return scalar;
}

} // namespace

std::uint8_t seven_segment_pattern(char32_t character) noexcept {
    const SegmentGlyph* const first = segment_glyphs.data();
    const SegmentGlyph* const last = first + segment_glyphs.size();
    const SegmentGlyph* const found = std::lower_bound(first, last, character, glyph_before);
    if (found == last || (*found).character != character) return 0U;
    return (*found).segments;
}

SevenSegmentDisplay::SevenSegmentDisplay(StableId stable_id) : Control(std::move(stable_id)) {}

std::vector<SevenSegmentCell> SevenSegmentDisplay::reading_cells() const {
    const std::string& reading = text.read();
    std::vector<SevenSegmentCell> split;
    split.reserve(reading.size());
    std::size_t index = 0U;
    while (index < reading.size()) {
        const char32_t scalar = next_scalar(reading, index);
        if (scalar == U'.' || scalar == U',') {
            // A point lights the previous digit's; a second point, or one
            // with no digit before it, takes a blank cell of its own.
            const bool joins = !split.empty() && !split.back().point && !split.back().colon;
            if (joins) {
                split.back().point = true;
            } else {
                split.push_back(SevenSegmentCell{.segments = 0U, .point = true, .colon = false});
            }
            continue;
        }
        if (scalar == U':') {
            split.push_back(SevenSegmentCell{.segments = 0U, .point = false, .colon = true});
            continue;
        }
        split.push_back(SevenSegmentCell{.segments = seven_segment_pattern(scalar), .point = false, .colon = false});
    }
    return split;
}

std::vector<SevenSegmentCell> SevenSegmentDisplay::cells() const {
    std::vector<SevenSegmentCell> split = reading_cells();
    const std::size_t wanted = digit_count.read();
    if (wanted == 0U) return split;
    std::size_t digits = 0U;
    for (const SevenSegmentCell& cell : split) {
        if (!cell.colon) ++digits;
    }
    // Too many digits: the first ones (and any colon among them) go.
    std::size_t first = 0U;
    while (digits > wanted && first < split.size()) {
        if (!split[first].colon) --digits;
        ++first;
    }
    std::vector<SevenSegmentCell> shown;
    shown.reserve(wanted + split.size() - first);
    const SevenSegmentCell blank{};
    for (std::size_t pad = digits; pad < wanted; ++pad) {
        shown.push_back(blank);
    }
    for (std::size_t index = first; index < split.size(); ++index) {
        shown.push_back(split[index]);
    }
    return shown;
}

bool SevenSegmentDisplay::overflowed() const {
    const std::size_t wanted = digit_count.read();
    if (wanted == 0U) return false;
    const std::vector<SevenSegmentCell> split = reading_cells();
    std::size_t digits = 0U;
    for (const SevenSegmentCell& cell : split) {
        if (!cell.colon) ++digits;
    }
    const bool over = digits > wanted;
    return over;
}

double SevenSegmentDisplay::natural_width(double height) const noexcept {
    const std::vector<SevenSegmentCell> shown = cells();
    double width = 0.0;
    for (const SevenSegmentCell& cell : shown) {
        const double cell_width = cell.colon ? height * colon_aspect : height * digit_aspect;
        width += cell_width + height * gap_share;
    }
    return width;
}

SevenSegmentDisplay::CellMetrics SevenSegmentDisplay::cell_metrics(Size size) const noexcept {
    CellMetrics metrics{};
    const double padding = std::floor(size.height * padding_share);
    double height = std::max(0.0, size.height - padding * 2.0);
    const double wanted = natural_width(height);
    const double room = std::max(0.0, size.width - padding * 2.0);
    // Too wide: every dimension shrinks by the same factor, so the cells
    // keep their proportions.
    if (wanted > room && wanted > 0.0) height *= room / wanted;
    metrics.height = height;
    metrics.digit_width = height * digit_aspect;
    metrics.colon_width = height * colon_aspect;
    metrics.gap = height * gap_share;
    metrics.thickness = std::max(1.0, height * thickness_share);
    const double width = natural_width(height);
    metrics.left = std::floor((size.width - width) * 0.5 + metrics.gap * 0.5);
    metrics.top = std::floor((size.height - height) * 0.5);
    return metrics;
}

Size SevenSegmentDisplay::measure(Size available) {
    const Rect requested = requested_bounds();
    const double height = requested.height > 0.0 ? requested.height : natural_height;
    const double padding = std::floor(height * padding_share);
    const double cell_height = std::max(0.0, height - padding * 2.0);
    const double width = requested.width > 0.0 ? requested.width : natural_width(cell_height) + padding * 2.0;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

void SevenSegmentDisplay::on_paint(Painter& painter, Rect) {
    const Rect client = client_rectangle();
    const AnatomyStateSet states = anatomy_states();
    PartPaint plot{.role = PartRole::plot, .bounds = client, .states = states, .family = ControlVisualRole::editor};
    plot.key = PartKey{"plot", {}, {}};
    paint_part(painter, *this, plot);

    const std::vector<SevenSegmentCell> shown = cells();
    const CellMetrics metrics = cell_metrics(Size{client.width, client.height});
    if (!(metrics.height > 0.0)) return;
    const bool ghosts = ghost_segments.read();
    AnatomyStateSet lit_states = states;
    lit_states.insert(AnatomyState::selected);
    PartPaint bar{.role = PartRole::trace, .states = lit_states, .family = ControlVisualRole::progress};
    bar.corner_radius = metrics.thickness * 0.5;
    PartPaint ghost{.role = PartRole::graticule, .states = states, .family = ControlVisualRole::progress};
    ghost.corner_radius = metrics.thickness * 0.5;
    double x = metrics.left;
    for (const SevenSegmentCell& cell : shown) {
        if (cell.colon) {
            // Two dots, a third and two thirds of the way down.
            const double dot_x = x + (metrics.colon_width - metrics.thickness) * 0.5;
            const std::array<double, 2> rows{metrics.top + metrics.height * 0.3 - metrics.thickness * 0.5,
                                             metrics.top + metrics.height * 0.7 - metrics.thickness * 0.5};
            for (const double row : rows) {
                bar.bounds = Rect{dot_x, row, metrics.thickness, metrics.thickness};
                paint_part(painter, *this, bar);
            }
            x += metrics.colon_width + metrics.gap;
            continue;
        }
        const Rect frame{x, metrics.top, metrics.digit_width, metrics.height};
        const std::array<Rect, segments_per_cell> bars = segment_bars(frame, metrics.thickness);
        for (std::size_t segment = 0U; segment < segments_per_cell; ++segment) {
            const bool lit = (cell.segments & (1U << segment)) != 0U;
            if (lit) {
                bar.bounds = bars[segment];
                paint_part(painter, *this, bar);
            } else if (ghosts) {
                ghost.bounds = bars[segment];
                paint_part(painter, *this, ghost);
            }
        }
        // The point sits in the gap after the digit, on the bottom bar's line.
        const Rect point{x + metrics.digit_width + (metrics.gap - metrics.thickness) * 0.5,
                         metrics.top + metrics.height - metrics.thickness, metrics.thickness, metrics.thickness};
        if (cell.point) {
            bar.bounds = point;
            paint_part(painter, *this, bar);
        } else if (ghosts) {
            ghost.bounds = point;
            paint_part(painter, *this, ghost);
        }
        x += metrics.digit_width + metrics.gap;
    }
}

// A readout takes no input: the pointer reaches whatever lies beneath it.
bool SevenSegmentDisplay::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor SevenSegmentDisplay::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::static_text;
    descriptor.name = accessible_name.read().empty() ? text.read() : accessible_name.read();
    descriptor.value = text.read();
    descriptor.description = accessible_description.read();
    descriptor.states |= SemanticState::read_only;
    descriptor.exposed = true;
    return descriptor;
}

const ControlAnatomy& SevenSegmentDisplay::anatomy() const noexcept {
    return seven_segment_anatomy;
}

void SevenSegmentDisplay::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect client = client_rectangle();
    const CellMetrics metrics = cell_metrics(Size{client.width, client.height});
    const std::vector<SevenSegmentCell> shown = cells();
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    record.plot = PartGeometry::from_bounds(client);
    Rect lit{};
    Rect all{};
    double x = metrics.left;
    for (const SevenSegmentCell& cell : shown) {
        const double width = cell.colon ? metrics.colon_width : metrics.digit_width;
        const Rect frame{x, metrics.top, width + metrics.gap, metrics.height};
        all = joined(all, frame);
        if (cell.segments != 0U || cell.point || cell.colon) lit = joined(lit, frame);
        x += width + metrics.gap;
    }
    record.trace = PartGeometry::from_bounds(lit);
    if (ghost_segments.read()) record.graticule = PartGeometry::from_bounds(all);
    write_parts(anatomy(), seven_segment_part_fields, record, geometry);
}

SevenSegmentDisplay::Parts SevenSegmentDisplay::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), seven_segment_part_fields, published.parts(), record);
    return record;
}

// property_codemod begin SevenSegmentDisplay (tools/property_families/seven_segment_display.json; generated, do not edit)

namespace {

PropertyDescriptor seven_segment_display_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Behavior";
    descriptor.description = "The reading the cells show.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_seven_segment_display_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        seven_segment_display_text_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

PropertyDescriptor seven_segment_display_digit_count_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "digit_count";
    descriptor.kind = BindingValueKind::unsigned_integer;
    descriptor.category = "Appearance";
    descriptor.description = "The digit cells shown; zero fits the text.";
    descriptor.default_value = BindingValue{std::uint64_t{0U}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_seven_segment_display_digit_count() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        seven_segment_display_digit_count_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor seven_segment_display_ghost_segments_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "ghost_segments";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether the unlit segments are drawn faintly.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_seven_segment_display_ghost_segments() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        seven_segment_display_ghost_segments_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, SevenSegmentDisplay::seven_segment_display_property_count>& SevenSegmentDisplay::seven_segment_display_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"text", &describe_seven_segment_display_text,
         &read_cell_entry<SevenSegmentDisplay, decltype(&SevenSegmentDisplay::text), &SevenSegmentDisplay::text>,
         &write_cell_entry<SevenSegmentDisplay, decltype(&SevenSegmentDisplay::text), &SevenSegmentDisplay::text>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<SevenSegmentDisplay, decltype(&SevenSegmentDisplay::text_changed_), &SevenSegmentDisplay::text_changed_>},
        {"digit_count", &describe_seven_segment_display_digit_count,
         &read_cell_entry<SevenSegmentDisplay, decltype(&SevenSegmentDisplay::digit_count), &SevenSegmentDisplay::digit_count>,
         &write_cell_entry<SevenSegmentDisplay, decltype(&SevenSegmentDisplay::digit_count), &SevenSegmentDisplay::digit_count>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"ghost_segments", &describe_seven_segment_display_ghost_segments,
         &read_cell_entry<SevenSegmentDisplay, decltype(&SevenSegmentDisplay::ghost_segments), &SevenSegmentDisplay::ghost_segments>,
         &write_cell_entry<SevenSegmentDisplay, decltype(&SevenSegmentDisplay::ghost_segments), &SevenSegmentDisplay::ghost_segments>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "SevenSegmentDisplay's property table names a property twice");
    static const std::array<PropertyEntry, seven_segment_display_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> SevenSegmentDisplay::property_table() const noexcept {
    const std::array<PropertyEntry, seven_segment_display_property_count>& entries = seven_segment_display_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void SevenSegmentDisplay::prepare_write(const decltype(text)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("SevenSegmentDisplay text must be valid UTF-8");
}

void SevenSegmentDisplay::written(const decltype(text)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    publish_change(text_changed_, text.read());
}

void SevenSegmentDisplay::prepare_write(const decltype(digit_count)&, std::uint32_t& incoming) {
    require_mutable();
    if (incoming > 64U) throw std::invalid_argument("SevenSegmentDisplay digit count must be at most 64");
}

void SevenSegmentDisplay::written(const decltype(digit_count)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void SevenSegmentDisplay::prepare_write(const decltype(ghost_segments)&, bool&) {
    require_mutable();
}

void SevenSegmentDisplay::written(const decltype(ghost_segments)&) {
    invalidate(Dirty::paint);
}

// property_codemod end SevenSegmentDisplay

} // namespace gui_forms
