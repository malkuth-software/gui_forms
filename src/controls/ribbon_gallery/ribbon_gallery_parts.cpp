// RibbonGallery lines, part geometry, painting and anatomy. Every part has
// one geometry function, used both by on_paint and by the published anatomy.

#include "gui_forms/controls/ribbon_gallery/ribbon_gallery.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace gui_forms {
namespace {

// Geometry in logical units at text scale 1.
constexpr double scroll_column_width = 16.0;
constexpr double viewport_inset = 2.0;
constexpr double label_height = 16.0;
constexpr double caption_height = 22.0;
constexpr double icon_inset = 4.0;
constexpr double glyph_size = 7.0;

Rect centred_square(Rect area, double side) noexcept {
    if (area.width < side || area.height < side) return {};
    const Rect square{std::floor(area.x + (area.width - side) * 0.5), std::floor(area.y + (area.height - side) * 0.5),
                      side, side};
    return square;
}

Rect inset_rect(Rect area, double inset) noexcept {
    const Rect inner{area.x + inset, area.y + inset, std::max(0.0, area.width - inset * 2.0),
                     std::max(0.0, area.height - inset * 2.0)};
    return inner;
}

PartGeometry visible_part(Rect bounds, Rect clip) noexcept {
    const PartGeometry geometry = PartGeometry::from_bounds(Rect::intersection(bounds, clip));
    return geometry;
}

// One cell's height at `scale`: the item's image area, and its label under
// it when labels show. Lines, geometry and measure all use it.
double cell_height(Size item_size, bool show_labels, double scale) noexcept {
    const double label = show_labels ? label_height : 0.0;
    const double height = (item_size.height + label) * scale;
    return height;
}

} // namespace

// -- lines ---------------------------------------------------------------------

std::size_t RibbonGallery::active_columns() const noexcept {
    const std::size_t columns = form_ == RibbonGalleryForm::in_ribbon ? columns_ : drop_down_columns_;
    return std::max<std::size_t>(1U, columns);
}

// Lays the display order out in lines: rows of `active_columns()` items, and
// in the drop-down form a caption line before each category's rows.
// Uncategorized items come first, without a caption.
void RibbonGallery::rebuild_lines() {
    lines_.clear();
    const double scale = effective_text_scale();
    const double row_height = cell_height(item_size.read(), show_labels.read(), scale);
    const std::size_t columns = active_columns();
    const bool grouped = form_ == RibbonGalleryForm::drop_down && !categories_.empty();
    lines_.reserve(order_.size() / columns + categories_.size() + 1U);
    double top = 0.0;
    std::size_t position = 0U;
    while (position < order_.size()) {
        // One group: the run of display positions with one category (the
        // whole order when captions are not shown).
        const std::string& name = items_[order_[position]].category;
        std::size_t group_end = position;
        while (group_end < order_.size() && (!grouped || items_[order_[group_end]].category == name)) ++group_end;
        if (grouped && !name.empty()) {
            Line caption;
            caption.kind = LineKind::caption;
            for (std::size_t index = 0; index < categories_.size(); ++index) {
                if (categories_[index].stable_id == name) caption.category = index;
            }
            caption.first = position;
            caption.top = top;
            caption.height = caption_height * scale;
            top += caption.height;
            lines_.push_back(caption);
        }
        for (std::size_t first = position; first < group_end; first += columns) {
            Line row;
            row.kind = LineKind::items;
            row.first = first;
            row.count = std::min(columns, group_end - first);
            row.top = top;
            row.height = row_height;
            top += row_height;
            lines_.push_back(row);
        }
        position = group_end;
    }
    if (first_line_ >= lines_.size()) first_line_ = lines_.empty() ? 0U : lines_.size() - 1U;
}

namespace {

// Orders a display position before the first line that starts after it. A
// template only because RibbonGallery::Line is private to the gallery.
struct PositionBeforeLine final {
    template <typename Line>
    bool operator()(std::size_t position, const Line& line) const noexcept {
        return position < line.first;
    }
};

} // namespace

