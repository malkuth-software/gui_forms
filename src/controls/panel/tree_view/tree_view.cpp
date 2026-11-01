#include "gui_forms/controls/panel/tree_view/tree_view.hpp"

#include "../collection_control_utilities.hpp"
#include "../cut_off_text/cut_off_text_reveal.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"

#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace gui_forms {

using namespace collection_detail;
TreeView::TreeView(StableId stable_id)
    : Panel(std::move(stable_id)), reveal_(std::make_unique<detail::CutOffTextRevealer>()) {
    set_paint_plane(PaintPlane::control);
    set_background(style().paper);
    set_border_style(BorderStyle::sunken);
    set_focusable(true);
}

TreeView::~TreeView() {}

void TreeView::set_items(std::vector<TreeViewItem> items) {
    require_mutable();
    std::unordered_set<std::string> identities;
    for (std::size_t index = 0; index < items.size(); ++index) {
        validate_identity_text(items[index].stable_id, items[index].text);
        if (items[index].image_key.size() > 256U ||
            (!items[index].image_key.empty() &&
             !validate_utf8(items[index].image_key).valid())) {
            throw std::invalid_argument("TreeView image keys must be bounded valid UTF-8");
        }
        if (!identities.insert(items[index].stable_id).second ||
            (index == 0U && items[index].depth != 0U) ||
            (index != 0U && items[index].depth > items[index - 1U].depth + 1U)) {
            throw std::invalid_argument("TreeView requires unique IDs and a valid preorder depth sequence");
        }
    }
    const std::string retained_selection = selected_id_;
    const std::string retained_active = active_id_;
    items_ = std::move(items);
    selected_id_ = item_index(retained_selection) ? retained_selection : std::string{};
    active_id_ = item_index(retained_active) ? retained_active : std::string{};
    rebuild_visible();
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

std::optional<std::size_t> TreeView::item_index(std::string_view id) const noexcept {
    std::vector<TreeViewItem>::const_iterator found = items_.begin();
    while (found != items_.end() && (*found).stable_id != id) {
        ++found;
    }
    return found == items_.end() ? std::optional<std::size_t>{}
                                : std::optional<std::size_t>{static_cast<std::size_t>(
                                      std::distance(items_.begin(), found))};
}

void TreeView::set_selected_id(std::string_view id) {
    require_mutable();
    if (!id.empty() && !item_index(id)) {
        throw std::out_of_range("TreeView selection ID is not in the model");
    }
    const std::string next(id);
    if (selected_id_ == next) return;
    TreeSelectionChange change{selected_id_, next};
    selected_id_ = next;
    if (!next.empty()) active_id_ = next;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(selection_changed_, change);
}

bool TreeView::expanded(std::string_view id) const {
    const std::optional<std::size_t> index = item_index(id);
    if (!index) throw std::out_of_range("TreeView expansion ID is not in the model");
    return items_[*index].expanded;
}

void TreeView::set_expanded(std::string_view id, bool value) {
    require_mutable();
    const std::optional<std::size_t> index = item_index(id);
    if (!index) throw std::out_of_range("TreeView expansion ID is not in the model");
    TreeViewItem& item = items_[*index];
    if (!item.expandable || item.expanded == value) return;
    bool selected_descendant{};
    if (!value) {
        if (const std::optional<std::size_t> selected = item_index(selected_id_);
            selected && *selected > *index && items_[*selected].depth > item.depth) {
            selected_descendant = true;
        }
    }
    item.expanded = value;
    // A copy: an expansion handler may replace the items, which ends
    // `item`'s lifetime and may remove the item itself.
    const std::string item_id = item.stable_id;
    rebuild_visible();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    publish_change(expansion_changed_,
                   TreeExpansionChange{item_id, value});
    if (selected_descendant && is_alive() && item_index(item_id)) set_selected_id(item_id);
}

void TreeView::set_item_height(double height) {
    require_mutable();
    if (!std::isfinite(height) || height < 14.0 || height > 256.0) {
        throw std::invalid_argument("TreeView item height must be finite and between 14 and 256");
    }
    if (item_height_ == height) return;
    item_height_ = height;
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void TreeView::set_indentation(double indentation) {
    require_mutable();
    if (!std::isfinite(indentation) || indentation < 8.0 || indentation > 96.0) {
        throw std::invalid_argument("TreeView indentation must be finite and between 8 and 96");
    }
    if (indentation_ == indentation) return;
    indentation_ = indentation;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void TreeView::set_show_expanders(bool show) {
    require_mutable();
    if (show_expanders_ == show) return;
    show_expanders_ = show;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void TreeView::set_top_row(std::size_t row) {
    require_mutable();
    if (visible_.empty()) {
        if (row != 0U) throw std::out_of_range("empty TreeView top row must be zero");
    } else if (row >= visible_.size()) {
        throw std::out_of_range("TreeView top row is outside the visible model");
    }
    if (top_row_ == row) return;
    top_row_ = row;
    conceal_cut_off_text();
    invalidate(Dirty::paint | Dirty::semantics);
}

void TreeView::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) throw std::invalid_argument("TreeView font is invalid");
    if (font_ == font) return;
    font_ = font;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void TreeView::set_image_list(std::shared_ptr<ImageList> image_list) {
    require_mutable();
    if (image_list && !(*image_list).is_alive()) {
        throw std::invalid_argument("TreeView requires a live ImageList");
    }
    if (image_list && window() && !(*image_list).belongs_to(*window())) {
        throw std::invalid_argument("TreeView and ImageList must belong to one Window");
    }
    if (image_list_ == image_list) return;
    image_list_changed_.disconnect();
    image_list_ = std::move(image_list);
    if (image_list_) {
        image_list_changed_ = (*image_list_).changed().subscribe(
            *this, Delegate<const ImageListChange&>::bind<
                TreeView, &TreeView::on_image_list_changed>(*this));
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

void TreeView::on_image_list_changed(const ImageListChange&) {
    if (is_alive()) invalidate(Dirty::paint | Dirty::semantics);
}

void TreeView::on_attached_to_window() {
    Panel::on_attached_to_window();
    if (image_list_ && !(*image_list_).belongs_to(*window())) {
        throw std::logic_error("TreeView cannot attach to a different ImageList Window");
    }
}

void TreeView::rebuild_visible() {
    visible_.clear();
    std::optional<std::size_t> collapsed_depth;
    for (std::size_t index = 0; index < items_.size(); ++index) {
        const TreeViewItem& item = items_[index];
        if (collapsed_depth && item.depth > *collapsed_depth) continue;
        collapsed_depth.reset();
        visible_.push_back(index);
        if (item.expandable && !item.expanded) collapsed_depth = item.depth;
    }
    top_row_ = visible_.empty() ? 0U : std::min(top_row_, visible_.size() - 1U);
    hovered_row_.reset();
    conceal_cut_off_text();
    pressed_row_.reset();
    pressed_expander_ = false;
}

double TreeView::text_left(const TreeViewItem& item, double row_height) const {
    const double indent = 7.0 + static_cast<double>(item.depth) * indentation_ * effective_text_scale();
    double left = indent + 14.0;
    if (image_list_ && (*image_list_).is_alive() && !item.image_key.empty()) {
        const Size logical = (*image_list_).image_size();
        const double slot_height = std::min(logical.height, std::max(0.0, row_height - 4.0));
        const double slot_width = logical.height > 0.0 ? logical.width * slot_height / logical.height : 0.0;
        left += slot_width + 5.0;
    }
    return left;
}

// The hovered node's text, revealed in place when the view's right edge
// (its paint clip) cuts it off (owner decision, 2026-10-28), painted as the
// row paints: the selection fill, else the hover fill.
void TreeView::reveal_cut_off_text(std::size_t visible_row) {
    if (visible_row >= visible_.size() || visible_row < top_row_) {
        conceal_cut_off_text();
        return;
    }
    const TreeViewItem& item = items_[visible_[visible_row]];
    const Rect bounds = absolute_bounds();
    const double row_height = item_height_ * effective_text_scale();
    const double y = 2.0 + static_cast<double>(visible_row - top_row_) * row_height;
    const double left = text_left(item, row_height);
    const double clip_right = std::max(0.0, bounds.width - 2.0);
    if (item.text.empty() || left >= clip_right) {
        conceal_cut_off_text();
        return;
    }
    const FontSpec font = effective_font(font_);
    const bool selected = item.stable_id == selected_id_;
    detail::PlainRevealPaint paint{};
    paint.face = selected ? (focused_ ? style().accent : style().accent_light) : style().face_light;
    paint.border = style().dark_border;
    paint.text = !item.enabled ? style().disabled_text : selected && focused_ ? style().highlight : style().text;
    detail::CutOffText revealed{};
    revealed.key = item.stable_id;
    revealed.row = {bounds.x + left - 2.0, bounds.y + y, clip_right - left + 2.0, row_height};
    revealed.cell = {bounds.x + left, bounds.y + y, clip_right - left, row_height};
    revealed.text = item.text;
    revealed.font = font;
    revealed.baseline = std::max(font.size, row_height * 0.5 + 4.0);
    revealed.paint = paint;
    static_cast<void>((*reveal_).reveal(shared_from_this(), std::move(revealed)));
}

void TreeView::conceal_cut_off_text() noexcept {
    if (reveal_) (*reveal_).conceal();
}

std::size_t TreeView::visible_row_count() const noexcept {
    double height = local_bounds().height;
    if (height <= 4.0) height = requested_bounds().height;
    const double row_height = item_height_ * effective_text_scale();
    return std::max<std::size_t>(1U, static_cast<std::size_t>(
        std::floor(std::max(0.0, height - 4.0) / row_height)));
}

std::optional<std::size_t> TreeView::visible_row_at(Point absolute) const noexcept {
    const Rect bounds = absolute_bounds();
    if (!bounds.contains(absolute) || absolute.y < bounds.y + 2.0) return {};
    const double row_height = item_height_ * effective_text_scale();
    const std::size_t row = top_row_ + static_cast<std::size_t>(
        std::floor((absolute.y - bounds.y - 2.0) / row_height));
    return row < visible_.size() ? std::optional<std::size_t>{row}
                                 : std::optional<std::size_t>{};
}

void TreeView::ensure_visible(std::size_t row) {
    const std::size_t count = visible_row_count();
    if (row < top_row_) top_row_ = row;
    else if (row >= top_row_ + count) top_row_ = row - count + 1U;
}

void TreeView::select_visible_row(std::size_t row, bool activate) {
    if (row >= visible_.size()) return;
    if (!items_[visible_[row]].enabled) return;
    // A reveal shows the row as it was; the next pointer move reveals the
    // row as it is.
    conceal_cut_off_text();
    // A copy: a selection handler may replace the items, which ends the
    // item's lifetime, so nothing here reads the item after it runs.
    const std::string item_id = items_[visible_[row]].stable_id;
    active_id_ = item_id;
    set_selected_id(item_id);
    if (row < visible_.size()) ensure_visible(row);
    if (activate && is_alive()) item_activated_.emit(item_id);
}

void TreeView::arrange(Rect final_bounds) {
    Panel::arrange(final_bounds);
    if (const std::optional<std::size_t> active = item_index(active_id_)) {
        const std::vector<std::size_t>::iterator row =
            std::find(visible_.begin(), visible_.end(), *active);
        if (row != visible_.end()) ensure_visible(static_cast<std::size_t>(
            std::distance(visible_.begin(), row)));
    }
}

void TreeView::on_paint(Painter& painter, Rect damage) {
    Panel::on_paint(painter, damage);
    const Rect bounds = local_bounds();
    const FontSpec font = effective_font(font_);
    const double text_scale = effective_text_scale();
    const double row_height = item_height_ * text_scale;
    painter.save();
    painter.clip_rect({2.0, 2.0, std::max(0.0, bounds.width - 4.0),
                       std::max(0.0, bounds.height - 4.0)});
    const std::size_t end = std::min(visible_.size(), top_row_ + visible_row_count() + 1U);
    const Color selected_fill = focused_ ? style().accent : style().accent_light;
    for (std::size_t row = top_row_; row < end; ++row) {
        const TreeViewItem& item = items_[visible_[row]];
        const double y = 2.0 + static_cast<double>(row - top_row_) * row_height;
        const bool selected = item.stable_id == selected_id_;
        const bool active = focused_ && item.stable_id == active_id_;
        if (selected) {
            painter.fill_rect({2.0, y, std::max(0.0, bounds.width - 4.0), row_height}, selected_fill);
        } else if (hovered_row_ == row) {
            painter.fill_rect({2.0, y, std::max(0.0, bounds.width - 4.0), row_height},
                              style().face_light);
        }
        const double indent = 7.0 + static_cast<double>(item.depth) *
            indentation_ * text_scale;
        if (show_expanders_ && item.expandable) {
            const Color arrow = item.enabled ? style().dark_border : style().disabled_text;
            if (item.expanded) {
                painter.draw_line({indent, y + 9.0}, {indent + 4.0, y + 13.0}, arrow, 1.4);
                painter.draw_line({indent + 4.0, y + 13.0}, {indent + 8.0, y + 9.0}, arrow, 1.4);
            } else {
                painter.draw_line({indent + 2.0, y + 7.0}, {indent + 6.0, y + 11.0}, arrow, 1.4);
                painter.draw_line({indent + 6.0, y + 11.0}, {indent + 2.0, y + 15.0}, arrow, 1.4);
            }
        }
        const Color text = !item.enabled ? style().disabled_text
            : selected && focused_ ? style().highlight : style().text;
        const double image_x = indent + 14.0;
        if (image_list_ && (*image_list_).is_alive() && !item.image_key.empty()) {
            const ImageVisualState state = !item.enabled
                ? ImageVisualState::disabled
                : selected ? ImageVisualState::selected
                : hovered_row_ == row ? ImageVisualState::hot
                                      : ImageVisualState::normal;
            const double scale = window() ? (*window()).scale() : 1.0;
            const ImageListResolution resolved =
                (*image_list_).resolve(item.image_key, state, scale);
            const Size logical = (*image_list_).image_size();
            const double slot_height = std::min(logical.height,
                                                std::max(0.0, row_height - 4.0));
            const double slot_width = logical.height > 0.0
                ? logical.width * slot_height / logical.height : 0.0;
            if (resolved && slot_width > 0.0 && slot_height > 0.0) {
                const Rect destination = fit_image_rect(
                    {image_x, y + (row_height - slot_height) * 0.5,
                     slot_width, slot_height},
                    resolved.source_size);
                painter.draw_image(
                    resolved.image, destination,
                    !item.enabled &&
                            resolved.resolved_state != ImageVisualState::disabled
                        ? 0.45 : 1.0);
            }
        }
        const double content_x = text_left(item, row_height);
        painter.draw_text_utf8({content_x,
                                y + std::max(font.size, row_height * 0.5 + 4.0)},
                               item.text, font, text);
        if (active) {
            painter.stroke_rect({3.5, y + 1.5, std::max(0.0, bounds.width - 7.0),
                                 std::max(0.0, row_height - 3.0)},
                                selected ? style().highlight : style().accent, 1.0);
        }
    }
    painter.restore();
}

void TreeView::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    if (event.action == PointerAction::move) {
        const std::optional<std::size_t> row = visible_row_at(event.position);
        if (row != hovered_row_) { hovered_row_ = row; invalidate(Dirty::paint); }
        if (row) {
            reveal_cut_off_text(*row);
        } else {
            conceal_cut_off_text();
        }
        return;
    }
    if (event.action == PointerAction::leave) {
        hovered_row_.reset();
        conceal_cut_off_text();
        invalidate(Dirty::paint);
        return;
    }
    // A press goes to the row under the reveal, which then shows the row as
    // the press leaves it.
    if (event.action == PointerAction::down) conceal_cut_off_text();
    if (event.action == PointerAction::wheel && !visible_.empty() &&
        event.wheel_delta.y != 0.0) {
        const std::ptrdiff_t next = std::clamp<std::ptrdiff_t>(
            static_cast<std::ptrdiff_t>(top_row_) +
                (event.wheel_delta.y > 0.0 ? -3 : 3),
            0, static_cast<std::ptrdiff_t>(visible_.size() - 1U));
        set_top_row(static_cast<std::size_t>(next));
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::down && event.button == PointerButton::primary) {
        if (window()) static_cast<void>((*window()).request_focus(shared_from_this()));
        pressed_row_ = visible_row_at(event.position);
        pressed_click_count_ = event.click_count;
        pressed_expander_ = false;
        if (!pressed_row_) return;
        const TreeViewItem& item = items_[visible_[*pressed_row_]];
        const double local_x = event.position.x - absolute_bounds().x;
        const double arrow_x = 7.0 + static_cast<double>(item.depth) *
            indentation_ * effective_text_scale();
        if (show_expanders_ && item.expandable && local_x >= arrow_x - 3.0 &&
            local_x <= arrow_x + 12.0) {
            pressed_expander_ = true;
            set_expanded(item.stable_id, !item.expanded);
        } else {
            select_visible_row(*pressed_row_, false);
        }
        event.handled = true;
    } else if (event.action == PointerAction::up &&
               event.button == PointerButton::primary) {
        const std::optional<std::size_t> row = visible_row_at(event.position);
        if (row && pressed_row_ == row && !pressed_expander_) {
            select_visible_row(*row, pressed_click_count_ >= 2U);
        }
        pressed_row_.reset();
        pressed_expander_ = false;
        event.handled = row.has_value();
    }
}

void TreeView::on_key(KeyEvent& event) {
    if (!focused_ || !enabled() || event.action != KeyAction::down || visible_.empty()) return;
    std::optional<std::size_t> active_index_value = item_index(active_id_);
    std::vector<std::size_t>::iterator active_row_iterator = active_index_value
        ? std::find(visible_.begin(), visible_.end(), *active_index_value)
        : visible_.end();
    std::size_t row = active_row_iterator == visible_.end() ? 0U
        : static_cast<std::size_t>(std::distance(visible_.begin(), active_row_iterator));
    if (event.physical_key == PhysicalKey::enter) {
        select_visible_row(row, true); event.handled = true; return;
    }
    if (event.physical_key == PhysicalKey::up && row > 0U) --row;
    else if (event.physical_key == PhysicalKey::down && row + 1U < visible_.size()) ++row;
    else if (event.physical_key == PhysicalKey::home) row = 0U;
    else if (event.physical_key == PhysicalKey::end) row = visible_.size() - 1U;
    else if (event.physical_key == PhysicalKey::page_up) {
        row = row > visible_row_count() ? row - visible_row_count() : 0U;
    } else if (event.physical_key == PhysicalKey::page_down) {
        row = std::min(row + visible_row_count(), visible_.size() - 1U);
    } else if (event.physical_key == PhysicalKey::right) {
        TreeViewItem& item = items_[visible_[row]];
        if (item.expandable && !item.expanded) set_expanded(item.stable_id, true);
        else if (row + 1U < visible_.size() &&
                 items_[visible_[row + 1U]].depth > item.depth) ++row;
    } else if (event.physical_key == PhysicalKey::left) {
        TreeViewItem& item = items_[visible_[row]];
        if (item.expandable && item.expanded) set_expanded(item.stable_id, false);
        else if (item.depth > 0U) {
            while (row > 0U && items_[visible_[row]].depth >= item.depth) --row;
        }
    } else return;
    select_visible_row(row, false);
    event.handled = true;
}

void TreeView::type_select(std::string_view text) {
    const std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now();
    if (now - last_type_time_ > type_timeout) type_prefix_.clear();
    last_type_time_ = now;
    type_prefix_ += fold_ascii(text);
    if (visible_.empty()) return;
    std::size_t start{};
    if (const std::optional<std::size_t> active = item_index(active_id_)) {
        const std::vector<std::size_t>::iterator found =
            std::find(visible_.begin(), visible_.end(), *active);
        if (found != visible_.end()) start = (static_cast<std::size_t>(
            std::distance(visible_.begin(), found)) + 1U) % visible_.size();
    }
    for (std::size_t offset = 0; offset < visible_.size(); ++offset) {
        const std::size_t row = (start + offset) % visible_.size();
        if (fold_ascii(items_[visible_[row]].text).starts_with(type_prefix_)) {
            select_visible_row(row, false);
            return;
        }
    }
}

void TreeView::on_text_input(TextInputEvent& event) {
    if (!focused_ || !enabled() || event.composing || event.text_utf8.empty() ||
        !validate_utf8(event.text_utf8).valid()) return;
    type_select(event.text_utf8);
    event.handled = true;
}

void TreeView::on_focus_changed(bool focused) {
    focused_ = focused;
    if (focused_ && active_id_.empty() && !visible_.empty()) {
        active_id_ = selected_id_.empty() ? items_[visible_.front()].stable_id : selected_id_;
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

SemanticDescriptor TreeView::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::tree;
    descriptor.name = accessible_name();
    descriptor.description = accessible_description();
    descriptor.value = selected_id_;
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> TreeView::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes;
    const std::size_t end = std::min(visible_.size(), top_row_ + visible_row_count());
    nodes.reserve(end - std::min(top_row_, end));
    const Rect tree_bounds = absolute_bounds();
    const double row_height = item_height_ * effective_text_scale();
    for (std::size_t row = top_row_; row < end; ++row) {
        const TreeViewItem& item = items_[visible_[row]];
        SemanticNode node;
        node.stable_id = item.stable_id;
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::tree_item;
        node.name = item.text;
        // The items are published flat, in display order; the level says
        // how deep each one is (counting from one at the top level).
        node.level = item.depth + 1U;
        node.bounds = {tree_bounds.x + 2.0,
                       tree_bounds.y + 2.0 +
                           static_cast<double>(row - top_row_) * row_height,
                       std::max(0.0, tree_bounds.width - 4.0), row_height};
        node.states = SemanticState::visible | SemanticState::focusable;
        if (effectively_enabled() && item.enabled) node.states |= SemanticState::enabled;
        if (item.stable_id == selected_id_) node.states |= SemanticState::selected;
        if (focused_ && item.stable_id == active_id_) node.states |= SemanticState::focused;
        if (item.expandable && item.expanded) node.states |= SemanticState::expanded;
        node.actions = {SemanticAction::focus, SemanticAction::select, SemanticAction::press};
        if (item.expandable) node.actions.push_back(item.expanded
            ? SemanticAction::collapse : SemanticAction::expand);
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool TreeView::on_semantic_child_action(std::string_view id,
                                        SemanticAction action,
                                        std::string_view) {
    const std::optional<std::size_t> index = item_index(id);
    if (!index) return false;
    const std::vector<std::size_t>::iterator row =
        std::find(visible_.begin(), visible_.end(), *index);
    if (row == visible_.end()) return false;
    if (action == SemanticAction::expand || action == SemanticAction::collapse) {
        set_expanded(id, action == SemanticAction::expand);
        return true;
    }
    if (action != SemanticAction::focus && action != SemanticAction::select &&
        action != SemanticAction::press) return false;
    if (window()) static_cast<void>((*window()).request_focus(shared_from_this()));
    select_visible_row(static_cast<std::size_t>(std::distance(visible_.begin(), row)),
                       action == SemanticAction::press);
    return true;
}



namespace {

constexpr std::array<PartField<TreeView::Parts>, 3> tree_view_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &TreeView::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &TreeView::Parts::edge},
    {.declaration = {.name = "content", .role = PartRole::content, .composed_from = {}},
     .member = &TreeView::Parts::content},
}};

constexpr std::array<PartField<TreeView::ItemParts>, 5> tree_view_item_part_fields{{
    {.declaration = {.name = "row", .role = PartRole::region, .composed_from = {}},
     .member = &TreeView::ItemParts::row},
    {.declaration = {.name = "expander", .role = PartRole::disclosure, .composed_from = {}},
     .member = &TreeView::ItemParts::expander},
    {.declaration = {.name = "icon", .role = PartRole::icon, .composed_from = {}},
     .member = &TreeView::ItemParts::icon},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &TreeView::ItemParts::label},
    {.declaration = {.name = "row_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &TreeView::ItemParts::row_focus},
}};

constexpr std::array<StateField<TreeView::ItemStates>, 6> tree_view_item_state_fields{{
    {.state = AnatomyState::selected, .member = &TreeView::ItemStates::selected},
    {.state = AnatomyState::hot, .member = &TreeView::ItemStates::hot},
    {.state = AnatomyState::current, .member = &TreeView::ItemStates::current},
    {.state = AnatomyState::open, .member = &TreeView::ItemStates::open},
    {.state = AnatomyState::closed, .member = &TreeView::ItemStates::closed},
    {.state = AnatomyState::disabled, .member = &TreeView::ItemStates::disabled},
}};

constexpr std::array<PartDeclaration, 5> tree_view_item_parts =
    part_declarations(tree_view_item_part_fields);
constexpr std::array<AnatomyState, 6> tree_view_item_states =
    state_declarations(tree_view_item_state_fields);

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 3>
    tree_view_parts = joined_declarations(scrollable_detail::parts,
                                         part_declarations(tree_view_extra_part_fields));

constexpr std::array<ItemSystemDeclaration, 1> tree_view_item_systems{{
    {.name = "item", .parts = tree_view_item_parts, .states = tree_view_item_states,
     .custom_states = {}},
}};

constexpr ControlAnatomy tree_view_anatomy{
    .control_id = "tree_view",
    .parts = tree_view_parts,
    .states = scrollable_detail::states,
    .custom_states = {},
    .item_systems = tree_view_item_systems,
};

} // namespace

const ControlAnatomy& TreeView::anatomy() const noexcept {
    return tree_view_anatomy;
}

void TreeView::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.scrollable = scrollable_parts();

    const Panel::Parts panel = panel_parts();
    record.face = panel.face;
    record.edge = panel.edge;
    const Rect bounds = local_bounds();
    record.content = PartGeometry::from_bounds(
        {2.0, 2.0, std::max(0.0, bounds.width - 4.0), std::max(0.0, bounds.height - 4.0)});
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), tree_view_extra_part_fields, record, geometry);
}

TreeView::Parts TreeView::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), tree_view_extra_part_fields, published.parts(), record);
    return record;
}

