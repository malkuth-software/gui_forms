#include "display_chunk.hpp"

#include <utility>

namespace gui_forms::detail {

namespace {

// One pass over the commands (DisplayOccluder states the rules). Depth
// counts open saves and paint groups; a group opened at the top level
// remembers where it began, its rectangle clips and whether anything in it
// (a translation, a rounded clip, a paint group's blending) disqualifies a
// surface drawn inside it. An unbalanced recording has no occluder.
std::optional<DisplayOccluder> find_occluder(const std::vector<DisplayCommand>& commands) {
    std::optional<DisplayOccluder> found;
    std::size_t depth = 0U;
    std::size_t group_start = 0U;
    bool group_disqualified = false;
    bool group_clipped = false;
    Rect group_clip{};
    bool searching = true;
    for (std::size_t index = 0U; index < commands.size(); ++index) {
        const DisplayCommand& command = commands[index];
        const DisplayOperation operation = command.operation;
        if (operation == DisplayOperation::save || operation == DisplayOperation::begin_group) {
            if (depth == 0U) {
                group_start = index;
                group_disqualified = operation == DisplayOperation::begin_group;
                group_clipped = false;
                group_clip = Rect{};
            } else if (depth == 1U && operation == DisplayOperation::begin_group) {
                group_disqualified = true;
            }
            ++depth;
            continue;
        }
        if (operation == DisplayOperation::restore || operation == DisplayOperation::end_group) {
            if (depth == 0U) return std::nullopt;
            --depth;
            continue;
        }
        const bool rounded_clip = operation == DisplayOperation::clip_rounded_rect ||
                                  operation == DisplayOperation::clip_rounded_rect_corners;
        if (operation == DisplayOperation::translate || rounded_clip ||
            (operation == DisplayOperation::clip_rect && depth == 0U)) {
            // At the top level it changes the state of everything after it,
            // which a replay of the commands beneath a surface inside a
            // band's save would lose; a surface found before it still hides
            // what came before that.
            if (depth == 0U) searching = false;
            if (depth == 1U) group_disqualified = true;
            continue;
        }
        if (operation == DisplayOperation::clip_rect) {
            if (depth == 1U) {
                group_clip = group_clipped ? Rect::intersection(group_clip, command.rect) : command.rect;
                group_clipped = true;
            }
            continue;
        }
        if (!searching || operation != DisplayOperation::draw_live_surface || !command.live_surface) continue;
        if (depth >= 2U || (depth == 1U && group_disqualified)) continue;
        const bool in_group = depth == 1U;
        DisplayOccluder occluder{};
        occluder.command = index;
        occluder.group_start = in_group ? group_start : index;
        occluder.clipped = in_group && group_clipped;
        if (occluder.clipped) occluder.clip = group_clip;
        found = occluder;
    }
    if (depth != 0U) return std::nullopt;
    return found;
}

} // namespace

DisplayChunk::DisplayChunk(std::uint64_t generation,
                           PaintPlane plane,
                           Rect logical_bounds,
                           std::vector<DisplayCommand> commands,
                           std::optional<Rect> opaque_cover)
    : generation_(generation), plane_(plane), logical_bounds_(logical_bounds),
      commands_(std::move(commands)), opaque_cover_(opaque_cover), occluder_(find_occluder(commands_)) {}

DisplayChunkInfo DisplayChunk::info() const noexcept {
    return {generation_, plane_, logical_bounds_, commands_.size()};
}

} // namespace gui_forms::detail
