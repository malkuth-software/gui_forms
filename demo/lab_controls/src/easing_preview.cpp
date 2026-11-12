#include "gui_forms_demo/easing_preview.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

using namespace std::chrono_literals;

[[nodiscard]] std::vector<EasingPreviewTrack> default_easing_tracks(
    const BasicControlStyle& style) {
    return {
        {EasingCurve::linear, "Linear", style.accent},
        {EasingCurve::ease_in, "Ease in", style.accent},
        {EasingCurve::ease_out, "Ease out", style.accent},
        {EasingCurve::ease_in_out, "Ease in/out", style.visited_link},
        {EasingCurve::smooth_step, "Smooth step", style.visited_link},
        {EasingCurve::back_out, "Back out", style.visited_link},
        {EasingCurve::bounce_out, "Bounce", Color::rgba(211, 121, 42)},
        {EasingCurve::elastic_out, "Elastic", Color::rgba(211, 121, 42)},
    };
}

// The colours one paint uses besides the ground: from the style set with
// set_style, or from the theme's editor recipe in the preview's context
// (its colour scheme, contrast and state).
struct PreviewColours final {
    Color border{};
    Color title{};
    Color label{};
    Color track{};
    Color marker_edge{};
    Color accent{};
};

PreviewColours styled_colours(const BasicControlStyle& style) noexcept {
    PreviewColours colours{};
    colours.border = style.border;
    colours.title = style.dark_border;
    colours.label = style.text;
    colours.track = style.face;
    colours.marker_edge = style.dark_border;
    colours.accent = style.accent;
    return colours;
}

PreviewColours themed_colours(const ControlVisualRecipe& recipe) noexcept {
    PreviewColours colours{};
    colours.border = recipe.muted_text;
    colours.title = recipe.text;
    colours.label = recipe.text;
    colours.track = recipe.muted_text;
    colours.marker_edge = recipe.glyph;
    colours.accent = recipe.focus_ring;
    return colours;
}

} // namespace

EasingPreview::EasingPreview(StableId stable_id)
    : Control(std::move(stable_id)) {
    AnimationSpec initial_specification;
    initial_specification.duration = 2400ms;
    initial_specification.infinite = true;
    initial_specification.easing = EasingCurve::linear;
    timeline_.set_specification(initial_specification);
    tracks_ = default_easing_tracks(BasicControlStyle{});
    accessible_name = "Animation easing preview";
    accessible_description = "Retained timeline tracks comparing configured easing curves";
}

const BasicControlStyle& EasingPreview::style() const noexcept {
    return style_override_ ? *style_override_ : effective_theme().basic_style();
}

void EasingPreview::set_style(BasicControlStyle style) {
    require_mutable();
    if (style_override_ && *style_override_ == style) return;
    style_override_ = style;
    invalidate(Dirty::paint);
}

void EasingPreview::clear_style() {
    require_mutable();
    if (!style_override_) return;
    style_override_.reset();
    invalidate(Dirty::paint);
}

void EasingPreview::write_view(const decltype(specification)&, AnimationSpec incoming) {
    require_mutable();
    timeline_.set_specification(incoming);
    if (timeline_started_) {
        const FrameTime now = FrameClock::now();
        timeline_.start(now);
        if (!motion_policy_.active()) timeline_.pause(now);
        phase_ = timeline_.sample(now).progress;
    }
    frames_.stop();
    if (motion_policy_.active()) register_frames();
    invalidate(Dirty::paint | Dirty::semantics);
}

AnimationSpec EasingPreview::read_view(const decltype(specification)&) const {
    AnimationSpec current = timeline_.specification();
    return current;
}

void EasingPreview::set_tracks(std::vector<EasingPreviewTrack> tracks) {
    require_mutable();
    if (tracks.empty() || tracks.size() > 32U) {
        throw std::invalid_argument(
            "easing preview requires between one and 32 tracks");
    }
    for (const EasingPreviewTrack& track : tracks) {
        if (track.label.empty()) {
            throw std::invalid_argument("easing preview track label may not be empty");
        }
    }
    if (tracks_ == tracks) return;
    tracks_ = std::move(tracks);
    invalidate(Dirty::paint | Dirty::semantics);
}

