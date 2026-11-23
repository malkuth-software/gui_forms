#include "zoom_model.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms::code_zoom {

double clamp_zoom(const double zoom) noexcept {
    if (!std::isfinite(zoom)) return 1.0;
    const double clamped = std::clamp(zoom, minimum_zoom, maximum_zoom);
    return clamped;
}

double ease_out_cubic(const double t) noexcept {
    const double clamped = std::isfinite(t) ? std::clamp(t, 0.0, 1.0) : 1.0;
    const double rest = 1.0 - clamped;
    const double eased = 1.0 - rest * rest * rest;
    return eased;
}

double glide_zoom(const double from, const double to, const double progress) noexcept {
    const double eased = ease_out_cubic(progress);
    if (eased >= 1.0) return to;
    const double log_from = std::log(from);
    const double log_to = std::log(to);
    const double zoom = std::exp(log_from + (log_to - log_from) * eased);
    return zoom;
}

void ZoomModel::set_extent(const Size content, const Size viewport) noexcept {
    content_ = content;
    viewport_ = viewport;
    clamp_origin();
}

Point ZoomModel::content_at(const Point screen) const noexcept {
    const Point content{origin_.x + screen.x / zoom_, origin_.y + screen.y / zoom_};
    return content;
}

void ZoomModel::zoom_at(const double zoom, const Point anchor) noexcept {
    const Point content = content_at(anchor);
    gliding_ = false;
    place(clamp_zoom(zoom), content, anchor);
}

void ZoomModel::show(const double zoom, const Point content, const Point screen) noexcept {
    gliding_ = false;
    place(clamp_zoom(zoom), content, screen);
}

void ZoomModel::glide_to(const double zoom, const Point anchor) noexcept {
    anchor_content_ = content_at(anchor);
    anchor_screen_ = anchor;
    from_ = zoom_;
    to_ = clamp_zoom(zoom);
    gliding_ = true;
    start_pending_ = true;
}

void ZoomModel::glide_by(const double factor, const Point anchor) noexcept {
    const double target = target_zoom() * factor;
    glide_to(target, anchor);
}

void ZoomModel::scroll_by(const Point content_delta) noexcept {
    gliding_ = false;
    origin_ = Point{origin_.x + content_delta.x, origin_.y + content_delta.y};
    clamp_origin();
}

bool ZoomModel::advance(const double now_seconds) noexcept {
    if (!gliding_) return false;
    if (start_pending_) {
        started_ = now_seconds;
        start_pending_ = false;
    }
    const double progress = (now_seconds - started_) / glide_seconds;
    const double zoom = glide_zoom(from_, to_, progress);
    place(zoom, anchor_content_, anchor_screen_);
    gliding_ = progress < 1.0;
    return gliding_;
}

void ZoomModel::finish() noexcept {
    if (!gliding_) return;
    gliding_ = false;
    place(to_, anchor_content_, anchor_screen_);
}

void ZoomModel::place(const double zoom, const Point content, const Point screen) noexcept {
    zoom_ = zoom;
    origin_ = Point{content.x - screen.x / zoom, content.y - screen.y / zoom};
    clamp_origin();
}

// On each axis the origin lies between 0 and content - viewport / zoom,
// whichever order they come in.
void ZoomModel::clamp_origin() noexcept {
    const double spare_x = content_.width - viewport_.width / zoom_;
    const double spare_y = content_.height - viewport_.height / zoom_;
    const double x = std::clamp(origin_.x, std::min(0.0, spare_x), std::max(0.0, spare_x));
    const double y = std::clamp(origin_.y, std::min(0.0, spare_y), std::max(0.0, spare_y));
    origin_ = Point{x, y};
}

} // namespace gui_forms::code_zoom
