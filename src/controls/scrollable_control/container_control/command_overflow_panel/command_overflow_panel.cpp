#include "gui_forms/controls/scrollable_control/container_control/command_overflow_panel/command_overflow_panel.hpp"

#include "gui_forms/controls/scrollable_control/container_control/responsive_track_panel/responsive_track_panel.hpp"
#include "gui_forms/window.hpp"

#include "../container_layout_utilities.hpp"
#include "../../scrollable_anatomy.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gui_forms {
namespace {

using namespace container_layout_detail;

using GroupSpecMap =
    std::unordered_map<std::uint64_t, CommandOverflowGroupSpec>;

constexpr std::size_t maximum_command_groups = 32U;
constexpr double maximum_extent = 1'000'000.0;

[[nodiscard]] double bounded_maximum(
    const CommandOverflowGroupSpec& spec) noexcept {
    return spec.maximum > 0.0 ? spec.maximum : maximum_extent;
}

[[nodiscard]] double desired_track_extent(
    const std::vector<ResponsiveTrackSpec>& tracks, double gap) noexcept {
    double value = 0.0;
    for (const ResponsiveTrackSpec& track : tracks) value += track.preferred;
    if (tracks.size() > 1U) value += gap * (tracks.size() - 1U);
    return value;
}

} // namespace

struct CommandOverflowPanel::LayoutResult final {
    CommandOverflowSnapshot snapshot;
    Size desired;
};

CommandOverflowPanel::CommandOverflowPanel(StableId stable_id)
    : ContainerControl(std::move(stable_id)) {}

void CommandOverflowPanel::validate_spec(
    const CommandOverflowGroupSpec& spec) {
    if (!std::isfinite(spec.minimum) || !std::isfinite(spec.preferred) ||
        !std::isfinite(spec.maximum) || spec.minimum < 0.0 ||
        spec.preferred < spec.minimum || spec.minimum > maximum_extent ||
        spec.preferred > maximum_extent || spec.maximum < 0.0 ||
        spec.maximum > maximum_extent ||
        (spec.maximum > 0.0 && spec.maximum < spec.preferred)) {
        throw std::invalid_argument(
            "CommandOverflowPanel group extents must be finite ordered nonnegative values");
    }
}

void CommandOverflowPanel::set_group_gap(double gap) {
    require_mutable();
    if (!std::isfinite(gap) || gap < 0.0 || gap > 256.0) {
        throw std::invalid_argument(
            "CommandOverflowPanel gap must be finite and between zero and 256");
    }
    if (group_gap_ == gap) return;
    group_gap_ = gap;
    invalidate(invalidation::bounds);
}

void CommandOverflowPanel::set_group_spec(
    const Control& group, CommandOverflowGroupSpec spec) {
    require_mutable();
    validate_spec(spec);
    if (group.parent().get() != this || !group.is_alive()) {
        throw std::invalid_argument(
            "CommandOverflowPanel group must be a live direct child");
    }
    const std::shared_ptr<Control> actuator = overflow_actuator_.lock();
    if (actuator && actuator.get() == &group) {
        throw std::invalid_argument(
            "CommandOverflowPanel actuator cannot also be a command group");
    }
    for (const GroupSpecMap::value_type& entry : group_specs_) {
        if (entry.first != group.runtime_id().value &&
            spec.collapse_priority && entry.second.collapse_priority == spec.collapse_priority) {
            throw std::invalid_argument(
                "CommandOverflowPanel collapse priorities must be unique");
        }
    }
    if (group_specs_.size() >= maximum_command_groups &&
        !group_specs_.contains(group.runtime_id().value)) {
        throw std::invalid_argument(
            "CommandOverflowPanel group count exceeds 32");
    }
    GroupSpecMap::iterator found = group_specs_.find(group.runtime_id().value);
    if (found != group_specs_.end() && (*found).second == spec) return;
    group_specs_[group.runtime_id().value] = std::move(spec);
    invalidate(invalidation::bounds);
}

std::optional<CommandOverflowGroupSpec> CommandOverflowPanel::group_spec(
    const Control& group) const {
    if (group.parent().get() != this || !group.is_alive()) return std::nullopt;
    const GroupSpecMap::const_iterator found =
        group_specs_.find(group.runtime_id().value);
    return found == group_specs_.end() ? std::nullopt
                                      : std::optional((*found).second);
}

void CommandOverflowPanel::clear_group_spec(const Control& group) {
    require_mutable();
    if (group_specs_.erase(group.runtime_id().value) != 0U) {
        invalidate(invalidation::bounds);
    }
}