void EasingPreview::write_view(const decltype(motion_policy)&, MotionPolicy incoming) {
    require_mutable();
    if (motion_policy_ == incoming) return;
    const bool was_live = motion_policy_.active();
    const bool becomes_live = incoming.active();
    frames_.stop();
    if (timeline_started_) {
        const FrameTime transition = FrameClock::now();
        if (was_live && !becomes_live) {
            timeline_.pause(transition);
            phase_ = timeline_.sample(transition).progress;
        } else if (!was_live && becomes_live) {
            timeline_.resume(transition);
        }
    }
    motion_policy_ = incoming;
    if (becomes_live) register_frames();
    invalidate(Dirty::paint | Dirty::semantics);
}

MotionPolicy EasingPreview::read_view(const decltype(motion_policy)&) const {
    return motion_policy_;
}

MotionPolicy EasingPreview::effective_motion_policy() const noexcept {
    MotionPolicy policy = motion_policy_;
    if (window() != nullptr &&
        (*window()).presentation_settings().reduced_motion) {
        policy.reduced = true;
    }
    return policy;
}

void EasingPreview::advance_animation(FrameTime now) {
    if (!effective_motion_policy().active()) return;
    const AnimationSample sample = timeline_.sample(now);
    phase_ = sample.progress;
    if (sample.finished) frames_.stop();
}

std::string EasingPreview::motion_readout(double presented_phase) const {
    const MotionPolicy policy = effective_motion_policy();
    const char* name = !policy.enabled
        ? "DISABLED"
        : policy.reduced && policy.paused
            ? "REDUCED + PAUSED"
            : policy.reduced
                ? "REDUCED"
                : policy.paused ? "PAUSED" : "LIVE";
    char readout[96]{};
    std::snprintf(readout, sizeof(readout), "%s · %03d%%", name,
                  static_cast<int>(std::round(presented_phase * 100.0)));
    return readout;
}

double EasingPreview::track_row_top(std::size_t index) const noexcept {
    const double s = effective_text_scale();
    const double height = committed_arranged_bounds().height;
    const double row_height = tracks_.empty()
        ? 0.0 : std::max(marker_size.read() + 4.0,
                         (height - 48.0 * s) / static_cast<double>(tracks_.size()));
    const double top = 48.0 * s + static_cast<double>(index) * row_height;
    return top;
}

double EasingPreview::track_left() const noexcept {
    const double left = 112.0 * effective_text_scale();
    return left;
}

double EasingPreview::track_width() const noexcept {
    const double s = effective_text_scale();
    const double width = std::max(40.0 * s, committed_arranged_bounds().width - 140.0 * s);
    return width;
}

Rect EasingPreview::marker_bounds(std::size_t index, double presented_phase) const noexcept {
    const double directed = presented_phase < 0.5
        ? presented_phase * 2.0 : (1.0 - presented_phase) * 2.0;
    const double y = track_row_top(index);
    const double left = track_left();
    const double travel = std::max(0.0, track_width() - marker_size.read());
    const double eased = std::clamp(
        apply_easing(tracks_[index].curve, directed), -0.08, 1.08);
    const double x = std::clamp(left + eased * travel, left, left + travel);
    const Rect marker{x, y + 2.0, marker_size.read(), marker_size.read()};
    return marker;
}

Rect EasingPreview::readout_bounds() const noexcept {
    // The readout's baseline is at 25 (scaled) in an 11-point face: 17 above
    // it and 7 below cover the face's ascent and descent at every scale.
    const double s = effective_text_scale();
    const double left = committed_arranged_bounds().width - 150.0 * s;
    const Rect readout{left, 8.0 * s, 150.0 * s, 24.0 * s};
    return readout;
}

