#pragma once
// The single-surface window manager's desk (owner decisions, 2026-11-15):
// the run's windows are layers in one surface, and the desk keeps their
// rectangles, their states (normal, minimised, maximised), their stacking,
// their frames, the active window and the modal blocking. It serves every
// host that has one surface and no window manager of its own: the browser
// now, the appliance's framebuffer host later. Plain values in and out, so
// the rules are tested on every platform (tests/surface_desk_tests.cpp).
//
// Coordinates are the surface's logical units, which are the run's desktop
// logical units: the surface is the one monitor.
//
// Stacking, back to front, in three bands: the primary window (always the
// back, filling the surface, without a frame); the owned, tool and floating
// windows, each above the windows that own it; then the popups, Splash
// windows above other popups. Raising a window carries the windows it owns
// with it. Minimised windows leave the stacking's drawing and hit tests
// (with the windows they own) and appear as tabs along the surface's bottom
// edge, above the window band and below the popups.
//
// The window controls are the window capsule (the default,
// gui_forms/controls/window_capsule.hpp) or, by the theme's choice
// (SurfaceFrameTheme::window_controls), the title bar's boxes: the desk places the capsule's three
// sections at the controls' end of the title bar and tiles windows to a
// half of the surface for the capsule's move.

#include "gui_forms/host/windowing/host_windowing.hpp"
#include "gui_forms/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gui_forms::host::surface_detail {

// What a desk point lands on. The edges and corners are the frame's resize
// grips; the boxes are the title bar's controls; a layer without a frame
// has only its client area.
enum class SurfaceDeskPart : std::uint8_t {
    none,
    client,
    title_bar,
    minimize_box,
    maximize_box,
    close_box,
    edge_left,
    edge_right,
    edge_top,
    edge_bottom,
    corner_top_left,
    corner_top_right,
    corner_bottom_left,
    corner_bottom_right,
    // A minimised window's tab.
    minimized_tab,
    // The window capsule's sections (SurfaceWindowControls::capsule).
    capsule_left,
    capsule_help,
    capsule_right,
};

enum class SurfaceWindowState : std::uint8_t {
    normal,
    minimized,
    maximized,
    // Filling the surface's left or right half within its frame (the
    // capsule's snap targets).
    tiled_left,
    tiled_right,
};

enum class SurfaceTileSide : std::uint8_t {
    left,
    right,
};

// One layer as the manager adds it. `content` is the client area; a framed
// layer's frame lies around it. `minimum` bounds a user's resize. A layer
// that is not resizable has no grips and cannot be maximised.
struct SurfaceDeskLayerInit final {
    HostWindowId id{};
    HostWindowId owner{};
    HostWindowKind kind{HostWindowKind::standard};
    // For a popup: a Splash takes no input (hit tests pass through it).
    HostTransientKind transient{HostTransientKind::popup};
    Rect content{};
    Size minimum{};
    bool resizable{true};
    bool minimizable{true};
    // False for a framed window without a title bar (a program that draws
    // its window controls into its own content): the edge and grips
    // remain, the title bar and its boxes do not.
    bool title_bar{true};
};

struct SurfaceDeskHit final {
    HostWindowId window{};
    SurfaceDeskPart part{SurfaceDeskPart::none};
    // The point in the window's client coordinates (meaningful for client).
    Point local{};
};

// One drawn layer, back to front (SurfaceDesk::scene).
struct SurfaceDeskQuad final {
    HostWindowId window{};
    Rect content{};
    // The frame's rectangle, equal to `content` for an unframed layer.
    Rect outer{};
    bool framed{};
    bool active{};
    // Which title bar boxes the frame shows (SurfaceDesk::title_boxes).
    bool minimize_box{};
    bool maximize_box{};
    // The title bar shows the window capsule instead of the boxes.
    bool capsule{};
    SurfaceWindowState state{SurfaceWindowState::normal};

    friend bool operator==(const SurfaceDeskQuad& a, const SurfaceDeskQuad& b) noexcept {
        const bool equal = a.window == b.window && a.content == b.content && a.outer == b.outer &&
                           a.framed == b.framed && a.active == b.active && a.minimize_box == b.minimize_box &&
                           a.maximize_box == b.maximize_box && a.capsule == b.capsule && a.state == b.state;
        return equal;
    }
};

// A minimised window's tab, left to right (SurfaceDesk::tabs).
struct SurfaceDeskTab final {
    HostWindowId window{};
    Rect bounds{};
};

// Which end of the title bar holds the window controls (the owner's
// standard is the right end).
enum class SurfaceControlsSide : std::uint8_t {
    right,
    left,
};

