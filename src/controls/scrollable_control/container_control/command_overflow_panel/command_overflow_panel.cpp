#include "gui_forms/controls/scrollable_control/container_control/command_overflow_panel/command_overflow_panel.hpp"

#include "gui_forms/controls/scrollable_control/container_control/responsive_track_panel/responsive_track_panel.hpp"
#include "gui_forms/window.hpp"

#include "../container_layout_utilities.hpp"
#include "../../scrollable_anatomy.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
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

// One whitespace-separated word of `text` from `position`; empty at the end.
std::string_view next_word(std::string_view text, std::size_t& position) noexcept {
    while (position < text.size() && text[position] == ' ') ++position;
    const std::size_t start = position;
    while (position < text.size() && text[position] != ' ') ++position;
    const std::string_view word = text.substr(start, position - start);
    return word;
}

// A decimal number of the text form; false when `word` is not one.
bool parse_extent(std::string_view word, double& value) noexcept {
    if (word.empty()) return false;
    const char* const end = word.data() + word.size();
    const std::from_chars_result parsed = std::from_chars(word.data(), end, value);
    const bool whole = parsed.ec == std::errc{} && parsed.ptr == end;
    return whole;
}

// The shortest text that reads back as `value`.
void append_number(std::string& text, double value) {
    std::array<char, 32U> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    text.append(buffer.data(), static_cast<std::size_t>(written.ptr - buffer.data()));
}

// The group spec's text form ("176 176 176" or "176 176 176 priority 2");
// the empty text is none. Parsing throws std::invalid_argument for text of
// another shape; the extents' order is the panel's check, on the write.
std::string format_command_overflow_group_spec(const std::optional<CommandOverflowGroupSpec>& spec) {
    std::string text;
    if (!spec) return text;
    append_number(text, (*spec).minimum);
    text.push_back(' ');
    append_number(text, (*spec).preferred);
    text.push_back(' ');
    append_number(text, (*spec).maximum);
    if ((*spec).collapse_priority) {
        text.append(" priority ");
        text.append(std::to_string(*(*spec).collapse_priority));
    }
    return text;
}

std::optional<CommandOverflowGroupSpec> parse_command_overflow_group_spec(std::string_view text) {
    std::size_t position = 0U;
    const std::string_view first = next_word(text, position);
    if (first.empty()) return std::nullopt;
    CommandOverflowGroupSpec spec{};
    const std::string_view second = next_word(text, position);
    const std::string_view third = next_word(text, position);
    if (!parse_extent(first, spec.minimum) || !parse_extent(second, spec.preferred) ||
        !parse_extent(third, spec.maximum)) {
        throw std::invalid_argument("CommandOverflowPanel group spec needs three extents");
    }
    const std::string_view keyword = next_word(text, position);
    if (!keyword.empty()) {
        const std::string_view priority = next_word(text, position);
        std::uint16_t parsed_priority{};
        const char* const end = priority.data() + priority.size();
        const std::from_chars_result parsed = std::from_chars(priority.data(), end, parsed_priority);
        if (keyword != "priority" || priority.empty() || parsed.ec != std::errc{} || parsed.ptr != end) {
            throw std::invalid_argument("CommandOverflowPanel group spec ends with priority and a number to 65535");
        }
        spec.collapse_priority = parsed_priority;
    }
    if (!next_word(text, position).empty()) {
        throw std::invalid_argument("CommandOverflowPanel group spec has text after its priority");
    }
    return spec;
}

const PropertyDescriptor& describe_group_spec() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        PropertyDescriptor{"group_spec", BindingValueKind::text, "Layout",
                           "The child's minimum, preferred and maximum extents and optional collapse priority, as "
                           "\"176 176 176 priority 2\"; empty: a non-collapsible child at its desired width.",
                           BindingValue{std::string{}}, invalidation::bounds},
        PropertyOperations{true, true, false, false});
    return descriptor;
}

BindingValue read_group_spec(const Component& container, const Control& child) {
    const CommandOverflowPanel& panel = static_cast<const CommandOverflowPanel&>(container);
    const std::optional<CommandOverflowGroupSpec> spec = panel.group_spec[child].read();
    BindingValue value{format_command_overflow_group_spec(spec)};
    return value;
}

