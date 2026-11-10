#pragma once

#include "gui_forms/controls/ribbon/ribbon.hpp"

#include <cstddef>
#include <cstdint>
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

    void detach() noexcept { ribbon_ = nullptr; }
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

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key_preview(KeyEvent& event) override;
    void on_key(KeyEvent& event) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;

private:
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
};

} // namespace gui_forms
