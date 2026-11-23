#include "gui_forms/controls/pie_chart.hpp"

#include "../../basic/number_text.hpp"

#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/controls/plot_view_palette.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "stxlib/math.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <memory>
#include <new>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>

#include <stxlib/result.hpp>
#include <stxlib/unique.hpp>

namespace gui_forms {

struct PieChart::State final {
    // Paint scratch, reused for every slice of a paint pass.
    std::vector<Point> wedge_{};
    std::string paint_text_{};
    // The share as text: "42%", or "0.5%" under one percent.
    [[nodiscard]] static std::string share_text(const PieChart& self, std::size_t slice);
    [[nodiscard]] static stxlib::Optional<std::string> selected_copy(const PieChart& self);
    static void publish_selection(PieChart& self, stxlib::Optional<std::string> previous);
};

stxlib::Unique<PieChart::State> PieChart::make_state() {
    stxlib::OperationResult<stxlib::Unique<State>> made = stxlib::Unique<State>::make();
    if (made.failed()) throw std::bad_alloc();
    stxlib::Unique<State> state = std::move(made).take_value();
    return state;
}

namespace {

// The PieChartLabelStyle enumeration, numbered as declared. Static
// storage; any thread may read it.
std::shared_ptr<const PropertyEnumDescriptor> pie_chart_label_style_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        gui_forms::detail::make_library_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "pie_chart_label_style", {{"inside", 0}, {"outside", 1}, {"disabled", 2}}, false});
    return value;
}

// Metrics in logical units: the inset around the plot and the legend, the
// gap between them, the legend's width bounds and its swatch, the distance
// a selected slice moves out from the centre, and where an outside label
// sits (a fraction of the radius past the edge).
constexpr double chart_inset = 8.0;
constexpr double legend_gap = 12.0;
constexpr double legend_minimum_width = 80.0;
constexpr double legend_maximum_width = 220.0;
constexpr double legend_share = 0.38;
constexpr double swatch_side = 12.0;
constexpr double swatch_gap = 6.0;
constexpr double explode_distance = 6.0;
constexpr double outside_label_reach = 1.18;
constexpr double label_width = 48.0;
// About one arc segment per this many device pixels, and the bounds.
constexpr double arc_segment_pixels = 3.0;
constexpr std::size_t minimum_arc_segments = 2U;
constexpr std::size_t maximum_arc_segments = 720U;
constexpr double whole_turn = 2.0 * std::numbers::pi;

bool valid_slice(const PieSlice& slice) noexcept {
    const bool valid = valid_stable_id(slice.stable_id) && validate_utf8(slice.label).valid() &&
                       std::isfinite(slice.value) && slice.value >= 0.0;
    return valid;
}

constexpr std::array<PartField<PieChart::Parts>, 4> pie_chart_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}, .states = {}},
     .member = &PieChart::Parts::region},
    {.declaration = {.name = "plot", .role = PartRole::plot, .composed_from = {}, .states = {}},
     .member = &PieChart::Parts::plot},
    {.declaration = {.name = "legend", .role = PartRole::legend, .composed_from = {}, .states = {}},
     .member = &PieChart::Parts::legend},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}, .states = {}},
     .member = &PieChart::Parts::focus},
}};

constexpr std::array<PartField<PieChart::SliceParts>, 4> pie_chart_slice_part_fields{{
    {.declaration = {.name = "slice", .role = PartRole::series, .composed_from = {}, .states = {}},
     .member = &PieChart::SliceParts::slice},
    {.declaration = {.name = "slice_label", .role = PartRole::label, .composed_from = {}, .states = {}},
     .member = &PieChart::SliceParts::slice_label},
    {.declaration = {.name = "legend_swatch", .role = PartRole::well, .composed_from = {}, .states = {}},
     .member = &PieChart::SliceParts::legend_swatch},
    {.declaration = {.name = "legend_label", .role = PartRole::legend_entry, .composed_from = {}, .states = {}},
     .member = &PieChart::SliceParts::legend_label},
}};

constexpr std::array<StateField<PieChart::SliceStates>, 4> pie_chart_slice_state_fields{{
    {.state = AnatomyState::hot, .member = &PieChart::SliceStates::hot},
    {.state = AnatomyState::selected, .member = &PieChart::SliceStates::selected},
    {.state = AnatomyState::focus_visible, .member = &PieChart::SliceStates::focus_visible},
    {.state = AnatomyState::disabled, .member = &PieChart::SliceStates::disabled},
}};

