// The web host's desk: layers, stacking, frames, activation and modal
// blocking (web_desk.hpp).
#include "web_desk.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms::host::web_detail {
namespace {

// The bands of the stacking order, back to front.
constexpr int primary_band = 0;
constexpr int window_band = 1;
constexpr int popup_band = 2;
constexpr int splash_band = 3;

bool finite_point(Point point) noexcept {
    const bool finite = std::isfinite(point.x) && std::isfinite(point.y);
    return finite;
}

} // namespace

WebDesk::WebDesk() {
    layers_.reserve(HostWindowing::maximum_windows);
    order_.reserve(HostWindowing::maximum_windows);
    family_.reserve(HostWindowing::maximum_windows);
    modals_.reserve(HostWindowing::maximum_modal_depth);
}

WebDesk::Layer* WebDesk::layer(HostWindowId window) noexcept {
    for (std::size_t index = 0U; index < layers_.size(); ++index) {
        if (layers_[index].id == window) return &layers_[index];
    }
    return nullptr;
}

const WebDesk::Layer* WebDesk::layer(HostWindowId window) const noexcept {
    for (std::size_t index = 0U; index < layers_.size(); ++index) {
        if (layers_[index].id == window) return &layers_[index];
    }
    return nullptr;
}

bool WebDesk::is_framed(const Layer& entry) noexcept {
    const bool framed = entry.kind != HostWindowKind::primary && entry.kind != HostWindowKind::popup;
    return framed;
}

Rect WebDesk::outer_of(const Layer& entry) noexcept {
    if (!is_framed(entry)) return entry.content;
    const Rect outer{entry.content.x - border_width, entry.content.y - title_bar_height,
                     entry.content.width + 2.0 * border_width,
                     entry.content.height + title_bar_height + border_width};
    return outer;
}

int WebDesk::band(const Layer& entry) noexcept {
    if (entry.kind == HostWindowKind::primary) return primary_band;
    if (entry.kind != HostWindowKind::popup) return window_band;
    if (host_transient_is_splash(entry.transient)) return splash_band;
    return popup_band;
}

std::size_t WebDesk::band_end(int band_index) const noexcept {
    // order_ is sorted by band, so the first layer of a higher band ends it.
    std::size_t index = 0U;
    while (index < order_.size()) {
        const Layer* entry = layer(order_[index]);
        if (entry != nullptr && band(*entry) > band_index) break;
        ++index;
    }
    return index;
}

Rect WebDesk::clamped(const Layer& entry, Rect content) const noexcept {
    if (entry.kind == HostWindowKind::primary) {
        const Rect whole{0.0, 0.0, extent_.width, extent_.height};
        return whole;
    }
    Rect result = content;
    if (!(result.width >= 1.0)) result.width = 1.0;
    if (!(result.height >= 1.0)) result.height = 1.0;
    if (!std::isfinite(result.x)) result.x = 0.0;
    if (!std::isfinite(result.y)) result.y = 0.0;
    if (entry.kind == HostWindowKind::popup) return result;
    // The frame inside the canvas: the size first, then the position.
    const double widest = std::max(1.0, extent_.width - 2.0 * border_width);
    const double tallest = std::max(1.0, extent_.height - title_bar_height - border_width);
    result.width = std::min(std::max(result.width, entry.minimum.width), widest);
    result.height = std::min(std::max(result.height, entry.minimum.height), tallest);
    // The title bar's top stays inside the canvas, and title_bar_kept of it
    // inside horizontally.
    const double top_limit = title_bar_height;
    const double bottom_limit = std::max(top_limit, extent_.height - border_width);
    result.y = std::clamp(result.y, top_limit, bottom_limit);
    const double kept = std::min(title_bar_kept, result.width);
    const double left_limit = kept - result.width;
    const double right_limit = std::max(left_limit, extent_.width - kept);
    result.x = std::clamp(result.x, left_limit, right_limit);
    return result;
}

