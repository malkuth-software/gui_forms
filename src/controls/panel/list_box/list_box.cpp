#include "gui_forms/controls/list_box.hpp"
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
#include "gui_forms/allocation/library_memory/library_memory.hpp"
#include "list_box_ic_checks.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace ic = detail::ic::list_box;
namespace {

// The ListSelectionMode enumeration, numbered as declared. Static storage;
// any thread may read it.
std::shared_ptr<const PropertyEnumDescriptor> list_selection_mode_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        gui_forms::detail::make_library_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "list_selection_mode", {{"one", 0}, {"multiple_extended", 1}}, false});
    return value;
}

// The list's insets (audit F6), named once for measure, paint, hit testing,
// damage and semantics. Logical units, not scaled by the text scale.
// The well's border: rows sit this far inside the control on every side.
constexpr double well_inset = 2.0;
// Both borders of one axis.
constexpr double well_insets = 2.0 * well_inset;
// The row text's left edge, from the control's left.
constexpr double row_text_inset = 8.0;
// The text baseline sits at least this far below the row's middle.
constexpr double baseline_below_middle = 4.0;
// The active row's focus ring sits this far inside the row.
constexpr double focus_ring_inset = 1.0;

// ListControl's selected index for a selection kept sorted: its first item,
// or none.
stxlib::Optional<std::size_t> first_selected(const std::vector<std::size_t>& selection) noexcept {
    if (selection.empty()) return stxlib::Optional<std::size_t>::none();
    const stxlib::Optional<std::size_t> first = stxlib::Optional<std::size_t>::of(selection.front());
    return first;
}

} // namespace
using namespace input_control_detail;

ListBox::ListBox(StableId stable_id) : ListControl(std::move(stable_id)) {
    set_paint_plane(PaintPlane::control);
    border_style = BorderStyle::sunken;
    focusable = true;
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
        const stxlib::Optional<std::size_t> kept = edit.remap(selected);
        if (kept.present()) edit_selection_.push_back(*kept.value());
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
        stable_id_order_.clear();
        break;
    case CollectionEditKind::replace:
        break;
    case CollectionEditKind::assign:
    case CollectionEditKind::insert:
    default:
        item_stable_ids_.clear();
        stable_id_order_.clear();
        break;
    }
    const bool selection_changed = edit_selection_ != selected_;
    if (selection_changed) std::swap(selected_, edit_selection_);
    if (active_index_.present()) active_index_ = edit.remap(*active_index_.value());
    if (anchor_index_.present()) anchor_index_ = edit.remap(*anchor_index_.value());
    if (hovered_index_.present()) hovered_index_ = edit.remap(*hovered_index_.value());
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
        const bool index_moved = record_selected_index(first_selected(selected_));
        publish_change(selection_changed_,
                       ListSelectionChange{edit_selection_, selected_, active_index_});
        if (index_moved) publish_change(selected_index_changed_, selected_index());
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
    stable_id_order_.clear();
    invalidate(Dirty::semantics);
}

