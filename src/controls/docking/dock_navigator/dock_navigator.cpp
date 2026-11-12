#include "gui_forms/controls/dock_navigator.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// Geometry in logical units at text scale 1.
constexpr double navigator_padding = 8.0;
constexpr double column_width = 200.0;
constexpr double column_gap = 8.0;
constexpr double heading_height = 22.0;
constexpr double heading_gap = 4.0;
constexpr double row_height = 20.0;
constexpr double label_inset = 8.0;

constexpr std::string_view tool_heading_text = "Active Tool Windows";
constexpr std::string_view document_heading_text = "Active Files";

Rect to_window(Rect absolute, Rect local) noexcept {
    const Rect result{absolute.x + local.x, absolute.y + local.y, local.width, local.height};
    return result;
}

DockNavigatorList other_list(DockNavigatorList list) noexcept {
    const DockNavigatorList other =
        list == DockNavigatorList::tools ? DockNavigatorList::documents : DockNavigatorList::tools;
    return other;
}

std::string_view list_suffix(DockNavigatorList list) noexcept {
    const std::string_view suffix = list == DockNavigatorList::tools ? ".tools" : ".documents";
    return suffix;
}

} // namespace

DockNavigator::DockNavigator(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = true;
    cursor = CursorKind::arrow;
    set_paint_plane(PaintPlane::control);
}

DockNavigator::~DockNavigator() = default;

// -- entries -------------------------------------------------------------------

