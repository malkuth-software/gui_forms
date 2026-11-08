#include "gui_forms/controls/panel/list_box/list_box.hpp"
#include "list_box_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include "../input_control_utilities.hpp"
#include "../text_items_property.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/detail/algorithm/binary_search.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// The ListSelectionMode enumeration, numbered as declared. Static storage;
// any thread may read it.
std::shared_ptr<const PropertyEnumDescriptor> list_selection_mode_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "list_selection_mode", {{"one", 0}, {"multiple_extended", 1}}, false});
    return value;
}

} // namespace
using namespace input_control_detail;

ListBox::ListBox(StableId stable_id) : Panel(std::move(stable_id)) {
    set_paint_plane(PaintPlane::control);
    border_style = BorderStyle::sunken;
    set_focusable(true);
    PropertyRegistration items_registration;
    items_registration.descriptor = text_items_property::descriptor("Ordered text items the list shows.");
    items_registration.get =
        detail::BoundMemberFunction<BindingValue (ListBox::*)() const>(*this, &ListBox::items_property_value);
    items_registration.set =
        detail::BoundMemberFunction<void (ListBox::*)(const BindingValue&)>(*this, &ListBox::set_items_property);
    items_registration.reset = detail::BoundMemberFunction<void (ListBox::*)()>(*this, &ListBox::reset_items_property);
    items_registration.should_serialize = detail::BoundMemberFunction<bool (ListBox::*)() const noexcept>(
        *this, &ListBox::should_serialize_items_property);
    define_bindable_property(std::move(items_registration));
}

BindingValue ListBox::items_property_value() const {
    BindingValue value = text_items_property::value_of(items.read());
    return value;
}

void ListBox::set_items_property(const BindingValue& value) {
    std::vector<std::string> incoming = text_items_property::items_of(value, "ListBox");
    items = std::move(incoming);
}

void ListBox::reset_items_property() {
    items.clear();
}

bool ListBox::should_serialize_items_property() const noexcept {
    return !items.read().empty();
}

// A refused edit throws here, before anything changes: the list is
// mutable, every incoming item is UTF-8, and the selection the edit leaves
// is made (the only allocation) before the subclass readies its own state.
void ListBox::prepare_edit(const decltype(items)&, const CollectionEdit& edit,
                           std::span<std::string> incoming) {
    require_mutable();
    for (const std::string& item : incoming) {
        if (!validate_utf8(item).valid()) {
            throw std::invalid_argument("ListBox items must be valid UTF-8");
        }
    }
    edit_selection_.clear();
    edit_selection_.reserve(selected_.size());
    for (const std::size_t selected : selected_) {
        const std::optional<std::size_t> kept = edit.remap(selected);
        if (kept) edit_selection_.push_back(*kept);
    }
    on_items_changing(edit);
}

// The edit is stored. Nothing here throws before the change event: the
// stable identities follow the edit (an insertion has no identity for its
// item, so the list returns to index-derived identities, as an assignment
// does), the selection is swapped in, and the anchor, active and hovered
// rows are remapped.
void ListBox::written(const decltype(items)&, const CollectionEdit& edit) {
    const std::size_t count = items.read().size();
    switch (edit.kind()) {
    case CollectionEditKind::erase:
        if (!item_stable_ids_.empty()) {
            const std::vector<std::string>::iterator first =
                item_stable_ids_.begin() + static_cast<std::ptrdiff_t>(edit.index());
            item_stable_ids_.erase(first, first + static_cast<std::ptrdiff_t>(edit.count()));
        }
        break;
    case CollectionEditKind::replace:
        break;
    case CollectionEditKind::assign:
    case CollectionEditKind::insert:
    default:
        item_stable_ids_.clear();
        break;
    }
    const bool selection_changed = edit_selection_ != selected_;
    if (selection_changed) std::swap(selected_, edit_selection_);
    if (active_index_) active_index_ = edit.remap(*active_index_);
    if (anchor_index_) anchor_index_ = edit.remap(*anchor_index_);
    if (hovered_index_) hovered_index_ = edit.remap(*hovered_index_);
    const std::size_t top_before = top_index_;
    top_index_ = count == 0U ? 0U : std::min(top_index_, count - 1U);
    on_items_changed(edit);
    // The rows are repainted at the flush (settle_items_damage, from
    // arrange); a structural edit also changes what the list measures.
    Dirty effects = Dirty::arrange | Dirty::semantics;
    if (edit.structural()) effects |= Dirty::measure;
    if (selection_changed || top_before != top_index_) effects |= Dirty::paint;
    invalidate(effects);
    if (selection_changed) {
        publish_change(selection_changed_,
                       ListSelectionChange{edit_selection_, selected_, active_index_});
    }
}

