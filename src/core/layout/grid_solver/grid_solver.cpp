// The grid solver (stage L, phase L4): CSS Grid Layout's placement (8.5)
// and track sizing (12.3 to 12.8) over caller-resolved records. Columns
// are computed flow-relative (inline-start to inline-end) and mirrored
// once, at output, for right-to-left.

#include "gui_forms/layout/grid_solver/grid_solver.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms {

namespace {

using Breadth = GridLayoutWorkspace::Breadth;
using TrackFunction = GridLayoutWorkspace::TrackFunction;
using AxisPlacement = GridLayoutWorkspace::AxisPlacement;

constexpr double infinity = std::numeric_limits<double>::infinity();
// Space below this is none: sums of logical lengths such as 0.1 + 0.2 must
// not leave a distribution step with a sliver to hand out.
constexpr double distribution_tolerance = 1.0e-9;

// ---------------------------------------------------------------------------
// Lengths and sizing functions

struct ResolvedLength final {
    double value{};
    bool definite{};
};

// A length-percentage against an axis: a percentage of an indefinite axis
// is unresolvable (CSS Grid 7.2: treated as `auto` for track sizing).
ResolvedLength resolve_length(const LengthPercentage& length, const GridAvailableExtent& extent) noexcept {
    ResolvedLength resolved{};
    if (length.percent == 0.0) {
        resolved.value = length.length.value();
        resolved.definite = true;
        return resolved;
    }
    if (extent.kind != GridSizingKind::definite) return resolved;
    resolved.value = length.length.value() + length.percent * extent.extent / 100.0;
    resolved.definite = true;
    return resolved;
}

TrackFunction track_function(const GridTrackSize& size, const GridAvailableExtent& extent) noexcept {
    TrackFunction function{};
    if (size.fit_content) {
        const ResolvedLength limit = resolve_length(size.fit_content_limit, extent);
        function.minimum = Breadth::automatic;
        function.maximum = limit.definite ? Breadth::fit_content : Breadth::max_content;
        function.maximum_value = std::max(0.0, limit.value);
        return function;
    }
    switch (size.minimum.kind) {
    case TrackBreadthKind::length_percentage: {
        const ResolvedLength minimum = resolve_length(size.minimum.length, extent);
        function.minimum = minimum.definite ? Breadth::fixed : Breadth::automatic;
        function.minimum_value = std::max(0.0, minimum.value);
        break;
    }
    case TrackBreadthKind::min_content:
        function.minimum = Breadth::min_content;
        break;
    case TrackBreadthKind::max_content:
        function.minimum = Breadth::max_content;
        break;
    case TrackBreadthKind::fraction:
    case TrackBreadthKind::automatic:
        function.minimum = Breadth::automatic;
        break;
    default: // Not a TrackBreadthKind: the minimum is `auto`, the initial value.
        function.minimum = Breadth::automatic;
        break;
    }
    switch (size.maximum.kind) {
    case TrackBreadthKind::length_percentage: {
        const ResolvedLength maximum = resolve_length(size.maximum.length, extent);
        function.maximum = maximum.definite ? Breadth::fixed : Breadth::automatic;
        function.maximum_value = std::max(0.0, maximum.value);
        break;
    }
    case TrackBreadthKind::fraction:
        function.maximum = Breadth::fraction;
        function.maximum_value = size.maximum.fraction.value();
        break;
    case TrackBreadthKind::min_content:
        function.maximum = Breadth::min_content;
        break;
    case TrackBreadthKind::max_content:
        function.maximum = Breadth::max_content;
        break;
    case TrackBreadthKind::automatic:
        function.maximum = Breadth::automatic;
        break;
    default: // Not a TrackBreadthKind: the maximum is `auto`, the initial value.
        function.maximum = Breadth::automatic;
        break;
    }
    return function;
}

bool intrinsic_breadth(Breadth breadth) noexcept {
    return breadth == Breadth::min_content || breadth == Breadth::max_content || breadth == Breadth::automatic ||
           breadth == Breadth::fit_content;
}

bool flexible(const TrackFunction& function) noexcept { return function.maximum == Breadth::fraction; }

// Whether the maximum is a fixed sizing function (CSS Grid's definition: a
// length or percentage, never fit-content()).
bool fixed_maximum(const TrackFunction& function) noexcept { return function.maximum == Breadth::fixed; }

// Whether the maximum limits a contribution (12.5's limited contributions):
// a fixed breadth, or the fit-content() argument.
bool limiting_maximum(const TrackFunction& function) noexcept {
    return function.maximum == Breadth::fixed || function.maximum == Breadth::fit_content;
}

// ---------------------------------------------------------------------------
// Axes

struct AxisView final {
    std::vector<GridTrack>* tracks{};
    std::vector<TrackFunction>* functions{};
    GridAvailableExtent extent{};
    double gap{};
};

AxisView axis_view(GridAxis axis, const GridContainerInput& container, std::vector<GridTrack>& columns,
                   std::vector<GridTrack>& rows, std::vector<TrackFunction>& column_functions,
                   std::vector<TrackFunction>& row_functions) noexcept {
    AxisView view{};
    if (axis == GridAxis::columns) {
        view.tracks = &columns;
        view.functions = &column_functions;
        view.extent = container.width;
        view.gap = container.column_gap;
    } else {
        view.tracks = &rows;
        view.functions = &row_functions;
        view.extent = container.height;
        view.gap = container.row_gap;
    }
    return view;
}

struct TrackRange final {
    std::uint32_t start{};
    std::uint32_t end{};
};

TrackRange item_range(const GridItemArea& area, GridAxis axis) noexcept {
    TrackRange range{};
    if (axis == GridAxis::columns) {
        range.start = area.column_start;
        range.end = area.column_end;
    } else {
        range.start = area.row_start;
        range.end = area.row_end;
    }
    return range;
}

// The gaps inside a range: one between each pair of neighbouring tracks
// that have not collapsed.
double inner_gaps(const std::vector<GridTrack>& tracks, TrackRange range, double gap) noexcept {
    std::uint32_t visible = 0U;
    for (std::uint32_t index = range.start; index < range.end; ++index) {
        if (!tracks[index].collapsed) ++visible;
    }
    return visible > 1U ? gap * static_cast<double>(visible - 1U) : 0.0;
}

double all_gaps(const std::vector<GridTrack>& tracks, double gap) noexcept {
    const TrackRange range{0U, static_cast<std::uint32_t>(tracks.size())};
    return inner_gaps(tracks, range, gap);
}

double base_sum(const std::vector<GridTrack>& tracks) noexcept {
    double sum = 0.0;
    for (const GridTrack& track : tracks) sum += track.base;
    return sum;
}

struct MarginPair final {
    GridMargin start{};
    GridMargin end{};
};

// The margins at the axis's start and end: inline-start is the physical
// right in a right-to-left container.
MarginPair axis_margins(const GridItemInput& item, GridAxis axis, bool right_to_left) noexcept {
    MarginPair pair{};
    if (axis == GridAxis::rows) {
        pair.start = item.margin.top;
        pair.end = item.margin.bottom;
    } else if (right_to_left) {
        pair.start = item.margin.right;
        pair.end = item.margin.left;
    } else {
        pair.start = item.margin.left;
        pair.end = item.margin.right;
    }
    return pair;
}

double fixed_margins(const MarginPair& margins) noexcept {
    const double start = margins.start.automatic ? 0.0 : margins.start.length;
    const double end = margins.end.automatic ? 0.0 : margins.end.length;
    return start + end;
}

bool right_to_left(const GridContainerInput& container) noexcept {
    return container.layout_direction == LayoutDirection::right_to_left;
}

// ---------------------------------------------------------------------------
// Contributions (CSS Grid 6.6 and 12.5)

struct SpanFacts final {
    bool spans_automatic_minimum{};
    bool spans_flexible{};
    // Every spanned maximum is fixed, and their sum: the area 6.6 clamps
    // an automatic minimum to.
    bool all_fixed_maximum{true};
    double fixed_maximum_sum{};
    // Every spanned maximum limits a contribution (fixed, or a
    // fit-content() argument), and their sum.
    bool all_limiting_maximum{true};
    double limiting_maximum_sum{};
};

SpanFacts span_facts(const std::vector<TrackFunction>& functions, TrackRange range) noexcept {
    SpanFacts facts{};
    for (std::uint32_t index = range.start; index < range.end; ++index) {
        const TrackFunction& function = functions[index];
        if (function.minimum == Breadth::automatic) facts.spans_automatic_minimum = true;
        if (flexible(function)) facts.spans_flexible = true;
        if (fixed_maximum(function)) {
            facts.fixed_maximum_sum += function.maximum_value;
        } else {
            facts.all_fixed_maximum = false;
        }
        if (limiting_maximum(function)) {
            facts.limiting_maximum_sum += function.maximum_value;
        } else {
            facts.all_limiting_maximum = false;
        }
    }
    return facts;
}

// The item's minimum contribution with 6.6's rule applied: the automatic
// minimum is content-based only where it spans an `auto` minimum (and no
// flexible track when it spans several), and then no larger than an area
// of fixed maximums.
double applied_minimum(const GridItemContribution& contribution, const SpanFacts& facts, TrackRange range,
                       const std::vector<GridTrack>& tracks, double gap, double margins) noexcept {
    if (!contribution.automatic_minimum) return contribution.minimum;
    const std::uint32_t span = range.end - range.start;
    const bool content_based = facts.spans_automatic_minimum && (span == 1U || !facts.spans_flexible);
    if (!content_based) return margins;
    double minimum = contribution.minimum;
    if (facts.all_fixed_maximum) {
        const double area = facts.fixed_maximum_sum + inner_gaps(tracks, range, gap);
        minimum = std::max(margins, std::min(minimum, area));
    }
    return minimum;
}

// A min-content or max-content contribution limited by the spanned tracks'
// fixed maximums or fit-content() arguments and floored by the minimum
// contribution (12.5, under a min-content or max-content constraint).
double limited_contribution(double contribution, double minimum, const SpanFacts& facts, TrackRange range,
                            const std::vector<GridTrack>& tracks, double gap) noexcept {
    double limited = contribution;
    if (facts.all_limiting_maximum) {
        limited = std::min(limited, facts.limiting_maximum_sum + inner_gaps(tracks, range, gap));
    }
    return std::max(limited, minimum);
}

// ---------------------------------------------------------------------------
// Distribution of extra space across spanned tracks (12.5.1)

enum class SizeTarget : std::uint8_t { base, growth };
enum class ContributionKind : std::uint8_t { minimum, min_content, max_content };
enum class AffectedTracks : std::uint8_t {
    // Tracks with an intrinsic minimum (min-content, max-content, auto).
    intrinsic_minimum,
    // Tracks with a min-content or max-content minimum.
    content_minimum,
    max_content_minimum,
    intrinsic_maximum,
    max_content_maximum,
};

bool affected(AffectedTracks which, const TrackFunction& function, bool flexible_only) noexcept {
    if (flexible_only && !flexible(function)) return false;
    switch (which) {
    case AffectedTracks::intrinsic_minimum:
        return function.minimum == Breadth::min_content || function.minimum == Breadth::max_content ||
               function.minimum == Breadth::automatic;
    case AffectedTracks::content_minimum:
        return function.minimum == Breadth::min_content || function.minimum == Breadth::max_content;
    case AffectedTracks::max_content_minimum:
        return function.minimum == Breadth::max_content;
    case AffectedTracks::intrinsic_maximum:
        return intrinsic_breadth(function.maximum);
    case AffectedTracks::max_content_maximum:
        return function.maximum == Breadth::max_content || function.maximum == Breadth::automatic ||
               function.maximum == Breadth::fit_content;
    }
    return false;
}

// One spanning step's settings.
struct SpanningStep final {
    SizeTarget target{SizeTarget::base};
    ContributionKind contribution{ContributionKind::minimum};
    AffectedTracks tracks{AffectedTracks::intrinsic_minimum};
    // Step 4: only flexible tracks take space, in proportion to their
    // fractions.
    bool flexible_only{};
    // Step 3.5 marks tracks whose growth limit became finite.
    bool marks_growable{};
};

// The scratch the distribution reads and writes, borrowed from the
// workspace for one sizing call.
struct DistributionScratch final {
    std::vector<double>* planned{};
    std::vector<double>* incurred{};
    std::vector<std::uint8_t>* frozen{};
    std::vector<std::uint8_t>* growable{};
};

double affected_size(const GridTrack& track, SizeTarget target) noexcept {
    if (target == SizeTarget::base) return track.base;
    return std::isinf(track.growth_limit) ? track.base : track.growth_limit;
}

// How far a track's affected size may grow in the first phase.
double distribution_limit(const GridTrack& track, const TrackFunction& function, SizeTarget target,
                          bool growable) noexcept {
    double limit = infinity;
    if (target == SizeTarget::base) {
        limit = track.growth_limit;
    } else if (!growable && !std::isinf(track.growth_limit)) {
        limit = track.growth_limit;
    }
    if (function.maximum == Breadth::fit_content) limit = std::min(limit, function.maximum_value);
    return limit;
}

// Whether a track takes space beyond its limit (12.5.1 step 2.3). A
// fit-content() track that reached its limit counts as fixed.
bool beyond_limit_candidate(const SpanningStep& step, const GridTrack& track, const TrackFunction& function,
                            double incurred) noexcept {
    const bool reached_fit_limit = function.maximum == Breadth::fit_content &&
                                   affected_size(track, step.target) + incurred >= function.maximum_value;
    if (step.target == SizeTarget::growth) return !reached_fit_limit;
    if (reached_fit_limit) return false;
    if (step.contribution == ContributionKind::max_content) {
        return function.maximum == Breadth::max_content || function.maximum == Breadth::automatic ||
               function.maximum == Breadth::fit_content;
    }
    return intrinsic_breadth(function.maximum);
}

// Shares `space` among the unfrozen affected tracks of `range` equally, each
// up to its limit (or without limits when `limited` is false); returns what
// is left.
double share_equally(const SpanningStep& step, const std::vector<GridTrack>& tracks,
                     const std::vector<TrackFunction>& functions, TrackRange range, double space, bool limited,
                     DistributionScratch& scratch) {
    std::vector<double>& incurred = *scratch.incurred;
    std::vector<std::uint8_t>& frozen = *scratch.frozen;
    const std::vector<std::uint8_t>& growable = *scratch.growable;
    double remaining = space;
    while (remaining > distribution_tolerance) {
        std::uint32_t open = 0U;
        for (std::uint32_t index = range.start; index < range.end; ++index) {
            if (frozen[index] == 0U) ++open;
        }
        if (open == 0U) break;
        const double share = remaining / static_cast<double>(open);
        bool froze = false;
        if (limited) {
            for (std::uint32_t index = range.start; index < range.end; ++index) {
                if (frozen[index] != 0U) continue;
                const double limit = distribution_limit(tracks[index], functions[index], step.target,
                                                        growable[index] != 0U);
                const double room =
                    std::max(0.0, limit - affected_size(tracks[index], step.target) - incurred[index]);
                if (room < share) {
                    incurred[index] += room;
                    remaining -= room;
                    frozen[index] = 1U;
                    froze = true;
                }
            }
        }
        if (froze) continue;
        for (std::uint32_t index = range.start; index < range.end; ++index) {
            if (frozen[index] == 0U) incurred[index] += share;
        }
        remaining = 0.0;
    }
    return remaining;
}

// Step 4's share: in proportion to the flexible tracks' fractions (the
// rest equally when they sum to less than one).
void share_by_fraction(const std::vector<TrackFunction>& functions, TrackRange range, double space,
                       DistributionScratch& scratch) {
    std::vector<double>& incurred = *scratch.incurred;
    const std::vector<std::uint8_t>& frozen = *scratch.frozen;
    double fractions = 0.0;
    std::uint32_t count = 0U;
    for (std::uint32_t index = range.start; index < range.end; ++index) {
        if (frozen[index] != 0U) continue;
        fractions += functions[index].maximum_value;
        ++count;
    }
    if (count == 0U) return;
    const double proportional = fractions >= 1.0 ? space : space * fractions;
    const double equal = (space - proportional) / static_cast<double>(count);
    for (std::uint32_t index = range.start; index < range.end; ++index) {
        if (frozen[index] != 0U) continue;
        const double ratio = fractions > 0.0 ? functions[index].maximum_value / fractions : 0.0;
        incurred[index] += proportional * ratio + equal;
    }
}

struct ItemContributionView final {
    std::span<const GridItemInput> items{};
    std::span<const GridItemContribution> contributions{};
    const std::vector<GridItemResult>* results{};
    GridAxis axis{GridAxis::columns};
    bool right_to_left{};
    GridSizingKind sizing{GridSizingKind::definite};
};

double step_contribution(const SpanningStep& step, const ItemContributionView& view, std::size_t item,
                         const std::vector<GridTrack>& tracks, const std::vector<TrackFunction>& functions,
                         double gap) noexcept {
    const GridItemContribution& contribution = view.contributions[item];
    const TrackRange range = item_range((*view.results)[item].area, view.axis);
    if (step.contribution == ContributionKind::min_content) return contribution.min_content;
    if (step.contribution == ContributionKind::max_content) return contribution.max_content;
    const SpanFacts facts = span_facts(functions, range);
    const double margins = fixed_margins(axis_margins(view.items[item], view.axis, view.right_to_left));
    const double minimum = applied_minimum(contribution, facts, range, tracks, gap, margins);
    if (view.sizing == GridSizingKind::min_content) {
        return limited_contribution(contribution.min_content, minimum, facts, range, tracks, gap);
    }
    if (view.sizing == GridSizingKind::max_content) {
        return limited_contribution(contribution.max_content, minimum, facts, range, tracks, gap);
    }
    return minimum;
}

// One spanning step over the items order[first, last) (12.5 steps 3 and 4,
// with 12.5.1's distribution).
void run_spanning_step(const SpanningStep& step, const ItemContributionView& view,
                       std::span<const std::uint32_t> group, std::vector<GridTrack>& tracks,
                       const std::vector<TrackFunction>& functions, double gap, DistributionScratch& scratch) {
    std::vector<double>& planned = *scratch.planned;
    std::vector<double>& incurred = *scratch.incurred;
    std::vector<std::uint8_t>& frozen = *scratch.frozen;
    std::fill(planned.begin(), planned.end(), 0.0);
    for (const std::uint32_t item : group) {
        const TrackRange range = item_range((*view.results)[item].area, view.axis);
        std::uint32_t affected_count = 0U;
        double spanned = 0.0;
        for (std::uint32_t index = range.start; index < range.end; ++index) {
            spanned += affected_size(tracks[index], step.target);
            incurred[index] = 0.0;
            const bool takes_space = affected(step.tracks, functions[index], step.flexible_only);
            frozen[index] = takes_space ? 0U : 1U;
            if (takes_space) ++affected_count;
        }
        if (affected_count == 0U) continue;
        const double contribution = step_contribution(step, view, item, tracks, functions, gap);
        const double space = contribution - spanned - inner_gaps(tracks, range, gap);
        if (space <= distribution_tolerance) continue;
        if (step.flexible_only) {
            share_by_fraction(functions, range, space, scratch);
        } else {
            const double remaining = share_equally(step, tracks, functions, range, space, true, scratch);
            if (remaining > distribution_tolerance) {
                // Beyond limits: the tracks whose maximum invites it, else
                // every affected track.
                std::uint32_t candidates = 0U;
                for (std::uint32_t index = range.start; index < range.end; ++index) {
                    if (!affected(step.tracks, functions[index], false)) continue;
                    if (beyond_limit_candidate(step, tracks[index], functions[index], incurred[index])) {
                        ++candidates;
                    }
                }
                for (std::uint32_t index = range.start; index < range.end; ++index) {
                    const bool is_affected = affected(step.tracks, functions[index], false);
                    const bool candidate =
                        is_affected &&
                        (candidates == 0U
                             ? step.target == SizeTarget::base ||
                                   functions[index].maximum != Breadth::fit_content
                             : beyond_limit_candidate(step, tracks[index], functions[index], incurred[index]));
                    frozen[index] = candidate ? 0U : 1U;
                }
                static_cast<void>(share_equally(step, tracks, functions, range, remaining, false, scratch));
            }
        }
        for (std::uint32_t index = range.start; index < range.end; ++index) {
            if (!affected(step.tracks, functions[index], step.flexible_only)) continue;
            planned[index] = std::max(planned[index], incurred[index]);
        }
    }
    for (std::size_t index = 0U; index < tracks.size(); ++index) {
        if (planned[index] <= 0.0) continue;
        GridTrack& track = tracks[index];
        if (step.target == SizeTarget::base) {
            track.base += planned[index];
        } else if (std::isinf(track.growth_limit)) {
            track.growth_limit = track.base + planned[index];
            if (step.marks_growable) (*scratch.growable)[index] = 1U;
        } else {
            track.growth_limit += planned[index];
        }
    }
}

void raise_growth_limits_to_bases(std::vector<GridTrack>& tracks) noexcept {
    for (GridTrack& track : tracks) {
        if (track.growth_limit < track.base) track.growth_limit = track.base;
    }
}

// Orders item positions by span, then position: a strict total order.
struct SpanBefore final {
    const std::vector<GridItemResult>* results{};
    GridAxis axis{GridAxis::columns};

