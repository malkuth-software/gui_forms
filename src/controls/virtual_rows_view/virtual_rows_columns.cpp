// VirtualRowsView's columns (the flame graph demonstrator's F4): the column
// model, the header strip's layout and paint, its pointer gestures (click,
// divider resize, reorder drag) and its semantic column headers. The row
// side (per-cell owner drawing inside the row loop) is in
// virtual_rows_view.cpp, beside the row paint target it follows.

#include "gui_forms/controls/virtual_rows_view.hpp"

#include "gui_forms/layout/types/layout_types.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

// The sort glyph's square and its lane at the header's reading end, and the
// label's inset from the header's edges, in logical units.
constexpr double sort_glyph_size = 8.0;
constexpr double sort_glyph_lane = 14.0;
constexpr double label_inset = 6.0;
constexpr std::string_view column_segment = ".column-";

bool valid_column(const VirtualRowsColumn& column) {
    if (column.id.empty() || !validate_utf8(column.header).valid()) return false;
    if (!std::isfinite(column.width) || !std::isfinite(column.minimum_width)) return false;
    const bool ordered = column.minimum_width >= 0.0 && column.width >= column.minimum_width;
    return ordered;
}

} // namespace

// -- the column model -------------------------------------------------------------------

void VirtualRowsView::set_columns(std::span<const VirtualRowsColumn> columns) {
    require_outside_callback();
    if (columns.size() > maximum_columns) throw std::invalid_argument("VirtualRowsView: too many columns");
    std::unordered_set<std::string_view> seen{};
    seen.reserve(columns.size());
    for (const VirtualRowsColumn& column : columns) {
        if (!valid_column(column)) throw std::invalid_argument("VirtualRowsView: invalid column");
        const bool fresh = seen.insert(std::string_view(column.id)).second;
        if (!fresh) throw std::invalid_argument("VirtualRowsView: repeated column id");
    }
    columns_.assign(columns.begin(), columns.end());
    sort_column_.reset();
    sort_direction_ = VirtualRowsSort::none;
    hot_column_.reset();
    header_gesture_ = HeaderGesture::none;
    place_columns();
    // The header strip comes or goes, so the viewport changes.
    invalidate(Dirty::layout | Dirty::paint | Dirty::semantics);
}

void VirtualRowsView::set_column_width(std::size_t column, double width) {
    require_outside_callback();
    if (column >= columns_.size()) throw std::out_of_range("VirtualRowsView: column past the end");
    if (!std::isfinite(width) || width < columns_[column].minimum_width) {
        throw std::invalid_argument("VirtualRowsView: column width below its minimum");
    }
    if (columns_[column].width == width) return;
    columns_[column].width = width;
    place_columns();
    invalidate(Dirty::paint | Dirty::semantics);
    column_resized_.emit(column);
}

