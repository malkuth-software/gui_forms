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

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

struct SelectionOutsideItemCount final {
    std::size_t item_count{};

    [[nodiscard]] bool operator()(std::size_t index) const noexcept {
        return index >= item_count;
    }
};

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
    set_border_style(BorderStyle::sunken);
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

    // What a form document sets besides the items (stage FD6.3); it raises
    // no change event.
    PropertyDescriptor mode;
    mode.name = "selection_mode";
    mode.kind = BindingValueKind::enumeration;
    mode.category = "Behavior";
    mode.description = "One selected item, or an extended selection with Shift and Ctrl.";
    mode.default_value = BindingValue{PropertyEnumValue{"list_selection_mode", "one", 0}};
    mode.invalidation_effects = Dirty::paint | Dirty::semantics;
    mode.bindable = false;
    mode.enumeration = list_selection_mode_enum();
    define_bindable_property({
        std::move(mode),
        detail::EnumerationPropertyGetter<ListBox, ListSelectionMode>(
            *this, &ListBox::selection_mode, list_selection_mode_enum()),
        detail::EnumerationPropertySetter<ListBox, ListSelectionMode>(*this, &ListBox::set_selection_mode),
        {}, {}, {}});
}

BindingValue ListBox::items_property_value() const {
    BindingValue value = text_items_property::value_of(items_);
    return value;
}

void ListBox::set_items_property(const BindingValue& value) {
    std::vector<std::string> items = text_items_property::items_of(value, "ListBox");
    set_items(std::move(items));
}

void ListBox::reset_items_property() {
    set_items({});
}

bool ListBox::should_serialize_items_property() const noexcept {
    return !items_.empty();
}

void ListBox::set_items(std::vector<std::string> items) {
    require_mutable();
    for (const std::string& item : items) {
        if (!validate_utf8(item).valid()) {
            throw std::invalid_argument("ListBox items must be valid UTF-8");
        }
    }
    items_ = std::move(items);
    item_stable_ids_.clear();
    const std::vector<std::size_t> previous = selected_;
    selected_.erase(
        std::remove_if(selected_.begin(), selected_.end(),
                       SelectionOutsideItemCount{items_.size()}),
        selected_.end());
    if (active_index_ && *active_index_ >= items_.size()) active_index_.reset();
    if (anchor_index_ && *anchor_index_ >= items_.size()) anchor_index_.reset();
    top_index_ = items_.empty() ? 0U : std::min(top_index_, items_.size() - 1U);
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    if (previous != selected_) {
        publish_change(selection_changed_,
                       ListSelectionChange{previous, selected_, active_index_});
    }
}