constexpr std::array<PartDeclaration, 4> pie_chart_parts = part_declarations(pie_chart_part_fields);
constexpr std::array<PartDeclaration, 4> pie_chart_slice_parts = part_declarations(pie_chart_slice_part_fields);
constexpr std::array<AnatomyState, 4> pie_chart_slice_states = state_declarations(pie_chart_slice_state_fields);
constexpr std::array<AnatomyState, 5> pie_chart_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr ItemSystemIndex slice_system{0U};

constexpr std::array<ItemSystemDeclaration, 1> pie_chart_item_systems{{
    {.name = "slice", .parts = pie_chart_slice_parts, .states = pie_chart_slice_states, .custom_states = {}},
}};

constexpr ControlAnatomy pie_chart_anatomy{
    .control_id = "pie_chart",
    .parts = pie_chart_parts,
    .states = pie_chart_states,
    .custom_states = {},
    .item_systems = pie_chart_item_systems,
};

// The events PieChart adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> pie_chart_interactions{{
    {.name = "selection_changed", .payload = "PieSliceSelection", .mutable_payload = false},
    {.name = "slice_activated", .payload = "PieSliceActivation", .mutable_payload = false},
}};

Rect centred_box(Point centre, double half_width, double half_height) noexcept {
    const Rect box{centre.x - half_width, centre.y - half_height, half_width * 2.0, half_height * 2.0};
    return box;
}

} // namespace

PieChart::PieChart(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = true;
    set_paint_plane(PaintPlane::control);
}

// -- slices ---------------------------------------------------------------------------

void PieChart::set_slices(std::vector<PieSlice> slices) {
    require_mutable();
    if (slices.size() > maximum_slices) throw std::invalid_argument("PieChart has more slices than maximum_slices");
    for (std::size_t index = 0; index < slices.size(); ++index) {
        if (!valid_slice(slices[index])) {
            throw std::invalid_argument(
                "PieChart slices require stable IDs following the StableId segment rule, valid UTF-8 labels and "
                "finite values that are not negative");
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (slices[earlier].stable_id == slices[index].stable_id) {
                throw std::invalid_argument("PieChart slice IDs must be unique");
            }
        }
    }
    stxlib::Optional<std::string> previous = State::selected_copy(*this);
    slices_.swap(slices);
    selected_.reset();
    hot_.reset();
    if (previous.present()) {
        const stxlib::Optional<std::size_t> kept = index_of(*previous.value());
        if (kept.present()) selected_.set(*kept.value());
    }
    recompute_total();
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    if (previous != State::selected_copy(*this)) State::publish_selection(*this, std::move(previous));
}

void PieChart::set_slice_value(std::string_view slice_id, double value) {
    require_mutable();
    const stxlib::Optional<std::size_t> index = index_of(slice_id);
    if (index.absent()) throw std::out_of_range("PieChart slice value names an unknown slice");
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument("PieChart slice value must be finite and not negative");
    }
    if (slices_[*index.value()].value == value) return;
    slices_[*index.value()].value = value;
    recompute_total();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void PieChart::recompute_total() noexcept {
    double sum = 0.0;
    for (const PieSlice& slice : slices_) sum += slice.value;
    total_ = sum;
}

double PieChart::share(std::size_t slice) const {
    if (slice >= slices_.size()) throw std::out_of_range("PieChart share names a slice past the end");
    if (total_ <= 0.0) return 0.0;
    const double result = slices_[slice].value / total_;
    return result;
}

stxlib::Optional<std::size_t> PieChart::index_of(std::string_view slice_id) const {
    for (std::size_t index = 0; index < slices_.size(); ++index) {
        if (slices_[index].stable_id == slice_id) return stxlib::Optional<std::size_t>::of(index);
    }
    return stxlib::Optional<std::size_t>::none();
}

// -- selection ----------------------------------------------------------------------------

stxlib::Optional<std::string_view> PieChart::selected_id() const noexcept {
    if (selected_.absent()) return stxlib::Optional<std::string_view>::none();
    const std::string_view id(slices_[*selected_.value()].stable_id);
    return stxlib::Optional<std::string_view>::of(id);
}

stxlib::Optional<std::string> PieChart::State::selected_copy(const PieChart& self) {
    stxlib::Optional<std::string> copy{};
    if (self.selected_.present()) copy.set(self.slices_[*self.selected_.value()].stable_id);
    return copy;
}

