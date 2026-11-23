#pragma once

#include "touch_input.hpp"

#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <stxlib/optional.hpp>
#include <vector>

// The touch start demonstrator (owner, 2026-11-21; notes:planning/stages/
// TC-touch/README.md): a start screen in the style of Windows 8's, an
// original design rather than a copy, whose point is telling a flick (pan)
// from a tap (select). A wide surface of live tiles in named groups pans
// sideways; a tile is selected, then launched, zooming to fill the window,
// and Escape brings the tiles back.
//
// Everything but touch works now: the wheel pans (a vertical wheel too, as
// the style has it), a mouse drag pans the surface directly, a click
// selects and a click on the selected tile launches it, the arrow keys move
// the selection to the nearest tile that way and glide it into view, Home
// and End go to the ends, Page Up and Page Down pan a screen, and Enter or
// Space launches. The live tiles turn their faces in turn, and the launch,
// the glide and the faces' slide run on the window's frames (AnimationTimer)
// eased, still under reduced motion.
//
// Touch plugs in at touch_input.hpp: every press, move and release goes
// through its GestureClassifier, every frame of a moving pan through
// PanInertia and settle_pan_offset; the TODO(touch) notes there and in the
// field's handlers say what each fills in.
//
// Demo code over public GUI.Forms vocabulary: no host, renderer or platform
// type appears here. The hosts' mains are src/host/*/touch_start_main.*;
// every one takes the demonstration programs' options
// (program_options.hpp), --headless --capture and --check included.
namespace gui_forms::touch_start {

inline constexpr std::string_view program_name = "GUI.Forms Touch Start";
inline constexpr Size window_size{1200.0, 720.0};
// The smallest window the tiles stay usable in: four tile rows.
inline constexpr Size minimum_window_size{720.0, 520.0};

// The tile grid, in logical pixels: a small tile is one unit square, a
// medium one two, a wide one four by two and a large one four square, with
// `tile_gap` between units.
inline constexpr double tile_unit = 62.0;
inline constexpr double tile_gap = 8.0;
// The surface's margins: the title band above, the left margin, and the
// space between groups and under the last row.
inline constexpr double surface_top = 132.0;
inline constexpr double surface_left = 104.0;
inline constexpr double group_gap = 64.0;
inline constexpr double surface_bottom = 48.0;
// The group name's band over its tiles.
inline constexpr double group_header = 34.0;
// A group's tiles flow in columns four units wide.
inline constexpr std::size_t group_column_units = 4U;
// The rows the surface has: as many as the height holds, within these.
inline constexpr std::size_t minimum_rows = 4U;
inline constexpr std::size_t maximum_rows = 8U;
// How far one wheel line pans.
inline constexpr double wheel_line_pan = 48.0;

enum class TileSize : std::uint8_t {
    small,
    medium,
    wide,
    large,
};

// A tile's width and height in grid units.
[[nodiscard]] std::size_t tile_columns(TileSize size) noexcept;
[[nodiscard]] std::size_t tile_rows(TileSize size) noexcept;

// One face a live tile turns to: a heading and a line under it.
struct TileFace final {
    std::string_view heading{};
    std::string_view line{};
};

// One tile: its stable id segment, the name on it, the short mark drawn
// large on it, its size and colour, and the faces it turns through (none
// for a still tile; small tiles show none).
struct TileSpec final {
    std::string_view id{};
    std::string_view name{};
    std::string_view mark{};
    TileSize size{TileSize::medium};
    Color colour{};
    std::array<TileFace, 3> faces{};
};

// The faces `tile` turns through: the leading non-empty ones.
[[nodiscard]] std::size_t face_count(const TileSpec& tile) noexcept;
[[nodiscard]] bool live_tile(const TileSpec& tile) noexcept;

// A named group of tiles, in order.
struct TileGroup final {
    std::string_view id{};
    std::string_view name{};
    std::span<const TileSpec> tiles{};
};

// The demonstrator's groups.
[[nodiscard]] std::span<const TileGroup> start_groups() noexcept;

// Where one tile lies on the surface (pan offset zero): its group, its index
// in the group and its rectangle.
struct TilePlacement final {
    std::size_t group{};
    std::size_t tile{};
    Rect bounds{};
};

// Where one group's name sits and how far its tiles reach.
struct GroupPlacement final {
    std::size_t group{};
    Rect header{};
    double right{};
};

// The whole surface: every tile in reading order (group by group), every
// group, the surface's width and the rows it was laid out with.
struct TileLayout final {
    std::vector<TilePlacement> tiles{};
    std::vector<GroupPlacement> groups{};
    double extent{};
    std::size_t rows{};
};

// The rows a field `field_height` tall holds, within minimum_rows and
// maximum_rows.
[[nodiscard]] std::size_t rows_for_height(double field_height) noexcept;
// Lays `groups` out for a field `field_height` tall: each group's tiles
// fill columns four units wide top to bottom, then left to right, each at
// the first place it fits.
[[nodiscard]] TileLayout lay_out_tiles(std::span<const TileGroup> groups, double field_height);

enum class TileDirection : std::uint8_t {
    left,
    right,
    up,
    down,
};

// The tile nearest `from` in `direction`: its centre must lie that way,
// and the distance across the direction counts twice. None at the edge.
[[nodiscard]] stxlib::Optional<std::size_t> neighbour_tile(const TileLayout& layout, std::size_t from,
                                                           TileDirection direction) noexcept;
// The tile under `surface_point` (surface coordinates, pan included).
[[nodiscard]] stxlib::Optional<std::size_t> tile_at(const TileLayout& layout, Point surface_point) noexcept;
// The largest pan offset for a field `viewport_width` wide.
[[nodiscard]] double maximum_pan_offset(const TileLayout& layout, double viewport_width) noexcept;
// The nearest offset to `offset` that shows `tile` whole with the margin
// beside it, within the ends.
[[nodiscard]] double offset_revealing(const TileLayout& layout, std::size_t tile, double offset,
                                      double viewport_width) noexcept;

// The stable id segment of tile `index` in `layout`'s reading order.
[[nodiscard]] std::string_view tile_id(std::span<const TileGroup> groups, const TileLayout& layout,
                                       std::size_t index) noexcept;
[[nodiscard]] const TileSpec& tile_spec(std::span<const TileGroup> groups, const TilePlacement& placement) noexcept;

// What the field shows: the tiles, a tile zooming open, the launched tile,
// or the launched tile zooming back.
enum class LaunchStage : std::uint8_t {
    tiles,
    opening,
    open,
    closing,
};

// The panning surface of live tiles.
class TileField final : public Control {
public:
    explicit TileField(StableId stable_id);

