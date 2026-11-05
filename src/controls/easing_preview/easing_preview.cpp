#include "gui_forms/controls/easing_preview/easing_preview.hpp"

#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/window.hpp"

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

} // namespace

EasingPreview::EasingPreview(StableId stable_id)
    : Control(std::move(stable_id)) {
    AnimationSpec specification;
    specification.duration = 2400ms;
    specification.infinite = true;
    specification.easing = EasingCurve::linear;
    timeline_.set_specification(specification);
    tracks_ = default_easing_tracks(style_);
    accessible_name = "Animation easing preview";
    accessible_description = "Retained timeline tracks comparing configured easing curves";
    // The run-time schema of a preview (stage FD6.3): what a form document
    // sets on it. The tracks stay in code (a list of records). It raises no
    // change event.
    PropertyDescriptor title;
    title.name = "title";
    title.kind = BindingValueKind::text;
    title.category = "Appearance";
    title.description = "The heading painted above the tracks.";
    title.default_value = BindingValue{std::string("Animation timeline and easing")};
    title.invalidation_effects = Dirty::paint | Dirty::semantics;
    title.bindable = false;
    define_bindable_property({
        std::move(title),
        detail::BindingMemberGetter<EasingPreview, std::string>(*this, &EasingPreview::title_),
        detail::DirectPropertySetter<EasingPreview, std::string>(*this, &EasingPreview::set_title),
        {}, {}, {}});
}

void EasingPreview::set_title(std::string title) {
    require_mutable();
    if (title_ == title) return;
    title_ = std::move(title);
    invalidate(Dirty::paint | Dirty::semantics);
}

void EasingPreview::set_style(BasicControlStyle style) {
    require_mutable();
    if (style_ == style) return;
    style_ = style;
    invalidate(Dirty::paint);
}

void EasingPreview::set_specification(AnimationSpec specification) {
    require_mutable();
    timeline_.set_specification(specification);
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

void EasingPreview::set_motion_policy(MotionPolicy policy) {
    require_mutable();
    if (motion_policy_ == policy) return;
    const bool was_live = motion_policy_.active();
    const bool becomes_live = policy.active();
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
    motion_policy_ = policy;
    if (becomes_live) register_frames();
    invalidate(Dirty::paint | Dirty::semantics);
}

void EasingPreview::set_marker_size(double size) {
    require_mutable();
    if (!std::isfinite(size) || size < 4.0 || size > 48.0) {
        throw std::invalid_argument("easing marker size must be within [4, 48]");
    }
    if (marker_size_ == size) return;
    marker_size_ = size;
    invalidate(Dirty::paint);
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
        ? 0.0 : std::max(marker_size_ + 4.0,
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
    const double travel = std::max(0.0, track_width() - marker_size_);
    const double eased = std::clamp(
        apply_easing(tracks_[index].curve, directed), -0.08, 1.08);
    const double x = std::clamp(left + eased * travel, left, left + travel);
    const Rect marker{x, y + 2.0, marker_size_, marker_size_};
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
    painter.fill_rect(bounds, style_.paper);
    painter.stroke_rect({0.5, 0.5, std::max(0.0, bounds.width - 1.0),
                         std::max(0.0, bounds.height - 1.0)},
                        style_.border, 1.0);
    painter.draw_text_utf8({18.0 * s, 25.0 * s}, title_,
                           effective_font({FontRole::control, 13.0, 700, false}),
                           style_.dark_border);
    const MotionPolicy policy = effective_motion_policy();
    const double presented_phase = policy.presentation_phase(phase_);
    const std::string readout = motion_readout(presented_phase);
    const Rect readout_box = readout_bounds();
    painter.draw_text_utf8({readout_box.x, 25.0 * s}, readout,
                           effective_font({FontRole::control, 11.0, 600, false}),
                           policy.active() ? style_.accent
                                           : Color::rgba(211, 121, 42));

    const double track_x = track_left();
    const double width = track_width();
    for (std::size_t index = 0U; index < tracks_.size(); ++index) {
        const double y = track_row_top(index);
        painter.draw_text_utf8({18.0 * s, y + 15.0 * s}, tracks_[index].label,
                               effective_font({FontRole::content, 11.0, 400, false}),
                               style_.text);
        painter.fill_rect({track_x, y + 8.0, width, 3.0}, style_.face);
        const Rect marker = marker_bounds(index, presented_phase);
        painter.fill_rect(marker, tracks_[index].color);
        painter.stroke_rect({marker.x + 0.5, marker.y + 0.5,
                             std::max(0.0, marker_size_ - 1.0),
                             std::max(0.0, marker_size_ - 1.0)},
                            style_.dark_border, 1.0);
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

} // namespace gui_forms