void ListBox::on_items_changing(const CollectionEdit&) {}

void ListBox::on_items_changed(const CollectionEdit&) noexcept {}

void ListBox::set_item_stable_ids(std::vector<std::string> stable_ids) {
    require_mutable();
    if (stable_ids.size() != items.read().size()) {
        throw std::invalid_argument(
            "ListBox stable item IDs must match the item count");
    }
    std::vector<std::string> sorted = stable_ids;
    for (const std::string& stable_id : sorted) {
        if (stable_id.empty() || !validate_utf8(stable_id).valid()) {
            throw std::invalid_argument(
                "ListBox stable item IDs must be nonempty valid UTF-8");
        }
    }
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
        throw std::invalid_argument("ListBox stable item IDs must be unique");
    }
    item_stable_ids_ = std::move(stable_ids);
    invalidate(Dirty::semantics);
}

std::string ListBox::item_stable_id(std::size_t index) const {
    if (index >= items.read().size()) {
        throw std::out_of_range(
            "ListBox item stable ID index is outside the collection");
    }
    if (!item_stable_ids_.empty()) return item_stable_ids_[index];
    return std::string(stable_id().value()) + ".item." +
           std::to_string(index);
}

void ListBox::prepare_write(const decltype(selection_mode)&, ListSelectionMode&) {
    require_mutable();
}

