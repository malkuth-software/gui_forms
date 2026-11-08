#include "gui_forms/controls/scrollable_control/container_control/responsive_track_panel/responsive_track_panel.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/window.hpp"

#include "../container_layout_utilities.hpp"
#include "../text_collection_property.hpp"
#include "../../scrollable_anatomy.hpp"
#include "../../../../core/types/restrict/restrict.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

namespace gui_forms {
namespace {

std::shared_ptr<const PropertyEnumDescriptor> orientation_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "responsive_track_orientation", {{"horizontal", 0}, {"vertical", 1}}, false});
    return value;
}

void append_number(std::string& text, double value) {
    std::array<char, 32U> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    text.append(buffer.data(), static_cast<std::size_t>(written.ptr - buffer.data()));
}

// A number word of a spec's text; false when `word` is not one.
bool number_word(std::string_view word, double& value) noexcept {
    const char* const end = word.data() + word.size();
    const std::from_chars_result parsed = std::from_chars(word.data(), end, value);
    const bool whole = !word.empty() && parsed.ec == std::errc{} && parsed.ptr == end;
    return whole;
}

const char* mode_word(ResponsiveTrackSizeMode mode) noexcept {
    switch (mode) {
    case ResponsiveTrackSizeMode::fixed:
        return "fixed";
    case ResponsiveTrackSizeMode::content:
        return "content";
    case ResponsiveTrackSizeMode::remaining:
        return "remaining";
    default:
        // validate_specs admits only the enumerators; content is the
        // spec's default mode, so a read never fails.
        return "content";
    }
}

// One spec's text: the mode and three extents, then the weight when it is
// not 1, "hidden", and the priority when there is one.
std::string spec_text(const ResponsiveTrackSpec& spec) {
    std::string text{mode_word(spec.size_mode)};
    text.push_back(' ');
    append_number(text, spec.minimum);
    text.push_back(' ');
    append_number(text, spec.preferred);
    text.push_back(' ');
    append_number(text, spec.maximum);
    if (spec.remaining_weight != 1.0) {
        text.append(" weight ");
        append_number(text, spec.remaining_weight);
    }
    if (spec.hidden) text.append(" hidden");
    if (spec.collapse_priority) {
        text.append(" priority ");
        text.append(std::to_string(*spec.collapse_priority));
    }
    return text;
}

// One spec read from its text; throws std::invalid_argument for another
// shape. The values' ranges are validate_specs'.
ResponsiveTrackSpec spec_from_text(std::string_view item) {
    const std::vector<std::string_view> words = text_collection_property::words_of(item);
    ResponsiveTrackSpec spec{};
    bool valid = words.size() >= 4U;
    if (valid) {
        if (words[0] == "fixed") {
            spec.size_mode = ResponsiveTrackSizeMode::fixed;
        } else if (words[0] == "content") {
            spec.size_mode = ResponsiveTrackSizeMode::content;
        } else if (words[0] == "remaining") {
            spec.size_mode = ResponsiveTrackSizeMode::remaining;
        } else {
            valid = false;
        }
    }
    valid = valid && number_word(words[1], spec.minimum) && number_word(words[2], spec.preferred) &&
            number_word(words[3], spec.maximum);
    std::size_t index = 4U;
    while (valid && index < words.size()) {
        if (words[index] == "hidden") {
            spec.hidden = true;
            ++index;
        } else if (words[index] == "weight" && index + 1U < words.size()) {
            valid = number_word(words[index + 1U], spec.remaining_weight);
            index += 2U;
        } else if (words[index] == "priority" && index + 1U < words.size()) {
            std::uint16_t priority{};
            const std::string_view word = words[index + 1U];
            const char* const end = word.data() + word.size();
            const std::from_chars_result parsed = std::from_chars(word.data(), end, priority);
            valid = parsed.ec == std::errc{} && parsed.ptr == end;
            spec.collapse_priority = priority;
            index += 2U;
        } else {
            valid = false;
        }
    }
    if (!valid) {
        throw std::invalid_argument(
            "ResponsiveTrackPanel track spec is \"<fixed|content|remaining> <minimum> <preferred> <maximum>\" "
            "with optional \"weight <w>\", \"hidden\" and \"priority <n>\"");
    }
    return spec;
}

