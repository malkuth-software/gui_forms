#include "gui_forms/controls/panel/correspondence_view/correspondence_view.hpp"

#include "../collection_control_utilities.hpp"
#include "gui_forms/detail/algorithm/sort.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"
#include "../../basic/basic_control_rendering.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

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

struct CorrespondenceView::HoverIntentCallback final {
    std::weak_ptr<CorrespondenceView> view;
    std::string intended;

    void operator()(FrameTime) const {
        const std::shared_ptr<CorrespondenceView> retained = view.lock();
        if (!retained) return;
        (*retained).hover_timer_.disconnect();
        (*retained).apply_hover_expansion(intended);
    }
};

CorrespondenceView::CorrespondenceView(StableId stable_id)
    : Panel(std::move(stable_id)) {
    set_paint_plane(PaintPlane::control);
    set_background(style().paper);
    set_border_style(BorderStyle::sunken);
    set_focusable(true);
}

std::optional<std::size_t> CorrespondenceView::item_index(
    std::string_view id) const noexcept {
    for (std::size_t index = 0U; index < items_.size(); ++index) {
        if (items_[index].stable_id == id) return index;
    }
    return {};
}

void CorrespondenceView::set_items(std::vector<CorrespondenceItem> items) {
    require_mutable();
    std::unordered_set<std::string> identities;
    for (const CorrespondenceItem& item : items) {
        validate_identity_text(item.stable_id, item.title);
        const std::string_view fields[]{item.secondary_text, item.metadata,
            item.excerpt, item.metric, item.information_heading,
            item.information_detail};
        bool valid = true;
        for (std::string_view value : fields) {
            if (!validate_utf8(value).valid()) {
                valid = false;
                break;
            }
        }
        for (const std::string& value : item.information_tags) {
            if (value.empty() || !validate_utf8(value).valid()) {
                valid = false;
                break;
            }
        }
        for (const std::string& value : item.emphasis_terms) {
            if (value.empty() || !validate_utf8(value).valid()) {
                valid = false;
                break;
            }
        }
        if (!valid || !identities.insert(item.stable_id).second) {
            throw std::invalid_argument(
                "CorrespondenceView requires unique IDs and valid UTF-8 fields");
        }
    }

    const ScrollAnchor anchor = capture_anchor(first_visible_index());
    const CorrespondenceSelectionChange selection_change{
        selected_id_, item_index(selected_id_) ? selected_id_ : std::string{}};
    const CorrespondencePinChange pin_change{
        pinned_id_, item_index(pinned_id_) ? pinned_id_ : std::string{}};
    // A press in progress survives a re-projection when its item identity
    // does: a consumer that re-projects on selection must not swallow the
    // release that follows (and the context request it carries).
    const std::string pressed_id = pressed_index_ && *pressed_index_ < items_.size()
        ? items_[*pressed_index_].stable_id : std::string{};
    items_ = std::move(items);
    retain_identity(selected_id_);
    retain_identity(focused_id_);
    retain_identity(pinned_id_);
    hovered_id_.clear();
    hover_expanded_id_.clear();
    pending_hover_id_.clear();
    pressed_index_ = pressed_id.empty() ? std::nullopt : item_index(pressed_id);
    if (!pressed_index_) pressed_button_ = PointerButton::none;
    hover_timer_.disconnect();
    restore_anchor(anchor);
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
    if (selection_change.previous_id != selected_id_) {
        publish_change(selection_changed_, CorrespondenceSelectionChange{
            selection_change.previous_id, selected_id_});
    }
    if (pin_change.previous_id != pinned_id_) {
        publish_change(pin_changed_, CorrespondencePinChange{
            pin_change.previous_id, pinned_id_});
    }
}

void CorrespondenceView::retain_identity(std::string& id) const {
    if (!id.empty() && !item_index(id)) id.clear();
}

void CorrespondenceView::set_selected_id(std::string_view stable_id) {
    require_mutable();
    if (!stable_id.empty() && !item_index(stable_id)) {
        throw std::out_of_range(
            "CorrespondenceView selection ID is not in the model");
    }
    const std::string next(stable_id);
    if (selected_id_ == next) return;
    CorrespondenceSelectionChange change{selected_id_, next};
    selected_id_ = next;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(selection_changed_, change);
}

bool CorrespondenceView::expanded(std::string_view stable_id) const {
    const std::optional<std::size_t> index = item_index(stable_id);
    if (!index) {
        throw std::out_of_range(
            "CorrespondenceView expansion ID is not in the model");
    }
    return expanded_index(*index);
}

void CorrespondenceView::emit_expansion_delta(
    std::string_view stable_id, bool before, bool after,
    CorrespondenceExpansionReason reason) {
    if (stable_id.empty() || before == after || !is_alive()) return;
    publish_change(expansion_changed_, CorrespondenceExpansionChange{
        std::string(stable_id), after, stable_id == pinned_id_, reason});
}

void CorrespondenceView::set_pinned_id(std::string_view stable_id) {
    require_mutable();
    const std::optional<std::size_t> next_index = stable_id.empty() ? std::optional<std::size_t>{}
                                               : item_index(stable_id);
    if (!stable_id.empty() && !next_index) {
        throw std::out_of_range(
            "CorrespondenceView pinned ID is not in the model");
    }
    const std::string next(stable_id);
    if (pinned_id_ == next) return;
    const std::string previous = pinned_id_;
    const bool previous_before = !previous.empty() && expanded(previous);
    const bool next_before = !next.empty() && expanded(next);
    std::size_t preferred = first_visible_index();
    if (next_index) {
        const double top = row_top(*next_index);
        const double bottom = top + row_height(*next_index);
        if (bottom > scroll_offset_ &&
            top < scroll_offset_ + viewport_height()) {
            preferred = *next_index;
        }
    }
    const ScrollAnchor anchor = capture_anchor(preferred);
    pinned_id_ = next;
    restore_anchor(anchor);
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
    publish_change(pin_changed_,
                   CorrespondencePinChange{previous, pinned_id_});
    if (!previous.empty()) {
        emit_expansion_delta(previous, previous_before, expanded(previous),
                             CorrespondenceExpansionReason::pin);
    }
    if (!next.empty() && next != previous) {
        emit_expansion_delta(next, next_before, expanded(next),
                             CorrespondenceExpansionReason::pin);
    }
}

void CorrespondenceView::set_compact_height(double height) {
    require_mutable();
    if (!std::isfinite(height) || height < 24.0 || height > 192.0 ||
        height >= expanded_height_) {
        throw std::invalid_argument(
            "CorrespondenceView compact height must be finite, bounded, and below expanded height");
    }
    if (compact_height_ == height) return;
    const ScrollAnchor anchor = capture_anchor(first_visible_index());
    compact_height_ = height;
    restore_anchor(anchor);
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
}