std::optional<std::size_t> RibbonGallery::line_of(std::size_t position) const noexcept {
    if (position >= order_.size()) return std::nullopt;
    const std::vector<Line>::const_iterator after =
        std::upper_bound(lines_.begin(), lines_.end(), position, PositionBeforeLine{});
    std::size_t line = static_cast<std::size_t>(after - lines_.begin());
    while (line > 0U) {
        --line;
        if (lines_[line].kind == LineKind::items) return line;
    }
    return std::nullopt;
}

// -- geometry ------------------------------------------------------------------

RibbonGallery::Geometry RibbonGallery::geometry() const {
    Geometry layout;
    const double scale = effective_text_scale();
    layout.scale = scale;
    layout.client = client_rectangle();
    layout.cell = {item_size.read().width * scale, cell_height(item_size.read(), show_labels.read(), scale)};
    const double column = scroll_column_width * scale;
    const double inset = viewport_inset * scale;
    const double width = layout.client.width;
    const double height = layout.client.height;
    layout.viewport = {inset, inset, std::max(0.0, width - column - inset * 2.0), std::max(0.0, height - inset * 2.0)};
    const double right = std::max(0.0, width - column);
    if (form_ == RibbonGalleryForm::in_ribbon) {
        // Scroll up, scroll down and more, stacked in thirds of the height.
        const double third = std::floor(height / 3.0);
        layout.scroll_up = {right, 0.0, column, third};
        layout.scroll_down = {right, third, column, third};
        layout.more = {right, third * 2.0, column, std::max(0.0, height - third * 2.0)};
    } else {
        const double half = std::floor(height / 2.0);
        layout.scroll_up = {right, 0.0, column, half};
        layout.scroll_down = {right, half, column, std::max(0.0, height - half)};
    }
    return layout;
}

// In the ribbon the gallery shows whole rows only (the owner's clipping
// report, 2026-11-09): a row the viewport cannot hold entirely is not shown,
// so no row is cut off at the bottom when the Ribbon gives the gallery more
// height than its rows fill; the first row always shows. A drop-down's list
// scrolls through rows that may be cut at its edge.
std::size_t RibbonGallery::visible_line_end(const Geometry& layout) const noexcept {
    if (first_line_ >= lines_.size()) return lines_.size();
    const double base = lines_[first_line_].top;
    const bool whole_rows = form_ == RibbonGalleryForm::in_ribbon;
    std::size_t end = first_line_;
    while (end < lines_.size()) {
        const double top = lines_[end].top - base;
        const double bottom = top + lines_[end].height;
        const bool shown = whole_rows ? (end == first_line_ || bottom <= layout.viewport.height + 0.5)
                                      : top < layout.viewport.height;
        if (!shown) break;
        ++end;
    }
    return end;
}

std::size_t RibbonGallery::lines_per_page(const Geometry& layout) const noexcept {
    if (layout.cell.height <= 0.0) return 1U;
    const double rows = std::floor(layout.viewport.height / layout.cell.height);
    return static_cast<std::size_t>(std::max(1.0, rows));
}

bool RibbonGallery::can_scroll_down() const noexcept {
    if (lines_.empty() || first_line_ >= lines_.size()) return false;
    const Geometry layout = geometry();
    const double remaining = lines_.back().top + lines_.back().height - lines_[first_line_].top;
    const bool result = remaining > layout.viewport.height + 0.5;
    return result;
}

Rect RibbonGallery::cell_bounds(std::size_t position, const Geometry& layout) const noexcept {
    const std::optional<std::size_t> line = line_of(position);
    if (!line || first_line_ >= lines_.size()) return {};
    const Line& row = lines_[*line];
    const double column = static_cast<double>(position - row.first);
    const Rect cell{layout.viewport.x + column * layout.cell.width,
                    layout.viewport.y + row.top - lines_[first_line_].top, layout.cell.width, layout.cell.height};
    return cell;
}

