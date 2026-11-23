#include "ribbon_application_menu.hpp"

#include "../menu/menu_utilities.hpp"

#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

// Panel geometry in logical units at text scale 1: a 190-unit command pane
// of 26-unit rows (7-unit separators) beside a 250-unit recent pane of a
// 24-unit caption over 22-unit rows, inside a 4-unit margin. Command labels
// start 28 units in, where a menu row's icon column ends.
constexpr double panel_margin = 4.0;
constexpr double commands_width = 190.0;
constexpr double recent_width = 250.0;
constexpr double command_row = 26.0;
constexpr double separator_row = 7.0;
constexpr double caption_row = 24.0;
constexpr double recent_row = 22.0;
constexpr double label_left = 28.0;
constexpr double recent_label_left = 8.0;
constexpr std::size_t numbered_recent = 9U;

} // namespace

RibbonApplicationMenu::RibbonApplicationMenu(StableId stable_id, Ribbon& ribbon, Mode mode)
    : Control(std::move(stable_id)), ribbon_(&ribbon), mode_(mode) {
    focusable = true;
    set_paint_plane(PaintPlane::control);
}

void RibbonApplicationMenu::set_mode(Mode mode) {
    require_mutable();
    if (mode_ == mode) return;
    mode_ = mode;
    cancel_settle();
    last_pointer_.reset();
    hot_row_.reset();
    focus_row_.reset();
    submenu_row_.reset();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// The panel's size: the taller pane, each pane its fixed width, inside the
// margin.
Size RibbonApplicationMenu::panel_size() const {
    if (!ribbon_alive()) return Size{};
    const Ribbon& ribbon = *ribbon_;
    const double scale = effective_text_scale();
    const double inner = panel_margin * scale;
    double commands_height = 0.0;
    for (const MenuItemSpec& item : ribbon.application_menu_) {
        commands_height += (item.kind == MenuItemKind::separator ? separator_row : command_row) * scale;
    }
    const double recent_height =
        caption_row * scale + static_cast<double>(ribbon.recent_items_.size()) * recent_row * scale;
    const double width = (commands_width + recent_width) * scale + inner * 2.0;
    const double height = std::max(commands_height, recent_height) + inner * 2.0;
    const Size size{width, height};
    return size;
}

void RibbonApplicationMenu::submenu_closed() {
    if (!submenu_row_) return;
    submenu_row_.reset();
    invalidate(Dirty::paint | Dirty::semantics);
}

bool RibbonApplicationMenu::submenu_open() const noexcept {
    const bool result = submenu_row_.has_value() && ribbon_alive() && (*ribbon_).overlay_ &&
                        (*(*ribbon_).overlay_).is_open();
    return result;
}

bool RibbonApplicationMenu::ribbon_alive() const noexcept {
    const bool result = ribbon_ != nullptr && (*ribbon_).is_alive();
    return result;
}

// Opens the submenu of the row at `row` (an index into layout().rows) in the
// ribbon's menu beside the row; the row shows open while it does.
void RibbonApplicationMenu::open_submenu(std::size_t row, bool keyboard) {
    if (!ribbon_alive()) return;
    const Layout placed = layout();
    if (row >= placed.rows.size() || placed.rows[row].recent) return;
    if (submenu_open() && submenu_row_ == row) return;
    const Control::Ptr lifetime = shared_from_this();
    const MenuItemSpec item = (*ribbon_).application_menu_[placed.rows[row].index];
    focus_row_ = row;
    if ((*ribbon_).open_application_submenu(item, *this, placed.rows[row].bounds, keyboard)) submenu_row_ = row;
    invalidate(Dirty::paint | Dirty::semantics);
}

void RibbonApplicationMenu::set_origin(Point origin) {
    require_mutable();
    origin_ = origin;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void RibbonApplicationMenu::reset(bool keyboard) {
    cancel_settle();
    last_pointer_.reset();
    hot_row_.reset();
    focus_row_.reset();
    if (keyboard) move(true);
    invalidate(Dirty::paint | Dirty::semantics);
}

// The panel at the origin, moved left and up only as far as the client
// requires (at 0,0 when lifted; none for a catcher); the command rows down
// its left pane and the caption and recent rows down its right.
RibbonApplicationMenu::Layout RibbonApplicationMenu::layout() const {
    Layout placed;
    if (!ribbon_alive() || mode_ == Mode::catcher) return placed;
    const Ribbon& ribbon = *ribbon_;
    const double scale = effective_text_scale();
    const double inner = panel_margin * scale;
    const Size size = panel_size();
    const double width = size.width;
    const double height = mode_ == Mode::panel ? std::min(size.height, client_rectangle().height) : size.height;
    double x = 0.0;
    double y = 0.0;
    if (mode_ == Mode::whole) {
        const Rect client = client_rectangle();
        x = std::max(0.0, std::min(origin_.x, client.width - width));
        y = std::max(0.0, std::min(origin_.y, client.height - height));
    }
    placed.panel = {x, y, width, height};
    placed.commands = {x + inner, y + inner, commands_width * scale, height - inner * 2.0};
    placed.recent = {x + inner + commands_width * scale, y + inner, recent_width * scale, height - inner * 2.0};
    placed.caption = {placed.recent.x + recent_label_left * scale, placed.recent.y,
                      placed.recent.width - recent_label_left * scale, caption_row * scale};
    double row_y = placed.commands.y;
    for (std::size_t index = 0; index < ribbon.application_menu_.size(); ++index) {
        const bool separator = ribbon.application_menu_[index].kind == MenuItemKind::separator;
        const double row_height = (separator ? separator_row : command_row) * scale;
        placed.rows.push_back(Row{{placed.commands.x, row_y, placed.commands.width, row_height}, false, index, separator});
        row_y += row_height;
    }
    row_y = placed.recent.y + caption_row * scale;
    for (std::size_t index = 0; index < ribbon.recent_items_.size(); ++index) {
        placed.rows.push_back(Row{{placed.recent.x, row_y, placed.recent.width, recent_row * scale}, true, index, false});
        row_y += recent_row * scale;
    }
    return placed;
}

bool RibbonApplicationMenu::row_enabled(const Row& row) const {
    if (row.separator || !ribbon_alive()) return false;
    if (row.recent) return true;
    const MenuItemSpec& item = (*ribbon_).application_menu_[row.index];
    const bool result = !item.command || (*item.command).state().enabled;
    return result;
}

// A command row's text (its own, or its Command's) without the mnemonic
// marker; a recent row's numbered text ("1 Report.txt"), numbered to nine.
std::string RibbonApplicationMenu::row_text(const Row& row) const {
    if (row.separator || !ribbon_alive()) return {};
    if (row.recent) {
        const RibbonRecentItem& item = (*ribbon_).recent_items_[row.index];
        std::string text;
        if (row.index < numbered_recent) text = std::to_string(row.index + 1U) + " ";
        text += item.text;
        return text;
    }
    const MenuItemSpec& item = (*ribbon_).application_menu_[row.index];
    const std::string& authored = item.text.empty() && item.command ? (*item.command).state().text : item.text;
    return parse_mnemonic_text(authored).display_text;
}

std::optional<std::size_t> RibbonApplicationMenu::row_at(const Layout& placed, Point local) const {
    for (std::size_t row = 0; row < placed.rows.size(); ++row) {
        if (placed.rows[row].bounds.contains(local)) return row;
    }
    return std::nullopt;
}

Size RibbonApplicationMenu::measure(Size available) {
    return available;
}

void RibbonApplicationMenu::on_paint(Painter& painter, Rect) {
    if (!ribbon_alive() || mode_ == Mode::catcher) return;
    const Layout placed = layout();
    const Window* const host = attached_window();
    const bool cue = host != nullptr && (*host).focus_cue_visible();
    PartPaint part;
    part.font = effective_font((*ribbon_).font.read());
    part.family = ControlVisualRole::panel;
    part.role = PartRole::shadow;
    part.bounds = placed.panel;
    paint_part(painter, *this, part);
    part.role = PartRole::face;
    paint_part(painter, *this, part);
    part.role = PartRole::divider;
    part.bounds = {placed.recent.x, placed.recent.y, 1.0, placed.recent.height};
    paint_part(painter, *this, part);
    part.role = PartRole::caption;
    part.bounds = placed.caption;
    part.text = lend_paint_text((*ribbon_).recent_caption.read());
    paint_part(painter, *this, part);
    part.text = {};
    const double scale = effective_text_scale();
    for (std::size_t row = 0; row < placed.rows.size(); ++row) {
        const Row& entry = placed.rows[row];
        if (entry.separator) {
            part.family = ControlVisualRole::panel;
            part.states = {};
            part.role = PartRole::divider;
            part.bounds = {entry.bounds.x + label_left * scale, entry.bounds.y + std::floor(entry.bounds.height * 0.5),
                           entry.bounds.width - label_left * scale, 1.0};
            paint_part(painter, *this, part);
            continue;
        }
        AnatomyStateSet states;
        if (!row_enabled(entry)) states.insert(AnatomyState::disabled);
        if (hot_row_ == row && row_enabled(entry)) states.insert(AnatomyState::hot);
        if (focus_row_ == row && cue) states.insert(AnatomyState::focus_visible);
        const bool submenu = !entry.recent && (*ribbon_).application_menu_[entry.index].kind == MenuItemKind::submenu;
        if (submenu && submenu_row_ == row) states.insert(AnatomyState::open);
        part.family = ControlVisualRole::menu_item;
        part.states = states;
        part.role = PartRole::face;
        part.bounds = entry.bounds;
        // A menu item as a drop-down menu's rows are (menu_utilities.hpp):
        // the built-in rule's dithered bulge, rising while the row is hot.
        const std::string_view row_id = entry.recent ? std::string_view((*ribbon_).recent_items_[entry.index].stable_id)
                                                     : std::string_view((*ribbon_).application_menu_[entry.index].stable_id);
        part.control_type = "menu_strip";
        part.key.set(PartKey{"menu_item", row_id, {}});
        paint_part(painter, *this, part);
        part.control_type = {};
        part.key.reset();
        const double left = (entry.recent ? recent_label_left : label_left) * scale;
        const std::string label = row_text(entry);
        part.role = PartRole::label;
        part.bounds = {entry.bounds.x + left, entry.bounds.y, std::max(0.0, entry.bounds.width - left), entry.bounds.height};
        part.text = lend_paint_text(label);
        paint_part(painter, *this, part);
        part.text = {};
        if (submenu) {
            // The submenu chevron, as a drop-down menu's submenu rows draw it.
            PartPaint chevron;
            chevron.family = ControlVisualRole::menu_item;
            chevron.role = PartRole::disclosure;
            chevron.mark = PartMark::chevron_right;
            chevron.bounds = {entry.bounds.x + entry.bounds.width - 18.0 * scale,
                              entry.bounds.y + std::floor((entry.bounds.height - 8.0 * scale) * 0.5), 8.0 * scale,
                              8.0 * scale};
            chevron.states = states;
            paint_part(painter, *this, chevron);
        }
        part.role = PartRole::focus;
        part.bounds = entry.bounds;
        paint_part(painter, *this, part);
    }
}

// Activating a row closes the menu first, then runs the command (with the
// application menu's event) or reports the recent item.
void RibbonApplicationMenu::activate(const Row& row) {
    if (!ribbon_alive() || !row_enabled(row)) return;
    Ribbon& ribbon = *ribbon_;
    if (!row.recent && ribbon.application_menu_[row.index].kind == MenuItemKind::submenu) {
        const Layout placed = layout();
        for (std::size_t index = 0; index < placed.rows.size(); ++index) {
            if (placed.rows[index].recent == row.recent && placed.rows[index].index == row.index) {
                open_submenu(index, true);
            }
        }
        return;
    }
    const Control::Ptr lifetime = shared_from_this();
    const Control::Ptr owner = ribbon.shared_from_this();
    // Copies taken before closing: the control that gets focus back may
    // replace either list, and running the command may replace the
    // application menu (A-gf-controls-11-F7).
    std::string recent_id;
    MenuItemSpec item;
    if (row.recent) {
        recent_id = ribbon.recent_items_[row.index].stable_id;
    } else {
        item = ribbon.application_menu_[row.index];
    }
    // Closes a submenu of this menu too.
    ribbon.close_menus();
    if (!ribbon.is_alive()) return;
    const std::string ribbon_id(ribbon.stable_id().value());
    if (row.recent) {
        const RibbonRecentItemInvocation invoked{ribbon_id, recent_id};
        ribbon.recent_item_invoked_.emit(invoked);
        return;
    }
    std::string command_id;
    if (item.command) {
        command_id = (*item.command).stable_id();
        static_cast<void>((*item.command).execute(ribbon_id + ".menu"));
    }
    if (!ribbon.is_alive()) return;
    const MenuItemInvocation invocation{ribbon_id + ".menu", item.stable_id, command_id, ribbon_id};
    ribbon.application_menu_item_invoked_.emit(invocation);
}

void RibbonApplicationMenu::set_hot_row(std::optional<std::size_t> hot) {
    if (hot_row_ != hot) {
        hot_row_ = hot;
        invalidate(Dirty::paint);
    }
}

// The panel's local coordinates: the whole overlay covers the client from
// 0,0 (its points are the window's); the lifted panel sits in its Popup
// inside the shadow's room.
Point RibbonApplicationMenu::local_point(const MouseEventArgs& arguments) const {
    const Rect bounds = absolute_bounds();
    const Point local{arguments.window_position.x - bounds.x, arguments.window_position.y - bounds.y};
    return local;
}

// The hot row follows the pointer's moves (the move that enters the menu
// sets it), and the menu rule runs.
void RibbonApplicationMenu::on_mouse_move(MouseEventArgs& arguments) {
    if (ribbon_alive() && mode_ != Mode::catcher) pointer_moved_at(local_point(arguments));
    Control::on_mouse_move(arguments);
}

// Leaving the panel leaves an open submenu as it is (as Windows, macOS and
// GTK menus do) and cancels a wait: the pointer may have reached the
// submenu.
void RibbonApplicationMenu::on_mouse_leave() {
    if (ribbon_alive() && mode_ != Mode::catcher) set_hot_row(std::nullopt);
    cancel_settle();
    last_pointer_.reset();
    Control::on_mouse_leave();
}

void RibbonApplicationMenu::on_mouse_down(MouseEventArgs& arguments) {
    if (ribbon_alive() && arguments.button == PointerButton::primary) {
        arguments.handled = true;
        press(local_point(arguments));
    }
    Control::on_mouse_down(arguments);
}

void RibbonApplicationMenu::on_mouse_up(MouseEventArgs& arguments) {
    if (ribbon_alive() && mode_ != Mode::catcher && arguments.button == PointerButton::primary) {
        arguments.handled = true;
        release(local_point(arguments));
    }
    Control::on_mouse_up(arguments);
}

void RibbonApplicationMenu::press(Point local) {
    const Layout placed = layout();
    // A press outside the panel closes the menu, as a click away does; the
    // catcher closes it on any press.
    if (mode_ == Mode::catcher || !placed.panel.contains(local)) {
        const Control::Ptr lifetime = shared_from_this();
        (*ribbon_).close_menus();
        return;
    }
    // A press on a submenu row opens its submenu at once.
    const std::optional<std::size_t> hit = row_at(placed, local);
    if (hit && !placed.rows[*hit].recent &&
        (*ribbon_).application_menu_[placed.rows[*hit].index].kind == MenuItemKind::submenu) {
        cancel_settle();
        if (row_enabled(placed.rows[*hit])) open_submenu(*hit, false);
    }
}

void RibbonApplicationMenu::release(Point local) {
    const Layout placed = layout();
    const std::optional<std::size_t> hit = row_at(placed, local);
    // A click is a release over a row, as in a menu.
    if (hit) activate(placed.rows[*hit]);
}

bool RibbonApplicationMenu::forwarded_press(Point local) {
    if (!ribbon_alive() || mode_ == Mode::catcher || !layout().panel.contains(local)) return false;
    press(local);
    return true;
}

bool RibbonApplicationMenu::forwarded_release(Point local) {
    if (!ribbon_alive() || mode_ == Mode::catcher || !layout().panel.contains(local)) return false;
    release(local);
    return true;
}

// -- The menu rule (menu_utilities.hpp) -------------------------------------
//
// Event order: each move over a row either cancels the wait (the pointer is
// back on the open submenu's row, or its row opens nothing and no submenu
// shows) or (re)starts it for the row it is on; a move toward the open
// submenu across other rows (the safe triangle) leaves the hot row as it
// is and restarts the wait from the row it ends on, for at least
// minimum_aim_rest. The timer then settles on that row, on the UI thread,
// unless the wait was replaced or cancelled (the serial) or the pointer
// reached the submenu meanwhile (the ribbon's menu's move count).

void RibbonApplicationMenu::SettleElapsed::operator()(FrameTime) const {
    const Control::Ptr retained = menu.lock();
    if (!retained) return;
    RibbonApplicationMenu& panel = static_cast<RibbonApplicationMenu&>(*retained);
    panel.settle_elapsed(serial);
}

std::chrono::milliseconds RibbonApplicationMenu::menu_show_delay() const {
    const Window* const host = ribbon_alive() ? (*ribbon_).attached_window() : nullptr;
    if (host == nullptr) return std::chrono::milliseconds(0);
    const std::uint32_t delay = (*host).presentation_settings().pointer.menu_show_milliseconds;
    return std::chrono::milliseconds(delay);
}

bool RibbonApplicationMenu::opens_submenu(const Row& row) const {
    if (row.recent || row.separator || !ribbon_alive()) return false;
    const MenuItemSpec& item = (*ribbon_).application_menu_[row.index];
    const bool opens = item.kind == MenuItemKind::submenu && !item.children.empty() && row_enabled(row);
    return opens;
}

bool RibbonApplicationMenu::aiming_at_submenu(const Layout& placed, Point from, Point to) const {
    const stxlib::Optional<Rect> child = (*ribbon_).application_submenu_face();
    if (child.absent()) return false;
    const Point origin = (*ribbon_).application_panel_origin(*this);
    const Rect parent{origin.x + placed.panel.x, origin.y + placed.panel.y, placed.panel.width, placed.panel.height};
    const bool aiming = menu_detail::aiming_at_submenu_face(parent, *child.value(), from, to);
    return aiming;
}

void RibbonApplicationMenu::cancel_settle() noexcept {
    settle_timer_.disconnect();
    settle_row_.reset();
}

void RibbonApplicationMenu::schedule_settle(std::size_t row, std::chrono::milliseconds delay) {
    cancel_settle();
    Window* const host = (*ribbon_).attached_window();
    if (host == nullptr) return;
    ++settle_serial_;
    settle_row_ = row;
    settle_moves_ = (*ribbon_).application_submenu_pointer_moves();
    const std::weak_ptr<Control> observed = shared_from_this();
    settle_timer_ = (*host).schedule_ui_timer(*this, menu_detail::menu_settle_interval, FrameClock::now() + delay,
                                              SettleElapsed{observed, settle_serial_});
}

void RibbonApplicationMenu::settle_elapsed(std::uint64_t serial) {
    if (serial != settle_serial_ || !settle_row_ || !ribbon_alive()) return;
    const std::size_t row = *settle_row_;
    const bool reached_submenu = submenu_open() && (*ribbon_).application_submenu_pointer_moves() != settle_moves_;
    cancel_settle();
    // The pointer reached the submenu meanwhile: the submenu stays.
    if (reached_submenu) return;
    settle_on(row);
}

void RibbonApplicationMenu::settle_on(std::size_t row) {
    const Layout placed = layout();
    if (row >= placed.rows.size()) return;
    if (submenu_open() && submenu_row_ == row) return;
    if (opens_submenu(placed.rows[row])) {
        open_submenu(row, false);
        return;
    }
    if (!submenu_open()) return;
    const Control::Ptr lifetime = shared_from_this();
    (*ribbon_).close_application_submenus();
}

void RibbonApplicationMenu::pointer_moved_at(Point local) {
    if (!ribbon_alive() || mode_ == Mode::catcher) return;
    const Layout placed = layout();
    const std::optional<std::size_t> hit = row_at(placed, local);
    const Point origin = (*ribbon_).application_panel_origin(*this);
    const Point at{origin.x + local.x, origin.y + local.y};
    const std::uint64_t moves = (*ribbon_).application_submenu_pointer_moves();
    // The start of the move, unless the pointer was over the submenu since.
    std::optional<Point> from{};
    if (last_pointer_ && last_pointer_moves_ == moves) from = last_pointer_;
    last_pointer_ = at;
    last_pointer_moves_ = moves;
    // Off the rows (the margin, the caption): the rows stay as they are.
    if (!hit) {
        set_hot_row(std::nullopt);
        cancel_settle();
        return;
    }
    const bool shown = submenu_open();
    const bool on_source = shown && submenu_row_ == *hit;
    const std::chrono::milliseconds delay = menu_show_delay();
    if (shown && !on_source && from && aiming_at_submenu(placed, *from, at)) {
        schedule_settle(*hit, std::max(delay, menu_detail::minimum_aim_rest));
        return;
    }
    set_hot_row(hit);
    if (on_source) {
        cancel_settle();
        return;
    }
    const bool opens = opens_submenu(placed.rows[*hit]);
    if (!opens && !shown) {
        cancel_settle();
        return;
    }
    if (delay.count() == 0) {
        cancel_settle();
        settle_on(*hit);
        return;
    }
    // The wait runs from the move that entered the row.
    if (settle_row_ == hit) return;
    schedule_settle(*hit, delay);
}

// Moves the keyboard to the next enabled row of the same pane (wrapping).
void RibbonApplicationMenu::move(bool forward) {
    const Layout placed = layout();
    if (placed.rows.empty()) return;
    const bool recent = focus_row_ ? placed.rows[*focus_row_].recent : false;
    const std::size_t count = placed.rows.size();
    std::size_t row = focus_row_.value_or(forward ? count - 1U : 0U);
    for (std::size_t step = 0; step < count; ++step) {
        row = forward ? (row + 1U) % count : (row + count - 1U) % count;
        if (placed.rows[row].recent != recent || !row_enabled(placed.rows[row])) continue;
        focus_row_ = row;
        invalidate(Dirty::paint | Dirty::semantics);
        return;
    }
}

// Moves the keyboard to the other pane's first enabled row.
void RibbonApplicationMenu::switch_pane() {
    const Layout placed = layout();
    const bool recent = focus_row_ ? placed.rows[*focus_row_].recent : false;
    for (std::size_t row = 0; row < placed.rows.size(); ++row) {
        if (placed.rows[row].recent == recent || !row_enabled(placed.rows[row])) continue;
        focus_row_ = row;
        invalidate(Dirty::paint | Dirty::semantics);
        return;
    }
}

void RibbonApplicationMenu::on_key_preview(KeyEvent& event) {
    if (event.action != KeyAction::down || event.physical_key != PhysicalKey::escape || !ribbon_alive()) return;
    if (submenu_open()) return;
    event.handled = true;
    const Control::Ptr lifetime = shared_from_this();
    (*ribbon_).close_application_overlay();
}

// Up and Down move within a pane, Left and Right between the panes, Enter
// and Space activate; Escape closes (on_key_preview).
void RibbonApplicationMenu::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    if (arguments.handled || !ribbon_alive()) return;
    // The keyboard takes over: a row the pointer rested on no longer opens
    // or closes a submenu when its delay elapses.
    cancel_settle();
    if (submenu_open()) return;
    const std::uint32_t key = arguments.physical_key;
    if (key == PhysicalKey::escape) {
        arguments.handled = true;
        const Control::Ptr lifetime = shared_from_this();
        (*ribbon_).close_application_overlay();
        return;
    }
    if (key == PhysicalKey::right && focus_row_) {
        const Layout placed = layout();
        const Row& row = placed.rows[*focus_row_];
        if (!row.recent && (*ribbon_).application_menu_[row.index].kind == MenuItemKind::submenu) {
            arguments.handled = true;
            open_submenu(*focus_row_, true);
            return;
        }
    }
    if (key == PhysicalKey::down || (key == PhysicalKey::tab && !has_modifier(arguments.modifiers, Modifier::shift))) {
        move(true);
    } else if (key == PhysicalKey::up || key == PhysicalKey::tab) {
        move(false);
    } else if (key == PhysicalKey::left || key == PhysicalKey::right) {
        switch_pane();
    } else if ((key == PhysicalKey::enter || key == PhysicalKey::space) && focus_row_) {
        arguments.handled = true;
        const Layout placed = layout();
        activate(placed.rows[*focus_row_]);
        return;
    } else {
        return;
    }
    arguments.handled = true;
}

// The whole client takes presses (so that a press away closes the menu),
// except the File tab, which the ribbon handles.
bool RibbonApplicationMenu::hit_test_local(Point local_point) const {
    if (!ribbon_alive()) return false;
    if (mode_ == Mode::panel) return layout().panel.contains(local_point);
    const Ribbon& ribbon = *ribbon_;
    const ScaleFrame ribbon_bounds_frame = ribbon.window_frame();
    const Rect file = ribbon.layout().application;
    const Rect window_file = frame_to_outer(ribbon_bounds_frame, file);
    return !window_file.contains(local_point);
}

Insets RibbonApplicationMenu::visual_outsets() const noexcept {
    if (mode_ == Mode::catcher) return Insets{};
    const Insets result = overlay_shadow_outsets(*this);
    return result;
}

SemanticDescriptor RibbonApplicationMenu::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::menu;
    descriptor.name = "Application menu";
    descriptor.actions = {SemanticAction::focus};
    // A catcher's panel publishes from its Popup.
    descriptor.exposed = mode_ != Mode::catcher;
    return descriptor;
}

