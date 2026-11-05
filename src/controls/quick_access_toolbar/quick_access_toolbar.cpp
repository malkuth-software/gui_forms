#include "gui_forms/controls/quick_access_toolbar/quick_access_toolbar.hpp"

#include "../scrollable_control/scrolling_utilities.hpp"

#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gui_forms {
namespace {

// The customize button's width along the strip and its glyph's side, in
// logical units at text scale 1 (ToolStrip's 12-unit overflow button and
// 7-unit arrow glyph, so the two end buttons match).
constexpr double customize_button_width = 13.0;
constexpr double customize_glyph_size = 7.0;

// A square glyph of `side` centred in `area`, on whole units.
Rect centred_glyph(Rect area, double side) noexcept {
    if (area.width < side || area.height < side) return {};
    const Rect glyph{std::floor(area.x + (area.width - side) * 0.5),
                     std::floor(area.y + (area.height - side) * 0.5), side, side};
    return glyph;
}

std::string placement_row_text(QuickAccessToolbarPlacement placement) {
    if (placement == QuickAccessToolbarPlacement::above_ribbon) return "Show Below the Ribbon";
    return "Show Above the Ribbon";
}

} // namespace

QuickAccessToolbar::QuickAccessToolbar(StableId stable_id) : ToolStrip(std::move(stable_id)) {
    set_grip_visible(false);
    set_end_reserve(customize_button_width);
    const std::string prefix((*this).stable_id().value());
    menu_ = std::make_unique<ContextMenu>(prefix + ".customize.menu");
    menu_invoked_ = (*menu_).item_invoked().subscribe(
        *this, Delegate<const MenuItemInvocation&>::bind<QuickAccessToolbar, &QuickAccessToolbar::on_menu_invoked>(
                   *this));
    menu_changed_ = (*menu_).open_changed().subscribe(
        *this, Delegate<bool>::bind<QuickAccessToolbar, &QuickAccessToolbar::on_menu_open_changed>(*this));
    placement_command_ = std::make_shared<Command>(prefix + ".customize.placement",
                                                   placement_row_text(placement_));
    rebuild_menu();
}

QuickAccessToolbar::~QuickAccessToolbar() {
    close_customize_menu();
}

void QuickAccessToolbar::on_dispose() noexcept {
    close_customize_menu();
    if (menu_) (*menu_).dispose();
    ToolStrip::on_dispose();
}

// -- commands ------------------------------------------------------------------

void QuickAccessToolbar::validate_items(const std::vector<ToolStripItemSpec>& items) const {
    for (const ToolStripItemSpec& item : items) {
        if (item.kind != ToolStripItemKind::button || !item.command || !item.drop_down.empty() ||
            item.check_on_click) {
            throw std::invalid_argument("A QuickAccessToolbar shows command buttons only");
        }
    }
}

void QuickAccessToolbar::set_commands(std::vector<QuickAccessToolbarCommand> commands) {
    require_mutable();
    if (commands.size() > maximum_commands) {
        throw std::invalid_argument("A QuickAccessToolbar declares at most 20 commands");
    }
    std::unordered_set<std::string> ids{};
    for (const QuickAccessToolbarCommand& declared : commands) {
        // Toolbar items and customize rows carry the command ID in a
        // StableId, so it follows the StableId segment rule.
        if (!declared.command || !valid_stable_id((*declared.command).stable_id()) ||
            !ids.insert((*declared.command).stable_id()).second) {
            throw std::invalid_argument(
                "QuickAccessToolbar commands require unique command IDs of '.'-separated lowercase dash "
                "segments");
        }
    }
    close_customize_menu();
    commands_ = std::move(commands);
    shown_ids_.clear();
    for (const QuickAccessToolbarCommand& declared : commands_) {
        if (declared.shown) shown_ids_.push_back((*declared.command).stable_id());
    }
    // Each declared command gets a private Command for its menu row.
    row_commands_.clear();
    row_commands_.reserve(commands_.size());
    for (const QuickAccessToolbarCommand& declared : commands_) {
        const std::string& id = (*declared.command).stable_id();
        std::shared_ptr<Command> row = std::make_shared<Command>(customize_row_id(id), (*declared.command).state().text);
        row_commands_.push_back(std::move(row));
    }
    rebuild_items();
    rebuild_menu();
}

void QuickAccessToolbar::set_shown_command_ids(std::vector<std::string> ids) {
    require_mutable();
    if (ids.size() > maximum_commands) {
        throw std::invalid_argument("A QuickAccessToolbar shows at most 20 commands");
    }
    std::unordered_set<std::string> seen{};
    for (const std::string& id : ids) {
        if (id.empty() || !validate_utf8(id).valid() || !seen.insert(id).second) {
            throw std::invalid_argument("QuickAccessToolbar command IDs must be unique, non-empty, valid UTF-8");
        }
    }
    close_customize_menu();
    shown_ids_ = std::move(ids);
    rebuild_items();
    rebuild_menu();
}

