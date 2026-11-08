#include "gui_forms/controls/tool_strip/tool_strip.hpp"

#include "../menu/menu_utilities.hpp"
#include "strip_merge.hpp"
#include "../scrollable_control/scrolling_utilities.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gui_forms {
namespace {

// Strip geometry in logical units at text scale 1 (Windows Forms' 25-unit
// strip, 16-unit images, 4-unit item padding).
constexpr double strip_height = 25.0;
constexpr double edge_inset = 2.0;
constexpr double grip_width = 6.0;
constexpr double item_padding = 4.0;
constexpr double image_size = 16.0;
constexpr double image_text_gap = 3.0;
constexpr double minimum_button_width = 22.0;
constexpr double separator_width = 6.0;
constexpr double drop_down_arrow_width = 11.0;
constexpr double split_arrow_width = 12.0;
constexpr double arrow_glyph_size = 7.0;
constexpr double overflow_button_width = 12.0;
constexpr double sizing_grip_size = 12.0;
constexpr double vertical_item_extent = 22.0;
constexpr double default_combo_box_width = 120.0;
constexpr double default_hosted_width = 100.0;
constexpr double maximum_hosted_width = 1024.0;
constexpr double maximum_end_reserve = 64.0;

bool has_menu(ToolStripItemKind kind) noexcept {
    const bool result = kind == ToolStripItemKind::drop_down_button ||
                        kind == ToolStripItemKind::split_button;
    return result;
}

// A combo box, text box or progress bar: an item that holds a child control.
bool hosted(ToolStripItemKind kind) noexcept {
    const bool result = kind == ToolStripItemKind::combo_box || kind == ToolStripItemKind::text_box ||
                        kind == ToolStripItemKind::progress_bar;
    return result;
}

// A label that StatusStrip stretches over the width the other items leave.
bool spring_label(const ToolStripItemSpec& item) noexcept {
    const bool result = item.spring && item.kind == ToolStripItemKind::label;
    return result;
}

// A rectangle from `position` for `extent` along a strip's main axis and
// from `cross_start` for `cross_extent` across it.
Rect axis_rect(bool vertical, double position, double extent, double cross_start, double cross_extent) noexcept {
    if (vertical) {
        const Rect result{cross_start, position, cross_extent, extent};
        return result;
    }
    const Rect result{position, cross_start, extent, cross_extent};
    return result;
}

// A square glyph of `side` centred in `area`, on whole units.
Rect centred_glyph(Rect area, double side) noexcept {
    if (area.width < side || area.height < side) return {};
    const Rect glyph{std::floor(area.x + (area.width - side) * 0.5),
                     std::floor(area.y + (area.height - side) * 0.5), side, side};
    return glyph;
}

void collect_menu_ids(const std::vector<MenuItemSpec>& items, std::unordered_set<std::string>& ids) {
    for (const MenuItemSpec& item : items) {
        ids.insert(item.stable_id);
        collect_menu_ids(item.children, ids);
    }
}

bool menu_contains(const std::vector<MenuItemSpec>& items, std::string_view id) {
    for (const MenuItemSpec& item : items) {
        if (item.stable_id == id || menu_contains(item.children, id)) return true;
    }
    return false;
}

} // namespace

ToolStrip::ToolStrip(StableId stable_id) : Control(std::move(stable_id)) {
    construct();
}

ToolStrip::ToolStrip(StableId stable_id, StatusLayout) : Control(std::move(stable_id)) {
    status_layout_ = true;
    grip_visible = false;
    sizing_grip_visible = true;
    construct();
}

void ToolStrip::construct() {
    set_focusable(true);
    // Items carry their own tooltips in their semantic nodes.
    set_publishes_item_tool_tips(true);
    cursor = CursorKind::arrow;
    set_paint_plane(PaintPlane::control);
    overlay_ = std::make_unique<ContextMenu>(std::string(stable_id().value()) + ".menu");
    overlay_invoked_ = (*overlay_).item_invoked().subscribe(
        *this, Delegate<const MenuItemInvocation&>::bind<ToolStrip, &ToolStrip::on_overlay_item_invoked>(*this));
    overlay_changed_ = (*overlay_).open_changed().subscribe(
        *this, Delegate<bool>::bind<ToolStrip, &ToolStrip::on_overlay_open_changed>(*this));
}

ToolStrip::~ToolStrip() {
    close_drop_down();
}

void ToolStrip::on_dispose() noexcept {
    close_drop_down();
    if (overlay_) (*overlay_).dispose();
}

// -- items -----------------------------------------------------------------

void ToolStrip::validate_items(const std::vector<ToolStripItemSpec>&) const {}

void ToolStrip::set_items(std::vector<ToolStripItemSpec> items) {
    apply_items(std::move(items), false);
}

void ToolStrip::apply_items(std::vector<ToolStripItemSpec> items, bool merged) {
    require_mutable();
    validate_items(items);
    std::unordered_set<std::string> item_ids{};
    std::unordered_set<std::string> menu_ids{};
    for (const ToolStripItemSpec& item : items) {
        // Hosted items and overflow menu rows carry the item ID in a
        // StableId, so every item ID follows the StableId segment rule.
        if (!valid_stable_id(item.stable_id) || !validate_utf8(item.text).valid() ||
            !validate_utf8(item.tool_tip_text).valid() || !item_ids.insert(item.stable_id).second) {
            throw std::invalid_argument(
                "ToolStrip items require unique stable IDs of '.'-separated lowercase dash segments "
                "and valid UTF-8");
        }
        if (hosted(item.kind) && (item.command || item.check_on_click || item.checked)) {
            throw std::invalid_argument("ToolStrip hosted items take no command or check");
        }
        if (item.kind != ToolStripItemKind::combo_box && !item.choices.empty()) {
            throw std::invalid_argument("ToolStrip choices belong to combo box items only");
        }
        for (const std::string& choice : item.choices) {
            if (!validate_utf8(choice).valid()) throw std::invalid_argument("ToolStrip choices must be valid UTF-8");
        }
        if (!std::isfinite(item.width) || item.width < 0.0 || item.width > maximum_hosted_width) {
            throw std::invalid_argument("ToolStrip item width must be finite and between 0 and 1024");
        }
        // Only a menu item has a menu; a merge may leave it empty.
        const bool stray_menu = !has_menu(item.kind) && !item.drop_down.empty();
        const bool missing_menu = has_menu(item.kind) && item.drop_down.empty() && !merged;
        if (stray_menu || missing_menu) {
            throw std::invalid_argument(
                "ToolStrip drop-down and split buttons require a menu, and other items have none");
        }
        if (!item.drop_down.empty()) {
            std::unordered_set<std::string> identities{};
            std::size_t count{};
            menu_detail::validate_specs(item.drop_down, identities, 1U, count, merged);
            collect_menu_ids(item.drop_down, menu_ids);
        }
    }
    // The overflow menu holds items and their menus together, so their IDs
    // may not meet.
    for (const std::string& id : menu_ids) {
        if (item_ids.count(id) != 0U) {
            throw std::invalid_argument("ToolStrip menu item IDs may not repeat an item ID");
        }
    }
    close_drop_down();
    // Hosted controls carry over to an item of the same ID and kind; the
    // others are removed. New hosted items get new controls.
    std::vector<ItemState> next_states(items.size(), ItemState{});
    std::vector<bool> kept(items_.size(), false);
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (!hosted(items[index].kind)) continue;
        for (std::size_t old = 0; old < items_.size(); ++old) {
            if (kept[old] || !item_states_[old].hosted || items_[old].stable_id != items[index].stable_id ||
                items_[old].kind != items[index].kind) {
                continue;
            }
            next_states[index].hosted = item_states_[old].hosted;
            kept[old] = true;
            break;
        }
    }
    for (std::size_t old = 0; old < items_.size(); ++old) {
        if (item_states_[old].hosted && !kept[old]) {
            static_cast<void>(remove_child((*item_states_[old].hosted).runtime_id()));
        }
    }
    items_ = std::move(items);
    item_states_ = std::move(next_states);
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (!hosted(items_[index].kind)) continue;
        if (!item_states_[index].hosted) {
            item_states_[index].hosted = make_hosted(items_[index]);
            add_child(item_states_[index].hosted);
        }
        configure_hosted(items_[index], *item_states_[index].hosted);
    }
    command_tokens_.clear();
    for (std::size_t index = 0; index < items_.size(); ++index) {
        item_states_[index].checked = items_[index].checked;
        if (items_[index].command) {
            const Delegate<const CommandState&> repaint =
                Delegate<const CommandState&>::bind<ToolStrip, &ToolStrip::on_command_state>(*this);
            SubscriptionToken token = (*items_[index].command).state_changed().subscribe(*this, repaint);
            command_tokens_.push_back(std::move(token));
        }
    }
    hot_index_.reset();
    pressed_index_.reset();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