void CorrespondenceView::set_expanded_height(double height) {
    require_mutable();
    if (!std::isfinite(height) || height <= compact_height_ || height > 512.0) {
        throw std::invalid_argument(
            "CorrespondenceView expanded height must be finite, bounded, and above compact height");
    }
    if (expanded_height_ == height) return;
    const ScrollAnchor anchor = capture_anchor(first_visible_index());
    expanded_height_ = height;
    restore_anchor(anchor);
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
}

void CorrespondenceView::set_status_rail_width(double width) {
    require_mutable();
    if (!std::isfinite(width) || width < 1.0 || width > 12.0) {
        throw std::invalid_argument(
            "CorrespondenceView status rail width must be finite and between 1 and 12");
    }
    if (status_rail_width_ == width) return;
    status_rail_width_ = width;
    invalidate(Dirty::paint);
}

void CorrespondenceView::set_hover_intent_delay(
    std::chrono::milliseconds delay) {
    require_mutable();
    if (delay < std::chrono::milliseconds(1) ||
        delay > std::chrono::seconds(5)) {
        throw std::invalid_argument(
            "CorrespondenceView hover intent must be between 1 ms and 5 s");
    }
    if (hover_intent_delay_ == delay) return;
    hover_intent_delay_ = delay;
    if (const std::optional<std::size_t> index = item_index(hovered_id_)) schedule_hover_intent(*index);
}

void CorrespondenceView::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) {
        throw std::invalid_argument("CorrespondenceView font is invalid");
    }
    set_font_from_code(true);
    if (font_ == font) return;
    font_ = font;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

std::vector<std::size_t> CorrespondenceView::expanded_indices() const {
    std::vector<std::size_t> indices;
    indices.reserve(3U);
    insert_expanded_index(indices, pinned_id_);
    insert_expanded_index(indices, hover_expanded_id_);
    if (focused_) insert_expanded_index(indices, focused_id_);
    // The collection is structurally bounded to the pinned, hover, and focus
    // roles. Stable insertion states that three-element contract directly and
    // avoids general-sort setup; SORT_LAB_M4_RESULTS.csv retains the control.
    detail::stable_insertion_sort(std::span<std::size_t>(indices));
    return indices;
}

void CorrespondenceView::insert_expanded_index(
    std::vector<std::size_t>& indices, std::string_view id) const {
    const std::optional<std::size_t> index = item_index(id);
    if (index && std::find(indices.begin(), indices.end(), *index) ==
                     indices.end()) {
        indices.push_back(*index);
    }
}

bool CorrespondenceView::expanded_index(std::size_t index) const noexcept {
    if (index >= items_.size()) return false;
    const std::string& id = items_[index].stable_id;
    return id == pinned_id_ || id == hover_expanded_id_ ||
           (focused_ && id == focused_id_);
}

double CorrespondenceView::scaled_compact_height() const noexcept {
    return compact_height_ * effective_text_scale();
}

double CorrespondenceView::scaled_expanded_height() const noexcept {
    return expanded_height_ * effective_text_scale();
}

double CorrespondenceView::row_height(std::size_t index) const noexcept {
    return expanded_index(index) ? scaled_expanded_height()
                                 : scaled_compact_height();
}

double CorrespondenceView::row_top(std::size_t index) const noexcept {
    const double compact = scaled_compact_height();
    const double delta = scaled_expanded_height() - compact;
    const std::vector<std::size_t> expanded = expanded_indices();
    std::size_t before = 0U;
    for (std::size_t value : expanded) {
        if (value < index) ++before;
    }
    return static_cast<double>(index) * compact +
           static_cast<double>(before) * delta;
}

double CorrespondenceView::content_height() const noexcept {
    if (items_.empty()) return 0.0;
    return static_cast<double>(items_.size()) * scaled_compact_height() +
        static_cast<double>(expanded_indices().size()) *
            (scaled_expanded_height() - scaled_compact_height());
}

double CorrespondenceView::viewport_height() const noexcept {
    double height = local_bounds().height;
    if (height <= 4.0) height = requested_bounds().height;
    return std::max(0.0, height - 4.0);
}

double CorrespondenceView::maximum_scroll_offset() const noexcept {
    return std::max(0.0, content_height() - viewport_height());
}

void CorrespondenceView::clamp_scroll_offset() noexcept {
    scroll_offset_ = std::clamp(scroll_offset_, 0.0, maximum_scroll_offset());
}

void CorrespondenceView::set_scroll_offset(double offset) {
    require_mutable();
    if (!std::isfinite(offset) || offset < 0.0) {
        throw std::invalid_argument(
            "CorrespondenceView scroll offset must be finite and nonnegative");
    }
    const double next = std::clamp(offset, 0.0, maximum_scroll_offset());
    if (scroll_offset_ == next) return;
    scroll_offset_ = next;
    clear_hover_intent(true);
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

std::size_t CorrespondenceView::first_visible_index() const noexcept {
    if (items_.empty()) return 0U;
    std::size_t low{};
    std::size_t high = items_.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (row_top(middle) + row_height(middle) <= scroll_offset_) low = middle + 1U;
        else high = middle;
    }
    return std::min(low, items_.size() - 1U);
}

std::pair<std::size_t, std::size_t>
CorrespondenceView::realized_range() const noexcept {
    if (items_.empty()) return {0U, 0U};
    const std::size_t first_visible = first_visible_index();
    const std::size_t first = first_visible == 0U ? 0U : first_visible - 1U;
    const double limit = scroll_offset_ + viewport_height();
    std::size_t end = first_visible;
    while (end < items_.size() && row_top(end) < limit) ++end;
    end = std::min(items_.size(), end + 1U);
    return {first, end};
}

std::size_t CorrespondenceView::realized_count() const noexcept {
    const std::pair<std::size_t, std::size_t> range = realized_range();
    const std::size_t first = range.first;
    const std::size_t end = range.second;
    return end - first;
}

Rect CorrespondenceView::item_bounds(std::size_t index) const noexcept {
    if (index >= items_.size()) return {};
    const double width = std::max(0.0, local_bounds().width - 4.0);
    return {2.0, 2.0 + row_top(index) - scroll_offset_, width,
            row_height(index)};
}

std::optional<Rect> CorrespondenceView::item_bounds(
    std::string_view stable_id) const noexcept {
    const std::optional<std::size_t> index = item_index(stable_id);
    return index ? std::optional<Rect>{item_bounds(*index)}
                 : std::optional<Rect>{};
}