void VirtualRowsView::move_column(std::size_t from, std::size_t to) {
    require_outside_callback();
    if (from >= columns_.size() || to >= columns_.size()) {
        throw std::out_of_range("VirtualRowsView: column past the end");
    }
    if (from == to) return;
    VirtualRowsColumn moved = std::move(columns_[from]);
    columns_.erase(columns_.begin() + static_cast<std::ptrdiff_t>(from));
    columns_.insert(columns_.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    // The sort column follows its column; the others between shift by one.
    if (sort_column_.has_value()) {
        std::size_t sorted = *sort_column_;
        if (sorted == from) {
            sorted = to;
        } else if (from < sorted && sorted <= to) {
            sorted = sorted - 1U;
        } else if (to <= sorted && sorted < from) {
            sorted = sorted + 1U;
        }
        sort_column_ = sorted;
    }
    hot_column_.reset();
    place_columns();
    invalidate(Dirty::paint | Dirty::semantics);
    VirtualRowsColumnMove change{};
    change.from = from;
    change.to = to;
    column_moved_.emit(change);
}

void VirtualRowsView::set_sort(std::optional<std::size_t> column, VirtualRowsSort direction) {
    require_outside_callback();
    if (column.has_value() && *column >= columns_.size()) {
        throw std::out_of_range("VirtualRowsView: column past the end");
    }
    const bool cleared = !column.has_value() || direction == VirtualRowsSort::none;
    const std::optional<std::size_t> next = cleared ? std::nullopt : column;
    const VirtualRowsSort next_direction = cleared ? VirtualRowsSort::none : direction;
    if (next == sort_column_ && next_direction == sort_direction_) return;
    sort_column_ = next;
    sort_direction_ = next_direction;
    invalidate(Dirty::paint | Dirty::semantics);
}

void VirtualRowsView::place_columns() {
    column_widths_.resize(columns_.size());
    for (std::size_t column = 0U; column < columns_.size(); ++column) column_widths_[column] = columns_[column].width;
    // No frozen columns here: the header scrolls with nothing.
    column_strip_.assign(column_widths_, 0U);
}

Rect VirtualRowsView::column_extent(std::size_t column) const noexcept {
    const double width = columns_[column].width;
    const double start = column_strip_.start(column);
    const double reading_x = viewport_.x + start;
    const double x = mirrored_ ? viewport_.x + viewport_.width - start - width : reading_x;
    const Rect extent{x, header_.y, width, header_.height};
    return extent;
}

Rect VirtualRowsView::column_cell(std::size_t column, Rect row) const {
    if (column >= columns_.size()) throw std::out_of_range("VirtualRowsView: column past the end");
    const Rect extent = column_extent(column);
    const Rect cell{extent.x, row.y, extent.width, row.height};
    return cell;
}

// By the strip's descent, O(log columns): left to right a column holds
// [start, start + width) from the viewport's left; mirrored it holds
// (start, start + width] measured from the viewport's right.
std::optional<std::size_t> VirtualRowsView::column_at(Point local_point) const noexcept {
    const std::size_t count = column_strip_.count();
    if (!mirrored_) {
        const std::size_t found = column_strip_.position_at(local_point.x - viewport_.x);
        if (found >= count) return std::nullopt;
        return found;
    }
    const double distance = viewport_.x + viewport_.width - local_point.x;
    const std::size_t past = column_strip_.first_start_at_or_past(distance, 0U, count);
    if (past == 0U) return std::nullopt;
    const std::size_t found = past - 1U;
    if (distance > column_strip_.start(found) + column_strip_.width(found)) return std::nullopt;
    return found;
}

AnatomyStateSet VirtualRowsView::column_states(std::size_t column) const noexcept {
    AnatomyStateSet states;
    if (hot_column_.has_value() && *hot_column_ == column) states.insert(AnatomyState::hot);
    const bool pressed = header_gesture_ != HeaderGesture::none && header_gesture_ != HeaderGesture::resizing &&
                         gesture_column_ == column;
    if (pressed) states.insert(AnatomyState::pressed);
    if (sort_column_.has_value() && *sort_column_ == column) states.insert(AnatomyState::selected);
    return states;
}

// -- the header's parts and paint ----------------------------------------------------------

VirtualRowsView::ColumnHeaderParts VirtualRowsView::column_header_parts(std::size_t column) const {
    ColumnHeaderParts record;
    const Rect header = column_extent(column);
    const Rect shown = Rect::intersection(header, header_);
    if (shown.empty()) return record;
    record.column_header = PartGeometry::from_bounds(shown);
    // The sort glyph's lane is kept at the reading end (the right, or the
    // left right to left); the label takes the rest, inset.
    const bool sorted = sort_column_.has_value() && *sort_column_ == column;
    const double lane = sorted ? sort_glyph_lane : 0.0;
    const double label_width = std::max(0.0, header.width - 2.0 * label_inset - lane);
    const double label_x = mirrored_ ? header.x + label_inset + lane : header.x + label_inset;
    const Rect label{label_x, header.y, label_width, header.height};
    record.column_label = PartGeometry::from_bounds(Rect::intersection(label, header_));
    if (sorted) {
        const double glyph_x = header.x + header.width - sort_glyph_lane + (sort_glyph_lane - sort_glyph_size) * 0.5;
        const Rect reading{glyph_x, header.y + (header.height - sort_glyph_size) * 0.5, sort_glyph_size,
                           sort_glyph_size};
        const Rect glyph = mirrored_ ? horizontally_mirrored(reading, header) : reading;
        record.sort_glyph = PartGeometry::from_bounds(Rect::intersection(glyph, header_));
    }
    const double edge = mirrored_ ? header.x : header.x + header.width - 1.0;
    record.column_divider = PartGeometry::from_bounds(Rect::intersection(Rect{edge, header.y, 1.0, header.height}, header_));
    return record;
}

FontSpec VirtualRowsView::part_text_font(const std::string_view part, const AnatomyStateSet states) const {
    const Theme& theme = effective_theme();
    const FontSpec base = theme.structure().typography.control;
    const ResolvedFont resolved = theme.resolve_font(*this, base, part, states);
    const FontSpec scaled = effective_font(resolved.font);
    return scaled;
}

void VirtualRowsView::paint_column_headers(Painter& painter) const {
    if (header_.empty()) return;
    painter.save();
    painter.clip_rect(header_);
    PartPaint band;
    band.family = ControlVisualRole::panel;
    band.role = PartRole::face;
    band.bounds = header_;
    paint_part(painter, *this, band);
    PartPaint part;
    part.family = ControlVisualRole::panel;
    for (std::size_t column = 0U; column < columns_.size(); ++column) {
        const ColumnHeaderParts parts = column_header_parts(column);
        if (!parts.column_header.present) continue;
        part.states = column_states(column);
        part.role = PartRole::face;
        part.bounds = parts.column_header.bounds;
        paint_part(painter, *this, part);
        if (parts.column_label.present && !columns_[column].header.empty()) {
            part.role = PartRole::label;
            part.bounds = parts.column_label.bounds;
            part.font = part_text_font("column_label", part.states);
            part.text = lend_paint_text(columns_[column].header);
            part.alignment = columns_[column].alignment;
            paint_part(painter, *this, part);
            part.text = {};
            part.alignment = PartTextAlignment::start;
        }
        if (parts.sort_glyph.present) {
            part.role = PartRole::glyph;
            part.bounds = parts.sort_glyph.bounds;
            part.mark = sort_direction_ == VirtualRowsSort::ascending ? PartMark::arrow_up : PartMark::arrow_down;
            paint_part(painter, *this, part);
            part.mark = PartMark::none;
        }
        if (parts.column_divider.present) {
            part.role = PartRole::divider;
            part.bounds = parts.column_divider.bounds;
            paint_part(painter, *this, part);
        }
    }
    painter.restore();
}

// -- the header's pointer ---------------------------------------------------------------

std::optional<std::size_t> VirtualRowsView::divider_at(Point local) const noexcept {
    if (!header_.contains(local)) return std::nullopt;
    for (std::size_t column = 0U; column < columns_.size(); ++column) {
        if (!columns_[column].resizable) continue;
        const Rect extent = column_extent(column);
        const double edge = mirrored_ ? extent.x : extent.x + extent.width;
        if (std::fabs(local.x - edge) <= column_grip_half_width) return column;
    }
    return std::nullopt;
}

std::size_t VirtualRowsView::reorder_target(Point local) const noexcept {
    // The column under the pointer, or the nearer end past the columns.
    const std::optional<std::size_t> under = column_at(local);
    if (under.has_value()) return *under;
    const double reading = mirrored_ ? viewport_.x + viewport_.width - local.x : local.x - viewport_.x;
    const std::size_t target = reading < 0.0 ? 0U : columns_.size() - 1U;
    return target;
}

bool VirtualRowsView::header_press(Point local, Modifier modifiers) {
    if (columns_.empty() || !header_.contains(local)) return false;
    const std::optional<std::size_t> divider = divider_at(local);
    if (divider.has_value()) {
        header_gesture_ = HeaderGesture::resizing;
        gesture_column_ = *divider;
        gesture_width_ = columns_[*divider].width;
    } else {
        const std::optional<std::size_t> under = column_at(local);
        if (!under.has_value()) return true;
        header_gesture_ = HeaderGesture::pressed;
        gesture_column_ = *under;
    }
    gesture_start_ = local;
    gesture_modifiers_ = modifiers;
    invalidate(Dirty::paint);
    return true;
}

bool VirtualRowsView::header_move(Point local) {
    std::optional<std::size_t> hot{};
    if (header_.contains(local)) hot = column_at(local);
    if (hot != hot_column_) {
        hot_column_ = hot;
        invalidate(Dirty::paint);
    }
    switch (header_gesture_) {
    case HeaderGesture::none:
        return false;
    case HeaderGesture::resizing: {
        // Right to left the divider is at the column's left: moving left
        // widens it.
        const double travel = mirrored_ ? gesture_start_.x - local.x : local.x - gesture_start_.x;
        const double width = std::max(columns_[gesture_column_].minimum_width, gesture_width_ + travel);
        if (width != columns_[gesture_column_].width) {
            columns_[gesture_column_].width = width;
            place_columns();
            invalidate(Dirty::paint | Dirty::semantics);
        }
        return true;
    }
    case HeaderGesture::pressed:
        if (std::fabs(local.x - gesture_start_.x) >= column_drag_threshold && columns_[gesture_column_].reorderable) {
            header_gesture_ = HeaderGesture::reordering;
            invalidate(Dirty::paint);
        }
        return true;
    case HeaderGesture::reordering:
        return true;
    default:
        header_gesture_ = HeaderGesture::none;
        return false;
    }
}

bool VirtualRowsView::header_release(Point local) {
    const HeaderGesture gesture = header_gesture_;
    const std::size_t column = gesture_column_;
    header_gesture_ = HeaderGesture::none;
    switch (gesture) {
    case HeaderGesture::none:
        return false;
    case HeaderGesture::resizing:
        invalidate(Dirty::paint);
        if (columns_[column].width != gesture_width_) column_resized_.emit(column);
        return true;
    case HeaderGesture::pressed: {
        invalidate(Dirty::paint);
        const bool on_header = header_.contains(local) && column_at(local) == std::optional<std::size_t>(column);
        if (on_header && columns_[column].sortable) {
            VirtualRowsHeaderClick click{};
            click.column = column;
            click.modifiers = gesture_modifiers_;
            header_clicked_.emit(click);
        }
        return true;
    }
    case HeaderGesture::reordering:
        invalidate(Dirty::paint);
        move_column(column, reorder_target(local));
        return true;
    default:
        return false;
    }
}

// -- semantics --------------------------------------------------------------------------

void VirtualRowsView::column_identity(std::size_t column, std::string& identity) const {
    identity.assign(stable_id().value());
    identity.append(column_segment);
    identity.append(columns_[column].id);
}

std::optional<std::size_t> VirtualRowsView::column_of_child_id(std::string_view stable_id_text) const noexcept {
    const std::string_view own = stable_id().value();
    if (stable_id_text.size() <= own.size() + column_segment.size()) return std::nullopt;
    if (stable_id_text.substr(0U, own.size()) != own) return std::nullopt;
    if (stable_id_text.substr(own.size(), column_segment.size()) != column_segment) return std::nullopt;
    const std::string_view id = stable_id_text.substr(own.size() + column_segment.size());
    for (std::size_t column = 0U; column < columns_.size(); ++column) {
        if (columns_[column].id == id) return column;
    }
    return std::nullopt;
}

void VirtualRowsView::append_column_semantics(std::vector<SemanticNode>& children) const {
    if (header_.empty()) return;
    const Rect absolute = absolute_bounds();
    const bool enabled = effectively_enabled();
    for (std::size_t column = 0U; column < columns_.size(); ++column) {
        const Rect shown = Rect::intersection(column_extent(column), header_);
        SemanticNode node{};
        column_identity(column, node.stable_id);
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::column_header;
        node.column_index.set(column + 1U);
        node.name = columns_[column].header;
        if (sort_column_.has_value() && *sort_column_ == column) {
            node.value = sort_direction_ == VirtualRowsSort::ascending ? "ascending" : "descending";
        }
        node.bounds = {absolute.x + shown.x, absolute.y + shown.y, shown.width, shown.height};
        node.states = SemanticState::visible;
        if (enabled) node.states |= SemanticState::enabled;
        if (columns_[column].sortable) node.actions = {SemanticAction::press};
        children.push_back(std::move(node));
    }
}

} // namespace gui_forms
