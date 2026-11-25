// The single-surface window manager's desk: layers, states, stacking,
// frames, activation and modal blocking (surface_desk.hpp).
#include "surface_desk.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms::host::surface_detail {
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

SurfaceDesk::SurfaceDesk() {
    layers_.reserve(HostWindowing::maximum_windows);
    order_.reserve(HostWindowing::maximum_windows);
    family_.reserve(HostWindowing::maximum_windows);
    modals_.reserve(HostWindowing::maximum_modal_depth);
}

SurfaceDesk::Layer* SurfaceDesk::layer(HostWindowId window) noexcept {
    for (std::size_t index = 0U; index < layers_.size(); ++index) {
        if (layers_[index].id == window) return &layers_[index];
    }
    return nullptr;
}

const SurfaceDesk::Layer* SurfaceDesk::layer(HostWindowId window) const noexcept {
    for (std::size_t index = 0U; index < layers_.size(); ++index) {
        if (layers_[index].id == window) return &layers_[index];
    }
    return nullptr;
}

bool SurfaceDesk::is_framed(const Layer& entry) noexcept {
    const bool framed = entry.kind != HostWindowKind::primary && entry.kind != HostWindowKind::popup;
    return framed;
}

double SurfaceDesk::top_of(const Layer& entry) const noexcept {
    const double top = entry.title_bar ? theme_.title_bar_height : theme_.border_width;
    return top;
}

Rect SurfaceDesk::outer_of(const Layer& entry) const noexcept {
    if (!is_framed(entry)) return entry.content;
    const double top = top_of(entry);
    const Rect outer{entry.content.x - theme_.border_width, entry.content.y - top,
                     entry.content.width + 2.0 * theme_.border_width,
                     entry.content.height + top + theme_.border_width};
    return outer;
}

void SurfaceDesk::set_theme(const SurfaceFrameTheme& theme) {
    theme_ = theme;
    // Measures that would make the frame meaningless fall back to 0.
    if (!(theme_.border_width >= 0.0)) theme_.border_width = 0.0;
    if (!(theme_.title_bar_height >= theme_.border_width)) theme_.title_bar_height = theme_.border_width;
    if (!(theme_.grip_outside >= 0.0)) theme_.grip_outside = 0.0;
    if (!(theme_.grip_inside >= 0.0)) theme_.grip_inside = 0.0;
    if (!(theme_.title_box_width >= 0.0)) theme_.title_box_width = 0.0;
    if (!(theme_.capsule_width >= 0.0)) theme_.capsule_width = 0.0;
    if (!(theme_.capsule_height >= 0.0)) theme_.capsule_height = 0.0;
    if (!(theme_.capsule_margin >= 0.0)) theme_.capsule_margin = 0.0;
    if (!(theme_.capsule_help_width >= theme_.capsule_height)) theme_.capsule_help_width = theme_.capsule_height;
    if (theme_.capsule_help_width > theme_.capsule_width) theme_.capsule_help_width = theme_.capsule_width;
    if (theme_.snap_preview_alpha > 255U) theme_.snap_preview_alpha = 255U;
    for (std::size_t index = 0U; index < layers_.size(); ++index) {
        Layer& entry = layers_[index];
        entry.content = clamped(entry, entry.content);
    }
}

bool SurfaceDesk::shows_minimize(const Layer& entry) noexcept {
    const bool shows = can_minimize(entry) && entry.title_bar;
    return shows;
}

bool SurfaceDesk::shows_maximize(const Layer& entry) noexcept {
    const bool shows = can_maximize(entry) && entry.title_bar;
    return shows;
}

bool SurfaceDesk::is_shown(const Layer& entry) const noexcept {
    if (!entry.visible) return false;
    // No layer of the ownership chain is minimised; chains are at most
    // maximum_windows long.
    const Layer* current = &entry;
    for (std::size_t depth = 0U; depth <= HostWindowing::maximum_windows && current != nullptr; ++depth) {
        if ((*current).state == SurfaceWindowState::minimized) return false;
        if (!(*current).owner.valid()) return true;
        current = layer((*current).owner);
    }
    return true;
}

Rect SurfaceDesk::maximized_content(const Layer& entry) const noexcept {
    const double top = top_of(entry);
    const Rect whole{theme_.border_width, top, std::max(1.0, extent_.width - 2.0 * theme_.border_width),
                     std::max(1.0, extent_.height - top - theme_.border_width)};
    return whole;
}