// Narrowing to one item keeps the active (or first) selected row; dropping
// the others raises selection_changed before the repaint request.
void ListBox::written(const decltype(selection_mode)&) {
    if (selection_mode.read() == ListSelectionMode::one && selected_.size() > 1U) {
        const std::size_t retained = active_index_.value_or(selected_.front());
        apply_selection({retained}, retained);
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

std::optional<std::size_t> ListBox::selected_index() const noexcept {
    return selected_.empty() ? std::optional<std::size_t>{}
                             : std::optional<std::size_t>{selected_.front()};
}

void ListBox::apply_selection(std::vector<std::size_t> selection,
                              std::optional<std::size_t> active) {
    std::sort(selection.begin(), selection.end());
    selection.erase(std::unique(selection.begin(), selection.end()), selection.end());
    const std::vector<std::size_t> previous = selected_;
    const std::optional<std::size_t> previous_active = active_index_;
    selected_ = std::move(selection);
    active_index_ = active;
    if (previous == selected_ && previous_active == active_index_) return;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(selection_changed_,
                   ListSelectionChange{previous, selected_, active_index_});
}

void ListBox::select_index(std::size_t index, bool extend, bool toggle) {
    require_mutable();
    if (index >= items.read().size()) {
        throw std::out_of_range("ListBox selection index is outside the collection");
    }
    if (selection_mode.read() == ListSelectionMode::one) {
        apply_selection({index}, index);
        anchor_index_ = index;
    } else if (extend && anchor_index_) {
        std::vector<std::size_t> next;
        const std::size_t first = std::min(*anchor_index_, index);
        const std::size_t last = std::max(*anchor_index_, index);
        next.reserve(last - first + 1U);
        for (std::size_t item = first; item <= last; ++item) next.push_back(item);
        apply_selection(std::move(next), index);
    } else if (toggle) {
        std::vector<std::size_t> next = selected_;
        const std::vector<std::size_t>::iterator found =
            std::find(next.begin(), next.end(), index);
        if (found == next.end()) next.push_back(index);
        else next.erase(found);
        apply_selection(std::move(next), index);
        anchor_index_ = index;
    } else {
        apply_selection({index}, index);
        anchor_index_ = index;
    }
    ensure_visible(index);
}

void ListBox::clear_selection() {
    require_mutable();
    apply_selection({}, {});
    anchor_index_.reset();
}

std::size_t ListBox::read_view(const decltype(top_index)&) const {
    return top_index_;
}

void ListBox::write_view(const decltype(top_index)&, std::size_t incoming) {
    require_mutable();
    if (items.read().empty()) {
        if (incoming != 0U) throw std::out_of_range("empty ListBox top index must be zero");
    } else if (incoming >= items.read().size()) {
        throw std::out_of_range("ListBox top index is outside the collection");
    }
    if (top_index_ == incoming) return;
    top_index_ = incoming;
    invalidate(Dirty::paint | Dirty::semantics);
}

std::size_t ListBox::visible_row_count() const noexcept {
    double height = local_bounds().height;
    if (height <= 4.0) {
        height = requested_bounds().height;
    }
    const double row_height = item_height.read() * effective_text_scale();
    return std::max<std::size_t>(1U, static_cast<std::size_t>(
        std::floor(std::max(0.0, height - 4.0) / row_height)));
}

void ListBox::ensure_visible(std::size_t index) {
    if (local_bounds().height <= 4.0 && requested_bounds().height <= 4.0) {
        return;
    }
    const std::size_t visible = visible_row_count();
    if (index < top_index_) top_index_ = index;
    else if (index >= top_index_ + visible) top_index_ = index - visible + 1U;
}

void ListBox::arrange(Rect final_bounds) {
    const std::size_t top_before = top_index_;
    Panel::arrange(final_bounds);
    if (active_index_) {
        ensure_visible(*active_index_);
    }
    settle_items_damage(top_before != top_index_);
}

// The design's flush intersection: the rows shown are [top, top + visible
// + 1) (the last one partly). A structural edit at or above the last shown
// row repaints the shown rows from it to the bottom of the content; one
// below the window changes only what is measured. A replaced row is
// repainted when shown and above the structural watermark.
void ListBox::settle_items_damage(bool window_moved) {
    settled_rows_.clear();
    const std::optional<std::size_t> structural_from =
        TextItemsCapability<ListBox>::take_dirty(*this, settled_rows_);
    if (window_moved) {
        invalidate(Dirty::paint);
        return;
    }
    if (!structural_from && settled_rows_.empty()) return;
    const Rect bounds = local_bounds();
    if (bounds.empty()) return;
    const double row_height = item_height.read() * effective_text_scale();
    const double row_width = std::max(0.0, bounds.width - 4.0);
    const double content_bottom = std::max(2.0, bounds.height - 2.0);
    const std::size_t window_end = top_index_ + visible_row_count() + 1U;
    std::size_t replaced_end = window_end;
    if (structural_from && *structural_from < window_end) {
        const std::size_t first = std::max(*structural_from, top_index_);
        const double top = 2.0 + static_cast<double>(first - top_index_) * row_height;
        if (top < content_bottom) invalidate(Rect{2.0, top, row_width, content_bottom - top});
        replaced_end = first;
    }
    for (const std::uint32_t row : settled_rows_) {
        const std::size_t index = static_cast<std::size_t>(row);
        if (index < top_index_) continue;
        if (index >= replaced_end) break;
        const double top = 2.0 + static_cast<double>(index - top_index_) * row_height;
        invalidate(Rect{2.0, top, row_width, row_height});
    }
}

std::optional<std::size_t> ListBox::index_at(Point absolute) const noexcept {
    const Rect bounds = absolute_bounds();
    if (!bounds.contains(absolute)) return {};
    const double local_y = absolute.y - bounds.y - 2.0;
    if (local_y < 0.0) return {};
    const double row_height = item_height.read() * effective_text_scale();
    const std::size_t index = top_index_ +
        static_cast<std::size_t>(std::floor(local_y / row_height));
    return index < items.read().size() ? std::optional<std::size_t>{index}
                                 : std::optional<std::size_t>{};
}

// The well, each row's selection plate, its text and the active row's focus
// ring are parts the theme's engine draws. A row is hot under the pointer
// and selected when selected. A background or style override supplies the
// well's ground; a style override the plates' grounds and the text (T4-1).
void ListBox::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    AnatomyStateSet states = anatomy_states();
    states.erase(AnatomyState::hot);
    const bool overridden = has_style_override();
    PartPaint well;
    well.family = ControlVisualRole::editor;
    well.role = PartRole::well;
    well.bounds = bounds;
    well.states = states;
    if (has_background_override() || overridden) well.authored_background = background();
    well.key = PartKey{"well", {}, {}};
    paint_part(painter, *this, well);
    const FontSpec row_font = text_font(font.read());
    const double row_height = item_height.read() * effective_text_scale();
    painter.save();
    painter.clip_rect({2.0, 2.0, std::max(0.0, bounds.width - 4.0),
                       std::max(0.0, bounds.height - 4.0)});
    const std::size_t visible = visible_row_count() + 1U;
    const std::size_t end = std::min(items.read().size(), top_index_ + visible);
    for (std::size_t index = top_index_; index < end; ++index) {
        const double y = 2.0 + static_cast<double>(index - top_index_) * row_height;
        const bool selected = detail::binary_search_contains(
            std::span<const std::size_t>(selected_), index);
        const bool hovered = hovered_index_ == index;
        const Rect row{2.0, y, std::max(0.0, bounds.width - 4.0), row_height};
        AnatomyStateSet row_states = states;
        if (hovered) row_states.insert(AnatomyState::hot);
        if (selected) row_states.insert(AnatomyState::selected);
        if (selected || hovered) {
            PartPaint plate;
            plate.family = ControlVisualRole::selection;
            plate.role = PartRole::selection;
            plate.bounds = row;
            plate.states = row_states;
            if (overridden) {
                const Color selected_fill = focused_ ? style().accent : style().accent_light;
                plate.authored_background = selected ? selected_fill : style().face_light;
            }
            paint_part(painter, *this, plate);
        }
        paint_row_adornment(painter, index, row, selected, focused_);
        PartPaint text;
        text.family = selected || hovered ? ControlVisualRole::selection : ControlVisualRole::editor;
        text.role = PartRole::content;
        text.states = row_states;
        text.text = items.read()[index];
        text.font = row_font;
        if (overridden) text.authored_foreground = selected && focused_ ? style().highlight : style().text;
        const double left = row_text_left();
        paint_text_part_at(painter, *this, text,
                           {left, y + std::max(row_font.size, row_height * 0.5 + 4.0)},
                           std::max(0.0, row.x + row.width - left));
        if (focused_ && active_index_ == index) {
            PartPaint ring;
            ring.family = ControlVisualRole::selection;
            ring.role = PartRole::focus;
            ring.bounds = {3.0, y + 1.0, std::max(0.0, bounds.width - 6.0), std::max(0.0, row_height - 2.0)};
            ring.states = row_states;
            ring.states.insert(AnatomyState::focus_visible);
            paint_part(painter, *this, ring);
        }
    }
    painter.restore();
}

double ListBox::row_text_left() const noexcept {
    return 8.0;
}

void ListBox::paint_row_adornment(Painter&, std::size_t, Rect, bool,
                                  bool) const {}

void ListBox::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    if (event.action == PointerAction::move) {
        const std::optional<std::size_t> next = index_at(event.position);
        if (next != hovered_index_) {
            hovered_index_ = next;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.action == PointerAction::leave) {
        hovered_index_.reset();
        invalidate(Dirty::paint);
        return;
    }
    if (event.action == PointerAction::wheel) {
        if (!items.read().empty() && event.wheel_delta.y != 0.0) {
            const int direction = event.wheel_delta.y > 0.0 ? -1 : 1;
            const std::ptrdiff_t next = std::clamp<std::ptrdiff_t>(
                static_cast<std::ptrdiff_t>(top_index_) + direction * 3,
                0, static_cast<std::ptrdiff_t>(items.read().size() - 1U));
            top_index = static_cast<std::size_t>(next);
            event.handled = true;
        }
        return;
    }
    if (event.action == PointerAction::down && event.button == PointerButton::primary) {
        if (window() != nullptr) static_cast<void>((*window()).request_focus(shared_from_this()));
        if (const std::optional<std::size_t> index = index_at(event.position)) {
            select_index(*index, includes(event.modifiers, Modifier::shift),
                         command_modifier(event.modifiers));
            event.handled = true;
        }
    } else if (event.action == PointerAction::up &&
               event.button == PointerButton::primary) {
        if (const std::optional<std::size_t> index = index_at(event.position);
            index && active_index_ == index) {
            item_activated_.emit(*index);
            event.handled = true;
        }
    }
}

void ListBox::on_key(KeyEvent& event) {
    if (!focused_ || !enabled.read() || event.action != KeyAction::down || items.read().empty()) return;
    if (event.physical_key == PhysicalKey::enter && active_index_) {
        item_activated_.emit(*active_index_);
        event.handled = true;
        return;
    }
    std::optional<std::size_t> next;
    const std::size_t current = active_index_.value_or(0U);
    if (event.physical_key == PhysicalKey::up) next = current == 0U ? 0U : current - 1U;
    else if (event.physical_key == PhysicalKey::down) next = std::min(current + 1U, items.read().size() - 1U);
    else if (event.physical_key == PhysicalKey::home) next = 0U;
    else if (event.physical_key == PhysicalKey::end) next = items.read().size() - 1U;
    else if (event.physical_key == PhysicalKey::page_up) next = current > visible_row_count() ? current - visible_row_count() : 0U;
    else if (event.physical_key == PhysicalKey::page_down) next = std::min(current + visible_row_count(), items.read().size() - 1U);
    if (next) {
        select_index(*next, includes(event.modifiers, Modifier::shift), false);
        event.handled = true;
    }
}

void ListBox::on_focus_changed(bool focused) {
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

SemanticDescriptor ListBox::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::list;
    if (selection_mode.read() == ListSelectionMode::multiple_extended) {
        descriptor.states |= SemanticState::multiselectable;
    }
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    if (const std::optional<std::size_t> selected = selected_index()) {
        descriptor.value = items.read()[*selected];
    } else if (!selected_.empty()) {
        descriptor.value = std::to_string(selected_.size()) + " items selected";
    }
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> ListBox::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes;
    nodes.reserve(items.read().size());
    const Rect list_bounds = absolute_bounds();
    const std::size_t visible_count = visible_row_count();
    const std::size_t visible_end = std::min(items.read().size(), top_index_ + visible_count);
    const double row_height = item_height.read() * effective_text_scale();
    for (std::size_t index = 0; index < items.read().size(); ++index) {
        SemanticNode node;
        node.stable_id = item_stable_id(index);
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::list_item;
        node.name = items.read()[index];
        node.value = items.read()[index];
        node.position_in_set = index + 1U;
        node.set_size = items.read().size();
        const double row_y = list_bounds.y + 2.0 +
            (static_cast<double>(index) - static_cast<double>(top_index_)) *
                row_height;
        node.bounds = Rect::intersection(
            {list_bounds.x + 2.0, row_y, std::max(0.0, list_bounds.width - 4.0),
             row_height},
            list_bounds);
        if (effectively_enabled()) node.states |= SemanticState::enabled;
        node.states |= SemanticState::focusable;
        if (index >= top_index_ && index < visible_end) {
            node.states |= SemanticState::visible;
        }
        if (detail::binary_search_contains(
                std::span<const std::size_t>(selected_), index)) {
            node.states |= SemanticState::selected;
        }
        if (focused_ && active_index_ == index) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::focus, SemanticAction::select,
                        SemanticAction::press};
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool ListBox::on_semantic_child_action(std::string_view child_stable_id,
                                       SemanticAction action,
                                       std::string_view) {
    std::size_t index{};
    if (!item_stable_ids_.empty()) {
        const std::vector<std::string>::iterator found =
            std::find(item_stable_ids_.begin(),
                                     item_stable_ids_.end(), child_stable_id);
        if (found == item_stable_ids_.end()) return false;
        index = static_cast<std::size_t>(
            std::distance(item_stable_ids_.begin(), found));
    } else {
        const std::string prefix = std::string(stable_id().value()) + ".item.";
        if (!child_stable_id.starts_with(prefix)) return false;
        const std::string_view suffix = child_stable_id.substr(prefix.size());
        const std::from_chars_result parsed = std::from_chars(
            suffix.data(), suffix.data() + suffix.size(), index);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != suffix.data() + suffix.size() ||
            index >= items.read().size()) return false;
    }
    if (action != SemanticAction::focus && action != SemanticAction::select &&
        action != SemanticAction::press) return false;
    if (window() != nullptr) {
        static_cast<void>((*window()).request_focus(shared_from_this()));
    }
    select_index(index, false, false);
    if (action == SemanticAction::press && is_alive()) {
        item_activated_.emit(index);
    }
    return true;
}

namespace {

constexpr std::array<PartField<ListBox::ItemParts>, 3> list_item_part_fields{{
    {.declaration = {.name = "row", .role = PartRole::region, .composed_from = {}},
     .member = &ListBox::ItemParts::row},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &ListBox::ItemParts::label},
    {.declaration = {.name = "row_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &ListBox::ItemParts::row_focus},
}};

constexpr std::array<StateField<ListBox::ItemStates>, 3> list_item_state_fields{{
    {.state = AnatomyState::selected, .member = &ListBox::ItemStates::selected},
    {.state = AnatomyState::hot, .member = &ListBox::ItemStates::hot},
    {.state = AnatomyState::current, .member = &ListBox::ItemStates::current},
}};

constexpr std::array<PartDeclaration, 3> list_item_parts_declared =
    part_declarations(list_item_part_fields);
constexpr std::array<AnatomyState, 3> list_item_states_declared =
    state_declarations(list_item_state_fields);

constexpr std::array<ItemSystemDeclaration, 1> list_box_item_systems{{
    {.name = "item", .parts = list_item_parts_declared, .states = list_item_states_declared,
     .custom_states = {}},
}};

constexpr ControlAnatomy list_box_anatomy{
    .control_id = "list_box",
    .parts = list_box_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
    .item_systems = list_box_item_systems,
};

} // namespace

const ControlAnatomy& ListBox::anatomy() const noexcept {
    return list_box_anatomy;
}

void ListBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = local_bounds();
    const bool themed = !has_background_override() && !has_style_override();
    const Panel::Parts panel = panel_parts();
    Parts record;
    record.scrollable = panel.scrollable;
    if (!themed) {
        record.face = panel.face;
        record.edge = panel.edge;
    }
    record.well = PartGeometry::from_bounds(bounds);
    const Rect content{2.0, 2.0, std::max(0.0, bounds.width - 4.0),
                       std::max(0.0, bounds.height - 4.0)};
    record.content = PartGeometry::from_bounds(content);
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), list_box_detail::extra_fields, record, geometry);
}

