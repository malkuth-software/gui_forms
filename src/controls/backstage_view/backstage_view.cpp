#include "gui_forms/controls/backstage_view/backstage_view.hpp"

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

// Column geometry in logical units at text scale 1: fast commands on 26-unit
// rows and topics on 34-unit rows from an 8-unit top margin, a 16-unit icon
// 10 units in and the label from 34 units.
constexpr double column_top = 8.0;
constexpr double command_row = 26.0;
constexpr double topic_row = 34.0;
constexpr double icon_left = 10.0;
constexpr double icon_side = 16.0;
constexpr double label_left = 34.0;
constexpr double label_right = 8.0;
constexpr double minimum_column = 48.0;
constexpr double maximum_column = 480.0;

std::string node_id(std::string_view view_id, std::string_view item_id) {
    std::string identity(view_id);
    identity.append(".item.");
    identity.append(item_id);
    return identity;
}

} // namespace

BackstageView::BackstageView(StableId stable_id) : Control(std::move(stable_id)) {
    set_focusable(true);
    set_paint_plane(PaintPlane::control);
}

// -- model ---------------------------------------------------------------------

void BackstageView::set_items(std::vector<BackstageItemSpec> items) {
    require_mutable();
    if (items.size() > maximum_items) throw std::invalid_argument("BackstageView has more items than maximum_items");
    std::unordered_set<std::string> identities{};
    std::unordered_set<const Control*> pages{};
    for (const BackstageItemSpec& item : items) {
        if (!valid_stable_id(item.stable_id) || !validate_utf8(item.text).valid() ||
            !identities.insert(item.stable_id).second) {
            throw std::invalid_argument(
                "BackstageView items require unique stable IDs following the StableId segment rule and valid UTF-8");
        }
        if (item.kind == BackstageItemKind::command && (!item.command || item.page)) {
            throw std::invalid_argument("BackstageView commands require a Command and take no page");
        }
        if (item.kind == BackstageItemKind::topic &&
            (!item.page || item.text.empty() || item.command || item.definitive)) {
            throw std::invalid_argument("BackstageView topics require a page and text, and take no Command");
        }
        if (item.page) {
            const Control::Ptr parent = (*item.page).parent();
            if ((parent && parent.get() != this) || !pages.insert(item.page.get()).second) {
                throw std::invalid_argument("BackstageView pages are used once and not owned elsewhere");
            }
        }
    }
    std::string selected_id;
    if (selected_) selected_id = items_[*selected_].stable_id;
    // Pages that leave are removed; pages that stay keep their place.
    for (const BackstageItemSpec& old : items_) {
        if (old.page && pages.count(old.page.get()) == 0U && (*old.page).parent().get() == this) {
            static_cast<void>(remove_child((*old.page).runtime_id()));
        }
    }
    items_ = std::move(items);
    watches_.clear();
    watches_.reserve(items_.size());
    for (const BackstageItemSpec& item : items_) {
        if (item.page && (*item.page).parent().get() != this) add_child(item.page);
        if (item.command) watches_.push_back((*item.command).state_changed().subscribe(*this, CommandWatch{this}));
    }
    selected_.reset();
    for (std::size_t item = 0; item < items_.size(); ++item) {
        if (items_[item].kind == BackstageItemKind::topic && items_[item].stable_id == selected_id) selected_ = item;
    }
    for (std::size_t item = 0; item < items_.size() && !selected_; ++item) {
        if (items_[item].kind == BackstageItemKind::topic) selected_ = item;
    }
    hot_.reset();
    pressed_.reset();
    focus_item_.reset();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void BackstageView::CommandWatch::operator()(const CommandState&) const {
    if (view != nullptr && (*view).is_alive()) (*view).invalidate(Dirty::paint | Dirty::semantics);
}

void BackstageView::set_selected_topic(std::size_t item) {
    require_mutable();
    if (item >= items_.size()) throw std::out_of_range("BackstageView item index is past the end");
    if (items_[item].kind != BackstageItemKind::topic) throw std::invalid_argument("BackstageView item is not a topic");
    select_topic(item);
}

void BackstageView::set_column_width(double width) {
    require_mutable();
    if (!std::isfinite(width) || width < minimum_column || width > maximum_column) {
        throw std::invalid_argument("BackstageView column width must lie within 48 to 480");
    }
    column_width_ = width;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

const std::string& BackstageView::authored_text(const BackstageItemSpec& item) noexcept {
    if (item.text.empty() && item.command) return (*item.command).state().text;
    return item.text;
}

// The label as shown: the authored text without its mnemonic marker.
std::string BackstageView::item_text(const BackstageItemSpec& item) const {
    return parse_mnemonic_text(authored_text(item)).display_text;
}

bool BackstageView::item_enabled(const BackstageItemSpec& item) const {
    const bool own = !item.command || (*item.command).state().enabled;
    return own && effectively_enabled();
}

void BackstageView::select_topic(std::size_t item) {
    if (selected_ == item) return;
    selected_ = item;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    const BackstageTopicChange change{std::string(stable_id().value()), item, items_[item].stable_id};
    publish_change(topic_changed_, change);
}

bool BackstageView::activate(std::string_view item_id) {
    require_mutable();
    for (std::size_t item = 0; item < items_.size(); ++item) {
        if (items_[item].stable_id != item_id) continue;
        if (!item_enabled(items_[item])) return false;
        activate_item(item);
        return true;
    }
    return false;
}

// The one path: a command runs (a definitive one then asks to close); a
// topic is selected.
void BackstageView::activate_item(std::size_t item) {
    if (item >= items_.size() || !item_enabled(items_[item])) return;
    focus_item_ = item;
    if (items_[item].kind == BackstageItemKind::topic) {
        select_topic(item);
        return;
    }
    const std::shared_ptr<Command> command = items_[item].command;
    const bool definitive = items_[item].definitive;
    const Control::Ptr lifetime = shared_from_this();
    static_cast<void>((*command).execute(stable_id().value()));
    if (is_alive() && definitive) close_requested_.emit();
}

// -- layout --------------------------------------------------------------------

BackstageView::Geometry BackstageView::geometry() const {
    const double scale = effective_text_scale();
    const Rect client = client_rectangle();
    const double column = std::min(client.width, column_width_ * scale);
    Geometry placed;
    placed.column = {0.0, 0.0, column, client.height};
    placed.page = {column, 0.0, std::max(0.0, client.width - column), client.height};
    double y = column_top * scale;
    placed.items.reserve(items_.size());
    for (const BackstageItemSpec& item : items_) {
        const double height = (item.kind == BackstageItemKind::topic ? topic_row : command_row) * scale;
        placed.items.push_back({0.0, y, column, height});
        y += height;
    }
    return placed;
}

Size BackstageView::measure(Size available) {
    return available;
}

// The selected topic's page fills the page part; the other pages collapse.
// The page comes from geometry(), the one function paint and the published
// parts use, in the bounds as arranged.
void BackstageView::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const Rect page = geometry().page;
    for (std::size_t item = 0; item < items_.size(); ++item) {
        const Control::Ptr& control = items_[item].page;
        if (!control || !is_current_layout_child(control)) continue;
        const bool shown = selected_ == item && !page.empty();
        set_child_layout_collapsed(control, !shown);
        if (shown) set_child_layout(control, page);
    }
}

BackstageView::ItemParts BackstageView::item_parts(const Geometry& placed, std::size_t item) const {
    const double scale = effective_text_scale();
    const Rect bounds = placed.items[item];
    const Rect visible = Rect::intersection(bounds, placed.column);
    ItemParts parts;
    parts.item_face = PartGeometry::from_bounds(visible);
    if (items_[item].image.value != 0U) {
        const double side = icon_side * scale;
        const Rect icon{icon_left * scale, std::floor(bounds.y + (bounds.height - side) * 0.5), side, side};
        parts.item_icon = PartGeometry::from_bounds(Rect::intersection(icon, visible));
    }
    const double left = label_left * scale;
    const Rect label{left, bounds.y, std::max(0.0, bounds.width - left - label_right * scale), bounds.height};
    parts.item_label = PartGeometry::from_bounds(Rect::intersection(label, visible));
    parts.item_focus = parts.item_face;
    return parts;
}

AnatomyStateSet BackstageView::item_states(std::size_t item) const {
    AnatomyStateSet states;
    const bool enabled = item_enabled(items_[item]);
    if (!enabled) states.insert(AnatomyState::disabled);
    if (enabled && hot_ == item) states.insert(AnatomyState::hot);
    if (pressed_ == item) states.insert(AnatomyState::pressed);
    if (selected_ == item) states.insert(AnatomyState::selected);
    const Window* const host = attached_window();
    if (focused_ && focus_item_ == item && host != nullptr && (*host).focus_cue_visible()) {
        states.insert(AnatomyState::focus_visible);
    }
    return states;
}

// -- paint ---------------------------------------------------------------------

void BackstageView::on_paint(Painter& painter, Rect) {
    const Geometry placed = geometry();
    PartPaint part;
    part.font = text_font(font_);
    part.role = PartRole::face;
    part.family = ControlVisualRole::window;
    part.bounds = placed.page;
    paint_part(painter, *this, part);
    part.family = ControlVisualRole::card;
    part.bounds = placed.column;
    paint_part(painter, *this, part);
    for (std::size_t item = 0; item < items_.size(); ++item) {
        const ItemParts parts = item_parts(placed, item);
        const AnatomyStateSet states = item_states(item);
        part.family = ControlVisualRole::menu_item;
        part.states = states;
        part.role = PartRole::face;
        part.bounds = parts.item_face.bounds;
        paint_part(painter, *this, part);
        if (parts.item_icon.present) {
            part.role = PartRole::icon;
            part.bounds = parts.item_icon.bounds;
            part.image = items_[item].image;
            paint_part(painter, *this, part);
            part.image = {};
        }
        const std::string label = item_text(items_[item]);
        part.role = PartRole::label;
        part.bounds = parts.item_label.bounds;
        part.text = label;
        paint_part(painter, *this, part);
        part.text = {};
        part.role = PartRole::focus;
        part.bounds = parts.item_focus.bounds;
        paint_part(painter, *this, part);
    }
}

// -- input ---------------------------------------------------------------------

std::optional<std::size_t> BackstageView::item_at(Point local) const {
    const Geometry placed = geometry();
    if (!placed.column.contains(local)) return std::nullopt;
    for (std::size_t item = 0; item < placed.items.size(); ++item) {
        if (placed.items[item].contains(local)) return item;
    }
    return std::nullopt;
}

void BackstageView::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    const std::optional<std::size_t> hit =
        event.action == PointerAction::leave ? std::nullopt : item_at(local);
    if (event.action == PointerAction::leave || event.action == PointerAction::move ||
        event.action == PointerAction::enter) {
        if (hot_ != hit) {
            hot_ = hit;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down) {
        pressed_ = hit && item_enabled(items_[*hit]) ? hit : std::nullopt;
        event.handled = hit.has_value();
        invalidate(Dirty::paint);
        return;
    }
    if (event.action != PointerAction::up) return;
    const std::optional<std::size_t> pressed = pressed_;
    pressed_.reset();
    invalidate(Dirty::paint);
    // A click is a press and release on the same item.
    if (pressed && hit == pressed) {
        event.handled = true;
        activate_item(*pressed);
    }
}

// Moves the keyboard to the next enabled item after (or before) the focused
// one; with none focused, to the first (or last) enabled item. It stops at
// the column's ends.
void BackstageView::move_focus(bool forward) {
    std::optional<std::size_t> found{};
    if (forward) {
        const std::size_t start = focus_item_ ? *focus_item_ + 1U : 0U;
        for (std::size_t item = start; item < items_.size() && !found; ++item) {
            if (item_enabled(items_[item])) found = item;
        }
    } else {
        const std::size_t end = focus_item_ ? *focus_item_ : items_.size();
        for (std::size_t item = end; item > 0U && !found; --item) {
            if (item_enabled(items_[item - 1U])) found = item - 1U;
        }
    }
    if (!found) return;
    focus_item_ = found;
    invalidate(Dirty::paint | Dirty::semantics);
}

// Keys: Up and Down move through the column (skipping disabled items), Home
// and End to its ends, Enter and Space activate, Escape asks to close. Tab
// leaves the column for the page's controls.
void BackstageView::on_key(KeyEvent& event) {
    if (!focused_ || event.action != KeyAction::down || !effectively_enabled()) return;
    const std::uint32_t key = event.physical_key;
    if (key == PhysicalKey::down) {
        move_focus(true);
    } else if (key == PhysicalKey::up) {
        move_focus(false);
    } else if (key == PhysicalKey::home) {
        focus_item_.reset();
        move_focus(true);
    } else if (key == PhysicalKey::end) {
        focus_item_.reset();
        move_focus(false);
    } else if ((key == PhysicalKey::enter || key == PhysicalKey::space) && focus_item_) {
        activate_item(*focus_item_);
    } else if (key == PhysicalKey::escape) {
        close_requested_.emit();
    } else {
        return;
    }
    event.handled = true;
}

void BackstageView::on_focus_changed(bool focused) {
    focused_ = focused;
    if (focused && !focus_item_) focus_item_ = selected_;
    if (focused && !focus_item_ && !items_.empty()) focus_item_ = 0U;
    invalidate(Dirty::paint | Dirty::semantics);
}

bool BackstageView::mnemonic_matches(char32_t character) const noexcept {
    for (const BackstageItemSpec& item : items_) {
        if (is_mnemonic(character, authored_text(item))) return true;
    }
    return false;
}

bool BackstageView::process_mnemonic_self(char32_t character) {
    for (std::size_t item = 0; item < items_.size(); ++item) {
        const BackstageItemSpec& spec = items_[item];
        if (!is_mnemonic(character, authored_text(spec)) || !item_enabled(spec)) continue;
        activate_item(item);
        return true;
    }
    return false;
}

// -- semantics -----------------------------------------------------------------

SemanticDescriptor BackstageView::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name().empty() ? "Backstage" : accessible_name();
    descriptor.description = accessible_description();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

// The column's entries: commands as buttons, topics as tabs (selected while
// shown), positioned among all entries.
std::vector<SemanticNode> BackstageView::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes;
    const Geometry placed = geometry();
    const Rect absolute = absolute_bounds();
    const std::string view_id(stable_id().value());
    nodes.reserve(items_.size());
    for (std::size_t item = 0; item < items_.size(); ++item) {
        const BackstageItemSpec& spec = items_[item];
        SemanticNode node{};
        node.stable_id = node_id(view_id, spec.stable_id);
        node.role = spec.kind == BackstageItemKind::topic ? SemanticRole::tab : SemanticRole::button;
        node.name = item_text(spec);
        node.access_key = mnemonic_access_key(authored_text(spec));
        if (spec.command) node.description = (*spec.command).state().description;
        node.position_in_set = item + 1U;
        node.set_size = items_.size();
        const Rect bounds = placed.items[item];
        node.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
        node.states = SemanticState::visible | SemanticState::focusable;
        if (item_enabled(spec)) node.states |= SemanticState::enabled;
        if (selected_ == item) node.states |= SemanticState::selected;
        if (focused_ && focus_item_ == item) node.states |= SemanticState::focused;
        node.actions = {spec.kind == BackstageItemKind::topic ? SemanticAction::select : SemanticAction::press};
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool BackstageView::on_semantic_child_action(std::string_view id, SemanticAction action, std::string_view) {
    const std::string view_id(stable_id().value());
    for (std::size_t item = 0; item < items_.size(); ++item) {
        if (node_id(view_id, items_[item].stable_id) != id) continue;
        const bool topic = items_[item].kind == BackstageItemKind::topic;
        const bool fits = topic ? action == SemanticAction::select || action == SemanticAction::press
                                : action == SemanticAction::press;
        if (!fits || !item_enabled(items_[item])) return false;
        activate_item(item);
        return true;
    }
    return false;
}

// -- anatomy -------------------------------------------------------------------

namespace {

constexpr std::array<PartField<BackstageView::Parts>, 4> backstage_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &BackstageView::Parts::region},
    {.declaration = {.name = "column", .role = PartRole::face, .composed_from = {}},
     .member = &BackstageView::Parts::column},
    {.declaration = {.name = "page", .role = PartRole::face, .composed_from = {}},
     .member = &BackstageView::Parts::page},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &BackstageView::Parts::focus},
}};

