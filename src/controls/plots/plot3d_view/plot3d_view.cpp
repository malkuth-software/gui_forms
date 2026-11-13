#include "gui_forms/controls/plot3d_view.hpp"

#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {

namespace {

// The size a plot asks for when its bounds request none.
constexpr double preferred_width = 360.0;
constexpr double preferred_height = 300.0;
// The plot's inset from the control's edges, and the readout's band.
constexpr double plot_inset = 2.0;
constexpr double readout_height = 16.0;
constexpr double readout_margin = 4.0;
constexpr double label_height = 14.0;
// Samples per pixel side while a gesture moves the picture, and at rest.
constexpr std::size_t moving_supersample = 1U;
constexpr std::size_t resting_supersample = 2U;
// Keyboard steps: degrees per arrow, logical units per Shift + arrow, the
// zoom per + or -.
constexpr double key_turn_degrees = 5.0;
constexpr double key_pan_step = 20.0;
constexpr double key_zoom_step = 1.25;
// The wheel's zoom per notch, and the logical pixels a precise device
// scrolls per notch.
constexpr double wheel_zoom_step = 1.1;
constexpr double wheel_pixels_per_notch = 40.0;
// The panes' and grid's opacity over the plot, from the rule colour.
constexpr std::array<std::uint8_t, 3> pane_alpha{16U, 24U, 32U};
constexpr std::uint8_t grid_alpha = 96U;
// Series colours step through the accent ramp as PlotView's do.
constexpr std::size_t series_accent_first = 5U;
constexpr std::size_t series_accent_stride = 3U;

constexpr std::array<PartField<Plot3DView::Parts>, 8> plot3d_view_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &Plot3DView::Parts::region},
    {.declaration = {.name = "plot", .role = PartRole::plot, .composed_from = {}},
     .member = &Plot3DView::Parts::plot},
    {.declaration = {.name = "picture", .role = PartRole::image, .composed_from = {}},
     .member = &Plot3DView::Parts::picture},
    {.declaration = {.name = "x_axis", .role = PartRole::axis, .composed_from = {}},
     .member = &Plot3DView::Parts::x_axis},
    {.declaration = {.name = "y_axis", .role = PartRole::axis, .composed_from = {}},
     .member = &Plot3DView::Parts::y_axis},
    {.declaration = {.name = "z_axis", .role = PartRole::axis, .composed_from = {}},
     .member = &Plot3DView::Parts::z_axis},
    {.declaration = {.name = "readout", .role = PartRole::label, .composed_from = {}},
     .member = &Plot3DView::Parts::readout},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &Plot3DView::Parts::focus},
}};