Control::Ptr ToolStrip::make_hosted(const ToolStripItemSpec& item) const {
    // The control is named after the strip and the item.
    const StableId identity(std::string(stable_id().value()) + "." + item.stable_id);
    if (item.kind == ToolStripItemKind::combo_box) return make_control<ComboBox>(identity);
    if (item.kind == ToolStripItemKind::text_box) return make_control<TextBox>(identity);
    return make_control<ProgressBar>(identity);
}

void ToolStrip::configure_hosted(const ToolStripItemSpec& item, Control& control) const {
    control.accessible_name = item.text;
    control.tool_tip_text = item.tool_tip_text;
    control.enabled = item.enabled;
    if (item.kind == ToolStripItemKind::combo_box) {
        ComboBox& combo = static_cast<ComboBox&>(control);
        const std::span<const std::string> current = combo.items.read();
        const bool same = current.size() == item.choices.size() &&
                          std::equal(current.begin(), current.end(), item.choices.begin());
        if (!same) combo.items = item.choices;
    }
}

Control::Ptr ToolStrip::hosted_control(std::string_view item_id, ToolStripItemKind kind) const {
    const std::optional<std::size_t> index = index_of(item_id);
    if (!index || items_[*index].kind != kind || !item_states_[*index].hosted) {
        throw std::out_of_range("ToolStrip has no hosted item of this kind with this ID");
    }
    return item_states_[*index].hosted;
}

std::shared_ptr<ComboBox> ToolStrip::combo_box(std::string_view item_id) const {
    return std::static_pointer_cast<ComboBox>(hosted_control(item_id, ToolStripItemKind::combo_box));
}

std::shared_ptr<TextBox> ToolStrip::text_box(std::string_view item_id) const {
    return std::static_pointer_cast<TextBox>(hosted_control(item_id, ToolStripItemKind::text_box));
}

std::shared_ptr<ProgressBar> ToolStrip::progress_bar(std::string_view item_id) const {
    return std::static_pointer_cast<ProgressBar>(hosted_control(item_id, ToolStripItemKind::progress_bar));
}

void ToolStrip::on_command_state(const CommandState&) {
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

std::optional<std::size_t> ToolStrip::index_of(std::string_view item_id) const {
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (items_[index].stable_id == item_id) return index;
    }
    return std::nullopt;
}

std::string ToolStrip::item_text(const ToolStripItemSpec& item) const {
    if (!item.text.empty() || !item.command) return item.text;
    return (*item.command).state().text;
}

bool ToolStrip::item_enabled(const ToolStripItemSpec& item) const {
    const bool command_enabled = !item.command || (*item.command).state().enabled;
    return item.enabled && command_enabled && effectively_enabled();
}

bool ToolStrip::item_visible(const ToolStripItemSpec& item) const {
    const bool command_visible = !item.command || (*item.command).state().visible;
    return item.visible && command_visible;
}

bool ToolStrip::item_interactive(std::size_t index) const {
    const ToolStripItemSpec& item = items_[index];
    const bool result = item.kind != ToolStripItemKind::label &&
                        item.kind != ToolStripItemKind::separator && !hosted(item.kind) && item_enabled(item);
    return result;
}

void ToolStrip::set_item_text(std::string_view item_id, std::string text) {
    require_mutable();
    const std::optional<std::size_t> index = index_of(item_id);
    if (!index) throw std::out_of_range("ToolStrip has no item with this ID");
    if (!validate_utf8(text).valid()) throw std::invalid_argument("ToolStrip item text must be valid UTF-8");
    if (items_[*index].text == text) return;
    items_[*index].text = std::move(text);
    if (item_states_[*index].hosted) (*item_states_[*index].hosted).accessible_name = items_[*index].text;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

bool ToolStrip::item_checked(std::string_view item_id) const {
    const std::optional<std::size_t> index = index_of(item_id);
    if (!index) throw std::out_of_range("ToolStrip has no item with this ID");
    const ToolStripItemSpec& item = items_[*index];
    if (item.command) return (*item.command).state().checked;
    return item_states_[*index].checked;
}

void ToolStrip::set_item_checked(std::string_view item_id, bool checked) {
    require_mutable();
    const std::optional<std::size_t> index = index_of(item_id);
    if (!index) throw std::out_of_range("ToolStrip has no item with this ID");
    const ToolStripItemSpec& item = items_[*index];
    if (item.command) {
        (*item.command).set_checked(checked);
        return;
    }
    if (item_states_[*index].checked == checked) return;
    item_states_[*index].checked = checked;
    invalidate(Dirty::paint | Dirty::semantics);
}

void ToolStrip::prepare_write(const decltype(end_reserve)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.0 || incoming > maximum_end_reserve) {
        throw std::invalid_argument("ToolStrip end reserve must be finite and between 0 and 64");
    }
}