void PieChart::set_selected_id(stxlib::Optional<std::string_view> slice_id) {
    require_mutable();
    stxlib::Optional<std::size_t> next{};
    if (slice_id.present()) {
        next = index_of(*slice_id.value());
        if (next.absent()) throw std::out_of_range("PieChart selection names an unknown slice");
    }
    if (next == selected_) return;
    stxlib::Optional<std::string> previous = State::selected_copy(*this);
    selected_ = next;
    invalidate(Dirty::paint | Dirty::semantics);
    State::publish_selection(*this, std::move(previous));
}

void PieChart::State::publish_selection(PieChart& self, stxlib::Optional<std::string> previous) {
    PieSliceSelection change{};
    change.previous_id = std::move(previous);
    change.current_id = State::selected_copy(self);
    self.publish_change(self.selection_changed_, std::move(change));
}

void PieChart::choose_slice(std::size_t slice) {
    if (slice >= slices_.size()) return;
    if (selected_ == stxlib::Optional<std::size_t>::of(slice)) return;
    stxlib::Optional<std::string> previous = State::selected_copy(*this);
    selected_.set(slice);
    invalidate(Dirty::paint | Dirty::semantics);
    State::publish_selection(*this, std::move(previous));
}

void PieChart::activate_slice(std::size_t slice) {
    if (slice >= slices_.size()) return;
    const PieSliceActivation activation{slices_[slice].stable_id};
    slice_activated_.emit(activation);
}

// -- geometry -------------------------------------------------------------------------------