constexpr std::array<StateField<Plot3DView::States>, 5> plot3d_view_state_fields{{
    {.state = AnatomyState::normal, .member = &Plot3DView::States::normal},
    {.state = AnatomyState::enabled, .member = &Plot3DView::States::enabled},
    {.state = AnatomyState::disabled, .member = &Plot3DView::States::disabled},
    {.state = AnatomyState::focused, .member = &Plot3DView::States::focused},
    {.state = AnatomyState::focus_visible, .member = &Plot3DView::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 8> plot3d_view_parts = part_declarations(plot3d_view_part_fields);
constexpr std::array<AnatomyState, 5> plot3d_view_states = state_declarations(plot3d_view_state_fields);

constexpr ControlAnatomy plot3d_view_anatomy{
    .control_id = "plot3d_view",
    .parts = plot3d_view_parts,
    .states = plot3d_view_states,
    .custom_states = {},
};

// The events Plot3DView adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 3> plot3d_view_interactions{{
    {.name = "hovered", .payload = "plots::Plot3DHit", .mutable_payload = false},
    {.name = "picked", .payload = "plots::Plot3DHit", .mutable_payload = false},
    {.name = "camera_changed", .payload = "plots::Plot3DCamera", .mutable_payload = false},
}};

// Tick label widths as the painter measures them in the labels' font.
class PainterWidthMetrics final : public plots::TickLabelMetrics {
public:
    PainterWidthMetrics(Painter& painter, FontSpec font) noexcept : painter_(painter), font_(font) {}

    [[nodiscard]] double label_extent(std::string_view label) const override {
        const Size measured = painter_.measure_text_utf8(label, font_);
        return measured.width;
    }

private:
    Painter& painter_;
    FontSpec font_;
};

[[nodiscard]] Color with_alpha(Color color, std::uint8_t alpha) noexcept {
    Color result = color;
    result.alpha = alpha;
    return result;
}

// The colour of the plot's rules: the editor's border, or its top edge,
// else its text, for a theme that draws it unbordered (as PlotView).
[[nodiscard]] Color rule_colour(const ControlVisualRecipe& recipe) noexcept {
    if (recipe.material.border) return (*recipe.material.border).color;
    if (recipe.material.border_edges.top) return (*recipe.material.border_edges.top).color;
    return recipe.text;
}

[[nodiscard]] bool same_hit(const plots::Plot3DHit& left, const plots::Plot3DHit& right) noexcept {
    return left.kind == right.kind && left.item == right.item && left.index == right.index &&
           left.data_point == right.data_point;
}

[[nodiscard]] bool same_frame(const plots::Plot3DFrame& left, const plots::Plot3DFrame& right) noexcept {
    return left.width == right.width && left.height == right.height && left.scale == right.scale &&
           left.supersample == right.supersample;
}

} // namespace

Plot3DView::Plot3DView(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = true;
}

Plot3DView::~Plot3DView() = default;

// -- items ---------------------------------------------------------------------------

plots::Plot3DItemId Plot3DView::add_surface(const plots::Plot3DSurfaceSpec& spec) {
    require_mutable();
    const plots::Plot3DItemId added = scene_.add_surface(spec);
    picture_stale_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    return added;
}

plots::Plot3DItemId Plot3DView::add_line(const plots::Plot3DLineSpec& spec) {
    require_mutable();
    const plots::Plot3DItemId added = scene_.add_line(spec);
    picture_stale_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    return added;
}

plots::Plot3DItemId Plot3DView::add_scatter(const plots::Plot3DScatterSpec& spec) {
    require_mutable();
    const plots::Plot3DItemId added = scene_.add_scatter(spec);
    picture_stale_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    return added;
}