// The order is rebuilt on the first lookup after the identities change
// (audit N46a): one sort per change, then logarithmic lookups.
stxlib::Optional<std::size_t> ListBox::index_of_item_stable_id(std::string_view stable_id) {
    if (stable_id_order_.size() != item_stable_ids_.size()) {
        stable_id_order_.resize(item_stable_ids_.size());
        for (std::size_t index = 0U; index < stable_id_order_.size(); ++index) stable_id_order_[index] = index;
        std::sort(stable_id_order_.begin(), stable_id_order_.end(), StableIdOrder{&item_stable_ids_});
    }
    const std::vector<std::size_t>::const_iterator found =
        std::lower_bound(stable_id_order_.cbegin(), stable_id_order_.cend(), stable_id,
                         StableIdOrder{&item_stable_ids_});
    if (found == stable_id_order_.cend() || item_stable_ids_[*found] != stable_id) return stxlib::Optional<std::size_t>::none();
    return stxlib::Optional<std::size_t>::of(*found);
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
        apply_selection({retained}, stxlib::Optional<std::size_t>::of(retained));
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

void ListBox::apply_selection(std::vector<std::size_t> selection,
                              stxlib::Optional<std::size_t> active) {
    std::sort(selection.begin(), selection.end());
    selection.erase(std::unique(selection.begin(), selection.end()), selection.end());
    const std::vector<std::size_t> previous = selected_;
    const stxlib::Optional<std::size_t> previous_active = active_index_;
    selected_ = std::move(selection);
    active_index_ = active;
    if (previous == selected_ && previous_active == active_index_) return;
    invalidate(Dirty::paint | Dirty::semantics);
    const bool index_moved = record_selected_index(first_selected(selected_));
    publish_change(selection_changed_,
                   ListSelectionChange{previous, selected_, active_index_});
    if (index_moved) publish_change(selected_index_changed_, selected_index());
}

void ListBox::select_index(std::size_t index, bool extend, bool toggle) {
    require_mutable();
    if (index >= items.read().size()) {
        throw std::out_of_range("ListBox selection index is outside the collection");
    }
    if (selection_mode.read() == ListSelectionMode::one) {
        apply_selection({index}, stxlib::Optional<std::size_t>::of(index));
        anchor_index_.set(index);
    } else if (extend && anchor_index_.present()) {
        std::vector<std::size_t> next;
        const std::size_t anchor = *anchor_index_.value();
        const std::size_t first = std::min(anchor, index);
        const std::size_t last = std::max(anchor, index);
        next.reserve(last - first + 1U);
        for (std::size_t item = first; item <= last; ++item) next.push_back(item);
        apply_selection(std::move(next), stxlib::Optional<std::size_t>::of(index));
    } else if (toggle) {
        std::vector<std::size_t> next = selected_;
        const std::vector<std::size_t>::iterator found =
            std::find(next.begin(), next.end(), index);
        if (found == next.end()) next.push_back(index);
        else next.erase(found);
        apply_selection(std::move(next), stxlib::Optional<std::size_t>::of(index));
        anchor_index_.set(index);
    } else {
        apply_selection({index}, stxlib::Optional<std::size_t>::of(index));
        anchor_index_.set(index);
    }
    ensure_visible(index);
}

void ListBox::clear_selection() {
    require_mutable();
    apply_selection({}, stxlib::Optional<std::size_t>::none());
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
    // The hot item is the one under the pointer, which the scroll moved
    // away: it is found again at the next pointer move.
    hovered_index_.reset();
    invalidate(Dirty::paint | Dirty::semantics);
}

std::size_t ListBox::visible_row_count() const noexcept {
    double height = local_bounds().height;
    if (height <= well_insets) {
        height = requested_bounds().height;
    }
    const double row_height = item_height.read() * effective_text_scale();
    return std::max<std::size_t>(1U, static_cast<std::size_t>(
        std::floor(std::max(0.0, height - well_insets) / row_height)));
}

void ListBox::ensure_visible(std::size_t index) {
    if (local_bounds().height <= well_insets && requested_bounds().height <= well_insets) {
        return;
    }
    const std::size_t visible = visible_row_count();
    const std::size_t previous_top = top_index_;
    if (index < top_index_) top_index_ = index;
    else if (index >= top_index_ + visible) top_index_ = index - visible + 1U;
    // A scroll moves the items under the pointer (write_view's rule).
    if (top_index_ != previous_top) hovered_index_.reset();
}

void ListBox::arrange(Rect final_bounds) {
    const std::size_t top_before = top_index_;
    Panel::arrange(final_bounds);
    if (active_index_.present()) {
        ensure_visible(*active_index_.value());
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
    const stxlib::Optional<std::size_t> structural_from =
        TextItemsCapability<ListBox>::take_dirty(*this, settled_rows_);
    if (window_moved) {
        invalidate(Dirty::paint);
        return;
    }
    if (structural_from.absent() && settled_rows_.empty()) return;
    const Rect bounds = local_bounds();
    if (bounds.empty()) return;
    const double row_height = item_height.read() * effective_text_scale();
    const double row_width = std::max(0.0, bounds.width - well_insets);
    const double content_bottom = std::max(well_inset, bounds.height - well_inset);
    const std::size_t window_end = top_index_ + visible_row_count() + 1U;
    std::size_t replaced_end = window_end;
    if (structural_from.present() && *structural_from.value() < window_end) {
        const std::size_t first = std::max(*structural_from.value(), top_index_);
        const double top = well_inset + static_cast<double>(first - top_index_) * row_height;
        if (top < content_bottom) invalidate(Rect{well_inset, top, row_width, content_bottom - top});
        replaced_end = first;
    }
    for (const std::uint32_t row : settled_rows_) {
        const std::size_t index = static_cast<std::size_t>(row);
        if (index < top_index_) continue;
        if (index >= replaced_end) break;
        invalidate_row(index);
    }
}

// Repaints one row when it is in the shown window (the last, partly shown
// row included); a row outside it has nothing on screen to repaint.
void ListBox::invalidate_row(std::size_t index) {
    if (index < top_index_) return;
    const std::size_t shown = visible_row_count() + 1U;
    if (index - top_index_ >= shown) return;
    const Rect bounds = local_bounds();
    if (bounds.empty()) return;
    const double row_height = item_height.read() * effective_text_scale();
    const double top = well_inset + static_cast<double>(index - top_index_) * row_height;
    invalidate(Rect{well_inset, top, std::max(0.0, bounds.width - well_insets), row_height});
}

stxlib::Optional<std::size_t> ListBox::index_at(Point absolute) const noexcept {
    const Rect bounds = absolute_bounds();
    if (!bounds.contains(absolute)) return stxlib::Optional<std::size_t>::none();
    const double local_y = point_from_window(absolute).y - well_inset;
    if (local_y < 0.0) return stxlib::Optional<std::size_t>::none();
    const double row_height = item_height.read() * effective_text_scale();
    const std::size_t index = top_index_ +
        static_cast<std::size_t>(std::floor(local_y / row_height));
    return index < items.read().size() ? stxlib::Optional<std::size_t>::of(index)
                                       : stxlib::Optional<std::size_t>::none();
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
    if (has_background_override() || overridden) well.authored_background.set(background());
    well.key.set(PartKey{"well", {}, {}});
    paint_part(painter, *this, well);
    const FontSpec row_font = text_font(font.read());
    const double row_height = item_height.read() * effective_text_scale();
    painter.save();
    painter.clip_rect({well_inset, well_inset, std::max(0.0, bounds.width - well_insets),
                       std::max(0.0, bounds.height - well_insets)});
    const std::size_t visible = visible_row_count() + 1U;
    const std::size_t end = std::min(items.read().size(), top_index_ + visible);
    for (std::size_t index = top_index_; index < end; ++index) {
        const double y = well_inset + static_cast<double>(index - top_index_) * row_height;
        const bool selected = detail::binary_search_contains(
            std::span<const std::size_t>(selected_), index);
        const bool hovered = hovered_index_ == stxlib::Optional<std::size_t>::of(index);
        const Rect row{well_inset, y, std::max(0.0, bounds.width - well_insets), row_height};
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
                plate.authored_background.set(selected ? selected_fill : style().face_light);
            }
            paint_part(painter, *this, plate);
        }
        paint_row_adornment(painter, index, row, selected, focused_);
        PartPaint text;
        text.family = selected || hovered ? ControlVisualRole::selection : ControlVisualRole::editor;
        text.role = PartRole::content;
        text.states = row_states;
        text.text = lend_paint_text(items.read()[index]);
        text.font = row_font;
        if (overridden) text.authored_foreground.set(selected && focused_ ? style().highlight : style().text);
        const double left = row_text_left();
        const double baseline = y + std::max(row_font.size, row_height * 0.5 + baseline_below_middle);
        const double room = std::max(0.0, row.x + row.width - left);
        if (effective_layout_direction() == LayoutDirection::right_to_left) {
            // Right-aligned, the text inset (and any adornment) at the right
            // (WinForms' ListBox under RightToLeft).
            const double text_width = std::min(room, measure_text_extent_utf8(text.text, row_font).logical_size.width);
            paint_text_part_at(painter, *this, text, {bounds.width - left - text_width, baseline}, text_width);
        } else {
            paint_text_part_at(painter, *this, text, {left, baseline}, room);
        }
        if (focused_ && active_index_ == stxlib::Optional<std::size_t>::of(index)) {
            PartPaint ring;
            ring.family = ControlVisualRole::selection;
            ring.role = PartRole::focus;
            ring.bounds = {well_inset + focus_ring_inset, y + focus_ring_inset,
                           std::max(0.0, bounds.width - well_insets - 2.0 * focus_ring_inset),
                           std::max(0.0, row_height - 2.0 * focus_ring_inset)};
            ring.states = row_states;
            ring.states.insert(AnatomyState::focus_visible);
            paint_part(painter, *this, ring);
        }
    }
    painter.restore();
}