Size PieChart::measure(Size available) {
    const Rect requested = requested_bounds();
    const double width = requested.width > 0.0 ? requested.width : 320.0;
    const double height = requested.height > 0.0 ? requested.height : 200.0;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

// The legend takes a share of the width at the trailing side (the right,
// or the left right to left, where the rectangle is mirrored), the pie the
// largest circle left.
PieChart::Layout PieChart::layout_for(Rect client) const {
    Layout placed;
    placed.mirrored = effective_layout_direction() == LayoutDirection::right_to_left;
    const ResolvedTextLayout metrics = resolve_text_layout_utf8("Mg", text_font(font.read()));
    placed.row_height = std::max(swatch_side, metrics.logical_size.height) + 4.0;
    placed.swatch = swatch_side;
    const Rect inner{client.x + chart_inset, client.y + chart_inset, std::max(0.0, client.width - chart_inset * 2.0),
                     std::max(0.0, client.height - chart_inset * 2.0)};
    double plot_width = inner.width;
    if (legend_visible.read() && !slices_.empty()) {
        const double wanted = std::clamp(inner.width * legend_share, legend_minimum_width, legend_maximum_width);
        const double legend_width = std::min(wanted, std::max(0.0, inner.width - legend_gap));
        const double rows_height = placed.row_height * static_cast<double>(slices_.size());
        const double legend_height = std::min(inner.height, rows_height);
        const double legend_x = inner.x + inner.width - legend_width;
        const double legend_y = inner.y + std::max(0.0, (inner.height - legend_height) * 0.5);
        placed.legend = {legend_x, legend_y, legend_width, legend_height};
        plot_width = std::max(0.0, inner.width - legend_width - legend_gap);
    }
    placed.plot = {inner.x, inner.y, plot_width, inner.height};
    placed.centre = {placed.plot.x + placed.plot.width * 0.5, placed.plot.y + placed.plot.height * 0.5};
    // Outside labels need room past the edge; a selected slice moves out.
    const double reach = label_style.read() == PieChartLabelStyle::outside ? outside_label_reach + 0.12 : 1.0;
    const double fit = std::min(placed.plot.width, placed.plot.height) * 0.5 / reach - explode_distance;
    placed.radius = std::max(0.0, fit);
    placed.inner_radius = placed.radius * doughnut_radius.read() / 100.0;
    if (placed.mirrored) {
        placed.legend.x = client.x * 2.0 + client.width - (placed.legend.x + placed.legend.width);
        placed.plot.x = client.x * 2.0 + client.width - (placed.plot.x + placed.plot.width);
        placed.centre.x = placed.plot.x + placed.plot.width * 0.5;
    }
    return placed;
}

double PieChart::slice_start(std::size_t slice) const noexcept {
    double angle = start_angle.read() * std::numbers::pi / 180.0;
    if (total_ <= 0.0) return angle;
    for (std::size_t earlier = 0; earlier < slice; ++earlier) angle += slices_[earlier].value / total_ * whole_turn;
    return angle;
}

double PieChart::slice_sweep(std::size_t slice) const noexcept {
    if (total_ <= 0.0) return 0.0;
    const double sweep = slices_[slice].value / total_ * whole_turn;
    return sweep;
}

// Clockwise from the top in reading order; right to left the angle runs
// counter-clockwise, the pie mirrored about its vertical.
Point PieChart::polar(const Layout& placed, double angle, double radius) const noexcept {
    const double across = stxlib::math::sin(angle) * radius;
    const double down = -stxlib::math::cos(angle) * radius;
    const double x = placed.mirrored ? placed.centre.x - across : placed.centre.x + across;
    const Point point{x, placed.centre.y + down};
    return point;
}

PieChart::SliceParts PieChart::slice_parts(const Layout& placed, std::size_t slice) const {
    SliceParts parts;
    const double sweep = slice_sweep(slice);
    const double middle = slice_start(slice) + sweep * 0.5;
    if (sweep > 0.0 && placed.radius > 0.0) {
        const Point centre = polar(placed, middle, (placed.radius + placed.inner_radius) * 0.5);
        const double half = (placed.radius - placed.inner_radius) * 0.5;
        parts.slice = PartGeometry::from_bounds(centred_box(centre, half, half));
        const ResolvedTextLayout metrics = resolve_text_layout_utf8("Mg", text_font(font.read()));
        const double half_height = metrics.logical_size.height * 0.5;
        if (label_style.read() == PieChartLabelStyle::inside) {
            parts.slice_label = PartGeometry::from_bounds(centred_box(centre, label_width * 0.5, half_height));
        } else if (label_style.read() == PieChartLabelStyle::outside) {
            const Point outside = polar(placed, middle, placed.radius * outside_label_reach);
            parts.slice_label = PartGeometry::from_bounds(centred_box(outside, label_width * 0.5, half_height));
        }
    }
    if (!placed.legend.empty()) {
        const double top = placed.legend.y + placed.row_height * static_cast<double>(slice);
        if (top + placed.row_height <= placed.legend.y + placed.legend.height + 0.5) {
            const double swatch_top = top + (placed.row_height - placed.swatch) * 0.5;
            const double label_left = placed.legend.x + placed.swatch + swatch_gap;
            Rect swatch{placed.legend.x, swatch_top, placed.swatch, placed.swatch};
            Rect label{label_left, top, std::max(0.0, placed.legend.width - placed.swatch - swatch_gap),
                       placed.row_height};
            if (placed.mirrored) {
                swatch.x = placed.legend.x + placed.legend.width - placed.swatch;
                label.x = placed.legend.x;
            }
            parts.legend_swatch = PartGeometry::from_bounds(swatch);
            parts.legend_label = PartGeometry::from_bounds(label);
        }
    }
    return parts;
}

AnatomyStateSet PieChart::slice_states(std::size_t slice) const {
    AnatomyStateSet states;
    const bool selected = selected_ == stxlib::Optional<std::size_t>::of(slice);
    if (hot_ == stxlib::Optional<std::size_t>::of(slice)) states.insert(AnatomyState::hot);
    if (selected) states.insert(AnatomyState::selected);
    if (!effectively_enabled()) states.insert(AnatomyState::disabled);
    const SurfaceRoot* const root = attached_surface_root();
    if (focused_ && selected && root != nullptr && (*root).focus_cue_visible()) {
        states.insert(AnatomyState::focus_visible);
    }
    return states;
}

Color PieChart::slice_color(std::size_t slice) const {
    if (slices_[slice].color.present()) return *slices_[slice].color.value();
    const Color themed = plot_series_color(*this, slice);
    return themed;
}

std::string PieChart::State::share_text(const PieChart& self, std::size_t slice) {
    const double percent = self.share(slice) * 100.0;
    std::string text;
    const int precision = percent > 0.0 && percent < 1.0 ? 1 : 0;
    number_text::append_number_text(text, percent, std::chars_format::fixed, precision);
    text += "%";
    return text;
}

// A pie slice is the centre and its outer arc; a doughnut slice the outer
// arc out and the inner arc back. A whole slice (one of one) is drawn as
// its arc alone, closed.
void PieChart::build_wedge(const Layout& placed, std::size_t slice, double scale) {
    State& state = *state_;
    state.wedge_.clear();
    const double sweep = slice_sweep(slice);
    const double start = slice_start(slice);
    const double middle = start + sweep * 0.5;
    const bool exploded = selected_ == stxlib::Optional<std::size_t>::of(slice) && sweep < whole_turn - 1e-9;
    Layout shifted = placed;
    if (exploded) {
        const Point moved = polar(placed, middle, explode_distance);
        shifted.centre = moved;
    }
    const double arc_pixels = sweep * placed.radius * scale;
    const double wanted = std::ceil(arc_pixels / arc_segment_pixels);
    const std::size_t segments =
        std::clamp(static_cast<std::size_t>(std::max(0.0, wanted)), minimum_arc_segments, maximum_arc_segments);
    const double step = sweep / static_cast<double>(segments);
    if (placed.inner_radius <= 0.0 && sweep < whole_turn - 1e-9) state.wedge_.push_back(shifted.centre);
    for (std::size_t point = 0; point <= segments; ++point) {
        state.wedge_.push_back(polar(shifted, start + step * static_cast<double>(point), placed.radius));
    }
    if (placed.inner_radius <= 0.0) return;
    for (std::size_t point = segments + 1U; point > 0U; --point) {
        state.wedge_.push_back(polar(shifted, start + step * static_cast<double>(point - 1U), placed.inner_radius));
    }
}

void PieChart::paint_slice(Painter& painter, const Layout& placed, std::size_t slice, double scale) {
    if (slice_sweep(slice) <= 0.0 || placed.radius <= 0.0) return;
    build_wedge(placed, slice, scale);
    const std::span<const Point> outline((*state_).wedge_.data(), (*state_).wedge_.size());
    painter.fill_polygon(outline, slice_color(slice), PolygonFillRule::even_odd);
}

void PieChart::on_paint(Painter& painter, Rect) {
    State& state = *state_;
    const Rect arranged = committed_arranged_bounds();
    const Layout placed = layout_for(Rect{0.0, 0.0, arranged.width, arranged.height});
    const SurfaceRoot* const root = attached_surface_root();
    const double scale = root != nullptr ? (*root).scale() : 1.0;
    const AnatomyStateSet states = base_anatomy_states();
    PartPaint part{};
    part.states = states;
    part.font = text_font(font.read());
    part.family = ControlVisualRole::editor;
    part.role = PartRole::plot;
    part.bounds = placed.plot;
    paint_part(painter, *this, part);
    for (std::size_t slice = 0; slice < slices_.size(); ++slice) paint_slice(painter, placed, slice, scale);
    part.family = ControlVisualRole::panel;
    if (!placed.legend.empty()) {
        part.role = PartRole::legend;
        part.bounds = placed.legend;
        paint_part(painter, *this, part);
    }
    for (std::size_t slice = 0; slice < slices_.size(); ++slice) {
        const SliceParts parts = slice_parts(placed, slice);
        part.states = slice_states(slice);
        if (parts.slice_label.present) {
            part.role = PartRole::label;
            part.bounds = parts.slice_label.bounds;
            part.alignment = PartTextAlignment::center;
            state.paint_text_ = State::share_text(*this, slice);
            part.text = lend_paint_text(state.paint_text_);
            paint_part(painter, *this, part);
            part.text = {};
        }
        if (parts.legend_swatch.present) {
            // The swatch is a fill showing the slice's colour inside a well
            // that frames it, as ColorPicker's.
            part.alignment = PartTextAlignment::start;
            part.family = ControlVisualRole::choice;
            part.role = PartRole::well;
            part.bounds = parts.legend_swatch.bounds;
            paint_part(painter, *this, part);
            part.role = PartRole::fill;
            part.content_color.set(slice_color(slice));
            paint_part(painter, *this, part);
            part.content_color.reset();
            part.family = ControlVisualRole::panel;
        }
        if (parts.legend_label.present) {
            part.role = PartRole::legend_entry;
            part.bounds = parts.legend_label.bounds;
            part.alignment = PartTextAlignment::start;
            state.paint_text_ = slices_[slice].label;
            state.paint_text_ += "  ";
            state.paint_text_ += State::share_text(*this, slice);
            part.text = lend_paint_text(state.paint_text_);
            paint_part(painter, *this, part);
            part.text = {};
        }
    }
    // The focus cue rings the selected slice's legend entry, or the plot
    // without a legend; the theme draws it only while focus is visible.
    part.states = states;
    part.role = PartRole::focus;
    part.bounds = focus_bounds(placed);
    paint_part(painter, *this, part);
}

Rect PieChart::focus_bounds(const Layout& placed) const {
    if (selected_.absent()) return placed.plot;
    const SliceParts selected = slice_parts(placed, *selected_.value());
    if (!selected.legend_label.present) return placed.plot;
    const Rect entry = Rect::united(selected.legend_swatch.bounds, selected.legend_label.bounds);
    return entry;
}

// The wedge under the point, then the legend entry under it.
stxlib::Optional<std::size_t> PieChart::slice_at(Point local) const {
    const Rect arranged = committed_arranged_bounds();
    const Layout placed = layout_for(Rect{0.0, 0.0, arranged.width, arranged.height});
    const double across = placed.mirrored ? placed.centre.x - local.x : local.x - placed.centre.x;
    const double down = local.y - placed.centre.y;
    const double distance = stxlib::math::hypot(across, down);
    if (total_ > 0.0 && distance <= placed.radius && distance >= placed.inner_radius) {
        // Clockwise from the top, then measured from the first slice's start.
        double angle = stxlib::math::atan2(across, -down) - start_angle.read() * std::numbers::pi / 180.0;
        angle = std::fmod(angle, whole_turn);
        if (angle < 0.0) angle += whole_turn;
        double reached = 0.0;
        for (std::size_t slice = 0; slice < slices_.size(); ++slice) {
            reached += slice_sweep(slice);
            if (slice_sweep(slice) > 0.0 && angle < reached) return stxlib::Optional<std::size_t>::of(slice);
        }
    }
    if (!placed.legend.empty() && placed.legend.contains(local) && placed.row_height > 0.0) {
        const std::size_t row = static_cast<std::size_t>((local.y - placed.legend.y) / placed.row_height);
        if (row < slices_.size()) return stxlib::Optional<std::size_t>::of(row);
    }
    return stxlib::Optional<std::size_t>::none();
}

// -- input -------------------------------------------------------------------------------------

void PieChart::on_mouse_down(MouseEventArgs& arguments) {
    Control::on_mouse_down(arguments);
    if (arguments.handled || !effectively_enabled() || arguments.button != PointerButton::primary) return;
    const Control::Ptr lifetime = shared_from_this();
    if (window() != nullptr) static_cast<void>((*window()).request_focus(lifetime));
    if (!is_alive()) return;
    arguments.handled = true;
    // The focus handlers may have replaced the slices: the press is hit
    // again against the chart as they left it.
    const stxlib::Optional<std::size_t> slice = slice_at(arguments.location);
    if (slice.absent()) return;
    choose_slice(*slice.value());
    if (is_alive() && arguments.clicks >= 2U) activate_slice(*slice.value());
}

void PieChart::on_mouse_move(MouseEventArgs& arguments) {
    Control::on_mouse_move(arguments);
    const stxlib::Optional<std::size_t> next = slice_at(arguments.location);
    if (next == hot_) return;
    hot_ = next;
    invalidate(Dirty::paint);
}

void PieChart::on_mouse_leave() {
    Control::on_mouse_leave();
    if (hot_.absent()) return;
    hot_.reset();
    invalidate(Dirty::paint);
}

// Right to left the slices run counter-clockwise, so Left and Right keep
// their sense along the legend: Down and Right step to the next slice.
void PieChart::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    if (arguments.handled || !focused_ || !effectively_enabled() || slices_.empty()) return;
    const std::size_t last = slices_.size() - 1U;
    const std::size_t from = selected_.present() ? *selected_.value() : 0U;
    const std::uint32_t key = arguments.physical_key;
    stxlib::Optional<std::size_t> target{};
    if (key == PhysicalKey::down || key == PhysicalKey::right) {
        target.set(selected_.absent() ? 0U : std::min(from + 1U, last));
    } else if (key == PhysicalKey::up || key == PhysicalKey::left) {
        target.set(from == 0U ? 0U : from - 1U);
    } else if (key == PhysicalKey::home) {
        target.set(0U);
    } else if (key == PhysicalKey::end) {
        target.set(last);
    } else if (key == PhysicalKey::enter) {
        if (selected_.present()) activate_slice(from);
        arguments.handled = true;
        return;
    }
    if (target.absent()) return;
    arguments.handled = true;
    choose_slice(*target.value());
}