bool Plot3DView::remove_item(plots::Plot3DItemId item) {
    require_mutable();
    const bool removed = scene_.remove(item);
    if (!removed) return false;
    picture_stale_ = true;
    clear_hover();
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

void Plot3DView::clear_items() {
    require_mutable();
    scene_.clear();
    picture_stale_ = true;
    clear_hover();
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- view ----------------------------------------------------------------------------

void Plot3DView::camera_moved() {
    picture_stale_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    // A copy: a handler may move the camera again.
    const plots::Plot3DCamera moved = scene_.camera();
    camera_changed_.emit(moved);
}

bool Plot3DView::set_camera(const plots::Plot3DCamera& camera) {
    require_mutable();
    const bool changed = scene_.set_camera(camera);
    if (changed) camera_moved();
    return changed;
}

bool Plot3DView::reset_view() {
    require_mutable();
    const bool changed = scene_.reset_camera();
    if (changed) camera_moved();
    return changed;
}

void Plot3DView::apply_options(const std::array<std::string, 3>& titles, const std::array<double, 3>& aspect) {
    plots::Plot3DOptions options{};
    options.box_aspect = aspect;
    options.show_panes = show_panes.read();
    options.show_grid = show_grid.read();
    options.show_axes = show_axes.read();
    options.label_alignment = labels_along_axis.read() ? plots::Plot3DLabelAlignment::along_axis
                                                       : plots::Plot3DLabelAlignment::screen;
    options.axis_titles = titles;
    if (options == scene_.options()) return;
    scene_.set_options(options);
    picture_stale_ = true;
    invalidate(Dirty::paint);
}

void Plot3DView::set_axis_title(plots::Plot3DAxis axis, std::string_view title) {
    require_mutable();
    std::array<std::string, 3> titles = scene_.options().axis_titles;
    titles[static_cast<std::size_t>(axis)] = std::string(title);
    apply_options(titles, scene_.options().box_aspect);
}

std::string_view Plot3DView::axis_title(plots::Plot3DAxis axis) const noexcept {
    const std::string_view title = scene_.options().axis_titles[static_cast<std::size_t>(axis)];
    return title;
}

void Plot3DView::set_box_aspect(const std::array<double, 3>& aspect) {
    require_mutable();
    for (const double side : aspect) {
        if (!std::isfinite(side) || !(side > 0.0)) throw std::invalid_argument("Plot3DView box aspect must be positive and finite");
    }
    apply_options(scene_.options().axis_titles, aspect);
}

void Plot3DView::sync_colors(AnatomyStateSet states) {
    const ControlVisualContext context = part_visual_context(*this, states);
    const ControlVisualRecipe& recipe = effective_theme().resolve(ControlVisualRole::editor, context);
    const Color rule = rule_colour(recipe);
    plots::Plot3DColors colors{};
    for (std::size_t axis = 0U; axis < 3U; ++axis) colors.panes[axis] = with_alpha(rule, pane_alpha[axis]);
    colors.grid = with_alpha(rule, grid_alpha);
    colors.axis = recipe.text;
    for (std::size_t position = 0U; position < plots::Plot3DColors::series_count; ++position) {
        const std::size_t step = (position * series_accent_stride + series_accent_first) % 256U;
        colors.series[position] = with_alpha(effective_theme().accent(AccentIndex{static_cast<std::uint8_t>(step)}), 255U);
    }
    if (colors == scene_.colors()) return;
    scene_.set_colors(colors);
    picture_stale_ = true;
}

// -- picking -------------------------------------------------------------------------

plots::Plot3DHit Plot3DView::hit_at(Point local_point) const {
    const Rect plot = plot_bounds();
    const Point inside{local_point.x - plot.x, local_point.y - plot.y};
    const plots::Plot3DHit found = scene_.hit(inside);
    return found;
}

void Plot3DView::update_hover(Point plot_point) {
    const plots::Plot3DHit found = scene_.hit(plot_point);
    if (same_hit(found, hover_)) return;
    hover_ = found;
    readout_ = scene_.readout(found);
    invalidate(Dirty::paint | Dirty::semantics);
    const plots::Plot3DHit raised = hover_;
    hovered_.emit(raised);
}

void Plot3DView::clear_hover() {
    if (hover_.kind == plots::Plot3DHitKind::none && readout_.empty()) return;
    hover_ = plots::Plot3DHit{};
    readout_.clear();
    invalidate(Dirty::paint | Dirty::semantics);
    const plots::Plot3DHit raised = hover_;
    hovered_.emit(raised);
}

std::string Plot3DView::angle_readout() const {
    const plots::Plot3DCamera& camera = scene_.camera();
    std::array<char, 96> text{};
    const int written = std::snprintf(text.data(), text.size(), "azimuth=%.0f\xc2\xb0, elevation=%.0f\xc2\xb0, roll=%.0f\xc2\xb0",
                                      camera.azimuth, camera.elevation, camera.roll);
    const std::size_t length = written > 0 ? std::min(static_cast<std::size_t>(written), text.size() - 1U) : 0U;
    std::string result(text.data(), length);
    return result;
}

// -- geometry and paint --------------------------------------------------------------

Size Plot3DView::measure(Size available) {
    const Rect requested = requested_bounds();
    const Size preferred{requested.width > 0.0 ? requested.width : preferred_width,
                         requested.height > 0.0 ? requested.height : preferred_height};
    return {std::min(available.width, preferred.width), std::min(available.height, preferred.height)};
}

Rect Plot3DView::plot_bounds() const noexcept {
    const Rect arranged = committed_arranged_bounds();
    const Rect plot{plot_inset, plot_inset, std::max(0.0, arranged.width - plot_inset * 2.0),
                    std::max(0.0, arranged.height - plot_inset * 2.0)};
    return plot;
}

Rect Plot3DView::readout_bounds() const noexcept {
    const Rect plot = plot_bounds();
    const Rect band{plot.x + readout_margin, plot.y + std::max(0.0, plot.height - readout_height - readout_margin),
                    std::max(0.0, plot.width - readout_margin * 2.0), readout_height};
    return band;
}

Rect Plot3DView::axis_bounds(plots::Plot3DAxis axis) const noexcept {
    const Rect plot = plot_bounds();
    const std::span<const plots::Plot3DLabel> labels = scene_.labels();
    bool found = false;
    Rect united{};
    for (const plots::Plot3DLabel& label : labels) {
        if (label.axis != axis) continue;
        const Rect around{plot.x + label.anchor.x - label_height, plot.y + label.anchor.y - label_height * 0.5,
                          label_height * 2.0, label_height};
        united = found ? Rect::united(united, around) : around;
        found = true;
    }
    return united;
}

double Plot3DView::device_scale() const noexcept {
    const SurfaceRoot* const root = surface_root();
    const double scale = root != nullptr ? (*root).scale() : 1.0;
    return scale;
}

bool Plot3DView::render_picture(const plots::Plot3DFrame& frame) {
    const DeviceExtent extent = plots::Plot3DScene::device_extent(frame);
    if (extent.width == 0U || extent.height == 0U) {
        picture_.reset();
        return false;
    }
    // The kept raster is written again only while nothing else (a
    // recording of an earlier paint) still holds it.
    const bool reusable = picture_ && picture_.use_count() == 1 && (*picture_).extent() == extent &&
                          (*picture_).scale() == frame.scale;
    std::shared_ptr<PartRaster> target = reusable ? picture_ : PartRaster::create(extent, frame.scale);
    if (!target) return false;
    const PartRasterView destination = (*target).view();
    const plots::Plot3DCheck check = scene_.render(frame, destination);
    (*target).record_opacity();
    if (check != plots::Plot3DCheck::ok) return false;
    picture_ = std::move(target);
    return true;
}

void Plot3DView::paint_labels(Painter& painter, AnatomyStateSet states) {
    const Rect plot = plot_bounds();
    const FontSpec label_font = effective_font(FontSpec{FontRole::control, 11.0, 400, false});
    const std::span<const plots::Plot3DLabel> labels = scene_.labels();
    PartPaint part{};
    part.states = states;
    part.family = ControlVisualRole::panel;
    part.font = label_font;
    part.alignment = PartTextAlignment::center;
    for (const plots::Plot3DLabel& label : labels) {
        const bool tick = label.role == plots::Plot3DLabelRole::tick;
        const std::string_view text = tick ? label.text.text() : axis_title(label.axis);
        if (text.empty()) continue;
        const double text_width = painter.measure_text_utf8(text, label_font).width + 2.0;
        const double centre_x = plot.x + label.anchor.x;
        const double centre_y = plot.y + label.anchor.y;
        part.role = tick ? PartRole::tick_label : PartRole::caption;
        part.tone = label.occluded ? PartTextTone::secondary : PartTextTone::ordinary;
        part.text = lend_paint_text(text);
        // Laid out level and centred on the anchor, then turned about it
        // (an along-axis title follows its edge).
        part.bounds = Rect{centre_x - text_width * 0.5, centre_y - label_height * 0.5, text_width, label_height};
        part.orientation = TextOrientation::horizontal;
        part.text_angle = label.angle;
        paint_part(painter, *this, part);
    }
    part.text_angle = 0.0;
    if (readout_.empty()) return;
    part.role = PartRole::label;
    part.tone = PartTextTone::secondary;
    part.orientation = TextOrientation::horizontal;
    part.alignment = PartTextAlignment::start;
    part.bounds = readout_bounds();
    part.text = lend_paint_text(readout_);
    paint_part(painter, *this, part);
}

void Plot3DView::on_paint(Painter& painter, Rect) {
    const AnatomyStateSet states = anatomy_states();
    const Rect plot = plot_bounds();
    PartPaint part{};
    part.states = states;
    part.family = ControlVisualRole::editor;
    part.role = PartRole::plot;
    part.bounds = plot;
    paint_part(painter, *this, part);
    if (plot.width <= 0.0 || plot.height <= 0.0) return;
    sync_colors(states);
    const FontSpec label_font = effective_font(FontSpec{FontRole::control, 11.0, 400, false});
    const PainterWidthMetrics metrics(painter, label_font);
    plots::Plot3DFrame frame{};
    frame.width = plot.width;
    frame.height = plot.height;
    frame.scale = DeviceScale{device_scale()};
    frame.supersample = drag_ == Drag::none ? resting_supersample : moving_supersample;
    frame.metrics = &metrics;
    if (picture_stale_ || !picture_ || !same_frame(frame, drawn_frame_)) {
        if (render_picture(frame)) {
            drawn_frame_ = frame;
            // The metrics are this paint's; the kept frame holds none.
            drawn_frame_.metrics = nullptr;
            picture_stale_ = false;
        }
    }
    if (picture_) {
        // The kept picture, drawn as given by the theme's image role.
        PartPaint picture{};
        picture.states = states;
        picture.family = ControlVisualRole::editor;
        picture.role = PartRole::image;
        picture.bounds = plot;
        picture.raster = picture_;
        paint_part(painter, *this, picture);
    }
    paint_labels(painter, states);
    if (states.contains(AnatomyState::focus_visible)) {
        part.role = PartRole::focus;
        part.bounds = Rect{plot.x - 2.0, plot.y - 2.0, plot.width + 4.0, plot.height + 4.0};
        paint_part(painter, *this, part);
    }
}

bool Plot3DView::hit_test_local(Point local_point) const {
    const Rect arranged = committed_arranged_bounds();
    const Rect local{0.0, 0.0, arranged.width, arranged.height};
    return local.contains(local_point);
}

// -- input ---------------------------------------------------------------------------

void Plot3DView::handle_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    const Rect absolute = absolute_bounds();
    const Rect plot = plot_bounds();
    const Point inside{event.position.x - absolute.x - plot.x, event.position.y - absolute.y - plot.y};
    switch (event.action) {
    case PointerAction::down: {
        if (surface_root() != nullptr) static_cast<void>((*surface_root()).request_focus(shared_from_this()));
        if (event.button == PointerButton::primary && event.click_count >= 2U) {
            drag_ = Drag::none;
            static_cast<void>(reset_view());
            event.handled = true;
            return;
        }
        Drag chosen = Drag::none;
        if (event.button == PointerButton::primary) {
            chosen = has_modifier(event.modifiers, Modifier::shift) ? Drag::pan : Drag::rotate;
        }
        if (event.button == PointerButton::secondary) chosen = Drag::zoom;
        if (event.button == PointerButton::middle) chosen = Drag::pan;
        if (chosen == Drag::none) return;
        drag_ = chosen;
        dragged_ = false;
        last_pointer_ = inside;
        set_pointer_capture(true);
        event.handled = true;
        return;
    }
    case PointerAction::move: {
        if (drag_ == Drag::none) {
            update_hover(inside);
            return;
        }
        const double dx = inside.x - last_pointer_.x;
        const double dy = inside.y - last_pointer_.y;
        last_pointer_ = inside;
        event.handled = true;
        if (dx == 0.0 && dy == 0.0) return;
        dragged_ = true;
        bool changed = false;
        switch (drag_) {
        case Drag::rotate:
            changed = scene_.rotate_by(dx, dy);
            readout_ = angle_readout();
            break;
        case Drag::pan:
            changed = scene_.pan_by(dx, dy);
            break;
        case Drag::zoom: {
            // Up zooms in: a doubling per half the plot's height.
            const double factor = std::exp2(-dy / std::max(plot.height * 0.5, 1.0));
            changed = scene_.zoom_by(factor, std::nullopt);
            break;
        }
        case Drag::none:
        default:
            break;
        }
        if (changed) camera_moved();
        return;
    }
    case PointerAction::up: {
        if (drag_ == Drag::none) return;
        const Drag ended = drag_;
        drag_ = Drag::none;
        set_pointer_capture(false);
        event.handled = true;
        // The resting frame samples 2 x 2: the next paint renders again.
        invalidate(Dirty::paint);
        if (!dragged_ && ended == Drag::rotate && event.button == PointerButton::primary) {
            const plots::Plot3DHit found = scene_.hit(inside);
            if (found.kind != plots::Plot3DHitKind::none) picked_.emit(found);
            if (!is_alive()) return;
        }
        // The readout showed the angles while turning: back to the point.
        clear_hover();
        update_hover(inside);
        return;
    }
    case PointerAction::wheel: {
        if (!wheel_zoom.read()) return;
        const double notches = event.wheel_unit == WheelDeltaUnit::pixels ? event.wheel_delta.y / wheel_pixels_per_notch
                                                                          : event.wheel_delta.y;
        if (notches == 0.0 || !std::isfinite(notches)) return;
        event.handled = true;
        if (scene_.zoom_by(std::pow(wheel_zoom_step, notches), inside)) camera_moved();
        return;
    }
    case PointerAction::leave:
        if (drag_ == Drag::none) clear_hover();
        return;
    case PointerAction::enter:
    default:
        return;
    }
}

// The mouse raisers hand the target phase to handle_pointer, which reads
// the routed form (as PlotView's).
void Plot3DView::on_mouse_down(MouseEventArgs& arguments) {
    PointerEvent event = pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    if (!is_alive()) return;
    Control::on_mouse_down(arguments);
}

void Plot3DView::on_mouse_up(MouseEventArgs& arguments) {
    PointerEvent event = pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    if (!is_alive()) return;
    Control::on_mouse_up(arguments);
}

void Plot3DView::on_mouse_move(MouseEventArgs& arguments) {
    PointerEvent event = pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    if (!is_alive()) return;
    Control::on_mouse_move(arguments);
}

void Plot3DView::on_mouse_wheel(MouseEventArgs& arguments) {
    PointerEvent event = pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    if (!is_alive()) return;
    Control::on_mouse_wheel(arguments);
}

void Plot3DView::on_mouse_leave() {
    PointerEvent leave{};
    leave.action = PointerAction::leave;
    handle_pointer(leave);
    if (!is_alive()) return;
    Control::on_mouse_leave();
}

void Plot3DView::handle_key(KeyEvent& event) {
    if (!eligible_for_input() || event.action != KeyAction::down) return;
    const bool shift = has_modifier(event.modifiers, Modifier::shift);
    plots::Plot3DCamera next = scene_.camera();
    bool changed = false;
    switch (event.physical_key) {
    case PhysicalKey::left:
        next.azimuth = next.azimuth + key_turn_degrees;
        changed = shift ? scene_.pan_by(-key_pan_step, 0.0) : scene_.set_camera(next);
        break;
    case PhysicalKey::right:
        next.azimuth = next.azimuth - key_turn_degrees;
        changed = shift ? scene_.pan_by(key_pan_step, 0.0) : scene_.set_camera(next);
        break;
    case PhysicalKey::up:
        next.elevation = next.elevation - key_turn_degrees;
        changed = shift ? scene_.pan_by(0.0, -key_pan_step) : scene_.set_camera(next);
        break;
    case PhysicalKey::down:
        next.elevation = next.elevation + key_turn_degrees;
        changed = shift ? scene_.pan_by(0.0, key_pan_step) : scene_.set_camera(next);
        break;
    case PhysicalKey::equal:
    case PhysicalKey::keypad_plus:
        changed = scene_.zoom_by(key_zoom_step, std::nullopt);
        break;
    case PhysicalKey::minus:
    case PhysicalKey::keypad_minus:
        changed = scene_.zoom_by(1.0 / key_zoom_step, std::nullopt);
        break;
    case PhysicalKey::home:
        changed = scene_.reset_camera();
        break;
    case PhysicalKey::p:
        next.projection = next.projection == plots::Plot3DProjection::perspective ? plots::Plot3DProjection::orthographic
                                                                                  : plots::Plot3DProjection::perspective;
        changed = scene_.set_camera(next);
        break;
    default:
        return;
    }
    event.handled = true;
    if (changed) camera_moved();
}

// The key raiser raises the event first; an unhandled key reaches
// handle_key.
void Plot3DView::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    if (arguments.handled) return;
    KeyEvent event = key_event_of(arguments, KeyAction::down);
    handle_key(event);
    if (event.handled) arguments.handled = true;
}

