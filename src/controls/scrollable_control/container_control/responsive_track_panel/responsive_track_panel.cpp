#include "gui_forms/controls/scrollable_control/container_control/responsive_track_panel/responsive_track_panel.hpp"
#include "gui_forms/window.hpp"

#include "../container_layout_utilities.hpp"
#include "../../scrollable_anatomy.hpp"
#include "../../../../core/types/restrict/restrict.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

constexpr std::size_t maximum_responsive_tracks = 64U;
constexpr double maximum_track_extent = 1'000'000.0;

[[nodiscard]] double bounded_maximum(const ResponsiveTrackSpec& spec) noexcept {
    return spec.maximum > 0.0 ? spec.maximum
                              : std::numeric_limits<double>::infinity();
}

void validate_track_specs(std::span<const ResponsiveTrackSpec> specs) {
    if (specs.empty() || specs.size() > maximum_responsive_tracks) {
        throw std::invalid_argument(
            "ResponsiveTrackPanel requires 1 through 64 tracks");
    }
    std::unordered_set<std::uint16_t> priorities;
    for (const ResponsiveTrackSpec& spec : specs) {
        if (spec.size_mode > ResponsiveTrackSizeMode::remaining) {
            throw std::invalid_argument(
                "ResponsiveTrackPanel track size mode is unknown");
        }
        if (!std::isfinite(spec.minimum) ||
            !std::isfinite(spec.preferred) ||
            !std::isfinite(spec.maximum) ||
            !std::isfinite(spec.remaining_weight) ||
            spec.minimum < 0.0 || spec.preferred < spec.minimum ||
            spec.minimum > maximum_track_extent ||
            spec.preferred > maximum_track_extent ||
            spec.maximum < 0.0 || spec.maximum > maximum_track_extent ||
            (spec.maximum > 0.0 && spec.maximum < spec.preferred)) {
            throw std::invalid_argument(
                "ResponsiveTrackPanel track bounds must be finite ordered nonnegative values");
        }
        if (spec.size_mode == ResponsiveTrackSizeMode::remaining) {
            if (spec.remaining_weight <= 0.0 ||
                spec.remaining_weight > 1024.0) {
                throw std::invalid_argument(
                    "ResponsiveTrackPanel remaining weight must be positive and at most 1024");
            }
        }
        if (spec.collapse_priority &&
            !priorities.insert(*spec.collapse_priority).second) {
            throw std::invalid_argument(
                "ResponsiveTrackPanel collapse priorities must be unique");
        }
    }
}

[[nodiscard]] double extent_with_gaps(
    const std::vector<bool>& active,
    std::span<const ResponsiveTrackResult> tracks,
    double gap,
    bool desired) noexcept {
    double extent = 0.0;
    std::size_t count = 0U;
    for (std::size_t index = 0U; index < tracks.size(); ++index) {
        if (!active[index]) continue;
        extent += desired ? tracks[index].desired : tracks[index].minimum;
        ++count;
    }
    if (count > 1U) extent += gap * static_cast<double>(count - 1U);
    return extent;
}

// Orders track indices by authored collapse priority, then by index.
struct CollapsePriorityOrder final {
    std::span<const ResponsiveTrackSpec> specs;

    [[nodiscard]] bool operator()(std::size_t left,
                                  std::size_t right) const noexcept {
        if (*specs[left].collapse_priority !=
            *specs[right].collapse_priority) {
            return *specs[left].collapse_priority <
                   *specs[right].collapse_priority;
        }
        return left < right;
    }
};

[[nodiscard]] std::vector<std::size_t> sorted_collapse_candidates(
    std::span<const ResponsiveTrackSpec> specs,
    std::span<const ResponsiveTrackResult> tracks) {
    std::vector<std::size_t> candidates;
    for (std::size_t index = 0U; index < specs.size(); ++index) {
        if (specs[index].hidden || !specs[index].collapse_priority ||
            tracks[index].collapse_protected) {
            continue;
        }
        candidates.push_back(index);
    }
    std::sort(candidates.begin(), candidates.end(),
              CollapsePriorityOrder{specs});
    return candidates;
}

