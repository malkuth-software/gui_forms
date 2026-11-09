#include "gui_forms/controls/splitter/splitter.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "../../core/drag_engine/drag_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

// Windows Forms' default splitter is three pixels across.
constexpr double default_thickness = 3.0;
// Adjacent edges closer than this touch; layout rounds to whole units.
constexpr double adjacency_tolerance = 0.5;
// The step of the semantic increment and decrement actions.
constexpr double semantic_step = 10.0;

void require_extent(double extent, const char* message) {
    if (!std::isfinite(extent) || extent < 0.0) throw std::invalid_argument(message);
}

bool touches(double left, double right) noexcept {
    const bool result = std::abs(left - right) <= adjacency_tolerance;
    return result;
}

} // namespace

// The splitter's drag client (the drag engine's first migrated client): one
// projection along the splitter's axis from the target's extent, clamped to
// the limits, with no threshold. A preview applies the extent live, after
// splitter_moving; the release applies the projected extent (not a
// splitter_moving handler's last change, as before the engine) and raises
// splitter_moved; any other end puts back the extent before the drag.
// Owned by the splitter, which the engine observes: a disposed splitter is
// never called.
class SplitterDrag final : public DragClient {
public:
    explicit SplitterDrag(Splitter& splitter) noexcept : splitter_(&splitter) {}

    [[nodiscard]] DragPlanRefusal plan_drag(DragPlanBuilder& builder, const DragSubject&) override {
        Splitter& splitter = *splitter_;
        const Control::Ptr resized = splitter.drag_target_.lock();
        if (!resized) return DragPlanRefusal::client_refused;
        const Splitter::SplitLimits bounds = splitter.limits(*resized);
        // A left or top target grows as the pointer moves away from it, so
        // to the right or down; a right or bottom target grows the other way.
        const DockStyle edge = splitter.dock.read();
        const bool forward = edge == DockStyle::left || edge == DockStyle::top;
        DragProjection projection{};
        projection.axis = splitter.horizontal_axis() ? DragAxis::x : DragAxis::y;
        projection.origin_value = splitter.drag_start_extent_;
        projection.scale = forward ? 1.0 : -1.0;
        projection.minimum = bounds.minimum;
        projection.maximum = bounds.maximum;
        projection.keyboard_step = semantic_step;
        const DragNodeIndex root = builder.project(projection);
        builder.group_container(DragGroup{}, *resized);
        const DragPlanRefusal refusal = builder.finish(root);
        if (refusal != DragPlanRefusal::none) return refusal;
        splitter.dragging_ = true;
        splitter.invalidate(Dirty::paint);
        return refusal;
    }

    void preview_drag(const DragFrame& frame) override {
        if (frame.selection.kind != DragSelectionKind::value) return;
        Splitter& splitter = *splitter_;
        const Control::Ptr resized = splitter.drag_target_.lock();
        SurfaceRoot* const root = splitter.surface_root();
        if (!resized || !(*resized).is_alive()) {
            // The target went away during the drag: the model changed.
            if (root != nullptr) (*root).drag_engine().cancel(DragEnd::model_changed);
            return;
        }
        SplitterMoveEvent move{pointer_in_parent(frame.pointer), frame.selection.value};
        splitter.splitter_moving_.emit(move);
        if (!splitter.is_alive()) return;
        splitter.apply(*resized, splitter.clamped(move.split_position, *resized));
        splitter.previewed_ = true;
    }

    void announce_drag(const DragFrame& frame, DragAnnouncement& announcement) override {
        if (frame.selection.kind != DragSelectionKind::value && frame.end == DragEnd::none) return;
        const bool committed = frame.end == DragEnd::free;
        const bool running = frame.end == DragEnd::none;
        const double shown = committed || running ? frame.selection.value : (*splitter_).drag_start_extent_;
        const char* suffix = "";
        if (committed) suffix = ", done";
        if (!committed && !running) suffix = ", restored";
        std::array<char, 96> text{};
        const int written = std::snprintf(text.data(), text.size(), "Split position %.0f%s", shown, suffix);
        if (written <= 0) return;
        const std::size_t length = std::min(static_cast<std::size_t>(written), text.size() - 1U);
        announcement.assign(std::string_view(text.data(), length));
    }

