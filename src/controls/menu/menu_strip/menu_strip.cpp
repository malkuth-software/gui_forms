#include "gui_forms/controls/menu_strip/menu_strip.hpp"

#include "../menu_utilities.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gui_forms {

using menu_detail::menu_display_text;
using menu_detail::validate_specs;

namespace {

constexpr double menu_strip_height = 24.0;

} // namespace

MenuStrip::MenuStrip(StableId stable_id)
    : Control(std::move(stable_id)),
      popup_(std::make_unique<ContextMenu>(
          std::string((*this).stable_id().value()) + ".menu")) {
    set_focusable(true);
    set_cursor(CursorKind::arrow);
    set_paint_plane(PaintPlane::control);
    // This owner retains the popup and subscriptions. Bound handlers borrow
    // this MenuStrip; disposal closes the popup before owner teardown.
    (*popup_).set_root_navigation_handler(
        detail::BoundMemberFunction<bool (MenuStrip::*)(int)>(
            *this, &MenuStrip::navigate_root));
    (*popup_).set_outside_pointer_handler(
        detail::BoundMemberFunction<
            bool (MenuStrip::*)(const PointerEvent&)>(
                *this, &MenuStrip::handle_popup_pointer));
    popup_invoked_ = (*popup_).item_invoked().subscribe(
        *this, Delegate<const MenuItemInvocation&>::bind<
            MenuStrip, &MenuStrip::on_popup_item_invoked>(*this));
    popup_changed_ = (*popup_).open_changed().subscribe(
        *this, Delegate<bool>::bind<
            MenuStrip, &MenuStrip::on_popup_open_changed>(*this));
}

void MenuStrip::on_popup_item_invoked(
    const MenuItemInvocation& invocation) {
    if (!active_index_ || *active_index_ >= items_.size()) return;
    MenuStripInvocation forwarded{
        std::string(stable_id().value()),
        items_[*active_index_].stable_id, invocation};
    item_invoked_.emit(forwarded);
}

void MenuStrip::on_popup_open_changed(bool open_state) {
    if (open_state || switching_ || !active_index_) return;
    active_index_.reset();
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(open_changed_, active_index_);
}

MenuStrip::~MenuStrip() {
    close();
}