std::optional<std::size_t> CorrespondenceView::index_at(
    Point absolute) const noexcept {
    const Rect bounds = absolute_bounds();
    if (!bounds.contains(absolute)) return {};
    const double content_y = absolute.y - bounds.y - 2.0 + scroll_offset_;
    if (content_y < 0.0 || items_.empty()) return {};
    std::size_t low{};
    std::size_t high = items_.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (row_top(middle) + row_height(middle) <= content_y) low = middle + 1U;
        else high = middle;
    }
    if (low >= items_.size() || content_y < row_top(low)) return {};
    return low;
}

CorrespondenceView::ScrollAnchor CorrespondenceView::capture_anchor(
    std::size_t preferred) const noexcept {
    if (items_.empty()) return {};
    const std::size_t index = std::min(preferred, items_.size() - 1U);
    return {items_[index].stable_id, index,
            row_top(index) - scroll_offset_, true};
}

void CorrespondenceView::restore_anchor(ScrollAnchor anchor) {
    if (!anchor.valid || items_.empty()) {
        clamp_scroll_offset();
        return;
    }
    const std::size_t index = item_index(anchor.stable_id).value_or(
        std::min(anchor.index, items_.size() - 1U));
    scroll_offset_ = row_top(index) - anchor.viewport_y;
    clamp_scroll_offset();
}

void CorrespondenceView::ensure_visible(std::size_t index) {
    if (index >= items_.size()) return;
    const double top = row_top(index);
    const double bottom = top + row_height(index);
    const double height = viewport_height();
    if (top < scroll_offset_) scroll_offset_ = top;
    else if (bottom > scroll_offset_ + height) {
        scroll_offset_ = std::max(0.0, bottom - height);
    }
    clamp_scroll_offset();
}

void CorrespondenceView::focus_index(
    std::size_t index, CorrespondenceExpansionReason reason) {
    if (index >= items_.size() || !items_[index].enabled) return;
    const std::string previous = focused_id_;
    const bool previous_before = !previous.empty() && expanded(previous);
    const bool next_before = expanded(items_[index].stable_id);
    const ScrollAnchor anchor = capture_anchor(index);
    focused_id_ = items_[index].stable_id;
    restore_anchor(anchor);
    ensure_visible(index);
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
    if (!previous.empty()) {
        emit_expansion_delta(previous, previous_before, expanded(previous), reason);
    }
    if (focused_id_ != previous) {
        emit_expansion_delta(focused_id_, next_before, expanded(focused_id_), reason);
    }
}

void CorrespondenceView::schedule_hover_intent(std::size_t index) {
    hover_timer_.disconnect();
    if (index >= items_.size() || !items_[index].enabled || !window()) return;
    pending_hover_id_ = items_[index].stable_id;
    const std::string intended = pending_hover_id_;
    const std::shared_ptr<gui_forms::CorrespondenceView> self = std::static_pointer_cast<CorrespondenceView>(
        shared_from_this());
    std::weak_ptr<CorrespondenceView> weak = self;
    hover_timer_ = (*window()).schedule_ui_timer(
        *this, std::chrono::hours(24),
        FrameClock::now() + hover_intent_delay_,
        HoverIntentCallback{weak, intended});
}

void CorrespondenceView::apply_hover_expansion(std::string stable_id) {
    if (hovered_id_ != stable_id || pending_hover_id_ != stable_id ||
        !item_index(stable_id)) return;
    const std::string previous = hover_expanded_id_;
    const bool previous_before = !previous.empty() && expanded(previous);
    const bool next_before = expanded(stable_id);
    const ScrollAnchor anchor = capture_anchor(*item_index(stable_id));
    hover_expanded_id_ = std::move(stable_id);
    pending_hover_id_.clear();
    restore_anchor(anchor);
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
    if (!previous.empty()) {
        emit_expansion_delta(previous, previous_before, expanded(previous),
                             CorrespondenceExpansionReason::hover_intent);
    }
    emit_expansion_delta(hover_expanded_id_, next_before,
                         expanded(hover_expanded_id_),
                         CorrespondenceExpansionReason::hover_intent);
}

void CorrespondenceView::clear_hover_intent(bool clear_expansion) {
    hover_timer_.disconnect();
    pending_hover_id_.clear();
    if (!clear_expansion || hover_expanded_id_.empty()) return;
    const std::string previous = hover_expanded_id_;
    const bool before = expanded(previous);
    const std::size_t anchor_index = item_index(hovered_id_).value_or(
        first_visible_index());
    const ScrollAnchor anchor = capture_anchor(anchor_index);
    hover_expanded_id_.clear();
    restore_anchor(anchor);
    emit_expansion_delta(previous, before, expanded(previous),
                         CorrespondenceExpansionReason::hover_intent);
}

void CorrespondenceView::set_hovered_index(
    std::optional<std::size_t> index) {
    const std::string next = index && *index < items_.size()
        ? items_[*index].stable_id : std::string{};
    if (hovered_id_ == next) return;
    clear_hover_intent(true);
    hovered_id_ = next;
    if (index) schedule_hover_intent(*index);
    invalidate(Dirty::paint | Dirty::semantics);
}

void CorrespondenceView::arrange(Rect final_bounds) {
    Panel::arrange(final_bounds);
    clamp_scroll_offset();
    if (const std::optional<std::size_t> focused = item_index(focused_id_)) ensure_visible(*focused);
}

// A glyph is a well (the folder's body or the page) with the glyph's colour
// or the paper as its ground, framed by the theme's edge, and content fills
// for the folder's tab and the page's band and marks: the colours are the
// item kind's own, as a swatch's are. A disabled item's well is disabled
// and its colour the face's.
void CorrespondenceView::paint_glyph(Painter& painter, Rect b,
                                     ObjectGlyph glyph,
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
        well.bounds = {b.x + 2.0, b.y + 8.0, b.width - 4.0, b.height - 10.0};
        well.authored_background = base;
        paint_part(painter, *this, well);
        fill.bounds = {b.x + 6.0, b.y + 4.0, b.width * .44, 4.0};
        fill.content_color = with_alpha(style().highlight, 190);
        paint_part(painter, *this, fill);
        return;
    }
    well.bounds = {b.x + 5.0, b.y + 1.0, b.width - 10.0, b.height - 2.0};
    well.authored_background = style().paper;
    paint_part(painter, *this, well);
    fill.bounds = {b.x + 6.0, b.y + 2.0, b.width - 12.0, 5.0};
    fill.content_color = base;
    paint_part(painter, *this, fill);
    if (glyph == ObjectGlyph::image) {
        fill.bounds = {b.x + 9.0, b.y + 11.0, b.width - 18.0, b.height - 17.0};
        fill.content_color = with_alpha(base, 120);
        paint_part(painter, *this, fill);
    } else {
        fill.bounds = {b.x + 9.0, b.y + 12.5, b.width - 18.0, 1.0};
        paint_part(painter, *this, fill);
        fill.bounds = {b.x + 9.0, b.y + 17.5, b.width - 21.0, 1.0};
        paint_part(painter, *this, fill);
    }
}

