#include "gui_forms/controls/panel/breadcrumb_trail/breadcrumb_trail.hpp"

#include "../collection_control_utilities.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <vector>
#include <string>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gui_forms {

using namespace collection_detail;

namespace {
// Non-owning spans are used only during this call; indices address widths.
double breadcrumb_occupied_width(const std::span<const std::size_t> indices,
                                  const std::span<const double> widths,
                                  const bool overflow, const double overlap) {
    double total = 0.0;
    bool first = true;
    for (const std::size_t index : indices) {
        const double shared_edge = first ? 0.0 : overlap;
        total += widths[index] - shared_edge;
        first = false;
    }
    if (overflow) total += 42.0 - (first ? 0.0 : overlap);
    return total;
}
} // namespace

BreadcrumbTrail::BreadcrumbTrail(StableId stable_id,
                                 std::string editor_stable_id)
    : Panel(std::move(stable_id)),
      editor_stable_id_(std::move(editor_stable_id)) {
    const std::string prefix((*this).stable_id().value());
    if (editor_stable_id_.empty()) editor_stable_id_ = prefix + ".editor";
    overflow_stable_id_ = prefix + ".overflow";
    edit_stable_id_ = prefix + ".edit";
    set_paint_plane(PaintPlane::control);
    set_background(style().paper);
    set_border_style(BorderStyle::sunken);
    set_focusable(true);
}

void BreadcrumbTrail::initialize_control_tree() {
    if (editor_) return;
    editor_ = make_control<TextBox>(StableId(editor_stable_id_));
    (*editor_).set_border_style(BorderStyle::none);
    (*editor_).set_placeholder_text("Enter an exact path");
    (*editor_).accessible_name = "Exact location path";
    // Enter commits the path (editor_committed) and Escape ends the edit
    // (editor_cancelled); neither goes further.
    (*editor_).set_accepts_enter(true);
    (*editor_).set_accepts_escape(true);
    (*editor_).visible = false;
    (*editor_).set_font(font_);
    add_child(editor_);
    const std::weak_ptr<BreadcrumbTrail> weak =
        std::static_pointer_cast<BreadcrumbTrail>(shared_from_this());
    // The trail owns the editor and tokens. Callbacks observe the trail weakly;
    // locking keeps it alive for one invocation and expired targets do nothing.
    editor_commit_ = (*editor_).committed().subscribe(
        *this, EditorCommitCallback{weak});
    editor_cancel_ = (*editor_).cancelled().subscribe(
        *this, EditorCancelCallback{weak});
}

void BreadcrumbTrail::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) throw std::invalid_argument("BreadcrumbTrail font is invalid");
    set_font_from_code(true);
    if (font_ == font) return;
    font_ = font;
    if (editor_) (*editor_).set_font(font);
    rebuild_layout(committed_arranged_bounds().width, committed_arranged_bounds().height);
    normalize_active();
    invalidate(Dirty::measure | Dirty::layout | Dirty::paint | Dirty::hit_test |
               Dirty::semantics | Dirty::accessibility);
}

void BreadcrumbTrail::set_appearance(BreadcrumbAppearance appearance) {
    require_mutable();
    if (appearance != BreadcrumbAppearance::plain && appearance != BreadcrumbAppearance::raised) {
        throw std::invalid_argument("BreadcrumbTrail appearance is invalid");
    }
    if (appearance_ == appearance) return;
    appearance_ = appearance;
    rebuild_layout(committed_arranged_bounds().width, committed_arranged_bounds().height);
    normalize_active();
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void BreadcrumbTrail::EditorCommitCallback::operator()(
    const std::string& text) const {
    if (const std::shared_ptr<BreadcrumbTrail> trail = target.lock()) {
        (*trail).editor_committed(text);
    }
}

void BreadcrumbTrail::EditorCancelCallback::operator()() const {
    if (const std::shared_ptr<BreadcrumbTrail> trail = target.lock()) {
        (*trail).editor_cancelled();
    }
}

void BreadcrumbTrail::editor_committed(std::string text) {
    publish_change(edit_committed_, text);
}

void BreadcrumbTrail::editor_cancelled() {
    set_editing(false);
    if (window()) {
        static_cast<void>((*window()).request_focus(shared_from_this()));
    }
    publish_change(edit_cancelled_);
}

void BreadcrumbTrail::set_segments(std::vector<BreadcrumbSegment> segments) {
    require_mutable();
    std::unordered_set<std::string> identities{};
    for (const BreadcrumbSegment& segment : segments) {
        validate_identity_text(segment.stable_id, segment.text);
        const std::pair<std::unordered_set<std::string>::iterator, bool> insertion =
            identities.insert(segment.stable_id);
        if (!validate_utf8(segment.description).valid() ||
            segment.description.size() > 4096U ||
            segment.stable_id == overflow_stable_id_ ||
            segment.stable_id == edit_stable_id_ ||
            !insertion.second) {
            throw std::invalid_argument(
                "BreadcrumbTrail requires unique segment IDs, valid text, and bounded descriptions");
        }
    }
    const std::string retained_active = active_id_;
    segments_ = std::move(segments);
    active_id_ = segment_index(retained_active) ? retained_active : std::string{};
    rebuild_layout(committed_arranged_bounds().width,
                   committed_arranged_bounds().height);
    normalize_active();
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test |
               Dirty::semantics | Dirty::accessibility);
}

