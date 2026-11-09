#include "ribbon_application_menu.hpp"

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

RibbonApplicationMenu::RibbonApplicationMenu(StableId stable_id, Ribbon& ribbon)
    : Control(std::move(stable_id)), ribbon_(&ribbon) {
    focusable = true;
    set_paint_plane(PaintPlane::control);
}

bool RibbonApplicationMenu::ribbon_alive() const noexcept {
    const bool result = ribbon_ != nullptr && (*ribbon_).is_alive();
    return result;
}

void RibbonApplicationMenu::set_origin(Point origin) {
    require_mutable();
    origin_ = origin;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void RibbonApplicationMenu::reset(bool keyboard) {
    hot_row_.reset();
    focus_row_.reset();
    if (keyboard) move(true);
    invalidate(Dirty::paint | Dirty::semantics);
}

// The panel at the origin, moved left and up only as far as the client
// requires; the command rows down its left pane and the caption and recent
// rows down its right.
RibbonApplicationMenu::Layout RibbonApplicationMenu::layout() const {
    Layout placed;
    if (!ribbon_alive()) return placed;
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
    const Rect client = client_rectangle();
    const double x = std::max(0.0, std::min(origin_.x, client.width - width));
    const double y = std::max(0.0, std::min(origin_.y, client.height - height));
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
    if (!ribbon_alive()) return;
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
    part.text = (*ribbon_).recent_caption.read();
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
        part.family = ControlVisualRole::menu_item;
        part.states = states;
        part.role = PartRole::face;
        part.bounds = entry.bounds;
        // A menu item as a drop-down menu's rows are (menu_utilities.hpp):
        // the built-in rule's dithered bulge, rising while the row is hot.
        const std::string_view row_id = entry.recent ? std::string_view((*ribbon_).recent_items_[entry.index].stable_id)
                                                     : std::string_view((*ribbon_).application_menu_[entry.index].stable_id);
        part.control_type = "menu_strip";
        part.key = PartKey{"menu_item", row_id, {}};
        paint_part(painter, *this, part);
        part.control_type = {};
        part.key.reset();
        const double left = (entry.recent ? recent_label_left : label_left) * scale;
        const std::string label = row_text(entry);
        part.role = PartRole::label;
        part.bounds = {entry.bounds.x + left, entry.bounds.y, std::max(0.0, entry.bounds.width - left), entry.bounds.height};
        part.text = label;
        paint_part(painter, *this, part);
        part.text = {};
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
    const Control::Ptr lifetime = shared_from_this();
    const Control::Ptr owner = ribbon.shared_from_this();
    ribbon.close_application_overlay();
    const std::string ribbon_id(ribbon.stable_id().value());
    if (row.recent) {
        const RibbonRecentItemInvocation invoked{ribbon_id, ribbon.recent_items_[row.index].stable_id};
        ribbon.recent_item_invoked_.emit(invoked);
        return;
    }
    // A copy: running the command may replace the application menu.
    const MenuItemSpec item = ribbon.application_menu_[row.index];
    std::string command_id;
    if (item.command) {
        command_id = (*item.command).stable_id();
        static_cast<void>((*item.command).execute(ribbon_id + ".menu"));
    }
    if (!ribbon.is_alive()) return;
    const MenuItemInvocation invocation{ribbon_id + ".menu", item.stable_id, command_id, ribbon_id};
    ribbon.application_menu_item_invoked_.emit(invocation);
}

void RibbonApplicationMenu::on_pointer(PointerEvent& event) {
    if (!ribbon_alive()) return;
    const Layout placed = layout();
    const Point local{event.position.x, event.position.y};
    const std::optional<std::size_t> hit = row_at(placed, local);
    if (event.action == PointerAction::leave || event.action == PointerAction::move ||
        event.action == PointerAction::enter) {
        const std::optional<std::size_t> hot = event.action == PointerAction::leave ? std::nullopt : hit;
        if (hot_row_ != hot) {
            hot_row_ = hot;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.button != PointerButton::primary) return;
    event.handled = true;
    // A press outside the panel closes the menu, as a click away does.
    if (event.action == PointerAction::down && !placed.panel.contains(local)) {
        const Control::Ptr lifetime = shared_from_this();
        (*ribbon_).close_application_overlay();
        return;
    }
    // A click is a release over a row, as in a menu.
    if (event.action == PointerAction::up && hit) activate(placed.rows[*hit]);
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
    event.handled = true;
    const Control::Ptr lifetime = shared_from_this();
    (*ribbon_).close_application_overlay();
}

// Up and Down move within a pane, Left and Right between the panes, Enter
// and Space activate; Escape closes (on_key_preview).
void RibbonApplicationMenu::on_key(KeyEvent& event) {
    if (event.action != KeyAction::down || !ribbon_alive()) return;
    const std::uint32_t key = event.physical_key;
    if (key == PhysicalKey::down || (key == PhysicalKey::tab && !has_modifier(event.modifiers, Modifier::shift))) {
        move(true);
    } else if (key == PhysicalKey::up || key == PhysicalKey::tab) {
        move(false);
    } else if (key == PhysicalKey::left || key == PhysicalKey::right) {
        switch_pane();
    } else if ((key == PhysicalKey::enter || key == PhysicalKey::space) && focus_row_) {
        event.handled = true;
        const Layout placed = layout();
        activate(placed.rows[*focus_row_]);
        return;
    } else {
        return;
    }
    event.handled = true;
}

// The whole client takes presses (so that a press away closes the menu),
// except the File tab, which the ribbon handles.
bool RibbonApplicationMenu::hit_test_local(Point local_point) const {
    if (!ribbon_alive()) return false;
    const Ribbon& ribbon = *ribbon_;
    const Rect ribbon_bounds = ribbon.absolute_bounds();
    const Rect file = ribbon.layout().application;
    const Rect window_file{ribbon_bounds.x + file.x, ribbon_bounds.y + file.y, file.width, file.height};
    return !window_file.contains(local_point);
}

Insets RibbonApplicationMenu::visual_outsets() const noexcept {
    const Insets result = overlay_shadow_outsets(*this);
    return result;
}

SemanticDescriptor RibbonApplicationMenu::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::menu;
    descriptor.name = "Application menu";
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
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
            node.position_in_set = ++recent_position;
            node.set_size = recent;
        } else {
            node.stable_id = menu_id + ".item." + ribbon.application_menu_[row.index].stable_id;
            node.position_in_set = ++command_position;
            node.set_size = commands;
        }
        node.bounds = row.bounds;
        node.states = SemanticState::visible | SemanticState::focusable;
        if (row_enabled(row)) node.states |= SemanticState::enabled;
        if (focus_row_ == index) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::press};
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