constexpr std::array<PartField<BackstageView::ItemParts>, 4> backstage_item_part_fields{{
    {.declaration = {.name = "item_face", .role = PartRole::face, .composed_from = {}},
     .member = &BackstageView::ItemParts::item_face},
    {.declaration = {.name = "item_icon", .role = PartRole::icon, .composed_from = {}},
     .member = &BackstageView::ItemParts::item_icon},
    {.declaration = {.name = "item_label", .role = PartRole::label, .composed_from = {}},
     .member = &BackstageView::ItemParts::item_label},
    {.declaration = {.name = "item_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &BackstageView::ItemParts::item_focus},
}};

constexpr std::array<StateField<BackstageView::ItemStates>, 5> backstage_item_state_fields{{
    {.state = AnatomyState::hot, .member = &BackstageView::ItemStates::hot},
    {.state = AnatomyState::pressed, .member = &BackstageView::ItemStates::pressed},
    {.state = AnatomyState::selected, .member = &BackstageView::ItemStates::selected},
    {.state = AnatomyState::disabled, .member = &BackstageView::ItemStates::disabled},
    {.state = AnatomyState::focus_visible, .member = &BackstageView::ItemStates::focus_visible},
}};

constexpr std::array<PartDeclaration, 4> backstage_parts = part_declarations(backstage_part_fields);
constexpr std::array<PartDeclaration, 4> backstage_item_parts = part_declarations(backstage_item_part_fields);
constexpr std::array<AnatomyState, 5> backstage_item_states = state_declarations(backstage_item_state_fields);
constexpr std::array<AnatomyState, 5> backstage_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};
constexpr std::array<ItemSystemDeclaration, 1> backstage_item_systems{{
    {.name = "backstage_item", .parts = backstage_item_parts, .states = backstage_item_states, .custom_states = {}},
}};

constexpr ControlAnatomy backstage_anatomy{
    .control_id = "backstage_view",
    .parts = backstage_parts,
    .states = backstage_states,
    .custom_states = {},
    .item_systems = backstage_item_systems,
};

// The events BackstageView adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> backstage_interactions{{
    {.name = "topic_changed", .payload = "BackstageTopicChange", .mutable_payload = false},
    {.name = "close_requested", .payload = "", .mutable_payload = false},
}};

} // namespace