std::optional<std::size_t> BreadcrumbTrail::segment_index(
    std::string_view stable_id) const noexcept {
    const std::size_t count = segments_.size();
    for (std::size_t index = 0U; index < count; ++index) {
        if (segments_[index].stable_id == stable_id) {
            const std::optional<std::size_t> result(index);
            return result;
        }
    }
    return {};
}

void BreadcrumbTrail::set_active_id(std::string_view stable_id) {
    require_mutable();
    if (!stable_id.empty() && stable_id != overflow_stable_id_ &&
        stable_id != edit_stable_id_ && !segment_index(stable_id)) {
        throw std::out_of_range("BreadcrumbTrail active ID is not in its model");
    }
    if (active_id_ == stable_id) return;
    active_id_ = stable_id;
    normalize_active();
    invalidate(Dirty::paint | Dirty::semantics | Dirty::accessibility);
}

void BreadcrumbTrail::set_editing(bool editing) {
    require_mutable();
    if (editing_ == editing) return;
    editing_ = editing;
    if (!editing_) tab_completion_available_ = false;
    if (editor_) (*editor_).visible = editing_;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics |
               Dirty::accessibility);
}

void BreadcrumbTrail::set_tab_completion_available(bool available) {
    require_mutable();
    available = available && editing_;
    if (tab_completion_available_ == available) return;
    tab_completion_available_ = available;
    invalidate(Dirty::semantics | Dirty::accessibility);
}

void BreadcrumbTrail::begin_edit(std::string text, bool select_all) {
    require_mutable();
    if (!editor_) {
        throw std::logic_error(
            "BreadcrumbTrail must be created through make_control before editing");
    }
    (*editor_).set_text(std::move(text));
    set_editing(true);
    if (select_all) (*editor_).select_all();
    if (window()) static_cast<void>((*window()).request_focus(editor_));
    publish_change(edit_started_, std::string((*editor_).text()));
}

double BreadcrumbTrail::natural_width(
    const BreadcrumbSegment& segment) const {
    FontSpec font = text_font(font_);
    if (!segments_.empty() && &segment == &segments_.back()) {
        font.weight = 700;
    }
    const double text_width =
        resolve_text_layout_utf8(segment.text, font).logical_size.width;
    const double padding = appearance_ == BreadcrumbAppearance::raised ? 33.0 : 23.0;
    const double result = std::clamp(padding + text_width, 38.0, 220.0);
    return result;
}