// Half the surface's width, the left half taking the odd unit; the frame
// inside it.
Rect SurfaceDesk::tiled_content(const Layer& entry, SurfaceTileSide side) const noexcept {
    const double top = top_of(entry);
    const double left_width = std::ceil(extent_.width * 0.5);
    const double half_left = side == SurfaceTileSide::left ? 0.0 : left_width;
    const double half_width = side == SurfaceTileSide::left ? left_width : extent_.width - left_width;
    const Rect half{half_left + theme_.border_width, top, std::max(1.0, half_width - 2.0 * theme_.border_width),
                    std::max(1.0, extent_.height - top - theme_.border_width)};
    return half;
}

Rect SurfaceDesk::state_content(const Layer& entry, Rect content) const noexcept {
    switch (entry.state) {
    case SurfaceWindowState::maximized:
        return maximized_content(entry);
    case SurfaceWindowState::tiled_left:
        return tiled_content(entry, SurfaceTileSide::left);
    case SurfaceWindowState::tiled_right:
        return tiled_content(entry, SurfaceTileSide::right);
    case SurfaceWindowState::normal:
    case SurfaceWindowState::minimized:
    default:
        return content;
    }
}

bool SurfaceDesk::shows_capsule(const Layer& entry) const noexcept {
    const bool shows = is_framed(entry) && entry.title_bar &&
                       theme_.window_controls == SurfaceWindowControls::capsule && theme_.capsule_width > 0.0;
    return shows;
}

// At the controls' end of the title bar, capsule_margin in from the edge,
// centred in the title bar's height below the frame's top edge.
Rect SurfaceDesk::capsule_of(const Layer& entry) const noexcept {
    if (!shows_capsule(entry)) return Rect{};
    const Rect outer = outer_of(entry);
    const double bar_top = outer.y + theme_.border_width;
    const double bar_height = theme_.title_bar_height - theme_.border_width;
    const double height = std::min(theme_.capsule_height, bar_height);
    const double width = theme_.capsule_width;
    const double left = theme_.controls == SurfaceControlsSide::left
                            ? outer.x + theme_.border_width + theme_.capsule_margin
                            : outer.right() - theme_.border_width - theme_.capsule_margin - width;
    const Rect result{left, bar_top + (bar_height - height) * 0.5, width, height};
    return result;
}

Rect SurfaceDesk::capsule(HostWindowId window) const noexcept {
    const Layer* entry = layer(window);
    if (entry == nullptr) return Rect{};
    const Rect result = capsule_of(*entry);
    return result;
}

Rect SurfaceDesk::tiled_outer(SurfaceTileSide side) const noexcept {
    Layer probe{};
    probe.kind = HostWindowKind::standard;
    const Rect content = tiled_content(probe, side);
    probe.content = content;
    const Rect result = outer_of(probe);
    return result;
}

bool SurfaceDesk::can_minimize(const Layer& entry) noexcept {
    const bool allowed = is_framed(entry) && entry.minimizable;
    return allowed;
}

bool SurfaceDesk::can_maximize(const Layer& entry) noexcept {
    const bool allowed = is_framed(entry) && entry.resizable;
    return allowed;
}

int SurfaceDesk::band(const Layer& entry) noexcept {
    if (entry.kind == HostWindowKind::primary) return primary_band;
    if (entry.kind != HostWindowKind::popup) return window_band;
    if (host_transient_is_splash(entry.transient)) return splash_band;
    return popup_band;
}

std::size_t SurfaceDesk::band_end(int band_index) const noexcept {
    // order_ is sorted by band, so the first layer of a higher band ends it.
    std::size_t index = 0U;
    while (index < order_.size()) {
        const Layer* entry = layer(order_[index]);
        if (entry != nullptr && band(*entry) > band_index) break;
        ++index;
    }
    return index;
}