ListBox::Parts ListBox::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), list_box_detail::extra_fields, published.parts(), record);
    return record;
}

std::size_t ListBox::realized_item_count() const {
    if (top_index_ >= items.read().size()) return 0U;
    // Paint realizes one row past the fully visible ones.
    const std::size_t end = std::min(items.read().size(), top_index_ + visible_row_count() + 1U);
    const std::size_t count = end - top_index_;
    return count;
}

std::size_t ListBox::realized_item_index(std::size_t item) const noexcept {
    const std::size_t index = top_index_ + item;
    return index;
}

std::string ListBox::realized_item_stable_id(std::size_t item) const {
    const std::string stable_id = item_stable_id(realized_item_index(item));
    return stable_id;
}

ListBox::ItemParts ListBox::list_item_parts(std::size_t item) const {
    const Rect bounds = local_bounds();
    const Rect content{2.0, 2.0, std::max(0.0, bounds.width - 4.0),
                       std::max(0.0, bounds.height - 4.0)};
    const double row_height = item_height.read() * effective_text_scale();
    const double y = 2.0 + static_cast<double>(item) * row_height;
    const Rect row{2.0, y, std::max(0.0, bounds.width - 4.0), row_height};
    const std::size_t index = realized_item_index(item);
    const FontSpec label_font = text_font(font.read());
    const ResolvedTextLayout text = resolve_text_layout_utf8(items.read()[index], label_font);
    const double label_left = row_text_left();
    const double label_width = std::min(text.logical_size.width,
                                        std::max(0.0, row.right() - label_left));
    const Rect label{label_left, y, label_width, row_height};
    ItemParts record;
    record.row = PartGeometry::from_bounds(Rect::intersection(row, content));
    record.label = PartGeometry::from_bounds(Rect::intersection(label, content));
    // The current row's focus cue marks the row.
    record.row_focus = record.row;
    return record;
}