// The rows: commands as menu items, then the recent items under their
// caption, each positioned within its pane.
std::vector<SemanticNode> RibbonApplicationMenu::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes;
    if (!ribbon_alive()) return nodes;
    const Ribbon& ribbon = *ribbon_;
    const Layout placed = layout();
    const std::string menu_id = std::string(ribbon.stable_id().value()) + ".application-menu";
    std::size_t commands = 0U;
    std::size_t recent = 0U;
    for (const Row& row : placed.rows) {
        if (row.separator) continue;
        if (row.recent) {
            ++recent;
        } else {
            ++commands;
        }
    }
    std::size_t command_position = 0U;
    std::size_t recent_position = 0U;
    for (std::size_t index = 0; index < placed.rows.size(); ++index) {
        const Row& row = placed.rows[index];
        if (row.separator) continue;
        SemanticNode node{};
        node.role = SemanticRole::menu_item;
        node.name = row_text(row);
        if (row.recent) {
            const RibbonRecentItem& item = ribbon.recent_items_[row.index];
            node.stable_id = menu_id + ".recent." + item.stable_id;
            node.description = item.description.empty() ? ribbon.recent_caption.read() : item.description;
            node.position_in_set.set(++recent_position);
            node.set_size.set(recent);
        } else {
            node.stable_id = menu_id + ".item." + ribbon.application_menu_[row.index].stable_id;
            node.position_in_set.set(++command_position);
            node.set_size.set(commands);
        }
        node.bounds = row.bounds;
        node.states = SemanticState::visible | SemanticState::focusable;
        if (row_enabled(row)) node.states |= SemanticState::enabled;
        if (focus_row_ == index) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::press};
        if (!row.recent && ribbon.application_menu_[row.index].kind == MenuItemKind::submenu) {
            node.has_popup = true;
            if (submenu_row_ == index) node.states |= SemanticState::expanded;
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool RibbonApplicationMenu::on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                                     std::string_view) {
    if (action != SemanticAction::press || !ribbon_alive()) return false;
    const Layout placed = layout();
    const std::vector<SemanticNode> nodes = semantic_virtual_children();
    std::size_t node = 0U;
    for (const Row& row : placed.rows) {
        if (row.separator) continue;
        if (nodes[node].stable_id == stable_id) {
            const Control::Ptr lifetime = shared_from_this();
            activate(row);
            return true;
        }
        ++node;
    }
    return false;
}

} // namespace gui_forms