void EasingPreview::on_paint(Painter& painter, Rect) {
    // The preview is shown: a timer stopped while it was not starts again.
    frames_.note_painted();
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width,
                      committed_arranged_bounds().height};
    const double s = effective_text_scale();
    // The ground: a style's paper, else the theme's editor face, which
    // resolves in the window's colour scheme.
    const ControlVisualRecipe& recipe = effective_theme().resolve(ControlVisualRole::editor, visual_context());
    const PreviewColours colours = style_override_ ? styled_colours(*style_override_) : themed_colours(recipe);
    if (style_override_) {
        painter.fill_rect(bounds, (*style_override_).paper);
    } else {
        PartPaint face{};
        face.family = ControlVisualRole::editor;
        face.role = PartRole::face;
        face.bounds = bounds;
        face.key = PartKey{"face", {}, {}};
        paint_part(painter, *this, face);
    }
    painter.stroke_rect({0.5, 0.5, std::max(0.0, bounds.width - 1.0),
                         std::max(0.0, bounds.height - 1.0)},
                        colours.border, 1.0);
    painter.draw_text_utf8({18.0 * s, 25.0 * s}, title.read(),
                           effective_font({FontRole::control, 13.0, 700, false}),
                           colours.title);
    const MotionPolicy policy = effective_motion_policy();
    const double presented_phase = policy.presentation_phase(phase_);
    const std::string readout = motion_readout(presented_phase);
    const Rect readout_box = readout_bounds();
    painter.draw_text_utf8({readout_box.x, 25.0 * s}, readout,
                           effective_font({FontRole::control, 11.0, 600, false}),
                           policy.active() ? colours.accent
                                           : Color::rgba(211, 121, 42));

    const double track_x = track_left();
    const double width = track_width();
    for (std::size_t index = 0U; index < tracks_.size(); ++index) {
        const double y = track_row_top(index);
        painter.draw_text_utf8({18.0 * s, y + 15.0 * s}, tracks_[index].label,
                               effective_font({FontRole::content, 11.0, 400, false}),
                               colours.label);
        painter.fill_rect({track_x, y + 8.0, width, 3.0}, colours.track);
        const Rect marker = marker_bounds(index, presented_phase);
        painter.fill_rect(marker, tracks_[index].color);
        painter.stroke_rect({marker.x + 0.5, marker.y + 0.5,
                             std::max(0.0, marker_size.read() - 1.0),
                             std::max(0.0, marker_size.read() - 1.0)},
                            colours.marker_edge, 1.0);
    }
}

bool EasingPreview::hit_test_local(Point) const { return false; }

SemanticDescriptor EasingPreview::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::image;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    const MotionPolicy policy = effective_motion_policy();
    const double presented = policy.presentation_phase(phase_);
    descriptor.value = motion_readout(presented);
    descriptor.numeric_value = presented;
    descriptor.minimum_value = 0.0;
    descriptor.maximum_value = 1.0;
    if (policy.active()) descriptor.states |= SemanticState::busy;
    descriptor.exposed = true;
    return descriptor;
}

void EasingPreview::on_attached_to_window() {
    Control::on_attached_to_window();
    if (window() != nullptr) {
        presentation_subscription_ = (*window()).presentation_changed().subscribe(
            *this,
            Delegate<const PresentationSettings&>::bind<
                EasingPreview, &EasingPreview::on_presentation_changed>(*this));
    }
    const FrameTime attached = FrameClock::now();
    if (!timeline_started_) {
        timeline_.start(attached);
        timeline_started_ = true;
    } else if (motion_policy_.active()) {
        timeline_.resume(attached);
    }
    if (!motion_policy_.active()) timeline_.pause(attached);
    else register_frames();
}

void EasingPreview::on_presentation_changed(
    const PresentationSettings&) {
    frames_.stop();
    if (effective_motion_policy().active()) register_frames();
    invalidate(Dirty::paint | Dirty::semantics);
}

void EasingPreview::on_detached_from_window() noexcept {
    presentation_subscription_.disconnect();
    frames_.stop();
    if (timeline_started_) {
        const FrameTime detached = FrameClock::now();
        phase_ = timeline_.sample(detached).progress;
        timeline_.pause(detached);
    }
    Control::on_detached_from_window();
}

void EasingPreview::register_frames() {
    const MotionPolicy policy = effective_motion_policy();
    if (window() == nullptr || !policy.active()) {
        frames_.stop();
        return;
    }
    // A motion demonstrator: its markers need every display frame.
    const FrameInterval interval = policy.frame_interval(display_frame_interval);
    const std::shared_ptr<EasingPreview> self =
        std::static_pointer_cast<EasingPreview>(shared_from_this());
    frames_.start(*this, interval, FrameTick{self});
}