void CommandOverflowPanel::set_overflow_actuator(Control& control) {
    require_mutable();
    if (control.parent().get() != this || !control.is_alive()) {
        throw std::invalid_argument(
            "CommandOverflowPanel actuator must be a live direct child");
    }
    if (group_specs_.contains(control.runtime_id().value)) {
        throw std::invalid_argument(
            "CommandOverflowPanel actuator cannot also be a command group");
    }
    const std::shared_ptr<Control> previous = overflow_actuator_.lock();
    if (previous.get() == &control) return;
    overflow_actuator_ = control.shared_from_this();
    invalidate(invalidation::bounds);
}

void CommandOverflowPanel::clear_overflow_actuator() {
    require_mutable();
    if (overflow_actuator_.expired()) return;
    overflow_actuator_.reset();
    invalidate(invalidation::bounds);
}

void CommandOverflowPanel::reconcile_metadata() {
    std::unordered_set<std::uint64_t> live;
    for (const Control::Ptr& child : children()) {
        if (child && (*child).is_alive() && (*child).parent().get() == this) {
            live.insert((*child).runtime_id().value);
        }
    }
    for (GroupSpecMap::iterator entry = group_specs_.begin(); entry != group_specs_.end();) {
        if (!live.contains((*entry).first)) entry = group_specs_.erase(entry);
        else ++entry;
    }
    const std::shared_ptr<Control> actuator = overflow_actuator_.lock();
    if (!actuator || !(*actuator).is_alive() || (*actuator).parent().get() != this) {
        overflow_actuator_.reset();
    }
}