void ListBox::publish_realized_item_geometry(std::size_t item,
                                             std::span<PartGeometry> geometry) const {
    const ItemParts record = list_item_parts(item);
    write_item_parts(anatomy().item_systems.front(), list_item_part_fields, record, geometry);
}

AnatomyStateSet ListBox::list_item_states(std::size_t index) const {
    AnatomyStateSet states;
    if (detail::binary_search_contains(std::span<const std::size_t>(selected_), index)) {
        states.insert(AnatomyState::selected);
    }
    if (hovered_index_ == index) {
        states.insert(AnatomyState::hot);
    }
    if (active_index_ == index) {
        states.insert(AnatomyState::current);
    }
    return states;
}

AnatomyStateSet ListBox::realized_item_anatomy_states(std::size_t item) const {
    const AnatomyStateSet states = list_item_states(realized_item_index(item));
    return states;
}

std::vector<ListBox::Item> ListBox::realized_items() const {
    const std::size_t count = realized_item_count();
    std::vector<Item> records;
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Item record;
        record.index = realized_item_index(item);
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems.front(), list_item_part_fields, published.parts(), record.parts);
        read_states(list_item_state_fields, realized_item_anatomy_states(item), record.states);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events ListBox adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 2> list_box_interactions{{
    {.name = "selection_changed", .payload = "ListSelectionChange", .mutable_payload = false},
    {.name = "item_activated", .payload = "std::size_t", .mutable_payload = false},
}};

} // namespace

