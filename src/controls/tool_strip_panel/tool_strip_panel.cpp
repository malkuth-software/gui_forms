#include "gui_forms/controls/tool_strip_panel/tool_strip_panel.hpp"

#include "gui_forms/controls/status_strip/status_strip.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

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

ToolStripPanel::ToolStripPanel(StableId stable_id, ToolStripPanelEdge edge) : Control(std::move(stable_id)) {
    if (!valid_edge(edge)) throw std::invalid_argument("ToolStripPanel edge is invalid");
    edge_ = edge;
    set_paint_plane(PaintPlane::control);
}

bool ToolStripPanel::vertical() const noexcept {
    const bool result = edge_ == ToolStripPanelEdge::left || edge_ == ToolStripPanelEdge::right;
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

void ToolStripPanel::set_edge(ToolStripPanelEdge edge) {
    require_mutable();
    if (!valid_edge(edge)) throw std::invalid_argument("ToolStripPanel edge is invalid");
    if (edge_ == edge) return;
    const bool turns_vertical = edge == ToolStripPanelEdge::left || edge == ToolStripPanelEdge::right;
    const std::vector<std::shared_ptr<ToolStrip>> joined = strips();
    if (turns_vertical) {
        for (const std::shared_ptr<ToolStrip>& strip : joined) {
            if (status_strip(*strip)) throw std::logic_error("A StatusStrip cannot join a left or right panel");
        }
    }
    edge_ = edge;
    const Orientation orientation = turns_vertical ? Orientation::vertical : Orientation::horizontal;
    for (const std::shared_ptr<ToolStrip>& strip : joined) (*strip).set_orientation(orientation);
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ToolStripPanel::join(std::shared_ptr<ToolStrip> strip) {
    require_mutable();
    if (!strip || (*strip).parent()) throw std::invalid_argument("ToolStripPanel joins a strip without a parent");
    if (vertical() && status_strip(*strip)) {
        throw std::logic_error("A StatusStrip cannot join a left or right panel");
    }
    (*strip).set_orientation(vertical() ? Orientation::vertical : Orientation::horizontal);
    add_child(std::move(strip));
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ToolStripPanel::insert(std::shared_ptr<ToolStrip> strip, std::size_t row) {
    require_mutable();
    const std::vector<std::shared_ptr<ToolStrip>> joined = strips();
    if (row > joined.size()) throw std::out_of_range("ToolStripPanel insert row is past the joined strips");
    // The position in children() of the strip now at `row` (paint order,
    // which is the panel's stacking order); none to join last.
    std::optional<std::size_t> position{};
    if (row < joined.size()) {
        const std::span<const Control::Ptr> owned = children();
        for (std::size_t index = 0U; index < owned.size(); ++index) {
            if (owned[index] == joined[row]) position = index;
        }
    }
    const RuntimeId inserted = strip ? (*strip).runtime_id() : RuntimeId{};
    join(std::move(strip));
    if (!position) return;
    // set_child_index counts as Windows Forms does, from the last child
    // (topmost) at 0: with the joined strip taken out, `count - 1 - position`
    // puts it just before the strip that was at `row`.
    const std::size_t count = children().size();
    static_cast<void>(set_child_index(inserted, count - 1U - *position));
}

std::shared_ptr<ToolStrip> ToolStripPanel::remove(const ToolStrip& strip) {
    require_mutable();
    const Control::Ptr owner = strip.parent();
    if (!owner || owner.get() != this) return nullptr;
    const Control::Ptr removed = remove_child(strip.runtime_id());
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    return std::static_pointer_cast<ToolStrip>(removed);
}

Size ToolStripPanel::measure(Size available) {
    const bool along_height = vertical();
    // Each strip is measured with unbounded thickness and the panel's full
    // length; the panel is their thicknesses together.
    const double unbounded = std::numeric_limits<double>::max();
    const Size offered = along_height ? Size{unbounded, available.height} : Size{available.width, unbounded};
    double thickness = 0.0;
    for (const std::shared_ptr<ToolStrip>& strip : strips()) {
        if (!(*strip).visible()) continue;
        thickness += strip_thickness(*strip, along_height, offered);
    }
    if (along_height) {
        const Size result{std::min(available.width, thickness), available.height};
        return result;
    }
    const Size result{available.width, std::min(available.height, thickness)};
    return result;
}

void ToolStripPanel::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const bool along_height = vertical();
    const double unbounded = std::numeric_limits<double>::max();
    const Size offered = along_height ? Size{unbounded, final_bounds.height} : Size{final_bounds.width, unbounded};
    double next = 0.0;
    const std::vector<Control::Ptr> retained = snapshot_layout_children();
    for (const Control::Ptr& child : retained) {
        if (!is_current_layout_child(child) || !(*child).visible()) continue;
        ToolStrip* const strip = dynamic_cast<ToolStrip*>(child.get());
        if (strip == nullptr) continue;
        const double thickness = strip_thickness(*strip, along_height, offered);
        // Measuring runs the strip's program code, which may take it out of
        // the panel.
        if (!is_current_layout_child(child)) continue;
        const Rect slot = along_height ? Rect{next, 0.0, thickness, final_bounds.height}
                                       : Rect{0.0, next, final_bounds.width, thickness};
        set_child_layout(child, slot);
        next += thickness;
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

} // namespace gui_forms
