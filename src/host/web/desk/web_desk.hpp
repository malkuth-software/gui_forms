#pragma once
// The web host's desk (owner decision, 2026-11-15): the run's windows are
// layers in one canvas, and the host, not a window manager, keeps their
// rectangles, their stacking, their frames, the active window and the modal
// blocking. Plain values in and out, no Emscripten, so the rules are tested
// on every platform (tests/web_desk_tests.cpp).
//
// Coordinates are the canvas's CSS pixels, which are the run's desktop
// logical units: the canvas is the one monitor.
//
// Stacking, back to front, in three bands: the primary window (always the
// back, filling the canvas, without a frame); the owned, tool and floating
// windows, each above the windows that own it; then the popups, Splash
// windows above other popups. Raising a window carries the windows it owns
// with it.

#include "gui_forms/host/windowing/host_windowing.hpp"
#include "gui_forms/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gui_forms::host::web_detail {

// What a desk point lands on. The edges and corners are the frame's resize
// grips; a layer without a frame has only its client area.
enum class WebDeskPart : std::uint8_t {
    none,
    client,
    title_bar,
    close_box,
    edge_left,
    edge_right,
    edge_top,
    edge_bottom,
    corner_top_left,
    corner_top_right,
    corner_bottom_left,
    corner_bottom_right,
};

// One layer as the host adds it. `content` is the client area; a framed
// layer's frame lies around it. `minimum` bounds a user's resize.
struct WebDeskLayerInit final {
    HostWindowId id{};
    HostWindowId owner{};
    HostWindowKind kind{HostWindowKind::standard};
    // For a popup: a Splash takes no input (hit tests pass through it).
    HostTransientKind transient{HostTransientKind::popup};
    Rect content{};
    Size minimum{};
    bool resizable{true};
};

struct WebDeskHit final {
    HostWindowId window{};
    WebDeskPart part{WebDeskPart::none};
    // The point in the window's client coordinates (meaningful for client).
    Point local{};
};

// One visible layer of a frame, back to front (WebDesk::scene).
struct WebDeskQuad final {
    HostWindowId window{};
    Rect content{};
    // The frame's rectangle, equal to `content` for an unframed layer.
    Rect outer{};
    bool framed{};
    bool active{};
};

class WebDesk final {
public:
    // The frame every owned, tool and floating window has: a title bar
    // above the client area and a border on the other sides, in logical
    // units. The resize grips reach edge_reach beyond the border.
    static constexpr double title_bar_height = 28.0;
    static constexpr double border_width = 1.0;
    static constexpr double edge_reach = 4.0;
    // The close box at the title bar's right end.
    static constexpr double close_box_width = 40.0;
    // Of a dragged window, at least this much of the title bar stays inside
    // the canvas horizontally, and the title bar's top never leaves it.
    static constexpr double title_bar_kept = 48.0;
    // The offset each further window placed at the same spot moves by.
    static constexpr double cascade_step = 24.0;

    WebDesk();

    // The canvas's logical size. The primary window takes it whole; every
    // framed layer is moved back inside as set_content would.
    void set_extent(Size extent);
    [[nodiscard]] Size extent() const noexcept { return extent_; }

    // Adds a hidden layer at the front of its band. False, adding nothing,
    // for a duplicate or invalid identity, a second primary, or a full desk
    // (HostWindowing::maximum_windows).
    bool add(const WebDeskLayerInit& init);
    // Removes the layer; its owned layers stay (the windowing closes them
    // first). The active window moves to the topmost window that takes it.
    void remove(HostWindowId window) noexcept;
    void set_visible(HostWindowId window, bool visible) noexcept;
    [[nodiscard]] bool visible(HostWindowId window) const noexcept;
    [[nodiscard]] bool contains(HostWindowId window) const noexcept;