void shrink_to_available(std::span<ResponsiveTrackResult> tracks,
                         const std::vector<bool>& active,
                         double available) noexcept {
    double minimum_sum = 0.0;
    double desired_sum = 0.0;
    for (std::size_t index = 0U; index < tracks.size(); ++index) {
        if (!active[index]) continue;
        minimum_sum += tracks[index].minimum;
        desired_sum += tracks[index].desired;
        tracks[index].allocated = tracks[index].desired;
    }
    if (available >= desired_sum) return;
    if (available < minimum_sum) {
        const double factor = minimum_sum > 0.0
            ? std::max(0.0, available) / minimum_sum : 0.0;
        for (std::size_t index = 0U; index < tracks.size(); ++index) {
            if (active[index]) {
                tracks[index].allocated = tracks[index].minimum * factor;
            }
        }
        return;
    }
    const double capacity = desired_sum - minimum_sum;
    const double reduction = desired_sum - available;
    for (std::size_t index = 0U; index < tracks.size(); ++index) {
        if (!active[index]) continue;
        const double own_capacity =
            tracks[index].desired - tracks[index].minimum;
        tracks[index].allocated = tracks[index].desired -
            (capacity > 0.0 ? reduction * own_capacity / capacity : 0.0);
    }
}

// Restricted parameters: no element is reached through both (distribute_remaining
// checks it).
void distribute_remaining_parts(
    const ResponsiveTrackSpec* GUI_FORMS_RESTRICT spec_data,
    ResponsiveTrackResult* GUI_FORMS_RESTRICT track_data,
    std::size_t count,
    const std::vector<bool>& active,
    double extra) noexcept {
    const std::span<const ResponsiveTrackSpec> specs(spec_data, count);
    const std::span<ResponsiveTrackResult> tracks(track_data, count);
    std::vector<std::size_t> open;
    for (std::size_t index = 0U; index < specs.size(); ++index) {
        if (active[index] &&
            specs[index].size_mode == ResponsiveTrackSizeMode::remaining &&
            tracks[index].allocated + 1e-9 < bounded_maximum(specs[index])) {
            open.push_back(index);
        }
    }
    while (extra > 1e-9 && !open.empty()) {
        double weight = 0.0;
        for (const std::size_t index : open) {
            weight += specs[index].remaining_weight;
        }
        if (weight <= 0.0) break;
        double consumed = 0.0;
        std::vector<std::size_t> next;
        for (const std::size_t index : open) {
            const double share = extra * specs[index].remaining_weight / weight;
            const double capacity =
                bounded_maximum(specs[index]) - tracks[index].allocated;
            const double addition = std::min(share, capacity);
            tracks[index].allocated += addition;
            consumed += addition;
            if (addition + 1e-9 < capacity) next.push_back(index);
        }
        if (consumed <= 1e-9) break;
        extra -= consumed;
        open = std::move(next);
    }
}

// Overlap rule: the caller's specs and the result's tracks, one per spec,
// share no element (asserted in debug and sanitizer builds).
void distribute_remaining(
    std::span<const ResponsiveTrackSpec> specs,
    std::span<ResponsiveTrackResult> tracks,
    const std::vector<bool>& active,
    double extra) noexcept {
    detail::require_disjoint(specs, tracks);
    distribute_remaining_parts(specs.data(), tracks.data(), specs.size(), active, extra);
}

[[nodiscard]] bool contains_track(std::span<const std::size_t> tracks,
                                  std::size_t candidate) noexcept {
    return std::find(tracks.begin(), tracks.end(), candidate) != tracks.end();
}

} // namespace