void Plot3DView::on_focus_changed(bool focused) {
    Control::on_focus_changed(focused);
    invalidate(Dirty::paint);
}

// -- anatomy and semantics -----------------------------------------------------------

const ControlAnatomy& Plot3DView::anatomy() const noexcept {
    return plot3d_view_anatomy;
}

AnatomyStateSet Plot3DView::anatomy_states() const {
    return base_anatomy_states();
}

void Plot3DView::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect arranged = committed_arranged_bounds();
    const Rect plot = plot_bounds();
    Parts record;
    record.region.bounds = Rect{0.0, 0.0, arranged.width, arranged.height};
    record.region.present = true;
    record.plot.bounds = plot;
    record.plot.present = true;
    record.picture.bounds = plot;
    record.picture.present = picture_ != nullptr;
    record.x_axis.bounds = axis_bounds(plots::Plot3DAxis::x);
    record.x_axis.present = record.x_axis.bounds.width > 0.0;
    record.y_axis.bounds = axis_bounds(plots::Plot3DAxis::y);
    record.y_axis.present = record.y_axis.bounds.width > 0.0;
    record.z_axis.bounds = axis_bounds(plots::Plot3DAxis::z);
    record.z_axis.present = record.z_axis.bounds.width > 0.0;
    record.readout.bounds = readout_bounds();
    record.readout.present = !readout_.empty();
    record.focus.bounds = Rect{plot.x - 2.0, plot.y - 2.0, plot.width + 4.0, plot.height + 4.0};
    record.focus.present = true;
    write_parts(anatomy(), plot3d_view_part_fields, record, geometry);
}