double ListBox::row_text_left() const noexcept {
    return row_text_inset;
}

void ListBox::paint_row_adornment(Painter&, std::size_t, Rect, bool,
                                  bool) const {}

// The raisers run ListBox's handling, then raise the event through
// Control: ScrollableControl's bar handling does not see the list's
// pointer events, as before the raisers.
void ListBox::on_mouse_down(MouseEventArgs& arguments) {
    // list_pointer's handlers may drop the list; the raise below still uses it.
    const Ptr self = shared_from_this();
    list_pointer(arguments);
    Control::on_mouse_down(arguments);
}

void ListBox::on_mouse_up(MouseEventArgs& arguments) {
    // item_activated's handlers may drop the list; the raise below uses it.
    const Ptr self = shared_from_this();
    list_pointer(arguments);
    Control::on_mouse_up(arguments);
}

void ListBox::on_mouse_move(MouseEventArgs& arguments) {
    list_pointer(arguments);
    Control::on_mouse_move(arguments);
}

void ListBox::on_mouse_wheel(MouseEventArgs& arguments) {
    list_pointer(arguments);
    Control::on_mouse_wheel(arguments);
}

void ListBox::on_mouse_leave() {
    list_pointer_left();
    Control::on_mouse_leave();
}

void ListBox::list_pointer(MouseEventArgs& arguments) {
    if (!eligible_for_input()) return;
    if (arguments.action == PointerAction::move) {
        const stxlib::Optional<std::size_t> next = index_at(arguments.window_position);
        if (next != hovered_index_) {
            // Only the row left and the row entered repaint (audit C9).
            const stxlib::Optional<std::size_t> previous = hovered_index_;
            hovered_index_ = next;
            if (previous.present()) invalidate_row(*previous.value());
            if (next.present()) invalidate_row(*next.value());
        }
        return;
    }
    if (arguments.action == PointerAction::wheel) {
        if (!items.read().empty() && arguments.wheel_delta.y != 0.0) {
            const int direction = arguments.wheel_delta.y > 0.0 ? -1 : 1;
            const std::ptrdiff_t next = std::clamp<std::ptrdiff_t>(
                static_cast<std::ptrdiff_t>(top_index_) + direction * 3,
                0, static_cast<std::ptrdiff_t>(items.read().size() - 1U));
            top_index = static_cast<std::size_t>(next);
            // The pointer is where the wheel turned: the item now under it
            // is hot.
            if (is_alive()) hovered_index_ = index_at(arguments.window_position);
            arguments.handled = true;
        }
        return;
    }
    if (arguments.action == PointerAction::down && arguments.button == PointerButton::primary) {
        // The focus handlers may drop the list: `self` keeps it alive, and
        // the press is hit-tested after them.
        const Ptr self = shared_from_this();
        if (window() != nullptr) static_cast<void>((*window()).request_focus(self));
        if (!is_alive()) return;
        const stxlib::Optional<std::size_t> index = index_at(arguments.window_position);
        if (index.present()) {
            select_index(*index.value(), includes(arguments.modifiers, Modifier::shift),
                         command_modifier(arguments.modifiers));
            arguments.handled = true;
        }
    } else if (arguments.action == PointerAction::up &&
               arguments.button == PointerButton::primary) {
        const stxlib::Optional<std::size_t> index = index_at(arguments.window_position);
        if (index.present() && active_index_ == index) {
            item_activated_.emit(*index.value());
            arguments.handled = true;
        }
    }
}