ResponsiveTrackResolution resolve_responsive_tracks(
    std::span<const ResponsiveTrackSpec> specs,
    std::span<const double> measured_content,
    double available_extent,
    double gap,
    double device_scale,
    std::span<const std::size_t> collapse_protected_tracks) {
    validate_track_specs(specs);
    if (measured_content.size() != specs.size()) {
        throw std::invalid_argument(
            "ResponsiveTrackPanel measurements must match track count");
    }
    if (!std::isfinite(available_extent) || available_extent < 0.0 ||
        available_extent > maximum_track_extent || !std::isfinite(gap) ||
        gap < 0.0 || gap > 256.0 || !std::isfinite(device_scale) ||
        device_scale <= 0.0 || device_scale > 16.0) {
        throw std::invalid_argument(
            "ResponsiveTrackPanel extent, gap, or device scale is invalid");
    }
    for (const std::size_t track : collapse_protected_tracks) {
        if (track >= specs.size()) {
            throw std::out_of_range(
                "ResponsiveTrackPanel protected track index");
        }
    }

    ResponsiveTrackResolution result;
    result.available_extent = available_extent;
    result.gap = gap;
    result.device_scale = device_scale;
    result.tracks.resize(specs.size());
    std::vector<bool> active(specs.size(), true);
    for (std::size_t index = 0U; index < specs.size(); ++index) {
        const ResponsiveTrackSpec& spec = specs[index];
        ResponsiveTrackResult& track = result.tracks[index];
        if (!std::isfinite(measured_content[index]) ||
            measured_content[index] < 0.0 ||
            measured_content[index] > maximum_track_extent) {
            throw std::invalid_argument(
                "ResponsiveTrackPanel content measurement is invalid");
        }
        track.measured_content = measured_content[index];
        // Content tracks treat their measured main-axis demand as a runtime
        // minimum. This is the reflow/grow-before-collapse rule: ordinary
        // nominal rows stay at preferred size, while larger text cannot be
        // shrunk back through its measured line box before authored regions
        // have had their declared collapse opportunity.
        track.minimum = spec.size_mode == ResponsiveTrackSizeMode::content
            ? std::clamp(std::max(spec.minimum, measured_content[index]),
                         spec.minimum, bounded_maximum(spec))
            : spec.minimum;
        double desired = spec.preferred;
        if (spec.size_mode == ResponsiveTrackSizeMode::content) {
            desired = std::max(desired, measured_content[index]);
        }
        track.desired = std::clamp(desired, spec.minimum,
                                   bounded_maximum(spec));
        track.collapse_protected =
            contains_track(collapse_protected_tracks, index);
        if (spec.hidden) {
            active[index] = false;
            track.collapse_reason =
                ResponsiveTrackCollapseReason::authored_hidden;
        }
    }

    const std::vector<std::size_t> candidates =
        sorted_collapse_candidates(specs, result.tracks);
    for (const std::size_t index : candidates) {
        const double minimum = extent_with_gaps(
            active, result.tracks, gap, false);
        if (minimum <= available_extent + 1e-9) break;
        result.tracks[index].collapse_threshold = minimum;
        result.tracks[index].collapse_reason =
            ResponsiveTrackCollapseReason::insufficient_extent;
        active[index] = false;
        result.collapse_order.push_back(index);
    }

    result.minimum_extent =
        extent_with_gaps(active, result.tracks, gap, false);
    result.desired_extent =
        extent_with_gaps(active, result.tracks, gap, true);
    result.overflow = result.minimum_extent > available_extent + 1e-9;
    const std::size_t active_count = static_cast<std::size_t>(
        std::count(active.begin(), active.end(), true));
    const double gaps = active_count > 1U
        ? gap * static_cast<double>(active_count - 1U) : 0.0;
    const double track_extent = std::max(0.0, available_extent - gaps);
    shrink_to_available(result.tracks, active, track_extent);
    double allocated_tracks = 0.0;
    for (std::size_t index = 0U; index < specs.size(); ++index) {
        if (active[index]) allocated_tracks += result.tracks[index].allocated;
    }
    if (track_extent > allocated_tracks) {
        distribute_remaining(specs, result.tracks, active,
                             track_extent - allocated_tracks);
    }

    double cursor = 0.0;
    bool first = true;
    for (std::size_t index = 0U; index < specs.size(); ++index) {
        ResponsiveTrackResult& track = result.tracks[index];
        if (!active[index]) {
            track.logical_start = cursor;
            track.logical_end = cursor;
            track.device_start = static_cast<std::int64_t>(
                std::llround(cursor * device_scale));
            track.device_end = track.device_start;
            continue;
        }
        if (!first) cursor += gap;
        first = false;
        track.logical_start = cursor;
        track.logical_end = cursor + track.allocated;
        track.device_start = static_cast<std::int64_t>(
            std::llround(track.logical_start * device_scale));
        track.device_end = static_cast<std::int64_t>(
            std::llround(track.logical_end * device_scale));
        cursor = track.logical_end;
    }
    result.allocated_extent = cursor;
    return result;
}

using namespace container_layout_detail;

ResponsiveTrackPanel::ResponsiveTrackPanel(StableId stable_id)
    : ContainerControl(std::move(stable_id)) {}

void ResponsiveTrackPanel::validate_specs(
    std::span<const ResponsiveTrackSpec> specs) {
    validate_track_specs(specs);
}