Plot3DView::Parts Plot3DView::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), plot3d_view_part_fields, published.parts(), record);
    return record;
}

Plot3DView::States Plot3DView::states() const {
    States record;
    read_states(plot3d_view_state_fields, anatomy_states(), record);
    return record;
}

SemanticDescriptor Plot3DView::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::figure;
    descriptor.name = accessible_name.read();
    const plots::Plot3DCamera& camera = scene_.camera();
    std::array<char, 160> text{};
    const int written = std::snprintf(text.data(), text.size(),
                                      "3D plot of %zu items, azimuth %.0f degrees, elevation %.0f degrees, zoom %.2g",
                                      scene_.item_count(), camera.azimuth, camera.elevation, camera.zoom);
    if (written > 0) descriptor.description = std::string(text.data());
    if (!accessible_description.read().empty()) descriptor.description = accessible_description.read();
    descriptor.value = readout_;
    descriptor.include_descendants = false;
    descriptor.exposed = true;
    return descriptor;
}

void Plot3DView::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), plot3d_view_interactions.begin(), plot3d_view_interactions.end());
}

// -- properties ----------------------------------------------------------------------

void Plot3DView::prepare_write(const decltype(show_panes)&, bool&) {
    require_mutable();
}

void Plot3DView::written(const decltype(show_panes)&) {
    apply_options(scene_.options().axis_titles, scene_.options().box_aspect);
}