void ListBox::list_pointer_left() {
    if (!eligible_for_input()) return;
    const stxlib::Optional<std::size_t> previous = hovered_index_;
    hovered_index_.reset();
    if (previous.present()) invalidate_row(*previous.value());
}

void ListBox::on_key_down(KeyEventArgs& arguments) {
    ListControl::on_key_down(arguments);
    if (!arguments.handled) list_key_down(arguments);
}

void ListBox::list_key_down(KeyEventArgs& arguments) {
    if (!focused_ || !enabled.read() || items.read().empty()) return;
    if (arguments.physical_key == PhysicalKey::enter && active_index_.present()) {
        item_activated_.emit(*active_index_.value());
        arguments.handled = true;
        return;
    }
    stxlib::Optional<std::size_t> next;
    const std::size_t current = active_index_.value_or(0U);
    if (arguments.physical_key == PhysicalKey::up) next.set(current == 0U ? 0U : current - 1U);
    else if (arguments.physical_key == PhysicalKey::down) next.set(std::min(current + 1U, items.read().size() - 1U));
    else if (arguments.physical_key == PhysicalKey::home) next.set(0U);
    else if (arguments.physical_key == PhysicalKey::end) next.set(items.read().size() - 1U);
    else if (arguments.physical_key == PhysicalKey::page_up) next.set(current > visible_row_count() ? current - visible_row_count() : 0U);
    else if (arguments.physical_key == PhysicalKey::page_down) next.set(std::min(current + visible_row_count(), items.read().size() - 1U));
    if (next.present()) {
        select_index(*next.value(), includes(arguments.modifiers, Modifier::shift), false);
        arguments.handled = true;
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
    const stxlib::Optional<std::size_t> selected = selected_index();
    if (selected.present()) {
        descriptor.value = items.read()[*selected.value()];
    } else if (!selected_.empty()) {
        descriptor.value = std::to_string(selected_.size()) + " items selected";
    }
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> ListBox::semantic_virtual_children() const {
    // Only the shown window is described (audit N46), as the data grid,
    // ObjectView and ListView do; position_in_set and set_size still count
    // the whole list, so a client knows where the window sits.
    std::vector<SemanticNode> nodes;
    const std::size_t count = items.read().size();
    const std::size_t first = std::min(top_index_, count);
    const std::size_t visible_count = visible_row_count();
    const std::size_t visible_end = std::min(count, first + visible_count);
    // Paint realizes one row past the fully visible ones; so does this.
    const std::size_t end = std::min(count, first + visible_count + 1U);
    nodes.reserve(end - first);
    const ScaleFrame list_frame = window_frame();
    const Rect list_area = client_rectangle();
    const double row_height = item_height.read() * effective_text_scale();
    for (std::size_t index = first; index < end; ++index) {
        SemanticNode node;
        node.stable_id = item_stable_id(index);
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::list_item;
        node.name = items.read()[index];
        node.value = items.read()[index];
        node.position_in_set.set(index + 1U);
        node.set_size.set(count);
        const double row_y = well_inset +
            (static_cast<double>(index) - static_cast<double>(top_index_)) *
                row_height;
        const Rect row = Rect::intersection(
            {well_inset, row_y, std::max(0.0, list_area.width - well_insets), row_height}, list_area);
        node.bounds = frame_to_outer(list_frame, row);
        if (effectively_enabled()) node.states |= SemanticState::enabled;
        node.states |= SemanticState::focusable;
        if (index >= top_index_ && index < visible_end) {
            node.states |= SemanticState::visible;
        }
        if (detail::binary_search_contains(
                std::span<const std::size_t>(selected_), index)) {
            node.states |= SemanticState::selected;
        }
        if (focused_ && active_index_ == stxlib::Optional<std::size_t>::of(index)) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::focus, SemanticAction::select,
                        SemanticAction::press};
        nodes.push_back(std::move(node));
    }
    return nodes;
}

namespace {

// The row a semantic child "<list id>.item.<n>" names in a list of `count`
// items without authored item IDs, or none.
stxlib::Optional<std::size_t> numbered_child_row(std::string_view list_id, std::size_t count,
                                                 std::string_view child_stable_id) {
    const std::string prefix = std::string(list_id) + ".item.";
    if (!child_stable_id.starts_with(prefix)) return stxlib::Optional<std::size_t>::none();
    const std::string_view suffix = child_stable_id.substr(prefix.size());
    std::size_t index{};
    const std::from_chars_result parsed = std::from_chars(
        suffix.data(), suffix.data() + suffix.size(), index);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != suffix.data() + suffix.size() ||
        index >= count) return stxlib::Optional<std::size_t>::none();
    return stxlib::Optional<std::size_t>::of(index);
}

} // namespace

