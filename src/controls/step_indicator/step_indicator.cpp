#include "gui_forms/controls/step_indicator/step_indicator.hpp"

#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ios>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// Geometry in logical units: a chip's side, the gap between a chip and its
// connector, a connector's thickness, the gap above a name, and the natural
// width of one step.
constexpr double chip_side = 24.0;
constexpr double connector_gap = 6.0;
constexpr double connector_thickness = 2.0;
constexpr double caption_gap = 4.0;
constexpr double natural_step_width = 120.0;
constexpr double check_side = 12.0;

constexpr std::array<PartField<StepIndicator::Parts>, 5> step_indicator_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &StepIndicator::Parts::region},
    {.declaration = {.name = "chip", .role = PartRole::chip, .composed_from = {}},
     .member = &StepIndicator::Parts::chip},
    {.declaration = {.name = "glyph", .role = PartRole::glyph, .composed_from = {}},
     .member = &StepIndicator::Parts::glyph},
    {.declaration = {.name = "divider", .role = PartRole::divider, .composed_from = {}},
     .member = &StepIndicator::Parts::divider},
    {.declaration = {.name = "caption", .role = PartRole::caption, .composed_from = {}},
     .member = &StepIndicator::Parts::caption},
}};

constexpr std::array<PartDeclaration, 5> step_indicator_parts = part_declarations(step_indicator_part_fields);

constexpr std::array<AnatomyState, 5> step_indicator_states{
    AnatomyState::normal, AnatomyState::selected, AnatomyState::checked, AnatomyState::enabled,
    AnatomyState::disabled,
};

constexpr ControlAnatomy step_indicator_anatomy{
    .control_id = "step_indicator",
    .parts = step_indicator_parts,
    .states = step_indicator_states,
    .custom_states = {},
};

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

std::string decimal(std::size_t number) {
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << number;
    return text.str();
}

} // namespace

StepIndicator::StepIndicator(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = false;
    cursor = CursorKind::arrow;
}

void StepIndicator::set_steps(std::vector<std::string> steps) {
    require_mutable();
    for (const std::string& step : steps) {
        if (!validate_utf8(step).valid()) throw std::invalid_argument("StepIndicator step names must be valid UTF-8");
    }
    steps_ = std::move(steps);
    const std::uint32_t last = steps_.empty() ? 0U : static_cast<std::uint32_t>(steps_.size() - 1U);
    if (current_step.read() > last) current_step = last;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

FontSpec StepIndicator::caption_font() const noexcept {
    const FontSpec font = text_font(effective_theme().structure().typography.caption);
    return font;
}

FontSpec StepIndicator::number_font() const noexcept {
    FontSpec font = text_font(effective_theme().structure().typography.control);
    font.weight = 600;
    return font;
}

std::vector<StepIndicator::StepPlacement> StepIndicator::placements(double width) const {
    std::vector<StepPlacement> placed;
    const std::size_t count = steps_.size();
    if (count == 0U || !(width > 0.0)) return placed;
    placed.reserve(count);
    // Each step owns an equal column; its chip is centred in the column and
    // the connector runs from this chip to the next one's.
    const double column = width / static_cast<double>(count);
    const Size caption_size = resolve_text_layout_utf8("Ag", caption_font()).logical_size;
    const double caption_height = std::ceil(caption_size.height);
    for (std::size_t index = 0U; index < count; ++index) {
        StepPlacement step;
        const double centre = column * (static_cast<double>(index) + 0.5);
        step.chip = Rect{std::floor(centre - chip_side * 0.5), 0.0, chip_side, chip_side};
        step.caption = Rect{std::floor(column * static_cast<double>(index)), chip_side + caption_gap, std::floor(column),
                            caption_height};
        if (index + 1U < count) {
            const double start = step.chip.x + chip_side + connector_gap;
            const double end = std::floor(centre + column - chip_side * 0.5) - connector_gap;
            if (end > start) {
                step.connector = Rect{start, std::floor((chip_side - connector_thickness) * 0.5), end - start,
                                      connector_thickness};
            }
        }
        placed.push_back(step);
    }
    return placed;
}

std::optional<std::size_t> StepIndicator::step_at(Point local_point) const {
    const Rect client = client_rectangle();
    const std::vector<StepPlacement> placed = placements(client.width);
    for (std::size_t index = 0U; index < placed.size(); ++index) {
        if (placed[index].chip.contains(local_point) || placed[index].caption.contains(local_point)) return index;
    }
    return std::nullopt;
}

Size StepIndicator::measure(Size available) {
    const Rect requested = requested_bounds();
    const Size caption_size = resolve_text_layout_utf8("Ag", caption_font()).logical_size;
    const double natural_height = chip_side + caption_gap + std::ceil(caption_size.height);
    const double natural_width = natural_step_width * static_cast<double>(std::max<std::size_t>(1U, steps_.size()));
    const double width = requested.width > 0.0 ? requested.width : natural_width;
    const double height = requested.height > 0.0 ? requested.height : natural_height;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

void StepIndicator::on_paint(Painter& painter, Rect) {
    const Rect client = client_rectangle();
    const std::vector<StepPlacement> placed = placements(client.width);
    const AnatomyStateSet states = anatomy_states();
    const std::size_t current = current_step.read();
    const FontSpec numbers = number_font();
    const FontSpec captions = caption_font();
    for (std::size_t index = 0U; index < placed.size(); ++index) {
        const StepPlacement& step = placed[index];
        const bool complete = index < current;
        const bool reached = index <= current;
        AnatomyStateSet chip_states = states;
        if (reached) chip_states.insert(AnatomyState::selected);
        if (complete) chip_states.insert(AnatomyState::checked);
        if (!step.connector.empty()) {
            AnatomyStateSet connector_states = states;
            if (complete) connector_states.insert(AnatomyState::selected);
            const PartPaint connector{.role = PartRole::divider, .bounds = step.connector,
                                      .states = connector_states, .family = ControlVisualRole::selection};
            paint_part(painter, *this, connector);
        }
        PartPaint chip{.role = PartRole::chip, .bounds = step.chip, .states = chip_states,
                       .family = ControlVisualRole::selection};
        chip.corner_radius = chip_side * 0.5;
        paint_part(painter, *this, chip);
        if (complete) {
            const Rect check{step.chip.x + (chip_side - check_side) * 0.5, step.chip.y + (chip_side - check_side) * 0.5,
                             check_side, check_side};
            PartPaint glyph{.role = PartRole::glyph, .bounds = check, .states = chip_states,
                            .family = ControlVisualRole::selection};
            glyph.mark = PartMark::check;
            paint_part(painter, *this, glyph);
        } else {
            const std::string number = decimal(index + 1U);
            PartPaint label{.role = PartRole::label, .bounds = step.chip, .states = chip_states,
                            .family = ControlVisualRole::selection};
            label.text = number;
            label.font = numbers;
            label.alignment = PartTextAlignment::center;
            paint_part(painter, *this, label);
        }
        PartPaint caption{.role = PartRole::caption, .bounds = step.caption, .states = states,
                          .family = ControlVisualRole::panel};
        caption.text = steps_[index];
        caption.font = captions;
        caption.alignment = PartTextAlignment::center;
        caption.tone = index == current ? PartTextTone::ordinary : PartTextTone::secondary;
        paint_part(painter, *this, caption);
    }
}

void StepIndicator::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    if (event.button != PointerButton::primary || event.action != PointerAction::down) return;
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    const std::optional<std::size_t> pressed = step_at(local);
    if (!pressed || *pressed >= current_step.read()) return;
    event.handled = true;
    step_clicked_.emit(*pressed);
}

SemanticDescriptor StepIndicator::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    if (!steps_.empty()) {
        const std::size_t current = current_step.read();
        std::string value = "Step " + decimal(current + 1U) + " of " + decimal(steps_.size());
        if (!steps_[current].empty()) value += ": " + steps_[current];
        descriptor.value = value;
    }
    descriptor.states |= SemanticState::read_only;
    descriptor.exposed = true;
    return descriptor;
}

