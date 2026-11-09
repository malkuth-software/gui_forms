// The drag engine (drag_engine.hpp): the plan builder, the evaluation
// kernel, the keyboard stepping and the session's state machine.

#include "drag_engine.hpp"

#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/surface_root/surface_root.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>

namespace gui_forms {
namespace {

constexpr double infinite_distance = std::numeric_limits<double>::infinity();

bool finite_point(Point point) noexcept {
    const bool finite = std::isfinite(point.x) && std::isfinite(point.y);
    return finite;
}

bool finite_rect(Rect rect) noexcept {
    const bool finite = std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
                        std::isfinite(rect.height) && rect.width >= 0.0 && rect.height >= 0.0;
    return finite;
}

Point centre_of(Rect rect) noexcept {
    const Point centre{rect.x + rect.width * 0.5, rect.y + rect.height * 0.5};
    return centre;
}

// Half-open containment: a point on a shared edge belongs to one target.
bool contains_point(Rect rect, Point point) noexcept {
    const bool inside = point.x >= rect.x && point.x < rect.x + rect.width && point.y >= rect.y &&
                        point.y < rect.y + rect.height;
    return inside;
}

double along(Point point, DragAxis axis) noexcept {
    const double value = axis == DragAxis::x ? point.x : point.y;
    return value;
}

double start_along(Rect rect, DragAxis axis) noexcept {
    const double value = axis == DragAxis::x ? rect.x : rect.y;
    return value;
}

double extent_along(Rect rect, DragAxis axis) noexcept {
    const double value = axis == DragAxis::x ? rect.width : rect.height;
    return value;
}

Rect placed_along(Rect rect, DragAxis axis, double start) noexcept {
    Rect placed = rect;
    if (axis == DragAxis::x) {
        placed.x = start;
    } else {
        placed.y = start;
    }
    return placed;
}

bool near_equal(double left, double right) noexcept {
    const bool equal = std::fabs(left - right) <= drag_tie_tolerance;
    return equal;
}

bool modifiers_held(Modifier held, Modifier wanted) noexcept {
    const std::uint8_t wanted_bits = static_cast<std::uint8_t>(wanted);
    const std::uint8_t held_bits = static_cast<std::uint8_t>(held);
    const bool all = wanted_bits != 0U && (held_bits & wanted_bits) == wanted_bits;
    return all;
}

// A projection's value for `raw`: clamped, then on the step grid.
double projected_value(const DragProjection& projection, double raw) noexcept {
    double value = std::clamp(raw, projection.minimum, projection.maximum);
    if (projection.step > 0.0) {
        const double steps = std::round((value - projection.minimum) / projection.step);
        value = projection.minimum + steps * projection.step;
        value = std::min(value, projection.maximum);
    }
    return value;
}

// The trace's number format: rounded to 1/64 unit, without trailing zeros.
void append_number(double value, std::string& out) {
    double rounded = std::round(value * 64.0) / 64.0;
    if (rounded == 0.0) rounded = 0.0;
    std::array<char, 64> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%.6f", rounded);
    if (written <= 0) {
        out += "nan";
        return;
    }
    std::size_t length = static_cast<std::size_t>(written) < buffer.size() ? static_cast<std::size_t>(written)
                                                                            : buffer.size() - 1U;
    while (length > 0U && buffer[length - 1U] == '0') --length;
    if (length > 0U && buffer[length - 1U] == '.') --length;
    out.append(buffer.data(), length);
}

void append_unsigned(std::uint64_t value, std::string& out) {
    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%llu", static_cast<unsigned long long>(value));
    if (written > 0) out.append(buffer.data(), static_cast<std::size_t>(written));
}

std::string_view trace_event_name(DragTraceEvent event) noexcept {
    switch (event) {
    case DragTraceEvent::arm: return "arm";
    case DragTraceEvent::plan: return "plan";
    case DragTraceEvent::move: return "move";
    case DragTraceEvent::key: return "key";
    case DragTraceEvent::chain: return "chain";
    case DragTraceEvent::end: return "end";
    default:
        break;
    }
    return "unknown";
}

} // namespace

// -- vocabulary -------------------------------------------------------------------

void DragAnnouncement::assign(std::string_view text) noexcept {
    std::size_t length = std::min(text.size(), bytes_.size());
    if (length < text.size()) {
        // Back off to the start of the character that would be cut.
        while (length > 0U && (static_cast<unsigned char>(text[length]) & 0xC0U) == 0x80U) --length;
    }
    std::copy_n(text.data(), length, bytes_.data());
    length_ = length;
}

std::string_view drag_end_name(DragEnd end) noexcept {
    switch (end) {
    case DragEnd::none: return "none";
    case DragEnd::dropped: return "dropped";
    case DragEnd::free: return "free";
    case DragEnd::rejected: return "rejected";
    case DragEnd::escaped: return "escaped";
    case DragEnd::capture_lost: return "capture_lost";
    case DragEnd::deactivated: return "deactivated";
    case DragEnd::model_changed: return "model_changed";
    case DragEnd::subject_gone: return "subject_gone";
    default:
        break;
    }
    return "unknown";
}

std::string_view drag_phase_name(DragPhase phase) noexcept {
    switch (phase) {
    case DragPhase::idle: return "idle";
    case DragPhase::armed: return "armed";
    case DragPhase::planning: return "planning";
    case DragPhase::dragging: return "dragging";
    case DragPhase::settling: return "settling";
    default:
        break;
    }
    return "unknown";
}

bool is_drag_move_command(const KeyEvent& event) noexcept {
    const Modifier wanted = Modifier::control | Modifier::alt | Modifier::shift;
    const bool command = event.action == KeyAction::down && event.physical_key == PhysicalKey::m &&
                         event.modifiers == wanted;
    return command;
}

void append_drag_trace_line(const DragTraceEntry& entry, std::string& out) {
    out += trace_event_name(entry.event);
    out += ' ';
    out += drag_phase_name(entry.phase);
    out += entry.mode == DragInputMode::keyboard ? " keyboard" : " pointer";
    out += " epoch=";
    append_unsigned(entry.epoch.value, out);
    out += " pointer=";
    append_number(entry.pointer.x, out);
    out += ',';
    append_number(entry.pointer.y, out);
    switch (entry.selection.kind) {
    case DragSelectionKind::candidate:
        out += " candidate=";
        append_unsigned(entry.selection.token.value, out);
        out += " landing=";
        append_number(entry.landing.x, out);
        out += ',';
        append_number(entry.landing.y, out);
        out += ',';
        append_number(entry.landing.width, out);
        out += ',';
        append_number(entry.landing.height, out);
        break;
    case DragSelectionKind::value:
        out += " value=";
        append_number(entry.selection.value, out);
        break;
    case DragSelectionKind::point:
        out += " point=";
        append_number(entry.selection.point.x, out);
        out += ',';
        append_number(entry.selection.point.y, out);
        break;
    case DragSelectionKind::none:
    default:
        out += " none";
        break;
    }
    if (entry.end != DragEnd::none) {
        out += " end=";
        out += drag_end_name(entry.end);
    }
    out += " ties=";
    append_unsigned(entry.ties, out);
    out += '\n';
}

// -- the client's defaults ---------------------------------------------------------

bool DragClient::accepts_drag_candidate(const DragLanding&) { return true; }
void DragClient::announce_drag(const DragFrame&, DragAnnouncement&) {}
void DragClient::rewrite_drag_outcome(DragOutcome&) {}
void DragClient::commit_drag_chain(const DragOutcome&) {}

// -- the plan builder --------------------------------------------------------------

void DragPlanBuilder::refuse(DragPlanRefusal refusal) noexcept {
    if (refusal_ == DragPlanRefusal::none) refusal_ = refusal;
}

bool DragPlanBuilder::admit_operator() noexcept {
    if (refusal_ != DragPlanRefusal::none) return false;
    if ((*engine_).operator_count_ >= maximum_drag_operators) {
        refuse(DragPlanRefusal::too_many_operators);
        return false;
    }
    return true;
}

bool DragPlanBuilder::valid_child(DragNodeIndex child) const noexcept {
    const bool valid = !(child == no_drag_node) && child.value < (*engine_).operator_count_;
    return valid;
}

bool DragPlanBuilder::admit_range(const DragCandidateRange& range) noexcept {
    const DragEngine& engine = *engine_;
    for (std::size_t index = 0U; index < engine.added_count_; ++index) {
        const DragCandidateRange& added = engine.added_[index];
        if (added.first == range.first && added.count == range.count) return true;
    }
    refuse(DragPlanRefusal::bad_value);
    return false;
}

DragNodeIndex DragPlanBuilder::append(const DragOperator& record) {
    DragEngine& engine = *engine_;
    const std::size_t index = engine.operator_count_;
    engine.operators_[index] = record;
    engine.operator_count_ = index + 1U;
    DragNodeIndex made{};
    made.value = static_cast<std::uint8_t>(index);
    return made;
}

DragCandidateRange DragPlanBuilder::store(std::span<const DragLanding> landings) {
    DragCandidateRange range{};
    if (refusal_ != DragPlanRefusal::none) return range;
    DragEngine& engine = *engine_;
    if (engine.added_count_ >= engine.added_.size()) {
        refuse(DragPlanRefusal::too_many_operators);
        return range;
    }
    const std::size_t present = engine.landings_.size();
    if (landings.size() > maximum_drag_candidates - present) {
        refuse(DragPlanRefusal::too_many_candidates);
        return range;
    }
    for (const DragLanding& landing : landings) {
        if (!finite_rect(landing.rect) || !finite_point(landing.anchor)) {
            refuse(DragPlanRefusal::bad_value);
            return range;
        }
        if (landing.group.value >= maximum_drag_groups) {
            refuse(DragPlanRefusal::too_many_groups);
            return range;
        }
    }
    // Within the capacity reserved at the first planning: no growth.
    for (const DragLanding& landing : landings) engine.landings_.push_back(landing);
    range.first = static_cast<std::uint32_t>(present);
    range.count = static_cast<std::uint32_t>(landings.size());
    engine.added_[engine.added_count_] = range;
    ++engine.added_count_;
    return range;
}

DragCandidateRange DragPlanBuilder::add_landings(std::span<const DragLanding> landings) {
    const DragCandidateRange range = store(landings);
    return range;
}

DragCandidateRange DragPlanBuilder::add_list(const DragListGeometry& list) {
    DragCandidateRange range{};
    if (refusal_ != DragPlanRefusal::none) return range;
    const std::size_t count = list.slots.size();
    if (count == 0U || list.subject >= count || list.group.value >= maximum_drag_groups) {
        refuse(DragPlanRefusal::bad_value);
        return range;
    }
    DragEngine& engine = *engine_;
    if (count > maximum_drag_candidates - engine.landings_.size()) {
        refuse(DragPlanRefusal::too_many_candidates);
        return range;
    }
    if (engine.added_count_ >= engine.added_.size()) {
        refuse(DragPlanRefusal::too_many_operators);
        return range;
    }
    for (const Rect& slot : list.slots) {
        if (!finite_rect(slot)) {
            refuse(DragPlanRefusal::bad_value);
            return range;
        }
    }
    const Rect subject = list.slots[list.subject];
    const double extent = extent_along(subject, list.axis);
    const std::size_t present = engine.landings_.size();
    for (std::size_t slot = 0U; slot < count; ++slot) {
        const Rect place = list.slots[slot];
        // Moving the item backward puts it where the slot starts; forward,
        // the items between move back by its extent, so it ends where the
        // slot ends.
        const double place_start = start_along(place, list.axis);
        const double place_end = place_start + extent_along(place, list.axis);
        const double start = slot <= list.subject ? place_start : place_end - extent;
        DragLanding landing{};
        landing.rect = placed_along(subject, list.axis, start);
        landing.anchor = centre_of(landing.rect);
        landing.token.value = list.first_token + static_cast<std::uint32_t>(slot);
        landing.group = list.group;
        landing.home = slot == list.subject;
        engine.landings_.push_back(landing);
    }
    range.first = static_cast<std::uint32_t>(present);
    range.count = static_cast<std::uint32_t>(count);
    range.axis = list.axis;
    // Equal extents at a constant positive pitch put the anchors at that
    // pitch too, so a nearest operator along the axis can index them.
    if (count >= 2U) {
        const double first_start = start_along(list.slots[0], list.axis);
        const double pitch = start_along(list.slots[1], list.axis) - first_start;
        bool uniform = pitch > 0.0;
        for (std::size_t slot = 0U; slot < count && uniform; ++slot) {
            const double expected = first_start + static_cast<double>(slot) * pitch;
            const bool placed = near_equal(start_along(list.slots[slot], list.axis), expected);
            const bool sized = near_equal(extent_along(list.slots[slot], list.axis), extent);
            uniform = placed && sized;
        }
        range.uniform = uniform;
        range.origin = first_start + extent * 0.5;
        range.pitch = pitch;
    }
    engine.added_[engine.added_count_] = range;
    ++engine.added_count_;
    return range;
}

DragNodeIndex DragPlanBuilder::nearest(const DragCandidateRange& range, DragMetric metric, double stickiness) {
    if (!admit_operator() || !admit_range(range)) return no_drag_node;
    if (!(stickiness >= 0.0) || !std::isfinite(stickiness)) {
        refuse(DragPlanRefusal::bad_value);
        return no_drag_node;
    }
    DragOperator record{};
    record.kind = DragOperatorKind::nearest;
    record.range = range;
    record.metric = metric;
    record.distance = stickiness;
    const DragNodeIndex made = append(record);
    return made;
}

DragNodeIndex DragPlanBuilder::target(const DragCandidateRange& range) {
    if (!admit_operator() || !admit_range(range)) return no_drag_node;
    DragOperator record{};
    record.kind = DragOperatorKind::target;
    record.range = range;
    const DragNodeIndex made = append(record);
    return made;
}

DragNodeIndex DragPlanBuilder::foreign_targets(std::span<const DragLanding> landings) {
    if (!admit_operator()) return no_drag_node;
    const DragCandidateRange range = store(landings);
    if (refusal_ != DragPlanRefusal::none) return no_drag_node;
    DragOperator record{};
    record.kind = DragOperatorKind::target;
    record.range = range;
    record.foreign = true;
    const DragNodeIndex made = append(record);
    return made;
}

DragNodeIndex DragPlanBuilder::project(const DragProjection& projection) {
    if (!admit_operator()) return no_drag_node;
    const bool finite = std::isfinite(projection.origin_value) && std::isfinite(projection.scale) &&
                        std::isfinite(projection.minimum) && std::isfinite(projection.maximum) &&
                        std::isfinite(projection.step) && std::isfinite(projection.keyboard_step);
    if (!finite || projection.scale == 0.0 || projection.minimum > projection.maximum || projection.step < 0.0 ||
        projection.keyboard_step <= 0.0) {
        refuse(DragPlanRefusal::bad_value);
        return no_drag_node;
    }
    if (projection.group.value >= maximum_drag_groups) {
        refuse(DragPlanRefusal::too_many_groups);
        return no_drag_node;
    }
    DragOperator record{};
    record.kind = DragOperatorKind::project;
    record.projection = projection;
    const DragNodeIndex made = append(record);
    return made;
}

DragNodeIndex DragPlanBuilder::free(const DragFreeArea& area) {
    if (!admit_operator()) return no_drag_node;
    const bool grid = std::isfinite(area.grid.width) && std::isfinite(area.grid.height) && area.grid.width >= 0.0 &&
                      area.grid.height >= 0.0;
    if (!finite_rect(area.bounds) || !grid) {
        refuse(DragPlanRefusal::bad_value);
        return no_drag_node;
    }
    if (area.group.value >= maximum_drag_groups) {
        refuse(DragPlanRefusal::too_many_groups);
        return no_drag_node;
    }
    DragOperator record{};
    record.kind = DragOperatorKind::free;
    record.free_area = area;
    const DragNodeIndex made = append(record);
    return made;
}

DragNodeIndex DragPlanBuilder::fallback(DragNodeIndex primary, DragNodeIndex secondary, double gap) {
    if (!admit_operator()) return no_drag_node;
    if (!valid_child(primary) || !valid_child(secondary)) {
        refuse(DragPlanRefusal::bad_child);
        return no_drag_node;
    }
    if (!(gap >= 0.0)) {
        refuse(DragPlanRefusal::bad_value);
        return no_drag_node;
    }
    DragOperator record{};
    record.kind = DragOperatorKind::fallback;
    record.primary = primary;
    record.secondary = secondary;
    record.distance = gap;
    const DragNodeIndex made = append(record);
    return made;
}

DragNodeIndex DragPlanBuilder::snap(DragNodeIndex child, double radius, DragChain chain) {
    if (!admit_operator()) return no_drag_node;
    if (!valid_child(child)) {
        refuse(DragPlanRefusal::bad_child);
        return no_drag_node;
    }
    if (!(radius >= 0.0)) {
        refuse(DragPlanRefusal::bad_value);
        return no_drag_node;
    }
    DragOperator record{};
    record.kind = DragOperatorKind::snap;
    record.primary = child;
    record.distance = radius;
    record.chain = chain;
    const DragNodeIndex made = append(record);
    return made;
}

DragNodeIndex DragPlanBuilder::suppress(DragNodeIndex child, Modifier suppressed_by) {
    if (!admit_operator()) return no_drag_node;
    if (!valid_child(child)) {
        refuse(DragPlanRefusal::bad_child);
        return no_drag_node;
    }
    DragOperator record{};
    record.kind = DragOperatorKind::suppress;
    record.primary = child;
    record.suppressed_by = suppressed_by;
    const DragNodeIndex made = append(record);
    return made;
}

void DragPlanBuilder::group_container(DragGroup group, Control& container) {
    if (refusal_ != DragPlanRefusal::none) return;
    if (group.value >= maximum_drag_groups) {
        refuse(DragPlanRefusal::too_many_groups);
        return;
    }
    (*engine_).containers_[group.value] = container.weak_from_this();
}

void DragPlanBuilder::drop_motion(bool settles) noexcept {
    if (refusal_ != DragPlanRefusal::none) return;
    (*engine_).settles_ = settles;
}

DragPlanRefusal DragPlanBuilder::finish(DragNodeIndex root) {
    if (refusal_ != DragPlanRefusal::none) return refusal_;
    if (!valid_child(root)) {
        refuse(DragPlanRefusal::empty);
        return refusal_;
    }
    (*engine_).root_node_ = root;
    return refusal_;
}

// -- the engine: storage and queries -------------------------------------------------

DragEngine::DragEngine(SurfaceRoot& root) noexcept : root_(&root) {}

void DragEngine::reserve_storage() {
    if (reserved_) return;
    landings_.reserve(maximum_drag_candidates);
    reachable_.reserve(maximum_drag_candidates);
    trace_.reserve(drag_trace_capacity);
    reserved_ = true;
}

void DragEngine::note_growth(std::size_t landings_capacity, std::size_t flags_capacity,
                             std::size_t trace_capacity) noexcept {
    if (landings_.capacity() != landings_capacity) ++statistics_.storage_growths;
    if (reachable_.capacity() != flags_capacity) ++statistics_.storage_growths;
    if (trace_.capacity() != trace_capacity) ++statistics_.storage_growths;
}

std::span<const DragLanding> DragEngine::landings() const noexcept {
    const std::span<const DragLanding> view(landings_.data(), landings_.size());
    return view;
}

std::span<const DragOperator> DragEngine::operators() const noexcept {
    const std::span<const DragOperator> view(operators_.data(), operator_count_);
    return view;
}

bool DragEngine::keyboard_reachable(std::size_t index) const noexcept {
    const bool reachable = index < reachable_.size() && reachable_[index] != 0U;
    return reachable;
}

bool DragEngine::runs(const DragClient& client) const noexcept {
    const bool running = phase_ != DragPhase::idle && client_ == &client;
    return running;
}

bool DragEngine::drags(const Control& control) const noexcept {
    if (phase_ != DragPhase::planning && phase_ != DragPhase::dragging && phase_ != DragPhase::settling) return false;
    const Control::Ptr subject = subject_.lock();
    if (!subject) return false;
    const bool dragging = subject.get() == &control || control.contains(*subject);
    return dragging;
}

bool DragEngine::drop_candidate(const Control& control) const noexcept {
    if (phase_ != DragPhase::dragging || selection_.kind == DragSelectionKind::none) return false;
    if (selection_.group.value >= maximum_drag_groups) return false;
    const Control::Ptr container = containers_[selection_.group.value].lock();
    const bool candidate = container && container.get() == &control;
    return candidate;
}

void DragEngine::copy_trace(std::vector<DragTraceEntry>& out) const {
    out.clear();
    if (trace_.size() < drag_trace_capacity) {
        out.assign(trace_.begin(), trace_.end());
        return;
    }
    out.reserve(trace_.size());
    for (std::size_t offset = 0U; offset < trace_.size(); ++offset) {
        const std::size_t index = (trace_next_ + offset) % trace_.size();
        out.push_back(trace_[index]);
    }
}

void DragEngine::clear_trace() noexcept {
    trace_.clear();
    trace_next_ = 0U;
}

void DragEngine::record(DragTraceEvent event, DragEnd ending) noexcept {
    DragTraceEntry entry{};
    ++trace_sequence_;
    entry.sequence = trace_sequence_;
    entry.event = event;
    entry.phase = phase_;
    entry.mode = mode_;
    entry.epoch = epoch_;
    entry.pointer = pointer_;
    entry.selection = selection_;
    if (selection_.kind == DragSelectionKind::candidate && selection_.landing < landings_.size()) {
        entry.landing = landings_[selection_.landing].rect;
    }
    entry.end = ending;
    entry.ties = statistics_.ties;
    if (trace_.capacity() < drag_trace_capacity) return;
    if (trace_.size() < drag_trace_capacity) {
        // Within the reserved capacity: no growth.
        trace_.push_back(entry);
        return;
    }
    trace_[trace_next_] = entry;
    trace_next_ = (trace_next_ + 1U) % drag_trace_capacity;
}

// -- the engine: evaluation ------------------------------------------------------------

Point DragEngine::adjusted(Point pointer) const noexcept {
    // The pointer moved to where the subject's centre would be, keeping the
    // grab: a press off-centre still selects the home landing.
    const Point centre = centre_of(source_);
    const Point moved{pointer.x - (press_.x - centre.x), pointer.y - (press_.y - centre.y)};
    return moved;
}

double DragEngine::distance_to(const DragLanding& landing, Point adjusted_pointer, DragMetric metric) const noexcept {
    switch (metric) {
    case DragMetric::axis_x: {
        const double distance = std::fabs(adjusted_pointer.x - landing.anchor.x);
        return distance;
    }
    case DragMetric::axis_y: {
        const double distance = std::fabs(adjusted_pointer.y - landing.anchor.y);
        return distance;
    }
    case DragMetric::edge: {
        const Rect rect = landing.rect;
        const double dx = std::max({rect.x - adjusted_pointer.x, 0.0, adjusted_pointer.x - (rect.x + rect.width)});
        const double dy = std::max({rect.y - adjusted_pointer.y, 0.0, adjusted_pointer.y - (rect.y + rect.height)});
        const double distance = std::hypot(dx, dy);
        return distance;
    }
    case DragMetric::euclidean:
    default:
        break;
    }
    const double dx = adjusted_pointer.x - landing.anchor.x;
    const double dy = adjusted_pointer.y - landing.anchor.y;
    const double distance = std::hypot(dx, dy);
    return distance;
}

DragEngine::Evaluation DragEngine::evaluate_nearest(const DragOperator& record, Point adjusted_pointer,
                                                    bool sticky) const noexcept {
    Evaluation result{};
    const DragCandidateRange range = record.range;
    if (range.count == 0U) return result;
    std::size_t low = range.first;
    std::size_t high = static_cast<std::size_t>(range.first) + range.count;
    const bool indexed = range.uniform && ((record.metric == DragMetric::axis_x && range.axis == DragAxis::x) ||
                                           (record.metric == DragMetric::axis_y && range.axis == DragAxis::y));
    if (indexed) {
        // The producer's inverse: the slot nearest the pointer along the
        // axis, then its neighbours, scanned in order so that a tie still
        // goes to the earlier one.
        const double offset = (along(adjusted_pointer, range.axis) - range.origin) / range.pitch;
        const double last = static_cast<double>(range.count - 1U);
        const double clamped = std::clamp(std::round(offset), 0.0, last);
        const std::size_t slot = static_cast<std::size_t>(clamped);
        const std::size_t first_slot = slot > 0U ? slot - 1U : 0U;
        const std::size_t end_slot = std::min<std::size_t>(slot + 2U, range.count);
        low = range.first + first_slot;
        high = range.first + end_slot;
    }
    double best = infinite_distance;
    std::size_t best_index = low;
    bool tied = false;
    for (std::size_t index = low; index < high; ++index) {
        const double distance = distance_to(landings_[index], adjusted_pointer, record.metric);
        if (distance < best - drag_tie_tolerance) {
            best = distance;
            best_index = index;
            tied = false;
        } else if (near_equal(distance, best)) {
            tied = true;
        }
    }
    const std::size_t range_end = static_cast<std::size_t>(range.first) + range.count;
    const bool current_here = sticky && record.distance > 0.0 && selection_.kind == DragSelectionKind::candidate &&
                              selection_.landing >= range.first && selection_.landing < range_end;
    if (current_here) {
        const double current = distance_to(landings_[selection_.landing], adjusted_pointer, record.metric);
        if (current - record.distance <= best) {
            best = current;
            best_index = selection_.landing;
            tied = false;
        }
    }
    const DragLanding& chosen = landings_[best_index];
    result.selection.kind = DragSelectionKind::candidate;
    result.selection.landing = static_cast<std::uint32_t>(best_index);
    result.selection.token = chosen.token;
    result.selection.group = chosen.group;
    result.selection.distance = best;
    result.tied = tied;
    return result;
}

DragEngine::Evaluation DragEngine::evaluate_target(const DragOperator& record, Point pointer) const noexcept {
    Evaluation result{};
    const std::size_t end_index = static_cast<std::size_t>(record.range.first) + record.range.count;
    for (std::size_t index = record.range.first; index < end_index; ++index) {
        if (!contains_point(landings_[index].rect, pointer)) continue;
        if (result.selection.kind == DragSelectionKind::candidate) {
            // Overlapping targets: the earlier one stays.
            result.tied = true;
            break;
        }
        const DragLanding& hit = landings_[index];
        result.selection.kind = DragSelectionKind::candidate;
        result.selection.landing = static_cast<std::uint32_t>(index);
        result.selection.token = hit.token;
        result.selection.group = hit.group;
        result.selection.distance = 0.0;
    }
    return result;
}

DragSelection DragEngine::evaluate_project(const DragOperator& record, Point pointer) const noexcept {
    const DragProjection& projection = record.projection;
    const double moved = along(pointer, projection.axis) - along(press_, projection.axis);
    const double raw = projection.origin_value + projection.scale * moved;
    DragSelection selection{};
    selection.kind = DragSelectionKind::value;
    selection.value = projected_value(projection, raw);
    selection.group = projection.group;
    selection.distance = 0.0;
    return selection;
}

DragSelection DragEngine::evaluate_free(const DragOperator& record, Point pointer) const noexcept {
    const DragFreeArea& area = record.free_area;
    const double right_limit = std::max(area.bounds.x, area.bounds.x + area.bounds.width - source_.width);
    const double bottom_limit = std::max(area.bounds.y, area.bounds.y + area.bounds.height - source_.height);
    double x = std::clamp(source_.x + (pointer.x - press_.x), area.bounds.x, right_limit);
    double y = std::clamp(source_.y + (pointer.y - press_.y), area.bounds.y, bottom_limit);
    if (area.grid.width > 0.0) {
        x = area.bounds.x + std::round((x - area.bounds.x) / area.grid.width) * area.grid.width;
        x = std::clamp(x, area.bounds.x, right_limit);
    }
    if (area.grid.height > 0.0) {
        y = area.bounds.y + std::round((y - area.bounds.y) / area.grid.height) * area.grid.height;
        y = std::clamp(y, area.bounds.y, bottom_limit);
    }
    DragSelection selection{};
    selection.kind = DragSelectionKind::point;
    selection.point = Point{x, y};
    selection.group = area.group;
    selection.distance = 0.0;
    return selection;
}

// Children precede their parent (the builder's rule), so the recursion is
// acyclic and at most maximum_drag_operators deep.
DragEngine::Evaluation DragEngine::evaluate(DragNodeIndex node, Point pointer, Modifier modifiers,
                                            bool sticky) const noexcept {
    Evaluation result{};
    if (node == no_drag_node || node.value >= operator_count_) return result;
    const DragOperator& record = operators_[node.value];
    switch (record.kind) {
    case DragOperatorKind::nearest:
        result = evaluate_nearest(record, adjusted(pointer), sticky);
        break;
    case DragOperatorKind::target:
        result = evaluate_target(record, pointer);
        break;
    case DragOperatorKind::project:
        result.selection = evaluate_project(record, pointer);
        break;
    case DragOperatorKind::free:
        result.selection = evaluate_free(record, pointer);
        break;
    case DragOperatorKind::fallback: {
        const Evaluation primary = evaluate(record.primary, pointer, modifiers, sticky);
        if (primary.selection.kind != DragSelectionKind::none && primary.selection.distance <= record.distance) {
            return primary;
        }
        result = evaluate(record.secondary, pointer, modifiers, sticky);
        return result;
    }
    case DragOperatorKind::snap: {
        const Evaluation child = evaluate(record.primary, pointer, modifiers, sticky);
        if (child.selection.kind != DragSelectionKind::candidate || child.selection.distance > record.distance) {
            return result;
        }
        result = child;
        result.chain = record.chain == DragChain::on && !landings_[child.selection.landing].home;
        return result;
    }
    case DragOperatorKind::suppress:
        if (modifiers_held(modifiers, record.suppressed_by)) return result;
        result = evaluate(record.primary, pointer, modifiers, sticky);
        return result;
    case DragOperatorKind::none:
    default:
        // The builder makes no other kind; an unknown record selects nothing.
        return result;
    }
    if (result.selection.decided_by == no_drag_node) result.selection.decided_by = node;
    return result;
}

DragSelection DragEngine::probe(Point pointer, Modifier modifiers) const noexcept {
    if (phase_ == DragPhase::idle || root_node_ == no_drag_node) {
        const DragSelection nothing{};
        return nothing;
    }
    const Evaluation evaluation = evaluate(root_node_, pointer, modifiers, false);
    return evaluation.selection;
}

bool DragEngine::sample_drop_zones(Rect area, Size cell, std::span<std::int32_t> out) const noexcept {
    if (!(cell.width > 0.0) || !(cell.height > 0.0) || !finite_rect(area)) return false;
    const double column_count = std::ceil(area.width / cell.width);
    const double row_count = std::ceil(area.height / cell.height);
    // The map is a diagnostic: refuse anything past a million cells rather
    // than overflow the count.
    if (column_count * row_count > 1.0e6) return false;
    const std::size_t columns = static_cast<std::size_t>(column_count);
    const std::size_t rows = static_cast<std::size_t>(row_count);
    if (columns * rows != out.size()) return false;
    for (std::size_t row = 0U; row < rows; ++row) {
        for (std::size_t column = 0U; column < columns; ++column) {
            const Point sample{area.x + (static_cast<double>(column) + 0.5) * cell.width,
                               area.y + (static_cast<double>(row) + 0.5) * cell.height};
            const DragSelection selection = probe(sample, Modifier::none);
            const std::int32_t written = selection.kind == DragSelectionKind::candidate
                                             ? static_cast<std::int32_t>(selection.landing)
                                             : -1;
            out[row * columns + column] = written;
        }
    }
    return true;
}

Point DragEngine::pointer_for(const DragSelection& selection) const noexcept {
    switch (selection.kind) {
    case DragSelectionKind::candidate: {
        const Point anchor = landings_[selection.landing].anchor;
        const Point centre = centre_of(source_);
        const Point pointer{anchor.x + (press_.x - centre.x), anchor.y + (press_.y - centre.y)};
        return pointer;
    }
    case DragSelectionKind::value: {
        const DragNodeIndex node = find_operator(DragOperatorKind::project);
        if (node == no_drag_node) return press_;
        const DragProjection& projection = operators_[node.value].projection;
        const double moved = (selection.value - projection.origin_value) / projection.scale;
        Point pointer = press_;
        if (projection.axis == DragAxis::x) {
            pointer.x += moved;
        } else {
            pointer.y += moved;
        }
        return pointer;
    }
    case DragSelectionKind::point: {
        const Point pointer{press_.x + (selection.point.x - source_.x), press_.y + (selection.point.y - source_.y)};
        return pointer;
    }
    case DragSelectionKind::none:
    default:
        break;
    }
    return pointer_;
}

DragNodeIndex DragEngine::find_operator(DragOperatorKind kind) const noexcept {
    if (root_node_ == no_drag_node) return no_drag_node;
    // A depth-first walk from the root with a fixed stack: every node is
    // pushed at most once per parent, and there are at most
    // maximum_drag_operators nodes with two children each.
    std::array<DragNodeIndex, maximum_drag_operators * 2U> pending{};
    std::size_t depth = 0U;
    pending[depth] = root_node_;
    ++depth;
    while (depth > 0U) {
        --depth;
        const DragNodeIndex node = pending[depth];
        if (node == no_drag_node || node.value >= operator_count_) continue;
        const DragOperator& record = operators_[node.value];
        if (record.kind == kind) return node;
        if (!(record.secondary == no_drag_node) && depth < pending.size()) {
            pending[depth] = record.secondary;
            ++depth;
        }
        if (!(record.primary == no_drag_node) && depth < pending.size()) {
            pending[depth] = record.primary;
            ++depth;
        }
    }
    return no_drag_node;
}

// -- the engine: sessions ------------------------------------------------------------

bool DragEngine::client_alive() const noexcept {
    if (client_ == nullptr) return false;
    const Control::Ptr owner = owner_.lock();
    const bool alive = owner && (*owner).is_alive();
    return alive;
}

bool DragEngine::still(std::uint64_t serial) const noexcept {
    const bool running = serial == serial_ && phase_ != DragPhase::idle;
    return running;
}

void DragEngine::reset_session() noexcept {
    client_ = nullptr;
    owner_.reset();
    subject_.reset();
    for (Control::WeakPtr& container : containers_) container.reset();
    settles_ = false;
}

bool DragEngine::start_session(DragClient& client, Control& owner, Control& subject, std::uint64_t key,
                               DragInputMode mode, Point press, Rect source) {
    if (phase_ != DragPhase::idle) return false;
    if (subject.surface_root() != root_ || !finite_point(press) || !finite_rect(source)) return false;
    reserve_storage();
    ++serial_;
    ++statistics_.sessions;
    client_ = &client;
    owner_ = owner.weak_from_this();
    subject_ = subject.weak_from_this();
    key_ = key;
    mode_ = mode;
    press_ = press;
    source_ = source;
    pointer_ = press;
    modifiers_ = Modifier::none;
    epoch_ = DragEpoch{};
    selection_ = DragSelection{};
    announcement_.clear();
    operator_count_ = 0U;
    root_node_ = no_drag_node;
    landings_.clear();
    reachable_.clear();
    added_count_ = 0U;
    settles_ = false;
    return true;
}

bool DragEngine::arm(DragClient& client, Control& owner, Control& subject, std::uint64_t key, Point press,
                     Rect source, double threshold) {
    if (!std::isfinite(threshold)) return false;
    if (!start_session(client, owner, subject, key, DragInputMode::pointer, press, source)) return false;
    threshold_ = threshold;
    phase_ = DragPhase::armed;
    subject.set_pointer_capture(true);
    record(DragTraceEvent::arm);
    if (threshold > 0.0) return true;
    const bool planned = plan();
    return planned;
}

bool DragEngine::begin_keyboard(DragClient& client, Control& owner, Control& subject, std::uint64_t key) {
    const Rect source = subject.absolute_bounds();
    const Point centre = centre_of(source);
    if (!start_session(client, owner, subject, key, DragInputMode::keyboard, centre, source)) return false;
    const std::uint64_t serial = serial_;
    if (!plan()) return false;
    if (!still(serial)) return false;
    pointer_ = pointer_for(selection_);
    const DragFrame frame = frame_now();
    announce(frame);
    return still(serial);
}

void DragEngine::compact_refused() {
    const std::uint64_t serial = serial_;
    std::array<DragCandidateRange, maximum_drag_operators> kept{};
    std::size_t write = 0U;
    // The added ranges are disjoint and ascending, so compacting them in
    // order never overwrites a landing not yet read.
    for (std::size_t index = 0U; index < added_count_; ++index) {
        const DragCandidateRange original = added_[index];
        DragCandidateRange compacted = original;
        compacted.first = static_cast<std::uint32_t>(write);
        const std::size_t end_index = static_cast<std::size_t>(original.first) + original.count;
        bool removed = false;
        for (std::size_t read = original.first; read < end_index; ++read) {
            const DragLanding landing = landings_[read];
            const bool accepted = (*client_).accepts_drag_candidate(landing);
            if (!still(serial)) return;
            if (!accepted) {
                ++statistics_.refused_candidates;
                removed = true;
                continue;
            }
            landings_[write] = landing;
            ++write;
        }
        compacted.count = static_cast<std::uint32_t>(write - compacted.first);
        if (removed) compacted.uniform = false;
        kept[index] = compacted;
    }
    // Shrinking keeps the capacity.
    landings_.resize(write);
    for (std::size_t node = 0U; node < operator_count_; ++node) {
        DragOperator& record = operators_[node];
        if (record.kind != DragOperatorKind::nearest && record.kind != DragOperatorKind::target) continue;
        for (std::size_t index = 0U; index < added_count_; ++index) {
            if (added_[index].first == record.range.first && added_[index].count == record.range.count) {
                record.range = kept[index];
                break;
            }
        }
    }
    for (std::size_t index = 0U; index < added_count_; ++index) added_[index] = kept[index];
}

void DragEngine::mark_keyboard_reachable() {
    // Within the reserved capacity: no growth.
    reachable_.assign(landings_.size(), 0U);
    const Point centre = centre_of(source_);
    const Point grab{press_.x - centre.x, press_.y - centre.y};
    for (std::size_t index = 0U; index < landings_.size(); ++index) {
        // A pointer at the anchor (a declared target's own place) or at the
        // anchor plus the grab (where a nearest operator compares).
        const Point anchor = landings_[index].anchor;
        const Point grabbed{anchor.x + grab.x, anchor.y + grab.y};
        const Evaluation at_anchor = evaluate(root_node_, anchor, Modifier::none, false);
        const Evaluation at_grab = evaluate(root_node_, grabbed, Modifier::none, false);
        const bool by_anchor = at_anchor.selection.kind == DragSelectionKind::candidate &&
                               at_anchor.selection.landing == index;
        const bool by_grab = at_grab.selection.kind == DragSelectionKind::candidate &&
                             at_grab.selection.landing == index;
        if (by_anchor || by_grab) reachable_[index] = 1U;
    }
}

void DragEngine::choose_keyboard_start() {
    DragSelection start{};
    std::size_t first_reachable = landings_.size();
    for (std::size_t index = 0U; index < landings_.size(); ++index) {
        if (reachable_[index] == 0U) continue;
        if (first_reachable == landings_.size()) first_reachable = index;
        if (landings_[index].home) {
            first_reachable = index;
            break;
        }
    }
    if (first_reachable < landings_.size()) {
        const DragLanding& landing = landings_[first_reachable];
        start.kind = DragSelectionKind::candidate;
        start.landing = static_cast<std::uint32_t>(first_reachable);
        start.token = landing.token;
        start.group = landing.group;
        selection_ = start;
        return;
    }
    const DragNodeIndex projection = find_operator(DragOperatorKind::project);
    if (!(projection == no_drag_node)) {
        const DragProjection& record = operators_[projection.value].projection;
        start.kind = DragSelectionKind::value;
        start.value = projected_value(record, record.origin_value);
        start.group = record.group;
        start.decided_by = projection;
        selection_ = start;
        return;
    }
    const DragNodeIndex area = find_operator(DragOperatorKind::free);
    if (!(area == no_drag_node)) {
        start = evaluate_free(operators_[area.value], press_);
        start.decided_by = area;
    }
    selection_ = start;
}

bool DragEngine::plan() {
    phase_ = DragPhase::planning;
    ++epoch_.value;
    operator_count_ = 0U;
    root_node_ = no_drag_node;
    landings_.clear();
    reachable_.clear();
    added_count_ = 0U;
    settles_ = false;
    for (Control::WeakPtr& container : containers_) container.reset();
    const std::uint64_t serial = serial_;
    const Control::Ptr subject = subject_.lock();
    if (!subject || !client_alive()) {
        end(DragEnd::subject_gone);
        return false;
    }
    DragSubject declared{};
    declared.control = subject.get();
    declared.key = key_;
    declared.press = press_;
    declared.source = source_;
    declared.mode = mode_;
    declared.epoch = epoch_;
    DragPlanBuilder builder(*this);
    DragPlanRefusal refusal = (*client_).plan_drag(builder, declared);
    if (!still(serial)) return false;
    ++statistics_.plans;
    if (refusal == DragPlanRefusal::none) refusal = builder.refusal();
    if (refusal == DragPlanRefusal::none && root_node_ == no_drag_node) refusal = DragPlanRefusal::empty;
    if (refusal != DragPlanRefusal::none) {
        ++statistics_.refused_plans;
        end(DragEnd::rejected);
        return false;
    }
    compact_refused();
    if (!still(serial)) return false;
    if (mode_ == DragInputMode::keyboard) {
        mark_keyboard_reachable();
        choose_keyboard_start();
    } else {
        const Evaluation start = evaluate(root_node_, pointer_, modifiers_, false);
        selection_ = start.selection;
    }
    phase_ = DragPhase::dragging;
    record(DragTraceEvent::plan);
    return true;
}

DragFrame DragEngine::frame_now() const noexcept {
    DragFrame frame{};
    frame.phase = phase_;
    frame.mode = mode_;
    frame.epoch = epoch_;
    frame.pointer = pointer_;
    frame.modifiers = modifiers_;
    frame.selection = selection_;
    if (selection_.kind == DragSelectionKind::candidate && selection_.landing < landings_.size()) {
        frame.landing = landings_[selection_.landing].rect;
    }
    return frame;
}

void DragEngine::announce(const DragFrame& frame) {
    if (!client_alive()) return;
    (*client_).announce_drag(frame, announcement_);
}

void DragEngine::select(const DragSelection& selection, DragTraceEvent event) {
    const bool changed = !(selection == selection_);
    const bool continuous = selection.kind == DragSelectionKind::value || selection.kind == DragSelectionKind::point;
    selection_ = selection;
    record(event);
    if (!changed && !continuous) return;
    const std::uint64_t serial = serial_;
    const DragFrame frame = frame_now();
    if (client_alive()) (*client_).preview_drag(frame);
    if (!still(serial)) return;
    if (changed) announce(frame);
}

void DragEngine::drag_to(Point pointer, Modifier modifiers, DragTraceEvent event) {
    pointer_ = pointer;
    modifiers_ = modifiers;
    const Evaluation evaluation = evaluate(root_node_, pointer, modifiers, true);
    ++statistics_.evaluations;
    if (evaluation.tied) ++statistics_.ties;
    const bool chains = evaluation.chain && !(evaluation.selection.token == selection_.token &&
                                              selection_.kind == DragSelectionKind::candidate);
    if (!chains) {
        select(evaluation.selection, event);
        return;
    }
    // A chain point: commit the candidate, then plan again from the
    // subject's new place, keeping the grab.
    const std::uint64_t serial = serial_;
    selection_ = evaluation.selection;
    DragOutcome outcome{};
    outcome.end = DragEnd::dropped;
    outcome.mode = mode_;
    outcome.epoch = epoch_;
    outcome.selection = selection_;
    outcome.pointer = pointer_;
    ++statistics_.chains;
    record(DragTraceEvent::chain);
    if (client_alive()) (*client_).commit_drag_chain(outcome);
    if (!still(serial)) return;
    const Point centre = centre_of(source_);
    const Point grab{press_.x - centre.x, press_.y - centre.y};
    source_ = landings_[selection_.landing].rect;
    const Point moved_centre = centre_of(source_);
    press_ = Point{moved_centre.x + grab.x, moved_centre.y + grab.y};
    if (!plan()) return;
    if (!still(serial)) return;
    const Evaluation replanned = evaluate(root_node_, pointer, modifiers, false);
    select(replanned.selection, event);
}

bool DragEngine::pointer(const PointerEvent& event) {
    if (mode_ != DragInputMode::pointer) return false;
    if (phase_ != DragPhase::armed && phase_ != DragPhase::dragging) return false;
    if (event.action != PointerAction::move && event.action != PointerAction::up) return false;
    const std::size_t landings_capacity = landings_.capacity();
    const std::size_t flags_capacity = reachable_.capacity();
    const std::size_t trace_capacity = trace_.capacity();
    const Control::Ptr subject = subject_.lock();
    if (!subject || !client_alive()) {
        end(DragEnd::subject_gone);
        return true;
    }
    // The window frees the capture before a primary release reaches the
    // subject, so only a move without it means the capture was lost.
    if (event.action == PointerAction::move && !(*subject).has_pointer_capture()) {
        end(DragEnd::capture_lost);
        return true;
    }
    if (phase_ == DragPhase::armed) {
        if (event.action == PointerAction::up) {
            // Released before the threshold: no drag; the caller treats it
            // as the press's release.
            disarm();
            return false;
        }
        const double dx = std::fabs(event.position.x - press_.x);
        const double dy = std::fabs(event.position.y - press_.y);
        if (dx < threshold_ && dy < threshold_) return true;
        const std::uint64_t serial = serial_;
        pointer_ = event.position;
        modifiers_ = event.modifiers;
        if (!plan()) return true;
        if (!still(serial)) return true;
        // The first dragging frame is a change from nothing, so the client
        // previews and announces it.
        selection_ = DragSelection{};
        drag_to(event.position, event.modifiers, DragTraceEvent::move);
        note_growth(landings_capacity, flags_capacity, trace_capacity);
        return true;
    }
    if (event.action == PointerAction::move) {
        drag_to(event.position, event.modifiers, DragTraceEvent::move);
        note_growth(landings_capacity, flags_capacity, trace_capacity);
        return true;
    }
    // The release: the selection there, without a preview, commits.
    pointer_ = event.position;
    modifiers_ = event.modifiers;
    const Evaluation evaluation = evaluate(root_node_, event.position, event.modifiers, true);
    ++statistics_.evaluations;
    if (evaluation.tied) ++statistics_.ties;
    selection_ = evaluation.selection;
    DragEnd ending = DragEnd::rejected;
    if (selection_.kind == DragSelectionKind::candidate) ending = DragEnd::dropped;
    if (selection_.kind == DragSelectionKind::value || selection_.kind == DragSelectionKind::point) {
        ending = DragEnd::free;
    }
    note_growth(landings_capacity, flags_capacity, trace_capacity);
    end(ending);
    return true;
}

bool DragEngine::step_spatial(double dx, double dy) {
    if (selection_.kind != DragSelectionKind::candidate) return false;
    const Point from = landings_[selection_.landing].anchor;
    double best = infinite_distance;
    std::size_t best_index = landings_.size();
    for (std::size_t index = 0U; index < landings_.size(); ++index) {
        if (reachable_[index] == 0U || index == selection_.landing) continue;
        const double vx = landings_[index].anchor.x - from.x;
        const double vy = landings_[index].anchor.y - from.y;
        const double ahead = vx * dx + vy * dy;
        if (ahead <= drag_tie_tolerance) continue;
        // Straight ahead is preferred: the sideways offset counts double.
        const double sideways = std::fabs(vx * dy - vy * dx);
        const double score = ahead + 2.0 * sideways;
        if (score < best - drag_tie_tolerance) {
            best = score;
            best_index = index;
        }
    }
    if (best_index == landings_.size()) return false;
    DragSelection next{};
    next.kind = DragSelectionKind::candidate;
    next.landing = static_cast<std::uint32_t>(best_index);
    next.token = landings_[best_index].token;
    next.group = landings_[best_index].group;
    pointer_ = pointer_for(next);
    select(next, DragTraceEvent::key);
    return true;
}

bool DragEngine::step_group(bool forward) {
    if (selection_.kind != DragSelectionKind::candidate) return false;
    // The groups in declaration order of their first reachable landing.
    std::array<std::size_t, maximum_drag_groups> first_of{};
    std::array<std::uint16_t, maximum_drag_groups> order{};
    std::size_t group_count = 0U;
    for (std::size_t index = 0U; index < landings_.size(); ++index) {
        if (reachable_[index] == 0U) continue;
        const std::uint16_t group = landings_[index].group.value;
        bool seen = false;
        for (std::size_t known = 0U; known < group_count; ++known) {
            if (order[known] == group) seen = true;
        }
        if (seen || group_count >= order.size()) continue;
        order[group_count] = group;
        first_of[group_count] = index;
        ++group_count;
    }
    if (group_count < 2U) return false;
    std::size_t current = 0U;
    for (std::size_t known = 0U; known < group_count; ++known) {
        if (order[known] == selection_.group.value) current = known;
    }
    const std::size_t next_group = forward ? (current + 1U) % group_count : (current + group_count - 1U) % group_count;
    const std::size_t index = first_of[next_group];
    DragSelection next{};
    next.kind = DragSelectionKind::candidate;
    next.landing = static_cast<std::uint32_t>(index);
    next.token = landings_[index].token;
    next.group = landings_[index].group;
    pointer_ = pointer_for(next);
    select(next, DragTraceEvent::key);
    return true;
}

bool DragEngine::step_continuous(double dx, double dy, Modifier modifiers) {
    double factor = 1.0;
    if (has_modifier(modifiers, Modifier::control)) factor = drag_coarse_factor;
    if (has_modifier(modifiers, Modifier::shift)) factor = drag_fine_factor;
    if (selection_.kind == DragSelectionKind::value) {
        const DragNodeIndex node = find_operator(DragOperatorKind::project);
        if (node == no_drag_node) return false;
        const DragProjection& projection = operators_[node.value].projection;
        const double direction = projection.axis == DragAxis::x ? dx : dy;
        if (direction == 0.0) return false;
        // The arrow moves the virtual pointer; the value follows the
        // projection's sign.
        const double sign = projection.scale > 0.0 ? 1.0 : -1.0;
        const double raw = selection_.value + sign * direction * projection.keyboard_step * factor;
        DragSelection next = selection_;
        next.value = projected_value(projection, raw);
        next.decided_by = node;
        pointer_ = pointer_for(next);
        select(next, DragTraceEvent::key);
        return true;
    }
    if (selection_.kind == DragSelectionKind::point) {
        const DragNodeIndex node = find_operator(DragOperatorKind::free);
        if (node == no_drag_node) return false;
        const DragFreeArea& area = operators_[node.value].free_area;
        const double step_x = area.grid.width > 0.0 ? area.grid.width : 1.0;
        const double step_y = area.grid.height > 0.0 ? area.grid.height : 1.0;
        const Point target{selection_.point.x + dx * step_x * factor, selection_.point.y + dy * step_y * factor};
        // The pointer that puts the subject's top-left at the target.
        const Point pointer{press_.x + (target.x - source_.x), press_.y + (target.y - source_.y)};
        DragSelection next = evaluate_free(operators_[node.value], pointer);
        next.decided_by = node;
        pointer_ = pointer_for(next);
        select(next, DragTraceEvent::key);
        return true;
    }
    return false;
}

bool DragEngine::key(const KeyEvent& event) {
    if (phase_ != DragPhase::armed && phase_ != DragPhase::dragging) return false;
    if (event.action != KeyAction::down) return false;
    const std::size_t landings_capacity = landings_.capacity();
    const std::size_t flags_capacity = reachable_.capacity();
    const std::size_t trace_capacity = trace_.capacity();
    if (event.physical_key == PhysicalKey::escape) {
        if (phase_ == DragPhase::armed) {
            disarm();
        } else {
            end(DragEnd::escaped);
        }
        return true;
    }
    if (mode_ != DragInputMode::keyboard || phase_ != DragPhase::dragging) return false;
    if (!client_alive() || subject_.expired()) {
        end(DragEnd::subject_gone);
        return true;
    }
    double dx = 0.0;
    double dy = 0.0;
    switch (event.physical_key) {
    case PhysicalKey::left: dx = -1.0; break;
    case PhysicalKey::right: dx = 1.0; break;
    case PhysicalKey::up: dy = -1.0; break;
    case PhysicalKey::down: dy = 1.0; break;
    case PhysicalKey::tab: {
        const bool forward = !has_modifier(event.modifiers, Modifier::shift);
        static_cast<void>(step_group(forward));
        note_growth(landings_capacity, flags_capacity, trace_capacity);
        return true;
    }
    case PhysicalKey::enter: {
        DragEnd ending = DragEnd::rejected;
        if (selection_.kind == DragSelectionKind::candidate) ending = DragEnd::dropped;
        if (selection_.kind == DragSelectionKind::value || selection_.kind == DragSelectionKind::point) {
            ending = DragEnd::free;
        }
        end(ending);
        return true;
    }
    default:
        // The move command again is taken and does nothing; other keys go
        // on to the window's rules and controls.
        return is_drag_move_command(event);
    }
    modifiers_ = event.modifiers;
    if (selection_.kind == DragSelectionKind::candidate) {
        static_cast<void>(step_spatial(dx, dy));
    } else {
        static_cast<void>(step_continuous(dx, dy, event.modifiers));
    }
    note_growth(landings_capacity, flags_capacity, trace_capacity);
    return true;
}

void DragEngine::release_capture() {
    if (mode_ != DragInputMode::pointer) return;
    const Control::Ptr subject = subject_.lock();
    if (subject && (*subject).has_pointer_capture()) (*subject).set_pointer_capture(false);
}

void DragEngine::disarm() {
    ending_ = true;
    phase_ = DragPhase::idle;
    release_capture();
    record(DragTraceEvent::end);
    reset_session();
    ending_ = false;
}

void DragEngine::end(DragEnd ending) {
    if (phase_ == DragPhase::idle) return;
    const std::uint64_t serial = serial_;
    ending_ = true;
    const bool commits = ending == DragEnd::dropped || ending == DragEnd::free;
    const bool alive = ending != DragEnd::subject_gone && client_alive();
    const bool suppressed = (*root_).motion_clock().suppressed();
    DragOutcome outcome{};
    outcome.end = ending;
    outcome.mode = mode_;
    outcome.epoch = epoch_;
    outcome.selection = commits ? selection_ : DragSelection{};
    outcome.pointer = pointer_;
    outcome.animate = settles_ && alive && !suppressed;
    const std::size_t end_index = static_cast<std::size_t>(ending);
    if (end_index < statistics_.ends.size()) ++statistics_.ends[end_index];
    phase_ = outcome.animate ? DragPhase::settling : DragPhase::idle;
    release_capture();
    record(DragTraceEvent::end, ending);
    if (alive) {
        if (commits) (*client_).rewrite_drag_outcome(outcome);
        if (serial == serial_) (*client_).end_drag(outcome);
        if (serial == serial_ && client_alive()) {
            DragFrame frame = frame_now();
            frame.end = ending;
            (*client_).announce_drag(frame, announcement_);
        }
    }
    if (serial == serial_ && phase_ == DragPhase::idle) reset_session();
    ending_ = false;
}

void DragEngine::cancel(DragEnd ending) {
    if (ending != DragEnd::escaped && ending != DragEnd::deactivated && ending != DragEnd::model_changed) return;
    if (phase_ == DragPhase::armed) {
        disarm();
        return;
    }
    if (phase_ != DragPhase::dragging && phase_ != DragPhase::planning) return;
    end(ending);
}

void DragEngine::note_capture_lost(const Control& control) {
    if (ending_ || mode_ != DragInputMode::pointer) return;
    if (phase_ != DragPhase::armed && phase_ != DragPhase::dragging) return;
    const Control::Ptr subject = subject_.lock();
    if (subject && subject.get() != &control) return;
    if (phase_ == DragPhase::armed) {
        disarm();
        return;
    }
    end(DragEnd::capture_lost);
}

void DragEngine::client_gone(const DragClient& client) noexcept {
    if (phase_ == DragPhase::idle || client_ != &client) return;
    ending_ = true;
    phase_ = DragPhase::idle;
    const std::size_t end_index = static_cast<std::size_t>(DragEnd::subject_gone);
    ++statistics_.ends[end_index];
    client_ = nullptr;
    // A capture release may raise events; a dying client is not called.
    try {
        release_capture();
    } catch (...) {
        // The capture is the window's; it is cleared with the subject.
    }
    record(DragTraceEvent::end, DragEnd::subject_gone);
    reset_session();
    ending_ = false;
}

void DragEngine::finish_settling() noexcept {
    if (phase_ != DragPhase::settling) return;
    phase_ = DragPhase::idle;
    reset_session();
}

} // namespace gui_forms