void MenuStrip::set_items(std::vector<MenuStripItemSpec> items) {
    require_mutable();
    std::unordered_set<std::string> identities{};
    for (const gui_forms::MenuStripItemSpec& item : items) {
        const std::pair<std::unordered_set<std::string>::iterator, bool> insertion =
            identities.insert(item.stable_id);
        if (!valid_stable_id(item.stable_id) ||
            item.text.empty() || !validate_utf8(item.text).valid() ||
            item.items.empty() || !insertion.second) {
            throw std::invalid_argument(
                "MenuStrip items require unique IDs following the StableId segment rule, text, and menu contents");
        }
        std::unordered_set<std::string> child_identities{};
        std::size_t count{};
        validate_specs(item.items, child_identities, 1U, count);
    }
    close();
    items_ = std::move(items);
    bool selection_retained = false;
    for (const MenuStripItemSpec& item : items_) {
        if (item.stable_id == selected_item_id_ && item.visible) {
            selection_retained = true;
            break;
        }
    }
    if (!selection_retained) selected_item_id_.clear();
    hot_index_.reset();
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void MenuStrip::set_selected_item_id(std::string_view stable_id) {
    require_mutable();
    if (!stable_id.empty()) {
        bool visible = false;
        for (const MenuStripItemSpec& item : items_) {
            if (item.stable_id == stable_id) {
                visible = item.visible;
                break;
            }
        }
        if (!visible) {
            throw std::out_of_range(
                "MenuStrip selected item must identify a visible top-level item");
        }
    }
    if (selected_item_id_ == stable_id) return;
    selected_item_id_ = stable_id;
    invalidate(Dirty::paint | Dirty::semantics | Dirty::accessibility);
}

void MenuStrip::set_use_mnemonic(bool value) {
    require_mutable();
    if (use_mnemonic_ == value) return;
    use_mnemonic_ = value;
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void MenuStrip::set_item_padding(double padding) {
    require_mutable();
    if (!std::isfinite(padding) || padding < 4.0 || padding > 64.0) {
        throw std::invalid_argument(
            "MenuStrip item padding must be finite and between 4 and 64");
    }
    if (item_padding_ == padding) return;
    close();
    item_padding_ = padding;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

bool MenuStrip::open(const std::size_t index, const MenuOpenMode mode) {
    require_mutable();
    if (mode != MenuOpenMode::keyboard && mode != MenuOpenMode::pointer)
        throw std::invalid_argument("MenuStrip opening mode is invalid");
    if (index >= items_.size() || !items_[index].visible ||
        !items_[index].enabled || !attached_window()) return false;
    if (active_index_ == index && (*popup_).is_open()) return true;
    switching_ = (*popup_).is_open();
    if (switching_) (*popup_).close();
    active_index_ = index;
    hot_index_ = index;
    (*popup_).set_items(items_[index].items);
    const std::vector<Rect> bounds = item_bounds();
    const Rect absolute = absolute_bounds();
    (*popup_).show(shared_from_this(),
                 {absolute.x + bounds[index].x,
                  absolute.y + bounds[index].y + bounds[index].height - 1.0}, mode);
    switching_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(open_changed_, active_index_);
    return true;
}

void MenuStrip::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) {
        throw std::invalid_argument("MenuStrip font specification is invalid");
    }
    if (font_ == font) return;
    close();
    font_ = font;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

void MenuStrip::close() noexcept {
    if (!popup_) return;
    (*popup_).close();
}

bool MenuStrip::is_open() const noexcept {
    const bool result = popup_ && (*popup_).is_open();
    return result;
}

Size MenuStrip::measure(Size available) {
    const double preferred = preferred_height();
    const double height = std::min(available.height, preferred);
    const Size result{.width = available.width, .height = height};
    return result;
}

double MenuStrip::preferred_height() const {
    FontSpec font = effective_font(font_);
    font.weight = std::max<std::uint16_t>(font.weight, 700U);
    const ResolvedTextLayout metrics = resolve_text_layout_utf8("Mg", font);
    const double text_scale = effective_text_scale();
    const double result = std::max(menu_strip_height * text_scale,
                    metrics.logical_size.height + 10.0 * text_scale);
    return result;
}

std::vector<Rect> MenuStrip::item_bounds() const {
    std::vector<Rect> result(items_.size());
    const FontSpec normal_font = effective_font(font_);
    FontSpec selected_font = normal_font;
    selected_font.weight = std::max<std::uint16_t>(selected_font.weight, 700U);
    const double scale = effective_text_scale();
    const double height = std::max(preferred_height(), committed_arranged_bounds().height);
    double x{};
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (!items_[index].visible) continue;
        const std::string display = use_mnemonic_
            ? menu_display_text(items_[index].text) : items_[index].text;
        const ResolvedTextLayout normal = resolve_text_layout_utf8(display, normal_font);
        const ResolvedTextLayout selected = resolve_text_layout_utf8(display, selected_font);
        const double text_width = std::max(normal.logical_size.width, selected.logical_size.width);
        const double width = std::max(42.0 * scale,
            item_padding_ * 2.0 * scale + text_width);
        result[index] = {x, 0.0, width, height};
        x += width;
    }
    return result;
}

std::optional<std::size_t> MenuStrip::index_at(Point window_point) const {
    const Rect absolute = absolute_bounds();
    if (!absolute.contains(window_point)) return {};
    const Point local{window_point.x - absolute.x, window_point.y - absolute.y};
    const std::vector<Rect> bounds = item_bounds();
    for (std::size_t index = 0; index < bounds.size(); ++index) {
        if (items_[index].visible && bounds[index].contains(local)) return index;
    }
    return {};
}

std::optional<std::size_t> MenuStrip::next_enabled(
    std::size_t start, int direction) const {
    if (items_.empty()) return {};
    for (std::size_t step = 1U; step <= items_.size(); ++step) {
        const std::ptrdiff_t raw = static_cast<std::ptrdiff_t>(start) +
            static_cast<std::ptrdiff_t>(direction) *
                static_cast<std::ptrdiff_t>(step);
        const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(items_.size());
        const std::size_t index = static_cast<std::size_t>((raw % count + count) % count);
        if (items_[index].visible && items_[index].enabled) return index;
    }
    return {};
}

bool MenuStrip::navigate_root(int direction) {
    if (!active_index_) return false;
    const std::optional<std::size_t> next = next_enabled(*active_index_, direction);
    if (!next) return false;
    const bool opened = open(*next);
    return opened;
}

bool MenuStrip::handle_popup_pointer(const PointerEvent& event) {
    if (event.action != PointerAction::move &&
        event.action != PointerAction::down) return false;
    const std::optional<std::size_t> index = index_at(event.position);
    if (!index || !items_[*index].enabled) return false;
    set_hot(index);
    if (event.action == PointerAction::move && active_index_ != index) {
        const bool opened = open(*index, MenuOpenMode::pointer);
        return opened;
    }
    if (event.action == PointerAction::down) {
        if (active_index_ == index) close();
        else static_cast<void>(open(*index, MenuOpenMode::pointer));
        return true;
    }
    return true;
}

void MenuStrip::set_hot(std::optional<std::size_t> index) {
    if (hot_index_ == index) return;
    hot_index_ = index;
    invalidate(Dirty::paint | Dirty::semantics);
}

void MenuStrip::on_paint(Painter& painter, Rect) {
    const Rect surface{0.0, 0.0, committed_arranged_bounds().width,
                      committed_arranged_bounds().height};
    const BasicControlStyle& style = effective_theme().basic_style();
    // The strip's face and each item's plate are parts (the menu_strip
    // anatomy's `face` and `menu_item`), so a theme's `menu-strip::part(...)`
    // rules select them; an item is keyed by its stable ID.
    PartPaint face;
    face.family = ControlVisualRole::panel;
    face.role = PartRole::face;
    face.bounds = surface;
    face.key = PartKey{"face", {}, {}};
    paint_part(painter, *this, face);
    painter.draw_line({0.0, std::max(0.0, surface.height - 1.0)},
                      {surface.width, std::max(0.0, surface.height - 1.0)},
                      style.border, 1.0);
    const std::vector<Rect> bounds = item_bounds();
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (!items_[index].visible) continue;
        const Rect item = bounds[index];
        const bool active = active_index_ == index;
        const bool hot = hot_index_ == index;
        const bool selected = selected_item_id_ == items_[index].stable_id;
        ControlVisualContext item_context =
            visual_context(hot, active, selected, focused_ && hot);
        if (!items_[index].enabled) {
            item_context.surface = ControlSurfaceState::disabled;
        }
        const ControlVisualRecipe& item_recipe = effective_theme().resolve(
            ControlVisualRole::menu_item, item_context);
        if (active || hot || selected) {
            // An open item shows pressed, as its recipe context does.
            AnatomyStateSet plate_states;
            if (hot) plate_states.insert(AnatomyState::hot);
            if (active) plate_states.insert(AnatomyState::pressed);
            if (selected) plate_states.insert(AnatomyState::selected);
            if (focused_ && hot) plate_states.insert(AnatomyState::focus_visible);
            if (!items_[index].enabled) plate_states.insert(AnatomyState::disabled);
            PartPaint plate;
            plate.family = ControlVisualRole::menu_item;
            plate.role = PartRole::face;
            plate.bounds = {item.x + 1.0, 1.0, item.width - 2.0,
                            std::max(0.0, item.height - 2.0)};
            plate.states = plate_states;
            plate.key = PartKey{"menu_item", items_[index].stable_id, index};
            paint_part(painter, *this, plate);
        }
        const Color ink = item_recipe.text;
        FontSpec font = effective_font(font_);
        if (active || selected) {
            font.weight = std::max<std::uint16_t>(font.weight, 700U);
        }
        painter.draw_text_utf8({item.x + item_padding_ *
                                             effective_text_scale(),
                                item.y + std::max(font.size,
                                    item.height * 0.5 + font.size * 0.35)},
                               use_mnemonic_
                                   ? menu_display_text(items_[index].text)
                                   : items_[index].text,
                               font, ink);
    }
    if (focused_ && !active_index_ && hot_index_) {
        const Rect item = bounds[*hot_index_];
        painter.stroke_rect({item.x + 2.5, 2.5, item.width - 5.0,
                             std::max(0.0, item.height - 5.0)},
                            effective_theme().resolve(
                                ControlVisualRole::menu_item,
                                visual_context(true, false, false, true))
                                .focus_ring,
                            1.0);
    }
}