void BreadcrumbTrail::rebuild_layout(double width, double height) {
    visible_items_.clear();
    hidden_segment_ids_.clear();
    const std::size_t segment_count_bound = segments_.size();
    if (segment_count_bound > visible_items_.max_size() - 2U) {
        throw std::length_error("BreadcrumbTrail layout exceeds vector capacity");
    }
    visible_items_.reserve(segment_count_bound + 2U);
    hidden_segment_ids_.reserve(segment_count_bound);
    hovered_visible_.reset();
    pressed_visible_.reset();
    if (width <= 0.0 || height <= 0.0) return;

    const double inner_y = 1.0;
    const double inner_height = std::max(0.0, height - 2.0);
    const double edit_x = std::max(1.0, width - edit_width_ - 1.0);
    const double segment_extent = std::max(0.0, edit_x - 1.0);
    std::vector<std::size_t> visible_segments{};
    visible_segments.reserve(segments_.size());

    const std::size_t segment_count = segments_.size();
    std::vector<double> natural_widths(segment_count, 0.0);
    for (std::size_t index = 0U; index < segment_count; ++index) {
        natural_widths[index] = natural_width(segments_[index]);
    }
    // Temporary proposal storage is allocated once and reused in the suffix walk.
    std::vector<std::size_t> proposed{};
    proposed.reserve(segment_count);

    for (std::size_t index = 0; index < segments_.size(); ++index) {
        visible_segments.push_back(index);
    }
    bool overflow = breadcrumb_occupied_width(visible_segments, natural_widths, false, edge_overlap_) > segment_extent;
    if (overflow && segments_.size() > 2U) {
        visible_segments = {0U, segments_.size() - 1U};
        for (std::size_t candidate = segments_.size() - 1U;
             candidate > 1U; --candidate) {
            proposed = visible_segments;
            proposed.insert(proposed.begin() + 1,
                            candidate - 1U);
            if (breadcrumb_occupied_width(proposed, natural_widths, true, edge_overlap_) > segment_extent) break;
            // Both allocations retain their capacity for the next proposal.
            visible_segments.swap(proposed);
        }
        std::size_t visible_position = 0U;
        for (std::size_t index = 0U; index < segment_count; ++index) {
            if (visible_position < visible_segments.size() &&
                visible_segments[visible_position] == index) {
                ++visible_position;
            } else {
                hidden_segment_ids_.push_back(segments_[index].stable_id);
            }
        }
        overflow = !hidden_segment_ids_.empty();
    } else if (overflow) {
        // A minimum-width host can still preserve both authorities. Their
        // faces compress, but no path identity is silently removed.
        overflow = false;
    }

    double x = 1.0;
    for (std::size_t position = 0U; position < visible_segments.size(); ++position) {
        if (overflow && position == 1U) {
            const double remaining = std::max(0.0, edit_x - x);
            const double item_width = std::min(42.0, remaining + edge_overlap_);
            const VisibleItem item{.kind = VisibleKind::overflow, .segment_index = 0U,
                .bounds = {x, inner_y, item_width, inner_height}};
            visible_items_.push_back(item);
            x += std::max(0.0, item_width - edge_overlap_);
        }
        const std::size_t index = visible_segments[position];
        const double remaining = std::max(0.0, edit_x - x);
        const double item_width = std::min(natural_widths[index], remaining + edge_overlap_);
        const VisibleItem item{.kind = VisibleKind::segment, .segment_index = index,
            .bounds = {x, inner_y, item_width, inner_height}};
        visible_items_.push_back(item);
        x += std::max(0.0, item_width - edge_overlap_);
    }
    visible_items_.push_back(
        {VisibleKind::edit, 0U,
         {edit_x, inner_y, std::max(0.0, width - edit_x - 1.0), inner_height}});
}

void BreadcrumbTrail::normalize_active() {
    if (visible_items_.empty()) {
        active_id_.clear();
        return;
    }
    bool visible = false;
    for (const VisibleItem& item : visible_items_) {
        const std::string_view identity = visible_stable_id(item);
        if (identity == active_id_) {
            visible = true;
            break;
        }
    }
    if (visible) return;
    active_id_ = edit_stable_id_;
    for (std::size_t remaining = visible_items_.size(); remaining > 0U; --remaining) {
        const VisibleItem& item = visible_items_[remaining - 1U];
        if (item.kind == VisibleKind::segment) {
            active_id_ = segments_[item.segment_index].stable_id;
            break;
        }
    }
}

void BreadcrumbTrail::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    rebuild_layout(final_bounds.width, final_bounds.height);
    normalize_active();
    if (editor_) {
        set_child_layout(editor_,
            {1.0, 1.0, std::max(0.0, final_bounds.width - 2.0),
             std::max(0.0, final_bounds.height - 2.0)});
    }
}

std::string_view BreadcrumbTrail::visible_stable_id(
    const VisibleItem& item) const noexcept {
    if (item.kind == VisibleKind::overflow) return overflow_stable_id_;
    if (item.kind == VisibleKind::edit) return edit_stable_id_;
    return segments_[item.segment_index].stable_id;
}

