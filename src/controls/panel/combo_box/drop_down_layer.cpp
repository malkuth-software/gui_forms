#include "drop_down_layer.hpp"

#include "../list_box/list_box_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <array>
#include <utility>
#include <algorithm>
#include <cmath>

namespace gui_forms {

DropDownList::DropDownList(StableId id) : ListBox(std::move(id)) {}

void DropDownList::initialize_control_tree() {
    scrollbar_ = make_control<ScrollBar>(StableId(std::string(stable_id().value()) + ".scrollbar"));
    (*scrollbar_).set_paint_plane(PaintPlane::overlay);
    (*scrollbar_).accessible_name = "Scroll choices";
    scroll_subscription_ = (*scrollbar_).value_changed().subscribe(
        *this, Delegate<double>::bind<DropDownList, &DropDownList::scroll_changed>(*this));
    add_child(scrollbar_);
}

void DropDownList::synchronize_scroll() {
    const std::size_t visible = std::max<std::size_t>(1, static_cast<std::size_t>(
        std::floor(std::max(0.0, local_bounds().height - 4) / (item_height.read() * effective_text_scale()))));
    const std::size_t maximum = items.read().size() > visible ? items.read().size() - visible : 0;
    synchronizing_ = true;
    (*scrollbar_).visible = maximum != 0;
    (*scrollbar_).set_range(0, static_cast<double>(std::max<std::size_t>(1, maximum)));
    (*scrollbar_).small_change = 1;
    (*scrollbar_).large_change = static_cast<double>(visible);
    const std::size_t top = std::min(top_index.read(), maximum);
    top_index = top;
    (*scrollbar_).value = static_cast<double>(top);
    synchronizing_ = false;
}

void DropDownList::arrange(Rect bounds) {
    ListBox::arrange(bounds);
    synchronize_scroll();
    set_child_layout(scrollbar_, {std::max(2.0, bounds.width - 20), 2, 18, std::max(0.0, bounds.height - 4)});
}

void DropDownList::scroll_changed(double value) {
    if (!synchronizing_) top_index = static_cast<std::size_t>(std::round(value));
}

// A press, release or move on the visible scroll bar is the bar's (a
// child); the rest is ListBox's, then the scroll bar follows the rows. The
// event is raised through Control after either.
void DropDownList::drop_down_pointer(MouseEventArgs& arguments) {
    if ((*scrollbar_).visible.read() && (*scrollbar_).absolute_bounds().contains(arguments.window_position) &&
        arguments.action != PointerAction::wheel) return;
    list_pointer(arguments);
    synchronize_scroll();
}

void DropDownList::on_mouse_down(MouseEventArgs& arguments) {
    drop_down_pointer(arguments);
    Control::on_mouse_down(arguments);
}

void DropDownList::on_mouse_up(MouseEventArgs& arguments) {
    drop_down_pointer(arguments);
    Control::on_mouse_up(arguments);
}

void DropDownList::on_mouse_move(MouseEventArgs& arguments) {
    drop_down_pointer(arguments);
    Control::on_mouse_move(arguments);
}

void DropDownList::on_mouse_wheel(MouseEventArgs& arguments) {
    drop_down_pointer(arguments);
    Control::on_mouse_wheel(arguments);
}

void DropDownList::on_mouse_leave() {
    list_pointer_left();
    synchronize_scroll();
    Control::on_mouse_leave();
}

void DropDownList::on_key_down(KeyEventArgs& arguments) {
    ListBox::on_key_down(arguments);
    synchronize_scroll();
}

bool DropDownList::on_semantic_child_action(std::string_view id, SemanticAction action,
                                           std::string_view value) {
    const bool result = ListBox::on_semantic_child_action(id, action, value);
    synchronize_scroll();
    return result;
}

void DropDownList::set_matches(std::vector<TextMatchRange> matches) {
    require_mutable();
    matches_ = std::move(matches);
    invalidate(Dirty::paint);
}

Rect DropDownList::match_bounds(std::size_t index, Rect row) const {
    if (index >= matches_.size() || index >= items.read().size()) return {};
    const TextMatchRange match = matches_[index];
    const std::string_view text = items.read()[index];
    if (match.length == 0U || match.offset >= text.size()) return {};
    // ListBox keeps its selected indices sorted.
    const std::span<const std::size_t> chosen = selected_indices();
    const bool selected = std::binary_search(chosen.begin(), chosen.end(), index);
    if (selected) return {};
    const std::size_t end = std::min(text.size(), match.offset + match.length);
    // The run's edges are the widths of the text before them, measured as
    // ListBox measures a row's label.
    const FontSpec label_font = effective_font(font.read());
    const ResolvedTextLayout before = resolve_text_layout_utf8(text.substr(0U, match.offset), label_font);
    const ResolvedTextLayout through = resolve_text_layout_utf8(text.substr(0U, end), label_font);
    const double left = row_text_left() + before.logical_size.width;
    const double right = row_text_left() + through.logical_size.width;
    const Rect bounds{left, row.y + 3.0, std::max(0.0, right - left), std::max(0.0, row.height - 6.0)};
    return bounds;
}

void DropDownList::paint_row_adornment(Painter& painter, std::size_t index, Rect row_bounds,
                                       bool, bool) const {
    const Rect bounds = match_bounds(index, row_bounds);
    if (bounds.empty()) return;
    // No vocabulary atom names a text match: it is a selection part of the
    // selection family in the hot state, the lighter plate the hovered row
    // uses, under the label's text (an owner vocabulary decision).
    AnatomyStateSet states;
    states.insert(AnatomyState::hot);
    const PartPaint part{.role = PartRole::selection, .bounds = bounds, .states = states,
                         .family = ControlVisualRole::selection};
    paint_part(painter, *this, part);
}

namespace {

// The matched text takes the hot state of its own.
constexpr std::array<AnatomyState, 1> match_part_states{{AnatomyState::hot}};

constexpr std::array<PartField<DropDownList::ItemParts>, 4> drop_down_item_part_fields{{
    {.declaration = {.name = "row", .role = PartRole::region, .composed_from = {}},
     .member = &DropDownList::ItemParts::row},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &DropDownList::ItemParts::label},
    {.declaration = {.name = "row_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &DropDownList::ItemParts::row_focus},
    {.declaration = {.name = "match", .role = PartRole::selection, .composed_from = {},
                     .states = match_part_states},
     .member = &DropDownList::ItemParts::match},
}};

constexpr std::array<AnatomyState, 3> drop_down_item_states{{
    AnatomyState::selected, AnatomyState::hot, AnatomyState::current,
}};

constexpr std::array<PartDeclaration, 4> drop_down_item_parts_declared =
    part_declarations(drop_down_item_part_fields);

constexpr std::array<ItemSystemDeclaration, 1> drop_down_list_item_systems{{
    {.name = "item", .parts = drop_down_item_parts_declared, .states = drop_down_item_states,
     .custom_states = {}},
}};

constexpr ControlAnatomy drop_down_list_anatomy{
    .control_id = "drop_down_list",
    .parts = list_box_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
    .item_systems = drop_down_list_item_systems,
};

} // namespace

const ControlAnatomy& DropDownList::anatomy() const noexcept {
    return drop_down_list_anatomy;
}

DropDownList::ItemParts DropDownList::drop_down_item_parts(std::size_t item) const {
    const ListBox::ItemParts base = list_item_parts(item);
    ItemParts record;
    record.row = base.row;
    record.label = base.label;
    record.row_focus = base.row_focus;
    // The row as paint lays it out, before ListBox clips it to the content.
    const Rect bounds = local_bounds();
    const double row_height = item_height.read() * effective_text_scale();
    const Rect row{2.0, 2.0 + static_cast<double>(item) * row_height,
                   std::max(0.0, bounds.width - 4.0), row_height};
    const Rect content{2.0, 2.0, std::max(0.0, bounds.width - 4.0), std::max(0.0, bounds.height - 4.0)};
    const Rect match = Rect::intersection(match_bounds(realized_item_index(item), row), content);
    if (!match.empty()) {
        record.match = PartGeometry::from_bounds(match);
        record.match.states.insert(AnatomyState::hot);
    }
    return record;
}

void DropDownList::publish_realized_item_geometry(std::size_t item,
                                                  std::span<PartGeometry> geometry) const {
    const ItemParts record = drop_down_item_parts(item);
    write_item_parts(anatomy().item_systems.front(), drop_down_item_part_fields, record, geometry);
}

DropDownLayer::DropDownLayer(StableId stable_id) : Panel(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    border_style = BorderStyle::none;
    set_background(Color::rgba(0, 0, 0, 0));
}

void DropDownLayer::on_mouse_down(MouseEventArgs& arguments) {
    if (arguments.button == PointerButton::primary) {
        dismissed_.emit();
        arguments.handled = true;
    }
    Control::on_mouse_down(arguments);
}

void DropDownLayer::on_key_preview(KeyEvent& event) {
    if (event.action == KeyAction::down &&
        event.physical_key == PhysicalKey::escape) {
        dismissed_.emit();
        event.handled = true;
    }
}

} // namespace gui_forms
