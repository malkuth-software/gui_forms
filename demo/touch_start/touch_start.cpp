#include "touch_start.hpp"

#include "gui_forms/animation/animation_timeline/animation_timeline.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/events/key_events/key_events.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::touch_start {
namespace {

using namespace std::chrono_literals;

// The palette: an original set of deep, matt colours on a dark ground.
constexpr Color ground_top = Color::rgba(0x15, 0x21, 0x2a);
constexpr Color ground_bottom = Color::rgba(0x1f, 0x33, 0x3d);
constexpr Color ink = Color::rgba(0xf4, 0xf6, 0xf7);
constexpr Color soft_ink = Color::rgba(0xf4, 0xf6, 0xf7, 0xb8);
constexpr Color steel = Color::rgba(0x24, 0x6b, 0xa8);
constexpr Color crimson = Color::rgba(0x9e, 0x23, 0x3c);
constexpr Color indigo = Color::rgba(0x3b, 0x3f, 0x8f);
constexpr Color slate = Color::rgba(0x3d, 0x55, 0x66);
constexpr Color amber = Color::rgba(0xb8, 0x74, 0x14);
constexpr Color plum = Color::rgba(0x76, 0x2e, 0x68);
constexpr Color rust = Color::rgba(0xae, 0x47, 0x28);
constexpr Color olive = Color::rgba(0x62, 0x72, 0x26);
constexpr Color teal = Color::rgba(0x1d, 0x76, 0x70);
constexpr Color green = Color::rgba(0x2b, 0x78, 0x4b);
constexpr Color graphite = Color::rgba(0x4a, 0x50, 0x57);

constexpr std::array<TileSpec, 6> today_tiles{{
    {.id = "weather", .name = "Weather", .mark = "14°", .size = TileSize::wide, .colour = steel,
     .faces = {{{.heading = "Harbour, 14°", .line = "Light wind from the north-west"},
                {.heading = "This afternoon", .line = "Cloud breaking by four"},
                {.heading = "Tomorrow", .line = "Showers, 11° to 15°"}}}},
    {.id = "calendar", .name = "Calendar", .mark = "22", .size = TileSize::medium, .colour = crimson,
     .faces = {{{.heading = "Thursday 22", .line = "10:00 Design review"},
                {.heading = "Later", .line = "14:30 Walk to the library"},
                {.heading = "Friday", .line = "Nothing planned"}}}},
    {.id = "mail", .name = "Mail", .mark = "@", .size = TileSize::medium, .colour = indigo,
     .faces = {{{.heading = "3 unread", .line = "The proofs are ready"},
                {.heading = "Workshop", .line = "Bench two is free"},
                {}}}},
    {.id = "clock", .name = "Clock", .mark = "9:41", .size = TileSize::small, .colour = slate, .faces = {}},
    {.id = "notes", .name = "Notes", .mark = "N", .size = TileSize::small, .colour = amber, .faces = {}},
    {.id = "music", .name = "Music", .mark = "M", .size = TileSize::wide, .colour = plum,
     .faces = {{{.heading = "Now playing", .line = "Harbour Lights, side two"},
                {.heading = "Up next", .line = "Slow Tide"},
                {.heading = "Library", .line = "312 albums"}}}},
}};

constexpr std::array<TileSpec, 7> workshop_tiles{{
    {.id = "sketchbook", .name = "Sketchbook", .mark = "S", .size = TileSize::medium, .colour = rust, .faces = {}},
    {.id = "terminal", .name = "Terminal", .mark = ">_", .size = TileSize::small, .colour = graphite, .faces = {}},
    {.id = "files", .name = "Files", .mark = "F", .size = TileSize::small, .colour = olive, .faces = {}},
    {.id = "plans", .name = "Plans", .mark = "P", .size = TileSize::wide, .colour = teal,
     .faces = {{{.heading = "Plans", .line = "Bench layout, revision 4"},
                {.heading = "Shared with you", .line = "Shelving drawings"},
                {}}}},
    {.id = "inventory", .name = "Inventory", .mark = "#", .size = TileSize::medium, .colour = green,
     .faces = {{{.heading = "Low stock", .line = "M4 screws, 40 left"},
                {.heading = "Arrived", .line = "Two sheets of birch ply"},
                {}}}},
    {.id = "measures", .name = "Measures", .mark = "mm", .size = TileSize::small, .colour = amber, .faces = {}},
    {.id = "timer", .name = "Timer", .mark = "25:00", .size = TileSize::small, .colour = crimson, .faces = {}},
}};

constexpr std::array<TileSpec, 5> field_tiles{{
    {.id = "map", .name = "Map", .mark = "N", .size = TileSize::large, .colour = green,
     .faces = {{{.heading = "Map", .line = "You are at the quay"},
                {.heading = "Nearby", .line = "Ferry terminal, 400 m"},
                {.heading = "Saved", .line = "Six places"}}}},
    {.id = "tides", .name = "Tides", .mark = "~", .size = TileSize::medium, .colour = steel,
     .faces = {{{.heading = "High water", .line = "16:12, 4.1 m"},
                {.heading = "Low water", .line = "22:30, 0.9 m"},
                {}}}},
    {.id = "camera", .name = "Camera", .mark = "C", .size = TileSize::medium, .colour = slate, .faces = {}},
    {.id = "stars", .name = "Stars", .mark = "*", .size = TileSize::small, .colour = indigo, .faces = {}},
    {.id = "compass", .name = "Compass", .mark = "NW", .size = TileSize::small, .colour = rust, .faces = {}},
}};

constexpr std::array<TileSpec, 5> settings_tiles{{
    {.id = "settings", .name = "Settings", .mark = "S", .size = TileSize::medium, .colour = graphite, .faces = {}},
    {.id = "accounts", .name = "Accounts", .mark = "A", .size = TileSize::small, .colour = teal, .faces = {}},
    {.id = "display", .name = "Display", .mark = "D", .size = TileSize::small, .colour = indigo, .faces = {}},
    {.id = "sound", .name = "Sound", .mark = "Sd", .size = TileSize::small, .colour = plum, .faces = {}},
    {.id = "network", .name = "Network", .mark = "Nw", .size = TileSize::small, .colour = olive, .faces = {}},
}};

constexpr std::array<TileGroup, 4> groups{{
    {.id = "today", .name = "Today", .tiles = today_tiles},
    {.id = "workshop", .name = "Workshop", .tiles = workshop_tiles},
    {.id = "field", .name = "Out and about", .tiles = field_tiles},
    {.id = "settings", .name = "Settings", .tiles = settings_tiles},
}};

// The motions' lengths.
constexpr std::chrono::milliseconds glide_duration = 240ms;
constexpr std::chrono::milliseconds opening_duration = 340ms;
constexpr std::chrono::milliseconds closing_duration = 260ms;
constexpr std::chrono::milliseconds slide_duration = 600ms;
// One live tile turns every this long, in turn.
constexpr std::chrono::milliseconds live_interval = 2500ms;

[[nodiscard]] double seconds_between(FrameTime from, FrameTime to) noexcept {
    const std::chrono::duration<double> elapsed = to - from;
    return elapsed.count();
}

[[nodiscard]] double progress_of(FrameTime start, FrameTime now, std::chrono::milliseconds length) noexcept {
    const std::chrono::duration<double> whole = length;
    const double progress = std::clamp(seconds_between(start, now) / whole.count(), 0.0, 1.0);
    return progress;
}

[[nodiscard]] double span_of(std::size_t units) noexcept {
    const double count = static_cast<double>(units);
    return count * tile_unit + (count - 1.0) * tile_gap;
}

[[nodiscard]] Point centre_of(Rect bounds) noexcept {
    const Point centre{bounds.x + bounds.width / 2.0, bounds.y + bounds.height / 2.0};
    return centre;
}

[[nodiscard]] bool rect_contains(Rect bounds, Point point) noexcept {
    return point.x >= bounds.x && point.x < bounds.x + bounds.width && point.y >= bounds.y &&
           point.y < bounds.y + bounds.height;
}

[[nodiscard]] Color with_alpha(Color colour, double opacity) noexcept {
    const double clamped = std::clamp(opacity, 0.0, 1.0);
    Color made = colour;
    made.alpha = static_cast<std::uint8_t>(std::lround(static_cast<double>(colour.alpha) * clamped));
    return made;
}

[[nodiscard]] Color lighter(Color colour, double amount) noexcept {
    Color made = colour;
    made.red = static_cast<std::uint8_t>(std::lround(colour.red + (255.0 - colour.red) * amount));
    made.green = static_cast<std::uint8_t>(std::lround(colour.green + (255.0 - colour.green) * amount));
    made.blue = static_cast<std::uint8_t>(std::lround(colour.blue + (255.0 - colour.blue) * amount));
    return made;
}

[[nodiscard]] double lerp(double from, double to, double amount) noexcept {
    return from + (to - from) * amount;
}

// One group's occupancy while its tiles are placed: columns of `rows`
// cells, grown four columns at a time.
class GroupGrid final {
public:
    explicit GroupGrid(std::size_t rows) : rows_(rows) {}