void BreadcrumbTrail::on_paint(Painter& painter, Rect damage) {
    Panel::on_paint(painter, damage);
    if (editing_) return;
    if (appearance_ == BreadcrumbAppearance::raised) {
        paint_raised(painter);
        return;
    }
    const FontSpec font = text_font(font_);
    const double baseline = snap_text_baseline(std::max(
        font.size, committed_arranged_bounds().height * 0.5 + font.size * 0.34));
    painter.save();
    painter.clip_rect(local_bounds());
    // Each segment's face and label, the chevrons between segments and the
    // active segment's focus ring are parts the theme's engine draws. A
    // segment is a command button's face, hot under the pointer or while
    // active; the edit terminal is an accent button's. A disabled segment's
    // parts are disabled.
    AnatomyStateSet states = anatomy_states();
    states.erase(AnatomyState::hot);
    // Paint every overlapping face before its shared chevron. Drawing a
    // joint inside this loop lets the next face erase that joint because each
    // successor deliberately overlaps the preceding face by edge_overlap_.
    for (std::size_t index = 0; index < visible_items_.size(); ++index) {
        const VisibleItem& item = visible_items_[index];
        const bool active = focused_ && visible_stable_id(item) == active_id_;
        const bool hovered = hovered_visible_ == index;
        AnatomyStateSet item_states = states;
        if (hovered || active) item_states.insert(AnatomyState::hot);
        ControlVisualRole family = ControlVisualRole::command_button;
        std::string_view text{};
        if (item.kind == VisibleKind::edit) {
            family = ControlVisualRole::accent_button;
            text = "./";
        } else if (item.kind == VisibleKind::overflow) {
            text = "…";
        } else {
            const BreadcrumbSegment& segment = segments_[item.segment_index];
            if (!segment.enabled) item_states.insert(AnatomyState::disabled);
            text = segment.text;
        }
        PartPaint face;
        face.family = family;
        face.role = PartRole::face;
        face.bounds = item.bounds;
        face.states = item_states;
        face.corner_radius = 0.0;
        paint_part(painter, *this, face);
        FontSpec item_font = font;
        if (item.kind == VisibleKind::edit ||
            (item.kind == VisibleKind::segment &&
             item.segment_index + 1U == segments_.size())) {
            item_font.weight = 700;
        }
        const double text_width =
            painter.measure_text_utf8(text, item_font).width;
        const double text_x = item.kind == VisibleKind::segment
            ? item.bounds.x + 9.0
            : item.bounds.x + std::max(0.0,
                (item.bounds.width - text_width) * 0.5);
        PartPaint label;
        label.family = family;
        label.role = PartRole::label;
        label.states = item_states;
        label.text = text;
        label.font = item_font;
        paint_text_part_at(painter, *this, label, {text_x, baseline},
                           std::max(0.0, item.bounds.x + item.bounds.width - text_x));
    }

    // Chevrons and focus rings are the foreground geometry of the one
    // continuous location instrument, so no later face may cover them.
    for (std::size_t index = 0; index < visible_items_.size(); ++index) {
        const VisibleItem& item = visible_items_[index];
        const bool active = focused_ && visible_stable_id(item) == active_id_;
        if (index + 1U < visible_items_.size()) {
            // An 8-unit square on the overlap holds the chevron.
            const double edge_x = std::floor(item.bounds.x + item.bounds.width - edge_overlap_ * 0.5);
            const double middle = std::floor(item.bounds.y + item.bounds.height * 0.5);
            PartPaint chevron;
            chevron.family = ControlVisualRole::command_button;
            chevron.role = PartRole::disclosure;
            chevron.mark = PartMark::chevron_right;
            chevron.bounds = {edge_x - 4.0, middle - 4.0, 8.0, 8.0};
            chevron.states = states;
            paint_part(painter, *this, chevron);
        }
        if (active) {
            PartPaint ring;
            ring.family = ControlVisualRole::command_button;
            ring.role = PartRole::focus;
            ring.bounds = {item.bounds.x + 2.0, item.bounds.y + 2.0,
                           std::max(0.0, item.bounds.width - 4.0),
                           std::max(0.0, item.bounds.height - 4.0)};
            ring.states = states;
            ring.states.insert(AnatomyState::focus_visible);
            paint_part(painter, *this, ring);
        }
    }
    painter.restore();
}