CommandOverflowPanel::LayoutResult CommandOverflowPanel::layout_groups(
    Size available, bool assign) {
    reconcile_metadata();
    const Insets inset = padding.type();
    const Size inner{
        std::max(0.0, available.width - horizontal_extent(inset)),
        std::max(0.0, available.height - vertical_extent(inset))};
    struct Item final {
        Control::Ptr control;
        CommandOverflowGroupSpec spec;
        Size desired;
        Insets margin;
    };
    std::vector<Item> items;
    for (const Control::Ptr& child : snapshot_layout_children()) {
        if (!is_current_layout_child(child) || !(*child).visible.type() ||
            child == overflow_actuator_.lock()) {
            continue;
        }
        const Size desired = preferred_child_size(child, inner);
        if (!is_alive()) return {};
        if (!is_current_layout_child(child) || !(*child).visible.type()) continue;
        const GroupSpecMap::iterator found = group_specs_.find((*child).runtime_id().value);
        CommandOverflowGroupSpec spec;
        if (found == group_specs_.end()) {
            spec.minimum = desired.width + horizontal_extent((*child).margin.type());
            spec.preferred = spec.minimum;
            spec.maximum = spec.minimum;
        } else {
            spec = (*found).second;
        }
        items.push_back({child, spec, desired, (*child).margin.type()});
    }
    if (items.size() > maximum_command_groups) {
        throw std::logic_error("CommandOverflowPanel retained group count exceeds 32");
    }

    std::vector<ResponsiveTrackSpec> tracks;
    std::vector<double> measured;
    tracks.reserve(items.size());
    measured.reserve(items.size());
    for (const Item& item : items) {
        tracks.push_back({ResponsiveTrackSizeMode::fixed,
                          item.spec.minimum, item.spec.preferred,
                          item.spec.maximum, 1.0, false,
                          item.spec.collapse_priority});
        measured.push_back(item.desired.width + horizontal_extent(item.margin));
    }
    Window* const owner = attached_window();
    const double device_scale = owner == nullptr ? 1.0 : (*owner).scale();
    const Control::Ptr focused = owner == nullptr ? Control::Ptr{}
                                                   : (*owner).focused_control();
    std::vector<std::size_t> protected_tracks;
    std::optional<std::size_t> focused_track;
    for (std::size_t index = 0U; index < items.size(); ++index) {
        if (focused && (items[index].control == focused ||
                        (*items[index].control).contains(*focused))) {
            protected_tracks.push_back(index);
            focused_track = index;
            break;
        }
    }
    ResponsiveTrackResolution resolution = resolve_responsive_tracks(
        tracks, measured, inner.width, group_gap_, device_scale,
        protected_tracks);
    bool needs_actuator = !resolution.collapse_order.empty();
    const Control::Ptr actuator = overflow_actuator_.lock();
    Size actuator_desired{};
    Insets actuator_margin{};
    double actuator_extent = 0.0;
    if (actuator && (*actuator).visible.type()) {
        actuator_desired = preferred_child_size(actuator, inner);
        actuator_margin = (*actuator).margin.type();
        actuator_extent = actuator_desired.width + horizontal_extent(actuator_margin);
    }
    if (needs_actuator && actuator) {
        const double reserved = actuator_extent + (items.empty() ? 0.0 : group_gap_);
        resolution = resolve_responsive_tracks(
            tracks, measured, std::max(0.0, inner.width - reserved),
            group_gap_, device_scale, protected_tracks);
        needs_actuator = true;
        if (assign && focused_track && resolution.overflow && owner != nullptr) {
            ResponsiveTrackResolution alternative = resolve_responsive_tracks(
                tracks, measured, std::max(0.0, inner.width - reserved),
                group_gap_, device_scale);
            if (alternative.tracks[*focused_track].collapsed() &&
                (*actuator).focusable() && (*owner).request_focus(actuator)) {
                resolution = std::move(alternative);
            }
        }
    }

    CommandOverflowSnapshot snapshot;
    snapshot.available_extent = inner.width;
    snapshot.group_gap = group_gap_;
    snapshot.overflow_actuator_visible = needs_actuator && bool(actuator);
    snapshot.overflow = resolution.overflow || (needs_actuator && !actuator);
    snapshot.groups.reserve(items.size());
    for (std::size_t index = 0U; index < items.size(); ++index) {
        snapshot.groups.push_back({
            std::string((*items[index].control).stable_id().value()),
            measured[index], resolution.tracks[index].allocated,
            resolution.tracks[index].collapsed(),
            items[index].spec.collapse_priority});
    }
    const double desired_groups = desired_track_extent(tracks, group_gap_);

    if (assign) {
        for (std::size_t index = 0U; index < items.size(); ++index) {
            const ResponsiveTrackResult& track = resolution.tracks[index];
            set_child_layout_collapsed(items[index].control, track.collapsed());
            if (track.collapsed()) {
                set_child_layout(items[index].control,
                                 {inset.left, inset.top, 0.0, 0.0});
                continue;
            }
            const double start = track.device_start / device_scale;
            const double end = track.device_end / device_scale;
            set_child_layout(items[index].control,
                {inset.left + start + items[index].margin.left,
                 inset.top + items[index].margin.top,
                 std::max(0.0, end - start - horizontal_extent(items[index].margin)),
                 std::max(0.0, inner.height - vertical_extent(items[index].margin))});
        }
        if (actuator) {
            set_child_layout_collapsed(actuator, !needs_actuator);
            if (!needs_actuator) {
                set_child_layout(actuator, {inset.left, inset.top, 0.0, 0.0});
            } else {
                set_child_layout(actuator,
                    {inset.left + std::max(0.0, inner.width - actuator_extent) +
                         actuator_margin.left,
                     inset.top + actuator_margin.top,
                     std::max(0.0, actuator_desired.width),
                     std::max(0.0, inner.height - vertical_extent(actuator_margin))});
            }
        }
        CommandOverflowSnapshot candidate = snapshot;
        candidate.committed_revision = committed_snapshot_.committed_revision;
        if (candidate != committed_snapshot_) {
            snapshot.committed_revision = committed_snapshot_.committed_revision + 1U;
            if (snapshot.committed_revision == 0U) snapshot.committed_revision = 1U;
            committed_snapshot_ = snapshot;
        } else {
            snapshot = committed_snapshot_;
        }
    }
    return {snapshot,
            {desired_groups + horizontal_extent(inset),
             inner.height + vertical_extent(inset)}};
}

CommandOverflowSnapshot CommandOverflowPanel::layout_snapshot() const {
    if (attached_window() != nullptr) static_cast<void>(arranged_bounds());
    return committed_snapshot_;
}

Size CommandOverflowPanel::measure(Size available) {
    available = {std::max(0.0, available.width),
                 std::max(0.0, available.height)};
    const LayoutResult result = layout_groups(available, false);
    if (auto_size.type()) {
        return {std::min(available.width, result.desired.width),
                std::min(available.height, result.desired.height)};
    }
    const Rect requested = requested_bounds();
    return {std::min(available.width,
                     requested.width > 0.0 ? requested.width : available.width),
            std::min(available.height,
                     requested.height > 0.0 ? requested.height : available.height)};
}

void CommandOverflowPanel::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    static_cast<void>(layout_groups(
        {final_bounds.width, final_bounds.height}, true));
}

SemanticDescriptor CommandOverflowPanel::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.type();
    descriptor.description = accessible_description.type();
    descriptor.exposed = true;
    return descriptor;
}


namespace {

// Its panes and members are child controls, which publish their own anatomies.
constexpr ControlAnatomy command_overflow_panel_anatomy{
    .control_id = "command_overflow_panel",
    .parts = scrollable_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& CommandOverflowPanel::anatomy() const noexcept {
    return command_overflow_panel_anatomy;
}

} // namespace gui_forms