    // The first free place for a tile `columns` x `rows` units: block by
    // block, row by row, column by column.
    [[nodiscard]] std::pair<std::size_t, std::size_t> place(std::size_t columns, std::size_t rows) {
        const std::size_t height = std::min(rows, rows_);
        const std::size_t width = std::min(columns, group_column_units);
        for (std::size_t block = 0U;; ++block) {
            if (block * group_column_units >= columns_) grow();
            for (std::size_t row = 0U; row + height <= rows_; ++row) {
                for (std::size_t column = 0U; column + width <= group_column_units; ++column) {
                    const std::size_t left = block * group_column_units + column;
                    if (!vacant(left, row, width, height)) continue;
                    mark(left, row, width, height);
                    used_columns_ = std::max(used_columns_, left + width);
                    return {left, row};
                }
            }
        }
    }

    [[nodiscard]] std::size_t used_columns() const noexcept { return used_columns_; }

private:
    void grow() {
        columns_ += group_column_units;
        occupied_.resize(columns_ * rows_, false);
    }
    [[nodiscard]] bool vacant(std::size_t left, std::size_t top, std::size_t width, std::size_t height) const {
        for (std::size_t column = left; column < left + width; ++column) {
            for (std::size_t row = top; row < top + height; ++row) {
                if (occupied_[column * rows_ + row]) return false;
            }
        }
        return true;
    }
    void mark(std::size_t left, std::size_t top, std::size_t width, std::size_t height) {
        for (std::size_t column = left; column < left + width; ++column) {
            for (std::size_t row = top; row < top + height; ++row) occupied_[column * rows_ + row] = true;
        }
    }