void PieChart::on_focus_changed(bool focused) {
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- semantics -------------------------------------------------------------------------------------

SemanticDescriptor PieChart::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::list;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    if (selected_.present()) descriptor.value = slices_[*selected_.value()].label;
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> PieChart::semantic_virtual_children() const {
    std::vector<SemanticNode> children{};
    children.reserve(slices_.size());
    const Rect arranged = committed_arranged_bounds();
    const Layout placed = layout_for(Rect{0.0, 0.0, arranged.width, arranged.height});
    const Rect absolute = absolute_bounds();
    const SemanticState enabled_state = effectively_enabled() ? SemanticState::enabled : SemanticState::none;
    for (std::size_t slice = 0; slice < slices_.size(); ++slice) {
        const SliceParts parts = slice_parts(placed, slice);
        const Rect bounds = parts.legend_label.present ? parts.legend_label.bounds : parts.slice.bounds;
        SemanticNode node{};
        node.stable_id = slices_[slice].stable_id;
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::list_item;
        node.name = slices_[slice].label;
        std::string value;
        number_text::append_number_text(value, slices_[slice].value, std::chars_format::general, 6);
        value += ", ";
        value += State::share_text(*this, slice);
        node.value = value;
        node.position_in_set.set(slice + 1U);
        node.set_size.set(slices_.size());
        node.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
        node.states = SemanticState::visible | SemanticState::focusable | enabled_state;
        if (selected_ == stxlib::Optional<std::size_t>::of(slice)) node.states |= SemanticState::selected;
        if (focused_ && selected_ == stxlib::Optional<std::size_t>::of(slice)) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::focus, SemanticAction::select, SemanticAction::press};
        children.push_back(std::move(node));
    }
    return children;
}