const ControlAnatomy& StepIndicator::anatomy() const noexcept {
    return step_indicator_anatomy;
}

void StepIndicator::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect client = client_rectangle();
    const std::vector<StepPlacement> placed = placements(client.width);
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    Rect chips{};
    Rect checks{};
    Rect connectors{};
    Rect captions{};
    for (std::size_t index = 0U; index < placed.size(); ++index) {
        chips = joined(chips, placed[index].chip);
        if (index < current_step.read()) checks = joined(checks, placed[index].chip);
        connectors = joined(connectors, placed[index].connector);
        captions = joined(captions, placed[index].caption);
    }
    record.chip = PartGeometry::from_bounds(chips);
    record.glyph = PartGeometry::from_bounds(checks);
    record.divider = PartGeometry::from_bounds(connectors);
    record.caption = PartGeometry::from_bounds(captions);
    write_parts(anatomy(), step_indicator_part_fields, record, geometry);
}

StepIndicator::Parts StepIndicator::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), step_indicator_part_fields, published.parts(), record);
    return record;
}

// property_codemod begin StepIndicator (tools/property_families/step_indicator.json; generated, do not edit)

namespace {

PropertyDescriptor step_indicator_current_step_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "current_step";
    descriptor.kind = BindingValueKind::unsigned_integer;
    descriptor.category = "Behavior";
    descriptor.description = "The step under way, counted from zero.";
    descriptor.default_value = BindingValue{std::uint64_t{0U}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_step_indicator_current_step() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        step_indicator_current_step_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, StepIndicator::step_indicator_property_count>& StepIndicator::step_indicator_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"current_step", &describe_step_indicator_current_step,
         &read_cell_entry<StepIndicator, decltype(&StepIndicator::current_step), &StepIndicator::current_step>,
         &write_cell_entry<StepIndicator, decltype(&StepIndicator::current_step), &StepIndicator::current_step>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<StepIndicator, decltype(&StepIndicator::current_step_changed_), &StepIndicator::current_step_changed_>},
    }};
    static_assert(property_names_distinct(own), "StepIndicator's property table names a property twice");
    static const std::array<PropertyEntry, step_indicator_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> StepIndicator::property_table() const noexcept {
    const std::array<PropertyEntry, step_indicator_property_count>& entries = step_indicator_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void StepIndicator::prepare_write(const decltype(current_step)&, std::uint32_t& incoming) {
    require_mutable();
    if (incoming != 0U && incoming >= steps_.size()) throw std::invalid_argument("StepIndicator current step is past the last step");
}

void StepIndicator::written(const decltype(current_step)&) {
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(current_step_changed_, current_step.read());
}

// property_codemod end StepIndicator

namespace {

// The events StepIndicator adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> step_indicator_interactions{{
    {.name = "step_clicked", .payload = "std::size_t", .mutable_payload = false},
    {.name = "current_step_changed", .payload = "std::uint32_t", .mutable_payload = false},
}};

} // namespace

void StepIndicator::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), step_indicator_interactions.begin(), step_indicator_interactions.end());
}

} // namespace gui_forms