void WebDesk::set_extent(Size extent) {
    const double width = std::isfinite(extent.width) && extent.width >= 1.0 ? extent.width : 1.0;
    const double height = std::isfinite(extent.height) && extent.height >= 1.0 ? extent.height : 1.0;
    extent_ = Size{width, height};
    for (std::size_t index = 0U; index < layers_.size(); ++index) {
        Layer& entry = layers_[index];
        entry.content = clamped(entry, entry.content);
    }
}

bool WebDesk::add(const WebDeskLayerInit& init) {
    if (!init.id.valid() || layer(init.id) != nullptr) return false;
    if (layers_.size() >= HostWindowing::maximum_windows) return false;
    if (init.kind == HostWindowKind::primary) {
        for (std::size_t index = 0U; index < layers_.size(); ++index) {
            if (layers_[index].kind == HostWindowKind::primary) return false;
        }
    }
    Layer entry{};
    entry.id = init.id;
    entry.owner = init.owner;
    entry.kind = init.kind;
    entry.transient = init.transient;
    entry.minimum = init.minimum;
    entry.resizable = init.resizable;
    entry.content = clamped(entry, init.content);
    const std::size_t at = band_end(band(entry));
    layers_.push_back(entry);
    order_.insert(order_.begin() + static_cast<std::ptrdiff_t>(at), entry.id);
    return true;
}

