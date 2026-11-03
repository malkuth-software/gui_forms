#include "gui_forms/controls/panel/object_view/object_view.hpp"

#include "../collection_control_utilities.hpp"
#include "../details_columns.hpp"
#include "../../basic/basic_control_rendering.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <vector>
#include <string>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace gui_forms {

namespace {

void account_object_text(const std::string_view text, std::size_t& total) {
    if (text.size() > ObjectView::maximum_details_text_bytes - total) {
        throw std::invalid_argument("ObjectView aggregate model text exceeds 64 MiB development guard");
    }
    total += text.size();
}

void validate_object_text_budget(const std::vector<ObjectDetailsColumn>& columns,
                                const std::vector<ObjectViewItem>& items) {
    std::size_t total = 0U;
    for (const ObjectDetailsColumn& column : columns) {
        account_object_text(column.id.value, total);
        account_object_text(column.label, total);
    }
    for (const ObjectViewItem& item : items) {
        if (item.cells.size() != columns.size()) {
            throw std::invalid_argument("ObjectView requires one cell per column");
        }
        account_object_text(item.stable_id, total);
        account_object_text(item.name, total);
        account_object_text(item.secondary_text, total);
        account_object_text(item.description, total);
        account_object_text(item.image_key, total);
        for (const ObjectDetailsCell& cell : item.cells) {
            account_object_text(cell.column.value, total);
            account_object_text(cell.text, total);
        }
    }
}

struct IconLabelLayout final {
    std::vector<std::string> lines;
    bool truncated{};
};

bool ascii_space(const char value) noexcept {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

std::string_view trim_ascii_space(std::string_view value) noexcept {
    while (!value.empty() && ascii_space(value.front())) value.remove_prefix(1U);
    while (!value.empty() && ascii_space(value.back())) value.remove_suffix(1U);
    return value;
}

std::size_t maximum_fitting_end(Painter& painter, std::string_view text,
                                FontSpec font, const double maximum_width) {
    if (text.empty() || maximum_width <= 0.0) return 0U;
    if (painter.measure_text_utf8(text, font).width <= maximum_width) {
        return text.size();
    }
    const TextStore store(text);
    std::size_t grapheme = store.grapheme_count().value();
    while (grapheme > 0U) {
        --grapheme;
        const std::size_t end =
            store.utf8_offset(GraphemeIndex(grapheme)).value();
        if (end != 0U && painter.measure_text_utf8(
                text.substr(0U, end), font).width <= maximum_width) {
            return end;
        }
    }
    return 0U;
}

std::size_t preferred_break(std::string_view text,
                            const std::size_t fitting_end) noexcept {
    if (fitting_end >= text.size()) return fitting_end;
    for (std::size_t index = fitting_end; index > 0U; --index) {
        if (ascii_space(text[index - 1U])) return index - 1U;
    }
    return fitting_end;
}

IconLabelLayout icon_label_layout(Painter& painter, std::string_view text,
                                  FontSpec font, const double maximum_width) {
    IconLabelLayout result;
    if (text.empty() || maximum_width <= 0.0) return result;
    if (painter.measure_text_utf8(text, font).width <= maximum_width) {
        result.lines.emplace_back(text);
        return result;
    }

    // Prefer a balanced authored word break whenever the complete name fits
    // in two lines. This keeps stable baselines without needlessly eliding a
    // name that the accepted two-line object cell can disclose.
    std::size_t balanced_break{};
    double balanced_delta = std::numeric_limits<double>::max();
    for (std::size_t index = 1U; index + 1U < text.size(); ++index) {
        if (!ascii_space(text[index])) continue;
        const std::string_view first = trim_ascii_space(text.substr(0U, index));
        const std::string_view second = trim_ascii_space(text.substr(index + 1U));
        if (first.empty() || second.empty()) continue;
        const double first_width = painter.measure_text_utf8(first, font).width;
        const double second_width = painter.measure_text_utf8(second, font).width;
        if (first_width > maximum_width || second_width > maximum_width) continue;
        const double delta = std::abs(first_width - second_width);
        if (delta < balanced_delta) {
            balanced_delta = delta;
            balanced_break = index;
        }
    }
    if (balanced_break != 0U) {
        result.lines.emplace_back(trim_ascii_space(
            text.substr(0U, balanced_break)));
        result.lines.emplace_back(trim_ascii_space(
            text.substr(balanced_break + 1U)));
        return result;
    }

    const std::size_t fit = maximum_fitting_end(
        painter, text, font, maximum_width);
    if (fit == 0U) {
        result.lines.push_back(elide_text_end(
            painter, text, font, maximum_width));
        result.truncated = true;
        return result;
    }
    const std::size_t split = preferred_break(text, fit);
    const std::size_t first_end = split == 0U ? fit : split;
    std::string_view first = trim_ascii_space(text.substr(0U, first_end));
    std::size_t remainder_start = first_end;
    while (remainder_start < text.size() &&
           ascii_space(text[remainder_start])) {
        ++remainder_start;
    }
    result.lines.emplace_back(first);
    const std::string_view remainder = text.substr(remainder_start);
    const std::string second = elide_text_end(
        painter, remainder, font, maximum_width);
    if (!second.empty()) result.lines.push_back(second);
    result.truncated = second != remainder;
    return result;
}

std::vector<std::string> wrap_complete_name(Painter& painter,
                                            std::string_view text,
                                            FontSpec font,
                                            const double maximum_width) {
    std::vector<std::string> lines;
    std::string_view remaining = text;
    while (!remaining.empty()) {
        const std::size_t fit = maximum_fitting_end(
            painter, remaining, font, maximum_width);
        if (fit >= remaining.size()) {
            lines.emplace_back(remaining);
            break;
        }
        const std::size_t split = preferred_break(remaining, fit);
        const std::size_t end = split == 0U ? fit : split;
        if (end == 0U) {
            const TextStore store(remaining);
            const std::size_t forced = store.utf8_offset(
                GraphemeIndex(std::min<std::size_t>(
                    1U, store.grapheme_count().value()))).value();
            lines.emplace_back(remaining.substr(0U, forced));
            remaining.remove_prefix(forced);
            continue;
        }
        lines.emplace_back(trim_ascii_space(remaining.substr(0U, end)));
        remaining.remove_prefix(end);
        while (!remaining.empty() && ascii_space(remaining.front())) {
            remaining.remove_prefix(1U);
        }
    }
    return lines;
}

// The bubble is a tool tip's face and its lines are its label parts, drawn
// by the theme's engine for `control`.
void paint_complete_name_inspection(Painter& painter, const Control& control,
                                    const ObjectViewItem& item,
                                    Rect anchor, Rect viewport,
                                    FontSpec authored_font) {
    if (item.name.empty() || viewport.width <= 20.0 || viewport.height <= 20.0) {
        return;
    }
    const FontSpec inspection_font{
        authored_font.role, std::max(8.0, authored_font.size - 2.0),
        400, false, authored_font.letter_spacing};
    const double maximum_text_width = std::max(
        8.0, std::min(244.0, viewport.width - 20.0));
    const std::vector<std::string> lines = wrap_complete_name(
        painter, item.name, inspection_font, maximum_text_width);
    if (lines.empty()) return;
    double text_width{};
    double line_height = std::max(
        inspection_font.size * 1.25,
        painter.measure_text_utf8("Ag", inspection_font).height);
    for (const std::string& line : lines) {
        text_width = std::max(
            text_width, painter.measure_text_utf8(line, inspection_font).width);
    }
    const double bubble_width = std::min(
        std::max(20.0, viewport.width - 8.0), std::max(72.0, text_width + 16.0));
    const double bubble_height = line_height * static_cast<double>(lines.size()) + 12.0;
    double x = anchor.x + (anchor.width - bubble_width) * .5;
    x = std::clamp(x, viewport.x + 4.0,
                   std::max(viewport.x + 4.0,
                            viewport.x + viewport.width - bubble_width - 4.0));
    double y = anchor.y + anchor.height - 8.0;
    if (y + bubble_height > viewport.y + viewport.height - 4.0) {
        y = anchor.y - bubble_height + 8.0;
    }
    y = std::clamp(y, viewport.y + 4.0,
                   std::max(viewport.y + 4.0,
                            viewport.y + viewport.height - bubble_height - 4.0));
    const Rect bubble{x, y, bubble_width, bubble_height};
    PartPaint face;
    face.family = ControlVisualRole::tool_tip;
    face.role = PartRole::face;
    face.bounds = bubble;
    paint_part(painter, control, face);
    double baseline = bubble.y + 6.0 + line_height * .82;
    for (const std::string& line : lines) {
        PartPaint label;
        label.family = ControlVisualRole::tool_tip;
        label.role = PartRole::label;
        label.text = line;
        label.font = inspection_font;
        paint_text_part_at(painter, control, label, {bubble.x + 8.0, baseline},
                           std::max(0.0, bubble.width - 16.0));
        baseline += line_height;
    }
}

} // namespace

using namespace collection_detail;
ObjectView::ObjectView(StableId stable_id) : Panel(std::move(stable_id)) {
    set_paint_plane(PaintPlane::control);
    set_background(style().paper);
    set_focusable(true);
}

void ObjectView::set_items(std::vector<ObjectViewItem> items) {
    set_details_model(details_columns_, std::move(items));
}

void ObjectView::set_details_model(std::vector<ObjectDetailsColumn> next_columns,
                                  std::vector<ObjectViewItem> items) {
    replace_details_model(std::move(next_columns), std::move(items), std::nullopt);
}

void ObjectView::set_details_model(std::vector<ObjectDetailsColumn> next_columns,
                                  std::vector<ObjectViewItem> items,
                                  ObjectDetailsSort accepted_sort) {
    replace_details_model(std::move(next_columns), std::move(items), std::move(accepted_sort));
}

void ObjectView::replace_details_model(std::vector<ObjectDetailsColumn> next_columns,
                                      std::vector<ObjectViewItem> items,
                                      std::optional<ObjectDetailsSort> accepted_sort) {
    require_mutable();
    if (next_columns.size() > 64U || items.size() > 1'000'000U) {
        throw std::invalid_argument("ObjectView model exceeds development bounds");
    }
    // This nonallocating pass runs before identity-index allocation or moving
    // cells. By-value input storage already belongs to this invocation.
    validate_object_text_budget(next_columns, items);
    std::unordered_map<std::string, std::size_t> column_indices{};
    for (std::size_t index = 0U; index < next_columns.size(); ++index) {
        const ObjectDetailsColumn& column = next_columns[index];
        validate_identity_text(column.id.value, column.label);
        if (column.id.value.size() > 256U || column.label.size() > 65536U ||
            !std::isfinite(column.width) || !std::isfinite(column.minimum_width) ||
            !std::isfinite(column.maximum_width) || column.minimum_width < 40.0 ||
            column.maximum_width > 4096.0 || column.width < column.minimum_width ||
            column.width > column.maximum_width ||
            (column.alignment != ObjectColumnAlignment::left &&
             column.alignment != ObjectColumnAlignment::right)) {
            throw std::invalid_argument("ObjectView column identity, width or alignment is invalid");
        }
        const std::pair<std::unordered_map<std::string, std::size_t>::iterator, bool> inserted =
            column_indices.emplace(column.id.value, index);
        if (!inserted.second) throw std::invalid_argument("ObjectView duplicate column identity");
    }
    if (accepted_sort) {
        const ObjectDetailsSort& requested = *accepted_sort;
        if (requested.direction != ObjectSortDirection::ascending &&
            requested.direction != ObjectSortDirection::descending) {
            throw std::invalid_argument("ObjectView accepted sort direction is invalid");
        }
        if (!requested.column.value.empty()) {
            const std::unordered_map<std::string, std::size_t>::const_iterator sorted =
                column_indices.find(requested.column.value);
            if (sorted == column_indices.end() || !next_columns[(*sorted).second].sortable) {
                throw std::invalid_argument("ObjectView accepted sort names an unavailable column");
            }
        }
    }
    ItemIndices next_indices{};
    next_indices.reserve(items.size());
    std::vector<ObjectDetailsCell> normalized_cells(next_columns.size());
    for (std::size_t index = 0U; index < items.size(); ++index) {
        ObjectViewItem& item = items[index];
        validate_identity_text(item.stable_id, item.name);
        if (item.stable_id.size() > 65536U || item.name.size() > 65536U ||
            item.secondary_text.size() > 65536U || item.description.size() > 65536U ||
            !validate_utf8(item.secondary_text).valid() ||
            !validate_utf8(item.description).valid() ||
            item.image_key.size() > 256U ||
            (!item.image_key.empty() && !validate_utf8(item.image_key).valid()) ||
            item.cells.size() != next_columns.size()) {
            throw std::invalid_argument("ObjectView requires unique IDs and valid UTF-8");
        }
        const std::pair<ItemIndices::iterator, bool> inserted = next_indices.emplace(item.stable_id, index);
        if (!inserted.second) throw std::invalid_argument("ObjectView duplicate item identity");
        std::array<bool, 64U> seen{};
        for (ObjectDetailsCell& cell : item.cells) {
            const std::unordered_map<std::string, std::size_t>::const_iterator found =
                column_indices.find(cell.column.value);
            if (found == column_indices.end() || cell.text.size() > 65536U ||
                !validate_utf8(cell.text).valid() ||
                (cell.availability != ObjectCellAvailability::available && cell.text.empty()) ||
                (cell.availability != ObjectCellAvailability::available &&
                 cell.availability != ObjectCellAvailability::unavailable &&
                 cell.availability != ObjectCellAvailability::not_applicable)) {
                throw std::invalid_argument("ObjectView cell identity, text or availability is invalid");
            }
            const std::size_t position = (*found).second;
            if (seen[position]) throw std::invalid_argument("ObjectView duplicate cell identity");
            seen[position] = true;
            normalized_cells[position] = std::move(cell);
        }
        item.cells.swap(normalized_cells);
    }
    const std::unordered_set<std::string> prior_selection(selected_ids_.begin(), selected_ids_.end());
    std::vector<std::string> next_selection{};
    next_selection.reserve(selected_ids_.size());
    for (const ObjectViewItem& item : items) {
        if (prior_selection.contains(item.stable_id)) {
            next_selection.push_back(item.stable_id);
        }
    }
    SelectionIds next_selected_lookup(next_selection.begin(), next_selection.end());
    std::string next_primary{};
    if (next_indices.contains(selected_id_)) next_primary = selected_id_;
    else if (!next_selection.empty()) next_primary = next_selection.front();
    std::string next_focus{};
    if (next_indices.contains(focused_id_)) next_focus = focused_id_;
    std::string next_anchor = next_primary;
    if (next_indices.contains(selection_anchor_id_)) next_anchor = selection_anchor_id_;
    std::size_t next_top = top_row_;
    const std::size_t old_top_index = top_row_ * columns();
    if (old_top_index < items_.size()) {
        const ItemIndices::const_iterator top =
            next_indices.find(items_[old_top_index].stable_id);
        if (top != next_indices.end()) next_top = (*top).second / columns();
    }
    ObjectDetailsSort next_sort{};
    if (accepted_sort) {
        next_sort = std::move(*accepted_sort);
    } else {
        next_sort = details_sort_;
        const std::unordered_map<std::string, std::size_t>::const_iterator sorted_column =
            column_indices.find(next_sort.column.value);
        if (sorted_column == column_indices.end() || !next_columns[(*sorted_column).second].sortable) next_sort = {};
    }
    std::size_t next_focused_column = 0U;
    if (focused_column_ < details_columns_.size()) {
        const std::unordered_map<std::string, std::size_t>::const_iterator focused_column =
            column_indices.find(details_columns_[focused_column_].id.value);
        if (focused_column != column_indices.end()) next_focused_column = (*focused_column).second;
    }
    ObjectSelectionChange change{};
    change.previous_id = selected_id_;
    change.current_id = next_primary;
    change.previous_ids = selected_ids_;
    change.current_ids = next_selection;
    const bool selection_changed = selected_id_ != next_primary || selected_ids_ != next_selection;
    // All allocating preparation has succeeded. Publish complete owned values
    // before invalidation or synchronous notifications can observe this object.
    const bool release_resize_capture = resizing_column_ && has_pointer_capture();
    pressed_column_.reset();
    resizing_column_.reset();
    items_.swap(items);
    item_indices_.swap(next_indices);
    details_columns_.swap(next_columns);
    selected_ids_.swap(next_selection);
    selected_lookup_.swap(next_selected_lookup);
    selected_id_.swap(next_primary);
    focused_id_.swap(next_focus);
    selection_anchor_id_.swap(next_anchor);
    details_sort_ = std::move(next_sort);
    top_row_ = std::min(next_top, maximum_top_row());
    focused_column_ = next_focused_column;
    if (details_columns_.empty()) header_focused_ = false;
    clamp_horizontal_offset();
    clear_details_cache();
    hovered_index_.reset();
    pressed_index_.reset();
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    // Capture notification is externally observable. Retire the old press and
    // publish all new state before it can reenter. Keep this control alive and
    // suppress a stale selection publication if that callback replaces it.
    const Control::Ptr lifetime = weak_from_this().lock();
    if (release_resize_capture && window()) (*window()).release_pointer();
    if (selection_changed && is_alive() && selected_id_ == change.current_id &&
        selected_ids_ == change.current_ids) publish_change(selection_changed_, change);
}

std::optional<std::size_t> ObjectView::item_index(const std::string_view id) const noexcept {
    const ItemIndices::const_iterator found = item_indices_.find(id);
    if (found != item_indices_.end()) return (*found).second;
    return {};
}

void ObjectView::set_view_mode(const ObjectViewMode mode) {
    require_mutable();
    if (mode != ObjectViewMode::icons && mode != ObjectViewMode::details) {
        throw std::invalid_argument("ObjectView mode is invalid");
    }
    if (view_mode_ == mode) return;
    const Control::Ptr lifetime = weak_from_this().lock();
    const std::size_t top_index = top_row_ * columns();
    view_mode_ = mode;
    top_row_ = std::min(top_index / columns(), maximum_top_row());
    header_focused_ = false;
    clear_details_cache();
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    // Publish the mode before capture-release observers run. They may dispose,
    // detach, or replace mode/model; no outer setter work follows notification.
    cancel_header_interaction();
}

void ObjectView::set_selected_id(std::string_view id) {
    require_mutable();
    if (!id.empty() && !item_index(id)) {
        throw std::out_of_range("ObjectView selection ID is not in the model");
    }
    const std::string next(id);
    apply_selection(next.empty() ? std::vector<std::string>{}
                                 : std::vector<std::string>{next},
                    next, next, false);
}

void ObjectView::set_selected_ids(std::vector<std::string> ids,
                                  const std::string_view primary_id) {
    require_mutable();
    if (selection_mode_ == ObjectSelectionMode::single && ids.size() > 1U) {
        throw std::invalid_argument(
            "ObjectView single-selection mode accepts at most one selected ID");
    }
    std::string primary(primary_id);
    std::string anchor(primary_id);
    if (primary.empty()) {
        if (std::find(ids.begin(), ids.end(), selected_id_) != ids.end()) primary = selected_id_;
        if (!ids.empty() && item_index(selection_anchor_id_)) anchor = selection_anchor_id_;
    }
    apply_selection(std::move(ids), std::move(primary), std::move(anchor), false);
}

void ObjectView::clear_selection() {
    require_mutable();
    apply_selection({}, {}, {}, false);
}

void ObjectView::select_all() {
    require_mutable();
    if (items_.empty()) {
        clear_selection();
        return;
    }
    std::vector<std::string> ids;
    ids.reserve(selection_mode_ == ObjectSelectionMode::single ? 1U : items_.size());
    for (const ObjectViewItem& item : items_) {
        if (item.enabled) {
            ids.push_back(item.stable_id);
            if (selection_mode_ == ObjectSelectionMode::single) break;
        }
    }
    const std::string primary = ids.empty() ? std::string{} : ids.front();
    apply_selection(std::move(ids), primary, primary, true);
}

void ObjectView::set_selection_mode(ObjectSelectionMode mode) {
    require_mutable();
    if (selection_mode_ == mode) return;
    selection_mode_ = mode;
    if (mode == ObjectSelectionMode::single && selected_ids_.size() > 1U) {
        const std::string keep = selected_id_.empty() ? selected_ids_.front()
                                                       : selected_id_;
        apply_selection({keep}, keep, keep, false);
    }
    invalidate(Dirty::semantics);
}

bool ObjectView::is_selected(const std::string_view id) const noexcept {
    const bool result = selected_lookup_.contains(id);
    return result;
}

void ObjectView::apply_selection(std::vector<std::string> ids,
                                 std::string primary,
                                 std::string anchor,
                                 const bool move_focus) {
    SelectionIds requested{};
    for (const std::string& id : ids) {
        if (!validate_utf8(id).valid() || !item_index(id)) {
            throw std::out_of_range("ObjectView selected ID is not in the model");
        }
        requested.insert(id);
    }
    if (selection_mode_ == ObjectSelectionMode::single && requested.size() > 1U) {
        throw std::invalid_argument(
            "ObjectView single-selection mode accepts at most one selected ID");
    }
    std::vector<std::string> normalized{};
    normalized.reserve(requested.size());
    for (const ObjectViewItem& item : items_) {
        if (requested.contains(item.stable_id)) normalized.push_back(item.stable_id);
    }
    if (normalized.empty()) {
        primary.clear();
    } else if (primary.empty()) {
        primary = normalized.front();
    } else if (std::find(normalized.begin(), normalized.end(), primary) ==
               normalized.end()) {
        throw std::invalid_argument(
            "ObjectView primary selection must be one of the selected IDs");
    }
    if (!anchor.empty() && !item_index(anchor)) {
        throw std::out_of_range("ObjectView selection anchor is not in the model");
    }
    const bool changed = selected_ids_ != normalized || selected_id_ != primary;
    std::string next_focus = focused_id_;
    if (move_focus) next_focus = primary;
    std::size_t next_top = top_row_;
    if (move_focus) {
        const std::optional<std::size_t> focus_index = item_index(next_focus);
        if (focus_index) {
            const std::size_t row = *focus_index / columns();
            const std::size_t count = fully_visible_row_count();
            if (row < next_top) next_top = row;
            else if (row >= next_top + count) next_top = row - count + 1U;
        }
    }
    ObjectSelectionChange change{};
    if (changed) {
        change.previous_id = selected_id_;
        change.current_id = primary;
        change.previous_ids = selected_ids_;
        change.current_ids = normalized;
    }
    selected_ids_.swap(normalized);
    selected_lookup_.swap(requested);
    selected_id_.swap(primary);
    selection_anchor_id_.swap(anchor);
    focused_id_.swap(next_focus);
    top_row_ = next_top;
    if (!changed) {
        if (move_focus) invalidate(Dirty::paint | Dirty::semantics);
        return;
    }
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(selection_changed_, change);
}

std::vector<std::string> ObjectView::range_selection(
    std::size_t target_index, bool preserve_existing) const {
    std::vector<std::string> result = preserve_existing ? selected_ids_
                                                        : std::vector<std::string>{};
    std::optional<std::size_t> resolved_anchor = item_index(selection_anchor_id_);
    if (!resolved_anchor) resolved_anchor = item_index(focused_id_);
    if (!resolved_anchor) resolved_anchor = item_index(selected_id_);
    const std::size_t anchor_index = resolved_anchor.value_or(target_index);
    const std::size_t first = std::min(anchor_index, target_index);
    const std::size_t last = std::max(anchor_index, target_index);
    for (std::size_t index = first; index <= last; ++index) {
        if (!items_[index].enabled ||
            std::find(result.begin(), result.end(), items_[index].stable_id) !=
                result.end()) continue;
        result.push_back(items_[index].stable_id);
    }
    return result;
}

void ObjectView::set_icon_cell_size(Size size) {
    require_mutable();
    if (!std::isfinite(size.width) || !std::isfinite(size.height) ||
        size.width < 86.0 || size.height < 78.0 ||
        size.width > 512.0 || size.height > 512.0) {
        throw std::invalid_argument("ObjectView icon cell must be 86x78 through 512x512");
    }
    if (icon_cell_size_ == size) return;
    icon_cell_size_ = size;
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ObjectView::set_details_row_height(double height) {
    require_mutable();
    if (!std::isfinite(height) || height < 20.0 || height > 256.0) {
        throw std::invalid_argument("ObjectView details row height must be 20 through 256");
    }
    if (details_row_height_ == height) return;
    details_row_height_ = height;
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ObjectView::set_show_secondary_text(bool show) {
    require_mutable();
    if (show_secondary_text_ == show) return;
    show_secondary_text_ = show;
    invalidate(Dirty::paint | Dirty::semantics);
}

void ObjectView::set_top_row(const std::size_t row) {
    require_mutable();
    const std::size_t rows = items_.empty() ? 0U : (items_.size() + columns() - 1U) / columns();
    if (rows == 0U) {
        if (row != 0U) throw std::out_of_range("empty ObjectView top row must be zero");
    } else if (row >= rows) throw std::out_of_range("ObjectView top row is outside the model");
    const std::size_t next_top = std::min(row, maximum_top_row());
    if (top_row_ == next_top) return;
    top_row_ = next_top;
    invalidate(Dirty::paint | Dirty::semantics);
}

void ObjectView::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) throw std::invalid_argument("ObjectView font is invalid");
    if (font_ == font) return;
    font_ = font;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void ObjectView::set_image_list(std::shared_ptr<ImageList> image_list) {
    require_mutable();
    if (image_list && !(*image_list).is_alive()) {
        throw std::invalid_argument("ObjectView requires a live ImageList");
    }
    if (image_list && window() && !(*image_list).belongs_to(*window())) {
        throw std::invalid_argument("ObjectView and ImageList must belong to one Window");
    }
    if (image_list_ == image_list) return;
    image_list_changed_.disconnect();
    image_list_ = std::move(image_list);
    if (image_list_) {
        image_list_changed_ = (*image_list_).changed().subscribe(
            *this, Delegate<const ImageListChange&>::bind<
                ObjectView, &ObjectView::image_list_content_changed>(*this));
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

void ObjectView::image_list_content_changed(const ImageListChange&) {
    if (is_alive()) invalidate(Dirty::paint | Dirty::semantics);
}

bool ObjectView::paint_item_image(
    Painter& painter, const ObjectViewItem& item,
    std::size_t index, bool selected, Rect destination_bounds) {
    if (!image_list_ || !(*image_list_).is_alive() || item.image_key.empty()) {
        return false;
    }
    const ImageVisualState state = !item.enabled
        ? ImageVisualState::disabled
        : selected ? ImageVisualState::selected
        : hovered_index_ == index ? ImageVisualState::hot
                                  : ImageVisualState::normal;
    const ImageListResolution resolved = (*image_list_).resolve(
        item.image_key, state, window() ? (*window()).scale() : 1.0);
    if (!resolved) return false;
    const Rect destination = fit_image_rect(
        destination_bounds, resolved.source_size);
    // The icon role dims a disabled item's image; an image list's own
    // disabled image is drawn as given.
    PartPaint icon;
    icon.family = ControlVisualRole::editor;
    icon.role = resolved.resolved_state == ImageVisualState::disabled ? PartRole::image
                                                                       : PartRole::icon;
    icon.image = resolved.image;
    icon.bounds = destination;
    if (!item.enabled) icon.states.insert(AnatomyState::disabled);
    paint_part(painter, *this, icon);
    return true;
}

void ObjectView::on_attached_to_window() {
    Panel::on_attached_to_window();
    if (image_list_ && !(*image_list_).belongs_to(*window())) {
        throw std::logic_error("ObjectView cannot attach to a different ImageList Window");
    }
}

std::size_t ObjectView::columns() const noexcept {
    if (view_mode_ == ObjectViewMode::details) return 1U;
    double width = local_bounds().width;
    if (width <= 4.0) width = requested_bounds().width;
    const double scaled_width = icon_cell_size_.width * effective_text_scale();
    const double available = std::max(0.0, width - 8.0);
    const std::size_t count = static_cast<std::size_t>(std::floor(available / scaled_width));
    const std::size_t result = std::max<std::size_t>(1U, count);
    return result;
}

double ObjectView::row_height() const noexcept {
    const double base = view_mode_ == ObjectViewMode::icons ? icon_cell_size_.height : details_row_height_;
    const double result = base * effective_text_scale();
    return result;
}

std::size_t ObjectView::visible_row_count() const noexcept {
    double height = local_bounds().height;
    if (height <= 4.0) height = requested_bounds().height;
    const double available = std::max(0.0, height - 4.0 - header_height());
    const std::size_t count = static_cast<std::size_t>(std::ceil(available / row_height()));
    const std::size_t result = std::max<std::size_t>(1U, count);
    return result;
}

std::size_t ObjectView::fully_visible_row_count() const noexcept {
    double height = local_bounds().height;
    if (height <= 4.0) height = requested_bounds().height;
    const double available = std::max(0.0, height - 4.0 - header_height());
    // A partially visible final row must remain reachable in its entirety.
    const std::size_t result = std::max<std::size_t>(1U,
        static_cast<std::size_t>(std::floor(available / row_height())));
    return result;
}

std::size_t ObjectView::maximum_top_row() const noexcept {
    const std::size_t column_count = columns();
    const std::size_t rows = items_.empty() ? 0U : (items_.size() - 1U) / column_count + 1U;
    const std::size_t full_rows = fully_visible_row_count();
    const std::size_t result = rows > full_rows ? rows - full_rows : 0U;
    return result;
}

Rect ObjectView::item_bounds(const std::size_t index) const noexcept {
    const std::size_t column_count = columns();
    const std::size_t row = index / column_count;
    const std::size_t column = index % column_count;
    const double available_width = std::max(0.0, local_bounds().width - 8.0);
    const double cell_width = view_mode_ == ObjectViewMode::icons
        ? available_width / static_cast<double>(column_count) : available_width;
    const double row_difference = static_cast<double>(row) - static_cast<double>(top_row_);
    const Rect result{4.0 + static_cast<double>(column) * cell_width,
            2.0 + header_height() + row_difference * row_height(), cell_width, row_height()};
    return result;
}

std::optional<std::size_t> ObjectView::index_at(const Point absolute) const noexcept {
    const Rect bounds = absolute_bounds();
    if (!bounds.contains(absolute)) return {};
    const double local_x = absolute.x - bounds.x - 4.0;
    const double local_y = absolute.y - bounds.y - 2.0 - header_height();
    if (local_x < 0.0 || local_y < 0.0) return {};
    const std::size_t column_count = columns();
    const double cell_width = std::max(1.0, (bounds.width - 8.0) / column_count);
    const std::size_t column = view_mode_ == ObjectViewMode::details ? 0U
        : std::min(column_count - 1U, static_cast<std::size_t>(local_x / cell_width));
    const std::size_t row = top_row_ + static_cast<std::size_t>(local_y / row_height());
    const std::size_t index = row * column_count + column;
    if (index < items_.size()) return index;
    return {};
}

std::string_view ObjectView::item_id_at(const Point absolute) const noexcept {
    const std::optional<std::size_t> index = index_at(absolute);
    if (!index) return {};
    const std::string_view result = items_[*index].stable_id;
    return result;
}

void ObjectView::ensure_visible(std::size_t index) {
    const std::size_t row = index / columns();
    const std::size_t count = fully_visible_row_count();
    if (row < top_row_) top_row_ = row;
    else if (row >= top_row_ + count) top_row_ = row - count + 1U;
}

void ObjectView::focus_index(std::size_t index) {
    if (index >= items_.size() || !items_[index].enabled) return;
    if (focused_id_ != items_[index].stable_id) {
        focused_id_ = items_[index].stable_id;
        invalidate(Dirty::paint | Dirty::semantics);
    }
    ensure_visible(index);
}

void ObjectView::select_index(std::size_t index, bool activate,
                              Modifier modifiers) {
    if (index >= items_.size() || !items_[index].enabled) return;
    const std::string target = items_[index].stable_id;
    const bool toggle = selection_mode_ == ObjectSelectionMode::multiple &&
        (has_modifier(modifiers, Modifier::control) ||
         has_modifier(modifiers, Modifier::meta));
    const bool extend = selection_mode_ == ObjectSelectionMode::multiple &&
        has_modifier(modifiers, Modifier::shift);
    if (extend) {
        const std::string anchor = selection_anchor_id_.empty()
            ? (focused_id_.empty() ? target : focused_id_)
            : selection_anchor_id_;
        apply_selection(range_selection(index, toggle), target, anchor, true);
    } else if (toggle) {
        std::vector<std::string> ids = selected_ids_;
        const std::vector<std::string>::iterator found =
            std::find(ids.begin(), ids.end(), target);
        if (found == ids.end()) ids.push_back(target);
        else ids.erase(found);
        const std::string primary = std::find(ids.begin(), ids.end(), target) != ids.end()
            ? target : ids.empty() ? std::string{} : ids.back();
        apply_selection(std::move(ids), primary, target, false);
        focus_index(index);
    } else {
        apply_selection({target}, target, target, true);
    }
    ensure_visible(index);
    if (activate && is_alive()) item_activated_.emit(target);
}

void ObjectView::arrange(const Rect final_bounds) {
    const std::size_t top_index = top_row_ * columns();
    Panel::arrange(final_bounds);
    const std::size_t column_count = columns();
    top_row_ = std::min(top_index / column_count, maximum_top_row());
    clamp_horizontal_offset();
    // Window metric-provider replacement invalidates measure/layout even when
    // authored font fields are unchanged. Do not retain elision across it.
    clear_details_cache();
}

// A glyph is a well (the folder's body or the page) with the glyph's colour
// or the paper as its ground, framed by the theme's edge, and content fills
// for the folder's tab and the page's band and marks: the colours are the
// item kind's own, as a swatch's are. A disabled item's well is disabled
// and its colour the face's.
void ObjectView::paint_glyph(Painter& painter, Rect b, ObjectGlyph glyph,
                             bool item_enabled) const {
    const Color base = !item_enabled ? style().face
        : glyph == ObjectGlyph::folder ? Color::rgba(79, 137, 196)
        : glyph == ObjectGlyph::image ? Color::rgba(83, 154, 119)
        : glyph == ObjectGlyph::audio ? Color::rgba(151, 95, 173)
        : glyph == ObjectGlyph::archive ? Color::rgba(190, 135, 61)
        : glyph == ObjectGlyph::code ? Color::rgba(67, 132, 135)
        : Color::rgba(107, 126, 158);
    AnatomyStateSet states{};
    if (!item_enabled) states.insert(AnatomyState::disabled);
    PartPaint well;
    well.family = ControlVisualRole::editor;
    well.role = PartRole::well;
    well.states = states;
    well.corner_radius = 0.0;
    PartPaint fill;
    fill.family = ControlVisualRole::editor;
    fill.role = PartRole::fill;
    fill.states = states;
    if (glyph == ObjectGlyph::folder) {
        well.bounds = {b.x + 3.0, b.y + 9.0, b.width - 6.0, b.height - 12.0};
        well.authored_background = base;
        paint_part(painter, *this, well);
        fill.bounds = {b.x + 7.0, b.y + 4.0, b.width * .42, 5.0};
        fill.content_color = with_alpha(style().highlight, 180);
        paint_part(painter, *this, fill);
        fill.bounds = {b.x + 5.0, b.y + 12.0, b.width - 10.0, 3.0};
        fill.content_color = with_alpha(style().highlight, 130);
        paint_part(painter, *this, fill);
        return;
    }
    well.bounds = {b.x + 7.0, b.y + 2.0, b.width - 14.0, b.height - 4.0};
    well.authored_background = style().paper;
    paint_part(painter, *this, well);
    fill.bounds = {b.x + 8.0, b.y + 3.0, b.width - 16.0, 6.0};
    fill.content_color = base;
    paint_part(painter, *this, fill);
    if (glyph == ObjectGlyph::image) {
        fill.bounds = {b.x + 12.0, b.y + 14.0, b.width - 24.0, b.height - 22.0};
        fill.content_color = with_alpha(base, 120);
        paint_part(painter, *this, fill);
    } else if (glyph == ObjectGlyph::audio) {
        // A sound bar across the page where the stroke rose.
        fill.bounds = {b.x + 15.0, b.y + b.height * .5 - 1.5, b.width - 29.0, 3.0};
        fill.content_color = base;
        paint_part(painter, *this, fill);
    } else {
        fill.content_color = style().border;
        fill.bounds = {b.x + 12.0, b.y + 16.0, b.width - 24.0, 2.0};
        paint_part(painter, *this, fill);
        fill.bounds = {b.x + 12.0, b.y + 22.0, b.width - 29.0, 2.0};
        paint_part(painter, *this, fill);
    }
}

void ObjectView::on_paint(Painter& painter, Rect damage) {
    Panel::on_paint(painter, damage);
    if (has_details_columns()) {
        paint_details(painter);
        return;
    }
    const Rect bounds = local_bounds();
    const FontSpec font = effective_font(font_);
    painter.save();
    painter.clip_rect({2.0, 2.0, std::max(0.0, bounds.width - 4.0),
                       std::max(0.0, bounds.height - 4.0)});
    const std::size_t first = top_row_ * columns();
    const std::size_t end = std::min(items_.size(),
        (top_row_ + visible_row_count() + 1U) * columns());
    std::optional<std::size_t> hovered_truncated;
    std::optional<std::size_t> focused_truncated;
    // Plates, item text and row rules are parts the theme's engine draws; an
    // item is hot under the pointer, selected when selected and disabled
    // with its item. A style override supplies the plates' grounds (T4-1).
    AnatomyStateSet states = anatomy_states();
    states.erase(AnatomyState::hot);
    const bool overridden = has_style_override();
    const Color selected_fill = focused_ ? style().accent_light : with_alpha(style().accent_light, 190);
    for (std::size_t index = first; index < end; ++index) {
        const ObjectViewItem& item = items_[index];
        const Rect cell = item_bounds(index);
        painter.save();
        painter.clip_rect({cell.x + 1.0, cell.y + 1.0,
                           std::max(0.0, cell.width - 2.0),
                           std::max(0.0, cell.height - 2.0)});
        const bool selected = is_selected(item.stable_id);
        const bool active = focused_ && item.stable_id == focused_id_;
        const bool hovered = hovered_index_ == index;
        AnatomyStateSet item_states = states;
        if (hovered) item_states.insert(AnatomyState::hot);
        if (selected) item_states.insert(AnatomyState::selected);
        if (!item.enabled) item_states.insert(AnatomyState::disabled);
        if (selected || hovered) {
            PartPaint plate;
            plate.family = ControlVisualRole::selection;
            plate.role = PartRole::selection;
            plate.bounds = {cell.x + 1.0, cell.y + 1.0, std::max(0.0, cell.width - 2.0),
                            std::max(0.0, cell.height - 2.0)};
            plate.states = item_states;
            if (overridden) plate.authored_background = selected ? selected_fill : style().face_light;
            paint_part(painter, *this, plate);
        }
        PartPaint text;
        text.family = selected || hovered ? ControlVisualRole::selection : ControlVisualRole::editor;
        text.role = PartRole::content;
        text.states = item_states;
        text.font = font;
        if (view_mode_ == ObjectViewMode::icons) {
            const double glyph_width = std::min(46.0, cell.width - 18.0);
            const Rect glyph_bounds{cell.x + (cell.width - glyph_width) * .5,
                                    cell.y + 4.0, glyph_width, 43.0};
            if (!paint_item_image(
                    painter, item, index, selected, glyph_bounds)) {
                paint_glyph(painter, glyph_bounds, item.glyph, item.enabled);
            }
            const IconLabelLayout label = icon_label_layout(
                painter, item.name, font, std::max(0.0, cell.width - 8.0));
            const double line_height = std::max(
                font.size * 1.2,
                painter.measure_text_utf8("Ag", font).height);
            double baseline = cell.y + std::max(57.0, 47.0 + font.size);
            for (const std::string& line : label.lines) {
                const Size measured = painter.measure_text_utf8(line, font);
                const double text_x = cell.x +
                    std::max(4.0, (cell.width - measured.width) * .5);
                text.text = line;
                paint_text_part_at(painter, *this, text, {text_x, baseline}, measured.width);
                baseline += line_height;
            }
            if (label.truncated) {
                if (hovered_index_ == index) hovered_truncated = index;
                if (active && window() && (*window()).focus_cue_visible()) {
                    focused_truncated = index;
                }
            }
            if (show_secondary_text_ && !item.secondary_text.empty() &&
                cell.height >= 90.0 && baseline + 8.0 < cell.y + cell.height) {
                FontSpec authored_secondary{font_.role,
                                            std::max(8.0, font_.size - 1.5),
                                            400, false};
                const FontSpec secondary = effective_font(authored_secondary);
                const std::string display_secondary = elide_text_end(
                    painter, item.secondary_text, secondary,
                    std::max(0.0, cell.width - 10.0));
                painter.draw_text_utf8(
                    {cell.x + 5.0, cell.y + cell.height - 5.0},
                    display_secondary, secondary, style().disabled_text);
            }
        } else {
            const Rect glyph_bounds{cell.x + 6.0, cell.y + 3.0, 24.0,
                                    std::max(18.0, cell.height - 6.0)};
            if (!paint_item_image(
                    painter, item, index, selected, glyph_bounds)) {
                paint_glyph(painter, glyph_bounds, item.glyph, item.enabled);
            }
            const bool paint_secondary = show_secondary_text_ &&
                !item.secondary_text.empty() && cell.width >= 112.0;
            const double secondary_x = paint_secondary
                ? std::max(cell.x + 92.0, cell.x + cell.width * .68)
                : cell.x + cell.width - 6.0;
            const double name_width = std::max(
                0.0, secondary_x - (cell.x + 38.0) -
                    (paint_secondary ? 8.0 : 0.0));
            const std::string display_name = elide_text_end(
                painter, item.name, font, name_width);
            text.text = display_name;
            paint_text_part_at(painter, *this, text,
                               {cell.x + 38.0, cell.y + cell.height * .5 + 4.0}, name_width);
            const bool name_truncated = display_name != item.name;
            if (name_truncated) {
                if (hovered_index_ == index) hovered_truncated = index;
                if (active && window() && (*window()).focus_cue_visible()) {
                    focused_truncated = index;
                }
            }
            if (paint_secondary) {
                const std::string display_secondary = elide_text_end(
                    painter, item.secondary_text, font,
                    std::max(0.0, cell.x + cell.width - 6.0 - secondary_x));
                painter.draw_text_utf8(
                    {secondary_x, cell.y + cell.height * .5 + 4.0},
                    display_secondary, font, style().disabled_text);
            }
            PartPaint rule;
            rule.family = ControlVisualRole::editor;
            rule.role = PartRole::divider;
            rule.bounds = {cell.x + 2.0, cell.y + cell.height - 1.5,
                           std::max(0.0, cell.width - 4.0), 1.0};
            rule.states = states;
            paint_part(painter, *this, rule);
        }
        if (active && window() && (*window()).focus_cue_visible()) {
            paint_focus(painter, cell, style().text);
        }
        painter.restore();
    }
    const std::optional<std::size_t> inspection = hovered_truncated
        ? hovered_truncated : focused_truncated;
    if (inspection && *inspection < items_.size()) {
        paint_complete_name_inspection(
            painter, *this, items_[*inspection], item_bounds(*inspection), bounds,
            font);
    }
    painter.restore();
}

void ObjectView::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    if (has_details_columns() && details_pointer(event)) return;
    if (event.action == PointerAction::move) {
        const std::optional<std::size_t> next = index_at(event.position);
        if (next != hovered_index_) { hovered_index_ = next; invalidate(Dirty::paint); }
        return;
    }
    if (event.action == PointerAction::leave) {
        hovered_index_.reset(); invalidate(Dirty::paint); return;
    }
    if (event.action == PointerAction::wheel && !items_.empty() &&
        event.wheel_delta.y != 0.0) {
        const std::size_t rows = (items_.size() + columns() - 1U) / columns();
        const std::ptrdiff_t next = std::clamp<std::ptrdiff_t>(
            static_cast<std::ptrdiff_t>(top_row_) +
                (event.wheel_delta.y > 0.0 ? -2 : 2), 0,
            static_cast<std::ptrdiff_t>(rows - 1U));
        set_top_row(static_cast<std::size_t>(next)); event.handled = true; return;
    }
    if (event.action == PointerAction::down &&
        (event.button == PointerButton::primary ||
         event.button == PointerButton::secondary)) {
        if (window()) static_cast<void>((*window()).request_focus(shared_from_this()));
        pressed_index_ = index_at(event.position);
        pressed_click_count_ = event.click_count;
        pressed_button_ = event.button;
        if (pressed_index_) {
            if (event.button == PointerButton::secondary) {
                if (!is_selected(items_[*pressed_index_].stable_id)) {
                    select_index(*pressed_index_, false);
                } else {
                    focus_index(*pressed_index_);
                }
            } else {
                select_index(*pressed_index_, false, event.modifiers);
            }
        } else if (event.button == PointerButton::primary &&
                   !has_modifier(event.modifiers, Modifier::shift) &&
                   !has_modifier(event.modifiers, Modifier::control) &&
                   !has_modifier(event.modifiers, Modifier::meta)) {
            clear_selection();
        }
        // A deliberate background click belongs to the collection even when
        // it clears selection and has no item target.
        event.handled = true;
    } else if (event.action == PointerAction::up) {
        const std::optional<std::size_t> index = index_at(event.position);
        if (index && pressed_index_ == index && event.button == pressed_button_) {
            if (pressed_button_ == PointerButton::secondary) {
                context_requested_.emit({items_[*index].stable_id, event.position});
            } else if (pressed_button_ == PointerButton::primary &&
                       pressed_click_count_ >= 2U && items_[*index].enabled &&
                       is_alive()) {
                // A copy: a handler may replace the items, which frees the
                // item's own string while later handlers still read it.
                const std::string activated_id = items_[*index].stable_id;
                item_activated_.emit(activated_id);
            }
            event.handled = true;
        } else if (!index && !pressed_index_ &&
                   event.button == PointerButton::secondary &&
                   pressed_button_ == PointerButton::secondary && is_alive()) {
            clear_selection();
            context_requested_.emit({{}, event.position});
            event.handled = true;
        }
        if (event.button == pressed_button_ && pressed_button_ != PointerButton::none) {
            event.handled = true;
        }
        pressed_index_.reset();
        pressed_button_ = PointerButton::none;
    }
}

void ObjectView::on_key(KeyEvent& event) {
    if (has_details_columns() && event.modifiers == Modifier::alt) return;
    if (focused_ && enabled() && event.action == KeyAction::down &&
        has_details_columns() && details_key(event)) return;
    if (!focused_ || !enabled() || event.action != KeyAction::down || items_.empty()) return;
    std::optional<std::size_t> current = item_index(focused_id_);
    if (!current) current = item_index(selected_id_);
    std::size_t index = current.value_or(0U);
    const bool toggle_modifier = has_modifier(event.modifiers, Modifier::control) ||
                                 has_modifier(event.modifiers, Modifier::meta);
    if (event.physical_key == PhysicalKey::a && toggle_modifier &&
        selection_mode_ == ObjectSelectionMode::multiple) {
        select_all();
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::enter) {
        if (items_[index].enabled && is_alive()) {
            const std::string activated_id = items_[index].stable_id;
            item_activated_.emit(activated_id);
        }
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::space) {
        select_index(index, false, event.modifiers);
        event.handled = true;
        return;
    }
    const std::size_t column_count = columns();
    if (event.physical_key == PhysicalKey::left && index > 0U) --index;
    else if (event.physical_key == PhysicalKey::right && index + 1U < items_.size()) ++index;
    else if (event.physical_key == PhysicalKey::up) {
        index = index >= column_count ? index - column_count : 0U;
    } else if (event.physical_key == PhysicalKey::down) {
        index = std::min(index + column_count, items_.size() - 1U);
    } else if (event.physical_key == PhysicalKey::home) index = 0U;
    else if (event.physical_key == PhysicalKey::end) index = items_.size() - 1U;
    else if (event.physical_key == PhysicalKey::page_up) {
        const std::size_t amount = visible_row_count() * column_count;
        index = index > amount ? index - amount : 0U;
    } else if (event.physical_key == PhysicalKey::page_down) {
        index = std::min(index + visible_row_count() * column_count, items_.size() - 1U);
    } else return;
    if (toggle_modifier && !has_modifier(event.modifiers, Modifier::shift)) {
        focus_index(index);
    } else {
        select_index(index, false, event.modifiers);
    }
    event.handled = true;
}

void ObjectView::type_select(std::string_view text) {
    const std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now();
    if (now - last_type_time_ > type_timeout) type_prefix_.clear();
    last_type_time_ = now;
    type_prefix_ += fold_ascii(text);
    if (items_.empty()) return;
    const std::size_t start = (item_index(focused_id_).value_or(items_.size() - 1U) + 1U)
        % items_.size();
    for (std::size_t offset = 0; offset < items_.size(); ++offset) {
        const std::size_t index = (start + offset) % items_.size();
        if (fold_ascii(items_[index].name).starts_with(type_prefix_)) {
            select_index(index, false);
            return;
        }
    }
}

void ObjectView::on_text_input(TextInputEvent& event) {
    if (header_focused_ && has_details_columns()) return;
    if (!focused_ || !enabled() || event.composing || event.text_utf8.empty() ||
        !validate_utf8(event.text_utf8).valid()) return;
    type_select(event.text_utf8);
    event.handled = true;
}

void ObjectView::on_focus_changed(bool focused) {
    if (!focused) cancel_header_interaction();
    focused_ = focused;
    if (focused_ && focused_id_.empty() && !items_.empty()) {
        focused_id_ = selected_id_.empty() ? items_.front().stable_id : selected_id_;
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

SemanticDescriptor ObjectView::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    // The details view with columns is a table (12.13), sized by every item
    // and every column, realized or not; the other views are lists.
    if (has_details_columns()) {
        descriptor.role = SemanticRole::table;
        descriptor.row_count = items_.size();
        descriptor.column_count = details_columns_.size();
    } else {
        descriptor.role = SemanticRole::list;
    }
    if (selection_mode_ == ObjectSelectionMode::multiple) descriptor.states |= SemanticState::multiselectable;
    descriptor.name = accessible_name();
    descriptor.description = accessible_description();
    descriptor.value = selected_ids_.size() <= 1U
        ? selected_id_ : std::to_string(selected_ids_.size()) + " selected";
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> ObjectView::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    const std::size_t first = top_row_ * columns();
    const std::size_t end = std::min(items_.size(),
        (top_row_ + visible_row_count()) * columns());
    const bool table = has_details_columns();
    nodes.reserve(end - std::min(first, end) + (table ? details_columns_.size() : 0U));
    const Rect absolute = absolute_bounds();
    if (table) append_column_headers(nodes);
    for (std::size_t index = first; index < end; ++index) {
        const ObjectViewItem& item = items_[index];
        const Rect local = item_bounds(index);
        SemanticNode node{};
        node.stable_id = item.stable_id;
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        // Only the visible items are described; their positions still count
        // the whole collection.
        if (table) {
            node.role = SemanticRole::row;
            node.row_index = index + 1U;
            append_row_cells(index, node);
        } else {
            node.role = SemanticRole::list_item;
            node.position_in_set = index + 1U;
            node.set_size = items_.size();
        }
        node.name = item.name;
        node.value = item.secondary_text;
        node.description = item.description;
        node.bounds = {absolute.x + local.x, absolute.y + local.y,
                       local.width, local.height};
        node.states = SemanticState::visible | SemanticState::focusable;
        if (effectively_enabled() && item.enabled) node.states |= SemanticState::enabled;
        if (is_selected(item.stable_id)) node.states |= SemanticState::selected;
        if (focused_ && !header_focused_ && item.stable_id == focused_id_) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::focus, SemanticAction::select,
                        SemanticAction::press, SemanticAction::show_menu};
        nodes.push_back(std::move(node));
    }
    return nodes;
}

// The details view's column headers whose extent meets the header band (the
// realized columns), in column order. A sortable column's header offers
// press, which sorts as clicking it does (owner decision, stage A gap 2);
// the others do nothing when clicked and offer no action.
void ObjectView::append_column_headers(std::vector<SemanticNode>& nodes) const {
    const Rect absolute = absolute_bounds();
    const Rect band{4.0, 2.0, std::max(0.0, local_bounds().width - 8.0), header_height()};
    const std::string view_id(stable_id().value());
    for (std::size_t column = 0U; column < details_columns_.size(); ++column) {
        const Rect bounds = Rect::intersection(column_bounds(column), band);
        if (bounds.empty()) continue;
        const ObjectDetailsColumn& definition = details_columns_[column];
        SemanticNode node{};
        node.stable_id = view_id + ".column." + definition.id.value;
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::column_header;
        node.column_index = column + 1U;
        node.name = definition.label;
        if (!details_sort_.column.value.empty() && definition.id == details_sort_.column) {
            node.value = details_sort_.direction == ObjectSortDirection::ascending ? "ascending" : "descending";
        }
        node.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
        node.states = SemanticState::visible;
        if (effectively_enabled()) node.states |= SemanticState::enabled;
        if (definition.sortable) node.actions = {SemanticAction::press};
        nodes.push_back(std::move(node));
    }
}

// A details row's cells, one per column whose extent meets the body (the
// realized columns). A cell has no actions of its own; its row takes them.
void ObjectView::append_row_cells(std::size_t index, SemanticNode& row_node) const {
    const ObjectViewItem& item = items_[index];
    const Rect absolute = absolute_bounds();
    const Rect line = item_bounds(index);
    const double header = header_height();
    const Rect body{4.0, 2.0 + header, std::max(0.0, local_bounds().width - 8.0),
                    std::max(0.0, local_bounds().height - header - 4.0)};
    const bool enabled = effectively_enabled() && item.enabled;
    for (std::size_t column = 0U; column < details_columns_.size(); ++column) {
        const Rect extent = column_bounds(column);
        const Rect cell_rect{extent.x, line.y, extent.width, line.height};
        const Rect bounds = Rect::intersection(cell_rect, body);
        if (bounds.empty()) continue;
        const ObjectDetailsCell& value = item.cells[column];
        SemanticNode cell{};
        cell.stable_id = item.stable_id + ".cell." + details_columns_[column].id.value;
        cell.runtime_id = virtual_semantic_runtime_id(cell.stable_id);
        cell.role = SemanticRole::cell;
        cell.name = details_columns_[column].label;
        cell.value = value.text;
        cell.row_index = index + 1U;
        cell.column_index = column + 1U;
        cell.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
        cell.states = SemanticState::visible;
        if (enabled && value.availability == ObjectCellAvailability::available) cell.states |= SemanticState::enabled;
        row_node.children.push_back(std::move(cell));
    }
}

bool ObjectView::on_semantic_child_action(std::string_view id,
                                          SemanticAction action,
                                          std::string_view) {
    const std::optional<std::size_t> index = item_index(id);
    if (!index) {
        const bool pressed = press_column_header(id, action);
        return pressed;
    }
    if ((action != SemanticAction::focus &&
                   action != SemanticAction::select &&
                   action != SemanticAction::press &&
                   action != SemanticAction::show_menu)) return false;
    // An item-addressed action enters the body even when its selection is
    // unchanged. Header focus otherwise consumes the consumer's next key.
    if (header_focused_) {
        header_focused_ = false;
        invalidate(Dirty::paint | Dirty::semantics);
    }
    if (window()) static_cast<void>((*window()).request_focus(shared_from_this()));
    if (action == SemanticAction::show_menu) {
        if (!is_selected(id)) select_index(*index, false);
        else focus_index(*index);
        const Rect local = item_bounds(*index);
        const Rect absolute = absolute_bounds();
        context_requested_.emit({std::string(id),
            {absolute.x + local.x + local.width * .5,
             absolute.y + local.y + local.height * .5}});
    } else if (action == SemanticAction::focus) {
        focus_index(*index);
    } else {
        select_index(*index, action == SemanticAction::press);
    }
    return true;
}


// A press sent to a sortable column header's node: the header takes focus,
// as a click gives it, and requests the sort the click would. False for any
// other node or action, a column that does not sort, or a disabled view.
bool ObjectView::press_column_header(std::string_view id, SemanticAction action) {
    if (action != SemanticAction::press || !has_details_columns() || !effectively_enabled()) return false;
    const std::string prefix = std::string(stable_id().value()) + ".column.";
    if (id.compare(0U, prefix.size(), prefix) != 0) return false;
    const ObjectColumnId column_id{std::string(id.substr(prefix.size()))};
    const std::optional<std::size_t> column = column_index(column_id);
    if (!column || !details_columns_[*column].sortable) return false;
    const Control::Ptr lifetime = shared_from_this();
    if (window() != nullptr) {
        const bool accepted = (*window()).request_focus(lifetime);
        if (!accepted) return false;
    }
    if (!is_alive() || !has_details_columns() || *column >= details_columns_.size()) return false;
    header_focused_ = true;
    focused_column_ = *column;
    reveal_header();
    request_header_sort(*column);
    return true;
}

bool ObjectView::has_details_columns() const noexcept {
    const bool result = view_mode_ == ObjectViewMode::details && !details_columns_.empty();
    return result;
}

double ObjectView::header_height() const noexcept {
    if (!has_details_columns()) return 0.0;
    const double result = details_row_height_ * effective_text_scale();
    return result;
}

double ObjectView::total_column_width() const noexcept {
    double result = 0.0;
    for (const ObjectDetailsColumn& column : details_columns_) result += column.width;
    return result;
}

std::optional<std::size_t> ObjectView::column_index(const ObjectColumnId& id) const noexcept {
    for (std::size_t index = 0U; index < details_columns_.size(); ++index) {
        if (details_columns_[index].id == id) return index;
    }
    return {};
}

Rect ObjectView::column_bounds(const std::size_t column) const noexcept {
    const double scale = effective_text_scale();
    double x = 4.0 - horizontal_offset_ * scale;
    for (std::size_t index = 0U; index < column; ++index) x += details_columns_[index].width * scale;
    const Rect result{x, 2.0, details_columns_[column].width * scale, header_height()};
    return result;
}

void ObjectView::clamp_horizontal_offset() noexcept {
    const double available = std::max(0.0, local_bounds().width - 8.0) / effective_text_scale();
    const double maximum = std::max(0.0, total_column_width() - available);
    horizontal_offset_ = std::clamp(horizontal_offset_, 0.0, maximum);
}

void ObjectView::set_horizontal_offset(const double offset) {
    require_mutable();
    if (!std::isfinite(offset) || offset < 0.0) throw std::invalid_argument("ObjectView horizontal offset is invalid");
    horizontal_offset_ = offset;
    clamp_horizontal_offset();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ObjectView::clear_details_cache() noexcept {
    // Conservatively retire input snapshots on model, column, or layout changes.
    ++details_revision_;
    for (DetailsTextCache& entry : details_cache_) entry.valid = false;
}

void ObjectView::set_details_column_width(const ObjectColumnId& id, const double width) {
    require_mutable();
    const std::optional<std::size_t> index = column_index(id);
    if (!index) throw std::invalid_argument("ObjectView width names an unknown column");
    ObjectDetailsColumn& column = details_columns_[*index];
    if (!std::isfinite(width) || width < column.minimum_width || width > column.maximum_width) {
        throw std::invalid_argument("ObjectView width is outside column bounds");
    }
    if (column.width == width) return;
    column.width = width;
    clamp_horizontal_offset();
    clear_details_cache();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ObjectView::set_details_sort(ObjectDetailsSort state) {
    require_mutable();
    const std::optional<std::size_t> index = column_index(state.column);
    if ((!state.column.value.empty() && (!index || !details_columns_[*index].sortable)) ||
        (state.direction != ObjectSortDirection::ascending && state.direction != ObjectSortDirection::descending)) {
        throw std::invalid_argument("ObjectView accepted sort state is invalid");
    }
    details_sort_ = std::move(state);
    clear_details_cache();
    invalidate(Dirty::paint | Dirty::semantics);
}

void ObjectView::request_header_sort(const std::size_t column) {
    const Control::Ptr lifetime = shared_from_this();
    // Geometry lookup may flush pending layout and invalidate the text cache.
    // Resolve it before snapshotting the revision or reading accepted sort.
    const Rect bounds = absolute_bounds();
    if (!is_alive() || !has_details_columns() || !header_focused_ || focused_column_ != column) return;
    if (column >= details_columns_.size() || !details_columns_[column].sortable) return;
    ObjectDetailsSort request{};
    request.column = details_columns_[column].id;
    if (request.column == details_sort_.column && details_sort_.direction == ObjectSortDirection::ascending) {
        request.direction = ObjectSortDirection::descending;
    }
    const DetailsPointerContext context{window(), details_revision_, bounds,
        effective_text_scale(), horizontal_offset_, header_height()};
    // Retire the old gesture before either release or sort observers run.
    // The request owns its identity across release; changed context cancels it.
    cancel_header_interaction();
    if (!details_pointer_context_valid(context) || !header_focused_ ||
        focused_column_ != column || pressed_column_ || resizing_column_ || has_pointer_capture()) return;
    sort_requested_.emit(request);
}

void ObjectView::cancel_header_interaction() {
    pressed_column_.reset();
    const bool release = resizing_column_ && has_pointer_capture();
    resizing_column_.reset();
    if (release && window()) (*window()).release_pointer();
}

void ObjectView::reveal_header() {
    if (focused_column_ >= details_columns_.size()) return;
    const Rect column = column_bounds(focused_column_);
    const double scale = effective_text_scale();
    const double right = local_bounds().width - 4.0;
    if (column.x < 4.0) horizontal_offset_ += (column.x - 4.0) / scale;
    else if (column.x + column.width > right) horizontal_offset_ += (column.x + column.width - right) / scale;
    clamp_horizontal_offset();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

bool ObjectView::details_pointer_context_valid(const DetailsPointerContext& context) const {
    if (!is_alive()) return false;
    const Rect bounds = absolute_bounds();
    if (!is_alive() || window() != context.attached || !has_details_columns() ||
        !effectively_enabled() || !effectively_visible() || details_revision_ != context.revision) return false;
    if (context.attached && (*context.attached).focused_control().get() != this) return false;
    const bool valid = bounds.x == context.bounds.x && bounds.y == context.bounds.y &&
        bounds.width == context.bounds.width && bounds.height == context.bounds.height &&
        effective_text_scale() == context.scale && horizontal_offset_ == context.offset &&
        header_height() == context.header;
    return valid;
}

bool ObjectView::details_pointer(PointerEvent& event) {
    const Control::Ptr lifetime = shared_from_this();
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    if (resizing_column_ && window() && !has_pointer_capture()) {
        // The host or another control may revoke capture without a final up.
        // A later pointer move must not resume that retired width gesture.
        resizing_column_.reset();
        pressed_column_.reset();
    }
    if (resizing_column_) {
        if (event.action == PointerAction::move) {
            const ObjectDetailsColumn& column = details_columns_[*resizing_column_];
            const double delta = (local.x - resize_start_x_) / effective_text_scale();
            const double width = std::clamp(resize_start_width_ + delta, column.minimum_width, column.maximum_width);
            set_details_column_width(column.id, width);
        } else if (event.action == PointerAction::up) {
            cancel_header_interaction();
        }
        event.handled = true;
        return true;
    }
    if (event.action == PointerAction::wheel &&
        (event.wheel_delta.x != 0.0 || has_modifier(event.modifiers, Modifier::shift))) {
        const double delta = event.wheel_delta.x != 0.0 ? event.wheel_delta.x : event.wheel_delta.y;
        const double next = std::max(0.0, horizontal_offset_ - delta * 32.0);
        set_horizontal_offset(next);
        event.handled = true;
        return true;
    }
    const bool in_header = local.y >= 2.0 && local.y < 2.0 + header_height() &&
        local.x >= 4.0 && local.x < absolute.width - 4.0;
    if (!in_header) {
        if (event.action == PointerAction::up && pressed_column_) {
            pressed_column_.reset();
            event.handled = true;
            return true;
        }
        if (event.action == PointerAction::down) {
            header_focused_ = false;
            pressed_column_.reset();
        }
        return false;
    }
    hovered_index_.reset();
    std::optional<std::size_t> hit{};
    bool edge = false;
    for (std::size_t index = 0U; index < details_columns_.size(); ++index) {
        const Rect bounds = column_bounds(index);
        if (std::abs(local.x - bounds.x - bounds.width) <= 4.0) {
            hit = index;
            edge = true;
            break;
        }
        if (bounds.contains(local)) hit = index;
    }
    if (event.action == PointerAction::down && event.button == PointerButton::primary) {
        const DetailsPointerContext context{window(), details_revision_, absolute,
            effective_text_scale(), horizontal_offset_, header_height()};
        event.handled = true;
        if (context.attached) {
            const bool accepted = (*context.attached).request_focus(lifetime);
            if (!accepted) return true;
        }
        if (!details_pointer_context_valid(context)) return true;
        pressed_index_.reset();
        pressed_button_ = PointerButton::none;
        header_focused_ = true;
        if (hit) {
            focused_column_ = *hit;
            if (edge) {
                resizing_column_ = hit;
                resize_start_x_ = local.x;
                resize_start_width_ = details_columns_[*hit].width;
                if (context.attached) {
                    (*context.attached).capture_pointer(lifetime, event.pointer_id);
                    if (!details_pointer_context_valid(context) || !has_pointer_capture() ||
                        resizing_column_ != hit) {
                        // Capture publication may replace/dispose this control or
                        // transfer capture again. Release only capture we still own.
                        const bool release = has_pointer_capture();
                        resizing_column_.reset();
                        pressed_column_.reset();
                        if (release && window()) (*window()).release_pointer();
                        return true;
                    }
                }
            } else pressed_column_ = hit;
        }
        invalidate(Dirty::paint | Dirty::semantics);
    } else if (event.action == PointerAction::up && event.button == PointerButton::primary) {
        const bool activate = hit && pressed_column_ == hit;
        pressed_column_.reset();
        event.handled = true;
        if (activate) request_header_sort(*hit);
        return true;
    }
    event.handled = true;
    return true;
}

bool ObjectView::details_key(KeyEvent& event) {
    const bool adjust_column = event.modifiers == (Modifier::alt | Modifier::shift);
    if (event.physical_key == PhysicalKey::escape && resizing_column_) {
        const ObjectDetailsColumn& column = details_columns_[*resizing_column_];
        set_details_column_width(column.id, resize_start_width_);
        cancel_header_interaction();
        event.handled = true;
        return true;
    }
    if (event.physical_key == PhysicalKey::f6) {
        header_focused_ = !header_focused_;
        if (header_focused_) reveal_header();
        invalidate(Dirty::paint | Dirty::semantics);
        event.handled = true;
        return true;
    }
    const bool left = event.physical_key == PhysicalKey::left;
    const bool right = event.physical_key == PhysicalKey::right;
    if (!header_focused_) {
        if (adjust_column && (left || right)) {
            const double next = std::max(0.0, horizontal_offset_ + (left ? -32.0 : 32.0));
            set_horizontal_offset(next);
            event.handled = true;
            return true;
        }
        return false;
    }
    if (event.physical_key == PhysicalKey::enter || event.physical_key == PhysicalKey::space) {
        event.handled = true;
        request_header_sort(focused_column_);
        return true;
    }
    if (adjust_column && (left || right)) {
        const ObjectDetailsColumn& column = details_columns_[focused_column_];
        const double width = std::clamp(column.width + (left ? -8.0 : 8.0), column.minimum_width, column.maximum_width);
        set_details_column_width(column.id, width);
    } else if (left && focused_column_ > 0U) --focused_column_;
    else if (right && focused_column_ + 1U < details_columns_.size()) ++focused_column_;
    else if (event.physical_key == PhysicalKey::home) focused_column_ = 0U;
    else if (event.physical_key == PhysicalKey::end) focused_column_ = details_columns_.size() - 1U;
    else if (event.physical_key == PhysicalKey::escape || event.physical_key == PhysicalKey::down) header_focused_ = false;
    else if (event.physical_key == PhysicalKey::tab) {
        header_focused_ = false;
        invalidate(Dirty::paint | Dirty::semantics);
        return false;
    }
    reveal_header();
    event.handled = true;
    return true;
}

const std::string& ObjectView::details_text(Painter& painter,
    const std::size_t slot, const std::size_t item, const std::size_t column, const bool header,
    const std::string_view text, const double width, const FontSpec font) {
    DetailsTextCache& cached = details_cache_[slot];
    if (cached.valid && cached.item == item && cached.column == column &&
        cached.header == header && cached.width == width) return cached.text;
    cached.valid = false;
    cached.item = item;
    cached.column = column;
    cached.header = header;
    cached.width = width;
    ++details_paint_work_.text_preparations;
    details_detail::elide_to_width(painter, text, font, width, cached.text);
    const Size measured = painter.measure_text_utf8(cached.text, font);
    cached.measured = measured;
    cached.valid = true;
    return cached.text;
}

void ObjectView::paint_details(Painter& painter) {
    details_paint_work_ = {};
    clamp_horizontal_offset();
    const FontSpec font = effective_font(font_);
    if (details_cache_font_ != font) {
        clear_details_cache();
        details_cache_font_ = font;
    }
    const Rect bounds = local_bounds();
    const double header = header_height();
    const double viewport_right = std::max(4.0, bounds.width - 4.0);
    const std::size_t first = std::min(top_row_, items_.size());
    const std::size_t end = std::min(items_.size(), first + visible_row_count() + 1U);
    std::array<Rect, 64U> geometry{};
    std::array<std::size_t, 64U> visible_columns{};
    std::size_t visible_count = 0U;
    double x = 4.0 - horizontal_offset_ * effective_text_scale();
    for (std::size_t index = 0U; index < details_columns_.size(); ++index) {
        const double width = details_columns_[index].width * effective_text_scale();
        geometry[index] = {x, 2.0, width, header};
        if (x < viewport_right && x + width > 4.0) {
            visible_columns[visible_count] = index;
            ++visible_count;
        }
        x += width;
    }
    const std::size_t required = (end - first + 1U) * visible_count;
    if (details_cache_.size() < required) details_cache_.resize(required);
    std::size_t cache_slot = 0U;
    painter.save();
    painter.clip_rect({4.0, 2.0, std::max(0.0, bounds.width - 8.0), std::max(0.0, bounds.height - 4.0)});
    // The header band, column labels, separators and sort arrows, the row
    // plates and the cells' text are parts the theme's engine draws. A row is
    // hot under the pointer, selected when selected and disabled with its
    // item; an unavailable cell's text is unavailable. A style override
    // supplies the plates' grounds (T4-1).
    AnatomyStateSet states = anatomy_states();
    states.erase(AnatomyState::hot);
    const bool overridden = has_style_override();
    PartPaint band;
    band.family = ControlVisualRole::panel;
    band.role = PartRole::face;
    band.bounds = {4.0, 2.0, std::max(0.0, viewport_right - 4.0), header};
    band.states = states;
    paint_part(painter, *this, band);
    for (std::size_t visible = 0U; visible < visible_count; ++visible) {
        const std::size_t index = visible_columns[visible];
        const ObjectDetailsColumn& column = details_columns_[index];
        const Rect cell = geometry[index];
        const bool sorted = column.id == details_sort_.column;
        const double reserved = sorted ? 22.0 : 12.0;
        const std::string& label = details_text(painter, cache_slot, 0U, index, true,
            column.label, std::max(0.0, cell.width - reserved), font);
        const Size label_measured = details_cache_[cache_slot].measured;
        ++cache_slot;
        painter.save();
        painter.clip_rect(cell);
        PartPaint caption;
        caption.family = ControlVisualRole::panel;
        caption.role = PartRole::label;
        caption.states = states;
        caption.text = label;
        caption.font = font;
        caption.measured_text = label_measured;
        paint_text_part_at(painter, *this, caption,
                           {cell.x + 6.0, cell.y + cell.height * .5 + font.size * .35},
                           std::max(0.0, cell.width - reserved));
        PartPaint separator;
        separator.family = ControlVisualRole::panel;
        separator.role = PartRole::divider;
        separator.bounds = {cell.x + cell.width - 1.5, cell.y + 3.0, 1.0,
                            std::max(0.0, cell.height - 6.0)};
        separator.states = states;
        paint_part(painter, *this, separator);
        if (sorted) {
            // An 8-unit square holds the arrow's four rows.
            const double center = std::floor(cell.x + cell.width - 10.0);
            const double y = std::floor(cell.y + cell.height * .5);
            PartPaint arrow;
            arrow.family = ControlVisualRole::panel;
            arrow.role = PartRole::glyph;
            arrow.mark = details_sort_.direction == ObjectSortDirection::ascending ? PartMark::arrow_up
                                                                                  : PartMark::arrow_down;
            arrow.bounds = {center - 4.0, y - 4.0, 8.0, 8.0};
            arrow.states = states;
            paint_part(painter, *this, arrow);
        }
        if (focused_ && header_focused_ && focused_column_ == index) paint_focus(painter, cell, style().text);
        painter.restore();
    }
    PartPaint header_rule;
    header_rule.family = ControlVisualRole::panel;
    header_rule.role = PartRole::divider;
    header_rule.bounds = {4.0, 1.5 + header, std::max(0.0, viewport_right - 4.0), 1.0};
    header_rule.states = states;
    paint_part(painter, *this, header_rule);
    painter.clip_rect({4.0, 2.0 + header, viewport_right - 4.0, std::max(0.0, bounds.height - header - 4.0)});
    const Color selected_row_fill = focused_ ? style().accent_light : with_alpha(style().accent_light, 190);
    for (std::size_t item_index_value = first; item_index_value < end; ++item_index_value) {
        const ObjectViewItem& item = items_[item_index_value];
        const Rect row = item_bounds(item_index_value);
        if (row.y >= bounds.height - 2.0) break;
        ++details_paint_work_.rows;
        const bool selected = is_selected(item.stable_id);
        const bool hovered = hovered_index_ == item_index_value;
        AnatomyStateSet row_states = states;
        if (hovered) row_states.insert(AnatomyState::hot);
        if (selected) row_states.insert(AnatomyState::selected);
        if (!item.enabled) row_states.insert(AnatomyState::disabled);
        if (selected || hovered) {
            PartPaint plate;
            plate.family = ControlVisualRole::selection;
            plate.role = PartRole::selection;
            plate.bounds = row;
            plate.states = row_states;
            if (overridden) plate.authored_background = selected ? selected_row_fill : style().face_light;
            paint_part(painter, *this, plate);
        }
        for (std::size_t visible = 0U; visible < visible_count; ++visible) {
            const std::size_t index = visible_columns[visible];
            const ObjectDetailsColumn& column = details_columns_[index];
            const ObjectDetailsCell& value = item.cells[index];
            const Rect cell{geometry[index].x, row.y, geometry[index].width, row.height};
            const double left_padding = index == 0U ? 34.0 : 6.0;
            const double text_width = std::max(0.0, cell.width - left_padding - 6.0);
            const std::size_t text_slot = cache_slot;
            ++cache_slot;
            const std::string& text = details_text(painter, text_slot, item_index_value, index, false, value.text, text_width, font);
            ++details_paint_work_.cells;
            painter.save();
            painter.clip_rect(cell);
            if (index == 0U) {
                const Rect icon{cell.x + 4.0, cell.y + 3.0, 24.0, std::max(18.0, cell.height - 6.0)};
                if (!paint_item_image(painter, item, item_index_value, selected, icon)) paint_glyph(painter, icon, item.glyph, item.enabled);
            }
            double origin = cell.x + left_padding;
            if (column.alignment == ObjectColumnAlignment::right) {
                origin = std::max(origin, cell.x + cell.width - 6.0 - details_cache_[text_slot].measured.width);
            }
            PartPaint content;
            content.family = selected || hovered ? ControlVisualRole::selection : ControlVisualRole::editor;
            content.role = PartRole::content;
            content.states = row_states;
            if (value.availability != ObjectCellAvailability::available) {
                content.states.insert(AnatomyState::unavailable);
            }
            content.text = text;
            content.font = font;
            content.measured_text = details_cache_[text_slot].measured;
            paint_text_part_at(painter, *this, content,
                               {origin, cell.y + cell.height * .5 + font.size * .35},
                               std::max(0.0, cell.x + cell.width - 6.0 - origin));
            painter.restore();
        }
        if (focused_ && !header_focused_ && item.stable_id == focused_id_ && window() && (*window()).focus_cue_visible()) {
            paint_focus(painter, row, style().text);
        }
    }
    painter.restore();
}


namespace {

constexpr std::array<PartField<ObjectView::Parts>, 3> object_view_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &ObjectView::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &ObjectView::Parts::edge},
    {.declaration = {.name = "header", .role = PartRole::face, .composed_from = {}},
     .member = &ObjectView::Parts::header},
}};

constexpr std::array<PartField<ObjectView::ItemParts>, 5> object_view_item_part_fields{{
    {.declaration = {.name = "cell", .role = PartRole::region, .composed_from = {}},
     .member = &ObjectView::ItemParts::cell},
    {.declaration = {.name = "glyph", .role = PartRole::icon, .composed_from = {}},
     .member = &ObjectView::ItemParts::glyph},
    {.declaration = {.name = "name", .role = PartRole::label, .composed_from = {}},
     .member = &ObjectView::ItemParts::name},
    {.declaration = {.name = "secondary", .role = PartRole::content, .composed_from = {}},
     .member = &ObjectView::ItemParts::secondary},
    {.declaration = {.name = "cell_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &ObjectView::ItemParts::cell_focus},
}};

constexpr std::array<StateField<ObjectView::ItemStates>, 4> object_view_item_state_fields{{
    {.state = AnatomyState::selected, .member = &ObjectView::ItemStates::selected},
    {.state = AnatomyState::hot, .member = &ObjectView::ItemStates::hot},
    {.state = AnatomyState::current, .member = &ObjectView::ItemStates::current},
    {.state = AnatomyState::disabled, .member = &ObjectView::ItemStates::disabled},
}};

constexpr std::array<PartDeclaration, 5> object_view_item_parts =
    part_declarations(object_view_item_part_fields);
constexpr std::array<AnatomyState, 4> object_view_item_states =
    state_declarations(object_view_item_state_fields);

constexpr std::array<PartField<ObjectView::ColumnParts>, 6> object_view_column_part_fields{{
    {.declaration = {.name = "column_header", .role = PartRole::region, .composed_from = {}},
     .member = &ObjectView::ColumnParts::column_header},
    {.declaration = {.name = "column_label", .role = PartRole::label, .composed_from = {}},
     .member = &ObjectView::ColumnParts::column_label},
    {.declaration = {.name = "sort_glyph", .role = PartRole::glyph, .composed_from = {}},
     .member = &ObjectView::ColumnParts::sort_glyph},
    {.declaration = {.name = "column_divider", .role = PartRole::divider, .composed_from = {}},
     .member = &ObjectView::ColumnParts::column_divider},
    {.declaration = {.name = "column_grip", .role = PartRole::grip, .composed_from = {}},
     .member = &ObjectView::ColumnParts::column_grip},
    {.declaration = {.name = "column_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &ObjectView::ColumnParts::column_focus},
}};

constexpr std::array<StateField<ObjectView::ColumnStates>, 3> object_view_column_state_fields{{
    {.state = AnatomyState::pressed, .member = &ObjectView::ColumnStates::pressed},
    {.state = AnatomyState::captured, .member = &ObjectView::ColumnStates::captured},
    {.state = AnatomyState::current, .member = &ObjectView::ColumnStates::current},
}};

constexpr std::array<CustomStateDeclaration, 2> object_view_column_custom_states{{
    {.name = "sorted_ascending"},
    {.name = "sorted_descending"},
}};
constexpr CustomStateIndex sorted_ascending_state{0U};
constexpr CustomStateIndex sorted_descending_state{1U};

constexpr std::array<PartDeclaration, 6> object_view_column_parts =
    part_declarations(object_view_column_part_fields);
constexpr std::array<AnatomyState, 3> object_view_column_states =
    state_declarations(object_view_column_state_fields);
constexpr ItemSystemIndex object_view_column_system{1U};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 3>
    object_view_parts = joined_declarations(scrollable_detail::parts,
                                         part_declarations(object_view_extra_part_fields));

constexpr std::array<ItemSystemDeclaration, 2> object_view_item_systems{{
    {.name = "item", .parts = object_view_item_parts, .states = object_view_item_states,
     .custom_states = {}},
    {.name = "column", .parts = object_view_column_parts, .states = object_view_column_states,
     .custom_states = object_view_column_custom_states},
}};

constexpr ControlAnatomy object_view_anatomy{
    .control_id = "object_view",
    .parts = object_view_parts,
    .states = scrollable_detail::states,
    .custom_states = {},
    .item_systems = object_view_item_systems,
};

} // namespace

const ControlAnatomy& ObjectView::anatomy() const noexcept {
    return object_view_anatomy;
}

void ObjectView::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.scrollable = scrollable_parts();

    const Panel::Parts panel = panel_parts();
    record.face = panel.face;
    record.edge = panel.edge;
    // The column header band of the details view; each visible column is
    // also published as an item of the column system.
    if (has_details_columns()) {
        const Rect bounds = local_bounds();
        const double viewport_right = std::max(4.0, bounds.width - 4.0);
        record.header = PartGeometry::from_bounds(
            {4.0, 2.0, std::max(0.0, viewport_right - 4.0), header_height()});
    }
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), object_view_extra_part_fields, record, geometry);
}

ObjectView::Parts ObjectView::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), object_view_extra_part_fields, published.parts(), record);
    return record;
}

std::size_t ObjectView::realized_row_count() const noexcept {
    const std::size_t first = top_row_ * columns();
    if (first >= items_.size()) return 0U;
    const std::size_t end = std::min(items_.size(),
        (top_row_ + visible_row_count() + 1U) * columns());
    return end - first;
}

std::size_t ObjectView::realized_column_count() const noexcept {
    if (!has_details_columns()) return 0U;
    const double viewport_right = std::max(4.0, local_bounds().width - 4.0);
    std::size_t count = 0U;
    for (std::size_t index = 0U; index < details_columns_.size(); ++index) {
        const Rect cell = column_bounds(index);
        if (cell.x < viewport_right && cell.x + cell.width > 4.0) ++count;
    }
    return count;
}

std::size_t ObjectView::realized_column_index(std::size_t position) const noexcept {
    const double viewport_right = std::max(4.0, local_bounds().width - 4.0);
    std::size_t seen = 0U;
    for (std::size_t index = 0U; index < details_columns_.size(); ++index) {
        const Rect cell = column_bounds(index);
        if (cell.x >= viewport_right || cell.x + cell.width <= 4.0) continue;
        if (seen == position) return index;
        ++seen;
    }
    return details_columns_.size();
}

std::size_t ObjectView::realized_item_count() const {
    return realized_row_count() + realized_column_count();
}

std::string ObjectView::realized_item_stable_id(std::size_t item) const {
    const std::size_t rows = realized_row_count();
    if (item < rows) return items_[top_row_ * columns() + item].stable_id;
    const std::size_t column = realized_column_index(item - rows);
    return std::string(stable_id().value()) + ".column." + details_columns_[column].id.value;
}

ItemSystemIndex ObjectView::publish_realized_item_system(std::size_t item) const {
    if (item < realized_row_count()) {
        const ItemSystemIndex rows;
        return rows;
    }
    return object_view_column_system;
}

// The header cell as paint_details lays it out: the label inset by six
// units and short of the sort glyph, the divider stroked one unit inside the
// right edge, and the resize grip four units either side of that edge.
ObjectView::ColumnParts ObjectView::column_parts(std::size_t column) const {
    const Rect bounds = local_bounds();
    const double viewport_right = std::max(4.0, bounds.width - 4.0);
    const Rect clip{4.0, 2.0, std::max(0.0, viewport_right - 4.0), header_height()};
    const bool sorted = details_columns_[column].id == details_sort_.column;
    const details_detail::ColumnHeaderGeometry header =
        details_detail::column_header_geometry(column_bounds(column), clip, sorted, 1.0);
    ColumnParts record;
    record.column_header = PartGeometry::from_bounds(header.header);
    record.column_label = PartGeometry::from_bounds(header.label);
    record.sort_glyph = PartGeometry::from_bounds(header.sort_glyph);
    record.column_divider = PartGeometry::from_bounds(header.divider);
    record.column_grip = PartGeometry::from_bounds(header.grip);
    record.column_focus = record.column_header;
    return record;
}

AnatomyStateSet ObjectView::column_states(std::size_t column) const {
    AnatomyStateSet states;
    if (pressed_column_ == column) states.insert(AnatomyState::pressed);
    if (resizing_column_ == column) states.insert(AnatomyState::captured);
    if (header_focused_ && focused_column_ == column) states.insert(AnatomyState::current);
    if (details_columns_[column].id == details_sort_.column) {
        if (details_sort_.direction == ObjectSortDirection::ascending) {
            states.insert(sorted_ascending_state);
        } else {
            states.insert(sorted_descending_state);
        }
    }
    return states;
}

void ObjectView::publish_realized_item_geometry(std::size_t item,
                                          std::span<PartGeometry> geometry) const {
    const std::size_t rows = realized_row_count();
    if (item >= rows) {
        const ColumnParts column = column_parts(realized_column_index(item - rows));
        write_item_parts(anatomy().item_systems[object_view_column_system.value],
                         object_view_column_part_fields, column, geometry);
        return;
    }
    ItemParts record;

    // Name and secondary text are published as the slots on_paint lays them
    // in; the painted text is elided to fit.
    const std::size_t index = top_row_ * columns() + item;
    const ObjectViewItem& entry = items_[index];
    const Rect bounds = local_bounds();
    const Rect cell = item_bounds(index);
    Rect clip{2.0, 2.0, std::max(0.0, bounds.width - 4.0), std::max(0.0, bounds.height - 4.0)};
    Rect glyph{};
    Rect name{};
    Rect secondary{};
    if (has_details_columns()) {
        const double viewport_right = std::max(4.0, bounds.width - 4.0);
        clip = {4.0, 2.0 + header_height(), std::max(0.0, viewport_right - 4.0),
                std::max(0.0, bounds.height - header_height() - 4.0)};
        const double column_x = 4.0 - horizontal_offset_ * effective_text_scale();
        const double column_width = details_columns_.front().width * effective_text_scale();
        glyph = {column_x + 4.0, cell.y + 3.0, 24.0, std::max(18.0, cell.height - 6.0)};
        name = {column_x + 34.0, cell.y, std::max(0.0, column_width - 40.0), cell.height};
    } else if (view_mode_ == ObjectViewMode::icons) {
        const double glyph_width = std::min(46.0, cell.width - 18.0);
        glyph = {cell.x + (cell.width - glyph_width) * 0.5, cell.y + 4.0, glyph_width, 43.0};
        name = {cell.x + 4.0, cell.y + 47.0, std::max(0.0, cell.width - 8.0),
                std::max(0.0, cell.height - 49.0)};
        if (show_secondary_text_ && !entry.secondary_text.empty() && cell.height >= 90.0) {
            const double size = std::max(8.0, font_.size - 1.5);
            secondary = {cell.x + 5.0, cell.bottom() - 5.0 - size,
                         std::max(0.0, cell.width - 10.0), size};
        }
    } else {
        glyph = {cell.x + 6.0, cell.y + 3.0, 24.0, std::max(18.0, cell.height - 6.0)};
        const bool shows_secondary = show_secondary_text_ &&
            !entry.secondary_text.empty() && cell.width >= 112.0;
        const double secondary_x = shows_secondary
            ? std::max(cell.x + 92.0, cell.x + cell.width * 0.68)
            : cell.x + cell.width - 6.0;
        const double name_width = std::max(
            0.0, secondary_x - (cell.x + 38.0) - (shows_secondary ? 8.0 : 0.0));
        name = {cell.x + 38.0, cell.y, name_width, cell.height};
        if (shows_secondary) {
            secondary = {secondary_x, cell.y, std::max(0.0, cell.right() - 6.0 - secondary_x),
                         cell.height};
        }
    }
    const Rect visible_cell = Rect::intersection(cell, clip);
    record.cell = PartGeometry::from_bounds(visible_cell);
    record.glyph = PartGeometry::from_bounds(Rect::intersection(glyph, visible_cell));
    record.name = PartGeometry::from_bounds(Rect::intersection(name, visible_cell));
    record.secondary = PartGeometry::from_bounds(Rect::intersection(secondary, visible_cell));
    record.cell_focus = record.cell;
    write_item_parts(anatomy().item_systems.front(), object_view_item_part_fields, record, geometry);
}

AnatomyStateSet ObjectView::realized_item_anatomy_states(std::size_t item) const {
    const std::size_t rows = realized_row_count();
    if (item >= rows) return column_states(realized_column_index(item - rows));
    AnatomyStateSet states;
    const std::size_t index = top_row_ * columns() + item;
    const ObjectViewItem& entry = items_[index];
    if (is_selected(entry.stable_id)) states.insert(AnatomyState::selected);
    if (hovered_index_ == index) states.insert(AnatomyState::hot);
    if (entry.stable_id == focused_id_) states.insert(AnatomyState::current);
    if (!entry.enabled) states.insert(AnatomyState::disabled);
    return states;
}

std::vector<ObjectView::Item> ObjectView::realized_items() const {
    const std::size_t count = realized_row_count();
    std::vector<Item> records;
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Item record;
        record.index = top_row_ * columns() + item;
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems.front(), object_view_item_part_fields, published.parts(), record.parts);
        const AnatomyStateSet item_states = realized_item_anatomy_states(item);
        read_states(object_view_item_state_fields, item_states, record.states);
        records.push_back(record);
    }
    return records;
}

std::vector<ObjectView::Column> ObjectView::realized_columns() const {
    const std::size_t count = realized_column_count();
    std::vector<Column> records;
    records.reserve(count);
    for (std::size_t position = 0; position < count; ++position) {
        const std::size_t index = realized_column_index(position);
        Column record;
        record.index = index;
        record.stable_id = std::string(stable_id().value()) + ".column." +
                           details_columns_[index].id.value;
        record.parts = column_parts(index);
        const AnatomyStateSet states = column_states(index);
        read_states(object_view_column_state_fields, states, record.states);
        record.states.sorted_ascending = states.contains(sorted_ascending_state);
        record.states.sorted_descending = states.contains(sorted_descending_state);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events ObjectView adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 4> object_view_interactions{{
    {.name = "sort_requested", .payload = "ObjectDetailsSort", .mutable_payload = false},
    {.name = "selection_changed", .payload = "ObjectSelectionChange", .mutable_payload = false},
    {.name = "item_activated", .payload = "std::string", .mutable_payload = false},
    {.name = "context_requested", .payload = "ObjectContextRequest", .mutable_payload = false},
}};

} // namespace

void ObjectView::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), object_view_interactions.begin(), object_view_interactions.end());
}

} // namespace gui_forms