void RibbonGallery::scroll_into_view(std::size_t position) {
    const std::optional<std::size_t> line = line_of(position);
    if (!line) return;
    const Geometry layout = geometry();
    std::size_t first = first_line_;
    if (*line < first) {
        first = *line;
        // Show a category's caption with its first row.
        if (first > 0U && lines_[first - 1U].kind == LineKind::caption) --first;
    } else {
        while (first < *line && lines_[*line].top + lines_[*line].height - lines_[first].top >
                                    layout.viewport.height + 0.5) {
            ++first;
        }
    }
    if (first == first_line_) return;
    first_line_ = first;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

double RibbonGallery::width_for_columns(std::size_t columns) const noexcept {
    const double scale = effective_text_scale();
    const double cell_width = item_size.read().width * scale;
    const double chrome_width = (scroll_column_width + viewport_inset * 2.0) * scale;
    const double width = static_cast<double>(columns) * cell_width + chrome_width;
    return width;
}

Size RibbonGallery::measure(Size available) {
    const double scale = effective_text_scale();
    const double row_height = cell_height(item_size.read(), show_labels.read(), scale);
    const double chrome_height = viewport_inset * 2.0 * scale;
    double width = 0.0;
    double height = 0.0;
    if (form_ == RibbonGalleryForm::in_ribbon) {
        width = width_for_columns(columns_);
        height = static_cast<double>(visible_rows.read()) * row_height + chrome_height;
    } else {
        // The drop-down shows its rows of items, plus the captions among
        // them, up to drop_down_rows rows; a short gallery is shorter.
        width = width_for_columns(drop_down_columns_);
        double content = 0.0;
        std::size_t rows = 0U;
        for (const Line& line : lines_) {
            if (line.kind == LineKind::items) {
                if (rows == drop_down_rows_) break;
                ++rows;
            }
            content += line.height;
        }
        height = std::max(content, row_height) + chrome_height;
    }
    const Size result{std::min(width, available.width), std::min(height, available.height)};
    return result;
}

RibbonGallery::Parts RibbonGallery::gallery_parts(const Geometry& layout) const {
    Parts record;
    record.region = PartGeometry::from_bounds(layout.client);
    record.well = PartGeometry::from_bounds(layout.client);
    record.item_viewport = PartGeometry::from_bounds(layout.viewport);
    const double side = glyph_size * layout.scale;
    record.scroll_up = PartGeometry::from_bounds(layout.scroll_up);
    record.scroll_up_glyph = PartGeometry::from_bounds(centred_square(layout.scroll_up, side));
    record.scroll_down = PartGeometry::from_bounds(layout.scroll_down);
    record.scroll_down_glyph = PartGeometry::from_bounds(centred_square(layout.scroll_down, side));
    record.more_button = PartGeometry::from_bounds(layout.more);
    record.more_glyph = PartGeometry::from_bounds(centred_square(layout.more, side));
    record.focus = PartGeometry::from_bounds(layout.client);
    // The actuators' own states.
    if (!can_scroll_up()) {
        record.scroll_up.states.insert(AnatomyState::disabled);
        record.scroll_up_glyph.states.insert(AnatomyState::disabled);
    }
    if (!can_scroll_down()) {
        record.scroll_down.states.insert(AnatomyState::disabled);
        record.scroll_down_glyph.states.insert(AnatomyState::disabled);
    }
    if (hot_zone_ == HitZone::scroll_up) record.scroll_up.states.insert(AnatomyState::hot);
    if (hot_zone_ == HitZone::scroll_down) record.scroll_down.states.insert(AnatomyState::hot);
    if (hot_zone_ == HitZone::more) record.more_button.states.insert(AnatomyState::hot);
    if (pressed_zone_ == HitZone::scroll_up) record.scroll_up.states.insert(AnatomyState::pressed);
    if (pressed_zone_ == HitZone::scroll_down) record.scroll_down.states.insert(AnatomyState::pressed);
    if (pressed_zone_ == HitZone::more) record.more_button.states.insert(AnatomyState::pressed);
    if (more_gallery_ && (*more_gallery_).overlay_open()) {
        record.more_button.states.insert(AnatomyState::open);
        record.more_glyph.states.insert(AnatomyState::open);
    }
    return record;
}

RibbonGallery::ItemParts RibbonGallery::item_parts(std::size_t position, const Geometry& layout) const {
    ItemParts record;
    const Rect clip = layout.viewport;
    const Rect cell = cell_bounds(position, layout);
    if (cell.empty()) return record;
    const double scale = layout.scale;
    const Rect inside = inset_rect(cell, 1.0);
    record.item_face = visible_part(inside, clip);
    record.item_selection = visible_part(inside, clip);
    record.item_focus = visible_part(inside, clip);
    const Rect image_area{cell.x, cell.y, cell.width, item_size.read().height * scale};
    record.item_icon = visible_part(inset_rect(image_area, icon_inset * scale), clip);
    if (show_labels.read()) {
        const Rect label{cell.x + 2.0, cell.y + item_size.read().height * scale, std::max(0.0, cell.width - 4.0),
                         label_height * scale};
        record.item_label = visible_part(label, clip);
    }
    return record;
}

RibbonGallery::CategoryParts RibbonGallery::category_parts(std::size_t line, const Geometry& layout) const {
    CategoryParts record;
    if (line >= lines_.size() || first_line_ >= lines_.size()) return record;
    const Line& caption = lines_[line];
    const double y = layout.viewport.y + caption.top - lines_[first_line_].top;
    const Rect band{layout.viewport.x, y, layout.viewport.width, caption.height};
    const double inset = 4.0 * layout.scale;
    record.category_caption = visible_part({band.x + inset, band.y, std::max(0.0, band.width - inset * 2.0),
                                            std::max(0.0, band.height - 1.0)},
                                           layout.viewport);
    record.category_divider = visible_part({band.x, band.y + band.height - 1.0, band.width, 1.0}, layout.viewport);
    return record;
}

AnatomyStateSet RibbonGallery::item_states(std::size_t position) const {
    AnatomyStateSet states;
    if (position >= order_.size()) return states;
    const std::optional<std::string> selected = selected_id.read();
    if (selected && *selected == items_[order_[position]].stable_id) states.insert(AnatomyState::selected);
    if (!item_enabled(position)) states.insert(AnatomyState::disabled);
    if (hot_ == position) states.insert(AnatomyState::hot);
    if (pressed_ == position) states.insert(AnatomyState::pressed);
    if (focused_ == position) states.insert(AnatomyState::current);
    const Window* const host = attached_window();
    if (has_focus_ && focused_ == position && host != nullptr && (*host).focus_cue_visible()) {
        states.insert(AnatomyState::focus_visible);
    }
    return states;
}

RibbonGallery::HitResult RibbonGallery::hit_test(Point local) const {
    const Geometry layout = geometry();
    HitResult result;
    if (!layout.scroll_up.empty() && layout.scroll_up.contains(local)) {
        result.zone = HitZone::scroll_up;
        return result;
    }
    if (!layout.scroll_down.empty() && layout.scroll_down.contains(local)) {
        result.zone = HitZone::scroll_down;
        return result;
    }
    if (!layout.more.empty() && layout.more.contains(local)) {
        result.zone = HitZone::more;
        return result;
    }
    if (!layout.viewport.contains(local) || first_line_ >= lines_.size() || layout.cell.width <= 0.0) return result;
    const double base = lines_[first_line_].top;
    const std::size_t end = visible_line_end(layout);
    for (std::size_t line = first_line_; line < end; ++line) {
        const Line& entry = lines_[line];
        const double top = layout.viewport.y + entry.top - base;
        if (local.y < top || local.y >= top + entry.height) continue;
        if (entry.kind != LineKind::items) return result;
        const double column = std::floor((local.x - layout.viewport.x) / layout.cell.width);
        if (column < 0.0 || column >= static_cast<double>(entry.count)) return result;
        result.zone = HitZone::item;
        result.position = entry.first + static_cast<std::size_t>(column);
        return result;
    }
    return result;
}

RibbonGallery::Realized RibbonGallery::realized(const Geometry& layout) const {
    Realized result;
    const std::size_t end = visible_line_end(layout);
    for (std::size_t line = first_line_; line < end; ++line) {
        const Line& entry = lines_[line];
        if (entry.kind == LineKind::caption) {
            result.captions.push_back(line);
            continue;
        }
        for (std::size_t offset = 0; offset < entry.count; ++offset) result.positions.push_back(entry.first + offset);
    }
    return result;
}

// -- paint ---------------------------------------------------------------------

void RibbonGallery::on_paint(Painter& painter, Rect) {
    const Geometry layout = geometry();
    const Parts parts = gallery_parts(layout);
    PartPaint part;
    part.font = effective_font({FontRole::control, 10.0, 400, false});
    part.family = ControlVisualRole::editor;
    part.role = PartRole::well;
    part.bounds = parts.well.bounds;
    paint_part(painter, *this, part);
    const Realized shown = realized(layout);
    painter.save();
    painter.clip_rect(layout.viewport);
    for (const std::size_t line : shown.captions) {
        const CategoryParts caption = category_parts(line, layout);
        part.family = ControlVisualRole::panel;
        part.states = {};
        part.role = PartRole::caption;
        part.bounds = caption.category_caption.bounds;
        part.text = categories_[lines_[line].category].caption;
        paint_part(painter, *this, part);
        part.text = {};
        part.role = PartRole::divider;
        part.bounds = caption.category_divider.bounds;
        paint_part(painter, *this, part);
    }
    for (const std::size_t position : shown.positions) {
        const ItemParts item = item_parts(position, layout);
        const RibbonGalleryItem& record = items_[order_[position]];
        part.states = item_states(position);
        // Items are flat at rest and rise in the soft dither while hot (the
        // engine's built-in rule for this key; owner decision RB-15, T4).
        part.family = ControlVisualRole::menu_item;
        part.role = PartRole::face;
        part.bounds = item.item_face.bounds;
        part.key = PartKey{"item_face", record.stable_id, {}};
        paint_part(painter, *this, part);
        part.key.reset();
        part.family = ControlVisualRole::selection;
        part.role = PartRole::selection;
        part.bounds = item.item_selection.bounds;
        paint_part(painter, *this, part);
        part.family = ControlVisualRole::menu_item;
        if (record.image.value != 0U) {
            part.role = PartRole::icon;
            part.bounds = item.item_icon.bounds;
            part.image = record.image;
            paint_part(painter, *this, part);
            part.image = {};
        }
        if (item.item_label.present) {
            part.role = PartRole::label;
            part.bounds = item.item_label.bounds;
            part.text = record.label;
            part.alignment = PartTextAlignment::center;
            paint_part(painter, *this, part);
            part.text = {};
            part.alignment = PartTextAlignment::start;
        }
        part.role = PartRole::focus;
        part.bounds = item.item_focus.bounds;
        paint_part(painter, *this, part);
    }
    painter.restore();
    part.family = ControlVisualRole::menu_item;
    struct Actuator final {
        const PartGeometry* face;
        const PartGeometry* glyph;
        PartMark mark;
    };
    const std::array<Actuator, 3> actuators{{
        {&parts.scroll_up, &parts.scroll_up_glyph, PartMark::arrow_up},
        {&parts.scroll_down, &parts.scroll_down_glyph, PartMark::arrow_down},
        {&parts.more_button, &parts.more_glyph, PartMark::more},
    }};
    for (const Actuator& actuator : actuators) {
        if (!(*actuator.face).present) continue;
        part.states = (*actuator.face).states;
        part.role = PartRole::actuator;
        part.bounds = (*actuator.face).bounds;
        paint_part(painter, *this, part);
        part.role = PartRole::glyph;
        part.bounds = (*actuator.glyph).bounds;
        part.mark = actuator.mark;
        paint_part(painter, *this, part);
        part.mark = PartMark::none;
    }
}

// -- anatomy -------------------------------------------------------------------

namespace {

constexpr std::array<AnatomyState, 3> scroll_states{{AnatomyState::hot, AnatomyState::pressed, AnatomyState::disabled}};
constexpr std::array<AnatomyState, 3> more_states{{AnatomyState::hot, AnatomyState::pressed, AnatomyState::open}};
constexpr std::array<AnatomyState, 1> more_glyph_states{{AnatomyState::open}};

constexpr std::array<PartField<RibbonGallery::Parts>, 10> gallery_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &RibbonGallery::Parts::region},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &RibbonGallery::Parts::well},
    {.declaration = {.name = "item_viewport", .role = PartRole::region, .composed_from = {}},
     .member = &RibbonGallery::Parts::item_viewport},
    {.declaration = {.name = "scroll_up", .role = PartRole::actuator, .composed_from = "button",
                     .states = scroll_states},
     .member = &RibbonGallery::Parts::scroll_up},
    {.declaration = {.name = "scroll_up_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = scroll_states},
     .member = &RibbonGallery::Parts::scroll_up_glyph},
    {.declaration = {.name = "scroll_down", .role = PartRole::actuator, .composed_from = "button",
                     .states = scroll_states},
     .member = &RibbonGallery::Parts::scroll_down},
    {.declaration = {.name = "scroll_down_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = scroll_states},
     .member = &RibbonGallery::Parts::scroll_down_glyph},
    {.declaration = {.name = "more_button", .role = PartRole::actuator, .composed_from = "button",
                     .states = more_states},
     .member = &RibbonGallery::Parts::more_button},
    {.declaration = {.name = "more_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = more_glyph_states},
     .member = &RibbonGallery::Parts::more_glyph},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &RibbonGallery::Parts::focus},
}};