// The title bar's window controls: the boxes (minimise, maximise, close),
// or the window capsule.
enum class SurfaceWindowControls : std::uint8_t {
    boxes,
    capsule,
};

// The window manager's own theme specification (owner, 2026-11-15): the
// frame's measures, where its invisible grips sit and how its parts look.
// Lengths are logical units; colours 0xRRGGBB. The defaults are an owner
// choice of 2026-11-15 standing until the theme engine supplies frame parts.
struct SurfaceFrameTheme final {
    // The title bar above a framed window's client area.
    double title_bar_height{28.0};
    // The visible edge on the frame's other sides; 0 is a frame without an
    // edge (the grips still work).
    double border_width{1.0};
    // The invisible resize grips: how far they reach beyond the frame's
    // outer edge, and how far inside it (at least the edge itself).
    double grip_outside{4.0};
    double grip_inside{1.0};
    // Each window control box of the title bar, and the end they sit at:
    // close outermost, then maximise, then minimise.
    double title_box_width{40.0};
    SurfaceControlsSide controls{SurfaceControlsSide::right};
    // The capsule (the owner's choice of default, 2026-11-15) or the
    // classic boxes.
    SurfaceWindowControls window_controls{SurfaceWindowControls::capsule};
    // The capsule: its size, the question mark's middle section (at least
    // the capsule's height, so the halves' rounded inner ends lie under
    // it), and its gap from the controls' end of the title bar, centred
    // in the title bar's height.
    double capsule_width{76.0};
    double capsule_height{20.0};
    double capsule_help_width{20.0};
    double capsule_margin{6.0};
    // The capsule's gestures (WindowCapsuleMeasures): the click slop, the
    // minimise drag, the snap zone at the surface's left and right edges
    // and its release distance, the animation, and the keyboard's move.
    double capsule_slop{4.0};
    double capsule_minimize_distance{40.0};
    double snap_edge{16.0};
    double snap_release{48.0};
    double capsule_animation_milliseconds{160.0};
    double capsule_nudge{16.0};
    // Of a dragged window, at least this much of the frame's top stays
    // inside the surface horizontally, and its top never leaves it.
    double title_bar_kept{48.0};
    // The offset each further window placed at the same spot moves by.
    double cascade_step{24.0};
    // The minimised windows' tabs along the bottom edge.
    double tab_width{160.0};
    double tab_height{24.0};
    double tab_gap{4.0};
    // The surface behind every layer, the edge, and the title bar's
    // background and text, for the active window and the others.
    std::uint32_t background{0x1E1E1EU};
    std::uint32_t border_active{0x3F3F46U};
    std::uint32_t border_inactive{0x2D2D30U};
    std::uint32_t title_active{0x2D2D30U};
    std::uint32_t title_inactive{0x3C3C40U};
    std::uint32_t title_text{0xF0F0F0U};
    // The capsule's face and glyphs; the half that invites the close's
    // second click (green) and the X (red); the snap target shown during a
    // move, drawn over the scene with snap_preview_alpha (0 to 255). These
    // are defaults (2026-11-15), standing until the theme engine supplies
    // frame and capsule colours.
    std::uint32_t capsule_face{0x3F3F46U};
    std::uint32_t capsule_glyph{0xF0F0F0U};
    std::uint32_t capsule_ready{0x2EA043U};
    std::uint32_t capsule_close{0xD1242FU};
    std::uint32_t snap_preview{0x3F7FBFU};
    std::uint32_t snap_preview_alpha{0x50U};
};

class SurfaceDesk final {
public:
    SurfaceDesk();

    // The frame's measures and colours (SurfaceFrameTheme). Takes effect
    // for every layer at once; the caller composes the whole surface again.
    void set_theme(const SurfaceFrameTheme& theme);
    [[nodiscard]] const SurfaceFrameTheme& theme() const noexcept { return theme_; }

    // The surface's logical size. The primary window takes it whole, a
    // maximised layer all of it within its frame; every other framed layer
    // is moved back inside as set_content would.
    void set_extent(Size extent);
    [[nodiscard]] Size extent() const noexcept { return extent_; }
    // Adds a hidden layer at the front of its band. False, adding nothing,
    // for a duplicate or invalid identity, a second primary, or a full desk
    // (HostWindowing::maximum_windows).
    bool add(const SurfaceDeskLayerInit& init);
    // Removes the layer; its owned layers stay (the windowing closes them
    // first). The active window moves to the topmost window that takes it.
    void remove(HostWindowId window) noexcept;
    void set_visible(HostWindowId window, bool visible) noexcept;
    [[nodiscard]] bool visible(HostWindowId window) const noexcept;
    // Visible, not minimised, and owned by no minimised layer.
    [[nodiscard]] bool shown(HostWindowId window) const noexcept;
    [[nodiscard]] bool contains(HostWindowId window) const noexcept;
    // Moves and sizes a framed layer's client area: the size at least its
    // minimum (and at least 1 by 1), at most the surface less the frame; the
    // position keeps the title bar reachable. A maximised layer becomes
    // normal first. A popup is placed as given; the primary window always
    // fills the surface. Returns the rectangle applied, or nothing for an
    // unknown window.
    std::optional<Rect> set_content(HostWindowId window, Rect content);
    [[nodiscard]] std::optional<Rect> content(HostWindowId window) const noexcept;
    [[nodiscard]] std::optional<Rect> outer(HostWindowId window) const noexcept;
    [[nodiscard]] bool framed(HostWindowId window) const noexcept;