AnimationTickResult EasingPreview::FrameTick::operator()(FrameTime now) const {
    const std::shared_ptr<EasingPreview> live = preview.lock();
    const AnimationTickResult result = live
        ? (*live).on_frame_tick(now) : AnimationTickResult::unchanged;
    return result;
}

AnimationTickResult EasingPreview::on_frame_tick(FrameTime now) {
    const MotionPolicy policy = effective_motion_policy();
    const double before = policy.presentation_phase(phase_);
    advance_animation(now);
    const double after = policy.presentation_phase(phase_);
    if (after == before) return AnimationTickResult::unchanged;
    // The preview publishes its phase as its accessible value.
    invalidate(Dirty::semantics);
    invalidate(readout_bounds());
    for (std::size_t index = 0U; index < tracks_.size(); ++index) {
        const Rect moved = Rect::united(marker_bounds(index, before), marker_bounds(index, after));
        invalidate(moved);
    }
    return AnimationTickResult::damaged;
}

namespace {

constexpr std::array<PartField<EasingPreview::Parts>, 4> easing_preview_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &EasingPreview::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &EasingPreview::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &EasingPreview::Parts::edge},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &EasingPreview::Parts::focus},
}};

constexpr std::array<StateField<EasingPreview::States>, 5> easing_preview_state_fields{{
    {.state = AnatomyState::normal, .member = &EasingPreview::States::normal},
    {.state = AnatomyState::enabled, .member = &EasingPreview::States::enabled},
    {.state = AnatomyState::disabled, .member = &EasingPreview::States::disabled},
    {.state = AnatomyState::focused, .member = &EasingPreview::States::focused},
    {.state = AnatomyState::focus_visible, .member = &EasingPreview::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 4> easing_preview_parts =
    part_declarations(easing_preview_part_fields);
constexpr std::array<AnatomyState, 5> easing_preview_states =
    state_declarations(easing_preview_state_fields);

constexpr ControlAnatomy easing_preview_anatomy{
    .control_id = "easing_preview",
    .parts = easing_preview_parts,
    .states = easing_preview_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& EasingPreview::anatomy() const noexcept {
    return easing_preview_anatomy;
}

void EasingPreview::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = client_rectangle();
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    record.face = PartGeometry::from_bounds(bounds);
    record.edge = PartGeometry::from_bounds(bounds);
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), easing_preview_part_fields, record, geometry);
}

EasingPreview::Parts EasingPreview::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), easing_preview_part_fields, published.parts(), record);
    return record;
}

EasingPreview::States EasingPreview::states() const {
    States record;
    read_states(easing_preview_state_fields, anatomy_states(), record);
    return record;
}

// property_codemod begin EasingPreview (tools/property_families/easing_preview.json; generated, do not edit)

namespace {

PropertyDescriptor easing_preview_title_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "title";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The heading painted above the tracks.";
    descriptor.default_value = BindingValue{std::string("Animation timeline and easing")};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_easing_preview_title() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        easing_preview_title_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, EasingPreview::easing_preview_property_count>& EasingPreview::easing_preview_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"title", &describe_easing_preview_title,
         &read_cell_entry<EasingPreview, decltype(&EasingPreview::title), &EasingPreview::title>,
         &write_cell_entry<EasingPreview, decltype(&EasingPreview::title), &EasingPreview::title>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "EasingPreview's property table names a property twice");
    static const std::array<PropertyEntry, easing_preview_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> EasingPreview::property_table() const noexcept {
    const std::array<PropertyEntry, easing_preview_property_count>& entries = easing_preview_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void EasingPreview::prepare_write(const decltype(title)&, std::string&) {
    require_mutable();
}

void EasingPreview::written(const decltype(title)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void EasingPreview::prepare_write(const decltype(marker_size)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 4.0 || incoming > 48.0) throw std::invalid_argument("easing marker size must be within [4, 48]");
}

void EasingPreview::written(const decltype(marker_size)&) {
    invalidate(Dirty::paint);
}

// property_codemod end EasingPreview

} // namespace gui_forms
