// The ribbon's file area (stage RB, phase 6; owner decisions 3, 23, 26): the
// application menu's recent pane, shown as a two-pane menu below the File
// tab, and Backstage, a BackstageView over everything below the tab row.

#include "gui_forms/controls/ribbon.hpp"

#include "ribbon_application_menu.hpp"

#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"

#include <stxlib/optional.hpp>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gui_forms {

bool Ribbon::file_area_open() const noexcept {
    const bool result = application_open_ || application_overlay_shown() || backstage_open_;
    return result;
}

// -- recent items --------------------------------------------------------------

// A two-pane menu takes command, separator and submenu items (no checks or
// radio items beside the recent pane); a submenu's items open in the
// ribbon's menu beside its row and take every kind.
void Ribbon::validate_two_pane(const std::vector<MenuItemSpec>& items, std::size_t recent) const {
    if (recent == 0U) return;
    for (const MenuItemSpec& item : items) {
        if (item.kind != MenuItemKind::command && item.kind != MenuItemKind::separator &&
            item.kind != MenuItemKind::submenu) {
            throw std::invalid_argument(
                "Ribbon application menus beside recent items take commands, separators and submenus only");
        }
    }
}

void Ribbon::set_recent_items(std::vector<RibbonRecentItem> items) {
    require_mutable();
    if (items.size() > maximum_recent_items) {
        throw std::invalid_argument("Ribbon has more recent items than maximum_recent_items");
    }
    std::unordered_set<std::string> identities{};
    for (const RibbonRecentItem& item : items) {
        const bool single_line = item.text.find('\n') == std::string::npos &&
                                 item.description.find('\n') == std::string::npos;
        if (!valid_stable_id(item.stable_id) || !validate_utf8(item.text).valid() ||
            !validate_utf8(item.description).valid() || !single_line || !identities.insert(item.stable_id).second) {
            throw std::invalid_argument(
                "Ribbon recent items require unique IDs following the StableId segment rule and single-line UTF-8");
        }
    }
    validate_two_pane(application_menu_, items.size());
    close_application_overlay();
    recent_items_ = std::move(items);
    invalidate(Dirty::semantics);
}

void Ribbon::prepare_write(const decltype(recent_caption)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("Ribbon recent caption must be valid UTF-8");
}

void Ribbon::written(const decltype(recent_caption)&) {
    if (application_overlay_) (*application_overlay_).invalidate(Dirty::paint);
}

Control::Ptr Ribbon::application_menu_overlay() const noexcept {
    return application_overlay_;
}

// Opens the two-pane menu as an overlay covering the window client, its panel
// below the File tab, over the ribbon (owner decision 23).
bool Ribbon::show_application_overlay(MenuOpenMode mode) {
    Window* const window = attached_window();
    if (window == nullptr) return false;
    // Opening, open or closing: the one menu there is serves.
    if (!application_presentation_.is_closed()) return application_presentation_.is_shown();
    close_menus();
    close_group_overlay();
    close_plane_popup();
    if (!application_presentation_.is_closed()) return false;
    const Control::Ptr self = shared_from_this();
    if (!application_overlay_) {
        application_overlay_ =
            make_control<RibbonApplicationMenu>(StableId(std::string(stable_id().value()) + ".application-menu"), *this);
    }
    const Layout placed = layout();
    const Rect absolute = absolute_bounds();
    const Size client = (*window).client_size();
    const Rect file{absolute.x + placed.application.x, absolute.y + placed.application.y, placed.application.width,
                    placed.application.height};
    const bool keyboard = mode == MenuOpenMode::keyboard;
    (*application_overlay_).set_mode(RibbonApplicationMenu::Mode::whole);
    (*application_overlay_).set_requested_bounds({0.0, 0.0, client.width, client.height});
    (*application_overlay_).set_origin({file.x, file.y + file.height});
    (*application_overlay_).reset(keyboard);
    sync_overlay_themes();
    ApplicationOpening opening{};
    opening.lease.overlay = (*window).open_overlay(self, application_overlay_);
    if (Event<>* closed = opening.lease.overlay.closed_event()) {
        opening.lease.overlay_closed =
            (*closed).subscribe(*this, Delegate<>::bind<Ribbon, &Ribbon::on_application_overlay_revoked>(*this));
    }
    opening.owner_moved = (*window).frame_moved().subscribe(
        *this, Delegate<>::bind<Ribbon, &Ribbon::on_application_owner_moved>(*this));
    application_presentation_ = OverlayPresentation<ApplicationOpening>::opening(std::move(opening));
    const FocusScopeId scope = (*window).begin_focus_scope(application_overlay_, application_overlay_);
    // The focus move ran the previous control's handlers; one may have
    // closed the menu, or closed and opened it again (which began its own
    // scope and lift). The scope is kept, and the panel lifted, only for
    // this call's menu still open (A-gf-controls-11-F2).
    if (!is_alive()) return false;
    if (!application_presentation_.settle_open()) {
        end_abandoned_scope(scope);
        return application_presentation_.is_shown();
    }
    (*application_presentation_.open_state()).lease.scope = scope;
    Window* const lift_window = attached_window();
    if (lift_window == nullptr) return true;
    // Where the run has Popup windows the panel shows in the picker Popup
    // below the File tab, reaching past the owner's edges where it must;
    // the overlay above stays as its catcher. Otherwise it stays whole.
    const Size size = (*application_overlay_).panel_size();
    const stxlib::Optional<Rect> face = drop_down_popup_face(*lift_window, file, size, file.x);
    if (face.present()) {
        if (!application_panel_) {
            application_panel_ = make_control<RibbonApplicationMenu>(
                StableId(std::string(stable_id().value()) + ".application-menu.panel"), *this,
                RibbonApplicationMenu::Mode::panel);
        }
        (*application_panel_).set_requested_bounds({0.0, 0.0, ((*face.value())).width, ((*face.value())).height});
        (*application_panel_).reset(keyboard);
        if (application_lift_.lift(*lift_window, application_panel_, (*face.value()), PopupLiftInput::forwarded)) {
            application_face_ = (*face.value());
            (*application_overlay_).set_mode(RibbonApplicationMenu::Mode::catcher);
        }
    }
    invalidate(Dirty::semantics);
    repaint_part(Hit::Kind::application, 0U, {});
    return true;
}