// The raised trail: each segment is a button's pointed face (design
// language §8.5, owner decision 2026-11-02), its nose over the next
// segment; the current segment's face is selected, a segment under the
// pointer or active hot, a pressed one pressed and a disabled one
// disabled. The edit terminal is an accent button's square face. Labels
// and the active segment's focus ring are parts too. Faces are painted
// back to front, so each nose lies whole over its successor.
void BreadcrumbTrail::paint_raised(Painter& painter) {
    const FontSpec font = text_font(font_);
    const double baseline = snap_text_baseline(std::max(
        font.size, committed_arranged_bounds().height * 0.5 + font.size * 0.34));
    AnatomyStateSet states = anatomy_states();
    states.erase(AnatomyState::hot);
    painter.save();
    painter.clip_rect(local_bounds());
    for (std::size_t remaining = visible_items_.size(); remaining > 0U; --remaining) {
        const std::size_t index = remaining - 1U;
        const VisibleItem& item = visible_items_[index];
        const Rect bounds = item.bounds;
        if (bounds.width <= 0.0 || bounds.height <= 0.0) continue;
        const bool current = item.kind == VisibleKind::segment &&
            item.segment_index + 1U == segments_.size();
        const bool active = focused_ && visible_stable_id(item) == active_id_;
        const bool terminal = item.kind == VisibleKind::edit;
        const double tip = terminal ? 0.0 : raised_point(bounds);
        const double shoulder = bounds.x + bounds.width - tip;
        AnatomyStateSet item_states = states;
        if (hovered_visible_ == index || active) item_states.insert(AnatomyState::hot);
        if (pressed_visible_ == index) item_states.insert(AnatomyState::pressed);
        if (current) item_states.insert(AnatomyState::selected);
        std::string_view text = terminal ? "./" : "…";
        if (item.kind == VisibleKind::segment) {
            text = segments_[item.segment_index].text;
            if (!segments_[item.segment_index].enabled) item_states.insert(AnatomyState::disabled);
        }
        const ControlVisualRole family = terminal ? ControlVisualRole::accent_button : ControlVisualRole::button;
        PartPaint face;
        face.family = family;
        face.role = PartRole::face;
        face.bounds = bounds;
        face.states = item_states;
        face.corner_radius = 0.0;
        if (tip > 0.0) face.point = tip;
        paint_part(painter, *this, face);
        FontSpec item_font = font;
        if (current || terminal) item_font.weight = 700;
        const double left_padding = index == 0U || terminal ? 9.0 : edge_overlap_ + 5.0;
        const double text_x = bounds.x + left_padding;
        const Size measured = painter.measure_text_utf8(text, item_font);
        const double box_height = std::max(measured.height, item_font.size * 1.2);
        const double box_ascent = std::max(item_font.size, box_height * 0.82);
        PartPaint label;
        label.family = family;
        label.role = PartRole::label;
        label.states = item_states;
        label.text = text;
        label.font = item_font;
        label.bounds = {text_x, baseline - box_ascent, std::max(0.0, shoulder - text_x - 2.0), box_height};
        label.measured_text = measured;
        paint_part(painter, *this, label);
        if (active) {
            PartPaint ring;
            ring.family = family;
            ring.role = PartRole::focus;
            ring.bounds = {bounds.x + left_padding - 5.0, bounds.y,
                           std::max(0.0, shoulder - bounds.x - left_padding + 5.0), bounds.height};
            ring.states = item_states;
            ring.states.insert(AnatomyState::focus_visible);
            paint_part(painter, *this, ring);
        }
    }
    painter.restore();
}

// The nose a raised segment of `bounds` points with: the overlap, at most
// a quarter of the segment.
double BreadcrumbTrail::raised_point(Rect bounds) const noexcept {
    const double result = std::min(edge_overlap_, bounds.width * 0.25);
    return result;
}

std::optional<std::size_t> BreadcrumbTrail::visible_index_at(
    Point absolute) const noexcept {
    const Rect bounds = absolute_bounds();
    const Point local{absolute.x - bounds.x, absolute.y - bounds.y};
    for (std::size_t index = visible_items_.size(); index > 0U; --index) {
        const VisibleItem& item = visible_items_[index - 1U];
        if (!item.bounds.contains(local)) continue;
        // A raised segment is hit in its pointed face (the shape it is
        // painted in), less the preceding segment's nose over it.
        if (appearance_ == BreadcrumbAppearance::raised && item.kind != VisibleKind::edit) {
            if (!pointed_face_contains(item.bounds, raised_point(item.bounds), local)) continue;
            if (index > 1U) {
                const Rect previous = visible_items_[index - 2U].bounds;
                if (pointed_face_contains(previous, raised_point(previous), local)) continue;
            }
        }
        const std::optional<std::size_t> result(index - 1U);
        return result;
    }
    return {};
}

