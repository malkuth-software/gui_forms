#include "display_chunk.hpp"

#include <utility>

namespace gui_forms::detail {

namespace {

// One open save while searching (find_occluder): where it began, the
// translation recorded in it, the clip it adds (in the chunk's
// coordinates) and whether it holds a rounded clip or is a paint group.
struct OpenSave final {
    std::size_t start{};
    Point offset{};
    bool clipped{};
    Rect clip{};
    bool disqualified{};
};

// One pass over the commands (DisplayOccluder states the rules), keeping
// the open saves on a stack that is reserved once. An unbalanced recording
// has no occluder.
std::optional<DisplayOccluder> find_occluder(const DisplayCommandList& commands) {
    std::optional<DisplayOccluder> found;
    std::vector<OpenSave> open;
    open.reserve(8U);
    bool searching = true;
    for (std::size_t index = 0U; index < commands.size(); ++index) {
        const DisplayCommand& command = commands[index];
        const DisplayOperation operation = command.operation;
        if (operation == DisplayOperation::save || operation == DisplayOperation::begin_group) {
            OpenSave level{};
            level.start = index;
            level.disqualified = operation == DisplayOperation::begin_group;
            open.push_back(level);
            continue;
        }
        if (operation == DisplayOperation::restore || operation == DisplayOperation::end_group) {
            if (open.empty()) return std::nullopt;
            open.pop_back();
            continue;
        }
        // The translation in force here, from the chunk's coordinates.
        Point offset{};
        for (const OpenSave& level : open) {
            offset.x += level.offset.x;
            offset.y += level.offset.y;
        }
        const bool rounded_clip = operation == DisplayOperation::clip_rounded_rect ||
                                  operation == DisplayOperation::clip_rounded_rect_corners;
        if (open.empty() && (operation == DisplayOperation::translate || operation == DisplayOperation::clip_rect ||
                             rounded_clip)) {
            // A surface found before it still hides what came before that.
            searching = false;
            continue;
        }
        if (operation == DisplayOperation::translate) {
            open.back().offset.x += command.first.x;
            open.back().offset.y += command.first.y;
            continue;
        }
        if (rounded_clip) {
            open.back().disqualified = true;
            continue;
        }
        if (operation == DisplayOperation::clip_rect) {
            const Rect moved{command.rect.x + offset.x, command.rect.y + offset.y, command.rect.width,
                             command.rect.height};
            OpenSave& level = open.back();
            level.clip = level.clipped ? Rect::intersection(level.clip, moved) : moved;
            level.clipped = true;
            continue;
        }
        if (!searching || operation != DisplayOperation::draw_live_surface || !command.live_surface) continue;
        bool disqualified = false;
        DisplayOccluder occluder{};
        occluder.command = index;
        occluder.group_start = open.empty() ? index : open.front().start;
        occluder.open_saves = open.size();
        occluder.offset = offset;
        for (const OpenSave& level : open) {
            disqualified = disqualified || level.disqualified;
            if (!level.clipped) continue;
            occluder.clip = occluder.clipped ? Rect::intersection(occluder.clip, level.clip) : level.clip;
            occluder.clipped = true;
        }
        if (!disqualified) found = occluder;
    }
    if (!open.empty()) return std::nullopt;
    return found;
}

} // namespace

DisplayChunk::DisplayChunk(std::uint64_t generation,
                           PaintPlane plane,
                           Rect logical_bounds,
                           DisplayCommandList commands,
                           std::optional<Rect> opaque_cover)
    : generation_(generation), plane_(plane), logical_bounds_(logical_bounds),
      commands_(std::move(commands)), opaque_cover_(opaque_cover), occluder_(find_occluder(commands_)) {}

DisplayChunkInfo DisplayChunk::info() const noexcept {
    return {generation_, plane_, logical_bounds_, commands_.size()};
}

} // namespace gui_forms::detail