Rect SurfaceDesk::clamped(const Layer& entry, Rect content) const noexcept {
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
    if (entry.state != SurfaceWindowState::normal && entry.state != SurfaceWindowState::minimized)
        return state_content(entry, result);
    // The frame inside the canvas: the size first, then the position.
    const double widest = std::max(1.0, extent_.width - 2.0 * theme_.border_width);
    const double frame_top = top_of(entry);
    const double tallest = std::max(1.0, extent_.height - frame_top - theme_.border_width);
    result.width = std::min(std::max(result.width, entry.minimum.width), widest);
    result.height = std::min(std::max(result.height, entry.minimum.height), tallest);
    // The frame's top stays inside the surface, and title_bar_kept of it
    // inside horizontally.
    const double top_limit = frame_top;
    const double bottom_limit = std::max(top_limit, extent_.height - theme_.border_width);
    result.y = std::clamp(result.y, top_limit, bottom_limit);
    const double kept = std::min(theme_.title_bar_kept, result.width);
    const double left_limit = kept - result.width;
    const double right_limit = std::max(left_limit, extent_.width - kept);
    result.x = std::clamp(result.x, left_limit, right_limit);
    return result;
}

void SurfaceDesk::set_extent(Size extent) {
    const double width = std::isfinite(extent.width) && extent.width >= 1.0 ? extent.width : 1.0;
    const double height = std::isfinite(extent.height) && extent.height >= 1.0 ? extent.height : 1.0;
    extent_ = Size{width, height};
    for (std::size_t index = 0U; index < layers_.size(); ++index) {
        Layer& entry = layers_[index];
        entry.content = clamped(entry, entry.content);
    }
}

bool SurfaceDesk::add(const SurfaceDeskLayerInit& init) {
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
    entry.minimizable = init.minimizable;
    entry.title_bar = init.title_bar;
    entry.outside_modality = init.outside_modality;
    entry.content = clamped(entry, init.content);
    const std::size_t at = band_end(band(entry));
    layers_.push_back(entry);
    order_.insert(order_.begin() + static_cast<std::ptrdiff_t>(at), entry.id);
    return true;
}