void ToolStrip::written(const decltype(end_reserve)&) {
    close_drop_down();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// -- layout ------------------------------------------------------------------

Size ToolStrip::measure(Size available) {
    const double scale = effective_text_scale();
    const ResolvedTextLayout metrics = resolve_text_layout_utf8("Mg", text_font(font.read()));
    const double preferred = std::max(strip_height * scale, metrics.logical_size.height + 10.0 * scale);
    if (!vertical()) {
        const Size result{.width = available.width, .height = std::min(available.height, preferred)};
        return result;
    }
    // A vertical strip is as wide as its widest visible item.
    double widest = 0.0;
    for (const ToolStripItemSpec& item : items_) {
        if (item_visible(item)) widest = std::max(widest, item_size(item).width);
    }
    const double width = std::max(preferred, widest + edge_inset * scale * 2.0);
    const Size result{.width = std::min(available.width, width), .height = available.height};
    return result;
}

double ToolStrip::content_length() const {
    const double scale = effective_text_scale();
    const bool along_height = vertical();
    double length = edge_inset * scale * 2.0;
    if (!status_layout_ && grip_visible.read()) length += edge_inset * scale + grip_width * scale;
    if (status_layout_ && sizing_grip_visible.read()) length += sizing_grip_size * scale;
    if (!status_layout_) length += end_reserve.read() * scale;
    for (const ToolStripItemSpec& item : items_) {
        if (!item_visible(item)) continue;
        double extent = item_size(item).width;
        if (along_height && item.kind != ToolStripItemKind::separator) extent = vertical_item_extent * scale;
        length += extent;
    }
    return length;
}

bool ToolStrip::vertical() const noexcept {
    const bool result = orientation.read() == Orientation::vertical && !status_layout_;
    return result;
}

void ToolStrip::prepare_write(const decltype(orientation)&, Orientation& incoming) {
    require_mutable();
    if (incoming != Orientation::horizontal && incoming != Orientation::vertical) {
        throw std::invalid_argument("ToolStrip orientation is invalid");
    }
    if (status_layout_ && incoming == Orientation::vertical) {
        throw std::logic_error("A StatusStrip is laid out horizontally");
    }
}

// An open drop-down was placed for the old layout, so it closes.
void ToolStrip::written(const decltype(orientation)&) {
    close_drop_down();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

Size ToolStrip::item_size(const ToolStripItemSpec& item) const {
    const double scale = effective_text_scale();
    if (item.kind == ToolStripItemKind::separator) return {separator_width * scale, 0.0};
    if (hosted(item.kind)) {
        const double fallback = item.kind == ToolStripItemKind::combo_box ? default_combo_box_width
                                                                          : default_hosted_width;
        const double width = item.width > 0.0 ? item.width : fallback;
        return {width * scale, 0.0};
    }
    const bool shows_image = item.image.value != 0U && item.display != ToolStripItemDisplay::text;
    const std::string text = item_text(item);
    const bool shows_text = !text.empty() && item.display != ToolStripItemDisplay::image;
    double width = item_padding * scale * 2.0;
    if (shows_image) width += image_size * scale;
    if (shows_image && shows_text) width += image_text_gap * scale;
    if (shows_text) {
        const ResolvedTextLayout label = resolve_text_layout_utf8(text, text_font(font.read()));
        width += label.logical_size.width;
    }
    if (item.kind == ToolStripItemKind::drop_down_button) width += drop_down_arrow_width * scale;
    if (item.kind == ToolStripItemKind::split_button) width += split_arrow_width * scale;
    if (item.kind != ToolStripItemKind::label) width = std::max(width, minimum_button_width * scale);
    return {width, 0.0};
}

ToolStrip::Layout ToolStrip::layout() const {
    return layout_for(client_rectangle());
}

ToolStrip::Layout ToolStrip::layout_for(Rect surface) const {
    const double scale = effective_text_scale();
    const double inset = edge_inset * scale;
    const std::size_t count = items_.size();
    Layout result;
    result.items.assign(count, Rect{});
    result.overflowed.assign(count, false);
    // Items run along the main axis (left to right, or top to bottom on a
    // vertical strip) and fill the strip across it.
    const bool along_height = vertical();
    const double main_extent = along_height ? surface.height : surface.width;
    const double cross_extent = along_height ? surface.width : surface.height;
    const double item_cross = std::max(0.0, cross_extent - inset * 2.0);
    double start = inset;
    double end = main_extent - inset;
    if (!status_layout_ && grip_visible.read()) {
        result.grip = axis_rect(along_height, inset, grip_width * scale, inset, item_cross);
        start = inset * 2.0 + grip_width * scale;
    }
    if (status_layout_ && sizing_grip_visible.read()) {
        const double side = sizing_grip_size * scale;
        result.sizing_grip = {surface.width - side - 1.0, std::max(0.0, surface.height - side - 1.0), side, side};
        end -= side;
    }
    if (!status_layout_ && end_reserve.read() > 0.0) {
        const double reserve = end_reserve.read() * scale;
        end -= reserve;
        result.end_reserve = axis_rect(along_height, end, reserve, inset, item_cross);
    }
    std::vector<double> widths(count, 0.0);
    std::vector<bool> shown(count, false);
    double total = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        shown[index] = item_visible(items_[index]);
        if (!shown[index]) continue;
        widths[index] = item_size(items_[index]).width;
        if (along_height && items_[index].kind != ToolStripItemKind::separator) {
            widths[index] = vertical_item_extent * scale;
        }
        // Spring labels take their width from what is left, below.
        if (!(status_layout_ && spring_label(items_[index]))) total += widths[index];
    }
    if (status_layout_) {
        // Spring labels share what the other items leave, equally.
        std::size_t springs = 0U;
        for (std::size_t index = 0; index < count; ++index) {
            if (shown[index] && spring_label(items_[index])) ++springs;
        }
        const double free = std::max(0.0, end - start - total);
        for (std::size_t index = 0; index < count; ++index) {
            if (shown[index] && spring_label(items_[index])) {
                widths[index] = free / static_cast<double>(springs);
            }
        }
    } else if (total > end - start) {
        // Overflow from the last item back, leaving room for the button.
        const double reserved = overflow_button_width * scale;
        bool moved = false;
        for (std::size_t step = count; step > 0U && total > end - reserved - start; --step) {
            const std::size_t index = step - 1U;
            // Hosted items stay on the strip: a menu row cannot hold a control.
            if (!shown[index] || items_[index].overflow != ToolStripOverflow::as_needed ||
                hosted(items_[index].kind)) {
                continue;
            }
            result.overflowed[index] = true;
            total -= widths[index];
            moved = true;
        }
        if (moved) {
            end -= reserved;
            result.overflow_button = axis_rect(along_height, end, reserved, inset, item_cross);
        }
    }
    double next = start;
    for (std::size_t index = 0; index < count; ++index) {
        if (!shown[index] || result.overflowed[index] ||
            items_[index].placement != ToolStripItemPlacement::start) continue;
        result.items[index] = axis_rect(along_height, next, widths[index], inset, item_cross);
        next += widths[index];
    }
    double last = end;
    for (std::size_t index = 0; index < count; ++index) {
        if (!shown[index] || result.overflowed[index] ||
            items_[index].placement != ToolStripItemPlacement::end) continue;
        last -= widths[index];
        result.items[index] = axis_rect(along_height, last, widths[index], inset, item_cross);
    }
    return result;
}

ToolStrip::ItemParts ToolStrip::item_parts(std::size_t index, Rect bounds) const {
    const double scale = effective_text_scale();
    const ToolStripItemSpec& item = items_[index];
    ItemParts parts;
    if (bounds.empty()) return parts;
    // A hosted control fills its item; the strip paints nothing of it.
    if (item.kind == ToolStripItemKind::combo_box) {
        parts.item_combo_box = PartGeometry::from_bounds(bounds);
        return parts;
    }
    if (item.kind == ToolStripItemKind::text_box) {
        parts.item_text_box = PartGeometry::from_bounds(bounds);
        return parts;
    }
    if (item.kind == ToolStripItemKind::progress_bar) {
        parts.item_progress_bar = PartGeometry::from_bounds(bounds);
        return parts;
    }
    if (item.kind == ToolStripItemKind::separator) {
        // A rule across the strip: upright on a horizontal strip, level on
        // a vertical one.
        const double inset = 2.0 * scale;
        Rect rule{std::floor(bounds.x + bounds.width * 0.5), bounds.y + inset, 1.0,
                  std::max(0.0, bounds.height - inset * 2.0)};
        if (vertical()) {
            rule = {bounds.x + inset, std::floor(bounds.y + bounds.height * 0.5),
                    std::max(0.0, bounds.width - inset * 2.0), 1.0};
        }
        parts.item_separator = PartGeometry::from_bounds(rule);
        return parts;
    }
    double arrow_width = 0.0;
    if (item.kind == ToolStripItemKind::drop_down_button) arrow_width = drop_down_arrow_width * scale;
    if (item.kind == ToolStripItemKind::split_button) arrow_width = split_arrow_width * scale;
    const Rect content{bounds.x, bounds.y, std::max(0.0, bounds.width - arrow_width), bounds.height};
    const Rect arrow{content.x + content.width, bounds.y, arrow_width, bounds.height};
    if (item.kind != ToolStripItemKind::label) {
        // A drop-down button is one actuator; a split button's face is its
        // main part, beside its arrow actuator.
        parts.item_face = PartGeometry::from_bounds(item.kind == ToolStripItemKind::split_button ? content : bounds);
        parts.item_focus = PartGeometry::from_bounds(bounds);
    }
    if (item.kind == ToolStripItemKind::split_button) parts.item_arrow = PartGeometry::from_bounds(arrow);
    if (has_menu(item.kind)) {
        parts.item_arrow_glyph = PartGeometry::from_bounds(centred_glyph(arrow, arrow_glyph_size * scale));
    }
    const bool shows_image = item.image.value != 0U && item.display != ToolStripItemDisplay::text;
    const std::string text = item_text(item);
    const bool shows_text = !text.empty() && item.display != ToolStripItemDisplay::image;
    const double padding = item_padding * scale;
    double x = content.x + padding;
    if (shows_image) {
        const double side = image_size * scale;
        // An image-only item centres its image.
        if (!shows_text) x = std::floor(content.x + (content.width - side) * 0.5);
        const Rect icon{x, std::floor(bounds.y + (bounds.height - side) * 0.5), side, side};
        parts.item_icon = PartGeometry::from_bounds(Rect::intersection(icon, content));
        x += side + image_text_gap * scale;
    }
    if (shows_text) {
        const ResolvedTextLayout measured = resolve_text_layout_utf8(text, text_font(font.read()));
        const Rect label{x, bounds.y, measured.logical_size.width, bounds.height};
        parts.item_label = PartGeometry::from_bounds(Rect::intersection(label, content));
    }
    return parts;
}

AnatomyStateSet ToolStrip::item_states(std::size_t index) const {
    AnatomyStateSet states;
    const ToolStripItemSpec& item = items_[index];
    const bool interactive = item_interactive(index);
    if (interactive && hot_index_ == index) states.insert(AnatomyState::hot);
    if (interactive && pressed_index_ == index) states.insert(AnatomyState::pressed);
    if (open_index_ == index) states.insert(AnatomyState::open);
    if (!item_enabled(item)) states.insert(AnatomyState::disabled);
    const bool checked = item.command ? (*item.command).state().checked : item_states_[index].checked;
    if (checked) states.insert(AnatomyState::checked);
    const Window* const host = attached_window();
    if (focused_ && hot_index_ == index && host != nullptr && (*host).focus_cue_visible()) {
        states.insert(AnatomyState::focus_visible);
    }
    return states;
}

void ToolStrip::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const Layout placed = layout_for({0.0, 0.0, final_bounds.width, final_bounds.height});
    for (std::size_t index = 0; index < items_.size(); ++index) {
        const Control::Ptr& control = item_states_[index].hosted;
        if (!control) continue;
        const Rect bounds = placed.items[index];
        set_child_layout_collapsed(control, bounds.empty());
        if (!bounds.empty()) set_child_layout(control, bounds);
    }
}