    bool operator()(std::uint32_t left, std::uint32_t right) const noexcept {
        const TrackRange left_range = item_range((*results)[left].area, axis);
        const TrackRange right_range = item_range((*results)[right].area, axis);
        const std::uint32_t left_span = left_range.end - left_range.start;
        const std::uint32_t right_span = right_range.end - right_range.start;
        if (left_span != right_span) return left_span < right_span;
        return left < right;
    }
};

// 12.7.1: the size of one fraction over tracks [range) filling `space`.
double fraction_size(const std::vector<GridTrack>& tracks, const std::vector<TrackFunction>& functions,
                     TrackRange range, double space, std::vector<std::uint8_t>& inflexible) {
    for (std::uint32_t index = range.start; index < range.end; ++index) {
        inflexible[index] = flexible(functions[index]) ? 0U : 1U;
    }
    double hypothetical = 0.0;
    bool restart = true;
    while (restart) {
        restart = false;
        double leftover = space;
        double fractions = 0.0;
        for (std::uint32_t index = range.start; index < range.end; ++index) {
            if (inflexible[index] != 0U) {
                leftover -= tracks[index].base;
            } else {
                fractions += functions[index].maximum_value;
            }
        }
        hypothetical = leftover / std::max(1.0, fractions);
        for (std::uint32_t index = range.start; index < range.end; ++index) {
            if (inflexible[index] != 0U) continue;
            if (hypothetical * functions[index].maximum_value < tracks[index].base) {
                inflexible[index] = 1U;
                restart = true;
            }
        }
    }
    return std::max(0.0, hypothetical);
}

// ---------------------------------------------------------------------------
// Alignment

enum class Packing : std::uint8_t { start, end, center, space_between, space_around, space_evenly };

Packing content_packing(LayoutAlignment alignment) noexcept {
    switch (alignment) {
    case LayoutAlignment::end:
    case LayoutAlignment::flex_end:
        return Packing::end;
    case LayoutAlignment::center:
        return Packing::center;
    case LayoutAlignment::space_between:
        return Packing::space_between;
    case LayoutAlignment::space_around:
        return Packing::space_around;
    case LayoutAlignment::space_evenly:
        return Packing::space_evenly;
    default: // automatic, normal, stretch, start, flex_start and baseline pack at the start.
        return Packing::start;
    }
}

struct Distribution final {
    double offset{};
    double spacing{};
};

// CSS Box Alignment's distribution with its safe fallbacks: with negative
// space every distribution starts at the start.
Distribution distribute(Packing packing, double free, std::size_t count) noexcept {
    Distribution distribution{};
    switch (packing) {
    case Packing::start:
        return distribution;
    case Packing::end:
        distribution.offset = free;
        return distribution;
    case Packing::center:
        distribution.offset = free * 0.5;
        return distribution;
    case Packing::space_between:
        if (free > 0.0 && count > 1U) distribution.spacing = free / static_cast<double>(count - 1U);
        return distribution;
    case Packing::space_around:
        if (free > 0.0 && count > 0U) {
            distribution.spacing = free / static_cast<double>(count);
            distribution.offset = distribution.spacing * 0.5;
        }
        return distribution;
    case Packing::space_evenly:
        if (free > 0.0 && count > 0U) {
            distribution.spacing = free / static_cast<double>(count + 1U);
            distribution.offset = distribution.spacing;
        }
        return distribution;
    }
    return distribution;
}

void position_tracks(std::vector<GridTrack>& tracks, const GridAvailableExtent& extent, double gap,
                     LayoutAlignment alignment, double content) noexcept {
    std::size_t visible = 0U;
    for (const GridTrack& track : tracks) {
        if (!track.collapsed) ++visible;
    }
    const double free = extent.kind == GridSizingKind::definite ? extent.extent - content : 0.0;
    const Distribution distribution = distribute(content_packing(alignment), free, visible);
    double position = distribution.offset;
    bool seen = false;
    for (GridTrack& track : tracks) {
        if (track.collapsed) {
            track.position = position;
            continue;
        }
        if (seen) position += gap + distribution.spacing;
        track.position = position;
        position += track.size;
        seen = true;
    }
}

enum class SelfPacking : std::uint8_t { start, end, center, stretch, baseline };

SelfPacking self_packing(LayoutAlignment self, LayoutAlignment items, bool definite_size, bool automatic_margin,
                         bool row_axis) noexcept {
    LayoutAlignment used = self == LayoutAlignment::automatic ? items : self;
    if (used == LayoutAlignment::automatic) used = LayoutAlignment::normal;
    switch (used) {
    case LayoutAlignment::normal:
    case LayoutAlignment::stretch:
        return definite_size || automatic_margin ? SelfPacking::start : SelfPacking::stretch;
    case LayoutAlignment::end:
    case LayoutAlignment::flex_end:
        return SelfPacking::end;
    case LayoutAlignment::center:
        return SelfPacking::center;
    case LayoutAlignment::baseline:
        return row_axis ? SelfPacking::baseline : SelfPacking::start;
    default: // start and flex_start, and the content-distribution values, which self alignment does not take.
        return SelfPacking::start;
    }
}

// How far a baseline-aligned item's margin box reaches above its baseline
// in its row: its top margin and its own baseline, or its outer height less
// its bottom margin (the border box's bottom) when it has none.
double baseline_ascent(const GridItemInput& item, const GridItemContribution& contribution) noexcept {
    if (std::isnan(contribution.baseline)) {
        const GridMargin& bottom = item.margin.bottom;
        const double synthesized = contribution.max_content - (bottom.automatic ? 0.0 : bottom.length);
        return synthesized;
    }
    const GridMargin& top = item.margin.top;
    const double ascent = (top.automatic ? 0.0 : top.length) + contribution.baseline;
    return ascent;
}

bool baseline_aligned(const GridContainerInput& container, const GridItemInput& item) noexcept {
    const LayoutAlignment used = item.align_self == LayoutAlignment::automatic ? container.align_items : item.align_self;
    return used == LayoutAlignment::baseline;
}

double resolve_size(const SizeValue& size, double basis, double fallback) noexcept {
    if (size.kind != SizeKind::length_percentage) return fallback;
    return size.value.length.value() + size.value.percent * basis / 100.0;
}

struct AxisSizes final {
    const SizeValue* size{};
    const SizeValue* minimum{};
    const SizeValue* maximum{};
};

AxisSizes axis_sizes(const GridItemInput& item, GridAxis axis) noexcept {
    AxisSizes sizes{};
    if (axis == GridAxis::columns) {
        sizes.size = &item.width;
        sizes.minimum = &item.min_width;
        sizes.maximum = &item.max_width;
    } else {
        sizes.size = &item.height;
        sizes.minimum = &item.min_height;
        sizes.maximum = &item.max_height;
    }
    return sizes;
}

struct PlacedSpan final {
    double offset{};
    double size{};
};

struct SelfPlacementInput final {
    double area{};
    LayoutAlignment self{LayoutAlignment::automatic};
    LayoutAlignment items{LayoutAlignment::normal};
    MarginPair margins{};
    AxisSizes sizes{};
    const GridItemContribution* contribution{};
    // The automatic minimum, inner, where 6.6 applies; zero elsewhere.
    double automatic_minimum{};
    double baseline_shim{};
    bool row_axis{};
};

// An item's size and offset within its area on one axis (CSS Box Alignment
// self alignment, with auto margins absorbing positive free space first).
PlacedSpan place_in_area(const SelfPlacementInput& input) noexcept {
    const MarginPair& margins = input.margins;
    const double margin_sum = fixed_margins(margins);
    const double available = input.area - margin_sum;
    const bool definite = (*input.sizes.size).kind == SizeKind::length_percentage;
    const bool automatic_margin = margins.start.automatic || margins.end.automatic;
    const SelfPacking packing = self_packing(input.self, input.items, definite, automatic_margin, input.row_axis);
    double used = 0.0;
    if (definite) {
        used = resolve_size(*input.sizes.size, input.area, 0.0);
    } else if (packing == SelfPacking::stretch) {
        used = available;
    } else if (input.row_axis) {
        used = (*input.contribution).max_content - margin_sum;
    } else {
        const double smallest = (*input.contribution).min_content - margin_sum;
        const double largest = (*input.contribution).max_content - margin_sum;
        used = std::min(largest, std::max(smallest, available));
    }
    const double maximum = (*input.sizes.maximum).kind == SizeKind::length_percentage
                               ? resolve_size(*input.sizes.maximum, input.area, infinity)
                               : infinity;
    const double minimum = (*input.sizes.minimum).kind == SizeKind::length_percentage
                               ? resolve_size(*input.sizes.minimum, input.area, 0.0)
                               : input.automatic_minimum;
    used = std::max(minimum, std::min(used, maximum));
    used = std::max(0.0, used);
    const double start_margin = margins.start.automatic ? 0.0 : margins.start.length;
    const double free = available - used;
    PlacedSpan placed{};
    placed.size = used;
    if (automatic_margin) {
        const double positive = std::max(0.0, free);
        if (margins.start.automatic && margins.end.automatic) {
            placed.offset = start_margin + positive * 0.5;
        } else if (margins.start.automatic) {
            placed.offset = start_margin + positive;
        } else {
            placed.offset = start_margin;
        }
        return placed;
    }
    switch (packing) {
    case SelfPacking::end:
        placed.offset = start_margin + free;
        break;
    case SelfPacking::center:
        placed.offset = start_margin + free * 0.5;
        break;
    case SelfPacking::baseline:
        placed.offset = start_margin + input.baseline_shim;
        break;
    case SelfPacking::start:
    case SelfPacking::stretch:
        placed.offset = start_margin;
        break;
    default: // Not a SelfPacking: the item sits at the start.
        placed.offset = start_margin;
        break;
    }
    return placed;
}

double inner_automatic_minimum(const GridItemContribution& contribution, const std::vector<TrackFunction>& functions,
                               const std::vector<GridTrack>& tracks, TrackRange range, double gap,
                               double margins) noexcept {
    if (!contribution.automatic_minimum) return 0.0;
    const SpanFacts facts = span_facts(functions, range);
    const double outer = applied_minimum(contribution, facts, range, tracks, gap, margins);
    return std::max(0.0, outer - margins);
}

// ---------------------------------------------------------------------------
// Placement (8.5)

// One line reference resolved against an explicit grid of `explicit_count`
// tracks: lines 0 to explicit_count are explicit.
std::int64_t line_index(const GridLine& line, std::uint32_t explicit_count, const GridTemplateAreas& areas,
                        GridAxis axis, bool start_side) noexcept {
    if (line.kind == GridLineKind::line) {
        if (line.number > 0) return static_cast<std::int64_t>(line.number) - 1;
        return static_cast<std::int64_t>(explicit_count) + 1 + static_cast<std::int64_t>(line.number);
    }
    for (const GridNamedArea& area : areas.areas) {
        if (area.name != line.area) continue;
        if (axis == GridAxis::columns) return start_side ? area.column_start : area.column_end;
        return start_side ? area.row_start : area.row_end;
    }
    // No such area: every implicit line bears the name, so the first one
    // after the explicit grid is meant (CSS Grid 8.3.1).
    return static_cast<std::int64_t>(explicit_count) + 1;
}

bool names_line(const GridLine& line) noexcept {
    return line.kind == GridLineKind::line || line.kind == GridLineKind::area;
}

std::uint32_t span_of(const GridLine& line) noexcept {
    return line.kind == GridLineKind::span && line.number > 0 ? static_cast<std::uint32_t>(line.number) : 1U;
}

AxisPlacement resolve_axis(const GridLine& start, const GridLine& end, std::uint32_t explicit_count,
                           const GridTemplateAreas& areas, GridAxis axis) noexcept {
    AxisPlacement placement{};
    const bool start_named = names_line(start);
    const bool end_named = names_line(end);
    if (start_named && end_named) {
        std::int64_t first = line_index(start, explicit_count, areas, axis, true);
        std::int64_t last = line_index(end, explicit_count, areas, axis, false);
        if (last < first) std::swap(first, last);
        if (last == first) last = first + 1;
        placement.start = first;
        placement.span = static_cast<std::uint32_t>(std::min<std::int64_t>(last - first, 0x7fffffff));
        placement.definite = true;
        return placement;
    }
    if (start_named) {
        placement.start = line_index(start, explicit_count, areas, axis, true);
        placement.span = span_of(end);
        placement.definite = true;
        return placement;
    }
    if (end_named) {
        placement.span = span_of(start);
        placement.start = line_index(end, explicit_count, areas, axis, false) - placement.span;
        placement.definite = true;
        return placement;
    }
    placement.span = start.kind == GridLineKind::span ? span_of(start) : span_of(end);
    return placement;
}

// The number of repetitions of the list's automatic repeat (7.2.3.2).
std::size_t repetitions(const GridTrackList& list, const GridAvailableExtent& extent, double gap) noexcept {
    if (list.auto_repeat == AutoRepeatKind::none || list.auto_repeat_tracks.empty()) return 0U;
    if (extent.kind != GridSizingKind::definite) return 1U;
    double fixed_sum = 0.0;
    for (const GridTrackSize& size : list.tracks) {
        const TrackFunction function = track_function(size, extent);
        if (function.maximum == Breadth::fixed) {
            fixed_sum += function.maximum_value;
        } else if (function.minimum == Breadth::fixed) {
            fixed_sum += function.minimum_value;
        }
    }
    double repeat_sum = 0.0;
    for (const GridTrackSize& size : list.auto_repeat_tracks) {
        const TrackFunction function = track_function(size, extent);
        if (function.maximum == Breadth::fixed) {
            repeat_sum += function.maximum_value;
        } else if (function.minimum == Breadth::fixed) {
            repeat_sum += function.minimum_value;
        }
    }
    const double per_repetition = repeat_sum + gap * static_cast<double>(list.auto_repeat_tracks.size());
    if (per_repetition <= 0.0) return 1U;
    // total(n) = fixed_sum + n * per_repetition + gap * (fixed tracks - 1).
    const double fixed_gaps = gap * (static_cast<double>(list.tracks.size()) - 1.0);
    const double room = extent.extent - fixed_sum - fixed_gaps;
    const double count = std::floor(room / per_repetition + 1.0e-9);
    if (!(count >= 1.0)) return 1U;
    return static_cast<std::size_t>(std::min(count, static_cast<double>(maximum_grid_repetitions)));
}

struct ExpandedList final {
    // The auto-fit repetition's tracks, as indices of the expanded list.
    std::uint32_t fit_first{};
    std::uint32_t fit_end{};
};

ExpandedList expand_track_list(const GridTrackList& list, const GridAvailableExtent& extent, double gap,
                               std::vector<GridTrackSize>& expanded) {
    const std::size_t count = repetitions(list, extent, gap);
    expanded.clear();
    expanded.reserve(list.tracks.size() + count * list.auto_repeat_tracks.size());
    const std::size_t insert_at = std::min<std::size_t>(list.auto_repeat_index, list.tracks.size());
    for (std::size_t index = 0U; index < insert_at; ++index) expanded.push_back(list.tracks[index]);
    ExpandedList result{};
    result.fit_first = static_cast<std::uint32_t>(expanded.size());
    for (std::size_t repeat = 0U; repeat < count; ++repeat) {
        for (const GridTrackSize& size : list.auto_repeat_tracks) expanded.push_back(size);
    }
    result.fit_end = list.auto_repeat == AutoRepeatKind::auto_fit ? static_cast<std::uint32_t>(expanded.size())
                                                                  : result.fit_first;
    for (std::size_t index = insert_at; index < list.tracks.size(); ++index) expanded.push_back(list.tracks[index]);
    return result;
}

// The occupancy of auto-placement: `lanes` rows of `across` cells, a lane
// being a row in row flow and a column in column flow. Lanes are appended
// as placement needs them; widening rebuilds the storage (step 2 only).
class Occupancy final {
public:
    Occupancy(std::vector<std::uint8_t>& cells, std::size_t across) noexcept : cells_(cells), across_(across) {}