void SurfaceDesk::remove(HostWindowId window) noexcept {
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

void SurfaceDesk::set_visible(HostWindowId window, bool visible) noexcept {
    Layer* entry = layer(window);
    if (entry == nullptr) return;
    (*entry).visible = visible;
    if (!visible) keyboard_left(window);
}

bool SurfaceDesk::shown(HostWindowId window) const noexcept {
    const Layer* entry = layer(window);
    const bool result = entry != nullptr && is_shown(*entry);
    return result;
}

void SurfaceDesk::keyboard_left(HostWindowId window) {
    // The active window, or one it owns, left the drawing.
    if (!active_.valid()) return;
    if (active_ != window && !owned_by(active_, window)) return;
    active_ = {};
    static_cast<void>(activate_topmost());
}

bool SurfaceDesk::minimize(HostWindowId window) {
    Layer* entry = layer(window);
    if (entry == nullptr || !can_minimize(*entry) || blocked(window)) return false;
    if ((*entry).state == SurfaceWindowState::minimized) return false;
    (*entry).minimized_from = (*entry).state;
    (*entry).state = SurfaceWindowState::minimized;
    if (dragging_ && (drag_window_ == window || owned_by(drag_window_, window))) dragging_ = false;
    keyboard_left(window);
    return true;
}

bool SurfaceDesk::maximize(HostWindowId window) {
    Layer* entry = layer(window);
    if (entry == nullptr || !can_maximize(*entry) || blocked(window)) return false;
    if ((*entry).state == SurfaceWindowState::maximized) return false;
    const bool from_normal = (*entry).state == SurfaceWindowState::normal ||
                             ((*entry).state == SurfaceWindowState::minimized &&
                              (*entry).minimized_from == SurfaceWindowState::normal);
    if (from_normal) (*entry).restore_content = (*entry).content;
    (*entry).minimized_from = SurfaceWindowState::normal;
    (*entry).state = SurfaceWindowState::maximized;
    (*entry).content = maximized_content(*entry);
    if (dragging_ && drag_window_ == window) dragging_ = false;
    return true;
}

bool SurfaceDesk::tile(HostWindowId window, SurfaceTileSide side) {
    Layer* entry = layer(window);
    if (entry == nullptr || !can_maximize(*entry) || blocked(window)) return false;
    const SurfaceWindowState wanted =
        side == SurfaceTileSide::left ? SurfaceWindowState::tiled_left : SurfaceWindowState::tiled_right;
    if ((*entry).state == wanted) return false;
    const bool from_normal = (*entry).state == SurfaceWindowState::normal ||
                             ((*entry).state == SurfaceWindowState::minimized &&
                              (*entry).minimized_from == SurfaceWindowState::normal);
    if (from_normal) (*entry).restore_content = (*entry).content;
    (*entry).minimized_from = SurfaceWindowState::normal;
    (*entry).state = wanted;
    (*entry).content = tiled_content(*entry, side);
    if (dragging_ && drag_window_ == window) dragging_ = false;
    return true;
}

bool SurfaceDesk::restore(HostWindowId window) {
    Layer* entry = layer(window);
    if (entry == nullptr) return false;
    if ((*entry).state == SurfaceWindowState::minimized) {
        (*entry).state = (*entry).minimized_from;
        (*entry).minimized_from = SurfaceWindowState::normal;
        (*entry).content = clamped(*entry, (*entry).content);
        return true;
    }
    if ((*entry).state != SurfaceWindowState::normal) {
        (*entry).state = SurfaceWindowState::normal;
        (*entry).content = clamped(*entry, (*entry).restore_content);
        return true;
    }
    return false;
}

SurfaceWindowState SurfaceDesk::state(HostWindowId window) const noexcept {
    const Layer* entry = layer(window);
    const SurfaceWindowState result = entry == nullptr ? SurfaceWindowState::normal : (*entry).state;
    return result;
}

bool SurfaceDesk::visible(HostWindowId window) const noexcept {
    const Layer* entry = layer(window);
    const bool shown = entry != nullptr && (*entry).visible;
    return shown;
}

bool SurfaceDesk::contains(HostWindowId window) const noexcept {
    const bool found = layer(window) != nullptr;
    return found;
}

std::optional<Rect> SurfaceDesk::set_content(HostWindowId window, Rect content) {
    Layer* entry = layer(window);
    if (entry == nullptr) return std::nullopt;
    if ((*entry).state != SurfaceWindowState::minimized) (*entry).state = SurfaceWindowState::normal;
    (*entry).content = clamped(*entry, content);
    const std::optional<Rect> applied = (*entry).content;
    return applied;
}

std::optional<Rect> SurfaceDesk::content(HostWindowId window) const noexcept {
    const Layer* entry = layer(window);
    if (entry == nullptr) return std::nullopt;
    const std::optional<Rect> result = (*entry).content;
    return result;
}

std::optional<Rect> SurfaceDesk::outer(HostWindowId window) const noexcept {
    const Layer* entry = layer(window);
    if (entry == nullptr) return std::nullopt;
    const std::optional<Rect> result = outer_of(*entry);
    return result;
}

bool SurfaceDesk::framed(HostWindowId window) const noexcept {
    const Layer* entry = layer(window);
    const bool result = entry != nullptr && is_framed(*entry);
    return result;
}

bool SurfaceDesk::owned_by(HostWindowId window, HostWindowId ancestor) const noexcept {
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

void SurfaceDesk::raise(HostWindowId window) {
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

void SurfaceDesk::scene(std::vector<SurfaceDeskQuad>& quads) const {
    quads.clear();
    for (std::size_t index = 0U; index < order_.size(); ++index) {
        const Layer* entry = layer(order_[index]);
        if (entry == nullptr || !is_shown(*entry)) continue;
        SurfaceDeskQuad quad{};
        quad.window = (*entry).id;
        quad.content = (*entry).content;
        quad.outer = outer_of(*entry);
        quad.framed = is_framed(*entry);
        quad.active = (*entry).id == active_;
        quad.minimize_box = shows_minimize(*entry);
        quad.capsule = shows_capsule(*entry);
        quad.minimize_box = shows_minimize(*entry) && !quad.capsule;
        quad.maximize_box = shows_maximize(*entry) && !quad.capsule;
        quad.state = (*entry).state;
        quads.push_back(quad);
    }
}

void SurfaceDesk::tabs(std::vector<SurfaceDeskTab>& tabs) const {
    tabs.clear();
    double left = theme_.tab_gap;
    const double top = extent_.height - theme_.tab_height - theme_.tab_gap;
    for (std::size_t index = 0U; index < order_.size(); ++index) {
        const Layer* entry = layer(order_[index]);
        if (entry == nullptr || !(*entry).visible || (*entry).state != SurfaceWindowState::minimized) continue;
        SurfaceDeskTab tab{};
        tab.window = (*entry).id;
        tab.bounds = Rect{left, top, theme_.tab_width, theme_.tab_height};
        tabs.push_back(tab);
        left += theme_.tab_width + theme_.tab_gap;
    }
}

Rect SurfaceDesk::box_of(const Layer& entry, SurfaceDeskPart part) const noexcept {
    if (!is_framed(entry) || !entry.title_bar) return Rect{};
    if (shows_capsule(entry)) {
        // Left half, the question mark's middle, right half.
        const Rect whole = capsule_of(entry);
        const double help = theme_.capsule_help_width;
        const double half = (whole.width - help) * 0.5;
        switch (part) {
        case SurfaceDeskPart::capsule_left:
            return Rect{whole.x, whole.y, half, whole.height};
        case SurfaceDeskPart::capsule_help:
            return Rect{whole.x + half, whole.y, help, whole.height};
        case SurfaceDeskPart::capsule_right:
            return Rect{whole.x + half + help, whole.y, whole.right() - (whole.x + half + help), whole.height};
        // Parts without a rectangle of their own (none, client, title_bar, ...): an empty one.
        default:
            return Rect{};
        }
    }
    const Rect outer = outer_of(entry);
    const double top = outer.y + theme_.border_width;
    const double height = theme_.title_bar_height - theme_.border_width;
    const double width = theme_.title_box_width;
    // From the controls' end inward: close, then maximise and minimise
    // where shown. `slot` counts boxes from that end.
    std::size_t slot = 0U;
    bool found = part == SurfaceDeskPart::close_box;
    if (!found) {
        ++slot;
        if (shows_maximize(entry)) {
            found = part == SurfaceDeskPart::maximize_box;
            if (!found) ++slot;
        }
    }
    if (!found && shows_minimize(entry)) found = part == SurfaceDeskPart::minimize_box;
    if (!found) return Rect{};
    const double offset = static_cast<double>(slot + 1U) * width;
    const double left = theme_.controls == SurfaceControlsSide::left
                            ? outer.x + theme_.border_width + offset - width
                            : outer.right() - theme_.border_width - offset;
    const Rect result{left, top, width, height};
    return result;
}

Rect SurfaceDesk::box(HostWindowId window, SurfaceDeskPart part) const noexcept {
    const Layer* entry = layer(window);
    if (entry == nullptr) return Rect{};
    const Rect result = box_of(*entry, part);
    return result;
}

SurfaceDeskPart SurfaceDesk::frame_part(const Layer& entry, Point point) const noexcept {
    const Rect outer = outer_of(entry);
    const bool resizable = entry.resizable && entry.state == SurfaceWindowState::normal;
    const double grip = std::max(theme_.border_width, theme_.grip_inside);
    const bool left = point.x < outer.x + grip;
    const bool right = point.x >= outer.right() - grip;
    const bool top = point.y < outer.y + grip;
    const bool bottom = point.y >= outer.bottom() - grip;
    if (resizable) {
        if (top && left) return SurfaceDeskPart::corner_top_left;
        if (top && right) return SurfaceDeskPart::corner_top_right;
        if (bottom && left) return SurfaceDeskPart::corner_bottom_left;
        if (bottom && right) return SurfaceDeskPart::corner_bottom_right;
        if (left) return SurfaceDeskPart::edge_left;
        if (right) return SurfaceDeskPart::edge_right;
        if (top) return SurfaceDeskPart::edge_top;
        if (bottom) return SurfaceDeskPart::edge_bottom;
    }
    if (point.y < entry.content.y) {
        if (box_of(entry, SurfaceDeskPart::capsule_left).contains(point)) return SurfaceDeskPart::capsule_left;
        if (box_of(entry, SurfaceDeskPart::capsule_help).contains(point)) return SurfaceDeskPart::capsule_help;
        if (box_of(entry, SurfaceDeskPart::capsule_right).contains(point)) return SurfaceDeskPart::capsule_right;
        if (box_of(entry, SurfaceDeskPart::close_box).contains(point)) return SurfaceDeskPart::close_box;
        if (box_of(entry, SurfaceDeskPart::maximize_box).contains(point)) return SurfaceDeskPart::maximize_box;
        if (box_of(entry, SurfaceDeskPart::minimize_box).contains(point)) return SurfaceDeskPart::minimize_box;
        return SurfaceDeskPart::title_bar;
    }
    if (entry.content.contains(point)) return SurfaceDeskPart::client;
    // On the border of a window that is not resizable.
    return SurfaceDeskPart::title_bar;
}

SurfaceDeskHit SurfaceDesk::hit(Point point) const noexcept {
    SurfaceDeskHit result{};
    if (!finite_point(point)) return result;
    bool tabs_tried = false;
    for (std::size_t index = order_.size(); index > 0U; --index) {
        const Layer* entry = layer(order_[index - 1U]);
        if (entry == nullptr) continue;
        // The tabs lie above the window band and below the popups.
        if (!tabs_tried && band(*entry) < popup_band) {
            tabs_tried = true;
            const SurfaceDeskHit tab = tab_hit(point);
            if (tab.window.valid()) return tab;
        }
        if (!is_shown(*entry)) continue;
        // A Splash takes no input: the point passes through it.
        if ((*entry).kind == HostWindowKind::popup && host_transient_is_splash((*entry).transient)) continue;
        Rect reach = outer_of(*entry);
        if (is_framed(*entry) && (*entry).resizable) {
            reach = Rect{reach.x - theme_.grip_outside, reach.y - theme_.grip_outside, reach.width + 2.0 * theme_.grip_outside,
                         reach.height + 2.0 * theme_.grip_outside};
        }
        if (!reach.contains(point)) continue;
        result.window = (*entry).id;
        result.local = Point{point.x - (*entry).content.x, point.y - (*entry).content.y};
        if (!is_framed(*entry)) {
            result.part = SurfaceDeskPart::client;
            return result;
        }
        // The grips beyond the border count as the border itself.
        const Rect outer = outer_of(*entry);
        const double grip = std::max(theme_.border_width, theme_.grip_inside) * 0.5;
        const Point inside{std::clamp(point.x, outer.x, std::max(outer.x, outer.right() - grip)),
                           std::clamp(point.y, outer.y, std::max(outer.y, outer.bottom() - grip))};
        result.part = frame_part(*entry, inside);
        return result;
    }
    if (!tabs_tried) result = tab_hit(point);
    return result;
}

SurfaceDeskHit SurfaceDesk::tab_hit(Point point) const noexcept {
    SurfaceDeskHit result{};
    double left = theme_.tab_gap;
    const double top = extent_.height - theme_.tab_height - theme_.tab_gap;
    for (std::size_t index = 0U; index < order_.size(); ++index) {
        const Layer* entry = layer(order_[index]);
        if (entry == nullptr || !(*entry).visible || (*entry).state != SurfaceWindowState::minimized) continue;
        const Rect bounds{left, top, theme_.tab_width, theme_.tab_height};
        left += theme_.tab_width + theme_.tab_gap;
        if (!bounds.contains(point)) continue;
        result.window = (*entry).id;
        result.part = SurfaceDeskPart::minimized_tab;
        return result;
    }
    return result;
}

void SurfaceDesk::begin_modal(HostWindowId modal) {
    ModalScope scope{};
    scope.modal = modal;
    scope.blocked.reserve(layers_.size());
    for (std::size_t index = 0U; index < layers_.size(); ++index) {
        const HostWindowId candidate = layers_[index].id;
        if (owned_by(candidate, modal) || layers_[index].outside_modality) continue;
        scope.blocked.push_back(candidate);
    }
    modals_.push_back(std::move(scope));
}

void SurfaceDesk::end_modal(HostWindowId modal) noexcept {
    for (std::size_t index = modals_.size(); index > 0U; --index) {
        if (modals_[index - 1U].modal != modal) continue;
        modals_.erase(modals_.begin() + static_cast<std::ptrdiff_t>(index - 1U));
        break;
    }
}

bool SurfaceDesk::blocked(HostWindowId window) const noexcept {
    for (std::size_t scope = 0U; scope < modals_.size(); ++scope) {
        const std::vector<HostWindowId>& list = modals_[scope].blocked;
        for (std::size_t index = 0U; index < list.size(); ++index) {
            if (list[index] == window) return true;
        }
    }
    return false;
}

bool SurfaceDesk::takes_keyboard(const Layer& entry) const noexcept {
    if (!is_shown(entry)) return false;
    if (entry.kind == HostWindowKind::popup) return false;
    const bool free = !blocked(entry.id);
    return free;
}

HostWindowId SurfaceDesk::activate(HostWindowId window, bool takes_focus) {
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
    if (chosen == nullptr || !is_shown(*chosen)) return active_;
    active_ = target;
    raise(target);
    return active_;
}

HostWindowId SurfaceDesk::activate_topmost() {
    for (std::size_t index = order_.size(); index > 0U; --index) {
        const Layer* entry = layer(order_[index - 1U]);
        if (entry == nullptr || !takes_keyboard(*entry)) continue;
        active_ = (*entry).id;
        return active_;
    }
    active_ = {};
    return active_;
}

bool SurfaceDesk::begin_frame_drag(HostWindowId window, SurfaceDeskPart part, Point point) {
    const Layer* entry = layer(window);
    if (entry == nullptr || !is_framed(*entry) || blocked(window) || !finite_point(point)) return false;
    if ((*entry).state != SurfaceWindowState::normal) return false;
    const bool grip = part == SurfaceDeskPart::title_bar || part == SurfaceDeskPart::edge_left ||
                      part == SurfaceDeskPart::edge_right || part == SurfaceDeskPart::edge_top ||
                      part == SurfaceDeskPart::edge_bottom || part == SurfaceDeskPart::corner_top_left ||
                      part == SurfaceDeskPart::corner_top_right || part == SurfaceDeskPart::corner_bottom_left ||
                      part == SurfaceDeskPart::corner_bottom_right;
    if (!grip) return false;
    const bool resizing = part != SurfaceDeskPart::title_bar;
    if (resizing && !(*entry).resizable) return false;
    dragging_ = true;
    drag_window_ = window;
    drag_part_ = part;
    drag_origin_ = point;
    drag_start_ = (*entry).content;
    return true;
}

bool SurfaceDesk::begin_capsule_move(HostWindowId window, Point point) {
    Layer* entry = layer(window);
    if (entry == nullptr || !is_framed(*entry) || blocked(window) || !finite_point(point)) return false;
    if ((*entry).state == SurfaceWindowState::minimized) return false;
    if ((*entry).state != SurfaceWindowState::normal) {
        // The point keeps its fraction of the frame's width and its
        // distance below the frame's top.
        const Rect before = outer_of(*entry);
        const double fraction = before.width > 0.0 ? std::clamp((point.x - before.x) / before.width, 0.0, 1.0) : 0.5;
        const double below = point.y - before.y;
        Rect normal = (*entry).restore_content;
        (*entry).state = SurfaceWindowState::normal;
        const double outer_width = normal.width + 2.0 * theme_.border_width;
        normal.x = point.x - fraction * outer_width + theme_.border_width;
        normal.y = point.y - below + top_of(*entry);
        (*entry).content = clamped(*entry, normal);
    }
    const bool begun = begin_frame_drag(window, SurfaceDeskPart::title_bar, point);
    return begun;
}

std::optional<Rect> SurfaceDesk::frame_drag_to(Point point) {
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
    const bool moves_left = drag_part_ == SurfaceDeskPart::edge_left || drag_part_ == SurfaceDeskPart::corner_top_left ||
                            drag_part_ == SurfaceDeskPart::corner_bottom_left;
    const bool moves_right = drag_part_ == SurfaceDeskPart::edge_right || drag_part_ == SurfaceDeskPart::corner_top_right ||
                             drag_part_ == SurfaceDeskPart::corner_bottom_right;
    const bool moves_top = drag_part_ == SurfaceDeskPart::edge_top || drag_part_ == SurfaceDeskPart::corner_top_left ||
                           drag_part_ == SurfaceDeskPart::corner_top_right;
    const bool moves_bottom = drag_part_ == SurfaceDeskPart::edge_bottom ||
                              drag_part_ == SurfaceDeskPart::corner_bottom_left ||
                              drag_part_ == SurfaceDeskPart::corner_bottom_right;
    if (drag_part_ == SurfaceDeskPart::title_bar) {
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

Rect SurfaceDesk::placement(HostWindowKind kind, HostWindowId owner, Size size) const {
    Rect anchor{0.0, 0.0, extent_.width, extent_.height};
    const Layer* owning = layer(owner);
    if (owning != nullptr) anchor = (*owning).content;
    Rect result{anchor.x + (anchor.width - size.width) * 0.5, anchor.y + (anchor.height - size.height) * 0.5,
                size.width, size.height};
    const bool floating = host_window_kind_traits(kind).floating;
    if (floating && owning != nullptr) {
        result.x = anchor.right() - size.width - theme_.cascade_step;
        result.y = anchor.y + theme_.cascade_step + theme_.title_bar_height;
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
        result.x += theme_.cascade_step;
        result.y += theme_.cascade_step;
    }
    const Rect kept = clamped(probe, result);
    return kept;
}

} // namespace gui_forms::host::surface_detail