void write_group_spec(Component& container, const Control& child, const BindingValue& value) {
    CommandOverflowPanel& panel = static_cast<CommandOverflowPanel&>(container);
    std::optional<CommandOverflowGroupSpec> spec = parse_command_overflow_group_spec(std::get<std::string>(value));
    panel.group_spec[child] = std::move(spec);
}

} // namespace

struct CommandOverflowPanel::LayoutResult final {
    CommandOverflowSnapshot snapshot;
    Size desired;
};

CommandOverflowPanel::CommandOverflowPanel(StableId stable_id)
    : ContainerControl(std::move(stable_id)) {}

const std::array<ChildPropertyEntry, CommandOverflowPanel::overflow_child_property_count>&
CommandOverflowPanel::overflow_child_property_entries() noexcept {
    static constexpr std::array<ChildPropertyEntry, overflow_child_property_count> entries{{
        {"group_spec", &describe_group_spec, &read_group_spec, &write_group_spec, nullptr, nullptr},
    }};
    static_assert(property_names_distinct(entries), "CommandOverflowPanel's child table names a property twice");
    return entries;
}

std::span<const ChildPropertyEntry> CommandOverflowPanel::child_property_table() const noexcept {
    const std::array<ChildPropertyEntry, overflow_child_property_count>& entries = overflow_child_property_entries();
    return std::span<const ChildPropertyEntry>(entries);
}

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

void CommandOverflowPanel::prepare_write(const Control& child,
                                         const std::optional<CommandOverflowGroupSpec>& incoming) const {
    require_mutable();
    require_direct_child(child);
    if (!incoming) return;
    const CommandOverflowGroupSpec& spec = *incoming;
    validate_spec(spec);
    const std::shared_ptr<Control> actuator = overflow_actuator_.lock();
    if (actuator && actuator.get() == &child) {
        throw std::invalid_argument("CommandOverflowPanel actuator cannot also be a command group");
    }
    const std::uint64_t id = child.runtime_id().value;
    for (const GroupSpecMap::value_type& entry : group_specs_) {
        if (entry.first != id && spec.collapse_priority && entry.second.collapse_priority == spec.collapse_priority) {
            throw std::invalid_argument("CommandOverflowPanel collapse priorities must be unique");
        }
    }
    if (group_specs_.size() >= maximum_command_groups && !group_specs_.contains(id)) {
        throw std::invalid_argument("CommandOverflowPanel group count exceeds 32");
    }
}

void CommandOverflowPanel::written(const Control& child) {
    static_cast<void>(child);
    invalidate(invalidation::bounds);
}

bool CommandOverflowPanel::replace_group_spec(const Control& child, std::optional<CommandOverflowGroupSpec> value) {
    const std::uint64_t id = child.runtime_id().value;
    const GroupSpecMap::iterator found = group_specs_.find(id);
    if (!value) {
        if (found == group_specs_.end()) return false;
        group_specs_.erase(found);
        return true;
    }
    if (found != group_specs_.end() && (*found).second == *value) return false;
    group_specs_[id] = std::move(*value);
    return true;
}

std::optional<CommandOverflowGroupSpec> CommandOverflowPanel::stored_group_spec(const Control& child) const {
    require_direct_child(child);
    const GroupSpecMap::const_iterator found = group_specs_.find(child.runtime_id().value);
    if (found == group_specs_.end()) return std::nullopt;
    std::optional<CommandOverflowGroupSpec> value = (*found).second;
    return value;
}