    // Selects the tile with id segment `id` and glides it into view; false
    // when there is none.
    [[nodiscard]] bool select_tile(std::string_view id);
    // The selected tile's id segment; empty when none is selected.
    [[nodiscard]] std::string_view selected_tile_id() const noexcept;
    // Launches the selected tile; false when none is selected or one is
    // already open.
    [[nodiscard]] bool launch_selected();
    // Back to the tiles from a launched tile; false when none is open.
    [[nodiscard]] bool return_to_tiles();
    [[nodiscard]] LaunchStage launch_stage() const noexcept;
    // The pan offset the surface is drawn at, and where a glide is taking it.
    [[nodiscard]] double pan_offset() const noexcept;
    [[nodiscard]] double pan_target() const noexcept;
    // The face live tile `index` (reading order) shows.
    [[nodiscard]] std::size_t tile_face(std::size_t index) const noexcept;
    // Turns the next live tile in turn to its next face, as its timer does.
    void turn_next_live_tile(FrameTime now);
    // The surface as laid out for the field's present height.
    [[nodiscard]] TileLayout current_layout() const;

    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    void on_attached_to_window() override;
    void on_detached_from_window() noexcept override;
    void on_focus_changed(bool focused) override;

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_wheel(MouseEventArgs& arguments) override;
    void on_mouse_capture_changed() override;
    void on_key_down(KeyEventArgs& arguments) override;

private:
    // The motion frames' and the live tiles' callback targets.
    struct MotionTick final {
        std::weak_ptr<TileField> field;
        AnimationTickResult operator()(FrameTime now) const;
    };
    struct LiveTick final {
        std::weak_ptr<TileField> field;
        AnimationTickResult operator()(FrameTime now) const;
    };

    [[nodiscard]] AnimationTickResult on_motion_tick(FrameTime now);
    [[nodiscard]] AnimationTickResult on_live_tick(FrameTime now);
    [[nodiscard]] bool reduced_motion() const noexcept;
    [[nodiscard]] double viewport_width() const noexcept;
    [[nodiscard]] Rect field_bounds() const noexcept;
    [[nodiscard]] std::string node_id(const TileLayout& layout, std::size_t index) const;
    void start_motion();
    void start_live();
    void select_index(std::size_t index, const TileLayout& layout);
    void glide_to(double offset);
    void jump_to(double offset);
    void launch_index(std::size_t index);
    void move_selection(TileDirection direction);
    void activate_at(Point local_point);
    void paint_tile(Painter& painter, const TileLayout& layout, std::size_t index, Rect bounds) const;
    void paint_launched(Painter& painter, const TileLayout& layout) const;
    [[nodiscard]] Rect launch_rect(const TileLayout& layout) const;

    GestureClassifier classifier_{};
    PanInertia inertia_{};
    AnimationTimer motion_frames_{};
    AnimationTimer live_frames_{};
    stxlib::Optional<std::size_t> selected_{};
    bool focused_{};
    double offset_{};
    // The glide: from where, to where, since when.
    double glide_from_{};
    double glide_to_{};
    FrameTime glide_start_{};
    bool gliding_{};
    // The launch and its progress, 0 at the tile and 1 filling the field.
    LaunchStage stage_{LaunchStage::tiles};
    std::size_t launched_{};
    FrameTime launch_start_{};
    double launch_progress_{};
    // The live tiles: each tile's face, the next to turn, and the one
    // sliding to its new face since `slide_start_`.
    std::vector<std::size_t> faces_{};
    std::size_t next_live_{};
    stxlib::Optional<std::size_t> sliding_{};
    FrameTime slide_start_{};
    double slide_progress_{1.0};
    FrameTime last_motion_{};
};

// The demonstrator's window, laid out, the tile field focused.
[[nodiscard]] std::unique_ptr<Window> make_touch_start();

// Selects the tile with id segment `id` (capture staging, --check); false
// when the window holds no such tile.
[[nodiscard]] bool check_touch_start_tile(Window& window, std::string_view id);

} // namespace gui_forms::touch_start