    [[nodiscard]] std::size_t across() const noexcept { return across_; }
    [[nodiscard]] std::size_t lanes() const noexcept { return across_ == 0U ? 0U : cells_.size() / across_; }

    // Makes room for `wanted` lanes; false when that exceeds the cell limit.
    bool reserve_lanes(std::size_t wanted) {
        if (wanted <= lanes()) return true;
        if (across_ != 0U && wanted > maximum_grid_cells / across_) return false;
        cells_.resize(wanted * across_, 0U);
        return true;
    }

    bool widen(std::size_t across) {
        if (across <= across_) return true;
        const std::size_t stored = lanes();
        if (stored != 0U && across > maximum_grid_cells / stored) return false;
        std::vector<std::uint8_t> wider(stored * across, 0U);
        for (std::size_t lane = 0U; lane < stored; ++lane) {
            for (std::size_t cell = 0U; cell < across_; ++cell) {
                wider[lane * across + cell] = cells_[lane * across_ + cell];
            }
        }
        cells_.swap(wider);
        across_ = across;
        return true;
    }

    // Whether the area is free; lanes and cells beyond the storage are.
    [[nodiscard]] bool free(std::size_t lane, std::size_t lane_span, std::size_t cell, std::size_t cell_span) const noexcept {
        const std::size_t stored = lanes();
        for (std::size_t row = lane; row < lane + lane_span && row < stored; ++row) {
            for (std::size_t column = cell; column < cell + cell_span && column < across_; ++column) {
                if (cells_[row * across_ + column] != 0U) return false;
            }
        }
        return true;
    }