// -- paint -------------------------------------------------------------------

void ToolStrip::paint_item(Painter& painter, std::size_t index, Rect bounds) const {
    const ToolStripItemSpec& item = items_[index];
    // A hosted item's control paints itself.
    if (hosted(item.kind)) return;
    const ItemParts parts = item_parts(index, bounds);
    const AnatomyStateSet states = item_states(index);
    if (item.kind == ToolStripItemKind::separator) {
        PartPaint rule;
        rule.role = PartRole::divider;
        rule.bounds = parts.item_separator.bounds;
        rule.family = ControlVisualRole::panel;
        rule.key = PartKey{"item_separator", item.stable_id, index};
        paint_part(painter, *this, rule);
        return;
    }
    // A split button's two actuators press apart: the face shows pressed
    // only for a press on it, the arrow only for a press on the arrow.
    AnatomyStateSet face_states = states;
    AnatomyStateSet arrow_states = states;
    if (item.kind == ToolStripItemKind::split_button) {
        if (pressed_zone_ != ItemZone::main) face_states.erase(AnatomyState::pressed);
        if (pressed_zone_ != ItemZone::arrow) arrow_states.erase(AnatomyState::pressed);
    }
    // Each part is keyed by its anatomy name and its item's stable ID (the
    // item's index where it has none), so that a theme's
    // `tool-strip::part(...)` rules select it. The ID is the item's own,
    // unchanged while the strip paints.
    PartPaint part;
    part.family = ControlVisualRole::menu_item;
    part.font = text_font(font.read());
    if (parts.item_face.present) {
        part.role = PartRole::face;
        part.bounds = parts.item_face.bounds;
        part.key = PartKey{"item_face", item.stable_id, index};
        part.states = face_states;
        paint_part(painter, *this, part);
    }
    if (parts.item_arrow.present) {
        part.role = PartRole::actuator;
        part.bounds = parts.item_arrow.bounds;
        part.key = PartKey{"item_arrow", item.stable_id, index};
        part.states = arrow_states;
        paint_part(painter, *this, part);
    }
    part.states = states;
    if (parts.item_icon.present) {
        part.role = PartRole::icon;
        part.bounds = parts.item_icon.bounds;
        part.key = PartKey{"item_icon", item.stable_id, index};
        part.image = item.image;
        paint_part(painter, *this, part);
    }
    if (parts.item_label.present) {
        part.role = PartRole::label;
        part.bounds = parts.item_label.bounds;
        part.key = PartKey{"item_label", item.stable_id, index};
        const std::string text = item_text(item);
        part.text = text;
        paint_part(painter, *this, part);
        part.text = {};
    }
    if (parts.item_arrow_glyph.present) {
        part.role = PartRole::glyph;
        part.bounds = parts.item_arrow_glyph.bounds;
        part.key = PartKey{"item_arrow_glyph", item.stable_id, index};
        part.mark = PartMark::arrow_down;
        paint_part(painter, *this, part);
        part.mark = PartMark::none;
    }
    if (parts.item_focus.present) {
        part.role = PartRole::focus;
        part.bounds = parts.item_focus.bounds;
        part.key = PartKey{"item_focus", item.stable_id, index};
        paint_part(painter, *this, part);
    }
}