constexpr std::array<PartField<RibbonGallery::ItemParts>, 5> item_part_fields{{
    {.declaration = {.name = "item_face", .role = PartRole::face, .composed_from = {}},
     .member = &RibbonGallery::ItemParts::item_face},
    {.declaration = {.name = "item_selection", .role = PartRole::selection, .composed_from = {}},
     .member = &RibbonGallery::ItemParts::item_selection},
    {.declaration = {.name = "item_icon", .role = PartRole::icon, .composed_from = {}},
     .member = &RibbonGallery::ItemParts::item_icon},
    {.declaration = {.name = "item_label", .role = PartRole::label, .composed_from = {}},
     .member = &RibbonGallery::ItemParts::item_label},
    {.declaration = {.name = "item_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &RibbonGallery::ItemParts::item_focus},
}};

constexpr std::array<StateField<RibbonGallery::ItemStates>, 6> item_state_fields{{
    {.state = AnatomyState::hot, .member = &RibbonGallery::ItemStates::hot},
    {.state = AnatomyState::pressed, .member = &RibbonGallery::ItemStates::pressed},
    {.state = AnatomyState::selected, .member = &RibbonGallery::ItemStates::selected},
    {.state = AnatomyState::current, .member = &RibbonGallery::ItemStates::current},
    {.state = AnatomyState::disabled, .member = &RibbonGallery::ItemStates::disabled},
    {.state = AnatomyState::focus_visible, .member = &RibbonGallery::ItemStates::focus_visible},
}};

constexpr std::array<PartField<RibbonGallery::CategoryParts>, 2> category_part_fields{{
    {.declaration = {.name = "category_caption", .role = PartRole::caption, .composed_from = {}},
     .member = &RibbonGallery::CategoryParts::category_caption},
    {.declaration = {.name = "category_divider", .role = PartRole::divider, .composed_from = {}},
     .member = &RibbonGallery::CategoryParts::category_divider},
}};

constexpr std::array<PartDeclaration, 10> gallery_parts_declared = part_declarations(gallery_part_fields);
constexpr std::array<PartDeclaration, 5> item_parts_declared = part_declarations(item_part_fields);
constexpr std::array<AnatomyState, 6> item_states_declared = state_declarations(item_state_fields);
constexpr std::array<PartDeclaration, 2> category_parts_declared = part_declarations(category_part_fields);
constexpr std::array<AnatomyState, 5> gallery_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr ItemSystemIndex item_system{0U};
constexpr ItemSystemIndex category_system{1U};

constexpr std::array<ItemSystemDeclaration, 2> gallery_item_systems{{
    {.name = "gallery_item", .parts = item_parts_declared, .states = item_states_declared, .custom_states = {}},
    {.name = "gallery_category", .parts = category_parts_declared, .states = {}, .custom_states = {}},
}};

constexpr ControlAnatomy ribbon_gallery_anatomy{
    .control_id = "ribbon_gallery",
    .parts = gallery_parts_declared,
    .states = gallery_states,
    .custom_states = {},
    .item_systems = gallery_item_systems,
};

} // namespace

const ControlAnatomy& RibbonGallery::anatomy() const noexcept {
    return ribbon_gallery_anatomy;
}

void RibbonGallery::publish_part_geometry(std::span<PartGeometry> geometry_out) const {
    Parts record = gallery_parts(geometry());
    if (form_ == RibbonGalleryForm::drop_down) {
        // A drop-down gallery has no more button.
        record.more_button = {};
        record.more_glyph = {};
    }
    write_parts(anatomy(), gallery_part_fields, record, geometry_out);
}

RibbonGallery::Parts RibbonGallery::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), gallery_part_fields, published.parts(), record);
    return record;
}