    // Moves and sizes a framed layer's client area: the size at least its
    // minimum (and at least 1 by 1), at most the canvas less the frame; the
    // position keeps the title bar reachable. A popup is placed as given;
    // the primary window always fills the canvas. Returns the rectangle
    // applied, or nothing for an unknown window.
    std::optional<Rect> set_content(HostWindowId window, Rect content);
    [[nodiscard]] std::optional<Rect> content(HostWindowId window) const noexcept;
    [[nodiscard]] std::optional<Rect> outer(HostWindowId window) const noexcept;
    [[nodiscard]] bool framed(HostWindowId window) const noexcept;

    // Puts the window at the front of its band with the windows it owns
    // above it in their order. The primary window stays at the back.
    void raise(HostWindowId window);
    // Live layers, back to front.
    [[nodiscard]] std::span<const HostWindowId> order() const& noexcept { return order_; }
    std::span<const HostWindowId> order() const&& = delete;
    // The visible layers to draw, back to front, filled into `quads` (its
    // storage reused).
    void scene(std::vector<WebDeskQuad>& quads) const;

    // The topmost visible layer at `point` that takes input, and the part.
    [[nodiscard]] WebDeskHit hit(Point point) const noexcept;

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
    // or a passive popup is never activated (the active window stays).
    // Returns the window that is active afterwards.
    HostWindowId activate(HostWindowId window, bool takes_focus = true);
    // Activates the topmost visible window that may take the keyboard;
    // returns it, or an invalid identity.
    HostWindowId activate_topmost();

    // A frame drag (a title bar move or a grip resize) of `window` begun at
    // `point` on `part`. False for an unframed layer, a client or close box
    // part, or a blocked window.
    bool begin_frame_drag(HostWindowId window, WebDeskPart part, Point point);
    // The client rectangle the drag gives at `point`, applied as
    // set_content does; nothing while no drag is under way.
    std::optional<Rect> frame_drag_to(Point point);
    void end_frame_drag() noexcept { dragging_ = false; }
    [[nodiscard]] bool frame_dragging() const noexcept { return dragging_; }
    [[nodiscard]] HostWindowId frame_drag_window() const noexcept { return drag_window_; }

    // Where a new window of `kind` and client size `size` opens: centred
    // over its owner's client area (the canvas's centre without an owner),
    // a floating window at its owner's right edge, then moved by
    // cascade_step while another layer's client area starts at the same
    // point, and kept inside the canvas.
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
        bool visible{};
    };
    struct ModalScope final {
        HostWindowId modal{};
        std::vector<HostWindowId> blocked{};
    };

    [[nodiscard]] Layer* layer(HostWindowId window) noexcept;
    [[nodiscard]] const Layer* layer(HostWindowId window) const noexcept;
    [[nodiscard]] static bool is_framed(const Layer& entry) noexcept;
    [[nodiscard]] static Rect outer_of(const Layer& entry) noexcept;
    [[nodiscard]] static int band(const Layer& entry) noexcept;
    [[nodiscard]] Rect clamped(const Layer& entry, Rect content) const noexcept;
    [[nodiscard]] WebDeskPart frame_part(const Layer& entry, Point point) const noexcept;
    // The index in order_ where a layer of `band_index` goes to be the front
    // of its band.
    [[nodiscard]] std::size_t band_end(int band_index) const noexcept;
    [[nodiscard]] bool takes_keyboard(const Layer& entry) const noexcept;

    // Capacity HostWindowing::maximum_windows, reserved at construction.
    std::vector<Layer> layers_;
    // Live identities, back to front.
    std::vector<HostWindowId> order_;
    // Scratch for raise: the moved family in stacking order.
    std::vector<HostWindowId> family_;
    std::vector<ModalScope> modals_;
    HostWindowId active_{};
    Size extent_{1.0, 1.0};
    bool dragging_{};
    HostWindowId drag_window_{};
    WebDeskPart drag_part_{WebDeskPart::none};
    Point drag_origin_{};
    Rect drag_start_{};
};

} // namespace gui_forms::host::web_detail