void MenuStrip::on_pointer(PointerEvent& event) {
    if (event.action == PointerAction::move) {
        const std::optional<std::size_t> index = index_at(event.position);
        set_hot(index);
        if (is_open() && index && active_index_ != index &&
            items_[*index].enabled) {
            static_cast<void>(open(*index, MenuOpenMode::pointer));
        }
        event.handled = hot_index_.has_value();
    } else if (event.action == PointerAction::leave && !is_open()) {
        set_hot({});
    } else if (event.action == PointerAction::down &&
               event.button == PointerButton::primary) {
        const std::optional<std::size_t> index = index_at(event.position);
        if (index && items_[*index].enabled) {
            if (active_index_ == index && is_open()) close();
            else static_cast<void>(open(*index, MenuOpenMode::pointer));
            event.handled = true;
        }
    }
}

void MenuStrip::on_key(KeyEvent& event) {
    if (!focused_ || event.action != KeyAction::down) return;
    std::optional<std::size_t> current = active_index_ ? active_index_ : hot_index_;
    if (!current) {
        for (std::size_t index = 0; index < items_.size(); ++index) {
            if (items_[index].visible && items_[index].enabled) {
                current = index;
                break;
            }
        }
    }
    if (!current) return;
    if (event.physical_key == PhysicalKey::left ||
        event.physical_key == PhysicalKey::right) {
        const std::optional<std::size_t> next = next_enabled(*current,
            event.physical_key == PhysicalKey::right ? 1 : -1);
        if (next) {
            if (is_open()) static_cast<void>(open(*next));
            else set_hot(next);
        }
    } else if (event.physical_key == PhysicalKey::down ||
               event.physical_key == PhysicalKey::enter ||
               event.physical_key == PhysicalKey::space) {
        static_cast<void>(open(*current));
    } else if (event.physical_key == PhysicalKey::escape) {
        close();
    } else {
        return;
    }
    event.handled = true;
}