std::size_t QuickAccessToolbar::declared_index(std::string_view command_id) const {
    for (std::size_t index = 0; index < commands_.size(); ++index) {
        if ((*commands_[index].command).stable_id() == command_id) return index;
    }
    return commands_.size();
}

std::string QuickAccessToolbar::customize_row_id(std::string_view command_id) const {
    std::string result(stable_id().value());
    result += ".customize.";
    result += command_id;
    return result;
}

bool QuickAccessToolbar::command_shown(std::string_view command_id) const {
    if (declared_index(command_id) == commands_.size()) {
        throw std::out_of_range("QuickAccessToolbar has no such command");
    }
    const bool result = std::find(shown_ids_.begin(), shown_ids_.end(), command_id) != shown_ids_.end();
    return result;
}

void QuickAccessToolbar::set_command_shown(std::string_view command_id, bool shown) {
    require_mutable();
    if (declared_index(command_id) == commands_.size()) {
        throw std::out_of_range("QuickAccessToolbar has no such command");
    }
    const std::vector<std::string>::iterator found = std::find(shown_ids_.begin(), shown_ids_.end(), command_id);
    const bool was_shown = found != shown_ids_.end();
    if (was_shown == shown) return;
    if (shown) {
        if (shown_ids_.size() >= maximum_commands) {
            throw std::invalid_argument("A QuickAccessToolbar shows at most 20 commands");
        }
        shown_ids_.emplace_back(command_id);
    } else {
        shown_ids_.erase(found);
    }
    rebuild_items();
    rebuild_menu();
    const QuickAccessToolbarCustomization change{std::string(stable_id().value()), std::string(command_id), shown};
    customized_.emit(change);
}

void QuickAccessToolbar::rebuild_items() {
    std::vector<ToolStripItemSpec> items{};
    items.reserve(shown_ids_.size());
    for (const std::string& id : shown_ids_) {
        const std::size_t index = declared_index(id);
        // An ID that names no declared command is kept but shows nothing.
        if (index == commands_.size()) continue;
        const QuickAccessToolbarCommand& declared = commands_[index];
        ToolStripItemSpec item{};
        item.stable_id = id;
        item.kind = ToolStripItemKind::button;
        item.command = declared.command;
        item.image = declared.image;
        item.display = declared.image.value != 0U ? ToolStripItemDisplay::image : ToolStripItemDisplay::text;
        items.push_back(std::move(item));
    }
    set_items(std::move(items));
}

void QuickAccessToolbar::rebuild_menu() {
    menu_items_.clear();
    menu_items_.reserve(commands_.size() + 2U);
    for (std::size_t index = 0; index < commands_.size(); ++index) {
        const Command& declared = *commands_[index].command;
        Command& row = *row_commands_[index];
        row.set_text(declared.state().text);
        row.set_checked(command_shown(declared.stable_id()));
        menu_items_.emplace_back(row.stable_id(), MenuItemKind::check, row_commands_[index]);
    }
    if (!commands_.empty()) {
        menu_items_.emplace_back(std::string(stable_id().value()) + ".customize.separator", MenuItemKind::separator);
    }
    (*placement_command_).set_text(placement_row_text(placement_));
    menu_items_.emplace_back((*placement_command_).stable_id(), MenuItemKind::command, placement_command_);
}

const std::vector<MenuItemSpec>& QuickAccessToolbar::customize_menu_items() const noexcept {
    return menu_items_;
}

// -- placement -----------------------------------------------------------------

void QuickAccessToolbar::set_placement(QuickAccessToolbarPlacement placement) {
    require_mutable();
    if (placement != QuickAccessToolbarPlacement::above_ribbon &&
        placement != QuickAccessToolbarPlacement::below_ribbon) {
        throw std::invalid_argument("QuickAccessToolbar placement is invalid");
    }
    if (placement_ == placement) return;
    placement_ = placement;
    rebuild_menu();
    const QuickAccessToolbarPlacementChange change{std::string(stable_id().value()), placement_};
    placement_changed_.emit(change);
}

// -- customize menu ------------------------------------------------------------

QuickAccessToolbar::CustomizeGeometry QuickAccessToolbar::customize_geometry() const {
    CustomizeGeometry result{};
    result.button = layout().end_reserve;
    result.glyph = centred_glyph(result.button, customize_glyph_size * effective_text_scale());
    return result;
}

bool QuickAccessToolbar::customize_at(Point window_point) const {
    const Rect absolute = absolute_bounds();
    const Rect button = customize_geometry().button;
    const bool result = !button.empty() &&
        button.contains(Point{window_point.x - absolute.x, window_point.y - absolute.y});
    return result;
}