    void end_drag(const DragOutcome& outcome) override {
        Splitter& splitter = *splitter_;
        const Control::Ptr resized = splitter.drag_target_.lock();
        const bool previewed = splitter.previewed_;
        splitter.dragging_ = false;
        splitter.previewed_ = false;
        splitter.drag_target_.reset();
        splitter.invalidate(Dirty::paint);
        if (!resized || !(*resized).is_alive()) return;
        if (outcome.end == DragEnd::free && outcome.selection.kind == DragSelectionKind::value) {
            const SplitterMoveEvent move{pointer_in_parent(outcome.pointer), outcome.selection.value};
            splitter.apply(*resized, move.split_position);
            splitter.splitter_moved_.emit(move);
            return;
        }
        if (previewed) splitter.apply(*resized, splitter.drag_start_extent_);
    }

    [[nodiscard]] DragInteractionEvents* drag_interactions() noexcept override {
        return &(*splitter_).drag_interactions_;
    }

private:
    [[nodiscard]] Point pointer_in_parent(Point window_point) const {
        const Control::Ptr owner = (*splitter_).parent();
        const Point pointer = owner ? (*owner).point_from_window(window_point) : window_point;
        return pointer;
    }

    Splitter* splitter_;
};

Splitter::Splitter(StableId stable_id)
    : Control(std::move(stable_id)), drag_(std::make_unique<SplitterDrag>(*this)) {
    dock = DockStyle::left;
    set_requested_bounds({0.0, 0.0, default_thickness, default_thickness});
}

// The engine observes this splitter, so a session it runs ends without
// calling the client once the splitter is gone; the client is released here.
Splitter::~Splitter() = default;

bool Splitter::horizontal_axis() const noexcept {
    // Left and right splitters move along x; top and bottom ones along y.
    const bool result = dock.read() == DockStyle::left || dock.read() == DockStyle::right;
    return result;
}

double Splitter::axis_of(Point point) const noexcept {
    return horizontal_axis() ? point.x : point.y;
}

double Splitter::extent_of(const Control& control) const noexcept {
    const Rect bounds = control.committed_arranged_bounds();
    return horizontal_axis() ? bounds.width : bounds.height;
}

// The target is found from the laid-out bounds, as Windows Forms' FindTarget
// does: the visible sibling docked at the same edge whose far side touches
// the splitter's near side.
Control::Ptr Splitter::target() const {
    const DockStyle edge = dock.read();
    if (edge == DockStyle::none || edge == DockStyle::fill) return nullptr;
    const Control::Ptr owner = parent();
    if (!owner) return nullptr;
    const Rect own = committed_arranged_bounds();
    for (const Control::Ptr& sibling : (*owner).children()) {
        if (!sibling || sibling.get() == this || !(*sibling).visible.read() || (*sibling).dock.read() != edge) continue;
        const Rect bounds = (*sibling).committed_arranged_bounds();
        bool adjacent = false;
        switch (edge) {
        case DockStyle::left: adjacent = touches(bounds.x + bounds.width, own.x); break;
        case DockStyle::right: adjacent = touches(bounds.x, own.x + own.width); break;
        case DockStyle::top: adjacent = touches(bounds.y + bounds.height, own.y); break;
        case DockStyle::bottom: adjacent = touches(bounds.y, own.y + own.height); break;
        case DockStyle::none:
        case DockStyle::fill:
        default:
            // Only the four edges have an adjacent sibling; none and fill
            // returned above, and any other value is no edge either.
            break;
        }
        if (adjacent) return sibling;
    }
    return nullptr;
}