void ToolStrip::on_paint(Painter& painter, Rect) {
    const Layout placed = layout();
    PartPaint strip;
    strip.family = ControlVisualRole::panel;
    strip.role = PartRole::face;
    strip.bounds = client_rectangle();
    strip.key = PartKey{"face", {}, {}};
    paint_part(painter, *this, strip);
    if (!placed.grip.empty()) {
        strip.role = PartRole::grip;
        strip.bounds = placed.grip;
        strip.key = PartKey{"grip", {}, {}};
        paint_part(painter, *this, strip);
    }
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (!placed.items[index].empty()) paint_item(painter, index, placed.items[index]);
    }
    if (!placed.overflow_button.empty()) {
        PartPaint overflow;
        overflow.family = ControlVisualRole::menu_item;
        overflow.role = PartRole::actuator;
        overflow.bounds = placed.overflow_button;
        if (overflow_hot_) overflow.states.insert(AnatomyState::hot);
        if (overflow_open_) overflow.states.insert(AnatomyState::open);
        overflow.key = PartKey{"overflow_button", {}, {}};
        paint_part(painter, *this, overflow);
        overflow.role = PartRole::glyph;
        overflow.bounds = centred_glyph(placed.overflow_button, arrow_glyph_size * effective_text_scale());
        overflow.mark = PartMark::overflow;
        overflow.key = PartKey{"overflow_glyph", {}, {}};
        paint_part(painter, *this, overflow);
    }
    if (!placed.sizing_grip.empty()) {
        strip.role = PartRole::grip;
        strip.bounds = placed.sizing_grip;
        strip.key = PartKey{"sizing_grip", {}, {}};
        paint_part(painter, *this, strip);
    }
}

// -- input -------------------------------------------------------------------

std::optional<std::size_t> ToolStrip::index_at(Point window_point, ItemZone& zone) const {
    zone = ItemZone::none;
    const Rect absolute = absolute_bounds();
    if (!absolute.contains(window_point)) return std::nullopt;
    const Point local{window_point.x - absolute.x, window_point.y - absolute.y};
    const Layout placed = layout();
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (placed.items[index].empty() || !placed.items[index].contains(local)) continue;
        const ItemParts parts = item_parts(index, placed.items[index]);
        zone = parts.item_arrow.present && parts.item_arrow.bounds.contains(local) ? ItemZone::arrow : ItemZone::main;
        return index;
    }
    return std::nullopt;
}

bool ToolStrip::overflow_at(Point window_point) const {
    const Rect absolute = absolute_bounds();
    const Rect button = layout().overflow_button;
    const bool result = !button.empty() &&
        button.contains(Point{window_point.x - absolute.x, window_point.y - absolute.y});
    return result;
}

void ToolStrip::set_hot(std::optional<std::size_t> index, ItemZone zone) {
    if (hot_index_ == index && hot_zone_ == zone) return;
    hot_index_ = index;
    hot_zone_ = zone;
    invalidate(Dirty::paint | Dirty::semantics);
}

void ToolStrip::focus_item(std::optional<std::size_t> index) {
    set_hot(index, ItemZone::main);
    if (!focused_ || !index) return;
    const ToolStripItemFocus moved{std::string(stable_id().value()), items_[*index].stable_id};
    item_focus_changed_.emit(moved);
}

std::string ToolStrip::item_id_at(Point window_point) const {
    ItemZone zone{ItemZone::none};
    const std::optional<std::size_t> index = index_at(window_point, zone);
    if (!index) return {};
    return items_[*index].stable_id;
}

std::string ToolStrip::focused_item_id() const {
    if (!focused_ || !hot_index_ || *hot_index_ >= items_.size()) return {};
    return items_[*hot_index_].stable_id;
}

std::optional<std::size_t> ToolStrip::next_interactive(std::size_t start, int direction) const {
    const Layout placed = layout();
    const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(items_.size());
    if (count == 0) return std::nullopt;
    for (std::ptrdiff_t step = 1; step <= count; ++step) {
        const std::ptrdiff_t raw = static_cast<std::ptrdiff_t>(start) + static_cast<std::ptrdiff_t>(direction) * step;
        const std::size_t index = static_cast<std::size_t>((raw % count + count) % count);
        if (!placed.items[index].empty() && item_interactive(index)) return index;
    }
    return std::nullopt;
}

bool ToolStrip::flip_checked(std::size_t index) {
    const ToolStripItemSpec& item = items_[index];
    if (item.command) {
        const bool checked = (*item.command).state().checked;
        (*item.command).set_checked(!checked);
        return false;
    }
    item_states_[index].checked = !item_states_[index].checked;
    return true;
}

void ToolStrip::click(std::size_t index) {
    // A copy of the whole item: the command it executes and the click
    // handlers may replace the strip's items.
    const ToolStripItemSpec item = items_[index];
    if (!item_interactive(index)) return;
    if (item.check_on_click && flip_checked(index)) invalidate(Dirty::paint | Dirty::semantics);
    Window* const host = window();
    if (item.command && host != nullptr) {
        static_cast<void>((*item.command).execute_routed(*host, stable_id().value()));
    } else if (item.command) {
        static_cast<void>((*item.command).execute(stable_id().value()));
    }
    const ToolStripItemClick clicked{std::string(stable_id().value()), item.stable_id};
    item_clicked_.emit(clicked);
}