void ListBox::add_item(std::string item) {
    require_mutable();
    if (!validate_utf8(item).valid()) {
        throw std::invalid_argument("ListBox item must be valid UTF-8");
    }
    // Appending without an accompanying model identity returns the collection
    // to the deterministic index-derived identity policy.
    item_stable_ids_.clear();
    items_.push_back(std::move(item));
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void ListBox::remove_item(std::size_t index) {
    require_mutable();
    if (index >= items_.size()) {
        throw std::out_of_range("ListBox item index is outside the collection");
    }
    const std::vector<std::size_t> previous = selected_;
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    if (!item_stable_ids_.empty()) {
        item_stable_ids_.erase(item_stable_ids_.begin() +
                               static_cast<std::ptrdiff_t>(index));
    }
    std::vector<std::size_t> adjusted;
    for (const std::size_t selected : selected_) {
        if (selected != index) adjusted.push_back(selected > index ? selected - 1U : selected);
    }
    selected_ = std::move(adjusted);
    if (active_index_) {
        if (*active_index_ == index) active_index_.reset();
        else if (*active_index_ > index) --*active_index_;
    }
    if (anchor_index_) {
        if (*anchor_index_ == index) anchor_index_.reset();
        else if (*anchor_index_ > index) --*anchor_index_;
    }
    top_index_ = items_.empty() ? 0U : std::min(top_index_, items_.size() - 1U);
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    if (previous != selected_) {
        publish_change(selection_changed_,
                       ListSelectionChange{previous, selected_, active_index_});
    }
}

void ListBox::clear_items() {
    set_items({});
}

void ListBox::set_item_stable_ids(std::vector<std::string> stable_ids) {
    require_mutable();
    if (stable_ids.size() != items_.size()) {
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
    if (index >= items_.size()) {
        throw std::out_of_range(
            "ListBox item stable ID index is outside the collection");
    }
    if (!item_stable_ids_.empty()) return item_stable_ids_[index];
    return std::string(stable_id().value()) + ".item." +
           std::to_string(index);
}

void ListBox::set_selection_mode(ListSelectionMode mode) {
    require_mutable();
    if (selection_mode_ == mode) return;
    selection_mode_ = mode;
    if (mode == ListSelectionMode::one && selected_.size() > 1U) {
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
    if (index >= items_.size()) {
        throw std::out_of_range("ListBox selection index is outside the collection");
    }
    if (selection_mode_ == ListSelectionMode::one) {
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

void ListBox::set_top_index(std::size_t index) {
    require_mutable();
    if (items_.empty()) {
        if (index != 0U) throw std::out_of_range("empty ListBox top index must be zero");
    } else if (index >= items_.size()) {
        throw std::out_of_range("ListBox top index is outside the collection");
    }
    if (top_index_ == index) return;
    top_index_ = index;
    invalidate(Dirty::paint | Dirty::semantics);
}

void ListBox::set_item_height(double height) {
    require_mutable();
    if (!std::isfinite(height) || height < 12.0 || height > 256.0) {
        throw std::invalid_argument("ListBox item height must be finite and between 12 and 256");
    }
    if (item_height_ == height) return;
    item_height_ = height;
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ListBox::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) {
        throw std::invalid_argument("ListBox font specification is invalid");
    }
    if (font_ == font) return;
    font_ = font;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

std::size_t ListBox::visible_row_count() const noexcept {
    double height = local_bounds().height;
    if (height <= 4.0) {
        height = requested_bounds().height;
    }
    const double row_height = item_height_ * effective_text_scale();
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
    Panel::arrange(final_bounds);
    if (active_index_) {
        ensure_visible(*active_index_);
    }
}

std::optional<std::size_t> ListBox::index_at(Point absolute) const noexcept {
    const Rect bounds = absolute_bounds();
    if (!bounds.contains(absolute)) return {};
    const double local_y = absolute.y - bounds.y - 2.0;
    if (local_y < 0.0) return {};
    const double row_height = item_height_ * effective_text_scale();
    const std::size_t index = top_index_ +
        static_cast<std::size_t>(std::floor(local_y / row_height));
    return index < items_.size() ? std::optional<std::size_t>{index}
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
    const FontSpec font = effective_font(font_);
    const double row_height = item_height_ * effective_text_scale();
    painter.save();
    painter.clip_rect({2.0, 2.0, std::max(0.0, bounds.width - 4.0),
                       std::max(0.0, bounds.height - 4.0)});
    const std::size_t visible = visible_row_count() + 1U;
    const std::size_t end = std::min(items_.size(), top_index_ + visible);
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
        text.text = items_[index];
        text.font = font;
        if (overridden) text.authored_foreground = selected && focused_ ? style().highlight : style().text;
        const double left = row_text_left();
        paint_text_part_at(painter, *this, text,
                           {left, y + std::max(font.size, row_height * 0.5 + 4.0)},
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
        if (!items_.empty() && event.wheel_delta.y != 0.0) {
            const int direction = event.wheel_delta.y > 0.0 ? -1 : 1;
            const std::ptrdiff_t next = std::clamp<std::ptrdiff_t>(
                static_cast<std::ptrdiff_t>(top_index_) + direction * 3,
                0, static_cast<std::ptrdiff_t>(items_.size() - 1U));
            set_top_index(static_cast<std::size_t>(next));
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
    if (!focused_ || !enabled() || event.action != KeyAction::down || items_.empty()) return;
    if (event.physical_key == PhysicalKey::enter && active_index_) {
        item_activated_.emit(*active_index_);
        event.handled = true;
        return;
    }
    std::optional<std::size_t> next;
    const std::size_t current = active_index_.value_or(0U);
    if (event.physical_key == PhysicalKey::up) next = current == 0U ? 0U : current - 1U;
    else if (event.physical_key == PhysicalKey::down) next = std::min(current + 1U, items_.size() - 1U);
    else if (event.physical_key == PhysicalKey::home) next = 0U;
    else if (event.physical_key == PhysicalKey::end) next = items_.size() - 1U;
    else if (event.physical_key == PhysicalKey::page_up) next = current > visible_row_count() ? current - visible_row_count() : 0U;
    else if (event.physical_key == PhysicalKey::page_down) next = std::min(current + visible_row_count(), items_.size() - 1U);
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
    if (selection_mode_ == ListSelectionMode::multiple_extended) {
        descriptor.states |= SemanticState::multiselectable;
    }
    descriptor.name = accessible_name();
    descriptor.description = accessible_description();
    if (const std::optional<std::size_t> selected = selected_index()) {
        descriptor.value = items_[*selected];
    } else if (!selected_.empty()) {
        descriptor.value = std::to_string(selected_.size()) + " items selected";
    }
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> ListBox::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes;
    nodes.reserve(items_.size());
    const Rect list_bounds = absolute_bounds();
    const std::size_t visible_count = visible_row_count();
    const std::size_t visible_end = std::min(items_.size(), top_index_ + visible_count);
    const double row_height = item_height_ * effective_text_scale();
    for (std::size_t index = 0; index < items_.size(); ++index) {
        SemanticNode node;
        node.stable_id = item_stable_id(index);
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::list_item;
        node.name = items_[index];
        node.value = items_[index];
        node.position_in_set = index + 1U;
        node.set_size = items_.size();
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
            index >= items_.size()) return false;
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
    if (top_index_ >= items_.size()) return 0U;
    // Paint realizes one row past the fully visible ones.
    const std::size_t end = std::min(items_.size(), top_index_ + visible_row_count() + 1U);
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
    const double row_height = item_height_ * effective_text_scale();
    const double y = 2.0 + static_cast<double>(item) * row_height;
    const Rect row{2.0, y, std::max(0.0, bounds.width - 4.0), row_height};
    const std::size_t index = realized_item_index(item);
    const FontSpec font = effective_font(font_);
    const ResolvedTextLayout text = resolve_text_layout_utf8(items_[index], font);
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

} // namespace gui_forms
