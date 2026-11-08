#include "gui_forms/controls/progress_ring/progress_ring.hpp"

#include "ring_dots.hpp"

#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

constexpr double natural_side = 32.0;

constexpr std::array<PartField<ProgressRing::Parts>, 4> progress_ring_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &ProgressRing::Parts::region},
    {.declaration = {.name = "track", .role = PartRole::track, .composed_from = {}},
     .member = &ProgressRing::Parts::track},
    {.declaration = {.name = "fill", .role = PartRole::fill, .composed_from = {}},
     .member = &ProgressRing::Parts::fill},
    {.declaration = {.name = "activity", .role = PartRole::activity, .composed_from = {}},
     .member = &ProgressRing::Parts::activity},
}};

constexpr std::array<PartDeclaration, 4> progress_ring_parts = part_declarations(progress_ring_part_fields);

// "indeterminate" (AnatomyState::indeterminate, as on ProgressBar) names the
// turning ring for a theme's selectors (`:state(indeterminate)`).
constexpr std::array<AnatomyState, 5> progress_ring_states{
    AnatomyState::normal, AnatomyState::indeterminate, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::reduced_motion,
};

constexpr ControlAnatomy progress_ring_anatomy{
    .control_id = "progress_ring",
    .parts = progress_ring_parts,
    .states = progress_ring_states,
};

constexpr FrameInterval step_interval =
    ProgressRing::turn_period / static_cast<std::int64_t>(ProgressRing::dot_count);

} // namespace

ProgressRing::ProgressRing(StableId stable_id) : Control(std::move(stable_id)) {}

double ProgressRing::fraction() const noexcept {
    const double ratio = value.read() / maximum.read();
    const double clamped = std::clamp(ratio, 0.0, 1.0);
    return clamped;
}

std::size_t ProgressRing::lit_dots() const noexcept {
    const double share = fraction();
    if (!(share > 0.0)) return 0U;
    const double rounded = std::round(share * static_cast<double>(dot_count));
    const std::size_t lit = std::clamp(static_cast<std::size_t>(rounded), std::size_t{1U}, dot_count);
    return lit;
}

bool ProgressRing::wants_motion() const noexcept {
    const Window* const host = attached_window();
    if (host == nullptr || !indeterminate.read()) return false;
    const bool moves = !(*host).motion_clock().ambient_suppressed();
    return moves;
}

bool ProgressRing::moving() const noexcept {
    const Window* const host = attached_window();
    const bool turning = host != nullptr && (*host).motion_clock().control_ambient(*this);
    return turning;
}

void ProgressRing::update_motion(bool painting) {
    Window* const host = attached_window();
    if (host == nullptr) return;
    MotionClock& clock = (*host).motion_clock();
    if (!wants_motion()) {
        clock.withdraw_control_ambient(*this);
        phase_ = 0U;
        return;
    }
    if (!painting && !clock.control_ambient(*this)) return;
    // One frame per dot step: the ring advances a dot a frame.
    const bool started = clock.enlist_control_ambient(*this, step_interval);
    if (!started) return;
    // The turn starts or resumes at twelve o'clock, with no catch-up for a
    // pause.
    motion_start_ = clock.now();
    phase_ = 0U;
    paints_seen_ = ~std::uint64_t{0};
}

bool ProgressRing::on_ambient_frame(FrameTime now) {
    if (paints_ == paints_seen_) return false;
    paints_seen_ = paints_;
    const std::int64_t steps = (now - motion_start_) / step_interval;
    const std::size_t next = steps > 0 ? static_cast<std::size_t>(steps) % dot_count : 0U;
    // Repainted every frame, a step moved or not: the paint keeps the ring
    // on the clock.
    phase_ = next;
    invalidate(Dirty::paint);
    return true;
}

void ProgressRing::on_detached_from_window() noexcept {
    Window* const host = attached_window();
    if (host != nullptr) (*host).motion_clock().withdraw_control_ambient(*this);
    phase_ = 0U;
    Control::on_detached_from_window();
}

Rect ProgressRing::ring_square() const noexcept {
    const Rect client = client_rectangle();
    const double side = std::min(client.width, client.height);
    const Rect square{client.x + (client.width - side) * 0.5, client.y + (client.height - side) * 0.5, side, side};
    return square;
}