void ListBox::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), list_box_interactions.begin(), list_box_interactions.end());
}

// property_codemod begin ListBox (tools/property_families/list_box.json; generated, do not edit)

namespace {

PropertyDescriptor list_box_selection_mode_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "selection_mode";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Behavior";
    descriptor.description = "One selected item, or an extended selection with Shift and Ctrl.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"list_selection_mode", "one", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = list_selection_mode_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_list_box_selection_mode() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        list_box_selection_mode_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<ListSelectionMode>::type() {
    return PropertyType{BindingValueKind::enumeration, false, list_selection_mode_enum()};
}

BindingValue PropertyValueTraits<ListSelectionMode>::to_binding(ListSelectionMode value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = list_selection_mode_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

ListSelectionMode PropertyValueTraits<ListSelectionMode>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<ListSelectionMode>(choice.value);
}

const std::array<PropertyEntry, ListBox::list_box_property_count>& ListBox::list_box_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"selection_mode", &describe_list_box_selection_mode,
         &read_cell_entry<ListBox, decltype(&ListBox::selection_mode), &ListBox::selection_mode>,
         &write_cell_entry<ListBox, decltype(&ListBox::selection_mode), &ListBox::selection_mode>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "ListBox's property table names a property twice");
    static const std::array<PropertyEntry, list_box_property_count> entries =
        join_property_tables(Panel::panel_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> ListBox::property_table() const noexcept {
    const std::array<PropertyEntry, list_box_property_count>& entries = list_box_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void ListBox::prepare_write(const decltype(item_height)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 12.0 || incoming > 256.0) {
        throw std::invalid_argument("ListBox item height must be finite and between 12 and 256");
    }
    coerce_item_height(incoming);
}

void ListBox::written(const decltype(item_height)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ListBox::coerce_item_height(double&) {}

void ListBox::prepare_write(const decltype(font)&, FontSpec& incoming) {
    require_mutable();
    if (!valid_font_spec(incoming)) throw std::invalid_argument("ListBox font specification is invalid");
    set_font_from_code(true);
    coerce_font(incoming);
}

void ListBox::written(const decltype(font)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    on_font_changed();
}

void ListBox::on_font_changed() {}

void ListBox::coerce_font(FontSpec&) {}

// property_codemod end ListBox

} // namespace gui_forms