void ToolStrip::on_pointer(PointerEvent& event) {
    if (event.action == PointerAction::leave) {
        set_hot(std::nullopt, ItemZone::none);
        if (overflow_hot_) {
            overflow_hot_ = false;
            invalidate(Dirty::paint);
        }
        return;
    }
    ItemZone zone{ItemZone::none};
    const std::optional<std::size_t> index = index_at(event.position, zone);
    const bool on_overflow = overflow_at(event.position);
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        set_hot(index && item_interactive(*index) ? index : std::nullopt, zone);
        if (overflow_hot_ != on_overflow) {
            overflow_hot_ = on_overflow;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down) {
        if (on_overflow) {
            if (overflow_open_) close_drop_down();
            else static_cast<void>(show_overflow(MenuOpenMode::pointer));
            event.handled = true;
            return;
        }
        if (!index || !item_interactive(*index)) return;
        pressed_index_ = index;
        pressed_zone_ = zone;
        const ToolStripItemSpec& item = items_[*index];
        const bool opens = item.kind == ToolStripItemKind::drop_down_button ||
                           (item.kind == ToolStripItemKind::split_button && zone == ItemZone::arrow);
        if (opens) {
            if (open_index_ == index) close_drop_down();
            else static_cast<void>(show_drop_down(item.stable_id, MenuOpenMode::pointer));
            pressed_index_.reset();
        }
        invalidate(Dirty::paint | Dirty::semantics);
        event.handled = true;
    } else if (event.action == PointerAction::up) {
        const std::optional<std::size_t> pressed = pressed_index_;
        const ItemZone pressed_zone = pressed_zone_;
        pressed_index_.reset();
        pressed_zone_ = ItemZone::none;
        invalidate(Dirty::paint | Dirty::semantics);
        // A click is a press and release on the same part of the same item.
        if (pressed && pressed == index && pressed_zone == zone) click(*pressed);
        event.handled = pressed.has_value();
    }
}

void ToolStrip::on_key(KeyEvent& event) {
    if (!focused_ || event.action != KeyAction::down) return;
    std::optional<std::size_t> current = hot_index_;
    if (!current) current = next_interactive(items_.size() == 0U ? 0U : items_.size() - 1U, 1);
    if (!current) return;
    const ToolStripItemSpec& item = items_[*current];
    // The arrows along the strip move between items; the arrow away from
    // it (Down, or Right on a vertical strip) opens a menu.
    const bool along_height = vertical();
    const std::uint32_t previous_key = along_height ? PhysicalKey::up : PhysicalKey::left;
    const std::uint32_t next_key = along_height ? PhysicalKey::down : PhysicalKey::right;
    const std::uint32_t open_key = along_height ? PhysicalKey::right : PhysicalKey::down;
    if (event.physical_key == previous_key || event.physical_key == next_key) {
        const std::optional<std::size_t> next =
            next_interactive(*current, event.physical_key == next_key ? 1 : -1);
        if (next) focus_item(next);
    } else if (event.physical_key == PhysicalKey::enter || event.physical_key == PhysicalKey::space) {
        set_hot(current, ItemZone::main);
        if (item.kind == ToolStripItemKind::drop_down_button) {
            static_cast<void>(show_drop_down(item.stable_id));
        } else {
            click(*current);
        }
    } else if (event.physical_key == open_key && has_menu(item.kind)) {
        static_cast<void>(show_drop_down(item.stable_id));
    } else if (event.physical_key == PhysicalKey::escape) {
        close_drop_down();
    } else {
        return;
    }
    event.handled = true;
}

void ToolStrip::on_focus_changed(bool focused) {
    focused_ = focused;
    if (focused_ && !hot_index_) {
        hot_index_ = next_interactive(items_.size() == 0U ? 0U : items_.size() - 1U, 1);
    }
    invalidate(Dirty::paint | Dirty::semantics);
    if (focused_ && hot_index_) {
        const ToolStripItemFocus moved{std::string(stable_id().value()), items_[*hot_index_].stable_id};
        item_focus_changed_.emit(moved);
    }
}

// -- menus -------------------------------------------------------------------

bool ToolStrip::show_drop_down(std::string_view item_id, MenuOpenMode mode) {
    require_mutable();
    if (mode != MenuOpenMode::keyboard && mode != MenuOpenMode::pointer) {
        throw std::invalid_argument("ToolStrip opening mode is invalid");
    }
    const std::optional<std::size_t> index = index_of(item_id);
    if (!index || !has_menu(items_[*index].kind) || !item_interactive(*index) || !attached_window()) return false;
    // A merge may have left the menu empty, or holding only empty submenus.
    std::vector<MenuItemSpec> menu = menu_detail::prune_empty_submenus(items_[*index].drop_down);
    if (menu.empty()) return false;
    const Layout placed = layout();
    const Rect bounds = placed.items[*index];
    if (bounds.empty()) return false;
    if (open_index_ == index && (*overlay_).is_open()) return true;
    close_drop_down();
    (*overlay_).set_items(std::move(menu));
    const Rect absolute = absolute_bounds();
    // A menu opens below an item, or beside it on a vertical strip.
    Point anchor{absolute.x + bounds.x, absolute.y + bounds.y + bounds.height};
    if (vertical()) anchor = {absolute.x + bounds.x + bounds.width, absolute.y + bounds.y};
    (*overlay_).show(shared_from_this(), anchor, mode);
    open_index_ = index;
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

std::vector<MenuItemSpec> ToolStrip::overflow_menu() const {
    std::vector<MenuItemSpec> menu;
    const Layout placed = layout();
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (!placed.overflowed[index]) continue;
        const ToolStripItemSpec& item = items_[index];
        const std::string text = item_text(item);
        if (item.kind == ToolStripItemKind::separator) {
            menu.emplace_back(item.stable_id, MenuItemKind::separator);
        } else if (has_menu(item.kind)) {
            std::vector<MenuItemSpec> shown = menu_detail::prune_empty_submenus(item.drop_down);
            if (shown.empty()) continue;
            menu.emplace_back(item.stable_id, MenuItemKind::submenu, nullptr,
                              text.empty() ? item.stable_id : text, std::move(shown));
        } else if (item.kind == ToolStripItemKind::button) {
            // A button without a command gets a private one for the menu
            // row; choosing the row clicks the button.
            std::shared_ptr<Command> command = item.command;
            if (!command) command = item_states_[index].overflow_command;
            menu.emplace_back(item.stable_id, MenuItemKind::command, command,
                              text.empty() ? item.stable_id : text);
        }
    }
    return menu;
}

bool ToolStrip::show_overflow(MenuOpenMode mode) {
    require_mutable();
    if (mode != MenuOpenMode::keyboard && mode != MenuOpenMode::pointer) {
        throw std::invalid_argument("ToolStrip opening mode is invalid");
    }
    const Layout placed = layout();
    if (placed.overflow_button.empty() || !attached_window()) return false;
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (!placed.overflowed[index] || items_[index].command ||
            items_[index].kind != ToolStripItemKind::button) continue;
        ItemState& state = item_states_[index];
        if (!state.overflow_command) {
            state.overflow_command = std::make_shared<Command>(items_[index].stable_id, item_text(items_[index]));
        }
        (*state.overflow_command).set_text(item_text(items_[index]));
        (*state.overflow_command).set_enabled(item_enabled(items_[index]));
    }
    std::vector<MenuItemSpec> menu = overflow_menu();
    if (menu.empty()) return false;
    close_drop_down();
    (*overlay_).set_items(std::move(menu));
    const Rect absolute = absolute_bounds();
    const double right = absolute.x + placed.overflow_button.x + placed.overflow_button.width;
    Point anchor{std::max(absolute.x, right - (*overlay_).preferred_width.read()),
                 absolute.y + placed.overflow_button.y + placed.overflow_button.height};
    if (vertical()) anchor = {right, absolute.y + placed.overflow_button.y};
    (*overlay_).show(shared_from_this(), anchor, mode);
    overflow_open_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