bool PieChart::on_semantic_child_action(std::string_view id, SemanticAction action, std::string_view) {
    if (!effectively_enabled()) return false;
    if (index_of(id).absent()) return false;
    if (action != SemanticAction::focus && action != SemanticAction::select && action != SemanticAction::press) {
        return false;
    }
    const Control::Ptr lifetime = shared_from_this();
    if (window() != nullptr) static_cast<void>((*window()).request_focus(lifetime));
    if (!is_alive()) return true;
    // The focus handlers may have replaced the slices: the slice is looked
    // up again by its identity.
    const stxlib::Optional<std::size_t> slice = index_of(id);
    if (slice.absent()) return true;
    choose_slice(*slice.value());
    if (action == SemanticAction::press && is_alive()) activate_slice(*slice.value());
    return true;
}

// -- anatomy ------------------------------------------------------------------------------------------

const ControlAnatomy& PieChart::anatomy() const noexcept {
    return pie_chart_anatomy;
}

void PieChart::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect arranged = committed_arranged_bounds();
    const Rect client{0.0, 0.0, arranged.width, arranged.height};
    const Layout placed = layout_for(client);
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    record.plot = PartGeometry::from_bounds(placed.plot);
    record.legend = PartGeometry::from_bounds(placed.legend);
    record.focus = PartGeometry::from_bounds(focus_bounds(placed));
    write_parts(anatomy(), pie_chart_part_fields, record, geometry);
}