void DockNavigator::set_entries(std::vector<DockNavigatorEntry> tools, std::vector<DockNavigatorEntry> documents,
                                DockNavigatorList list) {
    require_mutable();
    for (const DockNavigatorEntry& entry : tools) {
        if (entry.panel.value.empty()) throw std::invalid_argument("DockNavigator entries need a panel identity");
    }
    for (const DockNavigatorEntry& entry : documents) {
        if (entry.panel.value.empty()) throw std::invalid_argument("DockNavigator entries need a panel identity");
    }
    tools_ = std::move(tools);
    documents_ = std::move(documents);
    hot_.reset();
    list_ = list;
    if (list_entries(list_).empty()) list_ = other_list(list_);
    current_.reset();
    if (!list_entries(list_).empty()) current_ = 0U;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

const std::vector<DockNavigatorEntry>& DockNavigator::list_entries(DockNavigatorList list) const noexcept {
    if (list == DockNavigatorList::tools) return tools_;
    return documents_;
}

std::span<const DockNavigatorEntry> DockNavigator::entries(DockNavigatorList list) const noexcept {
    const std::vector<DockNavigatorEntry>& held = list_entries(list);
    const std::span<const DockNavigatorEntry> view(held);
    return view;
}

std::optional<DockPanelId> DockNavigator::current_panel() const {
    if (!current_) return std::nullopt;
    const DockPanelId panel = list_entries(list_)[*current_].panel;
    return panel;
}

bool DockNavigator::select(DockNavigatorList list, std::size_t index) {
    require_mutable();
    if (index >= list_entries(list).size()) return false;
    list_ = list;
    current_ = index;
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

bool DockNavigator::step(int direction) {
    require_mutable();
    const std::size_t count = list_entries(list_).size();
    if (count == 0U || !current_) return false;
    const std::size_t index = direction >= 0 ? (*current_ + 1U) % count : (*current_ + count - 1U) % count;
    const bool selected = select(list_, index);
    return selected;
}

bool DockNavigator::switch_list() {
    require_mutable();
    const DockNavigatorList other = other_list(list_);
    const std::size_t count = list_entries(other).size();
    if (count == 0U) return false;
    const std::size_t index = std::min(current_.value_or(0U), count - 1U);
    const bool selected = select(other, index);
    return selected;
}

bool DockNavigator::choose() {
    const std::optional<DockPanelId> panel = current_panel();
    if (!panel) return false;
    const DockNavigatorChoice choice{*panel, list_};
    const Control::Ptr keep = shared_from_this();
    chosen_.emit(choice);
    return true;
}

void DockNavigator::cancel() {
    const Control::Ptr keep = shared_from_this();
    cancelled_.emit();
}

// -- layout --------------------------------------------------------------------

FontSpec DockNavigator::row_font() const {
    const FontSpec font{FontRole::control, 10.5, 400, false, 0.12};
    return effective_font(font);
}

FontSpec DockNavigator::heading_font() const {
    const FontSpec font{FontRole::control, 10.5, 700, false, 0.12};
    return effective_font(font);
}

DockNavigator::Layout DockNavigator::layout_for(Size surface) const {
    const double scale = effective_text_scale();
    const double inset = navigator_padding * scale;
    const double column = std::max(0.0, (surface.width - inset * 2.0 - column_gap * scale) * 0.5);
    const double heading = heading_height * scale;
    const double top = inset + heading + heading_gap * scale;
    const double rows = std::max(0.0, surface.height - top - inset);
    Layout placed{};
    placed.face = {0.0, 0.0, surface.width, surface.height};
    placed.tool_heading = {inset, inset, column, heading};
    placed.document_heading = {inset + column + column_gap * scale, inset, column, heading};
    placed.tool_rows = {inset, top, column, rows};
    placed.document_rows = {placed.document_heading.x, top, column, rows};
    placed.row_height = row_height * scale;
    return placed;
}

Rect DockNavigator::row_bounds(const Layout& placed, DockNavigatorList list, std::size_t index) const {
    const Rect column = list == DockNavigatorList::tools ? placed.tool_rows : placed.document_rows;
    const double y = column.y + static_cast<double>(index) * placed.row_height;
    const Rect bounds{column.x, y, column.width, placed.row_height};
    return bounds;
}

Rect DockNavigator::entry_label_bounds(Rect row) const {
    const double inset = label_inset * effective_text_scale();
    const Rect label{row.x + inset, row.y, std::max(0.0, row.width - inset * 2.0), row.height};
    return label;
}

Size DockNavigator::measure(Size) {
    const double scale = effective_text_scale();
    const std::size_t rows = std::max<std::size_t>(1U, std::max(tools_.size(), documents_.size()));
    const double width = (navigator_padding * 2.0 + column_width * 2.0 + column_gap) * scale;
    const double height =
        (navigator_padding * 2.0 + heading_height + heading_gap) * scale + static_cast<double>(rows) * row_height * scale;
    const Size wanted{width, height};
    return wanted;
}

void DockNavigator::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
}

// -- states --------------------------------------------------------------------

AnatomyStateSet DockNavigator::row_states(DockNavigatorList list, std::size_t index) const {
    AnatomyStateSet states{};
    if (hot_ && (*hot_).list == list && (*hot_).index == index) states.insert(AnatomyState::hot);
    if (current_ && list_ == list && *current_ == index) states.insert(AnatomyState::selected);
    return states;
}

// -- paint ---------------------------------------------------------------------

// Paints from the entries and the current row: no allocation per row (a
// row's label is its entry's title, borrowed for the call).
void DockNavigator::on_paint(Painter& painter, Rect) {
    const Rect surface = client_rectangle();
    const Layout placed = layout_for(Size{surface.width, surface.height});
    PartPaint part{};
    part.family = ControlVisualRole::panel;
    part.role = PartRole::face;
    part.bounds = placed.face;
    paint_part(painter, *this, part);
    part.font = heading_font();
    part.role = PartRole::caption;
    part.bounds = placed.tool_heading;
    part.text = tool_heading_text;
    paint_part(painter, *this, part);
    part.bounds = placed.document_heading;
    part.text = document_heading_text;
    paint_part(painter, *this, part);
    part.font = row_font();
    part.family = ControlVisualRole::menu_item;
    const std::array<DockNavigatorList, 2> lists{DockNavigatorList::tools, DockNavigatorList::documents};
    for (const DockNavigatorList list : lists) {
        const std::vector<DockNavigatorEntry>& held = list_entries(list);
        for (std::size_t index = 0; index < held.size(); ++index) {
            const Rect row = row_bounds(placed, list, index);
            part.states = row_states(list, index);
            part.role = PartRole::face;
            part.bounds = row;
            part.text = {};
            paint_part(painter, *this, part);
            part.role = PartRole::label;
            part.bounds = entry_label_bounds(row);
            part.text = held[index].title;
            paint_part(painter, *this, part);
        }
    }
}

// -- input ---------------------------------------------------------------------

std::optional<DockNavigator::Row> DockNavigator::row_at(Point local) const {
    const Rect surface = client_rectangle();
    const Layout placed = layout_for(Size{surface.width, surface.height});
    const std::array<DockNavigatorList, 2> lists{DockNavigatorList::tools, DockNavigatorList::documents};
    for (const DockNavigatorList list : lists) {
        const std::size_t count = list_entries(list).size();
        for (std::size_t index = 0; index < count; ++index) {
            if (!row_bounds(placed, list, index).contains(local)) continue;
            const Row row{list, index};
            return row;
        }
    }
    return std::nullopt;
}

void DockNavigator::set_hot(std::optional<Row> row) {
    const bool same = hot_.has_value() == row.has_value() &&
                      (!row || ((*hot_).list == (*row).list && (*hot_).index == (*row).index));
    if (same) return;
    hot_ = row;
    invalidate(Dirty::paint);
}

void DockNavigator::on_pointer(PointerEvent& event) {
    if (event.action == PointerAction::leave) {
        set_hot(std::nullopt);
        return;
    }
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    const std::optional<Row> row = row_at(local);
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        set_hot(row);
        return;
    }
    if (event.button != PointerButton::primary) return;
    // A press makes its row current; the release over the same row chooses.
    if (event.action == PointerAction::down) {
        if (row) static_cast<void>(select((*row).list, (*row).index));
        event.handled = true;
        return;
    }
    if (event.action != PointerAction::up) return;
    event.handled = true;
    if (row && current_ && (*row).list == list_ && (*row).index == *current_) static_cast<void>(choose());
}