std::size_t TreeView::realized_item_count() const {
    if (top_row_ >= visible_.size()) return 0U;
    // Paint realizes one row past the fully visible ones.
    const std::size_t end = std::min(visible_.size(), top_row_ + visible_row_count() + 1U);
    return end - top_row_;
}

std::string TreeView::realized_item_stable_id(std::size_t item) const {
    return items_[visible_[top_row_ + item]].stable_id;
}

void TreeView::publish_realized_item_geometry(std::size_t item,
                                          std::span<PartGeometry> geometry) const {
    ItemParts record;

    const Rect bounds = local_bounds();
    const Rect content{2.0, 2.0, std::max(0.0, bounds.width - 4.0),
                       std::max(0.0, bounds.height - 4.0)};
    const TreeViewItem& node = items_[visible_[top_row_ + item]];
    const double text_scale = effective_text_scale();
    const double row_height = item_height_ * text_scale;
    const double y = 2.0 + static_cast<double>(item) * row_height;
    const Rect row{2.0, y, std::max(0.0, bounds.width - 4.0), row_height};
    const double indent = 7.0 + static_cast<double>(node.depth) * indentation_ * text_scale;
    if (show_expanders_ && node.expandable) {
        // The extent of the two 1.4-unit strokes on_paint draws.
        const Rect expander = node.expanded
            ? Rect{indent - 0.7, y + 8.3, 9.4, 5.4}
            : Rect{indent + 1.3, y + 6.3, 5.4, 9.4};
        record.expander = PartGeometry::from_bounds(Rect::intersection(expander, content));
    }
    double content_x = indent + 14.0;
    if (image_list_ && (*image_list_).is_alive() && !node.image_key.empty()) {
        // The image slot; on_paint fits the image inside it.
        const Size logical = (*image_list_).image_size();
        const double slot_height = std::min(logical.height, std::max(0.0, row_height - 4.0));
        const double slot_width = logical.height > 0.0
            ? logical.width * slot_height / logical.height : 0.0;
        if (slot_width > 0.0 && slot_height > 0.0) {
            const Rect slot{content_x, y + (row_height - slot_height) * 0.5,
                            slot_width, slot_height};
            record.icon = PartGeometry::from_bounds(Rect::intersection(slot, content));
            content_x += slot_width + 5.0;
        }
    }
    const double text_width =
        resolve_text_layout_utf8(node.text, effective_font(font_)).logical_size.width;
    const Rect label{content_x, y, text_width, row_height};
    record.row = PartGeometry::from_bounds(Rect::intersection(row, content));
    record.label = PartGeometry::from_bounds(Rect::intersection(Rect::intersection(label, row), content));
    record.row_focus = record.row;
    write_item_parts(anatomy().item_systems.front(), tree_view_item_part_fields, record, geometry);
}