    // Window states. minimize, maximize and tile refuse (false, changing nothing)
    // an unframed or blocked layer, one whose kind does not allow it (not
    // minimizable; not resizable for maximize), or one already in that
    // state. Minimising moves the keyboard to the topmost window that takes
    // it; maximising remembers the normal rectangle, which restore puts
    // back. restore from minimised returns to the state the window was
    // minimised from. Tiling gives the window the surface's left or right
    // half and, from normal, remembers the normal rectangle as maximise
    // does. Each returns whether the state changed.
    bool minimize(HostWindowId window);
    bool maximize(HostWindowId window);
    bool tile(HostWindowId window, SurfaceTileSide side);
    bool restore(HostWindowId window);
    [[nodiscard]] SurfaceWindowState state(HostWindowId window) const noexcept;

    // Puts the window at the front of its band with the windows it owns
    // above it in their order. The primary window stays at the back.
    void raise(HostWindowId window);
    // Live layers, back to front.
    [[nodiscard]] std::span<const HostWindowId> order() const& noexcept { return order_; }
    std::span<const HostWindowId> order() const&& = delete;
    // The shown layers to draw, back to front, filled into `quads` (its
    // storage reused).
    void scene(std::vector<SurfaceDeskQuad>& quads) const;
    // The minimised windows' tabs, filled into `tabs` (its storage reused).
    void tabs(std::vector<SurfaceDeskTab>& tabs) const;
    // The topmost shown layer or tab at `point` that takes input, and the
    // part.
    [[nodiscard]] SurfaceDeskHit hit(Point point) const noexcept;
    // The title bar boxes or capsule sections of `window`'s frame, in
    // surface coordinates, empty where it shows none.
    [[nodiscard]] Rect box(HostWindowId window, SurfaceDeskPart part) const noexcept;
    // The whole capsule of `window`'s frame, empty where it shows none.
    [[nodiscard]] Rect capsule(HostWindowId window) const noexcept;
    // The frame a window tiled to `side` would have: the capsule's snap
    // preview.
    [[nodiscard]] Rect tiled_outer(SurfaceTileSide side) const noexcept;

    // run_modal (HostWindowing::run_modal): every live layer that `modal`
    // does not own, at any depth, and that is not `modal` itself, refuses
    // input until end_modal for the same window. The set is fixed here, so
    // a window opened during the modal session is not blocked. Calls nest.
    void begin_modal(HostWindowId modal);
    void end_modal(HostWindowId modal) noexcept;
    [[nodiscard]] bool blocked(HostWindowId window) const noexcept;
    [[nodiscard]] std::size_t modal_depth() const noexcept { return modals_.size(); }
    // Whether `window` is `ancestor` or owned by it at any depth.
    [[nodiscard]] bool owned_by(HostWindowId window, HostWindowId ancestor) const noexcept;

    // The window with the keyboard (invalid while there is none).
    [[nodiscard]] HostWindowId active() const noexcept { return active_; }
    // Makes `window` active and raises it. A blocked window's activation
    // goes to the innermost modal window that blocks it instead; a Splash
    // or a passive popup is never activated, nor a minimised window (the
    // active window stays). Returns the window that is active afterwards.
    HostWindowId activate(HostWindowId window, bool takes_focus = true);
    // Activates the topmost shown window that may take the keyboard;
    // returns it, or an invalid identity.
    HostWindowId activate_topmost();

    // A frame drag (a title bar move or a grip resize) of `window` begun at
    // `point` on `part`. False for an unframed or maximised layer, a client,
    // box or tab part, or a blocked window.
    bool begin_frame_drag(HostWindowId window, SurfaceDeskPart part, Point point);
    // The client rectangle the drag gives at `point`, applied as
    // set_content does; nothing while no drag is under way.
    std::optional<Rect> frame_drag_to(Point point);
    void end_frame_drag() noexcept { dragging_ = false; }
    // The capsule's move (a title bar drag) begun at `point`: a maximised or
    // tiled window first becomes normal at its remembered size, placed so
    // that the point stays at the same fraction of its title bar's width.
    // False, changing nothing, where begin_frame_drag would refuse a normal
    // window or the window is minimised.
    bool begin_capsule_move(HostWindowId window, Point point);
    [[nodiscard]] bool frame_dragging() const noexcept { return dragging_; }
    [[nodiscard]] HostWindowId frame_drag_window() const noexcept { return drag_window_; }

