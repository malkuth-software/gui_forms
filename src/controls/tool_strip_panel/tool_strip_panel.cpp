#include "gui_forms/controls/tool_strip_panel/tool_strip_panel.hpp"

#include "gui_forms/controls/status_strip/status_strip.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

bool valid_edge(ToolStripPanelEdge edge) noexcept {
    const bool result = edge == ToolStripPanelEdge::top || edge == ToolStripPanelEdge::bottom ||
                        edge == ToolStripPanelEdge::left || edge == ToolStripPanelEdge::right;
    return result;
}

bool status_strip(const ToolStrip& strip) noexcept {
    const bool result = dynamic_cast<const StatusStrip*>(&strip) != nullptr;
    return result;
}

// A strip's thickness across the panel: its measured height in a row, or
// its measured width in a column. Non-finite or negative answers count as
// zero.
double strip_thickness(ToolStrip& strip, bool vertical, Size available) {
    const Size measured = strip.measure(available);
    double thickness = vertical ? measured.width : measured.height;
    if (!std::isfinite(thickness) || thickness < 0.0) thickness = 0.0;
    return thickness;
}

} // namespace

ToolStripPanel::ToolStripPanel(StableId stable_id, ToolStripPanelEdge initial_edge) : Control(std::move(stable_id)) {
    edge = initial_edge;
    set_paint_plane(PaintPlane::control);
}

bool ToolStripPanel::vertical() const noexcept {
    const bool result = edge.read() == ToolStripPanelEdge::left || edge.read() == ToolStripPanelEdge::right;
    return result;
}

std::vector<std::shared_ptr<ToolStrip>> ToolStripPanel::strips() const {
    std::vector<std::shared_ptr<ToolStrip>> joined;
    const std::span<const Control::Ptr> owned = children();
    joined.reserve(owned.size());
    for (const Control::Ptr& child : owned) {
        // join() adds only ToolStrips; a control added otherwise is skipped.
        std::shared_ptr<ToolStrip> strip = std::dynamic_pointer_cast<ToolStrip>(child);
        if (strip) joined.push_back(std::move(strip));
    }
    return joined;
}

// A vertical edge is refused while a StatusStrip is joined, before anything
// changes.
void ToolStripPanel::prepare_write(const decltype(edge)&, ToolStripPanelEdge& incoming) {
    require_mutable();
    if (!valid_edge(incoming)) throw std::invalid_argument("ToolStripPanel edge is invalid");
    const bool turns_vertical = incoming == ToolStripPanelEdge::left || incoming == ToolStripPanelEdge::right;
    if (!turns_vertical) return;
    const std::vector<std::shared_ptr<ToolStrip>> joined = strips();
    for (const std::shared_ptr<ToolStrip>& strip : joined) {
        if (status_strip(*strip)) throw std::logic_error("A StatusStrip cannot join a left or right panel");
    }
}