void MenuStrip::on_focus_changed(bool focused) {
    focused_ = focused;
    if (focused_ && !hot_index_) {
        for (std::size_t index = 0; index < items_.size(); ++index) {
            if (items_[index].visible && items_[index].enabled) {
                hot_index_ = index;
                break;
            }
        }
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

SemanticDescriptor MenuStrip::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::menu_bar;
    descriptor.name = accessible_name().empty() ? "Application menu" : accessible_name();
    descriptor.description = accessible_description();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> MenuStrip::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    nodes.reserve(items_.size());
    const Rect absolute = absolute_bounds();
    const std::vector<Rect> bounds = item_bounds();
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (!items_[index].visible) continue;
        SemanticNode node{};
        node.stable_id = items_[index].stable_id;
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::menu_bar_item;
        node.name = use_mnemonic_
            ? menu_display_text(items_[index].text) : items_[index].text;
        if (use_mnemonic_) node.access_key = mnemonic_access_key(items_[index].text);
        node.bounds = {absolute.x + bounds[index].x, absolute.y + bounds[index].y,
                       bounds[index].width, bounds[index].height};
        node.states = SemanticState::visible | SemanticState::focusable;
        if (effectively_enabled() && items_[index].enabled) {
            node.states |= SemanticState::enabled;
        }
        if (selected_item_id_ == items_[index].stable_id) {
            node.states |= SemanticState::selected;
        }
        if (active_index_ == index) {
            node.states |= SemanticState::expanded;
        }
        if (focused_ && hot_index_ == index) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::focus, SemanticAction::press,
                        SemanticAction::expand, SemanticAction::collapse};
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool MenuStrip::on_semantic_child_action(std::string_view id,
                                         SemanticAction action,
                                         std::string_view) {
    std::vector<MenuStripItemSpec>::iterator found = items_.begin();
    while (found != items_.end() && (*found).stable_id != id) ++found;
    if (found == items_.end() || !(*found).visible || !(*found).enabled) return false;
    const std::size_t index = static_cast<std::size_t>(
        std::distance(items_.begin(), found));
    if (action == SemanticAction::collapse) {
        if (active_index_ != index) return false;
        close();
        return true;
    }
    if (action != SemanticAction::focus && action != SemanticAction::press &&
        action != SemanticAction::expand) return false;
    if (window()) static_cast<void>((*window()).request_focus(shared_from_this()));
    set_hot(index);
    if (action != SemanticAction::focus) {
        const bool opened = open(index);
        return opened;
    }
    return true;
}

bool MenuStrip::mnemonic_matches(char32_t character) const noexcept {
    if (!use_mnemonic_) return false;
    for (const MenuStripItemSpec& item : items_) {
        if (item.visible && item.enabled &&
            is_mnemonic(character, item.text)) return true;
    }
    return false;
}

bool MenuStrip::process_mnemonic_self(char32_t character) {
    if (!mnemonic_matches(character)) return false;
    std::vector<std::size_t> matches{};
    for (std::size_t index = 0U; index < items_.size(); ++index) {
        if (items_[index].visible && items_[index].enabled &&
            is_mnemonic(character, items_[index].text)) {
            matches.push_back(index);
        }
    }
    if (matches.empty()) return false;
    std::size_t selected = matches.front();
    const std::optional<std::size_t> current = active_index_
        ? active_index_ : hot_index_;
    if (current) {
        const std::vector<std::size_t>::iterator found =
            std::find(matches.begin(), matches.end(), *current);
        if (found != matches.end()) {
            const std::vector<std::size_t>::iterator next = std::next(found);
            selected = next == matches.end() ? matches.front() : *next;
        }
    }
    if (window() && !(*window()).request_focus(shared_from_this())) {
        // The mnemonic is still owned, but prevent-mode validation may reject
        // the focus transaction that would authorize opening the popup.
        return true;
    }
    set_hot(selected);
    const bool opened = open(selected);
    return opened;
}

void MenuStrip::on_dispose() noexcept {
    close();
    if (popup_) (*popup_).dispose();
}



namespace {

constexpr std::array<PartField<MenuStrip::Parts>, 4> menu_strip_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &MenuStrip::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &MenuStrip::Parts::face},
    {.declaration = {.name = "divider", .role = PartRole::divider, .composed_from = {}},
     .member = &MenuStrip::Parts::divider},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &MenuStrip::Parts::focus},
}};