bool QuickAccessToolbar::show_customize_menu(MenuOpenMode mode) {
    require_mutable();
    if (mode != MenuOpenMode::keyboard && mode != MenuOpenMode::pointer) {
        throw std::invalid_argument("QuickAccessToolbar opening mode is invalid");
    }
    const Rect button = customize_geometry().button;
    if (button.empty() || !attached_window()) return false;
    if ((*menu_).is_open()) return true;
    close_drop_down();
    rebuild_menu();
    (*menu_).set_items(menu_items_);
    const Rect absolute = absolute_bounds();
    const Point anchor{absolute.x + button.x, absolute.y + button.y + button.height};
    (*menu_).show(shared_from_this(), anchor, mode);
    customize_open_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

void QuickAccessToolbar::close_customize_menu() noexcept {
    if (menu_) (*menu_).close();
}

bool QuickAccessToolbar::customize_menu_open() const noexcept {
    const bool result = menu_ && (*menu_).is_open();
    return result;
}

void QuickAccessToolbar::on_menu_open_changed(bool open) {
    if (open) return;
    customize_open_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
}

void QuickAccessToolbar::on_menu_invoked(const MenuItemInvocation& invocation) {
    if (invocation.item_id == (*placement_command_).stable_id()) {
        const QuickAccessToolbarPlacement next = placement_ == QuickAccessToolbarPlacement::above_ribbon
                                                     ? QuickAccessToolbarPlacement::below_ribbon
                                                     : QuickAccessToolbarPlacement::above_ribbon;
        set_placement(next);
        return;
    }
    for (const QuickAccessToolbarCommand& declared : commands_) {
        const std::string& id = (*declared.command).stable_id();
        if (customize_row_id(id) != invocation.item_id) continue;
        set_command_shown(id, !command_shown(id));
        return;
    }
}

// -- paint and input -----------------------------------------------------------

void QuickAccessToolbar::on_paint(Painter& painter, Rect local_damage) {
    ToolStrip::on_paint(painter, local_damage);
    const CustomizeGeometry geometry = customize_geometry();
    if (geometry.button.empty()) return;
    PartPaint customize;
    customize.family = ControlVisualRole::menu_item;
    customize.role = PartRole::actuator;
    customize.bounds = geometry.button;
    if (customize_hot_) customize.states.insert(AnatomyState::hot);
    if (customize_open_) customize.states.insert(AnatomyState::open);
    paint_part(painter, *this, customize);
    customize.role = PartRole::glyph;
    customize.bounds = geometry.glyph;
    customize.mark = PartMark::customize;
    paint_part(painter, *this, customize);
}

void QuickAccessToolbar::on_pointer(PointerEvent& event) {
    const bool on_customize = event.action != PointerAction::leave && customize_at(event.position);
    if (customize_hot_ != on_customize) {
        customize_hot_ = on_customize;
        invalidate(Dirty::paint);
    }
    if (on_customize && event.action == PointerAction::down && event.button == PointerButton::primary) {
        if (customize_menu_open()) {
            close_customize_menu();
        } else {
            static_cast<void>(show_customize_menu(MenuOpenMode::pointer));
        }
        event.handled = true;
        return;
    }
    ToolStrip::on_pointer(event);
}

void QuickAccessToolbar::on_key(KeyEvent& event) {
    // The arrow away from the strip opens the customize menu: the toolbar's
    // buttons have no menus of their own.
    const std::uint32_t open_key = orientation() == Orientation::vertical ? PhysicalKey::right : PhysicalKey::down;
    if (focused_ && event.action == KeyAction::down && event.physical_key == open_key &&
        event.modifiers == Modifier::none) {
        event.handled = show_customize_menu(MenuOpenMode::keyboard);
        if (event.handled) return;
    }
    ToolStrip::on_key(event);
}

void QuickAccessToolbar::on_focus_changed(bool focused) {
    focused_ = focused;
    ToolStrip::on_focus_changed(focused);
}

// -- semantics -----------------------------------------------------------------

SemanticDescriptor QuickAccessToolbar::semantic_descriptor() const {
    SemanticDescriptor descriptor = ToolStrip::semantic_descriptor();
    descriptor.name = accessible_name.read().empty() ? "Quick Access Toolbar" : accessible_name.read();
    return descriptor;
}

std::vector<SemanticNode> QuickAccessToolbar::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes = ToolStrip::semantic_virtual_children();
    const Rect button = customize_geometry().button;
    if (button.empty()) return nodes;
    const Rect absolute = absolute_bounds();
    SemanticNode node{};
    node.stable_id = std::string(stable_id().value()) + ".customize";
    node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
    node.name = "Customize Quick Access Toolbar";
    node.role = SemanticRole::menu_button;
    node.has_popup = true;
    node.bounds = {absolute.x + button.x, absolute.y + button.y, button.width, button.height};
    node.states = SemanticState::visible | SemanticState::enabled | SemanticState::focusable;
    if (customize_open_) node.states |= SemanticState::expanded;
    node.actions = {SemanticAction::press, SemanticAction::expand, SemanticAction::collapse};
    nodes.push_back(std::move(node));
    return nodes;
}