void DockNavigator::on_key(KeyEvent& event) {
    if (event.action == KeyAction::up) {
        // The release modifier was let go: every host reports a key-up whose
        // modifiers no longer hold it.
        if (release_modifier.read() == Modifier::none || has_modifier(event.modifiers, release_modifier.read())) return;
        event.handled = true;
        if (!choose()) cancel();
        return;
    }
    const bool shifted = has_modifier(event.modifiers, Modifier::shift);
    if (event.physical_key == PhysicalKey::tab) {
        static_cast<void>(step(shifted ? -1 : 1));
    } else if (event.physical_key == PhysicalKey::down) {
        static_cast<void>(step(1));
    } else if (event.physical_key == PhysicalKey::up) {
        static_cast<void>(step(-1));
    } else if (event.physical_key == PhysicalKey::left) {
        if (list_ == DockNavigatorList::documents) static_cast<void>(switch_list());
    } else if (event.physical_key == PhysicalKey::right) {
        if (list_ == DockNavigatorList::tools) static_cast<void>(switch_list());
    } else if (event.physical_key == PhysicalKey::enter) {
        if (!choose()) cancel();
    } else if (event.physical_key == PhysicalKey::escape) {
        cancel();
    } else {
        return;
    }
    event.handled = true;
}

// -- semantics -----------------------------------------------------------------

std::string DockNavigator::entry_identity(DockNavigatorList list, const DockPanelId& panel) const {
    std::string identity(stable_id().value());
    identity += list_suffix(list);
    identity += '.';
    identity += panel.value;
    return identity;
}

SemanticDescriptor DockNavigator::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read().empty() ? "Active windows" : accessible_name.read();
    descriptor.description = accessible_description.read();
    const std::optional<DockPanelId> panel = current_panel();
    if (panel) {
        const std::vector<DockNavigatorEntry>& held = list_entries(list_);
        descriptor.value = held[*current_].title;
    }
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