void ResponsiveTrackPanel::set_orientation(
    ResponsiveTrackOrientation orientation) {
    require_mutable();
    if (orientation > ResponsiveTrackOrientation::vertical) {
        throw std::invalid_argument(
            "ResponsiveTrackPanel orientation is unknown");
    }
    if (orientation_ == orientation) return;
    orientation_ = orientation;
    invalidate(invalidation::bounds);
}

void ResponsiveTrackPanel::set_track_gap(double gap) {
    require_mutable();
    if (!std::isfinite(gap) || gap < 0.0 || gap > 256.0) {
        throw std::invalid_argument(
            "ResponsiveTrackPanel gap must be finite and between zero and 256");
    }
    if (track_gap_ == gap) return;
    track_gap_ = gap;
    invalidate(invalidation::bounds);
}

void ResponsiveTrackPanel::set_track_specs(
    std::vector<ResponsiveTrackSpec> specs) {
    require_mutable();
    validate_specs(specs);
    if (track_specs_ == specs) return;
    track_specs_ = std::move(specs);
    for (ChildTrackMap::iterator entry = child_tracks_.begin();
         entry != child_tracks_.end();) {
        if ((*entry).second >= track_specs_.size()) entry = child_tracks_.erase(entry);
        else ++entry;
    }
    for (FocusFallbackMap::iterator entry = focus_fallbacks_.begin();
         entry != focus_fallbacks_.end();) {
        if ((*entry).first >= track_specs_.size()) entry = focus_fallbacks_.erase(entry);
        else ++entry;
    }
    for (std::unordered_set<std::size_t>::iterator entry = revealed_tracks_.begin();
         entry != revealed_tracks_.end();) {
        if (*entry >= track_specs_.size()) entry = revealed_tracks_.erase(entry);
        else ++entry;
    }
    invalidate(invalidation::bounds);
}

void ResponsiveTrackPanel::set_track_spec(
    std::size_t track, ResponsiveTrackSpec spec) {
    require_mutable();
    if (track >= track_specs_.size()) {
        throw std::out_of_range("ResponsiveTrackPanel track spec index");
    }
    std::vector<ResponsiveTrackSpec> candidate = track_specs_;
    candidate[track] = spec;
    validate_specs(candidate);
    if (track_specs_[track] == spec) return;
    track_specs_ = std::move(candidate);
    invalidate(invalidation::bounds);
}

void ResponsiveTrackPanel::set_child_track(
    const Control& child, std::size_t track) {
    require_mutable();
    if (child.parent().get() != this || !child.is_alive()) {
        throw std::invalid_argument(
            "ResponsiveTrackPanel track assignment requires a live direct child");
    }
    if (track >= track_specs_.size()) {
        throw std::out_of_range("ResponsiveTrackPanel child track index");
    }
    const std::uint64_t id = child.runtime_id().value;
    const ChildTrackMap::iterator found = child_tracks_.find(id);
    if (found != child_tracks_.end() && (*found).second == track) return;
    child_tracks_[id] = track;
    invalidate(invalidation::bounds);
}

std::optional<std::size_t> ResponsiveTrackPanel::child_track(
    const Control& child) const {
    if (child.parent().get() != this || !child.is_alive()) return std::nullopt;
    const ChildTrackMap::const_iterator found =
        child_tracks_.find(child.runtime_id().value);
    return found == child_tracks_.end()
        ? std::nullopt : std::optional<std::size_t>((*found).second);
}

void ResponsiveTrackPanel::set_track_revealed(
    std::size_t track, bool revealed) {
    require_mutable();
    if (track >= track_specs_.size()) {
        throw std::out_of_range("ResponsiveTrackPanel reveal track index");
    }
    const bool previous = revealed_tracks_.contains(track);
    if (previous == revealed) return;
    if (revealed) revealed_tracks_.insert(track);
    else revealed_tracks_.erase(track);
    invalidate(invalidation::bounds);
}

bool ResponsiveTrackPanel::track_revealed(std::size_t track) const noexcept {
    return revealed_tracks_.contains(track);
}

void ResponsiveTrackPanel::set_track_focus_fallback(
    std::size_t track, Control& target) {
    require_mutable();
    if (track >= track_specs_.size()) {
        throw std::out_of_range(
            "ResponsiveTrackPanel focus fallback track index");
    }
    if (!target.is_alive() || !contains(target)) {
        throw std::invalid_argument(
            "ResponsiveTrackPanel focus fallback must be a live descendant");
    }
    focus_fallbacks_[track] = target.shared_from_this();
}