CorrespondenceView::CorrespondenceCardFonts CorrespondenceView::card_fonts() const {
    CorrespondenceCardFonts fonts;
    const FontSpec title = text_font(font_);
    fonts.title = {title.role, title.size, 700, title.italic, title.letter_spacing};
    fonts.secondary = effective_font({font_.role, std::max(8.0, font_.size - 2.0), 400, false});
    fonts.metric = effective_font({FontRole::content, std::max(12.0, font_.size + 5.0), 700, false});
    fonts.evidence = effective_font({FontRole::content, std::max(8.0, font_.size - 1.5), 400, false});
    fonts.evidence_bold = effective_font({FontRole::content, std::max(8.0, font_.size - 1.5), 700, false});
    return fonts;
}

double CorrespondenceView::measured_text_width(std::string_view text, FontSpec font) const {
    if (text.empty()) return 0.0;
    const ResolvedTextLayout measured = window() != nullptr
        ? (*window()).resolve_text_layout_utf8(text, font)
        : estimate_text_layout_utf8(text, font);
    return measured.logical_size.width;
}

// Sweep defect D9: the state word sat at a fixed offset from the right
// edge, over the title of a narrow card; the metric at a fixed offset ran
// over the secondary line; and the narrow information line was drawn over
// the excerpt box of a short expanded card.
CorrespondenceView::CorrespondenceCardLayout CorrespondenceView::card_layout(std::size_t index,
                                                               const CorrespondenceCardFonts& fonts) const {
    const CorrespondenceItem& item = items_[index];
    const Rect row = item_bounds(index);
    const double visible_height = std::max(0.0, row.height - 5.0);
    constexpr double text_x = 48.0;
    constexpr double right_margin = 10.0;
    constexpr double gap = 8.0;
    CorrespondenceCardLayout layout;
    layout.card = {row.x, row.y, row.width, visible_height};
    layout.status_rail = {row.x + 1.0, row.y + 1.0, status_rail_width_, std::max(0.0, visible_height - 2.0)};
    layout.glyph = {row.x + 10.0, row.y + 7.0, 30.0, 30.0};

    const double metric_width = measured_text_width(item.metric, fonts.metric);
    const double metric_x = std::max(row.x + text_x, row.right() - right_margin - metric_width);
    layout.metric_origin = {metric_x, row.y + 27.0};
    layout.metric_box = {metric_x, row.y + 12.0, std::max(0.0, row.right() - metric_x - 4.0), 20.0};

    const bool pinned = item.stable_id == pinned_id_;
    if (pinned) layout.state_text = "PINNED";
    else if (item.unavailable) layout.state_text = "OFFLINE";
    else if (item.stale) layout.state_text = "STALE";
    double title_end = metric_x - gap;
    if (!layout.state_text.empty()) {
        const double state_width = measured_text_width(layout.state_text, fonts.evidence_bold);
        const double state_x = std::max(row.x + text_x, metric_x - gap - state_width);
        layout.state_origin = {state_x, row.y + 12.0};
        title_end = state_x - gap;
    }
    layout.title_origin = {row.x + text_x, row.y + 17.0};
    layout.title_width = std::max(0.0, title_end - layout.title_origin.x);
    layout.secondary_origin = {row.x + text_x, row.y + 34.0};
    layout.secondary_width = std::max(0.0, metric_x - gap - layout.secondary_origin.x);

    layout.expanded = expanded_index(index);
    if (!layout.expanded) return layout;
    const double scale = effective_text_scale();
    layout.body_top = row.y + scaled_compact_height() - 5.0 * scale;
    layout.wide = row.width >= 650.0 * scale;
    layout.lane_width = layout.wide ? std::min(190.0 * scale, std::max(150.0 * scale, row.width * 0.25)) : 0.0;
    layout.evidence_x = row.x + 52.0 * scale;
    const double evidence_width = std::max(40.0, row.width - 62.0 * scale - layout.lane_width);
    const double full_height = std::max(20.0, 43.0 * scale);
    layout.excerpt_box = {layout.evidence_x, layout.body_top + 22.0 * scale, evidence_width, full_height};
    if (layout.wide) return layout;
    // The narrow information line goes under the box. The box gives up
    // height down to what its heading (baseline +15) and excerpt line
    // (baseline +31) need; below that the line is not drawn.
    const double minimum_height = std::max(20.0, 34.0 * scale);
    const Point footer{layout.evidence_x, row.y + visible_height - 7.0 * scale};
    const double room = footer.y - fonts.evidence.size - 3.0 * scale - layout.excerpt_box.y;
    if (room >= minimum_height) {
        layout.excerpt_box.height = std::min(full_height, room);
        layout.footer_shown = true;
        layout.footer_origin = footer;
    }
    return layout;
}