    // Where a new window of `kind` and client size `size` opens: centred
    // over its owner's client area (the surface's centre without an owner),
    // a floating window at its owner's right edge, then moved by
    // cascade_step while another layer's client area starts at the same
    // point, and kept inside the surface.
    [[nodiscard]] Rect placement(HostWindowKind kind, HostWindowId owner, Size size) const;

private:
    struct Layer final {
        HostWindowId id{};
        HostWindowId owner{};
        HostWindowKind kind{HostWindowKind::standard};
        HostTransientKind transient{HostTransientKind::popup};
        Rect content{};
        Size minimum{};
        bool resizable{true};
        bool minimizable{true};
        bool title_bar{true};
        bool visible{};
        SurfaceWindowState state{SurfaceWindowState::normal};
        // The state a minimised layer returns to (normal, maximised or
        // tiled).
        SurfaceWindowState minimized_from{SurfaceWindowState::normal};
        // The normal client rectangle while maximised or tiled.
        Rect restore_content{};
    };
    struct ModalScope final {
        HostWindowId modal{};
        std::vector<HostWindowId> blocked{};
    };

    [[nodiscard]] Layer* layer(HostWindowId window) noexcept;
    [[nodiscard]] const Layer* layer(HostWindowId window) const noexcept;
    [[nodiscard]] static bool is_framed(const Layer& entry) noexcept;
    // The frame's height above the client area: the title bar, or the edge
    // of a layer without one.
    [[nodiscard]] double top_of(const Layer& entry) const noexcept;
    [[nodiscard]] Rect outer_of(const Layer& entry) const noexcept;
    [[nodiscard]] static int band(const Layer& entry) noexcept;
    [[nodiscard]] static bool shows_minimize(const Layer& entry) noexcept;
    [[nodiscard]] static bool shows_maximize(const Layer& entry) noexcept;
    [[nodiscard]] bool is_shown(const Layer& entry) const noexcept;
    [[nodiscard]] Rect clamped(const Layer& entry, Rect content) const noexcept;
    [[nodiscard]] Rect maximized_content(const Layer& entry) const noexcept;
    [[nodiscard]] Rect tiled_content(const Layer& entry, SurfaceTileSide side) const noexcept;
    // The client rectangle a maximised or tiled state fixes, or `content`
    // for the others.
    [[nodiscard]] Rect state_content(const Layer& entry, Rect content) const noexcept;
    [[nodiscard]] bool shows_capsule(const Layer& entry) const noexcept;
    [[nodiscard]] Rect capsule_of(const Layer& entry) const noexcept;
    // Whether the layer's kind allows the state at all; shows_minimize and
    // shows_maximize add that its title bar holds the box.
    [[nodiscard]] static bool can_minimize(const Layer& entry) noexcept;
    [[nodiscard]] static bool can_maximize(const Layer& entry) noexcept;
    [[nodiscard]] Rect box_of(const Layer& entry, SurfaceDeskPart part) const noexcept;
    [[nodiscard]] SurfaceDeskPart frame_part(const Layer& entry, Point point) const noexcept;
    // The minimised window whose tab holds `point`, or none.
    [[nodiscard]] SurfaceDeskHit tab_hit(Point point) const noexcept;
    // The index in order_ where a layer of `band_index` goes to be the front
    // of its band.
    [[nodiscard]] std::size_t band_end(int band_index) const noexcept;
    [[nodiscard]] bool takes_keyboard(const Layer& entry) const noexcept;
    void keyboard_left(HostWindowId window);

    // Capacity HostWindowing::maximum_windows, reserved at construction.
    std::vector<Layer> layers_;
    // Live identities, back to front.
    std::vector<HostWindowId> order_;
    // Scratch for raise: the moved family in stacking order.
    std::vector<HostWindowId> family_;
    std::vector<ModalScope> modals_;
    SurfaceFrameTheme theme_{};
    HostWindowId active_{};
    Size extent_{1.0, 1.0};
    bool dragging_{};
    HostWindowId drag_window_{};
    SurfaceDeskPart drag_part_{SurfaceDeskPart::none};
    Point drag_origin_{};
    Rect drag_start_{};
};

} // namespace gui_forms::host::surface_detail