std::vector<std::string> ToolStrip::overflow_item_ids() const {
    std::vector<std::string> ids;
    const Layout placed = layout();
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (placed.overflowed[index]) ids.push_back(items_[index].stable_id);
    }
    return ids;
}

void ToolStrip::close_drop_down() noexcept {
    if (overlay_) (*overlay_).close();
}

bool ToolStrip::drop_down_open() const noexcept {
    const bool result = overlay_ && (*overlay_).is_open();
    return result;
}

void ToolStrip::on_overlay_open_changed(bool open) {
    if (open) return;
    open_index_.reset();
    overflow_open_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
}

void ToolStrip::on_overlay_item_invoked(const MenuItemInvocation& invocation) {
    const std::string strip_id(stable_id().value());
    if (open_index_) {
        const ToolStripDropDownInvocation chosen{strip_id, items_[*open_index_].stable_id, invocation};
        drop_down_item_invoked_.emit(chosen);
        return;
    }
    if (!overflow_open_) return;
    // The menu has run the row's command; a button row is that button's click.
    const std::optional<std::size_t> index = index_of(invocation.item_id);
    if (index) {
        const ToolStripItemSpec& item = items_[*index];
        // The item is in the overflow menu, not on the strip, so the strip
        // has nothing of it to repaint.
        if (item.check_on_click) static_cast<void>(flip_checked(*index));
        const ToolStripItemClick clicked{strip_id, item.stable_id};
        item_clicked_.emit(clicked);
        return;
    }
    for (const ToolStripItemSpec& item : items_) {
        if (has_menu(item.kind) && menu_contains(item.drop_down, invocation.item_id)) {
            const ToolStripDropDownInvocation chosen{strip_id, item.stable_id, invocation};
            drop_down_item_invoked_.emit(chosen);
            return;
        }
    }
}

// -- semantics ---------------------------------------------------------------

SemanticDescriptor ToolStrip::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::toolbar;
    descriptor.name = accessible_name.read().empty() ? "Tool strip" : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> ToolStrip::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    const Rect absolute = absolute_bounds();
    const Layout placed = layout();
    for (std::size_t index = 0; index < items_.size(); ++index) {
        const Rect bounds = placed.items[index];
        if (bounds.empty()) continue;
        const ToolStripItemSpec& item = items_[index];
        // A hosted control describes itself as a child of the strip.
        if (hosted(item.kind)) continue;
        SemanticNode node{};
        node.stable_id = item.stable_id;
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.name = item_text(item);
        node.tool_tip = item.tool_tip_text;
        node.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
        node.states = SemanticState::visible;
        if (item_enabled(item)) node.states |= SemanticState::enabled;
        if (item.kind == ToolStripItemKind::separator) {
            node.role = SemanticRole::separator;
        } else if (item.kind == ToolStripItemKind::label) {
            node.role = SemanticRole::static_text;
        } else {
            // A split button clicks beside its menu arrow; a drop-down
            // button is one button that opens its menu.
            node.role = SemanticRole::button;
            if (item.kind == ToolStripItemKind::split_button) node.role = SemanticRole::split_button;
            if (item.kind == ToolStripItemKind::drop_down_button) node.role = SemanticRole::menu_button;
            node.has_popup = has_menu(item.kind);
            node.states |= SemanticState::focusable;
            const AnatomyStateSet states = item_states(index);
            // A check-on-click button can be checked whether or not it is
            // now; any other button while it is checked.
            const bool checked = states.contains(AnatomyState::checked);
            if (item.check_on_click || checked) node.states |= SemanticState::checkable;
            if (checked) node.states |= SemanticState::checked;
            if (states.contains(AnatomyState::open)) node.states |= SemanticState::expanded;
            if (focused_ && hot_index_ == index) node.states |= SemanticState::focused;
            node.actions = {SemanticAction::focus, SemanticAction::press};
            if (has_menu(item.kind)) {
                node.actions.push_back(SemanticAction::expand);
                node.actions.push_back(SemanticAction::collapse);
            }
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool ToolStrip::on_semantic_child_action(std::string_view id, SemanticAction action, std::string_view) {
    const std::optional<std::size_t> index = index_of(id);
    if (!index || layout().items[*index].empty() || !item_interactive(*index)) return false;
    const ToolStripItemSpec& item = items_[*index];
    if (action == SemanticAction::collapse) {
        if (open_index_ != index) return false;
        close_drop_down();
        return true;
    }
    if (action == SemanticAction::focus) {
        Window* const host = window();
        if (host != nullptr) static_cast<void>((*host).request_focus(shared_from_this()));
        focus_item(index);
        return true;
    }
    if (action == SemanticAction::expand) {
        if (!has_menu(item.kind)) return false;
        const bool opened = show_drop_down(item.stable_id);
        return opened;
    }
    if (action == SemanticAction::press) {
        if (item.kind == ToolStripItemKind::drop_down_button) {
            const bool opened = show_drop_down(item.stable_id);
            return opened;
        }
        click(*index);
        return true;
    }
    return false;
}

// -- anatomy -----------------------------------------------------------------

namespace {

constexpr std::array<AnatomyState, 2> overflow_button_states{{AnatomyState::hot, AnatomyState::open}};
constexpr std::array<AnatomyState, 2> arrow_states{{AnatomyState::hot, AnatomyState::pressed}};

constexpr std::array<PartField<ToolStrip::Parts>, 6> tool_strip_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &ToolStrip::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &ToolStrip::Parts::face},
    {.declaration = {.name = "grip", .role = PartRole::grip, .composed_from = {}},
     .member = &ToolStrip::Parts::grip},
    {.declaration = {.name = "overflow_button", .role = PartRole::actuator, .composed_from = "button",
                     .states = overflow_button_states},
     .member = &ToolStrip::Parts::overflow_button},
    {.declaration = {.name = "overflow_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = overflow_button_states},
     .member = &ToolStrip::Parts::overflow_glyph},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &ToolStrip::Parts::focus},
}};