void BreadcrumbTrail::activate_visible(std::size_t index) {
    if (index >= visible_items_.size()) return;
    const VisibleItem& item = visible_items_[index];
    active_id_ = std::string(visible_stable_id(item));
    invalidate(Dirty::paint | Dirty::semantics | Dirty::accessibility);
    if (item.kind == VisibleKind::edit) {
        begin_edit(editor_ ? std::string((*editor_).text()) : std::string{});
    } else if (item.kind == VisibleKind::overflow) {
        publish_change(overflow_activated_);
    } else if (segments_[item.segment_index].enabled) {
        publish_change(segment_activated_,
                       segments_[item.segment_index].stable_id);
    }
}

void BreadcrumbTrail::on_pointer(PointerEvent& event) {
    if (!eligible_for_input() || editing_) return;
    if (event.action == PointerAction::move) {
        const std::optional<std::size_t> hovered = visible_index_at(event.position);
        if (hovered != hovered_visible_) {
            hovered_visible_ = hovered;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.action == PointerAction::leave) {
        hovered_visible_.reset();
        invalidate(Dirty::paint);
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down) {
        if (window()) static_cast<void>((*window()).request_focus(shared_from_this()));
        pressed_visible_ = visible_index_at(event.position);
        event.handled = pressed_visible_.has_value();
    } else if (event.action == PointerAction::up) {
        const std::optional<std::size_t> released = visible_index_at(event.position);
        if (released && released == pressed_visible_) activate_visible(*released);
        event.handled = released.has_value();
        pressed_visible_.reset();
    }
}

void BreadcrumbTrail::on_key_preview(KeyEvent& event) {
    if (!editing_ || !tab_completion_available_ || !enabled.type() ||
        event.action != KeyAction::down ||
        event.physical_key != PhysicalKey::tab) {
        return;
    }
    publish_change(edit_completion_requested_);
    event.handled = true;
}

void BreadcrumbTrail::on_key(KeyEvent& event) {
    if (!focused_ || !enabled.type() || editing_ ||
        event.action != KeyAction::down || visible_items_.empty()) return;
    std::size_t active{};
    for (std::size_t index = 0U; index < visible_items_.size(); ++index) {
        const std::string_view identity = visible_stable_id(visible_items_[index]);
        if (identity == active_id_) {
            active = index;
            break;
        }
    }
    if (event.physical_key == PhysicalKey::enter ||
        event.physical_key == PhysicalKey::space) {
        activate_visible(active);
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::left && active > 0U) --active;
    else if (event.physical_key == PhysicalKey::right &&
             active + 1U < visible_items_.size()) ++active;
    else if (event.physical_key == PhysicalKey::home) active = 0U;
    else if (event.physical_key == PhysicalKey::end) {
        active = visible_items_.size() - 1U;
    } else return;
    active_id_ = std::string(visible_stable_id(visible_items_[active]));
    invalidate(Dirty::paint | Dirty::semantics | Dirty::accessibility);
    event.handled = true;
}

void BreadcrumbTrail::on_focus_changed(bool focused) {
    focused_ = focused;
    normalize_active();
    invalidate(Dirty::paint | Dirty::semantics | Dirty::accessibility);
}

SemanticDescriptor BreadcrumbTrail::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.type();
    descriptor.value = editor_ ? std::string((*editor_).text()) : std::string{};
    descriptor.description = accessible_description.type();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    descriptor.include_descendants = true;
    return descriptor;
}