    void mark(std::size_t lane, std::size_t lane_span, std::size_t cell, std::size_t cell_span) noexcept {
        for (std::size_t row = lane; row < lane + lane_span; ++row) {
            for (std::size_t column = cell; column < cell + cell_span; ++column) {
                cells_[row * across_ + column] = 1U;
            }
        }
    }

private:
    std::vector<std::uint8_t>& cells_;
    std::size_t across_{};
};

void build_tracks(std::vector<GridTrack>& tracks, std::size_t count, std::uint32_t leading,
                  const std::vector<GridTrackSize>& explicit_sizes, std::uint32_t explicit_count,
                  const std::vector<GridTrackSize>& automatic_sizes) {
    tracks.resize(count);
    const std::int64_t template_count = static_cast<std::int64_t>(explicit_sizes.size());
    const std::int64_t pattern = static_cast<std::int64_t>(automatic_sizes.size());
    for (std::size_t index = 0U; index < count; ++index) {
        GridTrack& track = tracks[index];
        track = GridTrack{};
        const std::int64_t position = static_cast<std::int64_t>(index) - static_cast<std::int64_t>(leading);
        if (position >= 0 && position < template_count) {
            track.sizing = explicit_sizes[static_cast<std::size_t>(position)];
            continue;
        }
        track.implicit = position < 0 || position >= static_cast<std::int64_t>(explicit_count);
        if (pattern == 0) continue;
        std::int64_t entry = 0;
        if (position < 0) {
            const std::int64_t before = -position;
            entry = (pattern - (before % pattern)) % pattern;
        } else {
            entry = (position - template_count) % pattern;
        }
        track.sizing = automatic_sizes[static_cast<std::size_t>(entry)];
    }
}

// A refused layout leaves no items and no tracks behind.
void clear_layout(std::vector<GridItemResult>& items, std::vector<GridTrack>& columns,
                  std::vector<GridTrack>& rows) noexcept {
    items.clear();
    columns.clear();
    rows.clear();
}

} // namespace