// Commit, then notify (house style 5): the focus request runs the focus
// handlers, which may edit the items or drop the list, so the row is found
// again after it, and `self` keeps the list alive across them. The
// selection's own handlers may edit the items too: the press is reported
// only for a row that is still there.
bool ListBox::on_semantic_child_action(std::string_view child_stable_id,
                                       SemanticAction action,
                                       std::string_view) {
    if (action != SemanticAction::focus && action != SemanticAction::select &&
        action != SemanticAction::press) return false;
    // The row is named by an authored item ID, or by its position.
    const stxlib::Optional<std::size_t> named = item_stable_ids_.empty()
        ? numbered_child_row(stable_id().value(), items.read().size(), child_stable_id)
        : index_of_item_stable_id(child_stable_id);
    if (named.absent()) return false;
    const Ptr self = shared_from_this();
    if (window() != nullptr) {
        static_cast<void>((*window()).request_focus(self));
    }
    if (!is_alive()) return true;
    const stxlib::Optional<std::size_t> focused_row = item_stable_ids_.empty()
        ? numbered_child_row(stable_id().value(), items.read().size(), child_stable_id)
        : index_of_item_stable_id(child_stable_id);
    if (focused_row.absent()) return true;
    select_index(*focused_row.value(), false, false);
    if (action != SemanticAction::press || !is_alive()) return true;
    const stxlib::Optional<std::size_t> pressed_row = item_stable_ids_.empty()
        ? numbered_child_row(stable_id().value(), items.read().size(), child_stable_id)
        : index_of_item_stable_id(child_stable_id);
    if (pressed_row.absent()) return true;
    item_activated_.emit(*pressed_row.value());
    return true;
}

