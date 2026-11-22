#pragma once

#include "gui_forms/controls/ribbon.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace gui_forms {

// The File tab's two-pane application menu (stage RB, phase 6; owner
// decisions 3, 23), shown while the ribbon has recent items: the
// application menu's commands down the left and the recent items, under
// their caption, down the right, on a panel below the File tab that casts
// the overlay shadow. A submenu row opens its items in the ribbon's menu
// (a ContextMenu, in the owner's Popup windows) beside the row. Private to
// the ribbon, which owns it, opens it as an overlay covering the window
// client (a press outside the panel closes it, as does Escape) and closes
// it before it goes; `ribbon` observes the owner and is cleared by
// detach().
//
// Where the run has Popup windows the panel is a separate instance lifted
// into the owner's picker Popup (PopupLift; owner, 2026-11-08: Part S), so
// that it may reach past the owner's edges, and the overlay instance is a
// catcher that paints nothing, takes the outside presses and holds the
// focus scope while the Popup has the keys.
//
// Its submenu rows follow the menu rule (owner, 2026-11-22; menu_utilities.hpp):
// resting on a submenu row for the menu show delay opens its submenu,
// resting on another row for the delay closes it, and a move from the open
// submenu's row toward the submenu across other rows (the safe triangle)
// leaves the rows as they are. The rest waits on the ribbon's window's
// timer; a pointer that reaches the submenu meanwhile (the ribbon's menu
// took a move over its rows), leaves every panel, or a key cancels it. A
// press on a submenu row, Right, Enter and Space open at once.
class RibbonApplicationMenu final : public Control {
public:
    enum class Mode : std::uint8_t {
        // The panel at its origin inside a client-sized overlay.
        whole,
        // The client-sized overlay of a lifted panel: no panel, no rows.
        catcher,
        // The lifted panel itself, at 0,0 in its Popup.
        panel,
    };

    RibbonApplicationMenu(StableId stable_id, Ribbon& ribbon, Mode mode = Mode::whole);

    void detach() noexcept {
        cancel_settle();
        ribbon_ = nullptr;
    }
    void set_mode(Mode mode);
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    // The panel's size for the ribbon's items now, in logical units.
    [[nodiscard]] Size panel_size() const;
    // Drops the row whose submenu showed (the ribbon's menu closed).
    void submenu_closed();
    // Places the panel's top-left corner, in window coordinates.
    void set_origin(Point origin);
    // Puts the keyboard on the first row (keyboard opening) or nowhere.
    void reset(bool keyboard);

    // One row of either pane as laid out: its bounds in local coordinates,
    // whether it is a recent item, its index in its list (the application
    // menu's items or the recent items), and whether it is a separator.
    struct Row final {
        Rect bounds{};
        bool recent{};
        std::size_t index{};
        bool separator{};
    };
    struct Layout final {
        Rect panel{};
        Rect commands{};
        Rect recent{};
        Rect caption{};
        std::vector<Row> rows;
    };
    [[nodiscard]] Layout layout() const;
    // The row under the keyboard, as an index into layout().rows, or none.
    [[nodiscard]] std::optional<std::size_t> focus_row() const noexcept { return focus_row_; }

    // The pointer moved to `local` (the panel's local coordinates): its own
    // move, or one the ribbon's menu's layer took over the panel while a
    // submenu shows and the panel shows whole in the client. The row under
    // it turns hot and the menu rule runs.
    void pointer_moved_at(Point local);
    // A press or a release at `local` that the ribbon's menu's layer took
    // over the panel (whole in the client, a submenu showing): handled as
    // the panel's own. False, doing nothing, when `local` is outside the
    // panel.
    bool forwarded_press(Point local);
    bool forwarded_release(Point local);

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_key_preview(KeyEvent& event) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;

protected:
    void on_key_down(KeyEventArgs& arguments) override;

    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;

private:
    // The settle timer's target: the menu, observed, and the wait's serial
    // (a wait replaced or cancelled meanwhile is stale).
    struct SettleElapsed final {
        std::weak_ptr<Control> menu;
        std::uint64_t serial{};
        void operator()(FrameTime) const;
    };

    void set_hot_row(std::optional<std::size_t> hot);
    // The primary press at `local`: the catcher's, or the panel's.
    void press(Point local);
    // The primary release at `local`: a click is a release over a row.
    void release(Point local);
    // `arguments`' point in the panel's local coordinates.
    [[nodiscard]] Point local_point(const MouseEventArgs& arguments) const;
    // The menu show delay the ribbon's window reports now.
    [[nodiscard]] std::chrono::milliseconds menu_show_delay() const;
    // Whether `row` is an enabled submenu row with items.
    [[nodiscard]] bool opens_submenu(const Row& row) const;
    // Whether a move from `from` to `to` (owner client coordinates) heads
    // for the open submenu (the safe triangle).
    [[nodiscard]] bool aiming_at_submenu(const Layout& placed, Point from, Point to) const;
    void cancel_settle() noexcept;
    // Waits `delay` for the pointer to rest on `row`, replacing any wait.
    void schedule_settle(std::size_t row, std::chrono::milliseconds delay);
    void settle_elapsed(std::uint64_t serial);
    // The pointer rested on `row`: its submenu opens, or the open submenu
    // of another row closes.
    void settle_on(std::size_t row);
    [[nodiscard]] bool ribbon_alive() const noexcept;
    // Whether the ribbon's menu shows a submenu of this panel's.
    [[nodiscard]] bool submenu_open() const noexcept;
    void open_submenu(std::size_t row, bool keyboard);
    [[nodiscard]] bool row_enabled(const Row& row) const;
    [[nodiscard]] std::string row_text(const Row& row) const;
    [[nodiscard]] std::optional<std::size_t> row_at(const Layout& placed, Point local) const;
    void activate(const Row& row);
    void move(bool forward);
    void switch_pane();

    Ribbon* ribbon_{};
    Mode mode_{Mode::whole};
    Point origin_{};
    // The row, as an index into layout().rows, whose submenu shows.
    std::optional<std::size_t> submenu_row_{};
    std::optional<std::size_t> hot_row_{};
    std::optional<std::size_t> focus_row_{};
    // The row the pointer rests on, waiting for the menu show delay, and its
    // timer (on the ribbon's window); none, and disconnected, while nothing
    // waits. `settle_moves_` is the ribbon's menu's move count when the wait
    // began, `settle_serial_` counts waits.
    std::optional<std::size_t> settle_row_{};
    FrameRequestToken settle_timer_{};
    std::uint64_t settle_moves_{};
    std::uint64_t settle_serial_{};
    // The pointer's last move over the panel, in the owner's client
    // coordinates, and the ribbon's menu's move count then (a move over the
    // submenu since makes it no start for the safe triangle); none until a
    // move, and after the pointer leaves.
    std::optional<Point> last_pointer_{};
    std::uint64_t last_pointer_moves_{};
};

} // namespace gui_forms