// The child track's descriptor before validation: nullable, so its default
// (none) is of its kind.
PropertyDescriptor nullable_child_track() {
    PropertyDescriptor descriptor{"child_track", BindingValueKind::unsigned_integer, "Layout",
                                  "The track the child occupies; none takes the next track no child names.",
                                  BindingValue{}, invalidation::bounds};
    descriptor.nullable = true;
    // The write is the child view's assignment (generated forms assign it).
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_child_track() {
    static const PropertyDescriptor descriptor = property_table_descriptor(nullable_child_track(),
                                                                           PropertyOperations{true, true, false, false});
    return descriptor;
}

BindingValue read_child_track(const Component& container, const Control& child) {
    const ResponsiveTrackPanel& panel = static_cast<const ResponsiveTrackPanel&>(container);
    const std::optional<std::uint32_t> track = panel.child_track[child].read();
    const BindingValue value = PropertyValueTraits<std::optional<std::uint32_t>>::to_binding(track);
    return value;
}

// Writes a null (none) or an unsigned track index; an index past 32 bits
// cannot name a track, so it is refused as out of range.
void write_child_track(Component& container, const Control& child, const BindingValue& value) {
    ResponsiveTrackPanel& panel = static_cast<ResponsiveTrackPanel&>(container);
    if (std::holds_alternative<std::monostate>(value)) {
        panel.child_track[child] = std::nullopt;
        return;
    }
    const std::uint64_t index = std::get<std::uint64_t>(value);
    if (index > std::numeric_limits<std::uint32_t>::max()) {
        throw std::out_of_range("ResponsiveTrackPanel child track index");
    }
    panel.child_track[child] = static_cast<std::uint32_t>(index);
}

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

BindingValue ResponsiveTrackPanel::track_specs_property() const {
    std::vector<std::string> items{};
    items.reserve(track_specs_.size());
    for (const ResponsiveTrackSpec& spec : track_specs_) items.push_back(spec_text(spec));
    BindingValue value = text_collection_property::value_of(items);
    return value;
}

void ResponsiveTrackPanel::set_track_specs_property(const BindingValue& value) {
    const std::vector<std::string> items = text_collection_property::items_of(value, "ResponsiveTrackPanel.track_specs");
    std::vector<ResponsiveTrackSpec> specs{};
    specs.reserve(items.size());
    for (const std::string& item : items) specs.push_back(spec_from_text(item));
    set_track_specs(std::move(specs));
}

void ResponsiveTrackPanel::validate_specs(
    std::span<const ResponsiveTrackSpec> specs) {
    validate_track_specs(specs);
}

const std::array<ChildPropertyEntry, ResponsiveTrackPanel::track_child_property_count>&
ResponsiveTrackPanel::track_child_property_entries() noexcept {
    static constexpr std::array<ChildPropertyEntry, track_child_property_count> entries{{
        {"child_track", &describe_child_track, &read_child_track, &write_child_track, nullptr, nullptr},
    }};
    static_assert(property_names_distinct(entries), "ResponsiveTrackPanel's child table names a property twice");
    return entries;
}

std::span<const ChildPropertyEntry> ResponsiveTrackPanel::child_property_table() const noexcept {
    const std::array<ChildPropertyEntry, track_child_property_count>& entries = track_child_property_entries();
    return std::span<const ChildPropertyEntry>(entries);
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

void ResponsiveTrackPanel::prepare_write(const Control& child,
                                         const std::optional<std::uint32_t>& incoming) const {
    require_mutable();
    require_direct_child(child);
    if (incoming && *incoming >= track_specs_.size()) {
        throw std::out_of_range("ResponsiveTrackPanel child track index");
    }
}

void ResponsiveTrackPanel::written(const Control& child) {
    static_cast<void>(child);
    invalidate(invalidation::bounds);
}

bool ResponsiveTrackPanel::replace_child_track(const Control& child, std::optional<std::uint32_t> value) {
    const std::uint64_t id = child.runtime_id().value;
    const ChildTrackMap::iterator found = child_tracks_.find(id);
    if (!value) {
        if (found == child_tracks_.end()) return false;
        child_tracks_.erase(found);
        return true;
    }
    if (found != child_tracks_.end() && (*found).second == *value) return false;
    child_tracks_[id] = *value;
    return true;
}

std::optional<std::uint32_t> ResponsiveTrackPanel::stored_child_track(const Control& child) const {
    require_direct_child(child);
    const ChildTrackMap::const_iterator found = child_tracks_.find(child.runtime_id().value);
    if (found == child_tracks_.end()) return std::nullopt;
    const std::optional<std::uint32_t> value = static_cast<std::uint32_t>((*found).second);
    return value;
}

void ResponsiveTrackPanel::child_leaving(const Control& child) noexcept {
    child_tracks_.erase(child.runtime_id().value);
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
    return {orientation.read(), committed_resolution_, layout_transaction_state(),
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
    const Insets inset = padding.read();
    const Size inner{
        std::max(0.0, available.width - horizontal_extent(inset)),
        std::max(0.0, available.height - vertical_extent(inset))};
    const bool horizontal =
        orientation.read() == ResponsiveTrackOrientation::horizontal;
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
        if (!is_current_layout_child(child) || !(*child).visible.read()) continue;
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
        if (!is_current_layout_child(child) || !(*child).visible.read()) continue;
        const Insets margin = (*child).margin.read();
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
        track_specs_, measured, main_extent, track_gap.read(), device_scale,
        protected_tracks);

    if (assign && focused_track && resolution.overflow && owner != nullptr) {
        std::vector<std::size_t> reveal_only(
            revealed_tracks_.begin(), revealed_tracks_.end());
        std::sort(reveal_only.begin(), reveal_only.end());
        ResponsiveTrackResolution alternative = resolve_responsive_tracks(
            track_specs_, measured, main_extent, track_gap.read(), device_scale,
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
            const Rect placed = place_in_cell((*item.control).dock.read(), (*item.control).anchor.read(),
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
    const Insets inset = padding.read();
    if (auto_size.read()) {
        if (orientation.read() == ResponsiveTrackOrientation::horizontal) {
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
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
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

namespace {

// The track specifications as text items: a hand table entry
// (responsive_track_panel.json extra_entries).
PropertyDescriptor responsive_track_panel_track_specs_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "track_specs";
    descriptor.kind = BindingValueKind::collection;
    descriptor.category = "Layout";
    descriptor.description = "One item per track: \"<fixed|content|remaining> <minimum> <preferred> <maximum>\", optionally followed by \"weight <w>\", \"hidden\" and \"priority <n>\".";
    descriptor.default_value = text_collection_property::value_of(std::array<std::string, 1U>{spec_text(ResponsiveTrackSpec{})});
    descriptor.invalidation_effects = invalidation::bounds;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_responsive_track_panel_track_specs() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        responsive_track_panel_track_specs_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

// property_codemod begin ResponsiveTrackPanel (tools/property_families/responsive_track_panel.json; generated, do not edit)

namespace {

PropertyDescriptor responsive_track_panel_orientation_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "orientation";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Layout";
    descriptor.description = "The axis the tracks run along: horizontal or vertical.";
    descriptor.default_value = BindingValue{std::int64_t{0}};
    descriptor.invalidation_effects = invalidation::bounds;
    descriptor.bindable = false;
    descriptor.enumeration = orientation_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_responsive_track_panel_orientation() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        responsive_track_panel_orientation_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor responsive_track_panel_track_gap_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "track_gap";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Layout";
    descriptor.description = "The space between neighbouring tracks.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = invalidation::bounds;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_responsive_track_panel_track_gap() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        responsive_track_panel_track_gap_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<ResponsiveTrackOrientation>::type() {
    return PropertyType{BindingValueKind::enumeration, false, orientation_enum()};
}

BindingValue PropertyValueTraits<ResponsiveTrackOrientation>::to_binding(ResponsiveTrackOrientation value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = orientation_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

ResponsiveTrackOrientation PropertyValueTraits<ResponsiveTrackOrientation>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<ResponsiveTrackOrientation>(choice.value);
}

const std::array<PropertyEntry, ResponsiveTrackPanel::responsive_track_panel_property_count>& ResponsiveTrackPanel::responsive_track_panel_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"orientation", &describe_responsive_track_panel_orientation,
         &read_cell_entry<ResponsiveTrackPanel, decltype(&ResponsiveTrackPanel::orientation), &ResponsiveTrackPanel::orientation>,
         &write_cell_entry<ResponsiveTrackPanel, decltype(&ResponsiveTrackPanel::orientation), &ResponsiveTrackPanel::orientation>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"track_gap", &describe_responsive_track_panel_track_gap,
         &read_cell_entry<ResponsiveTrackPanel, decltype(&ResponsiveTrackPanel::track_gap), &ResponsiveTrackPanel::track_gap>,
         &write_cell_entry<ResponsiveTrackPanel, decltype(&ResponsiveTrackPanel::track_gap), &ResponsiveTrackPanel::track_gap>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"track_specs", &describe_responsive_track_panel_track_specs,
         &read_member_entry<ResponsiveTrackPanel, decltype(&ResponsiveTrackPanel::track_specs_property), &ResponsiveTrackPanel::track_specs_property>,
         &write_member_entry<ResponsiveTrackPanel, decltype(&ResponsiveTrackPanel::set_track_specs_property), &ResponsiveTrackPanel::set_track_specs_property>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "ResponsiveTrackPanel's property table names a property twice");
    static const std::array<PropertyEntry, responsive_track_panel_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> ResponsiveTrackPanel::property_table() const noexcept {
    const std::array<PropertyEntry, responsive_track_panel_property_count>& entries = responsive_track_panel_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void ResponsiveTrackPanel::prepare_write(const decltype(orientation)&, ResponsiveTrackOrientation& incoming) {
    require_mutable();
    if (incoming > ResponsiveTrackOrientation::vertical) throw std::invalid_argument("ResponsiveTrackPanel orientation is unknown");
}

void ResponsiveTrackPanel::written(const decltype(orientation)&) {
    invalidate(invalidation::bounds);
}

void ResponsiveTrackPanel::prepare_write(const decltype(track_gap)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.0 || incoming > 256.0) throw std::invalid_argument("ResponsiveTrackPanel gap must be finite and between zero and 256");
}

void ResponsiveTrackPanel::written(const decltype(track_gap)&) {
    invalidate(invalidation::bounds);
}

// property_codemod end ResponsiveTrackPanel

} // namespace gui_forms