PieChart::Parts PieChart::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), pie_chart_part_fields, published.parts(), record);
    return record;
}

std::size_t PieChart::realized_item_count() const {
    return slices_.size();
}

std::string PieChart::realized_item_stable_id(std::size_t item) const {
    if (item >= slices_.size()) throw std::out_of_range("PieChart realized item is past the slices");
    return slices_[item].stable_id;
}

AnatomyStateSet PieChart::realized_item_anatomy_states(std::size_t item) const {
    if (item >= slices_.size()) throw std::out_of_range("PieChart realized item is past the slices");
    return slice_states(item);
}

ItemSystemIndex PieChart::publish_realized_item_system(std::size_t item) const {
    if (item >= slices_.size()) throw std::out_of_range("PieChart realized item is past the slices");
    return slice_system;
}

void PieChart::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    if (item >= slices_.size()) throw std::out_of_range("PieChart realized item is past the slices");
    const Rect arranged = committed_arranged_bounds();
    const Layout placed = layout_for(Rect{0.0, 0.0, arranged.width, arranged.height});
    const SliceParts record = slice_parts(placed, item);
    write_item_parts(anatomy().item_systems[slice_system.value], pie_chart_slice_part_fields, record, geometry);
}

std::vector<PieChart::Slice> PieChart::realized_slices() const {
    std::vector<Slice> records;
    records.reserve(slices_.size());
    for (std::size_t item = 0; item < slices_.size(); ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Slice record;
        record.index = item;
        record.stable_id = slices_[item].stable_id;
        read_item_parts(anatomy().item_systems[slice_system.value], pie_chart_slice_part_fields, published.parts(),
                        record.parts);
        read_states(pie_chart_slice_state_fields, slice_states(item), record.states);
        records.push_back(record);
    }
    return records;
}