void CorrespondenceView::on_paint(Painter& painter, Rect damage) {
    Panel::on_paint(painter, damage);
    const Rect bounds = local_bounds();
    const CorrespondenceCardFonts fonts = card_fonts();
    painter.save();
    painter.clip_rect({2.0, 2.0, std::max(0.0, bounds.width - 4.0),
                       std::max(0.0, bounds.height - 4.0)});
    // Each item's card and plate, glyph, title, metric, rules, evidence
    // well and texts and the active item's focus ring are parts the theme's
    // engine draws; an item is hot under the pointer, selected when selected
    // and disabled with its item. A style override supplies the plates'
    // grounds (T4-1).
    AnatomyStateSet states = anatomy_states();
    states.erase(AnatomyState::hot);
    const bool overridden = has_style_override();
    const std::pair<std::size_t, std::size_t> range = realized_range();
    const std::size_t first = range.first;
    const std::size_t end = range.second;
    for (std::size_t index = first; index < end; ++index) {
        const CorrespondenceItem& item = items_[index];
        const CorrespondenceCardLayout layout = card_layout(index, fonts);
        const Rect row = layout.card;
        const double visible_height = row.height;
        const bool selected = item.stable_id == selected_id_;
        const bool active = focused_ && item.stable_id == focused_id_;
        const bool pinned = item.stable_id == pinned_id_;
        painter.save();
        painter.clip_rect(row);
        const bool hovered = item.stable_id == hovered_id_;
        AnatomyStateSet item_states = states;
        if (hovered) item_states.insert(AnatomyState::hot);
        if (selected) item_states.insert(AnatomyState::selected);
        if (!item.enabled) item_states.insert(AnatomyState::disabled);
        const Rect card_bounds{row.x, row.y, row.width, visible_height};
        PartPaint card;
        card.family = ControlVisualRole::card;
        card.role = PartRole::face;
        card.bounds = card_bounds;
        card.states = item_states;
        if (overridden) card.authored_background = style().paper;
        paint_part(painter, *this, card);
        if (selected || hovered) {
            PartPaint plate;
            plate.family = ControlVisualRole::selection;
            plate.role = PartRole::selection;
            plate.bounds = card_bounds;
            plate.states = item_states;
            if (overridden) plate.authored_background = selected ? style().accent_light : style().face_light;
            paint_part(painter, *this, plate);
        }
        // The status rail is a fill; pinned and stale items take the first
        // and second accent indices and an offline item is unavailable
        // (owner decision, 2026-11-02).
        std::optional<AccentIndex> status_accent{};
        if (pinned) {
            status_accent = AccentIndex{0U};
        } else if (item.stale && !item.unavailable) {
            status_accent = AccentIndex{1U};
        }
        AnatomyStateSet status_states = item_states;
        if (item.unavailable) status_states.insert(AnatomyState::unavailable);
        PartPaint rail;
        rail.family = ControlVisualRole::card;
        rail.role = PartRole::fill;
        rail.bounds = layout.status_rail;
        rail.states = status_states;
        rail.accent = status_accent;
        paint_part(painter, *this, rail);
        paint_glyph(painter, layout.glyph, item.glyph, item.enabled);
        PartPaint content;
        content.family = selected || hovered ? ControlVisualRole::selection : ControlVisualRole::editor;
        content.role = PartRole::content;
        content.states = item_states;
        // PartPaint::text borrows: each elided line is held in a named
        // string that outlives its part's paint.
        const std::string title = elide_text_end(painter, item.title, fonts.title, layout.title_width);
        content.text = title;
        content.font = fonts.title;
        paint_text_part_at(painter, *this, content, layout.title_origin, layout.title_width);
        PartPaint secondary = content;
        secondary.tone = PartTextTone::secondary;
        secondary.states = status_states;
        const std::string secondary_line =
            elide_text_end(painter, item.secondary_text, fonts.secondary, layout.secondary_width);
        secondary.text = secondary_line;
        secondary.font = fonts.secondary;
        paint_text_part_at(painter, *this, secondary, layout.secondary_origin, layout.secondary_width);
        content.text = item.metric;
        content.font = fonts.metric;
        paint_text_part_at(painter, *this, content, layout.metric_origin,
                           std::max(0.0, row.x + row.width - layout.metric_origin.x));
        if (!layout.state_text.empty()) {
            // The state word is a label on a chip in the rail's accent.
            const FontSpec& word_font = fonts.evidence_bold;
            const Point word_origin = layout.state_origin;
            const Size word = painter.measure_text_utf8(layout.state_text, word_font);
            PartPaint chip;
            chip.family = ControlVisualRole::card;
            chip.role = PartRole::chip;
            chip.bounds = {word_origin.x - 3.0, word_origin.y - word_font.size - 1.0,
                           word.width + 6.0, word_font.size + 5.0};
            chip.states = status_states;
            chip.accent = status_accent;
            paint_part(painter, *this, chip);
            PartPaint word_label;
            word_label.family = ControlVisualRole::card;
            word_label.role = PartRole::label;
            word_label.states = status_states;
            word_label.text = layout.state_text;
            word_label.font = word_font;
            word_label.measured_text = word;
            paint_text_part_at(painter, *this, word_label, word_origin, word.width);
        }
        if (layout.expanded) {
            const double scale = effective_text_scale();
            const double body_top = layout.body_top;
            PartPaint rule;
            rule.family = ControlVisualRole::card;
            rule.role = PartRole::divider;
            rule.bounds = {row.x + 48.0, body_top - 0.5, std::max(0.0, row.width - 56.0), 1.0};
            rule.states = item_states;
            paint_part(painter, *this, rule);
            const double evidence_x = layout.evidence_x;
            const Rect excerpt_box = layout.excerpt_box;
            content.text = item.metadata;
            content.font = fonts.evidence;
            paint_text_part_at(painter, *this, content, {evidence_x, body_top + 16.0 * scale},
                               excerpt_box.width);
            // The excerpt sits in a well, unavailable with an unavailable item.
            PartPaint excerpt_well;
            excerpt_well.family = ControlVisualRole::editor;
            excerpt_well.role = PartRole::well;
            excerpt_well.bounds = excerpt_box;
            excerpt_well.states = item_states;
            if (item.unavailable) excerpt_well.states.insert(AnatomyState::unavailable);
            paint_part(painter, *this, excerpt_well);
            PartPaint quiet = content;
            quiet.family = ControlVisualRole::editor;
            quiet.tone = PartTextTone::secondary;
            quiet.states = status_states;
            quiet.text = "MATCHED EXCERPT";
            quiet.font = fonts.evidence_bold;
            paint_text_part_at(painter, *this, quiet,
                               {excerpt_box.x + 6.0, excerpt_box.y + 15.0 * scale},
                               std::max(0.0, excerpt_box.width - 12.0));
            PartPaint excerpt = content;
            excerpt.family = ControlVisualRole::editor;
            excerpt.states = status_states;
            excerpt.font = fonts.evidence;
            paint_emphasized_text(
                painter, *this,
                {excerpt_box.x + 6.0, excerpt_box.y + 31.0 * scale},
                item.excerpt, item.emphasis_terms, excerpt);
            std::string tags;
            for (const std::string& tag : item.information_tags) {
                if (!tags.empty()) tags += "  ·  ";
                tags += tag;
            }
            if (layout.wide) {
                const double lane_x = row.x + row.width - layout.lane_width;
                PartPaint lane_rule;
                lane_rule.family = ControlVisualRole::card;
                lane_rule.role = PartRole::divider;
                lane_rule.bounds = {lane_x - 0.5, body_top + 7.0 * scale, 1.0,
                                    std::max(0.0, row.y + visible_height - 14.0 * scale - body_top)};
                lane_rule.states = item_states;
                paint_part(painter, *this, lane_rule);
                PartPaint heading;
                heading.family = ControlVisualRole::card;
                heading.role = PartRole::label;
                heading.states = item_states;
                heading.text = item.information_heading;
                heading.font = fonts.evidence_bold;
                paint_text_part_at(painter, *this, heading,
                                   {lane_x + 9.0 * scale, body_top + 18.0 * scale},
                                   std::max(0.0, layout.lane_width - 12.0 * scale));
                PartPaint lane_text = heading;
                lane_text.tone = PartTextTone::secondary;
                lane_text.font = fonts.evidence;
                lane_text.text = tags;
                paint_text_part_at(painter, *this, lane_text,
                                   {lane_x + 9.0 * scale, body_top + 36.0 * scale},
                                   std::max(0.0, layout.lane_width - 12.0 * scale));
                lane_text.text = item.information_detail;
                paint_text_part_at(painter, *this, lane_text,
                                   {lane_x + 9.0 * scale, body_top + 54.0 * scale},
                                   std::max(0.0, layout.lane_width - 12.0 * scale));
            } else if (layout.footer_shown) {
                const std::string summary = item.information_heading + " · " + tags;
                PartPaint lane_text;
                lane_text.family = ControlVisualRole::card;
                lane_text.role = PartRole::label;
                lane_text.tone = PartTextTone::secondary;
                lane_text.states = item_states;
                lane_text.text = summary;
                lane_text.font = fonts.evidence;
                paint_text_part_at(painter, *this, lane_text, layout.footer_origin,
                                   std::max(0.0, row.x + row.width - layout.footer_origin.x));
            }
        }
        if (active) {
            PartPaint ring;
            ring.family = ControlVisualRole::selection;
            ring.role = PartRole::focus;
            ring.bounds = {row.x + 6.0, row.y + 2.0, std::max(0.0, row.width - 8.0),
                           std::max(0.0, visible_height - 4.0)};
            ring.states = item_states;
            ring.states.insert(AnatomyState::focus_visible);
            paint_part(painter, *this, ring);
        }
        painter.restore();
    }
    painter.restore();
}