namespace {

constexpr std::array<PartField<ListBox::ItemParts>, 3> list_item_part_fields{{
    {.declaration = {.name = "row", .role = PartRole::region, .composed_from = {}, .states = {}},
     .member = &ListBox::ItemParts::row},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}, .states = {}},
     .member = &ListBox::ItemParts::label},
    {.declaration = {.name = "row_focus", .role = PartRole::focus, .composed_from = {}, .states = {}},
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
    const Rect content{well_inset, well_inset, std::max(0.0, bounds.width - well_insets),
                       std::max(0.0, bounds.height - well_insets)};
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

// Every realized-item accessor of ListBox and CheckedListBox goes through
// here, so an item past the realized rows is refused before it indexes the
// items (A-gf-controls-08-F7).
std::size_t ListBox::realized_item_index(std::size_t item) const {
    if (item >= realized_item_count()) {
        throw std::out_of_range("ListBox realized item is past the realized rows");
    }
    const std::size_t index = top_index_ + item;
    return index;
}

std::string ListBox::realized_item_stable_id(std::size_t item) const {
    const std::string stable_id = item_stable_id(realized_item_index(item));
    return stable_id;
}

ListBox::ItemParts ListBox::list_item_parts(std::size_t item) const {
    const Rect bounds = local_bounds();
    const Rect content{well_inset, well_inset, std::max(0.0, bounds.width - well_insets),
                       std::max(0.0, bounds.height - well_insets)};
    const double row_height = item_height.read() * effective_text_scale();
    const double y = well_inset + static_cast<double>(item) * row_height;
    const Rect row{well_inset, y, std::max(0.0, bounds.width - well_insets), row_height};
    const std::size_t index = realized_item_index(item);
    const FontSpec label_font = text_font(font.read());
    const ResolvedTextLayout text = resolve_text_layout_utf8(items.read()[index], label_font);
    const double label_left = row_text_left();
    const double label_width = std::min(text.logical_size.width,
                                        std::max(0.0, row.right() - label_left));
    Rect label{label_left, y, label_width, row_height};
    if (effective_layout_direction() == LayoutDirection::right_to_left) {
        label = horizontally_mirrored(label, Rect{0.0, 0.0, bounds.width, bounds.height});
    }
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
    if (hovered_index_ == stxlib::Optional<std::size_t>::of(index)) {
        states.insert(AnatomyState::hot);
    }
    if (active_index_ == stxlib::Optional<std::size_t>::of(index)) {
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

// The events ListBox adds to those of ListControl, in declaration order.
constexpr std::array<InteractionDeclaration, 2> list_box_interactions{{
    {.name = "selection_changed", .payload = "ListSelectionChange", .mutable_payload = false},
    {.name = "item_activated", .payload = "std::size_t", .mutable_payload = false},
}};

} // namespace

void ListBox::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    ListControl::append_interactions(interactions);
    interactions.insert(interactions.end(), list_box_interactions.begin(), list_box_interactions.end());
}

namespace {

// The items collection in its BindingValue form (text_items_property.hpp):
// a hand table entry (extra_entries) over the CollectionCell.
PropertyDescriptor list_box_items_descriptor() {
    PropertyDescriptor descriptor = text_items_property::descriptor("Ordered text items the list shows.");
    return descriptor;
}

const PropertyDescriptor& describe_list_box_items() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        list_box_items_descriptor(), PropertyOperations{true, true, true, false});
    return descriptor;
}

} // namespace

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
    static constexpr std::array<PropertyEntry, 2U> own{{
        {"selection_mode", &describe_list_box_selection_mode,
         &read_cell_entry<ListBox, decltype(&ListBox::selection_mode), &ListBox::selection_mode>,
         &write_cell_entry<ListBox, decltype(&ListBox::selection_mode), &ListBox::selection_mode>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"items", &describe_list_box_items,
         &read_member_entry<ListBox, decltype(&ListBox::items_property_value), &ListBox::items_property_value>,
         &write_member_entry<ListBox, decltype(&ListBox::set_items_property), &ListBox::set_items_property>,
         &reset_member_entry<ListBox, decltype(&ListBox::reset_items_property), &ListBox::reset_items_property>, &should_serialize_member_entry<ListBox, decltype(&ListBox::should_serialize_items_property), &ListBox::should_serialize_items_property>, nullptr,
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
    if (!ic::admits_item_height(incoming)) {
        throw std::invalid_argument("ListBox item height must be finite and between 12 and 256");
    }
}

void ListBox::written(const decltype(item_height)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ListBox::prepare_write(const decltype(font)&, FontSpec& incoming) {
    require_mutable();
    if (!valid_font_spec(incoming)) throw std::invalid_argument("ListBox font specification is invalid");
    set_font_from_code(true);
}

void ListBox::written(const decltype(font)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    on_font_changed();
}

void ListBox::on_font_changed() {}

// property_codemod end ListBox

} // namespace gui_forms