constexpr std::array<PartField<MenuStrip::ItemParts>, 3> menu_strip_item_part_fields{{
    {.declaration = {.name = "menu_item", .role = PartRole::face, .composed_from = {}},
     .member = &MenuStrip::ItemParts::menu_item},
    {.declaration = {.name = "menu_label", .role = PartRole::label, .composed_from = {}},
     .member = &MenuStrip::ItemParts::menu_label},
    {.declaration = {.name = "menu_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &MenuStrip::ItemParts::menu_focus},
}};

constexpr std::array<StateField<MenuStrip::ItemStates>, 4> menu_strip_item_state_fields{{
    {.state = AnatomyState::hot, .member = &MenuStrip::ItemStates::hot},
    {.state = AnatomyState::open, .member = &MenuStrip::ItemStates::open},
    {.state = AnatomyState::selected, .member = &MenuStrip::ItemStates::selected},
    {.state = AnatomyState::disabled, .member = &MenuStrip::ItemStates::disabled},
}};

constexpr std::array<PartDeclaration, 3> menu_strip_item_parts =
    part_declarations(menu_strip_item_part_fields);
constexpr std::array<AnatomyState, 4> menu_strip_item_states =
    state_declarations(menu_strip_item_state_fields);

constexpr std::array<PartDeclaration, 4> menu_strip_parts =
    part_declarations(menu_strip_part_fields);
constexpr std::array<AnatomyState, 5> menu_strip_states{
    AnatomyState::normal,
    AnatomyState::enabled,
    AnatomyState::disabled,
    AnatomyState::focused,
    AnatomyState::focus_visible,
};

constexpr std::array<ItemSystemDeclaration, 1> menu_strip_item_systems{{
    {.name = "item", .parts = menu_strip_item_parts, .states = menu_strip_item_states,
     .custom_states = {}},
}};

constexpr ControlAnatomy menu_strip_anatomy{
    .control_id = "menu_strip",
    .parts = menu_strip_parts,
    .states = menu_strip_states,
    .custom_states = {},
    .item_systems = menu_strip_item_systems,
};

} // namespace

const ControlAnatomy& MenuStrip::anatomy() const noexcept {
    return menu_strip_anatomy;
}

void MenuStrip::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect surface{0.0, 0.0, committed_arranged_bounds().width,
                       committed_arranged_bounds().height};
    Parts record;
    record.region = PartGeometry::from_bounds(surface);
    record.face = PartGeometry::from_bounds(surface);
    // The one-unit rule along the strip's bottom.
    const Rect rule{0.0, std::max(0.0, surface.height - 1.5), surface.width, 1.0};
    record.divider = PartGeometry::from_bounds(Rect::intersection(rule, surface));
    record.focus = PartGeometry::from_bounds(surface);
    write_parts(anatomy(), menu_strip_part_fields, record, geometry);
}