const ControlAnatomy& BackstageView::anatomy() const noexcept {
    return backstage_anatomy;
}

void BackstageView::publish_part_geometry(std::span<PartGeometry> geometry_parts) const {
    const Geometry placed = geometry();
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    record.column = PartGeometry::from_bounds(placed.column);
    record.page = PartGeometry::from_bounds(placed.page);
    record.focus = PartGeometry::from_bounds(placed.column);
    write_parts(anatomy(), backstage_part_fields, record, geometry_parts);
}

BackstageView::Parts BackstageView::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), backstage_part_fields, published.parts(), record);
    return record;
}

std::size_t BackstageView::realized_item_count() const {
    return items_.size();
}

std::string BackstageView::realized_item_stable_id(std::size_t item) const {
    if (item >= items_.size()) return {};
    return node_id(stable_id().value(), items_[item].stable_id);
}

ItemSystemIndex BackstageView::publish_realized_item_system(std::size_t) const {
    return ItemSystemIndex{0U};
}

void BackstageView::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry_parts) const {
    if (item >= items_.size()) return;
    const ItemParts record = item_parts(geometry(), item);
    write_item_parts(anatomy().item_systems[0], backstage_item_part_fields, record, geometry_parts);
}

AnatomyStateSet BackstageView::realized_item_anatomy_states(std::size_t item) const {
    if (item >= items_.size()) return AnatomyStateSet{};
    return item_states(item);
}

std::vector<BackstageView::Item> BackstageView::realized_items() const {
    const Geometry placed = geometry();
    std::vector<Item> records;
    records.reserve(items_.size());
    for (std::size_t item = 0; item < items_.size(); ++item) {
        Item record;
        record.index = item;
        record.stable_id = items_[item].stable_id;
        record.parts = item_parts(placed, item);
        read_states(backstage_item_state_fields, item_states(item), record.states);
        records.push_back(record);
    }
    return records;
}

void BackstageView::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), backstage_interactions.begin(), backstage_interactions.end());
}

} // namespace gui_forms