    std::size_t rows_{};
    std::size_t columns_{};
    std::size_t used_columns_{};
    std::vector<bool> occupied_{};
};

[[nodiscard]] std::size_t tile_total() noexcept {
    std::size_t total = 0U;
    for (const TileGroup& group : groups) total += group.tiles.size();
    return total;
}

// The text faces: the field's control face at the sizes the tiles use.
[[nodiscard]] FontSpec face(double size, std::uint16_t weight) noexcept {
    const FontSpec made{.role = FontRole::control, .size = size, .weight = weight, .italic = false,
                        .letter_spacing = 0.0};
    return made;
}

} // namespace

std::size_t tile_columns(TileSize size) noexcept {
    switch (size) {
    case TileSize::small: return 1U;
    case TileSize::medium: return 2U;
    case TileSize::wide: return 4U;
    case TileSize::large: return 4U;
    default: return 2U;
    }
}

std::size_t tile_rows(TileSize size) noexcept {
    switch (size) {
    case TileSize::small: return 1U;
    case TileSize::medium: return 2U;
    case TileSize::wide: return 2U;
    case TileSize::large: return 4U;
    default: return 2U;
    }
}

std::size_t face_count(const TileSpec& tile) noexcept {
    if (tile.size == TileSize::small) return 0U;
    std::size_t count = 0U;
    while (count < tile.faces.size() && !tile.faces[count].heading.empty()) ++count;
    return count;
}

bool live_tile(const TileSpec& tile) noexcept { return face_count(tile) > 1U; }

std::span<const TileGroup> start_groups() noexcept { return groups; }

std::size_t rows_for_height(double field_height) noexcept {
    const double usable = field_height - surface_top - group_header - surface_bottom;
    const double fitted = std::floor((usable + tile_gap) / (tile_unit + tile_gap));
    const double bounded = std::clamp(fitted, static_cast<double>(minimum_rows), static_cast<double>(maximum_rows));
    return static_cast<std::size_t>(bounded);
}

TileLayout lay_out_tiles(std::span<const TileGroup> tile_groups, double field_height) {
    TileLayout layout{};
    layout.rows = rows_for_height(field_height);
    double left = surface_left;
    const double tiles_top = surface_top + group_header;
    for (std::size_t group = 0U; group < tile_groups.size(); ++group) {
        GroupGrid grid(layout.rows);
        const std::span<const TileSpec> tiles = tile_groups[group].tiles;
        for (std::size_t tile = 0U; tile < tiles.size(); ++tile) {
            const std::size_t columns = tile_columns(tiles[tile].size);
            const std::size_t rows = tile_rows(tiles[tile].size);
            const std::pair<std::size_t, std::size_t> place = grid.place(columns, rows);
            const Rect bounds{left + static_cast<double>(place.first) * (tile_unit + tile_gap),
                              tiles_top + static_cast<double>(place.second) * (tile_unit + tile_gap),
                              span_of(columns), span_of(std::min(rows, layout.rows))};
            layout.tiles.push_back(TilePlacement{.group = group, .tile = tile, .bounds = bounds});
        }
        const double width = grid.used_columns() == 0U ? 0.0 : span_of(grid.used_columns());
        layout.groups.push_back(GroupPlacement{
            .group = group, .header = Rect{left, surface_top, width, group_header}, .right = left + width});
        left += width + group_gap;
    }
    const double last_right = layout.groups.empty() ? surface_left : layout.groups.back().right;
    layout.extent = last_right + surface_left;
    return layout;
}

stxlib::Optional<std::size_t> neighbour_tile(const TileLayout& layout, std::size_t from,
                                             TileDirection direction) noexcept {
    stxlib::Optional<std::size_t> best{};
    if (from >= layout.tiles.size()) return best;
    const Point origin = centre_of(layout.tiles[from].bounds);
    double best_score = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < layout.tiles.size(); ++index) {
        if (index == from) continue;
        const Point centre = centre_of(layout.tiles[index].bounds);
        const double dx = centre.x - origin.x;
        const double dy = centre.y - origin.y;
        double along = 0.0;
        double across = 0.0;
        switch (direction) {
        case TileDirection::left: along = -dx; across = std::abs(dy); break;
        case TileDirection::right: along = dx; across = std::abs(dy); break;
        case TileDirection::up: along = -dy; across = std::abs(dx); break;
        case TileDirection::down: along = dy; across = std::abs(dx); break;
        default: continue;
        }
        if (along < 1.0) continue;
        const double score = along + 2.0 * across;
        if (score < best_score) {
            best_score = score;
            best = stxlib::Optional<std::size_t>::of(index);
        }
    }
    return best;
}

stxlib::Optional<std::size_t> tile_at(const TileLayout& layout, Point surface_point) noexcept {
    for (std::size_t index = 0U; index < layout.tiles.size(); ++index) {
        if (rect_contains(layout.tiles[index].bounds, surface_point)) return stxlib::Optional<std::size_t>::of(index);
    }
    return stxlib::Optional<std::size_t>::none();
}