void ResponsiveTrackPanel::clear_track_focus_fallback(std::size_t track) {
    require_mutable();
    if (track >= track_specs_.size()) {
        throw std::out_of_range(
            "ResponsiveTrackPanel focus fallback track index");
    }
    focus_fallbacks_.erase(track);
}

ResponsiveLayoutSnapshot ResponsiveTrackPanel::layout_snapshot() const {
    if (attached_window() != nullptr) static_cast<void>(arranged_bounds());
    return {orientation_, committed_resolution_, layout_transaction_state(),
            committed_resolution_revision_};
}

void ResponsiveTrackPanel::reconcile_metadata() {
    std::unordered_set<std::uint64_t> live;
    for (const Control::Ptr& child : children()) {
        if (child && (*child).is_alive() && (*child).parent().get() == this) {
            live.insert((*child).runtime_id().value);
        }
    }
    for (ChildTrackMap::iterator entry = child_tracks_.begin();
         entry != child_tracks_.end();) {
        if (!live.contains((*entry).first)) entry = child_tracks_.erase(entry);
        else ++entry;
    }
    for (FocusFallbackMap::iterator entry = focus_fallbacks_.begin();
         entry != focus_fallbacks_.end();) {
        const std::shared_ptr<Control> target = (*entry).second.lock();
        if (!target || !(*target).is_alive() || !contains(*target)) {
            entry = focus_fallbacks_.erase(entry);
        } else {
            ++entry;
        }
    }
}