void Plot3DView::prepare_write(const decltype(show_grid)&, bool&) {
    require_mutable();
}

void Plot3DView::written(const decltype(show_grid)&) {
    apply_options(scene_.options().axis_titles, scene_.options().box_aspect);
}

void Plot3DView::prepare_write(const decltype(show_axes)&, bool&) {
    require_mutable();
}

void Plot3DView::written(const decltype(show_axes)&) {
    apply_options(scene_.options().axis_titles, scene_.options().box_aspect);
}

void Plot3DView::prepare_write(const decltype(wheel_zoom)&, bool&) {
    require_mutable();
}

void Plot3DView::written(const decltype(wheel_zoom)&) {
    // Nothing drawn depends on it.
}

void Plot3DView::prepare_write(const decltype(labels_along_axis)&, bool&) {
    require_mutable();
}

void Plot3DView::written(const decltype(labels_along_axis)&) {
    apply_options(scene_.options().axis_titles, scene_.options().box_aspect);
}

// property_codemod begin Plot3DView (tools/property_families/plot3d_view.json; generated, do not edit)

namespace {

PropertyDescriptor plot3d_view_show_panes_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "show_panes";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether the back panes are drawn.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot3d_view_show_panes() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot3d_view_show_panes_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot3d_view_show_grid_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "show_grid";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether the grid is drawn.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot3d_view_show_grid() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot3d_view_show_grid_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot3d_view_show_axes_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "show_axes";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether the axes are drawn.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot3d_view_show_axes() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot3d_view_show_axes_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot3d_view_wheel_zoom_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "wheel_zoom";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether the wheel zooms.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot3d_view_wheel_zoom() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot3d_view_wheel_zoom_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot3d_view_labels_along_axis_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "labels_along_axis";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether axis titles follow their axes.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot3d_view_labels_along_axis() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot3d_view_labels_along_axis_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, Plot3DView::plot3d_view_property_count>& Plot3DView::plot3d_view_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 5U> own{{
        {"show_panes", &describe_plot3d_view_show_panes,
         &read_cell_entry<Plot3DView, decltype(&Plot3DView::show_panes), &Plot3DView::show_panes>,
         &write_cell_entry<Plot3DView, decltype(&Plot3DView::show_panes), &Plot3DView::show_panes>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"show_grid", &describe_plot3d_view_show_grid,
         &read_cell_entry<Plot3DView, decltype(&Plot3DView::show_grid), &Plot3DView::show_grid>,
         &write_cell_entry<Plot3DView, decltype(&Plot3DView::show_grid), &Plot3DView::show_grid>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"show_axes", &describe_plot3d_view_show_axes,
         &read_cell_entry<Plot3DView, decltype(&Plot3DView::show_axes), &Plot3DView::show_axes>,
         &write_cell_entry<Plot3DView, decltype(&Plot3DView::show_axes), &Plot3DView::show_axes>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"wheel_zoom", &describe_plot3d_view_wheel_zoom,
         &read_cell_entry<Plot3DView, decltype(&Plot3DView::wheel_zoom), &Plot3DView::wheel_zoom>,
         &write_cell_entry<Plot3DView, decltype(&Plot3DView::wheel_zoom), &Plot3DView::wheel_zoom>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"labels_along_axis", &describe_plot3d_view_labels_along_axis,
         &read_cell_entry<Plot3DView, decltype(&Plot3DView::labels_along_axis), &Plot3DView::labels_along_axis>,
         &write_cell_entry<Plot3DView, decltype(&Plot3DView::labels_along_axis), &Plot3DView::labels_along_axis>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "Plot3DView's property table names a property twice");
    static const std::array<PropertyEntry, plot3d_view_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> Plot3DView::property_table() const noexcept {
    const std::array<PropertyEntry, plot3d_view_property_count>& entries = plot3d_view_property_entries();
    return std::span<const PropertyEntry>(entries);
}

// property_codemod end Plot3DView

} // namespace gui_forms