MenuStrip::Parts MenuStrip::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), menu_strip_part_fields, published.parts(), record);
    return record;
}

std::size_t MenuStrip::realized_item_index(std::size_t item) const noexcept {
    std::size_t seen = 0U;
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (!items_[index].visible) continue;
        if (seen == item) return index;
        ++seen;
    }
    return items_.size();
}

std::size_t MenuStrip::realized_item_count() const {
    std::size_t count = 0U;
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (items_[index].visible) ++count;
    }
    return count;
}

std::string MenuStrip::realized_item_stable_id(std::size_t item) const {
    return items_[realized_item_index(item)].stable_id;
}

void MenuStrip::publish_realized_item_geometry(std::size_t item,
                                          std::span<PartGeometry> geometry) const {
    ItemParts record;

    const std::size_t index = realized_item_index(item);
    const Rect bounds = item_bounds()[index];
    const bool emphasised = active_index_ == index ||
                            selected_item_id_ == items_[index].stable_id;
    FontSpec font = effective_font(font_);
    if (emphasised) {
        font.weight = std::max<std::uint16_t>(font.weight, 700U);
    }
    const std::string display = use_mnemonic_
        ? menu_display_text(items_[index].text) : items_[index].text;
    const double text_width = resolve_text_layout_utf8(display, font).logical_size.width;
    const double text_x = bounds.x + item_padding_ * effective_text_scale();
    const Rect label{text_x, bounds.y, text_width, bounds.height};
    // The highlight on_paint lays inside the item, one unit in.
    const Rect face{bounds.x + 1.0, 1.0, std::max(0.0, bounds.width - 2.0),
                    std::max(0.0, bounds.height - 2.0)};
    record.menu_item = PartGeometry::from_bounds(face);
    record.menu_label = PartGeometry::from_bounds(Rect::intersection(label, bounds));
    record.menu_focus = PartGeometry::from_bounds(bounds);
    write_item_parts(anatomy().item_systems.front(), menu_strip_item_part_fields, record, geometry);
}

AnatomyStateSet MenuStrip::realized_item_anatomy_states(std::size_t item) const {
    AnatomyStateSet states;
    const std::size_t index = realized_item_index(item);
    if (hot_index_ == index) {
        states.insert(AnatomyState::hot);
    }
    if (active_index_ == index) {
        states.insert(AnatomyState::open);
    }
    if (selected_item_id_ == items_[index].stable_id) {
        states.insert(AnatomyState::selected);
    }
    if (!items_[index].enabled) {
        states.insert(AnatomyState::disabled);
    }
    return states;
}

std::vector<MenuStrip::Item> MenuStrip::realized_items() const {
    const std::size_t count = realized_item_count();
    std::vector<Item> records;
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Item record;
        record.index = realized_item_index(item);
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems.front(), menu_strip_item_part_fields, published.parts(), record.parts);
        read_states(menu_strip_item_state_fields, realized_item_anatomy_states(item), record.states);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events MenuStrip adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> menu_strip_interactions{{
    {.name = "item_invoked", .payload = "MenuStripInvocation", .mutable_payload = false},
    {.name = "open_changed", .payload = "std::optional<std::size_t>", .mutable_payload = false},
}};

} // namespace

void MenuStrip::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), menu_strip_interactions.begin(), menu_strip_interactions.end());
}

} // namespace gui_forms