std::size_t RibbonGallery::realized_item_count() const {
    const Realized shown = realized(geometry());
    const std::size_t result = shown.positions.size() + shown.captions.size();
    return result;
}

ItemSystemIndex RibbonGallery::publish_realized_item_system(std::size_t item) const {
    const Realized shown = realized(geometry());
    if (item < shown.positions.size()) return item_system;
    return category_system;
}

std::string RibbonGallery::realized_item_stable_id(std::size_t item) const {
    const Realized shown = realized(geometry());
    if (item < shown.positions.size()) return items_[order_[shown.positions[item]]].stable_id;
    const std::size_t line = shown.captions[item - shown.positions.size()];
    return std::string(stable_id().value()) + ".category." + categories_[lines_[line].category].stable_id;
}

AnatomyStateSet RibbonGallery::realized_item_anatomy_states(std::size_t item) const {
    const Realized shown = realized(geometry());
    if (item < shown.positions.size()) return item_states(shown.positions[item]);
    const AnatomyStateSet none;
    return none;
}

void RibbonGallery::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry_out) const {
    const Geometry layout = geometry();
    const Realized shown = realized(layout);
    if (item < shown.positions.size()) {
        const ItemParts record = item_parts(shown.positions[item], layout);
        write_item_parts(anatomy().item_systems[item_system.value], item_part_fields, record, geometry_out);
        return;
    }
    const CategoryParts record = category_parts(shown.captions[item - shown.positions.size()], layout);
    write_item_parts(anatomy().item_systems[category_system.value], category_part_fields, record, geometry_out);
}