constexpr std::array<PartField<ToolStrip::ItemParts>, 10> tool_strip_item_part_fields{{
    {.declaration = {.name = "item_face", .role = PartRole::face, .composed_from = {}},
     .member = &ToolStrip::ItemParts::item_face},
    {.declaration = {.name = "item_icon", .role = PartRole::icon, .composed_from = {}},
     .member = &ToolStrip::ItemParts::item_icon},
    {.declaration = {.name = "item_label", .role = PartRole::label, .composed_from = {}},
     .member = &ToolStrip::ItemParts::item_label},
    {.declaration = {.name = "item_arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = arrow_states},
     .member = &ToolStrip::ItemParts::item_arrow},
    {.declaration = {.name = "item_arrow_glyph", .role = PartRole::glyph, .composed_from = {}},
     .member = &ToolStrip::ItemParts::item_arrow_glyph},
    {.declaration = {.name = "item_separator", .role = PartRole::divider, .composed_from = {}},
     .member = &ToolStrip::ItemParts::item_separator},
    {.declaration = {.name = "item_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &ToolStrip::ItemParts::item_focus},
    {.declaration = {.name = "item_combo_box", .role = PartRole::well, .composed_from = "combo_box"},
     .member = &ToolStrip::ItemParts::item_combo_box},
    {.declaration = {.name = "item_text_box", .role = PartRole::well, .composed_from = "text_box"},
     .member = &ToolStrip::ItemParts::item_text_box},
    {.declaration = {.name = "item_progress_bar", .role = PartRole::track, .composed_from = "progress_bar"},
     .member = &ToolStrip::ItemParts::item_progress_bar},
}};

constexpr std::array<StateField<ToolStrip::ItemStates>, 6> tool_strip_item_state_fields{{
    {.state = AnatomyState::hot, .member = &ToolStrip::ItemStates::hot},
    {.state = AnatomyState::pressed, .member = &ToolStrip::ItemStates::pressed},
    {.state = AnatomyState::checked, .member = &ToolStrip::ItemStates::checked},
    {.state = AnatomyState::open, .member = &ToolStrip::ItemStates::open},
    {.state = AnatomyState::disabled, .member = &ToolStrip::ItemStates::disabled},
    {.state = AnatomyState::focus_visible, .member = &ToolStrip::ItemStates::focus_visible},
}};

constexpr std::array<PartDeclaration, 6> tool_strip_parts = part_declarations(tool_strip_part_fields);
constexpr std::array<PartDeclaration, 10> tool_strip_item_parts = part_declarations(tool_strip_item_part_fields);
constexpr std::array<AnatomyState, 6> tool_strip_item_states = state_declarations(tool_strip_item_state_fields);
constexpr std::array<AnatomyState, 5> tool_strip_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr std::array<ItemSystemDeclaration, 1> tool_strip_item_systems{{
    {.name = "item", .parts = tool_strip_item_parts, .states = tool_strip_item_states, .custom_states = {}},
}};

constexpr ControlAnatomy tool_strip_anatomy{
    .control_id = "tool_strip",
    .parts = tool_strip_parts,
    .states = tool_strip_states,
    .custom_states = {},
    .item_systems = tool_strip_item_systems,
};

} // namespace

const ControlAnatomy& ToolStrip::anatomy() const noexcept {
    return tool_strip_anatomy;
}

ToolStrip::Parts ToolStrip::strip_parts() const {
    const Layout placed = layout();
    const Rect surface = client_rectangle();
    Parts record;
    record.region = PartGeometry::from_bounds(surface);
    record.face = PartGeometry::from_bounds(surface);
    record.grip = PartGeometry::from_bounds(placed.grip);
    record.overflow_button = PartGeometry::from_bounds(placed.overflow_button);
    record.overflow_glyph = PartGeometry::from_bounds(
        centred_glyph(placed.overflow_button, arrow_glyph_size * effective_text_scale()));
    if (overflow_hot_) {
        record.overflow_button.states.insert(AnatomyState::hot);
        record.overflow_glyph.states.insert(AnatomyState::hot);
    }
    if (overflow_open_) {
        record.overflow_button.states.insert(AnatomyState::open);
        record.overflow_glyph.states.insert(AnatomyState::open);
    }
    record.focus = PartGeometry::from_bounds(surface);
    return record;
}

void ToolStrip::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Parts record = strip_parts();
    write_parts(anatomy(), tool_strip_part_fields, record, geometry);
}

ToolStrip::Parts ToolStrip::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), tool_strip_part_fields, published.parts(), record);
    return record;
}

std::size_t ToolStrip::realized_item_index(std::size_t item) const {
    const Layout placed = layout();
    std::size_t seen = 0U;
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (placed.items[index].empty()) continue;
        if (seen == item) return index;
        ++seen;
    }
    return items_.size();
}

std::size_t ToolStrip::realized_item_count() const {
    const Layout placed = layout();
    std::size_t count = 0U;
    for (const Rect& bounds : placed.items) {
        if (!bounds.empty()) ++count;
    }
    return count;
}

std::string ToolStrip::realized_item_stable_id(std::size_t item) const {
    return items_[realized_item_index(item)].stable_id;
}

void ToolStrip::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    const std::size_t index = realized_item_index(item);
    ItemParts record = item_parts(index, layout().items[index]);
    const AnatomyStateSet states = item_states(index);
    if (record.item_arrow.present) {
        if (states.contains(AnatomyState::hot)) record.item_arrow.states.insert(AnatomyState::hot);
        if (states.contains(AnatomyState::pressed) && pressed_zone_ == ItemZone::arrow) {
            record.item_arrow.states.insert(AnatomyState::pressed);
        }
    }
    write_item_parts(anatomy().item_systems.front(), tool_strip_item_part_fields, record, geometry);
}

AnatomyStateSet ToolStrip::realized_item_anatomy_states(std::size_t item) const {
    return item_states(realized_item_index(item));
}

std::vector<ToolStrip::Item> ToolStrip::realized_items() const {
    const std::size_t count = realized_item_count();
    std::vector<Item> records;
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Item record;
        record.index = realized_item_index(item);
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems.front(), tool_strip_item_part_fields, published.parts(),
                        record.parts);
        read_states(tool_strip_item_state_fields, realized_item_anatomy_states(item), record.states);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events ToolStrip adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 3> tool_strip_interactions{{
    {.name = "item_clicked", .payload = "ToolStripItemClick", .mutable_payload = false},
    {.name = "drop_down_item_invoked", .payload = "ToolStripDropDownInvocation", .mutable_payload = false},
    {.name = "item_focus_changed", .payload = "ToolStripItemFocus", .mutable_payload = false},
}};

} // namespace

void ToolStrip::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), tool_strip_interactions.begin(), tool_strip_interactions.end());
}

// property_codemod begin ToolStrip (tools/property_families/tool_strip.json; generated, do not edit)

void ToolStrip::prepare_write(const decltype(grip_visible)&, bool&) {
    require_mutable();
}

void ToolStrip::written(const decltype(grip_visible)&) {
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ToolStrip::prepare_write(const decltype(allow_merge)&, bool&) {
    require_mutable();
}

void ToolStrip::written(const decltype(allow_merge)&) {}

void ToolStrip::prepare_write(const decltype(font)&, FontSpec& incoming) {
    require_mutable();
    if (!valid_font_spec(incoming)) throw std::invalid_argument("ToolStrip font specification is invalid");
    set_font_from_code(true);
}

void ToolStrip::written(const decltype(font)&) {
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ToolStrip::prepare_write(const decltype(sizing_grip_visible)&, bool&) {
    require_mutable();
}

void ToolStrip::written(const decltype(sizing_grip_visible)&) {
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// property_codemod end ToolStrip

} // namespace gui_forms