std::vector<SemanticNode> BreadcrumbTrail::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    if (editing_) return nodes;
    nodes.reserve(visible_items_.size());
    const Rect trail = absolute_bounds();
    for (const VisibleItem& item : visible_items_) {
        SemanticNode node{};
        node.stable_id = std::string(visible_stable_id(item));
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::button;
        node.bounds = {trail.x + item.bounds.x, trail.y + item.bounds.y,
                       item.bounds.width, item.bounds.height};
        node.states = SemanticState::visible | SemanticState::focusable;
        if (effectively_enabled()) node.states |= SemanticState::enabled;
        if (focused_ && node.stable_id == active_id_) {
            node.states |= SemanticState::focused;
        }
        node.actions = {SemanticAction::focus, SemanticAction::press};
        if (item.kind == VisibleKind::edit) {
            node.name = "Edit complete path";
            node.description =
                "Switch this breadcrumb to one inline exact-path editor";
        } else if (item.kind == VisibleKind::overflow) {
            node.name = "Hidden path segments";
            node.description = std::to_string(hidden_segment_ids_.size()) +
                " middle path segments";
            node.actions.push_back(SemanticAction::show_menu);
        } else {
            const BreadcrumbSegment& segment = segments_[item.segment_index];
            node.name = segment.text;
            node.description = segment.description;
            if (!segment.enabled) {
                node.states = SemanticState::visible | SemanticState::focusable;
            }
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool BreadcrumbTrail::on_semantic_child_action(
    std::string_view stable_id, SemanticAction action, std::string_view) {
    if (editing_) return false;
    std::size_t index = 0U;
    while (index < visible_items_.size()) {
        const std::string_view identity = visible_stable_id(visible_items_[index]);
        if (identity == stable_id) break;
        ++index;
    }
    if (index == visible_items_.size()) return false;
    if (action != SemanticAction::focus && action != SemanticAction::press &&
        action != SemanticAction::show_menu) return false;
    if (window()) static_cast<void>((*window()).request_focus(shared_from_this()));
    active_id_ = std::string(stable_id);
    if (action == SemanticAction::press || action == SemanticAction::show_menu) {
        activate_visible(index);
    } else {
        invalidate(Dirty::paint | Dirty::semantics | Dirty::accessibility);
    }
    return true;
}


namespace {

constexpr std::array<PartField<BreadcrumbTrail::Parts>, 2> breadcrumb_trail_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &BreadcrumbTrail::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &BreadcrumbTrail::Parts::edge},
}};

constexpr std::array<PartField<BreadcrumbTrail::ItemParts>, 4> breadcrumb_trail_item_part_fields{{
    {.declaration = {.name = "segment", .role = PartRole::face, .composed_from = "button"},
     .member = &BreadcrumbTrail::ItemParts::segment},
    {.declaration = {.name = "segment_label", .role = PartRole::label, .composed_from = {}},
     .member = &BreadcrumbTrail::ItemParts::segment_label},
    {.declaration = {.name = "segment_cap", .role = PartRole::cap, .composed_from = {}},
     .member = &BreadcrumbTrail::ItemParts::segment_cap},
    {.declaration = {.name = "segment_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &BreadcrumbTrail::ItemParts::segment_focus},
}};

constexpr std::array<StateField<BreadcrumbTrail::ItemStates>, 3> breadcrumb_trail_item_state_fields{{
    {.state = AnatomyState::hot, .member = &BreadcrumbTrail::ItemStates::hot},
    {.state = AnatomyState::current, .member = &BreadcrumbTrail::ItemStates::current},
    {.state = AnatomyState::disabled, .member = &BreadcrumbTrail::ItemStates::disabled},
}};

constexpr std::array<PartDeclaration, 4> breadcrumb_trail_item_parts =
    part_declarations(breadcrumb_trail_item_part_fields);
constexpr std::array<AnatomyState, 3> breadcrumb_trail_item_states =
    state_declarations(breadcrumb_trail_item_state_fields);

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 2>
    breadcrumb_trail_parts = joined_declarations(scrollable_detail::parts,
                                         part_declarations(breadcrumb_trail_extra_part_fields));

constexpr std::array<ItemSystemDeclaration, 1> breadcrumb_trail_item_systems{{
    {.name = "item", .parts = breadcrumb_trail_item_parts, .states = breadcrumb_trail_item_states,
     .custom_states = {}},
}};

constexpr ControlAnatomy breadcrumb_trail_anatomy{
    .control_id = "breadcrumb_trail",
    .parts = breadcrumb_trail_parts,
    .states = scrollable_detail::states,
    .custom_states = {},
    .item_systems = breadcrumb_trail_item_systems,
};

} // namespace

const ControlAnatomy& BreadcrumbTrail::anatomy() const noexcept {
    return breadcrumb_trail_anatomy;
}

void BreadcrumbTrail::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.scrollable = scrollable_parts();

    const Panel::Parts panel = panel_parts();
    record.face = panel.face;
    record.edge = panel.edge;
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), breadcrumb_trail_extra_part_fields, record, geometry);
}