void GridLayoutWorkspace::reserve(std::size_t count) {
    solve_lender_.invalidate();
    items_.reserve(count);
    column_placements_.reserve(count);
    row_placements_.reserve(count);
    span_order_.reserve(count);
    adjusted_.reserve(count);
    baseline_shims_.reserve(count);
    columns_.reserve(count);
    rows_.reserve(count);
    column_functions_.reserve(count);
    row_functions_.reserve(count);
    planned_.reserve(count);
    incurred_.reserve(count);
    frozen_.reserve(count);
    growable_.reserve(count);
    group_ascents_.reserve(count);
    lane_cursors_.reserve(count);
}

double GridLayoutWorkspace::area_extent(std::size_t index, GridAxis axis) const noexcept {
    const bool sized = axis == GridAxis::columns ? columns_sized_ : rows_sized_;
    if (!sized || index >= items_.size()) return 0.0;
    const std::vector<GridTrack>& tracks = axis == GridAxis::columns ? columns_ : rows_;
    const TrackRange range = item_range(items_[index].area, axis);
    double extent = 0.0;
    for (std::uint32_t track = range.start; track < range.end; ++track) extent += tracks[track].size;
    extent += inner_gaps(tracks, range, axis == GridAxis::columns ? column_gap_ : row_gap_);
    return extent;
}