void CorrespondenceView::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    if (event.action == PointerAction::move ||
        event.action == PointerAction::enter) {
        set_hovered_index(index_at(event.position));
        return;
    }
    if (event.action == PointerAction::leave) {
        set_hovered_index({});
        return;
    }
    if (event.action == PointerAction::wheel && event.wheel_delta.y != 0.0) {
        const double direction = event.wheel_delta.y > 0.0 ? -1.0 : 1.0;
        set_scroll_offset(std::clamp(
            scroll_offset_ + direction * scaled_compact_height() * 2.0,
            0.0, maximum_scroll_offset()));
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::down &&
        (event.button == PointerButton::primary ||
         event.button == PointerButton::secondary)) {
        pressed_index_ = index_at(event.position);
        pressed_button_ = event.button;
        pressed_click_count_ = event.click_count;
        if (window()) {
            static_cast<void>((*window()).request_focus(shared_from_this()));
        }
        if (pressed_index_) {
            set_selected_id(items_[*pressed_index_].stable_id);
            focus_index(*pressed_index_,
                        CorrespondenceExpansionReason::keyboard_focus);
        }
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::up &&
        event.button == pressed_button_ && pressed_button_ != PointerButton::none) {
        const std::optional<std::size_t> released = index_at(event.position);
        if (released && pressed_index_ == released) {
            const CorrespondenceItem& item = items_[*released];
            if (pressed_button_ == PointerButton::secondary) {
                context_requested_.emit({item.stable_id, event.position});
            } else if (pressed_click_count_ >= 2U) {
                emit_item_activated(item.stable_id);
            } else {
                set_pinned_id(item.stable_id == pinned_id_ ? std::string_view{}
                                                           : item.stable_id);
            }
        } else if (!released && !pressed_index_) {
            set_selected_id({});
            if (pressed_button_ == PointerButton::secondary && is_alive()) {
                context_requested_.emit({{}, event.position});
            }
        }
        pressed_index_.reset();
        pressed_button_ = PointerButton::none;
        event.handled = true;
    }
}

void CorrespondenceView::on_key(KeyEvent& event) {
    if (!focused_ || !enabled.type() || event.action != KeyAction::down ||
        items_.empty()) return;
    std::size_t index = item_index(focused_id_)
        .value_or(item_index(selected_id_).value_or(0U));
    if (event.physical_key == PhysicalKey::enter) {
        if (items_[index].enabled) emit_item_activated(items_[index].stable_id);
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::space) {
        set_pinned_id(items_[index].stable_id == pinned_id_
            ? std::string_view{} : std::string_view(items_[index].stable_id));
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::up) {
        if (index > 0U) --index;
    } else if (event.physical_key == PhysicalKey::down) {
        index = std::min(index + 1U, items_.size() - 1U);
    } else if (event.physical_key == PhysicalKey::home) {
        index = 0U;
    } else if (event.physical_key == PhysicalKey::end) {
        index = items_.size() - 1U;
    } else if (event.physical_key == PhysicalKey::page_up) {
        const std::size_t amount = std::max<std::size_t>(
            1U, static_cast<std::size_t>(viewport_height() /
                std::max(1.0, scaled_compact_height())));
        index = index > amount ? index - amount : 0U;
    } else if (event.physical_key == PhysicalKey::page_down) {
        const std::size_t amount = std::max<std::size_t>(
            1U, static_cast<std::size_t>(viewport_height() /
                std::max(1.0, scaled_compact_height())));
        index = std::min(index + amount, items_.size() - 1U);
    } else {
        return;
    }
    focus_index(index, CorrespondenceExpansionReason::keyboard_focus);
    set_selected_id(items_[index].stable_id);
    event.handled = true;
}

void CorrespondenceView::on_focus_changed(bool focused) {
    const std::string active = focused_id_.empty() && !items_.empty()
        ? (selected_id_.empty() ? items_.front().stable_id : selected_id_)
        : focused_id_;
    const bool before = !active.empty() && expanded(active);
    const ScrollAnchor anchor = capture_anchor(
        item_index(active).value_or(first_visible_index()));
    focused_ = focused;
    if (focused_ && focused_id_.empty()) focused_id_ = active;
    restore_anchor(anchor);
    if (focused_) {
        if (const std::optional<std::size_t> index = item_index(focused_id_)) ensure_visible(*index);
    }
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
    if (!active.empty()) {
        emit_expansion_delta(active, before, expanded(active),
                             CorrespondenceExpansionReason::keyboard_focus);
    }
}

// A list, not a table: a record shows its facts as lines, not in columns
// (owner decision, stage A gap 3; a view that shows real columns would be a
// table).
SemanticDescriptor CorrespondenceView::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::list;
    descriptor.name = accessible_name.type();
    descriptor.description = accessible_description.type();
    descriptor.value = selected_id_;
    descriptor.numeric_value = scroll_offset_;
    descriptor.minimum_value = 0.0;
    descriptor.maximum_value = maximum_scroll_offset();
    descriptor.actions = {SemanticAction::focus, SemanticAction::increment,
                          SemanticAction::decrement};
    descriptor.exposed = true;
    return descriptor;
}

void CorrespondenceView::add_semantic_text_child(
    SemanticNode& node, std::string suffix,
    std::string name, std::string value, SemanticRole role) {
    SemanticNode child;
    child.stable_id = node.stable_id + std::move(suffix);
    child.runtime_id = virtual_semantic_runtime_id(child.stable_id);
    child.role = role;
    child.name = std::move(name);
    child.value = std::move(value);
    child.bounds = node.bounds;
    child.states = SemanticState::visible;
    node.children.push_back(std::move(child));
}