AnatomyStateSet TreeView::realized_item_anatomy_states(std::size_t item) const {
    AnatomyStateSet states;
    const std::size_t row = top_row_ + item;
    const TreeViewItem& node = items_[visible_[row]];
    if (node.stable_id == selected_id_) states.insert(AnatomyState::selected);
    if (hovered_row_ == row) states.insert(AnatomyState::hot);
    if (node.stable_id == active_id_) states.insert(AnatomyState::current);
    if (node.expandable) {
        states.insert(node.expanded ? AnatomyState::open : AnatomyState::closed);
    }
    if (!node.enabled) states.insert(AnatomyState::disabled);
    return states;
}

std::vector<TreeView::Item> TreeView::realized_items() const {
    const std::size_t count = realized_item_count();
    std::vector<Item> records;
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Item record;
        record.index = visible_[top_row_ + item];
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems.front(), tree_view_item_part_fields, published.parts(), record.parts);
        const AnatomyStateSet item_states = realized_item_anatomy_states(item);
        read_states(tree_view_item_state_fields, item_states, record.states);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events TreeView adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 3> tree_view_interactions{{
    {.name = "selection_changed", .payload = "TreeSelectionChange", .mutable_payload = false},
    {.name = "expansion_changed", .payload = "TreeExpansionChange", .mutable_payload = false},
    {.name = "item_activated", .payload = "std::string", .mutable_payload = false},
}};

} // namespace

void TreeView::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), tree_view_interactions.begin(), tree_view_interactions.end());
}

} // namespace gui_forms