GridSolveOutcome place_grid_items(const GridContainerStyle& style, const GridContainerInput& container,
                                  std::span<const GridItemInput> items, GridLayoutWorkspace& workspace) {
    // A new solve: the views of the last one end (they may relocate below).
    workspace.solve_lender_.invalidate();
    workspace.placed_ = false;
    workspace.columns_sized_ = false;
    workspace.rows_sized_ = false;
    workspace.content_width_ = 0.0;
    workspace.content_height_ = 0.0;
    if (items.size() > maximum_grid_items) {
        clear_layout(workspace.items_, workspace.columns_, workspace.rows_);
        return GridSolveOutcome::refused_too_many_items;
    }
    for (const GridItemInput& item : items) {
        if (item.placement == nullptr) return GridSolveOutcome::refused_mismatched_input;
    }
    workspace.column_gap_ = container.column_gap;
    workspace.row_gap_ = container.row_gap;

    // The explicit grid: the template lists with any automatic repeat
    // counted, widened to the template areas.
    const ExpandedList fit_columns =
        expand_track_list(style.template_columns, container.width, container.column_gap, workspace.explicit_columns_);
    const ExpandedList fit_rows =
        expand_track_list(style.template_rows, container.height, container.row_gap, workspace.explicit_rows_);
    const std::uint32_t explicit_columns =
        std::max(static_cast<std::uint32_t>(workspace.explicit_columns_.size()), style.template_areas.columns);
    const std::uint32_t explicit_rows =
        std::max(static_cast<std::uint32_t>(workspace.explicit_rows_.size()), style.template_areas.rows);
    workspace.explicit_column_count_ = explicit_columns;
    workspace.explicit_row_count_ = explicit_rows;

    // Line-based placement on both axes.
    const std::size_t count = items.size();
    workspace.column_placements_.resize(count);
    workspace.row_placements_.resize(count);
    std::int64_t lowest_column = 0;
    std::int64_t lowest_row = 0;
    for (std::size_t index = 0U; index < count; ++index) {
        const GridItemStyle& placement = *items[index].placement;
        AxisPlacement& column = workspace.column_placements_[index];
        AxisPlacement& row = workspace.row_placements_[index];
        column = resolve_axis(placement.column_start, placement.column_end, explicit_columns, style.template_areas,
                              GridAxis::columns);
        row = resolve_axis(placement.row_start, placement.row_end, explicit_rows, style.template_areas,
                           GridAxis::rows);
        if (column.definite) lowest_column = std::min(lowest_column, column.start);
        if (row.definite) lowest_row = std::min(lowest_row, row.start);
    }
    const std::int64_t cell_limit = static_cast<std::int64_t>(maximum_grid_cells);
    if (-lowest_column > cell_limit || -lowest_row > cell_limit) {
        clear_layout(workspace.items_, workspace.columns_, workspace.rows_);
        return GridSolveOutcome::refused_too_many_cells;
    }
    const std::uint32_t leading_columns = static_cast<std::uint32_t>(-lowest_column);
    const std::uint32_t leading_rows = static_cast<std::uint32_t>(-lowest_row);
    workspace.leading_columns_ = leading_columns;
    workspace.leading_rows_ = leading_rows;

    // Auto-placement runs along lanes: rows in row flow, columns in column
    // flow. `across` is the axis a lane's cells run along.
    const bool row_flow = style.auto_flow == GridAutoFlow::row || style.auto_flow == GridAutoFlow::row_dense;
    const bool dense = style.auto_flow == GridAutoFlow::row_dense || style.auto_flow == GridAutoFlow::column_dense;
    std::vector<AxisPlacement>& across_placements = row_flow ? workspace.column_placements_ : workspace.row_placements_;
    std::vector<AxisPlacement>& lane_placements = row_flow ? workspace.row_placements_ : workspace.column_placements_;
    const std::int64_t across_leading = row_flow ? leading_columns : leading_rows;
    const std::int64_t lane_leading = row_flow ? leading_rows : leading_columns;
    std::int64_t across_count = (row_flow ? explicit_columns : explicit_rows) + across_leading;
    std::int64_t lane_count = (row_flow ? explicit_rows : explicit_columns) + lane_leading;
    for (std::size_t index = 0U; index < count; ++index) {
        AxisPlacement& across = across_placements[index];
        AxisPlacement& lane = lane_placements[index];
        if (across.definite) {
            across.start += across_leading;
            across_count = std::max(across_count, across.start + static_cast<std::int64_t>(across.span));
        } else {
            across_count = std::max(across_count, static_cast<std::int64_t>(across.span));
        }
        if (lane.definite) {
            lane.start += lane_leading;
            lane_count = std::max(lane_count, lane.start + static_cast<std::int64_t>(lane.span));
        }
    }
    if (across_count > cell_limit || lane_count > cell_limit ||
        (across_count != 0 && lane_count > cell_limit / std::max<std::int64_t>(1, across_count))) {
        clear_layout(workspace.items_, workspace.columns_, workspace.rows_);
        return GridSolveOutcome::refused_too_many_cells;
    }
    workspace.occupancy_.clear();
    Occupancy occupancy(workspace.occupancy_, static_cast<std::size_t>(across_count));
    if (!occupancy.reserve_lanes(static_cast<std::size_t>(lane_count))) {
        clear_layout(workspace.items_, workspace.columns_, workspace.rows_);
        return GridSolveOutcome::refused_too_many_cells;
    }

    // Step 1: items fixed on both axes.
    for (std::size_t index = 0U; index < count; ++index) {
        const AxisPlacement& across = across_placements[index];
        const AxisPlacement& lane = lane_placements[index];
        if (!across.definite || !lane.definite) continue;
        occupancy.mark(static_cast<std::size_t>(lane.start), lane.span, static_cast<std::size_t>(across.start),
                       across.span);
    }
    // Step 2: items fixed to a lane, placed along it past the items this
    // step placed there before (from the lane's start when dense).
    workspace.lane_cursors_.assign(static_cast<std::size_t>(lane_count), 0U);
    for (std::size_t index = 0U; index < count; ++index) {
        AxisPlacement& across = across_placements[index];
        const AxisPlacement& lane = lane_placements[index];
        if (across.definite || !lane.definite) continue;
        const std::size_t lane_start = static_cast<std::size_t>(lane.start);
        std::size_t cell = dense ? 0U : workspace.lane_cursors_[lane_start];
        while (!occupancy.free(lane_start, lane.span, cell, across.span)) ++cell;
        if (cell + across.span > occupancy.across() && !occupancy.widen(cell + across.span)) {
            clear_layout(workspace.items_, workspace.columns_, workspace.rows_);
            return GridSolveOutcome::refused_too_many_cells;
        }
        occupancy.mark(lane_start, lane.span, cell, across.span);
        across.start = static_cast<std::int64_t>(cell);
        across.definite = true;
        workspace.lane_cursors_[lane_start] = static_cast<std::uint32_t>(cell + across.span);
    }
    // Step 4: the rest, from the auto-placement cursor.
    std::size_t cursor_lane = 0U;
    std::size_t cursor_cell = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        AxisPlacement& across = across_placements[index];
        AxisPlacement& lane = lane_placements[index];
        if (lane.definite) continue;
        const std::size_t width = occupancy.across();
        if (across.definite) {
            const std::size_t cell = static_cast<std::size_t>(across.start);
            if (dense) {
                cursor_lane = 0U;
            } else if (cell < cursor_cell) {
                ++cursor_lane;
            }
            cursor_cell = cell;
            while (!occupancy.free(cursor_lane, lane.span, cell, across.span)) ++cursor_lane;
        } else {
            if (dense) {
                cursor_lane = 0U;
                cursor_cell = 0U;
            }
            while (true) {
                if (cursor_cell + across.span > width) {
                    ++cursor_lane;
                    cursor_cell = 0U;
                    continue;
                }
                if (occupancy.free(cursor_lane, lane.span, cursor_cell, across.span)) break;
                ++cursor_cell;
            }
            across.start = static_cast<std::int64_t>(cursor_cell);
            across.definite = true;
        }
        if (!occupancy.reserve_lanes(cursor_lane + lane.span)) {
            clear_layout(workspace.items_, workspace.columns_, workspace.rows_);
            return GridSolveOutcome::refused_too_many_cells;
        }
        occupancy.mark(cursor_lane, lane.span, static_cast<std::size_t>(across.start), across.span);
        lane.start = static_cast<std::int64_t>(cursor_lane);
        lane.definite = true;
    }
    across_count = static_cast<std::int64_t>(occupancy.across());
    lane_count = std::max(lane_count, static_cast<std::int64_t>(occupancy.lanes()));
    for (std::size_t index = 0U; index < count; ++index) {
        const AxisPlacement& lane = lane_placements[index];
        lane_count = std::max(lane_count, lane.start + static_cast<std::int64_t>(lane.span));
    }

    // Areas and tracks.
    const std::size_t column_count = static_cast<std::size_t>(row_flow ? across_count : lane_count);
    const std::size_t row_count = static_cast<std::size_t>(row_flow ? lane_count : across_count);
    workspace.items_.resize(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const AxisPlacement& column = workspace.column_placements_[index];
        const AxisPlacement& row = workspace.row_placements_[index];
        const GridItemStyle& placement = *items[index].placement;
        GridItemResult& result = workspace.items_[index];
        result = GridItemResult{};
        result.area.column_start = static_cast<std::uint32_t>(column.start);
        result.area.column_end = static_cast<std::uint32_t>(column.start + column.span);
        result.area.row_start = static_cast<std::uint32_t>(row.start);
        result.area.row_end = static_cast<std::uint32_t>(row.start + row.span);
        result.column_definite = names_line(placement.column_start) || names_line(placement.column_end);
        result.row_definite = names_line(placement.row_start) || names_line(placement.row_end);
    }
    build_tracks(workspace.columns_, column_count, leading_columns, workspace.explicit_columns_, explicit_columns,
                 style.auto_columns);
    build_tracks(workspace.rows_, row_count, leading_rows, workspace.explicit_rows_, explicit_rows, style.auto_rows);

    // auto-fit: a repetition no item occupies collapses.
    workspace.fit_columns_first_ = fit_columns.fit_first + leading_columns;
    workspace.fit_columns_end_ = fit_columns.fit_end + leading_columns;
    workspace.fit_rows_first_ = fit_rows.fit_first + leading_rows;
    workspace.fit_rows_end_ = fit_rows.fit_end + leading_rows;
    for (std::uint32_t track = workspace.fit_columns_first_; track < workspace.fit_columns_end_; ++track) {
        workspace.columns_[track].collapsed = true;
    }
    for (std::uint32_t track = workspace.fit_rows_first_; track < workspace.fit_rows_end_; ++track) {
        workspace.rows_[track].collapsed = true;
    }
    for (const GridItemResult& result : workspace.items_) {
        for (std::uint32_t track = result.area.column_start; track < result.area.column_end; ++track) {
            workspace.columns_[track].collapsed = false;
        }
        for (std::uint32_t track = result.area.row_start; track < result.area.row_end; ++track) {
            workspace.rows_[track].collapsed = false;
        }
    }
    workspace.placed_ = true;
    return GridSolveOutcome::solved;
}