// Two lists, each with its rows as list items; the current row is selected.
// A row's select makes it current and its press chooses it.
std::vector<SemanticNode> DockNavigator::semantic_virtual_children() const {
    std::vector<SemanticNode> lists{};
    const Rect surface = client_rectangle();
    const Layout placed = layout_for(Size{surface.width, surface.height});
    const Rect absolute = absolute_bounds();
    const std::array<DockNavigatorList, 2> kinds{DockNavigatorList::tools, DockNavigatorList::documents};
    const std::array<std::string_view, 2> names{tool_heading_text, document_heading_text};
    const std::string own(stable_id().value());
    lists.reserve(kinds.size());
    for (std::size_t kind = 0; kind < kinds.size(); ++kind) {
        const DockNavigatorList list = kinds[kind];
        const std::vector<DockNavigatorEntry>& held = list_entries(list);
        SemanticNode node{};
        node.stable_id = own + std::string(list_suffix(list));
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::list;
        node.name = std::string(names[kind]);
        node.bounds = to_window(absolute, list == DockNavigatorList::tools ? placed.tool_rows : placed.document_rows);
        node.states = SemanticState::visible | SemanticState::enabled;
        node.children.reserve(held.size());
        for (std::size_t index = 0; index < held.size(); ++index) {
            SemanticNode row{};
            row.stable_id = entry_identity(list, held[index].panel);
            row.runtime_id = virtual_semantic_runtime_id(row.stable_id);
            row.role = SemanticRole::list_item;
            row.name = held[index].title;
            row.position_in_set = index + 1U;
            row.set_size = held.size();
            row.bounds = to_window(absolute, row_bounds(placed, list, index));
            row.states = SemanticState::visible | SemanticState::enabled | SemanticState::focusable;
            if (current_ && list_ == list && *current_ == index) row.states |= SemanticState::selected;
            row.actions = {SemanticAction::select, SemanticAction::press};
            node.children.push_back(std::move(row));
        }
        lists.push_back(std::move(node));
    }
    return lists;
}

bool DockNavigator::on_semantic_child_action(std::string_view stable_id, SemanticAction action, std::string_view) {
    if (action != SemanticAction::select && action != SemanticAction::press) return false;
    const std::array<DockNavigatorList, 2> kinds{DockNavigatorList::tools, DockNavigatorList::documents};
    for (const DockNavigatorList list : kinds) {
        const std::vector<DockNavigatorEntry>& held = list_entries(list);
        for (std::size_t index = 0; index < held.size(); ++index) {
            if (entry_identity(list, held[index].panel) != stable_id) continue;
            static_cast<void>(select(list, index));
            if (action == SemanticAction::press) static_cast<void>(choose());
            return true;
        }
    }
    return false;
}

// -- anatomy -------------------------------------------------------------------

namespace {

constexpr std::array<PartField<DockNavigator::Parts>, 5> navigator_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &DockNavigator::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &DockNavigator::Parts::face},
    {.declaration = {.name = "tool_heading", .role = PartRole::caption, .composed_from = {}},
     .member = &DockNavigator::Parts::tool_heading},
    {.declaration = {.name = "document_heading", .role = PartRole::caption, .composed_from = {}},
     .member = &DockNavigator::Parts::document_heading},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &DockNavigator::Parts::focus},
}};

constexpr std::array<PartDeclaration, 5> navigator_parts = part_declarations(navigator_part_fields);
constexpr std::array<AnatomyState, 5> navigator_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr std::array<PartField<DockNavigator::ItemParts>, 2> entry_part_fields{{
    {.declaration = {.name = "entry_face", .role = PartRole::face, .composed_from = "menu_item"},
     .member = &DockNavigator::ItemParts::entry_face},
    {.declaration = {.name = "entry_label", .role = PartRole::label, .composed_from = {}},
     .member = &DockNavigator::ItemParts::entry_label},
}};
constexpr std::array<PartDeclaration, 2> entry_parts = part_declarations(entry_part_fields);
constexpr std::array<StateField<DockNavigator::ItemStates>, 2> entry_state_fields{{
    {AnatomyState::hot, &DockNavigator::ItemStates::hot},
    {AnatomyState::selected, &DockNavigator::ItemStates::selected},
}};
constexpr std::array<AnatomyState, 2> entry_states = state_declarations(entry_state_fields);
constexpr std::array<ItemSystemDeclaration, 1> navigator_item_systems{{
    {.name = "entry", .parts = entry_parts, .states = entry_states, .custom_states = {}},
}};