double maximum_pan_offset(const TileLayout& layout, double viewport_width) noexcept {
    return std::max(0.0, layout.extent - viewport_width);
}

double offset_revealing(const TileLayout& layout, std::size_t tile, double offset, double viewport_width) noexcept {
    if (tile >= layout.tiles.size()) return offset;
    const Rect bounds = layout.tiles[tile].bounds;
    double revealed = offset;
    if (bounds.x - surface_left < revealed) revealed = bounds.x - surface_left;
    if (bounds.x + bounds.width + surface_left > revealed + viewport_width) {
        revealed = bounds.x + bounds.width + surface_left - viewport_width;
    }
    const double settled = std::clamp(revealed, 0.0, maximum_pan_offset(layout, viewport_width));
    return settled;
}

const TileSpec& tile_spec(std::span<const TileGroup> tile_groups, const TilePlacement& placement) noexcept {
    return tile_groups[placement.group].tiles[placement.tile];
}

std::string_view tile_id(std::span<const TileGroup> tile_groups, const TileLayout& layout, std::size_t index) noexcept {
    if (index >= layout.tiles.size()) return {};
    return tile_spec(tile_groups, layout.tiles[index]).id;
}

// -- the field ------------------------------------------------------------------

TileField::TileField(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = true;
    accessible_name = std::string("Start");
    faces_.assign(tile_total(), 0U);
}

bool TileField::select_tile(std::string_view id) {
    const TileLayout layout = current_layout();
    for (std::size_t index = 0U; index < layout.tiles.size(); ++index) {
        if (tile_id(groups, layout, index) != id) continue;
        // Staging shows the tile at once: no glide to catch mid-way.
        selected_ = stxlib::Optional<std::size_t>::of(index);
        jump_to(offset_revealing(layout, index, offset_, viewport_width()));
        return true;
    }
    return false;
}

std::string_view TileField::selected_tile_id() const noexcept {
    if (selected_.absent()) return {};
    const TileLayout layout = current_layout();
    return tile_id(groups, layout, *selected_.value());
}

bool TileField::launch_selected() {
    if (selected_.absent() || stage_ != LaunchStage::tiles) return false;
    launch_index(*selected_.value());
    return true;
}