// The target and the rest of the dock area share what the parent's client
// area leaves after its other edge-docked children along this axis.
Splitter::SplitLimits Splitter::limits(const Control& target) const {
    SplitLimits result{};
    result.minimum = min_size.read();
    const Control::Ptr owner = parent();
    if (!owner) {
        result.maximum = result.minimum;
        return result;
    }
    const Rect client = (*owner).client_rectangle();
    const Insets padding = (*owner).padding.read();
    double shared = horizontal_axis() ? client.width - padding.left - padding.right
                                      : client.height - padding.top - padding.bottom;
    for (const Control::Ptr& sibling : (*owner).children()) {
        if (!sibling || sibling.get() == &target || !(*sibling).visible.read()) continue;
        const DockStyle edge = (*sibling).dock.read();
        const bool same_axis = horizontal_axis() ? (edge == DockStyle::left || edge == DockStyle::right)
                                                 : (edge == DockStyle::top || edge == DockStyle::bottom);
        if (same_axis) shared -= extent_of(*sibling);
    }
    result.maximum = std::max(result.minimum, shared - min_extra.read());
    return result;
}

double Splitter::clamped(double extent, const Control& target) const {
    const SplitLimits bounds = limits(target);
    const double result = std::clamp(extent, bounds.minimum, bounds.maximum);
    return result;
}

std::optional<double> Splitter::split_position() const {
    const Control::Ptr resized = target();
    if (!resized) return std::nullopt;
    const double extent = extent_of(*resized);
    return extent;
}

void Splitter::apply(Control& resized, double extent) {
    Rect requested = resized.requested_bounds();
    if (horizontal_axis()) {
        requested.width = extent;
    } else {
        requested.height = extent;
    }
    resized.set_requested_bounds(requested);
}

void Splitter::set_split_position(double extent) {
    require_mutable();
    if (!std::isfinite(extent)) throw std::invalid_argument("Splitter split position must be finite");
    const Control::Ptr resized = target();
    if (!resized) return;
    apply(*resized, clamped(extent, *resized));
}

Size Splitter::measure(Size) {
    const Rect requested = requested_bounds();
    const Size result{requested.width, requested.height};
    return result;
}

void Splitter::on_pointer(PointerEvent& event) {
    if (event.action == PointerAction::enter || event.action == PointerAction::move ||
        event.action == PointerAction::leave) {
        // A splitter with nothing to resize ignores the pointer: no resize
        // cursor and no hot face.
        const bool hot = event.action != PointerAction::leave && enabled.read() && target() != nullptr;
        if (hot) cursor = horizontal_axis() ? CursorKind::resize_horizontal : CursorKind::resize_vertical;
        if (hot_ != hot) {
            hot_ = hot;
            invalidate(Dirty::paint);
        }
    }
    SurfaceRoot* const root = surface_root();
    if (root == nullptr) return;
    if (event.action == PointerAction::down && event.button == PointerButton::primary && enabled.read()) {
        if (!prepare_drag()) return;
        // No threshold: the press starts the drag, as before the engine.
        DragEngine& engine = (*root).drag_engine();
        const bool started = engine.arm(*drag_, *this, *this, 0U, event.position, absolute_bounds(), 0.0);
        if (!started) return;
        event.handled = true;
        return;
    }
    if (!dragging_) return;
    DragEngine& engine = (*root).drag_engine();
    if (!engine.runs(*drag_)) return;
    if (engine.pointer(event)) event.handled = true;
}

bool Splitter::prepare_drag() {
    const Control::Ptr resized = target();
    if (!resized) return false;
    drag_target_ = resized;
    drag_start_extent_ = extent_of(*resized);
    previewed_ = false;
    return true;
}

bool Splitter::begin_keyboard_drag() {
    SurfaceRoot* const root = surface_root();
    if (root == nullptr || !enabled.read() || !prepare_drag()) return false;
    DragEngine& engine = (*root).drag_engine();
    const bool started = engine.begin_keyboard(*drag_, *this, *this, 0U);
    return started;
}

AnatomyStateSet Splitter::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (hot_ || dragging_) states.insert(AnatomyState::hot);
    if (dragging_) {
        states.insert(AnatomyState::pressed);
        states.insert(AnatomyState::captured);
    }
    return states;
}

void Splitter::on_paint(Painter& painter, Rect) {
    PartPaint face{};
    face.role = PartRole::face;
    face.bounds = client_rectangle();
    face.states = anatomy_states();
    face.family = ControlVisualRole::panel;
    paint_part(painter, *this, face);
}