constexpr ControlAnatomy navigator_anatomy{
    .control_id = "dock_navigator",
    .parts = navigator_parts,
    .states = navigator_states,
    .custom_states = {},
    .item_systems = navigator_item_systems,
};

// The events DockNavigator adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> navigator_interactions{{
    {.name = "chosen", .payload = "DockNavigatorChoice", .mutable_payload = false},
    {.name = "cancelled", .payload = "", .mutable_payload = false},
}};

} // namespace

const ControlAnatomy& DockNavigator::anatomy() const noexcept {
    return navigator_anatomy;
}

void DockNavigator::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect surface = client_rectangle();
    const Layout placed = layout_for(Size{surface.width, surface.height});
    Parts record{};
    record.region = PartGeometry::from_bounds(surface);
    record.face = PartGeometry::from_bounds(placed.face);
    record.tool_heading = PartGeometry::from_bounds(placed.tool_heading);
    record.document_heading = PartGeometry::from_bounds(placed.document_heading);
    record.focus = PartGeometry::from_bounds(surface);
    write_parts(anatomy(), navigator_part_fields, record, geometry);
}

DockNavigator::Parts DockNavigator::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record{};
    read_parts(anatomy(), navigator_part_fields, published.parts(), record);
    return record;
}

std::size_t DockNavigator::realized_item_count() const {
    const std::size_t count = tools_.size() + documents_.size();
    return count;
}

DockNavigator::Row DockNavigator::realized_row(std::size_t item) const {
    Row row{};
    if (item < tools_.size()) {
        row.list = DockNavigatorList::tools;
        row.index = item;
        return row;
    }
    row.list = DockNavigatorList::documents;
    row.index = item - tools_.size();
    return row;
}

std::string DockNavigator::realized_item_stable_id(std::size_t item) const {
    if (item >= realized_item_count()) throw std::out_of_range("DockNavigator item is out of range");
    const Row row = realized_row(item);
    const std::string identity = entry_identity(row.list, list_entries(row.list)[row.index].panel);
    return identity;
}

AnatomyStateSet DockNavigator::realized_item_anatomy_states(std::size_t item) const {
    if (item >= realized_item_count()) throw std::out_of_range("DockNavigator item is out of range");
    const Row row = realized_row(item);
    const AnatomyStateSet states = row_states(row.list, row.index);
    return states;
}

void DockNavigator::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    const Row row = realized_row(item);
    const Rect surface = client_rectangle();
    const Layout placed = layout_for(Size{surface.width, surface.height});
    const Rect bounds = row_bounds(placed, row.list, row.index);
    ItemParts record{};
    record.entry_face = PartGeometry::from_bounds(bounds);
    record.entry_label = PartGeometry::from_bounds(entry_label_bounds(bounds));
    write_declared_parts(entry_parts, entry_part_fields, record, geometry);
}

std::vector<DockNavigator::Item> DockNavigator::realized_items() const {
    std::vector<Item> items{};
    const std::size_t count = realized_item_count();
    items.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const Row row = realized_row(item);
        Item record{};
        record.list = row.list;
        record.index = row.index;
        record.panel = list_entries(row.list)[row.index].panel;
        record.stable_id = realized_item_stable_id(item);
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        read_declared_parts(entry_parts, entry_part_fields, published.parts(), record.parts);
        const AnatomyStateSet states = realized_item_anatomy_states(item);
        read_states(entry_state_fields, states, record.states);
        items.push_back(std::move(record));
    }
    return items;
}

void DockNavigator::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), navigator_interactions.begin(), navigator_interactions.end());
}

// property_codemod begin DockNavigator (tools/property_families/dock_navigator.json; generated, do not edit)

void DockNavigator::prepare_write(const decltype(release_modifier)&, Modifier& incoming) {
    require_mutable();
    const bool accepted = incoming == Modifier::none || incoming == Modifier::control ||
                          incoming == Modifier::alt || incoming == Modifier::meta;
    if (!accepted) throw std::invalid_argument("DockNavigator release modifier must be one of control, alt and meta");
}

void DockNavigator::written(const decltype(release_modifier)&) {}

// property_codemod end DockNavigator

} // namespace gui_forms
