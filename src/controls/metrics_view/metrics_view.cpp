#include "gui_forms/controls/metrics_view/metrics_view.hpp"

#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gui_forms {

MetricsView::MetricsView(StableId stable_id, std::string initial_title)
    : Control(std::move(stable_id)) {
    title = std::move(initial_title);
}

void MetricsView::prepare_write(const decltype(title)&, std::string&) {
    require_mutable();
}

// The title is also the accessible name.
void MetricsView::written(const decltype(title)&) {
    accessible_name = title.read();
    invalidate(Dirty::paint | Dirty::semantics);
}

void MetricsView::set_style(BasicControlStyle style) {
    require_mutable();
    if (style_ == style) return;
    style_ = style;
    invalidate(Dirty::paint);
}

std::string MetricsView::metrics_text() const {
    if (window() == nullptr) return "Detached";
    const MetricsSnapshot metrics = (*window()).metrics_snapshot();
    std::ostringstream text;
    text << metrics.control_count << " controls · "
         << metrics.paint_passes << " paints · "
         << metrics.display_chunks_reused << " chunks reused · "
         << metrics.input_events << " inputs · "
         << metrics.focus_scope_depth << " focus scopes · damage "
         << static_cast<unsigned long long>(std::round(metrics.painted_damage_area))
         << " px";
    return text.str();
}

void MetricsView::on_paint(Painter& painter, Rect) {
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width,
                      committed_arranged_bounds().height};
    const double s = effective_text_scale();
    painter.fill_rect(bounds, style_.text);
    painter.fill_rect({0.0, 0.0, accent_width.read(), bounds.height}, style_.accent);
    painter.draw_text_utf8({16.0 * s, 24.0 * s}, title.read(),
                           effective_font({FontRole::control, 12.0, 700, false}),
                           style_.face_light);
    std::string metrics = metrics_text();
    if (metrics != painted_metrics_) {
        painted_metrics_ = std::move(metrics);
        // Semantics only: the generation advances, no repaint is owed.
        invalidate(Dirty::semantics);
    }
    const std::string& painted = painted_metrics_;
    const std::size_t split = painted.find(" · ", painted.size() / 2U);
    const std::string first = split == std::string::npos
        ? painted : painted.substr(0U, split);
    const std::string second = split == std::string::npos
        ? std::string{} : painted.substr(split + 3U);
    painter.draw_text_utf8({16.0 * s, 48.0 * s}, first,
                           effective_font({FontRole::content, 11.0, 400, false}),
                           style_.face);
    if (!second.empty()) {
        painter.draw_text_utf8({16.0 * s, 68.0 * s}, second,
                               effective_font({FontRole::content, 11.0, 400, false}),
                               style_.face);
    }
}

bool MetricsView::hit_test_local(Point) const { return false; }

SemanticDescriptor MetricsView::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    descriptor.name = title.read();
    descriptor.value = painted_metrics_;
    descriptor.exposed = true;
    return descriptor;
}

namespace {

constexpr std::array<PartField<MetricsView::Parts>, 4> metrics_view_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &MetricsView::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &MetricsView::Parts::face},
    {.declaration = {.name = "accent_strip", .role = PartRole::keyline, .composed_from = {}},
     .member = &MetricsView::Parts::accent_strip},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &MetricsView::Parts::focus},
}};

constexpr std::array<StateField<MetricsView::States>, 5> metrics_view_state_fields{{
    {.state = AnatomyState::normal, .member = &MetricsView::States::normal},
    {.state = AnatomyState::enabled, .member = &MetricsView::States::enabled},
    {.state = AnatomyState::disabled, .member = &MetricsView::States::disabled},
    {.state = AnatomyState::focused, .member = &MetricsView::States::focused},
    {.state = AnatomyState::focus_visible, .member = &MetricsView::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 4> metrics_view_parts =
    part_declarations(metrics_view_part_fields);
constexpr std::array<AnatomyState, 5> metrics_view_states =
    state_declarations(metrics_view_state_fields);

constexpr ControlAnatomy metrics_view_anatomy{
    .control_id = "metrics_view",
    .parts = metrics_view_parts,
    .states = metrics_view_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& MetricsView::anatomy() const noexcept {
    return metrics_view_anatomy;
}

void MetricsView::publish_part_geometry(std::span<PartGeometry> geometry) const {

    const Rect bounds = client_rectangle();
    const Rect strip{0.0, 0.0, accent_width.read(), bounds.height};
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    record.face = PartGeometry::from_bounds(bounds);
    record.accent_strip = PartGeometry::from_bounds(Rect::intersection(strip, bounds));
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), metrics_view_part_fields, record, geometry);
}

MetricsView::Parts MetricsView::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), metrics_view_part_fields, published.parts(), record);
    return record;
}

MetricsView::States MetricsView::states() const {
    States record;
    read_states(metrics_view_state_fields, anatomy_states(), record);
    return record;
}

// property_codemod begin MetricsView (tools/property_families/metrics_view.json; generated, do not edit)

void MetricsView::prepare_write(const decltype(accent_width)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 1.0 || incoming > 32.0) {
        throw std::invalid_argument("metrics accent width must be within [1, 32]");
    }
}

void MetricsView::written(const decltype(accent_width)&) {
    invalidate(Dirty::paint);
}

// property_codemod end MetricsView

} // namespace gui_forms