Size ProgressRing::measure(Size available) {
    const Rect requested = requested_bounds();
    const double width = requested.width > 0.0 ? requested.width : natural_side;
    const double height = requested.height > 0.0 ? requested.height : natural_side;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

void ProgressRing::on_paint(Painter& painter, Rect) {
    // A drawn turning ring enlists on the motion clock (or stays on it).
    if (indeterminate.read()) {
        ++paints_;
        update_motion(true);
    }
    const detail::RingPlacement ring = detail::place_ring(ring_square(), dot_count);
    detail::RingLighting lighting{};
    lighting.count = dot_count;
    if (indeterminate.read()) {
        lighting.first = phase_;
        lighting.lit = run_count;
        lighting.lit_role = PartRole::activity;
        lighting.lit_part = "activity";
    } else {
        lighting.first = 0U;
        lighting.lit = lit_dots();
        lighting.lit_role = PartRole::fill;
        lighting.lit_part = "fill";
    }
    detail::paint_ring(painter, *this, ring, lighting, anatomy_states());
}

// A ring takes no input: the pointer reaches whatever lies beneath it.
bool ProgressRing::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor ProgressRing::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::progress_bar;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    if (indeterminate.read()) {
        // No amount: busy, without a value.
        descriptor.states |= SemanticState::busy;
    } else {
        const double shown = std::clamp(value.read(), 0.0, maximum.read());
        std::ostringstream text;
        text << shown;
        descriptor.value = text.str();
        descriptor.numeric_value = shown;
        descriptor.minimum_value = 0.0;
        descriptor.maximum_value = maximum.read();
    }
    descriptor.exposed = true;
    return descriptor;
}

const ControlAnatomy& ProgressRing::anatomy() const noexcept {
    return progress_ring_anatomy;
}

AnatomyStateSet ProgressRing::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (indeterminate.read()) states.insert(AnatomyState::indeterminate);
    const Window* const window = attached_window();
    if (window != nullptr && (*window).presentation_settings().reduced_motion) {
        states.insert(AnatomyState::reduced_motion);
    }
    return states;
}

void ProgressRing::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    const Rect square = ring_square();
    record.region = PartGeometry::from_bounds(client_rectangle());
    record.track = PartGeometry::from_bounds(square);
    if (indeterminate.read()) {
        record.activity = PartGeometry::from_bounds(square);
    } else if (lit_dots() != 0U) {
        record.fill = PartGeometry::from_bounds(square);
    }
    write_parts(anatomy(), progress_ring_part_fields, record, geometry);
}

void ProgressRing::prepare_write(const decltype(indeterminate)&, bool&) {
    require_mutable();
}

// Turning indeterminate shows the run, whose next paint starts it on the
// motion clock; turning determinate withdraws it from the clock.
void ProgressRing::written(const decltype(indeterminate)&) {
    invalidate(Dirty::paint | Dirty::semantics);
    update_motion(false);
}

ProgressRing::Parts ProgressRing::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), progress_ring_part_fields, published.parts(), record);
    return record;
}

// property_codemod begin ProgressRing (tools/property_families/progress_ring.json; generated, do not edit)

namespace {

PropertyDescriptor progress_ring_value_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "value";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "The progress, from 0 to maximum.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_progress_ring_value() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        progress_ring_value_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor progress_ring_maximum_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "maximum";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "The value of a full ring.";
    descriptor.default_value = BindingValue{100.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_progress_ring_maximum() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        progress_ring_maximum_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor progress_ring_indeterminate_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "indeterminate";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether the ring turns instead of showing the value.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_progress_ring_indeterminate() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        progress_ring_indeterminate_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, ProgressRing::progress_ring_property_count>& ProgressRing::progress_ring_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"value", &describe_progress_ring_value,
         &read_cell_entry<ProgressRing, decltype(&ProgressRing::value), &ProgressRing::value>,
         &write_cell_entry<ProgressRing, decltype(&ProgressRing::value), &ProgressRing::value>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"maximum", &describe_progress_ring_maximum,
         &read_cell_entry<ProgressRing, decltype(&ProgressRing::maximum), &ProgressRing::maximum>,
         &write_cell_entry<ProgressRing, decltype(&ProgressRing::maximum), &ProgressRing::maximum>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"indeterminate", &describe_progress_ring_indeterminate,
         &read_cell_entry<ProgressRing, decltype(&ProgressRing::indeterminate), &ProgressRing::indeterminate>,
         &write_cell_entry<ProgressRing, decltype(&ProgressRing::indeterminate), &ProgressRing::indeterminate>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "ProgressRing's property table names a property twice");
    static const std::array<PropertyEntry, progress_ring_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> ProgressRing::property_table() const noexcept {
    const std::array<PropertyEntry, progress_ring_property_count>& entries = progress_ring_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void ProgressRing::prepare_write(const decltype(value)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming)) throw std::invalid_argument("ProgressRing value must be finite");
}

void ProgressRing::written(const decltype(value)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void ProgressRing::prepare_write(const decltype(maximum)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming <= 0.0) {
        throw std::invalid_argument("ProgressRing maximum must be finite and greater than zero");
    }
}

void ProgressRing::written(const decltype(maximum)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

// property_codemod end ProgressRing

} // namespace gui_forms