std::vector<SemanticNode>
CorrespondenceView::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes;
    const std::pair<std::size_t, std::size_t> range = realized_range();
    const std::size_t first = range.first;
    const std::size_t end = range.second;
    nodes.reserve(end - first);
    const Rect absolute = absolute_bounds();
    for (std::size_t index = first; index < end; ++index) {
        const CorrespondenceItem& item = items_[index];
        const Rect local = item_bounds(index);
        SemanticNode node;
        node.stable_id = item.stable_id;
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::list_item;
        node.name = item.title;
        node.value = item.metric;
        node.description = item.secondary_text;
        if (item.unavailable) node.description += " · source unavailable";
        if (item.stale) node.description += " · stale metadata";
        if (item.stable_id == pinned_id_) node.description += " · pinned";
        node.bounds = {absolute.x + local.x, absolute.y + local.y,
                       local.width, local.height};
        node.states = SemanticState::visible | SemanticState::focusable;
        if (effectively_enabled() && item.enabled) {
            node.states |= SemanticState::enabled;
        }
        if (item.stable_id == selected_id_) node.states |= SemanticState::selected;
        if (focused_ && item.stable_id == focused_id_) {
            node.states |= SemanticState::focused;
        }
        const bool is_expanded = expanded_index(index);
        if (is_expanded) node.states |= SemanticState::expanded;
        node.actions = {SemanticAction::focus, SemanticAction::select,
                        SemanticAction::press, SemanticAction::show_menu,
                        is_expanded ? SemanticAction::collapse
                                    : SemanticAction::expand};
        SemanticNode activate;
        activate.stable_id = node.stable_id + ".activate";
        activate.runtime_id = virtual_semantic_runtime_id(activate.stable_id);
        activate.role = SemanticRole::button;
        activate.name = "Open " + item.title;
        activate.description = item.unavailable
            ? "Source unavailable" : "Activate this correspondence";
        activate.bounds = node.bounds;
        activate.states = SemanticState::visible | SemanticState::focusable;
        if (effectively_enabled() && item.enabled) {
            activate.states |= SemanticState::enabled;
        }
        activate.actions = {SemanticAction::press};
        node.children.push_back(std::move(activate));
        if (is_expanded) {
            add_semantic_text_child(
                node, ".percentage", "Match evidence", item.metric);
            add_semantic_text_child(
                node, ".metadata", "Why matched", item.metadata);
            add_semantic_text_child(
                node, ".excerpt", "Matched excerpt", item.excerpt);
            std::string information = item.information_detail;
            for (const std::string& tag : item.information_tags) {
                if (!information.empty()) information += " · ";
                information += tag;
            }
            add_semantic_text_child(
                node, ".plugins", item.information_heading,
                std::move(information), SemanticRole::group);
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

// Emits item_activated with a copy of the ID: a handler may replace the
// items (the Gallery's ledger does), which would free the item's own string
// while the handler still reads it.
void CorrespondenceView::emit_item_activated(const std::string& stable_id) {
    const std::string activated_id = stable_id;
    item_activated_.emit(activated_id);
}

bool CorrespondenceView::on_semantic_child_action(
    std::string_view stable_id, SemanticAction action, std::string_view) {
    constexpr std::string_view activate_suffix = ".activate";
    if (stable_id.ends_with(activate_suffix) &&
        action == SemanticAction::press) {
        const std::string_view item_id = stable_id.substr(
            0U, stable_id.size() - activate_suffix.size());
        const std::optional<std::size_t> activate_index = item_index(item_id);
        if (!activate_index) return false;
        if (window()) {
            static_cast<void>((*window()).request_focus(shared_from_this()));
        }
        focus_index(*activate_index,
                    CorrespondenceExpansionReason::keyboard_focus);
        set_selected_id(item_id);
        // A selection handler may have replaced the items: look the item up
        // again rather than reading items_ at the old index.
        const std::optional<std::size_t> activated = item_index(item_id);
        if (activated && items_[*activated].enabled) {
            emit_item_activated(items_[*activated].stable_id);
        }
        return true;
    }
    const std::optional<std::size_t> index = item_index(stable_id);
    if (!index || (action != SemanticAction::focus &&
                   action != SemanticAction::select &&
                   action != SemanticAction::press &&
                   action != SemanticAction::show_menu &&
                   action != SemanticAction::expand &&
                   action != SemanticAction::collapse)) return false;
    if (window()) static_cast<void>((*window()).request_focus(shared_from_this()));
    focus_index(*index, CorrespondenceExpansionReason::keyboard_focus);
    // Focus needs nothing more; every other action was refused above.
    if (action == SemanticAction::select) {
        set_selected_id(stable_id);
    } else if (action == SemanticAction::press) {
        set_selected_id(stable_id);
        // As above: the selection handler may have replaced the items.
        const std::optional<std::size_t> activated = item_index(stable_id);
        if (activated) emit_item_activated(items_[*activated].stable_id);
    } else if (action == SemanticAction::show_menu) {
        set_selected_id(stable_id);
        const Rect local = item_bounds(*index);
        const Rect absolute = absolute_bounds();
        context_requested_.emit({std::string(stable_id),
            {absolute.x + local.x + local.width * .5,
             absolute.y + local.y + std::min(local.height, viewport_height()) * .5}});
    } else if (action == SemanticAction::expand) {
        set_pinned_id(stable_id);
    } else if (action == SemanticAction::collapse) {
        if (pinned_id_ == stable_id) set_pinned_id({});
        if (hover_expanded_id_ == stable_id) clear_hover_intent(true);
    }
    return true;
}

void CorrespondenceView::on_detached_from_window() noexcept {
    hover_timer_.disconnect();
    pending_hover_id_.clear();
    Panel::on_detached_from_window();
}



namespace {

constexpr std::array<PartField<CorrespondenceView::Parts>, 2> correspondence_view_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &CorrespondenceView::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &CorrespondenceView::Parts::edge},
}};