GridSolveOutcome size_grid_tracks(GridAxis axis, const GridContainerInput& container,
                                  std::span<const GridItemInput> items,
                                  std::span<const GridItemContribution> contributions,
                                  GridLayoutWorkspace& workspace) {
    if (!workspace.placed_ || items.size() != workspace.items_.size() || contributions.size() != items.size()) {
        return GridSolveOutcome::refused_mismatched_input;
    }
    if (axis == GridAxis::rows && !workspace.columns_sized_) return GridSolveOutcome::refused_mismatched_input;
    const AxisView view = axis_view(axis, container, workspace.columns_, workspace.rows_, workspace.column_functions_,
                                    workspace.row_functions_);
    std::vector<GridTrack>& tracks = *view.tracks;
    std::vector<TrackFunction>& functions = *view.functions;
    const double gap = view.gap;
    const std::size_t track_count = tracks.size();
    const std::size_t count = items.size();
    const bool mirrored = right_to_left(container);

    // Row contributions carry each baseline-aligned item's shim: the space
    // above it that lines its baseline (its own, or synthesized from its
    // border box's bottom) up with its row's other baseline-aligned items
    // (12.5 step 1).
    std::span<const GridItemContribution> used = contributions;
    if (axis == GridAxis::rows) {
        workspace.adjusted_.assign(contributions.begin(), contributions.end());
        workspace.baseline_shims_.assign(count, 0.0);
        workspace.group_ascents_.assign(track_count, 0.0);
        for (std::size_t index = 0U; index < count; ++index) {
            if (!baseline_aligned(container, items[index])) continue;
            const double ascent = baseline_ascent(items[index], contributions[index]);
            double& group = workspace.group_ascents_[workspace.items_[index].area.row_start];
            group = std::max(group, ascent);
        }
        for (std::size_t index = 0U; index < count; ++index) {
            if (!baseline_aligned(container, items[index])) continue;
            const double ascent = baseline_ascent(items[index], contributions[index]);
            const double shim = workspace.group_ascents_[workspace.items_[index].area.row_start] - ascent;
            workspace.baseline_shims_[index] = shim;
            GridItemContribution& adjusted = workspace.adjusted_[index];
            adjusted.minimum += shim;
            adjusted.min_content += shim;
            adjusted.max_content += shim;
        }
        used = workspace.adjusted_;
    }

    // 12.4: the sizing functions, base sizes and growth limits.
    functions.resize(track_count);
    for (std::size_t index = 0U; index < track_count; ++index) {
        GridTrack& track = tracks[index];
        TrackFunction& function = functions[index];
        if (track.collapsed) {
            function = TrackFunction{Breadth::fixed, 0.0, Breadth::fixed, 0.0};
        } else {
            function = track_function(track.sizing, view.extent);
        }
        track.base = function.minimum == Breadth::fixed ? function.minimum_value : 0.0;
        track.growth_limit = function.maximum == Breadth::fixed ? function.maximum_value : infinity;
        if (track.growth_limit < track.base) track.growth_limit = track.base;
    }
    workspace.planned_.assign(track_count, 0.0);
    workspace.incurred_.assign(track_count, 0.0);
    workspace.frozen_.assign(track_count, 0U);
    workspace.growable_.assign(track_count, 0U);
    DistributionScratch scratch{&workspace.planned_, &workspace.incurred_, &workspace.frozen_, &workspace.growable_};
    ItemContributionView contribution_view{items, used, &workspace.items_, axis, mirrored, view.extent.kind};

    // 12.5 step 2: items spanning one track that is not flexible. The
    // growth accumulators start below any contribution.
    std::vector<double>& growth = workspace.incurred_;
    std::fill(growth.begin(), growth.end(), -1.0);
    for (std::size_t index = 0U; index < count; ++index) {
        const TrackRange range = item_range(workspace.items_[index].area, axis);
        if (range.end - range.start != 1U) continue;
        const std::uint32_t track_index = range.start;
        const TrackFunction& function = functions[track_index];
        if (flexible(function)) continue;
        GridTrack& track = tracks[track_index];
        const GridItemContribution& contribution = used[index];
        // A fixed minimum is already the base; only the intrinsic minima grow it.
        if (function.minimum == Breadth::min_content) {
            track.base = std::max(track.base, contribution.min_content);
        } else if (function.minimum == Breadth::max_content) {
            track.base = std::max(track.base, contribution.max_content);
        } else if (function.minimum == Breadth::automatic) {
            const SpanningStep minimum_step{};
            track.base = std::max(track.base,
                                  step_contribution(minimum_step, contribution_view, index, tracks, functions, gap));
        }
        double& accumulated = growth[track_index];
        if (function.maximum == Breadth::min_content) {
            accumulated = std::max(accumulated, contribution.min_content);
        } else if (function.maximum == Breadth::max_content || function.maximum == Breadth::automatic ||
                   function.maximum == Breadth::fit_content) {
            accumulated = std::max(accumulated, contribution.max_content);
        }
    }
    for (std::size_t index = 0U; index < track_count; ++index) {
        const TrackFunction& function = functions[index];
        GridTrack& track = tracks[index];
        if (growth[index] >= 0.0 && intrinsic_breadth(function.maximum)) {
            double limit = growth[index];
            if (function.maximum == Breadth::fit_content) limit = std::min(limit, function.maximum_value);
            track.growth_limit = std::max(limit, track.base);
        }
        if (track.growth_limit < track.base) track.growth_limit = track.base;
    }
    std::fill(growth.begin(), growth.end(), 0.0);

    // 12.5 step 3: spanning items that cross no flexible track, by span;
    // step 4: those that do, together.
    workspace.span_order_.clear();
    for (std::size_t index = 0U; index < count; ++index) {
        const TrackRange range = item_range(workspace.items_[index].area, axis);
        if (range.end - range.start < 2U) continue;
        if (span_facts(functions, range).spans_flexible) continue;
        workspace.span_order_.push_back(static_cast<std::uint32_t>(index));
    }
    const SpanBefore span_before{&workspace.items_, axis};
    std::sort(workspace.span_order_.begin(), workspace.span_order_.end(), span_before);
    const std::size_t spanning = workspace.span_order_.size();
    std::size_t group_start = 0U;
    while (group_start < spanning) {
        const TrackRange first = item_range(workspace.items_[workspace.span_order_[group_start]].area, axis);
        const std::uint32_t span = first.end - first.start;
        std::size_t group_end = group_start + 1U;
        while (group_end < spanning) {
            const TrackRange next = item_range(workspace.items_[workspace.span_order_[group_end]].area, axis);
            if (next.end - next.start != span) break;
            ++group_end;
        }
        const std::span<const std::uint32_t> group(workspace.span_order_.data() + group_start, group_end - group_start);
        const SpanningStep intrinsic_minimums{SizeTarget::base, ContributionKind::minimum,
                                              AffectedTracks::intrinsic_minimum, false, false};
        const SpanningStep content_minimums{SizeTarget::base, ContributionKind::min_content,
                                            AffectedTracks::content_minimum, false, false};
        const SpanningStep max_content_minimums{SizeTarget::base, ContributionKind::max_content,
                                                AffectedTracks::max_content_minimum, false, false};
        const SpanningStep intrinsic_maximums{SizeTarget::growth, ContributionKind::min_content,
                                              AffectedTracks::intrinsic_maximum, false, true};
        const SpanningStep max_content_maximums{SizeTarget::growth, ContributionKind::max_content,
                                                AffectedTracks::max_content_maximum, false, false};
        run_spanning_step(intrinsic_minimums, contribution_view, group, tracks, functions, gap, scratch);
        run_spanning_step(content_minimums, contribution_view, group, tracks, functions, gap, scratch);
        run_spanning_step(max_content_minimums, contribution_view, group, tracks, functions, gap, scratch);
        raise_growth_limits_to_bases(tracks);
        run_spanning_step(intrinsic_maximums, contribution_view, group, tracks, functions, gap, scratch);
        run_spanning_step(max_content_maximums, contribution_view, group, tracks, functions, gap, scratch);
        group_start = group_end;
    }
    workspace.span_order_.clear();
    for (std::size_t index = 0U; index < count; ++index) {
        const TrackRange range = item_range(workspace.items_[index].area, axis);
        if (span_facts(functions, range).spans_flexible) {
            workspace.span_order_.push_back(static_cast<std::uint32_t>(index));
        }
    }
    if (!workspace.span_order_.empty()) {
        const std::span<const std::uint32_t> group(workspace.span_order_.data(), workspace.span_order_.size());
        const SpanningStep flexible_minimums{SizeTarget::base, ContributionKind::minimum,
                                             AffectedTracks::intrinsic_minimum, true, false};
        const SpanningStep flexible_content{SizeTarget::base, ContributionKind::min_content,
                                            AffectedTracks::content_minimum, true, false};
        const SpanningStep flexible_max_content{SizeTarget::base, ContributionKind::max_content,
                                                AffectedTracks::max_content_minimum, true, false};
        run_spanning_step(flexible_minimums, contribution_view, group, tracks, functions, gap, scratch);
        run_spanning_step(flexible_content, contribution_view, group, tracks, functions, gap, scratch);
        run_spanning_step(flexible_max_content, contribution_view, group, tracks, functions, gap, scratch);
        raise_growth_limits_to_bases(tracks);
    }
    // 12.5 step 5: growth limits still unbounded become the base size.
    for (GridTrack& track : tracks) {
        if (std::isinf(track.growth_limit)) track.growth_limit = track.base;
    }

    const double gaps = all_gaps(tracks, gap);
    // 12.6: maximize the tracks.
    if (view.extent.kind == GridSizingKind::max_content) {
        for (GridTrack& track : tracks) track.base = track.growth_limit;
    } else if (view.extent.kind == GridSizingKind::definite) {
        const double free = view.extent.extent - base_sum(tracks) - gaps;
        if (free > distribution_tolerance) {
            std::fill(workspace.frozen_.begin(), workspace.frozen_.end(), 0U);
            std::fill(workspace.incurred_.begin(), workspace.incurred_.end(), 0.0);
            std::fill(workspace.growable_.begin(), workspace.growable_.end(), 0U);
            const SpanningStep maximize{SizeTarget::base, ContributionKind::minimum, AffectedTracks::intrinsic_minimum,
                                        false, false};
            const TrackRange everything{0U, static_cast<std::uint32_t>(track_count)};
            static_cast<void>(share_equally(maximize, tracks, functions, everything, free, true, scratch));
            for (std::size_t index = 0U; index < track_count; ++index) tracks[index].base += workspace.incurred_[index];
        }
    }

    // 12.7: expand the flexible tracks.
    bool any_flexible = false;
    for (const TrackFunction& function : functions) {
        if (flexible(function)) any_flexible = true;
    }
    if (any_flexible && view.extent.kind != GridSizingKind::min_content) {
        double fraction = 0.0;
        if (view.extent.kind == GridSizingKind::definite) {
            const double free = view.extent.extent - base_sum(tracks) - gaps;
            if (free > 0.0) {
                const TrackRange everything{0U, static_cast<std::uint32_t>(track_count)};
                fraction = fraction_size(tracks, functions, everything, view.extent.extent - gaps, workspace.frozen_);
            }
        } else {
            for (std::size_t index = 0U; index < track_count; ++index) {
                if (!flexible(functions[index])) continue;
                const double factor = functions[index].maximum_value;
                const double base = tracks[index].base;
                fraction = std::max(fraction, factor > 1.0 ? base / factor : base);
            }
            for (std::size_t index = 0U; index < count; ++index) {
                const TrackRange range = item_range(workspace.items_[index].area, axis);
                if (!span_facts(functions, range).spans_flexible) continue;
                const double space = used[index].max_content - inner_gaps(tracks, range, gap);
                fraction = std::max(fraction, fraction_size(tracks, functions, range, space, workspace.frozen_));
            }
        }
        for (std::size_t index = 0U; index < track_count; ++index) {
            if (!flexible(functions[index])) continue;
            tracks[index].base = std::max(tracks[index].base, fraction * functions[index].maximum_value);
        }
    }

    // 12.8: stretch the `auto` tracks.
    const LayoutAlignment content = axis == GridAxis::columns ? container.justify_content : container.align_content;
    const bool stretches = content == LayoutAlignment::normal || content == LayoutAlignment::stretch;
    if (stretches && view.extent.kind == GridSizingKind::definite) {
        const double free = view.extent.extent - base_sum(tracks) - gaps;
        std::size_t automatic = 0U;
        for (std::size_t index = 0U; index < track_count; ++index) {
            if (functions[index].maximum == Breadth::automatic && !tracks[index].collapsed) ++automatic;
        }
        if (free > distribution_tolerance && automatic != 0U) {
            const double share = free / static_cast<double>(automatic);
            for (std::size_t index = 0U; index < track_count; ++index) {
                if (functions[index].maximum == Breadth::automatic && !tracks[index].collapsed) {
                    tracks[index].base += share;
                }
            }
        }
    }

    for (GridTrack& track : tracks) {
        track.size = track.base;
        if (track.growth_limit < track.base) track.growth_limit = track.base;
    }
    const double extent = base_sum(tracks) + gaps;
    if (axis == GridAxis::columns) {
        workspace.content_width_ = extent;
        workspace.columns_sized_ = true;
        workspace.rows_sized_ = false;
    } else {
        workspace.content_height_ = extent;
        workspace.rows_sized_ = true;
    }
    return GridSolveOutcome::solved;
}