void CommandOverflowPanel::child_leaving(const Control& child) noexcept {
    group_specs_.erase(child.runtime_id().value);
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
    const Insets inset = padding.read();
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
        if (!is_current_layout_child(child) || !(*child).visible.read() ||
            child == overflow_actuator_.lock()) {
            continue;
        }
        const Size desired = preferred_child_size(child, inner);
        if (!is_alive()) return {};
        if (!is_current_layout_child(child) || !(*child).visible.read()) continue;
        const GroupSpecMap::iterator found = group_specs_.find((*child).runtime_id().value);
        CommandOverflowGroupSpec spec;
        if (found == group_specs_.end()) {
            spec.minimum = desired.width + horizontal_extent((*child).margin.read());
            spec.preferred = spec.minimum;
            spec.maximum = spec.minimum;
        } else {
            spec = (*found).second;
        }
        items.push_back({child, spec, desired, (*child).margin.read()});
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
    // A panel with no groups lays out empty: the track resolver requires
    // 1 to 64 tracks and refuses none, which made a freshly made panel throw
    // in its first layout (the stock specimen catalogue found it).
    ResponsiveTrackResolution resolution{};
    resolution.available_extent = inner.width;
    resolution.gap = group_gap.read();
    resolution.device_scale = device_scale;
    if (!tracks.empty()) {
        resolution = resolve_responsive_tracks(tracks, measured, inner.width, group_gap.read(), device_scale,
                                               protected_tracks);
    }
    bool needs_actuator = !resolution.collapse_order.empty();
    const Control::Ptr actuator = overflow_actuator_.lock();
    Size actuator_desired{};
    Insets actuator_margin{};
    double actuator_extent = 0.0;
    if (actuator && (*actuator).visible.read()) {
        actuator_desired = preferred_child_size(actuator, inner);
        actuator_margin = (*actuator).margin.read();
        actuator_extent = actuator_desired.width + horizontal_extent(actuator_margin);
    }
    if (needs_actuator && actuator) {
        const double reserved = actuator_extent + (items.empty() ? 0.0 : group_gap.read());
        resolution = resolve_responsive_tracks(
            tracks, measured, std::max(0.0, inner.width - reserved),
            group_gap.read(), device_scale, protected_tracks);
        needs_actuator = true;
        if (assign && focused_track && resolution.overflow && owner != nullptr) {
            ResponsiveTrackResolution alternative = resolve_responsive_tracks(
                tracks, measured, std::max(0.0, inner.width - reserved),
                group_gap.read(), device_scale);
            if (alternative.tracks[*focused_track].collapsed() &&
                (*actuator).focusable.read() && (*owner).request_focus(actuator)) {
                resolution = std::move(alternative);
            }
        }
    }

    CommandOverflowSnapshot snapshot;
    snapshot.available_extent = inner.width;
    snapshot.group_gap = group_gap.read();
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
    const double desired_groups = desired_track_extent(tracks, group_gap.read());

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
    if (auto_size.read()) {
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
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
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

// property_codemod begin CommandOverflowPanel (tools/property_families/command_overflow_panel.json; generated, do not edit)

namespace {

PropertyDescriptor command_overflow_panel_group_gap_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "group_gap";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Layout";
    descriptor.description = "The space between neighbouring groups and before the actuator, 0 through 256.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = invalidation::bounds;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_command_overflow_panel_group_gap() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        command_overflow_panel_group_gap_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, CommandOverflowPanel::command_overflow_panel_property_count>& CommandOverflowPanel::command_overflow_panel_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"group_gap", &describe_command_overflow_panel_group_gap,
         &read_cell_entry<CommandOverflowPanel, decltype(&CommandOverflowPanel::group_gap), &CommandOverflowPanel::group_gap>,
         &write_cell_entry<CommandOverflowPanel, decltype(&CommandOverflowPanel::group_gap), &CommandOverflowPanel::group_gap>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "CommandOverflowPanel's property table names a property twice");
    static const std::array<PropertyEntry, command_overflow_panel_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> CommandOverflowPanel::property_table() const noexcept {
    const std::array<PropertyEntry, command_overflow_panel_property_count>& entries = command_overflow_panel_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void CommandOverflowPanel::prepare_write(const decltype(group_gap)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.0 || incoming > 256.0) throw std::invalid_argument("CommandOverflowPanel gap must be finite and between zero and 256");
}

void CommandOverflowPanel::written(const decltype(group_gap)&) {
    invalidate(invalidation::bounds);
}

// property_codemod end CommandOverflowPanel

} // namespace gui_forms