Point Ribbon::application_panel_origin(const Control& panel) const {
    // The lifted panel sits at its face; the whole overlay at its own place
    // in the client.
    const bool lifted = application_lift_.lifted() && &panel == application_panel_.get();
    const Rect origin = lifted ? application_face_ : panel.absolute_bounds();
    const Point point{origin.x, origin.y};
    return point;
}

stxlib::Optional<Rect> Ribbon::application_submenu_face() const noexcept {
    stxlib::Optional<Rect> face{};
    if (!overlay_ || !application_open_) return face;
    face = (*overlay_).panel_face(0U);
    return face;
}

std::uint64_t Ribbon::application_submenu_pointer_moves() const noexcept {
    const std::uint64_t moves = overlay_ ? (*overlay_).pointer_moves_over_rows() : 0U;
    return moves;
}

// The drop-down menu's pointer events outside its panels, while it shows a
// two-pane submenu. Where the two-pane panel shows whole in the client the
// menu's layer covers it: a move over the panel reaches the panel, which
// follows the menu rule with it, and a press or release on the panel is the
// panel's (taken: the submenu stays unless the panel closes it). Otherwise
// a press dismisses the two-pane menu too (as choosing a command does), so
// that a press away from both, the File tab's included, closes every menu,
// and still closes the submenu (false).
bool Ribbon::on_menu_outside_pointer(const PointerEvent& event) {
    if (event.phase != EventPhase::target || !application_open_ || !application_overlay_shown()) return false;
    const bool press = event.action == PointerAction::down && event.button != PointerButton::none;
    const bool release = event.action == PointerAction::up && event.button != PointerButton::none;
    if (!application_lift_.lifted() && application_overlay_) {
        const Control::Ptr panel = application_overlay_;
        const Rect bounds = (*panel).absolute_bounds();
        const Point local{event.position.x - bounds.x, event.position.y - bounds.y};
        RibbonApplicationMenu& menu = *application_overlay_;
        if (event.action == PointerAction::move) {
            menu.pointer_moved_at(local);
            return false;
        }
        if (press && event.button == PointerButton::primary && menu.forwarded_press(local)) return true;
        if (release && event.button == PointerButton::primary && menu.forwarded_release(local)) return true;
    }
    if (press) mark_application_submenu_invoked();
    return false;
}

void Ribbon::mark_application_submenu_invoked() noexcept {
    ApplicationOpening* const shown = application_presentation_.shown_state();
    if (shown != nullptr) (*shown).submenu_invoked = true;
}

bool Ribbon::open_application_submenu(const MenuItemSpec& item, const Control& panel, Rect row, bool keyboard) {
    if (item.children.empty() || !overlay_) return false;
    const Control::Ptr invoker = application_overlay_;
    if (!invoker || attached_window() == nullptr) return false;
    // The row in the owner's client coordinates.
    const Point origin = application_panel_origin(panel);
    const Point at{origin.x + row.x + row.width, origin.y + row.y};
    if ((*overlay_).is_open()) (*overlay_).close();
    (*overlay_).set_items(item.children);
    (*overlay_).show(invoker, at, keyboard ? MenuOpenMode::keyboard : MenuOpenMode::pointer);
    application_open_ = (*overlay_).is_open();
    return application_open_;
}

// The two-pane menu's submenus show in the drop-down menu; it serves them
// only while the two-pane menu is open.
void Ribbon::close_application_submenus() noexcept {
    if (!overlay_ || !(*overlay_).is_open()) return;
    try {
        (*overlay_).close();
    } catch (...) {
    }
}

