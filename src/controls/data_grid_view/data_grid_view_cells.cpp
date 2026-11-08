#include "data_grid_view_cells_control.hpp"

#include "gui_forms/types/painter/painter.hpp"

#include <algorithm>
#include <span>
#include <string_view>
#include <utility>

namespace gui_forms {

namespace {

// The grid's anatomy ID with no parts of its own: the theme's rules for the
// grid's roles and states select the cells' parts as they do the grid's
// (ThemeEngine reads the control's anatomy ID, classes and inline style).
constexpr ControlAnatomy cells_anatomy{.control_id = "data_grid_view",
                                       .parts = {},
                                       .states = {},
                                       .custom_states = {},
                                       .item_systems = {}};

[[nodiscard]] bool range_within(DataGridViewCellsTextRange range, std::size_t size) noexcept {
    return range.begin_byte <= range.end_byte && range.end_byte <= size;
}

[[nodiscard]] bool cell_valid(const DataGridViewCellsSnapshot& snapshot, const DataGridViewCellsCell& cell) noexcept {
    if (!range_within(cell.text, snapshot.text.size())) return false;
    const std::size_t line_total = snapshot.lines.size();
    if (cell.lines.first > line_total || cell.lines.count > line_total - cell.lines.first) return false;
    for (std::size_t index = 0U; index < cell.lines.count; ++index) {
        const DataGridViewCellsTextRange line = snapshot.lines[cell.lines.first + index];
        const bool inside = line.begin_byte >= cell.text.begin_byte && line.begin_byte <= line.end_byte &&
                            line.end_byte <= cell.text.end_byte;
        if (!inside) return false;
    }
    return true;
}

[[nodiscard]] std::string_view text_of(const DataGridViewCellsSnapshot& snapshot,
                                       DataGridViewCellsTextRange range) noexcept {
    const std::string_view all(snapshot.text);
    const std::string_view text = all.substr(range.begin_byte, range.end_byte - range.begin_byte);
    return text;
}

} // namespace

bool data_grid_view_cells_snapshot_valid(const DataGridViewCellsSnapshot& snapshot) noexcept {
    for (const DataGridViewCellsCell& cell : snapshot.cells) {
        if (!cell_valid(snapshot, cell)) return false;
    }
    return true;
}

// A resolved cell, as the grid paints it: the selection plate, the content
// (link, wrapped or single-line text), the image, the check box, the
// button, the drop-down actuator, the error glyph, the rules and the focus
// part, in that order.
void paint_data_grid_view_cell(Painter& painter, const Control& owner, const DataGridViewCellsSnapshot& snapshot,
                               const DataGridViewCellsCell& cell, Rect data) {
    paint_data_grid_view_cell_selection(painter, owner, cell);
    paint_data_grid_view_cell_parts(painter, owner, snapshot, cell, data, true);
}

void paint_data_grid_view_cell_selection(Painter& painter, const Control& owner, const DataGridViewCellsCell& cell) {
    if (!cell.parts.cell.present) return;
    // The selection plate: transparent at rest in the selection recipe.
    PartPaint part;
    part.states = cell.states;
    part.font = cell.font;
    part.family = ControlVisualRole::selection;
    part.role = PartRole::selection;
    part.bounds = cell.parts.selection.bounds;
    part.authored_background = cell.parts.selection.authored_background;
    paint_part(painter, owner, part);
}

void paint_data_grid_view_cell_parts(Painter& painter, const Control& owner, const DataGridViewCellsSnapshot& snapshot,
                                     const DataGridViewCellsCell& cell, Rect data, bool default_content) {
    const DataGridViewCellsParts& parts = cell.parts;
    if (!parts.cell.present) return;
    const std::string_view text = text_of(snapshot, cell.text);
    PartPaint part;
    part.states = cell.states;
    part.font = cell.font;
    part.family = ControlVisualRole::editor;
    if (default_content && parts.link.present) {
        part.family = cell.visited ? ControlVisualRole::visited_link : ControlVisualRole::link;
        part.role = PartRole::content;
        part.bounds = parts.link.bounds;
        part.text = text;
        paint_part(painter, owner, part);
        part.text = {};
        if (parts.link_underline.present) {
            part.role = PartRole::glyph;
            part.mark = PartMark::underline;
            part.bounds = parts.link_underline.bounds;
            paint_part(painter, owner, part);
            part.mark = PartMark::none;
        }
        part.family = ControlVisualRole::editor;
    } else if (default_content && parts.content.present && !cell.editing_text && cell.lines.count != 0U) {
        // Wrapped text: each line a content part of one line's height, the
        // whole cut to the content box so that lines past it do not reach
        // the next row.
        painter.save();
        painter.clip_rect(Rect::intersection(cell.content_box, data));
        part.role = PartRole::content;
        part.alignment = cell.alignment;
        part.authored_foreground = parts.content.authored_foreground;
        const Rect box = cell.lines_box;
        for (std::size_t index = 0U; index < cell.lines.count; ++index) {
            const DataGridViewCellsTextRange line = snapshot.lines[cell.lines.first + index];
            part.bounds = {box.x, box.y + cell.line_height * static_cast<double>(index), box.width, cell.line_height};
            part.text = text_of(snapshot, line);
            paint_part(painter, owner, part);
        }
        painter.restore();
        part.authored_foreground.reset();
        part.text = {};
        part.alignment = PartTextAlignment::start;
    } else if (default_content && parts.content.present && !cell.editing_text) {
        part.role = PartRole::content;
        part.bounds = parts.content.bounds;
        part.text = text;
        part.alignment = cell.alignment;
        part.authored_foreground = parts.content.authored_foreground;
        paint_part(painter, owner, part);
        part.authored_foreground.reset();
        part.text = {};
        part.alignment = PartTextAlignment::start;
    }
    if (default_content && parts.image.present) {
        // Drawn whole and cut at the cell's inside, so that an image wider
        // than its cell, or one partly scrolled away, keeps its shape.
        painter.save();
        painter.clip_rect(Rect::intersection(cell.image_clip, data));
        part.role = PartRole::image;
        part.bounds = parts.image.bounds;
        part.image = cell.image;
        paint_part(painter, owner, part);
        part.image = {};
        painter.restore();
    }
    if (default_content && parts.check_well.present) {
        part.family = ControlVisualRole::choice;
        part.role = PartRole::well;
        part.bounds = parts.check_well.bounds;
        paint_part(painter, owner, part);
        // The indicator is a mark: drawn while checked, absent otherwise.
        if (cell.states.contains(AnatomyState::checked)) {
            part.role = PartRole::indicator;
            part.bounds = parts.check_indicator.bounds;
            part.mark = PartMark::check;
            paint_part(painter, owner, part);
            part.mark = PartMark::none;
        }
    }
    if (default_content && parts.button_face.present) {
        part.family = ControlVisualRole::button;
        part.role = PartRole::face;
        part.bounds = parts.button_face.bounds;
        paint_part(painter, owner, part);
        part.role = PartRole::label;
        part.bounds = parts.button_label.bounds;
        part.text = text;
        part.alignment = PartTextAlignment::center;
        part.authored_foreground = parts.button_label.authored_foreground;
        paint_part(painter, owner, part);
        part.authored_foreground.reset();
        part.text = {};
        part.alignment = PartTextAlignment::start;
    }
    if (parts.drop_down.present) {
        // A drop-down actuator in a strip-like family: flat at rest.
        part.family = ControlVisualRole::menu_item;
        part.role = PartRole::actuator;
        part.bounds = parts.drop_down.bounds;
        paint_part(painter, owner, part);
        part.role = PartRole::glyph;
        part.bounds = parts.drop_down_glyph.bounds;
        part.mark = PartMark::arrow_down;
        paint_part(painter, owner, part);
        part.mark = PartMark::none;
    }
    if (parts.error.present) {
        part.family = ControlVisualRole::panel;
        part.role = PartRole::glyph;
        part.bounds = parts.error.bounds;
        part.mark = PartMark::error;
        paint_part(painter, owner, part);
        part.mark = PartMark::none;
    }
    part.family = ControlVisualRole::panel;
    part.role = PartRole::divider;
    if (parts.vertical_rule.present) {
        part.bounds = parts.vertical_rule.bounds;
        part.authored_foreground = parts.vertical_rule.authored_foreground;
        paint_part(painter, owner, part);
    }
    if (parts.horizontal_rule.present) {
        part.bounds = parts.horizontal_rule.bounds;
        part.authored_foreground = parts.horizontal_rule.authored_foreground;
        paint_part(painter, owner, part);
    }
    part.authored_foreground.reset();
    part.family = ControlVisualRole::editor;
    part.role = PartRole::focus;
    part.bounds = parts.focus.bounds;
    paint_part(painter, owner, part);
}

DataGridViewCells::DataGridViewCells(StableId stable_id) : Control(std::move(stable_id)) {
    set_focusable(false);
}

bool DataGridViewCells::set_snapshot(DataGridViewCellsSnapshot snapshot) {
    require_mutable();
    if (!data_grid_view_cells_snapshot_valid(snapshot)) {
        ++snapshots_refused_;
        return false;
    }
    take_look(snapshot.look);
    snapshot_ = std::move(snapshot);
    ++snapshots_taken_;
    invalidate(Dirty::paint);
    return true;
}

// Only what changed is written, so an unchanged look recompiles no inline
// style and repaints nothing beyond the snapshot's own repaint.
void DataGridViewCells::take_look(const DataGridViewCellsLook& look) {
    if (snapshot_.look == look && snapshots_taken_ != 0U) return;
    const std::span<const std::string> current = classes();
    const bool same_classes = std::equal(current.begin(), current.end(), look.classes.begin(), look.classes.end());
    if (!same_classes) set_classes(look.classes);
    if (inline_style() != look.inline_style) static_cast<void>(set_inline_style(look.inline_style));
    if (back_color.read() != look.back_color) back_color = look.back_color;
    if (fore_color.read() != look.fore_color) fore_color = look.fore_color;
}

void DataGridViewCells::on_paint(Painter& painter, Rect) {
    const Rect data = client_rectangle();
    PartPaint well;
    well.family = ControlVisualRole::editor;
    well.role = PartRole::well;
    well.bounds = snapshot_.well.bounds;
    well.authored_background = snapshot_.well.authored_background;
    paint_part(painter, *this, well);
    if (well.authored_background && !snapshot_.cells_extent.empty()) {
        // Under the cells the well keeps the theme's ground: the same well
        // painted again without the authored colour, cut to the cells.
        well.authored_background.reset();
        painter.save();
        painter.clip_rect(Rect::intersection(snapshot_.cells_extent, data));
        paint_part(painter, *this, well);
        painter.restore();
    }
    if (data.empty()) return;
    painter.save();
    painter.clip_rect(data);
    for (const DataGridViewCellsCell& cell : snapshot_.cells) {
        paint_data_grid_view_cell(painter, *this, snapshot_, cell, data);
    }
    painter.restore();
}

const ControlAnatomy& DataGridViewCells::anatomy() const noexcept {
    return cells_anatomy;
}

// Not exposed: the grid on the UI thread publishes the cells' semantic
// nodes, so a client never sees them twice.
SemanticDescriptor DataGridViewCells::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::pane;
    descriptor.exposed = false;
    return descriptor;
}

Control::Ptr DataGridViewCellsContent::build_root() {
    cells_ = std::make_shared<DataGridViewCells>(stable_id_);
    Control::Ptr root = cells_;
    return root;
}

void DataGridViewCellsContent::apply_update(SubSurfaceContentUpdate& update) {
    DataGridViewCellsUpdate* const cells_update = dynamic_cast<DataGridViewCellsUpdate*>(&update);
    if (cells_update == nullptr || !cells_) return;
    static_cast<void>((*cells_).set_snapshot(std::move((*cells_update).snapshot)));
}

} // namespace gui_forms