BreadcrumbTrail::Parts BreadcrumbTrail::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), breadcrumb_trail_extra_part_fields, published.parts(), record);
    return record;
}

std::size_t BreadcrumbTrail::realized_item_count() const {
    if (editing_) return 0U;
    return visible_items_.size();
}

std::string BreadcrumbTrail::realized_item_stable_id(std::size_t item) const {
    const std::string stable_id(visible_stable_id(visible_items_[item]));
    return stable_id;
}

void BreadcrumbTrail::publish_realized_item_geometry(std::size_t item,
                                          std::span<PartGeometry> geometry) const {
    ItemParts record;

    const VisibleItem& visible = visible_items_[item];
    const Rect bounds = visible.bounds;
    FontSpec item_font = text_font(font_);
    std::string_view text{};
    if (visible.kind == VisibleKind::edit) {
        text = "./";
    } else if (visible.kind == VisibleKind::overflow) {
        text = "…";
    } else {
        text = segments_[visible.segment_index].text;
    }
    if (visible.kind == VisibleKind::edit ||
        (visible.kind == VisibleKind::segment &&
         visible.segment_index + 1U == segments_.size())) {
        item_font.weight = 700;
    }
    // Labels sit nine units into a segment and centred on the others.
    const double text_width = resolve_text_layout_utf8(text, item_font).logical_size.width;
    const double text_x = visible.kind == VisibleKind::segment
        ? bounds.x + 9.0
        : bounds.x + std::max(0.0, (bounds.width - text_width) * 0.5);
    const Rect label{text_x, bounds.y, text_width, bounds.height};
    record.segment = PartGeometry::from_bounds(bounds);
    record.segment_label = PartGeometry::from_bounds(Rect::intersection(label, bounds));
    // Every segment but the last ends in the chevron its successor overlaps.
    if (item + 1U < visible_items_.size()) {
        const Rect cap{bounds.x + bounds.width - edge_overlap_, bounds.y + 2.0,
                       edge_overlap_, std::max(0.0, bounds.height - 4.0)};
        record.segment_cap = PartGeometry::from_bounds(cap);
    }
    record.segment_focus = PartGeometry::from_bounds(bounds);
    write_item_parts(anatomy().item_systems.front(), breadcrumb_trail_item_part_fields, record, geometry);
}

AnatomyStateSet BreadcrumbTrail::realized_item_anatomy_states(std::size_t item) const {
    AnatomyStateSet states;
    const VisibleItem& visible = visible_items_[item];
    if (hovered_visible_ == item) {
        states.insert(AnatomyState::hot);
    }
    if (visible_stable_id(visible) == active_id_) {
        states.insert(AnatomyState::current);
    }
    if (visible.kind == VisibleKind::segment && !segments_[visible.segment_index].enabled) {
        states.insert(AnatomyState::disabled);
    }
    return states;
}

std::vector<BreadcrumbTrail::Item> BreadcrumbTrail::realized_items() const {
    const std::size_t count = realized_item_count();
    std::vector<Item> records;
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Item record;
        record.index = item;
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems.front(), breadcrumb_trail_item_part_fields, published.parts(), record.parts);
        read_states(breadcrumb_trail_item_state_fields, realized_item_anatomy_states(item), record.states);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events BreadcrumbTrail adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 6> breadcrumb_trail_interactions{{
    {.name = "segment_activated", .payload = "std::string", .mutable_payload = false},
    {.name = "overflow_activated", .payload = "", .mutable_payload = false},
    {.name = "edit_committed", .payload = "std::string", .mutable_payload = false},
    {.name = "edit_started", .payload = "std::string", .mutable_payload = false},
    {.name = "edit_cancelled", .payload = "", .mutable_payload = false},
    {.name = "edit_completion_requested", .payload = "", .mutable_payload = false},
}};

} // namespace

void BreadcrumbTrail::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), breadcrumb_trail_interactions.begin(), breadcrumb_trail_interactions.end());
}

} // namespace gui_forms