// The menu reads as closed from the first step; a close or open asked by a
// handler the steps run is refused until it is closed.
void Ribbon::close_application_overlay() noexcept {
    if (!application_presentation_.begin_closing()) return;
    // Its submenus first, deepest first (the menu closes its own chain),
    // then the panel: no submenu outlives the menu that opened it.
    close_application_submenus();
    application_lift_.release();
    ApplicationOpening& closing = *application_presentation_.closing_state();
    closing.submenu_invoked = false;
    Window* const window = attached_window();
    if (window != nullptr && closing.lease.scope) {
        try {
            static_cast<void>((*window).end_focus_scope(closing.lease.scope));
        } catch (...) {
        }
    }
    closing.lease.scope = {};
    closing.lease.overlay_closed.disconnect();
    closing.owner_moved.disconnect();
    closing.lease.overlay.disconnect();
    application_presentation_.finish_closing();
    if (!is_alive()) return;
    try {
        invalidate(Dirty::semantics);
        repaint_part(Hit::Kind::application, 0U, {});
    } catch (...) {
    }
}

void Ribbon::on_application_overlay_revoked() {
    if (!application_presentation_.begin_closing()) return;
    close_application_submenus();
    application_lift_.release();
    ApplicationOpening& closing = *application_presentation_.closing_state();
    closing.submenu_invoked = false;
    closing.lease.scope = {};
    closing.lease.overlay_closed.disconnect();
    closing.owner_moved.disconnect();
    application_presentation_.finish_closing();
    invalidate(Dirty::semantics);
    repaint_part(Hit::Kind::application, 0U, {});
}

// The owner moved under the menu's Popup windows: the menu and its
// submenus close.
void Ribbon::on_application_owner_moved() {
    close_menus();
}

// -- Backstage -----------------------------------------------------------------

// A changed view closes the open Backstage first; the close subscription
// follows the cell.
void Ribbon::prepare_write(const decltype(backstage)&, std::shared_ptr<BackstageView>& incoming) {
    require_mutable();
    if (backstage.read() == incoming) return;
    if (incoming && (*incoming).parent()) throw std::invalid_argument("Ribbon Backstage view already has a parent");
    close_backstage();
    backstage_close_.disconnect();
}

void Ribbon::written(const decltype(backstage)&) {
    if (backstage.read()) {
        backstage_close_ = (*backstage.read()).close_requested().subscribe(
            *this, Delegate<>::bind<Ribbon, &Ribbon::on_backstage_close_requested>(*this));
    }
    invalidate(Dirty::semantics);
}

// Backstage covers the window from the bottom of the ribbon's tab row to the
// client's bottom edge, the full width; the tab row stays usable above it.
void Ribbon::place_backstage() {
    Window* const window = attached_window();
    if (window == nullptr || !backstage.read()) return;
    const Rect absolute = absolute_bounds();
    const Size client = (*window).client_size();
    const double top = std::min(client.height, absolute.y + tab_row_height());
    (*backstage.read()).set_requested_bounds({0.0, top, client.width, std::max(0.0, client.height - top)});
}

bool Ribbon::show_backstage() {
    require_mutable();
    Window* const window = attached_window();
    if (!backstage.read() || window == nullptr || !effectively_enabled()) return false;
    if (backstage_open_) return true;
    close_menus();
    close_group_overlay();
    close_plane_popup();
    place_backstage();
    sync_backstage_theme();
    backstage_token_ = (*window).open_overlay(shared_from_this(), backstage.read());
    backstage_open_ = true;
    if (Event<>* closed = backstage_token_.closed_event()) {
        backstage_revoked_ = (*closed).subscribe(*this, Delegate<>::bind<Ribbon, &Ribbon::on_backstage_revoked>(*this));
    }
    const Control::Ptr self = shared_from_this();
    const FocusScopeId scope = (*window).begin_focus_scope(backstage.read(), backstage.read());
    // The focus move ran the previous control's handlers; one may have
    // closed Backstage (raising backstage_open_changed(false)), or closed
    // and opened it again (which began its own scope and raised true).
    // Neither is followed by this call's true (A-gf-controls-11-F2).
    if (!is_alive()) return false;
    if (!backstage_open_ || backstage_scope_) {
        end_abandoned_scope(scope);
        return backstage_open_;
    }
    backstage_scope_ = scope;
    invalidate(Dirty::paint | Dirty::semantics);
    backstage_open_changed_.emit(true);
    return true;
}

void Ribbon::close_backstage() noexcept {
    if (!backstage_open_) return;
    backstage_open_ = false;
    Window* const window = attached_window();
    if (window != nullptr && backstage_scope_) {
        try {
            static_cast<void>((*window).end_focus_scope(backstage_scope_));
        } catch (...) {
        }
    }
    backstage_scope_ = {};
    backstage_revoked_.disconnect();
    backstage_token_.disconnect();
    release_backstage_theme();
    if (!is_alive()) return;
    try {
        invalidate(Dirty::paint | Dirty::semantics);
        backstage_open_changed_.emit(false);
    } catch (...) {
    }
}

void Ribbon::on_backstage_close_requested() {
    close_backstage();
}

void Ribbon::on_backstage_revoked() {
    if (!backstage_open_) return;
    backstage_open_ = false;
    backstage_scope_ = {};
    backstage_revoked_.disconnect();
    invalidate(Dirty::paint | Dirty::semantics);
    backstage_open_changed_.emit(false);
}

} // namespace gui_forms