SemanticDescriptor Splitter::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::separator;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.orientation = horizontal_axis() ? SemanticOrientation::vertical : SemanticOrientation::horizontal;
    const Control::Ptr resized = target();
    if (resized) {
        const double extent = extent_of(*resized);
        const SplitLimits bounds = limits(*resized);
        descriptor.value = std::to_string(extent);
        descriptor.numeric_value = extent;
        descriptor.minimum_value = bounds.minimum;
        descriptor.maximum_value = bounds.maximum;
        descriptor.small_step = semantic_step;
        descriptor.actions = {SemanticAction::decrement, SemanticAction::increment, SemanticAction::set_value};
    }
    descriptor.exposed = true;
    return descriptor;
}

bool Splitter::on_semantic_action(SemanticAction action, std::string_view value) {
    const std::optional<double> current = split_position();
    if (!current || !enabled.read()) return false;
    if (action == SemanticAction::increment) {
        set_split_position(*current + semantic_step);
        return true;
    }
    if (action == SemanticAction::decrement) {
        set_split_position(*current - semantic_step);
        return true;
    }
    if (action != SemanticAction::set_value) return false;
    try {
        const std::string owned(value);
        std::size_t consumed{};
        const double requested = std::stod(owned, &consumed);
        if (consumed != owned.size() || !std::isfinite(requested)) return false;
        set_split_position(requested);
        return true;
    } catch (const std::exception&) {
        // A value that is not a number is refused.
        return false;
    }
}

namespace {

constexpr std::array<PartField<Splitter::Parts>, 2> splitter_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &Splitter::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &Splitter::Parts::face},
}};

constexpr std::array<StateField<Splitter::States>, 6> splitter_state_fields{{
    {.state = AnatomyState::normal, .member = &Splitter::States::normal},
    {.state = AnatomyState::hot, .member = &Splitter::States::hot},
    {.state = AnatomyState::pressed, .member = &Splitter::States::pressed},
    {.state = AnatomyState::captured, .member = &Splitter::States::captured},
    {.state = AnatomyState::enabled, .member = &Splitter::States::enabled},
    {.state = AnatomyState::disabled, .member = &Splitter::States::disabled},
}};

constexpr std::array<PartDeclaration, 2> splitter_parts = part_declarations(splitter_part_fields);
constexpr std::array<AnatomyState, 6> splitter_states = state_declarations(splitter_state_fields);

constexpr ControlAnatomy splitter_anatomy{
    .control_id = "splitter",
    .parts = splitter_parts,
    .states = splitter_states,
    .custom_states = {},
};

// The events Splitter adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 6> splitter_interactions{{
    {.name = "splitter_moving", .payload = "SplitterMoveEvent", .mutable_payload = true},
    {.name = "splitter_moved", .payload = "SplitterMoveEvent", .mutable_payload = false},
    {.name = "drag_started", .payload = "DragInteraction", .mutable_payload = false},
    {.name = "drag_target_changed", .payload = "DragInteraction", .mutable_payload = false},
    {.name = "drag_dropped", .payload = "DragInteraction", .mutable_payload = false},
    {.name = "drag_ended", .payload = "DragInteraction", .mutable_payload = false},
}};

} // namespace

const ControlAnatomy& Splitter::anatomy() const noexcept {
    return splitter_anatomy;
}

void Splitter::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = client_rectangle();
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    record.face = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), splitter_part_fields, record, geometry);
}

Splitter::Parts Splitter::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), splitter_part_fields, published.parts(), record);
    return record;
}

Splitter::States Splitter::states() const {
    States record;
    read_states(splitter_state_fields, anatomy_states(), record);
    return record;
}

void Splitter::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), splitter_interactions.begin(), splitter_interactions.end());
}

// property_codemod begin Splitter (tools/property_families/splitter.json; generated, do not edit)

void Splitter::prepare_write(const decltype(min_size)&, double& incoming) {
    require_mutable();
    require_extent(incoming, "Splitter min_size must be finite and not negative");
}

void Splitter::written(const decltype(min_size)&) {}

void Splitter::prepare_write(const decltype(min_extra)&, double& incoming) {
    require_mutable();
    require_extent(incoming, "Splitter min_extra must be finite and not negative");
}

void Splitter::written(const decltype(min_extra)&) {}

// property_codemod end Splitter

} // namespace gui_forms