void PieChart::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), pie_chart_interactions.begin(), pie_chart_interactions.end());
}

// property_codemod begin PieChart (tools/property_families/pie_chart.json; generated, do not edit)

namespace {

PropertyDescriptor pie_chart_doughnut_radius_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "doughnut_radius";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Appearance";
    descriptor.description = "The hole's radius as a percentage of the pie's; 0 draws a pie.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::hit_test | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_pie_chart_doughnut_radius() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        pie_chart_doughnut_radius_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor pie_chart_start_angle_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "start_angle";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Appearance";
    descriptor.description = "Where the first slice starts, in degrees clockwise from the top.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::hit_test | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_pie_chart_start_angle() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        pie_chart_start_angle_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor pie_chart_label_style_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "label_style";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "Where each slice's share is written.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"pie_chart_label_style", "inside", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::hit_test | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = pie_chart_label_style_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_pie_chart_label_style() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        pie_chart_label_style_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor pie_chart_legend_visible_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "legend_visible";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether the legend lists the slices.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_pie_chart_legend_visible() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        pie_chart_legend_visible_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<PieChartLabelStyle>::type() {
    return PropertyType{BindingValueKind::enumeration, false, pie_chart_label_style_enum()};
}

BindingValue PropertyValueTraits<PieChartLabelStyle>::to_binding(PieChartLabelStyle value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = pie_chart_label_style_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

PieChartLabelStyle PropertyValueTraits<PieChartLabelStyle>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<PieChartLabelStyle>(choice.value);
}

const std::array<PropertyEntry, PieChart::pie_chart_property_count>& PieChart::pie_chart_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 4U> own{{
        {"doughnut_radius", &describe_pie_chart_doughnut_radius,
         &read_cell_entry<PieChart, decltype(&PieChart::doughnut_radius), &PieChart::doughnut_radius>,
         &write_cell_entry<PieChart, decltype(&PieChart::doughnut_radius), &PieChart::doughnut_radius>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"start_angle", &describe_pie_chart_start_angle,
         &read_cell_entry<PieChart, decltype(&PieChart::start_angle), &PieChart::start_angle>,
         &write_cell_entry<PieChart, decltype(&PieChart::start_angle), &PieChart::start_angle>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"label_style", &describe_pie_chart_label_style,
         &read_cell_entry<PieChart, decltype(&PieChart::label_style), &PieChart::label_style>,
         &write_cell_entry<PieChart, decltype(&PieChart::label_style), &PieChart::label_style>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"legend_visible", &describe_pie_chart_legend_visible,
         &read_cell_entry<PieChart, decltype(&PieChart::legend_visible), &PieChart::legend_visible>,
         &write_cell_entry<PieChart, decltype(&PieChart::legend_visible), &PieChart::legend_visible>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "PieChart's property table names a property twice");
    static const std::array<PropertyEntry, pie_chart_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> PieChart::property_table() const noexcept {
    const std::array<PropertyEntry, pie_chart_property_count>& entries = pie_chart_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void PieChart::prepare_write(const decltype(doughnut_radius)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.0 || incoming > maximum_doughnut_radius) {
        throw std::invalid_argument("PieChart doughnut_radius must be from 0 to 90");
    }
}

void PieChart::written(const decltype(doughnut_radius)&) {
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void PieChart::prepare_write(const decltype(start_angle)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.0 || incoming >= 360.0) {
        throw std::invalid_argument("PieChart start_angle must be from 0 to below 360");
    }
}

void PieChart::written(const decltype(start_angle)&) {
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void PieChart::prepare_write(const decltype(label_style)&, PieChartLabelStyle& incoming) {
    require_mutable();
    if (incoming != PieChartLabelStyle::inside && incoming != PieChartLabelStyle::outside &&
        incoming != PieChartLabelStyle::disabled) {
        throw std::invalid_argument("PieChart label_style is outside PieChartLabelStyle");
    }
}

void PieChart::written(const decltype(label_style)&) {
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void PieChart::prepare_write(const decltype(legend_visible)&, bool&) {
    require_mutable();
}

void PieChart::written(const decltype(legend_visible)&) {
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void PieChart::prepare_write(const decltype(font)&, FontSpec& incoming) {
    require_mutable();
    if (!valid_font_spec(incoming)) throw std::invalid_argument("PieChart font specification is invalid");
    set_font_from_code(true);
}

void PieChart::written(const decltype(font)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    on_font_changed();
}

void PieChart::on_font_changed() {}

// property_codemod end PieChart

} // namespace gui_forms