ResponsiveTrackResolution ResponsiveTrackPanel::layout_children(
    Size available, bool assign) {
    reconcile_metadata();
    const Insets inset = padding.type();
    const Size inner{
        std::max(0.0, available.width - horizontal_extent(inset)),
        std::max(0.0, available.height - vertical_extent(inset))};
    const bool horizontal =
        orientation_ == ResponsiveTrackOrientation::horizontal;
    const double main_extent = horizontal ? inner.width : inner.height;

    struct Item final {
        Control::Ptr control;
        std::size_t track{};
        Size desired;
        Insets margin;
    };
    std::vector<Item> items;
    std::vector<double> measured(track_specs_.size(), 0.0);
    std::unordered_set<std::size_t> explicitly_used;
    for (const ChildTrackMap::value_type& entry : child_tracks_) {
        explicitly_used.insert(entry.second);
    }
    std::size_t automatic_track = 0U;
    const std::vector<Control::Ptr> retained = snapshot_layout_children();
    for (const Control::Ptr& child : retained) {
        if (!is_current_layout_child(child) || !(*child).visible.type()) continue;
        std::size_t track = track_specs_.size();
        const ChildTrackMap::const_iterator authored =
            child_tracks_.find((*child).runtime_id().value);
        if (authored != child_tracks_.end()) {
            track = (*authored).second;
        } else {
            while (automatic_track < track_specs_.size() &&
                   explicitly_used.contains(automatic_track)) {
                ++automatic_track;
            }
            if (automatic_track < track_specs_.size()) track = automatic_track++;
        }
        if (track >= track_specs_.size()) continue;
        const Size desired = preferred_child_size(child, inner);
        if (!is_alive()) return {};
        if (!is_current_layout_child(child) || !(*child).visible.type()) continue;
        const Insets margin = (*child).margin.type();
        const double demand = horizontal
            ? desired.width + horizontal_extent(margin)
            : desired.height + vertical_extent(margin);
        measured[track] = std::max(measured[track], demand);
        items.push_back({child, track, desired, margin});
    }

    std::vector<std::size_t> protected_tracks(
        revealed_tracks_.begin(), revealed_tracks_.end());
    std::sort(protected_tracks.begin(), protected_tracks.end());
    Window* const owner = attached_window();
    const Control::Ptr focused = owner == nullptr
        ? Control::Ptr{} : (*owner).focused_control();
    std::optional<std::size_t> focused_track;
    if (focused) {
        for (const Item& item : items) {
            if (item.control == focused || (*item.control).contains(*focused)) {
                focused_track = item.track;
                break;
            }
        }
    }
    if (focused_track &&
        std::find(protected_tracks.begin(), protected_tracks.end(),
                  *focused_track) == protected_tracks.end()) {
        protected_tracks.push_back(*focused_track);
        std::sort(protected_tracks.begin(), protected_tracks.end());
    }
    const double device_scale = owner == nullptr ? 1.0 : (*owner).scale();
    ResponsiveTrackResolution resolution = resolve_responsive_tracks(
        track_specs_, measured, main_extent, track_gap_, device_scale,
        protected_tracks);

    if (assign && focused_track && resolution.overflow && owner != nullptr) {
        std::vector<std::size_t> reveal_only(
            revealed_tracks_.begin(), revealed_tracks_.end());
        std::sort(reveal_only.begin(), reveal_only.end());
        ResponsiveTrackResolution alternative = resolve_responsive_tracks(
            track_specs_, measured, main_extent, track_gap_, device_scale,
            reveal_only);
        const bool would_collapse =
            alternative.tracks[*focused_track].collapsed();
        const FocusFallbackMap::const_iterator fallback_entry =
            focus_fallbacks_.find(*focused_track);
        const std::shared_ptr<Control> fallback =
            fallback_entry == focus_fallbacks_.end()
                ? Control::Ptr{} : (*fallback_entry).second.lock();
        if (would_collapse && fallback && (*fallback).is_alive()) {
            for (const Item& item : items) {
                if (item.control == fallback ||
                    (*item.control).contains(*fallback)) {
                    if (!alternative.tracks[item.track].collapsed()) {
                        set_child_layout_collapsed(item.control, false);
                    }
                }
            }
            if ((*owner).request_focus(fallback)) {
                resolution = std::move(alternative);
            }
        }
    }

    if (assign) {
        for (const Item& item : items) {
            const ResponsiveTrackResult& track = resolution.tracks[item.track];
            set_child_layout_collapsed(item.control, track.collapsed());
            if (track.collapsed()) {
                set_child_layout(item.control,
                    {inset.left, inset.top, 0.0, 0.0});
                continue;
            }
            const double snapped_start =
                static_cast<double>(track.device_start) / device_scale;
            const double snapped_end =
                static_cast<double>(track.device_end) / device_scale;
            Rect cell = horizontal
                ? Rect{inset.left + snapped_start, inset.top,
                       std::max(0.0, snapped_end - snapped_start), inner.height}
                : Rect{inset.left, inset.top + snapped_start, inner.width,
                       std::max(0.0, snapped_end - snapped_start)};
            const double available_width = std::max(
                0.0, cell.width - horizontal_extent(item.margin));
            const double available_height = std::max(
                0.0, cell.height - vertical_extent(item.margin));
            const Rect area{cell.x + item.margin.left, cell.y + item.margin.top,
                            available_width, available_height};
            const Rect placed = place_in_cell((*item.control).dock.type(), (*item.control).anchor.type(),
                                              area, item.desired);
            set_child_layout(item.control, placed);
        }
        if (committed_resolution_ != resolution) {
            committed_resolution_ = resolution;
            ++committed_resolution_revision_;
            if (committed_resolution_revision_ == 0U) {
                ++committed_resolution_revision_;
            }
        }
    }
    return resolution;
}

Size ResponsiveTrackPanel::measure(Size available) {
    available = {std::max(0.0, available.width),
                 std::max(0.0, available.height)};
    const ResponsiveTrackResolution resolution =
        layout_children(available, false);
    const Insets inset = padding.type();
    if (auto_size.type()) {
        if (orientation_ == ResponsiveTrackOrientation::horizontal) {
            return {std::min(available.width,
                             resolution.desired_extent + horizontal_extent(inset)),
                    available.height};
        }
        return {available.width,
                std::min(available.height,
                         resolution.desired_extent + vertical_extent(inset))};
    }
    const Rect requested = requested_bounds();
    return {std::min(available.width,
                     requested.width > 0.0 ? requested.width : available.width),
            std::min(available.height,
                     requested.height > 0.0 ? requested.height : available.height)};
}

void ResponsiveTrackPanel::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    static_cast<void>(layout_children(
        {final_bounds.width, final_bounds.height}, true));
}

SemanticDescriptor ResponsiveTrackPanel::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.type();
    descriptor.description = accessible_description.type();
    descriptor.exposed = !descriptor.name.empty() ||
                         !descriptor.description.empty();
    return descriptor;
}


namespace {

constexpr ControlAnatomy responsive_track_panel_anatomy{
    .control_id = "responsive_track_panel",
    .parts = scrollable_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& ResponsiveTrackPanel::anatomy() const noexcept {
    return responsive_track_panel_anatomy;
}

} // namespace gui_forms