void WebDesk::remove(HostWindowId window) noexcept {
    for (std::size_t index = 0U; index < layers_.size(); ++index) {
        if (layers_[index].id != window) continue;
        layers_.erase(layers_.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
    for (std::size_t index = 0U; index < order_.size(); ++index) {
        if (order_[index] != window) continue;
        order_.erase(order_.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
    for (std::size_t scope = 0U; scope < modals_.size(); ++scope) {
        std::vector<HostWindowId>& blocked = modals_[scope].blocked;
        for (std::size_t index = blocked.size(); index > 0U; --index) {
            if (blocked[index - 1U] == window) blocked.erase(blocked.begin() + static_cast<std::ptrdiff_t>(index - 1U));
        }
    }
    if (dragging_ && drag_window_ == window) dragging_ = false;
    if (active_ == window) {
        active_ = {};
        static_cast<void>(activate_topmost());
    }
}

void WebDesk::set_visible(HostWindowId window, bool visible) noexcept {
    Layer* entry = layer(window);
    if (entry == nullptr) return;
    (*entry).visible = visible;
    if (!visible && active_ == window) {
        active_ = {};
        static_cast<void>(activate_topmost());
    }
}

bool WebDesk::visible(HostWindowId window) const noexcept {
    const Layer* entry = layer(window);
    const bool shown = entry != nullptr && (*entry).visible;
    return shown;
}

bool WebDesk::contains(HostWindowId window) const noexcept {
    const bool found = layer(window) != nullptr;
    return found;
}

std::optional<Rect> WebDesk::set_content(HostWindowId window, Rect content) {
    Layer* entry = layer(window);
    if (entry == nullptr) return std::nullopt;
    (*entry).content = clamped(*entry, content);
    const std::optional<Rect> applied = (*entry).content;
    return applied;
}

std::optional<Rect> WebDesk::content(HostWindowId window) const noexcept {
    const Layer* entry = layer(window);
    if (entry == nullptr) return std::nullopt;
    const std::optional<Rect> result = (*entry).content;
    return result;
}

std::optional<Rect> WebDesk::outer(HostWindowId window) const noexcept {
    const Layer* entry = layer(window);
    if (entry == nullptr) return std::nullopt;
    const std::optional<Rect> result = outer_of(*entry);
    return result;
}

bool WebDesk::framed(HostWindowId window) const noexcept {
    const Layer* entry = layer(window);
    const bool result = entry != nullptr && is_framed(*entry);
    return result;
}

bool WebDesk::owned_by(HostWindowId window, HostWindowId ancestor) const noexcept {
    // Ownership chains are at most maximum_windows long.
    HostWindowId current = window;
    for (std::size_t depth = 0U; depth <= HostWindowing::maximum_windows && current.valid(); ++depth) {
        if (current == ancestor) return true;
        const Layer* entry = layer(current);
        if (entry == nullptr) return false;
        current = (*entry).owner;
    }
    return false;
}

void WebDesk::raise(HostWindowId window) {
    const Layer* entry = layer(window);
    if (entry == nullptr || (*entry).kind == HostWindowKind::primary) return;
    const int raised_band = band(*entry);
    // The family: the window, then the layers of its band it owns, in their
    // stacking order.
    family_.clear();
    family_.push_back(window);
    for (std::size_t index = 0U; index < order_.size(); ++index) {
        const HostWindowId candidate = order_[index];
        if (candidate == window) continue;
        const Layer* other = layer(candidate);
        if (other == nullptr || band(*other) != raised_band) continue;
        if (owned_by(candidate, window)) family_.push_back(candidate);
    }
    for (std::size_t member = 0U; member < family_.size(); ++member) {
        for (std::size_t index = 0U; index < order_.size(); ++index) {
            if (order_[index] != family_[member]) continue;
            order_.erase(order_.begin() + static_cast<std::ptrdiff_t>(index));
            break;
        }
    }
    const std::size_t at = band_end(raised_band);
    order_.insert(order_.begin() + static_cast<std::ptrdiff_t>(at), family_.begin(), family_.end());
    family_.clear();
}

void WebDesk::scene(std::vector<WebDeskQuad>& quads) const {
    quads.clear();
    for (std::size_t index = 0U; index < order_.size(); ++index) {
        const Layer* entry = layer(order_[index]);
        if (entry == nullptr || !(*entry).visible) continue;
        WebDeskQuad quad{};
        quad.window = (*entry).id;
        quad.content = (*entry).content;
        quad.outer = outer_of(*entry);
        quad.framed = is_framed(*entry);
        quad.active = (*entry).id == active_;
        quads.push_back(quad);
    }
}

WebDeskPart WebDesk::frame_part(const Layer& entry, Point point) const noexcept {
    const Rect outer = outer_of(entry);
    const bool resizable = entry.resizable;
    const bool left = point.x < outer.x + border_width;
    const bool right = point.x >= outer.right() - border_width;
    const bool top = point.y < outer.y + border_width;
    const bool bottom = point.y >= outer.bottom() - border_width;
    if (resizable) {
        if (top && left) return WebDeskPart::corner_top_left;
        if (top && right) return WebDeskPart::corner_top_right;
        if (bottom && left) return WebDeskPart::corner_bottom_left;
        if (bottom && right) return WebDeskPart::corner_bottom_right;
        if (left) return WebDeskPart::edge_left;
        if (right) return WebDeskPart::edge_right;
        if (top) return WebDeskPart::edge_top;
        if (bottom) return WebDeskPart::edge_bottom;
    }
    if (point.y < entry.content.y) {
        const bool close_box = point.x >= outer.right() - border_width - close_box_width;
        if (close_box) return WebDeskPart::close_box;
        return WebDeskPart::title_bar;
    }
    if (entry.content.contains(point)) return WebDeskPart::client;
    // On the border of a window that is not resizable.
    return WebDeskPart::title_bar;
}

WebDeskHit WebDesk::hit(Point point) const noexcept {
    WebDeskHit result{};
    if (!finite_point(point)) return result;
    for (std::size_t index = order_.size(); index > 0U; --index) {
        const Layer* entry = layer(order_[index - 1U]);
        if (entry == nullptr || !(*entry).visible) continue;
        // A Splash takes no input: the point passes through it.
        if ((*entry).kind == HostWindowKind::popup && host_transient_is_splash((*entry).transient)) continue;
        Rect reach = outer_of(*entry);
        if (is_framed(*entry) && (*entry).resizable) {
            reach = Rect{reach.x - edge_reach, reach.y - edge_reach, reach.width + 2.0 * edge_reach,
                         reach.height + 2.0 * edge_reach};
        }
        if (!reach.contains(point)) continue;
        result.window = (*entry).id;
        result.local = Point{point.x - (*entry).content.x, point.y - (*entry).content.y};
        if (!is_framed(*entry)) {
            result.part = WebDeskPart::client;
            return result;
        }
        // The grips beyond the border count as the border itself.
        const Rect outer = outer_of(*entry);
        const Point inside{std::clamp(point.x, outer.x, outer.right() - border_width * 0.5),
                           std::clamp(point.y, outer.y, outer.bottom() - border_width * 0.5)};
        result.part = frame_part(*entry, inside);
        return result;
    }
    return result;
}

void WebDesk::begin_modal(HostWindowId modal) {
    ModalScope scope{};
    scope.modal = modal;
    scope.blocked.reserve(layers_.size());
    for (std::size_t index = 0U; index < layers_.size(); ++index) {
        const HostWindowId candidate = layers_[index].id;
        if (owned_by(candidate, modal)) continue;
        scope.blocked.push_back(candidate);
    }
    modals_.push_back(std::move(scope));
}

void WebDesk::end_modal(HostWindowId modal) noexcept {
    for (std::size_t index = modals_.size(); index > 0U; --index) {
        if (modals_[index - 1U].modal != modal) continue;
        modals_.erase(modals_.begin() + static_cast<std::ptrdiff_t>(index - 1U));
        break;
    }
}

bool WebDesk::blocked(HostWindowId window) const noexcept {
    for (std::size_t scope = 0U; scope < modals_.size(); ++scope) {
        const std::vector<HostWindowId>& list = modals_[scope].blocked;
        for (std::size_t index = 0U; index < list.size(); ++index) {
            if (list[index] == window) return true;
        }
    }
    return false;
}

bool WebDesk::takes_keyboard(const Layer& entry) const noexcept {
    if (!entry.visible) return false;
    if (entry.kind == HostWindowKind::popup) return false;
    const bool free = !blocked(entry.id);
    return free;
}

HostWindowId WebDesk::activate(HostWindowId window, bool takes_focus) {
    const Layer* entry = layer(window);
    if (entry == nullptr) return active_;
    if ((*entry).kind == HostWindowKind::popup) {
        // A Splash is never activated; a popup only when it takes focus.
        const bool splash = host_transient_is_splash((*entry).transient);
        if (splash || !takes_focus || blocked(window)) return active_;
        active_ = window;
        return active_;
    }
    HostWindowId target = window;
    if (blocked(window)) {
        // The innermost modal window that blocks it takes the activation.
        for (std::size_t scope = modals_.size(); scope > 0U; --scope) {
            const std::vector<HostWindowId>& list = modals_[scope - 1U].blocked;
            const bool blocks = std::find(list.begin(), list.end(), window) != list.end();
            if (!blocks) continue;
            target = modals_[scope - 1U].modal;
            break;
        }
    }
    const Layer* chosen = layer(target);
    if (chosen == nullptr || !(*chosen).visible) return active_;
    active_ = target;
    raise(target);
    return active_;
}

HostWindowId WebDesk::activate_topmost() {
    for (std::size_t index = order_.size(); index > 0U; --index) {
        const Layer* entry = layer(order_[index - 1U]);
        if (entry == nullptr || !takes_keyboard(*entry)) continue;
        active_ = (*entry).id;
        return active_;
    }
    active_ = {};
    return active_;
}

bool WebDesk::begin_frame_drag(HostWindowId window, WebDeskPart part, Point point) {
    const Layer* entry = layer(window);
    if (entry == nullptr || !is_framed(*entry) || blocked(window) || !finite_point(point)) return false;
    if (part == WebDeskPart::none || part == WebDeskPart::client || part == WebDeskPart::close_box) return false;
    const bool resizing = part != WebDeskPart::title_bar;
    if (resizing && !(*entry).resizable) return false;
    dragging_ = true;
    drag_window_ = window;
    drag_part_ = part;
    drag_origin_ = point;
    drag_start_ = (*entry).content;
    return true;
}

std::optional<Rect> WebDesk::frame_drag_to(Point point) {
    if (!dragging_ || !finite_point(point)) return std::nullopt;
    const Layer* entry = layer(drag_window_);
    if (entry == nullptr) {
        dragging_ = false;
        return std::nullopt;
    }
    const double dx = point.x - drag_origin_.x;
    const double dy = point.y - drag_origin_.y;
    Rect next = drag_start_;
    const double minimum_width = std::max(1.0, (*entry).minimum.width);
    const double minimum_height = std::max(1.0, (*entry).minimum.height);
    const bool moves_left = drag_part_ == WebDeskPart::edge_left || drag_part_ == WebDeskPart::corner_top_left ||
                            drag_part_ == WebDeskPart::corner_bottom_left;
    const bool moves_right = drag_part_ == WebDeskPart::edge_right || drag_part_ == WebDeskPart::corner_top_right ||
                             drag_part_ == WebDeskPart::corner_bottom_right;
    const bool moves_top = drag_part_ == WebDeskPart::edge_top || drag_part_ == WebDeskPart::corner_top_left ||
                           drag_part_ == WebDeskPart::corner_top_right;
    const bool moves_bottom = drag_part_ == WebDeskPart::edge_bottom ||
                              drag_part_ == WebDeskPart::corner_bottom_left ||
                              drag_part_ == WebDeskPart::corner_bottom_right;
    if (drag_part_ == WebDeskPart::title_bar) {
        next.x = drag_start_.x + dx;
        next.y = drag_start_.y + dy;
    }
    if (moves_left) {
        // The right edge stays; the width never goes below the minimum.
        const double width = std::max(minimum_width, drag_start_.width - dx);
        next.x = drag_start_.right() - width;
        next.width = width;
    }
    if (moves_right) next.width = std::max(minimum_width, drag_start_.width + dx);
    if (moves_top) {
        const double height = std::max(minimum_height, drag_start_.height - dy);
        next.y = drag_start_.bottom() - height;
        next.height = height;
    }
    if (moves_bottom) next.height = std::max(minimum_height, drag_start_.height + dy);
    const std::optional<Rect> applied = set_content(drag_window_, next);
    return applied;
}

Rect WebDesk::placement(HostWindowKind kind, HostWindowId owner, Size size) const {
    Rect anchor{0.0, 0.0, extent_.width, extent_.height};
    const Layer* owning = layer(owner);
    if (owning != nullptr) anchor = (*owning).content;
    Rect result{anchor.x + (anchor.width - size.width) * 0.5, anchor.y + (anchor.height - size.height) * 0.5,
                size.width, size.height};
    const bool floating = host_window_kind_traits(kind).floating;
    if (floating && owning != nullptr) {
        result.x = anchor.right() - size.width - cascade_step;
        result.y = anchor.y + cascade_step + title_bar_height;
    }
    Layer probe{};
    probe.kind = kind;
    // Moved on while another layer's client area starts at the same point,
    // at most once per layer (the desk holds at most maximum_windows).
    for (std::size_t attempt = 0U; attempt < layers_.size(); ++attempt) {
        bool taken = false;
        for (std::size_t index = 0U; index < layers_.size(); ++index) {
            const Rect other = layers_[index].content;
            if (std::abs(other.x - result.x) < 1.0 && std::abs(other.y - result.y) < 1.0) taken = true;
        }
        if (!taken) break;
        result.x += cascade_step;
        result.y += cascade_step;
    }
    const Rect kept = clamped(probe, result);
    return kept;
}

} // namespace gui_forms::host::web_detail