constexpr std::array<PartField<CorrespondenceView::ItemParts>, 8> correspondence_view_item_part_fields{{
    {.declaration = {.name = "card", .role = PartRole::face, .composed_from = {}},
     .member = &CorrespondenceView::ItemParts::card},
    {.declaration = {.name = "status_rail", .role = PartRole::keyline, .composed_from = {}},
     .member = &CorrespondenceView::ItemParts::status_rail},
    {.declaration = {.name = "glyph", .role = PartRole::icon, .composed_from = {}},
     .member = &CorrespondenceView::ItemParts::glyph},
    {.declaration = {.name = "title", .role = PartRole::label, .composed_from = {}},
     .member = &CorrespondenceView::ItemParts::title},
    {.declaration = {.name = "secondary", .role = PartRole::content, .composed_from = {}},
     .member = &CorrespondenceView::ItemParts::secondary},
    {.declaration = {.name = "metric", .role = PartRole::content, .composed_from = {}},
     .member = &CorrespondenceView::ItemParts::metric},
    {.declaration = {.name = "evidence", .role = PartRole::well, .composed_from = {}},
     .member = &CorrespondenceView::ItemParts::evidence},
    {.declaration = {.name = "card_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &CorrespondenceView::ItemParts::card_focus},
}};

constexpr std::array<StateField<CorrespondenceView::ItemStates>, 6> correspondence_view_item_state_fields{{
    {.state = AnatomyState::selected, .member = &CorrespondenceView::ItemStates::selected},
    {.state = AnatomyState::hot, .member = &CorrespondenceView::ItemStates::hot},
    {.state = AnatomyState::current, .member = &CorrespondenceView::ItemStates::current},
    {.state = AnatomyState::open, .member = &CorrespondenceView::ItemStates::open},
    {.state = AnatomyState::unavailable, .member = &CorrespondenceView::ItemStates::unavailable},
    {.state = AnatomyState::disabled, .member = &CorrespondenceView::ItemStates::disabled},
}};

constexpr std::array<PartDeclaration, 8> correspondence_view_item_parts =
    part_declarations(correspondence_view_item_part_fields);
constexpr std::array<AnatomyState, 6> correspondence_view_item_states =
    state_declarations(correspondence_view_item_state_fields);
constexpr std::array<CustomStateDeclaration, 2> correspondence_view_item_custom_states{{
    {.name = "pinned"},
    {.name = "stale"},
}};
constexpr CustomStateIndex pinned_item_state{0U};
constexpr CustomStateIndex stale_item_state{1U};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 2>
    correspondence_view_parts = joined_declarations(scrollable_detail::parts,
                                         part_declarations(correspondence_view_extra_part_fields));

constexpr std::array<ItemSystemDeclaration, 1> correspondence_view_item_systems{{
    {.name = "item", .parts = correspondence_view_item_parts, .states = correspondence_view_item_states,
     .custom_states = correspondence_view_item_custom_states},
}};

constexpr ControlAnatomy correspondence_view_anatomy{
    .control_id = "correspondence_view",
    .parts = correspondence_view_parts,
    .states = scrollable_detail::states,
    .custom_states = {},
    .item_systems = correspondence_view_item_systems,
};

} // namespace

const ControlAnatomy& CorrespondenceView::anatomy() const noexcept {
    return correspondence_view_anatomy;
}

void CorrespondenceView::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.scrollable = scrollable_parts();

    const Panel::Parts panel = panel_parts();
    record.face = panel.face;
    record.edge = panel.edge;
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), correspondence_view_extra_part_fields, record, geometry);
}

CorrespondenceView::Parts CorrespondenceView::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), correspondence_view_extra_part_fields, published.parts(), record);
    return record;
}

std::size_t CorrespondenceView::realized_item_count() const {
    const std::pair<std::size_t, std::size_t> range = realized_range();
    return range.second - range.first;
}

std::string CorrespondenceView::realized_item_stable_id(std::size_t item) const {
    return items_[realized_range().first + item].stable_id;
}

void CorrespondenceView::publish_realized_item_geometry(std::size_t item,
                                          std::span<PartGeometry> geometry) const {
    ItemParts record;

    // Text is published as the slots on_paint draws into.
    const std::size_t index = realized_range().first + item;
    const Rect bounds = local_bounds();
    const Rect clip{2.0, 2.0, std::max(0.0, bounds.width - 4.0),
                    std::max(0.0, bounds.height - 4.0)};
    const CorrespondenceCardFonts fonts = card_fonts();
    const CorrespondenceCardLayout layout = card_layout(index, fonts);
    record.card = PartGeometry::from_bounds(Rect::intersection(layout.card, clip));
    record.status_rail = PartGeometry::from_bounds(Rect::intersection(layout.status_rail, clip));
    record.glyph = PartGeometry::from_bounds(Rect::intersection(layout.glyph, clip));
    record.title = PartGeometry::from_bounds(Rect::intersection(
        {layout.title_origin.x, layout.card.y + 4.0, layout.title_width, 17.0}, clip));
    record.secondary = PartGeometry::from_bounds(Rect::intersection(
        {layout.secondary_origin.x, layout.card.y + 22.0, layout.secondary_width, 15.0}, clip));
    record.metric = PartGeometry::from_bounds(Rect::intersection(layout.metric_box, clip));
    // The excerpt box of the expanded evidence body.
    if (layout.expanded) {
        record.evidence = PartGeometry::from_bounds(Rect::intersection(layout.excerpt_box, clip));
    }
    record.card_focus = record.card;
    write_item_parts(anatomy().item_systems.front(), correspondence_view_item_part_fields, record, geometry);
}

AnatomyStateSet CorrespondenceView::realized_item_anatomy_states(std::size_t item) const {
    AnatomyStateSet states;
    const std::size_t index = realized_range().first + item;
    const CorrespondenceItem& entry = items_[index];
    if (entry.stable_id == selected_id_) states.insert(AnatomyState::selected);
    if (entry.stable_id == hovered_id_) states.insert(AnatomyState::hot);
    if (entry.stable_id == focused_id_) states.insert(AnatomyState::current);
    if (expanded_index(index)) states.insert(AnatomyState::open);
    if (entry.unavailable) states.insert(AnatomyState::unavailable);
    if (!entry.enabled) states.insert(AnatomyState::disabled);
    if (entry.stable_id == pinned_id_) states.insert(pinned_item_state);
    if (entry.stale) states.insert(stale_item_state);
    return states;
}

std::vector<CorrespondenceView::Item> CorrespondenceView::realized_items() const {
    const std::size_t count = realized_item_count();
    std::vector<Item> records;
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Item record;
        record.index = realized_range().first + item;
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems.front(), correspondence_view_item_part_fields, published.parts(), record.parts);
        const AnatomyStateSet item_states = realized_item_anatomy_states(item);
        read_states(correspondence_view_item_state_fields, item_states, record.states);
        record.states.pinned = item_states.contains(pinned_item_state);
        record.states.stale = item_states.contains(stale_item_state);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events CorrespondenceView adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 5> correspondence_view_interactions{{
    {.name = "selection_changed", .payload = "CorrespondenceSelectionChange", .mutable_payload = false},
    {.name = "pin_changed", .payload = "CorrespondencePinChange", .mutable_payload = false},
    {.name = "expansion_changed", .payload = "CorrespondenceExpansionChange", .mutable_payload = false},
    {.name = "item_activated", .payload = "std::string", .mutable_payload = false},
    {.name = "context_requested", .payload = "ObjectContextRequest", .mutable_payload = false},
}};

} // namespace

void CorrespondenceView::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), correspondence_view_interactions.begin(), correspondence_view_interactions.end());
}

} // namespace gui_forms