double grid_item_column_size(const GridContainerInput& container, const GridItemInput& item,
                             const GridItemContribution& column_contribution, const GridLayoutWorkspace& workspace,
                             std::size_t index) {
    if (!workspace.columns_sized_ || index >= workspace.items_.size()) return 0.0;
    const TrackRange range = item_range(workspace.items_[index].area, GridAxis::columns);
    const MarginPair margins = axis_margins(item, GridAxis::columns, right_to_left(container));
    SelfPlacementInput input{};
    input.area = workspace.area_extent(index, GridAxis::columns);
    input.self = item.justify_self;
    input.items = container.justify_items;
    input.margins = margins;
    input.sizes = axis_sizes(item, GridAxis::columns);
    input.contribution = &column_contribution;
    input.automatic_minimum = inner_automatic_minimum(column_contribution, workspace.column_functions_,
                                                      workspace.columns_, range, workspace.column_gap_,
                                                      fixed_margins(margins));
    input.row_axis = false;
    const PlacedSpan placed = place_in_area(input);
    return placed.size;
}

GridSolveOutcome arrange_grid_items(const GridContainerInput& container, std::span<const GridItemInput> items,
                                    std::span<const GridItemContribution> column_contributions,
                                    std::span<const GridItemContribution> row_contributions,
                                    GridLayoutWorkspace& workspace) {
    const std::size_t count = items.size();
    if (!workspace.columns_sized_ || !workspace.rows_sized_ || count != workspace.items_.size() ||
        column_contributions.size() != count || row_contributions.size() != count) {
        return GridSolveOutcome::refused_mismatched_input;
    }
    position_tracks(workspace.columns_, container.width, container.column_gap, container.justify_content,
                    workspace.content_width_);
    position_tracks(workspace.rows_, container.height, container.row_gap, container.align_content,
                    workspace.content_height_);
    const bool mirrored = right_to_left(container);
    const double mirror_width =
        container.width.kind == GridSizingKind::definite ? container.width.extent : workspace.content_width_;
    for (std::size_t index = 0U; index < count; ++index) {
        const GridItemInput& item = items[index];
        GridItemResult& result = workspace.items_[index];
        const TrackRange columns = item_range(result.area, GridAxis::columns);
        const TrackRange rows = item_range(result.area, GridAxis::rows);
        const GridTrack& first_column = workspace.columns_[columns.start];
        const GridTrack& last_column = workspace.columns_[columns.end - 1U];
        const GridTrack& first_row = workspace.rows_[rows.start];
        const GridTrack& last_row = workspace.rows_[rows.end - 1U];
        const double area_x = first_column.position;
        const double area_width = last_column.position + last_column.size - area_x;
        const double area_y = first_row.position;
        const double area_height = last_row.position + last_row.size - area_y;

        const MarginPair column_margins = axis_margins(item, GridAxis::columns, mirrored);
        SelfPlacementInput column_input{};
        column_input.area = area_width;
        column_input.self = item.justify_self;
        column_input.items = container.justify_items;
        column_input.margins = column_margins;
        column_input.sizes = axis_sizes(item, GridAxis::columns);
        column_input.contribution = &column_contributions[index];
        column_input.automatic_minimum = inner_automatic_minimum(
            column_contributions[index], workspace.column_functions_, workspace.columns_, columns,
            container.column_gap, fixed_margins(column_margins));
        const PlacedSpan horizontal = place_in_area(column_input);

        const MarginPair row_margins = axis_margins(item, GridAxis::rows, mirrored);
        SelfPlacementInput row_input{};
        row_input.area = area_height;
        row_input.self = item.align_self;
        row_input.items = container.align_items;
        row_input.margins = row_margins;
        row_input.sizes = axis_sizes(item, GridAxis::rows);
        row_input.contribution = &row_contributions[index];
        row_input.automatic_minimum =
            inner_automatic_minimum(row_contributions[index], workspace.row_functions_, workspace.rows_, rows,
                                    container.row_gap, fixed_margins(row_margins));
        row_input.baseline_shim = index < workspace.baseline_shims_.size() ? workspace.baseline_shims_[index] : 0.0;
        row_input.row_axis = true;
        const PlacedSpan vertical = place_in_area(row_input);

        double x = area_x + horizontal.offset;
        if (mirrored) x = mirror_width - (x + horizontal.size);
        result.bounds = Rect{x, area_y + vertical.offset, horizontal.size, vertical.size};
    }
    return GridSolveOutcome::solved;
}

} // namespace gui_forms