std::vector<RibbonGallery::Item> RibbonGallery::realized_items() const {
    const Geometry layout = geometry();
    const Realized shown = realized(layout);
    std::vector<Item> records;
    records.reserve(shown.positions.size());
    for (const std::size_t position : shown.positions) {
        Item record;
        record.index = order_[position];
        record.stable_id = items_[order_[position]].stable_id;
        record.parts = item_parts(position, layout);
        read_states(item_state_fields, item_states(position), record.states);
        records.push_back(record);
    }
    return records;
}

std::vector<RibbonGallery::Category> RibbonGallery::realized_categories() const {
    const Geometry layout = geometry();
    const Realized shown = realized(layout);
    std::vector<Category> records;
    records.reserve(shown.captions.size());
    for (const std::size_t line : shown.captions) {
        Category record;
        record.index = lines_[line].category;
        record.stable_id = categories_[lines_[line].category].stable_id;
        record.parts = category_parts(line, layout);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events RibbonGallery adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> ribbon_gallery_interactions{{
    {.name = "selection_changed", .payload = "std::optional<std::string>", .mutable_payload = false},
    {.name = "overlay_open_changed", .payload = "bool", .mutable_payload = false},
}};

} // namespace

void RibbonGallery::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), ribbon_gallery_interactions.begin(), ribbon_gallery_interactions.end());
}

} // namespace gui_forms