// The joined strips turn to the new edge's orientation.
void ToolStripPanel::written(const decltype(edge)&) {
    const Orientation orientation = vertical() ? Orientation::vertical : Orientation::horizontal;
    const std::vector<std::shared_ptr<ToolStrip>> joined = strips();
    for (const std::shared_ptr<ToolStrip>& strip : joined) (*strip).orientation = orientation;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ToolStripPanel::join(std::shared_ptr<ToolStrip> strip) {
    require_mutable();
    if (!strip || (*strip).parent()) throw std::invalid_argument("ToolStripPanel joins a strip without a parent");
    if (vertical() && status_strip(*strip)) {
        throw std::logic_error("A StatusStrip cannot join a left or right panel");
    }
    (*strip).orientation = vertical() ? Orientation::vertical : Orientation::horizontal;
    add_child(std::move(strip));
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

bool ToolStripPanel::continues_row(const ToolStrip& strip) const noexcept {
    const RuntimeId id = strip.runtime_id();
    const bool continues = std::find(continuing_.begin(), continuing_.end(), id) != continuing_.end();
    return continues;
}

std::vector<std::vector<std::shared_ptr<ToolStrip>>> ToolStripPanel::rows() const {
    std::vector<std::vector<std::shared_ptr<ToolStrip>>> result;
    for (const std::shared_ptr<ToolStrip>& strip : strips()) {
        if (result.empty() || !continues_row(*strip)) result.emplace_back();
        result.back().push_back(strip);
    }
    return result;
}

std::size_t ToolStripPanel::row_count() const {
    std::size_t count = 0U;
    for (const std::shared_ptr<ToolStrip>& strip : strips()) {
        if (count == 0U || !continues_row(*strip)) ++count;
    }
    return count;
}

void ToolStripPanel::place(std::shared_ptr<ToolStrip> strip, const std::shared_ptr<ToolStrip>& before, bool continues) {
    // The position in children() of `before` (paint order, which is the
    // panel's stacking order).
    std::optional<std::size_t> position{};
    if (before) {
        const std::span<const Control::Ptr> owned = children();
        for (std::size_t index = 0U; index < owned.size(); ++index) {
            if (owned[index] == before) position = index;
        }
    }
    const RuntimeId inserted = (*strip).runtime_id();
    join(std::move(strip));
    if (continues) continuing_.push_back(inserted);
    if (!position) return;
    // set_child_index counts as Windows Forms does, from the last child
    // (topmost) at 0: with the joined strip taken out, `count - 1 - position`
    // puts it just before `before`.
    const std::size_t count = children().size();
    static_cast<void>(set_child_index_from_top(inserted, count - 1U - *position));
}

void ToolStripPanel::insert(std::shared_ptr<ToolStrip> strip, std::size_t row) {
    require_mutable();
    const std::vector<std::vector<std::shared_ptr<ToolStrip>>> joined = rows();
    if (row > joined.size()) throw std::out_of_range("ToolStripPanel insert row is past the rows");
    if (!strip || (*strip).parent()) throw std::invalid_argument("ToolStripPanel joins a strip without a parent");
    const std::shared_ptr<ToolStrip> before = row < joined.size() ? joined[row].front() : nullptr;
    place(std::move(strip), before, false);
}

void ToolStripPanel::join_row(std::shared_ptr<ToolStrip> strip, std::size_t row, std::size_t position) {
    require_mutable();
    const std::vector<std::vector<std::shared_ptr<ToolStrip>>> joined = rows();
    if (row >= joined.size()) throw std::out_of_range("ToolStripPanel join_row row is past the rows");
    const std::vector<std::shared_ptr<ToolStrip>>& members = joined[row];
    if (position > members.size()) throw std::out_of_range("ToolStripPanel join_row position is past the row");
    if (!strip || (*strip).parent()) throw std::invalid_argument("ToolStripPanel joins a strip without a parent");
    if (vertical() && status_strip(*strip)) {
        throw std::logic_error("A StatusStrip cannot join a left or right panel");
    }
    // Before the strip now at `position`, or before the next row's first.
    std::shared_ptr<ToolStrip> before{};
    if (position < members.size()) before = members[position];
    else if (row + 1U < joined.size()) before = joined[row + 1U].front();
    if (position == 0U) {
        // The new strip starts the row; the row's first strip continues it.
        const RuntimeId first = (*members.front()).runtime_id();
        place(std::move(strip), before, false);
        continuing_.push_back(first);
        return;
    }
    place(std::move(strip), before, true);
}

std::shared_ptr<ToolStrip> ToolStripPanel::remove(const ToolStrip& strip) {
    require_mutable();
    const Control::Ptr owner = strip.parent();
    if (!owner || owner.get() != this) return nullptr;
    const RuntimeId id = strip.runtime_id();
    const bool continued = continues_row(strip);
    // The strip after it, which starts the row when this one did.
    std::shared_ptr<ToolStrip> next{};
    const std::vector<std::shared_ptr<ToolStrip>> joined = strips();
    for (std::size_t index = 0U; index + 1U < joined.size(); ++index) {
        if (joined[index].get() == &strip) next = joined[index + 1U];
    }
    const Control::Ptr removed = remove_child(id);
    std::erase(continuing_, id);
    if (!continued && next) std::erase(continuing_, (*next).runtime_id());
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    return std::static_pointer_cast<ToolStrip>(removed);
}

Size ToolStripPanel::measure(Size available) {
    const bool along_height = vertical();
    // Each strip is measured with unbounded thickness and the panel's full
    // length; a row is as thick as its thickest shown strip, and the panel
    // is its rows together.
    const double unbounded = std::numeric_limits<double>::max();
    const Size offered = along_height ? Size{unbounded, available.height} : Size{available.width, unbounded};
    double thickness = 0.0;
    for (const std::vector<std::shared_ptr<ToolStrip>>& row : rows()) {
        double row_thickness = 0.0;
        for (const std::shared_ptr<ToolStrip>& strip : row) {
            if (!(*strip).visible.read()) continue;
            row_thickness = std::max(row_thickness, strip_thickness(*strip, along_height, offered));
        }
        thickness += row_thickness;
    }
    if (along_height) {
        const Size result{std::min(available.width, thickness), available.height};
        return result;
    }
    const Size result{available.width, std::min(available.height, thickness)};
    return result;
}

// Row by row: the row's shown strips get their content lengths in order, no
// more than is left, and the last shown strip the rest; each is as thick as
// the row.
void ToolStripPanel::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const bool along_height = vertical();
    const double length = along_height ? final_bounds.height : final_bounds.width;
    const double unbounded = std::numeric_limits<double>::max();
    const Size offered = along_height ? Size{unbounded, final_bounds.height} : Size{final_bounds.width, unbounded};
    const std::vector<std::vector<std::shared_ptr<ToolStrip>>> joined = rows();
    double next_row = 0.0;
    for (const std::vector<std::shared_ptr<ToolStrip>>& row : joined) {
        // The row's shown strips and its thickness. Measuring runs the
        // strips' program code, which may take one out of the panel.
        std::vector<std::shared_ptr<ToolStrip>> shown;
        shown.reserve(row.size());
        double thickness = 0.0;
        for (const std::shared_ptr<ToolStrip>& strip : row) {
            if (!is_current_layout_child(strip) || !(*strip).visible.read()) continue;
            const double measured = strip_thickness(*strip, along_height, offered);
            if (!is_current_layout_child(strip)) continue;
            thickness = std::max(thickness, measured);
            shown.push_back(strip);
        }
        double along = 0.0;
        for (std::size_t index = 0U; index < shown.size(); ++index) {
            const double left = std::max(0.0, length - along);
            double extent = left;
            if (index + 1U < shown.size()) {
                const double wanted = (*shown[index]).content_length();
                extent = std::isfinite(wanted) && wanted > 0.0 ? std::min(left, wanted) : 0.0;
            }
            const Rect slot = along_height ? Rect{next_row, along, thickness, extent}
                                           : Rect{along, next_row, extent, thickness};
            set_child_layout(shown[index], slot);
            along += extent;
        }
        next_row += thickness;
    }
}

void ToolStripPanel::on_paint(Painter& painter, Rect) {
    PartPaint face;
    face.family = ControlVisualRole::panel;
    face.role = PartRole::face;
    face.bounds = client_rectangle();
    paint_part(painter, *this, face);
}

SemanticDescriptor ToolStripPanel::semantic_descriptor() const {
    // A layout band: its strips describe themselves.
    SemanticDescriptor descriptor = Control::semantic_descriptor();
    descriptor.role = SemanticRole::group;
    descriptor.exposed = false;
    return descriptor;
}

namespace {

constexpr std::array<PartField<ToolStripPanel::Parts>, 2> tool_strip_panel_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &ToolStripPanel::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &ToolStripPanel::Parts::face},
}};

constexpr std::array<PartDeclaration, 2> tool_strip_panel_parts = part_declarations(tool_strip_panel_part_fields);
constexpr std::array<AnatomyState, 5> tool_strip_panel_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr ControlAnatomy tool_strip_panel_anatomy{
    .control_id = "tool_strip_panel",
    .parts = tool_strip_panel_parts,
    .states = tool_strip_panel_states,
    .custom_states = {},
    .item_systems = {},
};

} // namespace

const ControlAnatomy& ToolStripPanel::anatomy() const noexcept {
    return tool_strip_panel_anatomy;
}

void ToolStripPanel::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    record.face = PartGeometry::from_bounds(client_rectangle());
    write_parts(anatomy(), tool_strip_panel_part_fields, record, geometry);
}

ToolStripPanel::Parts ToolStripPanel::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), tool_strip_panel_part_fields, published.parts(), record);
    return record;
}

// property_codemod begin ToolStripPanel (tools/property_families/tool_strip_panel.json; generated, do not edit)

// property_codemod end ToolStripPanel

} // namespace gui_forms