bool TileField::return_to_tiles() {
    if (stage_ != LaunchStage::open && stage_ != LaunchStage::opening) return false;
    if (reduced_motion()) {
        stage_ = LaunchStage::tiles;
        launch_progress_ = 0.0;
    } else {
        // Closing from wherever the opening got to.
        const std::chrono::duration<double> remaining = (1.0 - launch_progress_) * closing_duration;
        stage_ = LaunchStage::closing;
        launch_start_ = FrameClock::now() - std::chrono::duration_cast<FrameInterval>(remaining);
        start_motion();
    }
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

LaunchStage TileField::launch_stage() const noexcept { return stage_; }

double TileField::pan_offset() const noexcept { return offset_; }

double TileField::pan_target() const noexcept { return gliding_ ? glide_to_ : offset_; }

std::size_t TileField::tile_face(std::size_t index) const noexcept {
    return index < faces_.size() ? faces_[index] : 0U;
}

void TileField::turn_next_live_tile(FrameTime now) {
    if (stage_ != LaunchStage::tiles) return;
    const TileLayout layout = current_layout();
    const std::size_t count = layout.tiles.size();
    for (std::size_t step = 0U; step < count; ++step) {
        const std::size_t index = (next_live_ + step) % count;
        const TileSpec& spec = tile_spec(groups, layout.tiles[index]);
        if (!live_tile(spec)) continue;
        faces_[index] = (faces_[index] + 1U) % face_count(spec);
        next_live_ = (index + 1U) % count;
        if (reduced_motion()) {
            sliding_.reset();
            slide_progress_ = 1.0;
        } else {
            sliding_ = stxlib::Optional<std::size_t>::of(index);
            slide_start_ = now;
            slide_progress_ = 0.0;
            start_motion();
        }
        invalidate(Dirty::paint | Dirty::semantics);
        return;
    }
}

TileLayout TileField::current_layout() const {
    const double height = committed_arranged_bounds().height;
    const TileLayout layout = lay_out_tiles(groups, height > 0.0 ? height : window_size.height);
    return layout;
}

bool TileField::reduced_motion() const noexcept {
    return window() != nullptr && (*window()).presentation_settings().reduced_motion;
}

double TileField::viewport_width() const noexcept {
    const double width = committed_arranged_bounds().width;
    return width > 0.0 ? width : window_size.width;
}

Rect TileField::field_bounds() const noexcept {
    const Rect bounds{0.0, 0.0, viewport_width(), committed_arranged_bounds().height};
    return bounds;
}

std::string TileField::node_id(const TileLayout& layout, std::size_t index) const {
    std::string identity(stable_id().value());
    identity.append(".tile.");
    identity.append(tile_id(groups, layout, index));
    return identity;
}

// -- motion ---------------------------------------------------------------------

void TileField::start_motion() {
    if (motion_frames_.started()) return;
    last_motion_ = FrameClock::now();
    const std::shared_ptr<TileField> self = std::static_pointer_cast<TileField>(shared_from_this());
    // The launch, the glide and the slides want every display frame
    // (with claude/frame-source, the display's own pace).
    motion_frames_.start(*this, display_frame_interval, MotionTick{self});
}

void TileField::start_live() {
    if (live_frames_.started()) return;
    const std::shared_ptr<TileField> self = std::static_pointer_cast<TileField>(shared_from_this());
    live_frames_.start(*this, live_interval, LiveTick{self});
}

AnimationTickResult TileField::MotionTick::operator()(FrameTime now) const {
    const std::shared_ptr<TileField> live = field.lock();
    const AnimationTickResult result = live ? (*live).on_motion_tick(now) : AnimationTickResult::unchanged;
    return result;
}

AnimationTickResult TileField::LiveTick::operator()(FrameTime now) const {
    const std::shared_ptr<TileField> live = field.lock();
    const AnimationTickResult result = live ? (*live).on_live_tick(now) : AnimationTickResult::unchanged;
    return result;
}

AnimationTickResult TileField::on_motion_tick(FrameTime now) {
    const double elapsed = std::max(0.0, seconds_between(last_motion_, now));
    last_motion_ = now;
    bool moved = false;
    const double before = offset_;
    if (gliding_) {
        const double progress = progress_of(glide_start_, now, glide_duration);
        offset_ = lerp(glide_from_, glide_to_, apply_easing(EasingCurve::ease_out, progress));
        if (progress >= 1.0) gliding_ = false;
        moved = true;
    }
    if (inertia_.active()) {
        // TODO(touch): the flick's momentum (phase momentum): the decay's
        // distance this frame; at an end the inertia stops and, with the
        // rubber band, the surface springs back (spring_back_target).
        const TileLayout layout = current_layout();
        const double maximum = maximum_pan_offset(layout, viewport_width());
        offset_ = settle_pan_offset(offset_ - inertia_.step(elapsed), maximum, viewport_width());
        if (!inertia_.active() || offset_ <= 0.0 || offset_ >= maximum) {
            inertia_.stop();
            glide_to(spring_back_target(offset_, maximum));
        }
        moved = true;
    }
    if (stage_ == LaunchStage::opening) {
        launch_progress_ = progress_of(launch_start_, now, opening_duration);
        if (launch_progress_ >= 1.0) stage_ = LaunchStage::open;
        moved = true;
    } else if (stage_ == LaunchStage::closing) {
        launch_progress_ = 1.0 - progress_of(launch_start_, now, closing_duration);
        if (launch_progress_ <= 0.0) stage_ = LaunchStage::tiles;
        moved = true;
    }
    if (sliding_.present()) {
        slide_progress_ = progress_of(slide_start_, now, slide_duration);
        if (slide_progress_ >= 1.0) sliding_.reset();
        moved = true;
    }
    const bool settled = !gliding_ && !inertia_.active() && sliding_.absent() &&
                         stage_ != LaunchStage::opening && stage_ != LaunchStage::closing;
    if (settled) motion_frames_.stop();
    if (!moved) return AnimationTickResult::unchanged;
    invalidate(offset_ != before ? Dirty::paint | Dirty::semantics : Dirty::paint);
    return AnimationTickResult::damaged;
}

AnimationTickResult TileField::on_live_tick(FrameTime now) {
    const std::size_t before = next_live_;
    turn_next_live_tile(now);
    const AnimationTickResult result =
        next_live_ != before ? AnimationTickResult::damaged : AnimationTickResult::unchanged;
    return result;
}

void TileField::glide_to(double offset) {
    const TileLayout layout = current_layout();
    const double target = settle_pan_offset(offset, maximum_pan_offset(layout, viewport_width()), viewport_width());
    if (reduced_motion()) {
        jump_to(target);
        return;
    }
    if (target == offset_) {
        gliding_ = false;
        return;
    }
    glide_from_ = offset_;
    glide_to_ = target;
    glide_start_ = FrameClock::now();
    gliding_ = true;
    start_motion();
}

void TileField::jump_to(double offset) {
    const TileLayout layout = current_layout();
    const double target = settle_pan_offset(offset, maximum_pan_offset(layout, viewport_width()), viewport_width());
    gliding_ = false;
    if (target == offset_) return;
    offset_ = target;
    invalidate(Dirty::paint | Dirty::semantics);
}

void TileField::select_index(std::size_t index, const TileLayout& layout) {
    if (index >= layout.tiles.size()) return;
    selected_ = stxlib::Optional<std::size_t>::of(index);
    glide_to(offset_revealing(layout, index, pan_target(), viewport_width()));
    invalidate(Dirty::paint | Dirty::semantics);
}

void TileField::launch_index(std::size_t index) {
    if (stage_ != LaunchStage::tiles) return;
    selected_ = stxlib::Optional<std::size_t>::of(index);
    launched_ = index;
    if (reduced_motion()) {
        stage_ = LaunchStage::open;
        launch_progress_ = 1.0;
    } else {
        stage_ = LaunchStage::opening;
        launch_start_ = FrameClock::now();
        launch_progress_ = 0.0;
        start_motion();
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

void TileField::move_selection(TileDirection direction) {
    const TileLayout layout = current_layout();
    if (layout.tiles.empty()) return;
    if (selected_.absent()) {
        // The first tile in view.
        for (std::size_t index = 0U; index < layout.tiles.size(); ++index) {
            if (layout.tiles[index].bounds.x >= offset_) {
                select_index(index, layout);
                return;
            }
        }
        select_index(0U, layout);
        return;
    }
    const stxlib::Optional<std::size_t> next = neighbour_tile(layout, *selected_.value(), direction);
    if (next.present()) select_index(*next.value(), layout);
}

void TileField::activate_at(Point local_point) {
    const TileLayout layout = current_layout();
    const stxlib::Optional<std::size_t> hit = tile_at(layout, Point{local_point.x + offset_, local_point.y});
    if (hit.absent()) return;
    if (selected_ == hit) {
        launch_index(*hit.value());
        return;
    }
    select_index(*hit.value(), layout);
}

// -- input ----------------------------------------------------------------------

namespace {

[[nodiscard]] TouchSample sample_of(const MouseEventArgs& arguments) noexcept {
    const TouchSample sample{.position = arguments.location, .timestamp_nanoseconds = arguments.timestamp_nanoseconds};
    return sample;
}

} // namespace

void TileField::on_mouse_down(MouseEventArgs& arguments) {
    Control::on_mouse_down(arguments);
    if (arguments.handled) return;
    if (stage_ != LaunchStage::tiles) {
        // The launched tile: a secondary click returns, as Escape does.
        if (arguments.button == PointerButton::secondary) arguments.handled = return_to_tiles();
        return;
    }
    if (arguments.button != PointerButton::primary) return;
    gliding_ = false;
    inertia_.stop();
    // The primary press captures the pointer to the field until its release.
    // TODO(touch): the pointer's kind from the event (TC-touch's PointerEvent
    // kind); touch takes the finger's slop and, on release, flicks and holds.
    classifier_.press(PointerKind::mouse, sample_of(arguments));
    arguments.handled = true;
}

void TileField::on_mouse_move(MouseEventArgs& arguments) {
    Control::on_mouse_move(arguments);
    if (!classifier_.pressed()) return;
    const PanStep step = classifier_.move(sample_of(arguments));
    // The surface pans along its one axis and follows the pointer.
    if (step.panning) jump_to(offset_ - step.delta.x);
    arguments.handled = true;
}

void TileField::on_mouse_up(MouseEventArgs& arguments) {
    Control::on_mouse_up(arguments);
    if (!classifier_.pressed()) return;
    const GestureClassification gesture = classifier_.release(sample_of(arguments));
    arguments.handled = true;
    const TileLayout layout = current_layout();
    const double maximum = maximum_pan_offset(layout, viewport_width());
    switch (gesture.kind) {
    case GestureKind::tap:
        activate_at(gesture.position);
        break;
    case GestureKind::pan:
        glide_to(spring_back_target(offset_, maximum));
        break;
    case GestureKind::flick:
        // The content moves with the finger: the offset runs against it.
        inertia_.start(-gesture.velocity.x);
        if (inertia_.active()) start_motion();
        else glide_to(spring_back_target(offset_, maximum));
        break;
    case GestureKind::press_and_hold:
        // TODO(touch): the held tile's tool tip (its name and live face),
        // shown until the finger lifts or moves past the slop; no selection.
        break;
    case GestureKind::none:
        break;
    default:
        break;
    }
}

void TileField::on_mouse_capture_changed() {
    Control::on_mouse_capture_changed();
    // Capture lost mid-press (the release reports its own end after the
    // release's events, when no press is left): the press is cancelled,
    // the pan stays where it got to.
    if (classifier_.pressed() && !has_pointer_capture()) classifier_.cancel();
}

void TileField::on_mouse_wheel(MouseEventArgs& arguments) {
    Control::on_mouse_wheel(arguments);
    if (arguments.handled || stage_ != LaunchStage::tiles) return;
    // The surface has one axis: a vertical wheel pans it too.
    const Point delta = arguments.wheel_delta;
    const double along = std::abs(delta.x) > std::abs(delta.y) ? delta.x : delta.y;
    if (along == 0.0) return;
    if (arguments.wheel_unit == WheelDeltaUnit::lines) {
        glide_to(pan_target() - along * wheel_line_pan);
    } else {
        // Pixel deltas (trackpads, with their own momentum) follow at once.
        jump_to(offset_ - along);
    }
    arguments.handled = true;
}

void TileField::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    if (arguments.handled) return;
    const std::uint32_t key = arguments.physical_key;
    if (stage_ != LaunchStage::tiles) {
        if (key == PhysicalKey::escape || key == PhysicalKey::backspace) arguments.handled = return_to_tiles();
        return;
    }
    const TileLayout layout = current_layout();
    if (key == PhysicalKey::left) {
        move_selection(TileDirection::left);
    } else if (key == PhysicalKey::right) {
        move_selection(TileDirection::right);
    } else if (key == PhysicalKey::up) {
        move_selection(TileDirection::up);
    } else if (key == PhysicalKey::down) {
        move_selection(TileDirection::down);
    } else if (key == PhysicalKey::home && !layout.tiles.empty()) {
        select_index(0U, layout);
    } else if (key == PhysicalKey::end && !layout.tiles.empty()) {
        select_index(layout.tiles.size() - 1U, layout);
    } else if (key == PhysicalKey::page_up) {
        glide_to(pan_target() - viewport_width() * 0.8);
    } else if (key == PhysicalKey::page_down) {
        glide_to(pan_target() + viewport_width() * 0.8);
    } else if (key == PhysicalKey::enter || key == PhysicalKey::space) {
        if (selected_.absent()) move_selection(TileDirection::right);
        else launch_index(*selected_.value());
    } else {
        return;
    }
    arguments.handled = true;
}

void TileField::on_focus_changed(bool focused) {
    Control::on_focus_changed(focused);
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

void TileField::on_attached_to_window() {
    Control::on_attached_to_window();
    start_live();
}

void TileField::on_detached_from_window() noexcept {
    motion_frames_.stop();
    live_frames_.stop();
    classifier_.cancel();
    Control::on_detached_from_window();
}

// -- paint ----------------------------------------------------------------------

Rect TileField::launch_rect(const TileLayout& layout) const {
    const Rect field = field_bounds();
    if (launched_ >= layout.tiles.size()) return field;
    const Rect tile = layout.tiles[launched_].bounds;
    const double eased = apply_easing(EasingCurve::ease_in_out, launch_progress_);
    const Rect between{lerp(tile.x - offset_, field.x, eased), lerp(tile.y, field.y, eased),
                       lerp(tile.width, field.width, eased), lerp(tile.height, field.height, eased)};
    return between;
}

void TileField::paint_tile(Painter& painter, const TileLayout& layout, std::size_t index, Rect bounds) const {
    const TileSpec& spec = tile_spec(groups, layout.tiles[index]);
    const std::array<GradientStop, 2> stops{{{.offset = 0.0, .color = lighter(spec.colour, 0.10)},
                                             {.offset = 1.0, .color = spec.colour}}};
    painter.fill_linear_gradient(bounds, Point{bounds.x, bounds.y}, Point{bounds.x + bounds.width, bounds.y + bounds.height},
                                 stops);
    painter.save();
    painter.clip_rect(bounds);
    const std::size_t faces = face_count(spec);
    if (faces > 0U) {
        // A face: its heading and line at the tile's top left. A turning
        // tile slides its old face up and out and the new one in under it.
        const bool turning = sliding_ == stxlib::Optional<std::size_t>::of(index);
        const double slide = turning ? apply_easing(EasingCurve::ease_out, slide_progress_) : 1.0;
        const std::size_t shown = faces_[index] % faces;
        const std::size_t previous = (shown + faces - 1U) % faces;
        const double rise = bounds.height * (1.0 - slide);
        const std::array<std::pair<std::size_t, double>, 2> drawn{{{previous, rise - bounds.height}, {shown, rise}}};
        for (const std::pair<std::size_t, double>& each : drawn) {
            if (!turning && each.first == previous) continue;
            const TileFace& turned = spec.faces[each.first];
            const double top = bounds.y + each.second;
            painter.draw_text_utf8(Point{bounds.x + 12.0, top + 30.0}, turned.heading, face(17.0, 600), ink);
            painter.draw_text_utf8(Point{bounds.x + 12.0, top + 52.0}, turned.line, face(13.0, 400), soft_ink);
        }
    } else {
        // A still tile: its mark, large, in the middle.
        const double size = spec.size == TileSize::small ? 22.0 : 34.0;
        const FontSpec mark_font = face(size, 300);
        const Size measured = painter.measure_text_utf8(spec.mark, mark_font);
        const Point centre = centre_of(bounds);
        const double lift = spec.size == TileSize::small ? 0.0 : 8.0;
        painter.draw_text_utf8(Point{centre.x - measured.width / 2.0, centre.y + size * 0.35 - lift}, spec.mark,
                               mark_font, ink);
    }
    if (spec.size != TileSize::small) {
        painter.draw_text_utf8(Point{bounds.x + 12.0, bounds.y + bounds.height - 11.0}, spec.name, face(13.0, 400),
                               ink);
    }
    painter.restore();
    if (selected_ == stxlib::Optional<std::size_t>::of(index)) {
        // The selection: a white edge, heavier while the field has focus.
        const double edge = focused_ ? 3.0 : 2.0;
        const Rect inset{bounds.x + edge / 2.0, bounds.y + edge / 2.0, bounds.width - edge, bounds.height - edge};
        painter.stroke_rect(inset, focused_ ? ink : soft_ink, edge);
    }
}

void TileField::paint_launched(Painter& painter, const TileLayout& layout) const {
    if (launched_ >= layout.tiles.size()) return;
    const TileSpec& spec = tile_spec(groups, layout.tiles[launched_]);
    const Rect area = launch_rect(layout);
    painter.fill_rect(area, spec.colour);
    // The page's type fades in over the last stretch of the zoom.
    const double reveal = std::clamp((launch_progress_ - 0.6) / 0.4, 0.0, 1.0);
    if (reveal <= 0.0) return;
    painter.save();
    painter.clip_rect(area);
    painter.draw_text_utf8(Point{area.x + surface_left, area.y + 96.0}, spec.name, face(42.0, 300),
                           with_alpha(ink, reveal));
    double line = area.y + 150.0;
    const std::size_t faces = face_count(spec);
    for (std::size_t index = 0U; index < faces; ++index) {
        painter.draw_text_utf8(Point{area.x + surface_left, line}, spec.faces[index].heading, face(19.0, 600),
                               with_alpha(ink, reveal));
        painter.draw_text_utf8(Point{area.x + surface_left, line + 24.0}, spec.faces[index].line, face(15.0, 400),
                               with_alpha(soft_ink, reveal));
        line += 64.0;
    }
    painter.draw_text_utf8(Point{area.x + surface_left, area.y + area.height - surface_bottom},
                           "Escape or a right click returns to Start", face(13.0, 400), with_alpha(soft_ink, reveal));
    painter.restore();
}

void TileField::on_paint(Painter& painter, Rect) {
    // Shown: timers waiting for a paint start again.
    motion_frames_.note_painted();
    live_frames_.note_painted();
    const Rect bounds = field_bounds();
    const std::array<GradientStop, 2> ground{{{.offset = 0.0, .color = ground_top},
                                              {.offset = 1.0, .color = ground_bottom}}};
    painter.fill_linear_gradient(bounds, Point{0.0, 0.0}, Point{0.0, bounds.height}, ground);
    const TileLayout layout = current_layout();
    if (stage_ != LaunchStage::open) {
        painter.draw_text_utf8(Point{surface_left, 84.0}, "Start", face(40.0, 300), ink);
        const std::string_view hint = "Wheel or drag to pan · click to select · click again or Enter to open";
        const FontSpec hint_font = face(13.0, 400);
        const Size measured = painter.measure_text_utf8(hint, hint_font);
        painter.draw_text_utf8(Point{bounds.width - measured.width - 40.0, 80.0}, hint, hint_font, soft_ink);
        painter.save();
        painter.clip_rect(bounds);
        painter.translate(Point{-offset_, 0.0});
        for (const GroupPlacement& group : layout.groups) {
            if (group.right < offset_ || group.header.x > offset_ + bounds.width) continue;
            painter.draw_text_utf8(Point{group.header.x, group.header.y + 22.0}, groups[group.group].name,
                                   face(17.0, 400), soft_ink);
        }
        for (std::size_t index = 0U; index < layout.tiles.size(); ++index) {
            const Rect tile = layout.tiles[index].bounds;
            if (tile.x + tile.width < offset_ || tile.x > offset_ + bounds.width) continue;
            paint_tile(painter, layout, index, tile);
        }
        painter.restore();
    }
    if (stage_ != LaunchStage::tiles) paint_launched(painter, layout);
}

// -- semantics ------------------------------------------------------------------

SemanticDescriptor TileField::semantic_descriptor() const {
    SemanticDescriptor descriptor = Control::semantic_descriptor();
    descriptor.role = SemanticRole::list;
    descriptor.name = accessible_name.read().empty() ? "Start" : accessible_name.read();
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> TileField::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes;
    const TileLayout layout = current_layout();
    const Rect absolute = absolute_bounds();
    const double width = viewport_width();
    nodes.reserve(layout.tiles.size());
    for (std::size_t index = 0U; index < layout.tiles.size(); ++index) {
        const TileSpec& spec = tile_spec(groups, layout.tiles[index]);
        const Rect bounds = layout.tiles[index].bounds;
        SemanticNode node{};
        node.stable_id = node_id(layout, index);
        node.role = SemanticRole::list_item;
        node.name = std::string(spec.name);
        const std::size_t faces = face_count(spec);
        if (faces > 0U) {
            const TileFace& shown = spec.faces[faces_[index] % faces];
            node.description = std::string(shown.heading) + ", " + std::string(shown.line);
        }
        node.position_in_set.set(index + 1U);
        node.set_size.set(layout.tiles.size());
        node.bounds = {absolute.x + bounds.x - offset_, absolute.y + bounds.y, bounds.width, bounds.height};
        node.states = SemanticState::focusable | SemanticState::enabled;
        const bool in_view = bounds.x + bounds.width > offset_ && bounds.x < offset_ + width;
        if (in_view && stage_ == LaunchStage::tiles) node.states |= SemanticState::visible;
        const bool selected = selected_ == stxlib::Optional<std::size_t>::of(index);
        if (selected) node.states |= SemanticState::selected;
        if (selected && focused_) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::select, SemanticAction::press};
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool TileField::on_semantic_child_action(std::string_view id, SemanticAction action, std::string_view) {
    const TileLayout layout = current_layout();
    for (std::size_t index = 0U; index < layout.tiles.size(); ++index) {
        if (node_id(layout, index) != id) continue;
        if (action == SemanticAction::select) {
            select_index(index, layout);
            return true;
        }
        if (action == SemanticAction::press) {
            launch_index(index);
            return stage_ != LaunchStage::tiles;
        }
        return false;
    }
    return false;
}

// -- the window -----------------------------------------------------------------

std::unique_ptr<Window> make_touch_start() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("touch.root"));
    std::shared_ptr<TileField> field = make_control<TileField>(StableId("touch.tiles"));
    (*field).dock = DockStyle::fill;
    (*root).add_child(field);
    std::unique_ptr<Window> window =
        std::make_unique<Window>(root, window_size, std::make_shared<ApplicationResources>());
    (*window).perform_layout();
    static_cast<void>((*window).request_focus(field));
    return window;
}

bool check_touch_start_tile(Window& window, std::string_view id) {
    const std::shared_ptr<TileField> field = std::dynamic_pointer_cast<TileField>(window.find("touch.tiles"));
    if (field == nullptr) return false;
    return (*field).select_tile(id);
}

} // namespace gui_forms::touch_start
