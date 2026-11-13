#include "gui_forms/controls/spinner.hpp"

#include "../progress_ring/ring_dots.hpp"

#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// The side a spinner measures without a requested size.
constexpr double natural_side = 24.0;

constexpr std::array<PartField<Spinner::Parts>, 2> spinner_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &Spinner::Parts::region},
    {.declaration = {.name = "activity", .role = PartRole::activity, .composed_from = {}},
     .member = &Spinner::Parts::activity},
}};

constexpr std::array<PartDeclaration, 2> spinner_parts = part_declarations(spinner_part_fields);

constexpr std::array<AnatomyState, 4> spinner_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled, AnatomyState::reduced_motion,
};

constexpr ControlAnatomy spinner_anatomy{
    .control_id = "spinner",
    .parts = spinner_parts,
    .states = spinner_states,
    .custom_states = {},
};

// The time one dot's step takes.
constexpr FrameInterval step_interval = Spinner::turn_period / static_cast<std::int64_t>(Spinner::dot_count);

} // namespace

Spinner::Spinner(StableId stable_id) : Control(std::move(stable_id)) {}

bool Spinner::wants_motion() const noexcept {
    const Window* const host = attached_window();
    if (host == nullptr || !active.read()) return false;
    const bool moves = !(*host).motion_clock().ambient_suppressed();
    return moves;
}

bool Spinner::moving() const noexcept {
    const Window* const host = attached_window();
    const bool turning = host != nullptr && (*host).motion_clock().control_ambient(*this);
    return turning;
}

void Spinner::update_motion(bool painting) {
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

bool Spinner::on_ambient_frame(FrameTime now) {
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

void Spinner::on_detached_from_window() noexcept {
    Window* const host = attached_window();
    if (host != nullptr) (*host).motion_clock().withdraw_control_ambient(*this);
    phase_ = 0U;
    Control::on_detached_from_window();
}

Rect Spinner::ring_square() const noexcept {
    const Rect client = client_rectangle();
    const double side = std::min(client.width, client.height);
    const Rect square{client.x + (client.width - side) * 0.5, client.y + (client.height - side) * 0.5, side, side};
    return square;
}

Size Spinner::measure(Size available) {
    const Rect requested = requested_bounds();
    const double width = requested.width > 0.0 ? requested.width : natural_side;
    const double height = requested.height > 0.0 ? requested.height : natural_side;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

void Spinner::on_paint(Painter& painter, Rect) {
    // A drawn ring enlists on the motion clock (or stays on it).
    if (active.read()) {
        ++paints_;
        update_motion(true);
    }
    if (!active.read()) return;
    const detail::RingPlacement ring = detail::place_ring(ring_square(), dot_count);
    detail::RingLighting lighting{};
    lighting.count = dot_count;
    lighting.first = phase_;
    lighting.lit = lit_count;
    lighting.lit_role = PartRole::activity;
    lighting.lit_part = "activity";
    detail::paint_ring(painter, *this, ring, lighting, anatomy_states());
}

// A spinner takes no input: the pointer reaches whatever lies beneath it.
bool Spinner::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor Spinner::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::progress_bar;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    // Busy while active, whether or not reduced motion holds it still: the
    // work is under way either way.
    if (active.read()) descriptor.states |= SemanticState::busy;
    descriptor.exposed = active.read();
    return descriptor;
}

const ControlAnatomy& Spinner::anatomy() const noexcept {
    return spinner_anatomy;
}

AnatomyStateSet Spinner::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    const Window* const window = attached_window();
    if (window != nullptr && (*window).presentation_settings().reduced_motion) {
        states.insert(AnatomyState::reduced_motion);
    }
    return states;
}

void Spinner::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    if (active.read()) record.activity = PartGeometry::from_bounds(ring_square());
    write_parts(anatomy(), spinner_part_fields, record, geometry);
}

void Spinner::prepare_write(const decltype(active)&, bool&) {
    require_mutable();
}

// Activating shows the ring, whose next paint starts the turn on the motion
// clock; deactivating hides it and withdraws it from the clock.
void Spinner::written(const decltype(active)&) {
    invalidate(Dirty::paint | Dirty::semantics);
    update_motion(false);
}

Spinner::Parts Spinner::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), spinner_part_fields, published.parts(), record);
    return record;
}

// property_codemod begin Spinner (tools/property_families/spinner.json; generated, do not edit)

namespace {

PropertyDescriptor spinner_active_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "active";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether the spinner shows and turns.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_spinner_active() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        spinner_active_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, Spinner::spinner_property_count>& Spinner::spinner_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"active", &describe_spinner_active,
         &read_cell_entry<Spinner, decltype(&Spinner::active), &Spinner::active>,
         &write_cell_entry<Spinner, decltype(&Spinner::active), &Spinner::active>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "Spinner's property table names a property twice");
    static const std::array<PropertyEntry, spinner_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> Spinner::property_table() const noexcept {
    const std::array<PropertyEntry, spinner_property_count>& entries = spinner_property_entries();
    return std::span<const PropertyEntry>(entries);
}

// property_codemod end Spinner

} // namespace gui_forms