bool QuickAccessToolbar::on_semantic_child_action(std::string_view id, SemanticAction action,
                                                  std::string_view value) {
    if (id != std::string(stable_id().value()) + ".customize") {
        const bool result = ToolStrip::on_semantic_child_action(id, action, value);
        return result;
    }
    if (action == SemanticAction::collapse) {
        if (!customize_menu_open()) return false;
        close_customize_menu();
        return true;
    }
    if (action == SemanticAction::press || action == SemanticAction::expand) {
        const bool opened = show_customize_menu(MenuOpenMode::keyboard);
        return opened;
    }
    return false;
}

// -- anatomy -------------------------------------------------------------------

namespace {

constexpr std::array<AnatomyState, 2> end_button_states{{AnatomyState::hot, AnatomyState::open}};

constexpr std::array<PartField<QuickAccessToolbar::Parts>, 8> quick_access_toolbar_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &QuickAccessToolbar::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &QuickAccessToolbar::Parts::face},
    {.declaration = {.name = "grip", .role = PartRole::grip, .composed_from = {}},
     .member = &QuickAccessToolbar::Parts::grip},
    {.declaration = {.name = "overflow_button", .role = PartRole::actuator, .composed_from = "button",
                     .states = end_button_states},
     .member = &QuickAccessToolbar::Parts::overflow_button},
    {.declaration = {.name = "overflow_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = end_button_states},
     .member = &QuickAccessToolbar::Parts::overflow_glyph},
    {.declaration = {.name = "customize_button", .role = PartRole::actuator, .composed_from = "button",
                     .states = end_button_states},
     .member = &QuickAccessToolbar::Parts::customize_button},
    {.declaration = {.name = "customize_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = end_button_states},
     .member = &QuickAccessToolbar::Parts::customize_glyph},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &QuickAccessToolbar::Parts::focus},
}};

constexpr std::array<PartDeclaration, 8> quick_access_toolbar_parts =
    part_declarations(quick_access_toolbar_part_fields);
constexpr std::array<AnatomyState, 5> quick_access_toolbar_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

// The events QuickAccessToolbar adds to those of ToolStrip, in declaration
// order.
constexpr std::array<InteractionDeclaration, 2> quick_access_toolbar_interactions{{
    {.name = "customized", .payload = "QuickAccessToolbarCustomization", .mutable_payload = false},
    {.name = "placement_changed", .payload = "QuickAccessToolbarPlacementChange", .mutable_payload = false},
}};

} // namespace

const ControlAnatomy& QuickAccessToolbar::anatomy() const noexcept {
    // The item system is ToolStrip's: the same parts and states per item.
    static const ControlAnatomy quick_access_toolbar_anatomy{
        .control_id = "quick_access_toolbar",
        .parts = quick_access_toolbar_parts,
        .states = quick_access_toolbar_states,
        .custom_states = {},
        .item_systems = ToolStrip::anatomy().item_systems,
    };
    return quick_access_toolbar_anatomy;
}

void QuickAccessToolbar::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const ToolStrip::Parts strip = strip_parts();
    const CustomizeGeometry customize = customize_geometry();
    Parts record;
    record.region = strip.region;
    record.face = strip.face;
    record.grip = strip.grip;
    record.overflow_button = strip.overflow_button;
    record.overflow_glyph = strip.overflow_glyph;
    record.customize_button = PartGeometry::from_bounds(customize.button);
    record.customize_glyph = PartGeometry::from_bounds(customize.glyph);
    if (customize_hot_) {
        record.customize_button.states.insert(AnatomyState::hot);
        record.customize_glyph.states.insert(AnatomyState::hot);
    }
    if (customize_open_) {
        record.customize_button.states.insert(AnatomyState::open);
        record.customize_glyph.states.insert(AnatomyState::open);
    }
    record.focus = strip.focus;
    write_parts(anatomy(), quick_access_toolbar_part_fields, record, geometry);
}

QuickAccessToolbar::Parts QuickAccessToolbar::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), quick_access_toolbar_part_fields, published.parts(), record);
    return record;
}

void QuickAccessToolbar::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    ToolStrip::append_interactions(interactions);
    interactions.insert(interactions.end(), quick_access_toolbar_interactions.begin(),
                        quick_access_toolbar_interactions.end());
}

} // namespace gui_forms
