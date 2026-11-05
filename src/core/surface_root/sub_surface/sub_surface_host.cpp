#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"

#include "gui_forms/window.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace gui_forms {

namespace {

// The device extent of a logical one at `scale`, rounded outward (resize
// rule 2); zero when it is empty or does not fit.
[[nodiscard]] std::uint32_t device_extent(double logical, double scale) noexcept {
    const double extent = std::ceil(logical * scale);
    if (!std::isfinite(extent) || extent < 1.0) return 0U;
    if (extent > static_cast<double>(std::numeric_limits<std::int32_t>::max())) return 0U;
    return static_cast<std::uint32_t>(extent);
}

} // namespace

class SubSurfaceHost::KeyReturn final : public SubSurfaceKeyReturn {
public:
    explicit KeyReturn(SubSurfaceHost& host) noexcept : host_(&host) {}
    void return_unhandled_key(const KeyEvent& event, std::uint64_t) override {
        if (host_ == nullptr) return;
        (*host_).continue_returned_key(event);
    }
    // The host stopped this target's thread or is going away.
    void revoke() noexcept { host_ = nullptr; }

private:
    // Observes the host that owns this object; null once revoked.
    SubSurfaceHost* host_{};
};

SubSurfaceHost::SubSurfaceHost(StableId stable_id)
    : Control(std::move(stable_id)) {
    set_focusable(true);
}

SubSurfaceHost::~SubSurfaceHost() {
    stop_thread();
}

void SubSurfaceHost::set_content(std::unique_ptr<SubSurfaceContent> content, bool opaque) {
    require_mutable();
    stop_thread();
    content_ = std::move(content);
    opaque_ = opaque;
    configure_region();
    invalidate(Dirty::paint);
}

SubSurfaceState SubSurfaceHost::state() const {
    if (!thread_) return SubSurfaceState::stopped;
    const SubSurfaceState state = (*thread_).state();
    return state;
}

SubSurfaceStatistics SubSurfaceHost::statistics() const {
    if (!thread_) return SubSurfaceStatistics{};
    const SubSurfaceStatistics statistics = (*thread_).statistics();
    return statistics;
}

WindowDispatcher SubSurfaceHost::dispatcher() const {
    if (!thread_) return WindowDispatcher{};
    const WindowDispatcher dispatcher = (*thread_).dispatcher();
    return dispatcher;
}

void SubSurfaceHost::arrange(Rect final_bounds) {
    Control::arrange(final_bounds);
    configure_region();
}

void SubSurfaceHost::on_paint(Painter& painter, Rect) {
    if (!surface_) return;
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width, committed_arranged_bounds().height};
    painter.draw_live_surface(surface_, bounds, 1.0);
}

void SubSurfaceHost::on_attached_to_window() {
    Control::on_attached_to_window();
    Window* const window = attached_window();
    if (window != nullptr) {
        theme_subscription_ = (*window).theme_changed().subscribe(
            *this, Delegate<const Theme&>::bind<SubSurfaceHost, &SubSurfaceHost::on_window_theme_changed>(*this));
        presentation_subscription_ = (*window).presentation_changed().subscribe(
            *this, Delegate<const PresentationSettings&>::bind<
                       SubSurfaceHost, &SubSurfaceHost::on_window_presentation_changed>(*this));
        active_subscription_ = (*window).active_changed().subscribe(
            *this, Delegate<bool>::bind<SubSurfaceHost, &SubSurfaceHost::on_window_active_changed>(*this));
    }
    configure_region();
}

void SubSurfaceHost::on_window_theme_changed(const Theme&) {
    environment_changed_ = true;
    configure_region();
}

void SubSurfaceHost::on_window_presentation_changed(const PresentationSettings&) {
    environment_changed_ = true;
    configure_region();
}

void SubSurfaceHost::on_window_active_changed(bool) {
    update_keyboard_focus(SubSurfaceFocusEntry::restore);
}

void SubSurfaceHost::on_pointer(PointerEvent& event) {
    if (!thread_) return;
    SubSurfaceInput input;
    input.kind = SubSurfaceInputKind::pointer;
    input.pointer = event;
    input.pointer.position = point_from_window(event.position);
    input.pointer.phase = EventPhase::target;
    input.pointer.handled = false;
    // Entering the host is a move inside the sub-surface: its own hover
    // starts there.
    if (input.pointer.action == PointerAction::enter) input.pointer.action = PointerAction::move;
    static_cast<void>((*thread_).post_input(input));
    event.handled = true;
}

void SubSurfaceHost::on_key(KeyEvent& event) {
    if (!thread_) return;
    SubSurfaceInput input;
    input.kind = SubSurfaceInputKind::key;
    input.key = event;
    input.key.phase = EventPhase::target;
    input.key.handled = false;
    static_cast<void>((*thread_).post_input(input));
    event.handled = true;
}

void SubSurfaceHost::on_text_input(TextInputEvent& event) {
    if (!thread_) return;
    SubSurfaceInput input;
    input.kind = SubSurfaceInputKind::text;
    input.text = event;
    input.text.handled = false;
    static_cast<void>((*thread_).post_input(input));
    event.handled = true;
}

void SubSurfaceHost::on_focus_changed(bool focused) {
    Control::on_focus_changed(focused);
    SubSurfaceFocusEntry entry = SubSurfaceFocusEntry::restore;
    SurfaceRoot* const root = attached_surface_root();
    if (focused && root != nullptr) {
        if ((*root).last_focus_traversal_ == SurfaceRoot::FocusTraversal::forward) {
            entry = SubSurfaceFocusEntry::first;
        } else if ((*root).last_focus_traversal_ == SurfaceRoot::FocusTraversal::backward) {
            entry = SubSurfaceFocusEntry::last;
        }
    }
    update_keyboard_focus(entry);
}

void SubSurfaceHost::update_keyboard_focus(SubSurfaceFocusEntry entry) {
    SurfaceRoot* const root = attached_surface_root();
    bool inside = false;
    if (thread_ && root != nullptr && (*root).active()) {
        const Control::Ptr focused = (*root).focused_control();
        inside = focused.get() == this;
    }
    if (inside == keyboard_inside_) return;
    keyboard_inside_ = inside;
    SubSurfaceInput input;
    input.kind = inside ? SubSurfaceInputKind::focus_enter : SubSurfaceInputKind::focus_leave;
    input.focus_entry = entry;
    static_cast<void>((*thread_).post_input(input));
}

void SubSurfaceHost::continue_returned_key(const KeyEvent& event) {
    SurfaceRoot* const root = attached_surface_root();
    const Control::Ptr focused = root != nullptr ? (*root).focused_control() : Control::Ptr{};
    if (focused.get() != this) {
        ++returned_keys_discarded_;
        return;
    }
    static_cast<void>((*root).resume_unhandled_key(focused, event));
}

void SubSurfaceHost::on_detaching_from_window(SurfaceRoot& former_window) noexcept {
    theme_subscription_.disconnect();
    presentation_subscription_.disconnect();
    active_subscription_.disconnect();
    // The window drops the LiveSurface registration with the control; the
    // thread is revoked and joined here, while the window still exists.
    stop_thread();
    Control::on_detaching_from_window(former_window);
}

void SubSurfaceHost::configure_region() {
    // A host inside a sub-surface has no window to compose into; it shows
    // nothing (nesting is not part of stage S2).
    Window* const window = attached_window();
    if (window == nullptr) return;
    if (!thread_ && !content_) return;
    const Rect bounds = committed_arranged_bounds();
    const double scale = (*window).scale();
    const std::uint32_t pixel_width = device_extent(bounds.width, scale);
    const std::uint32_t pixel_height = device_extent(bounds.height, scale);
    if (pixel_width == 0U || pixel_height == 0U) return;
    // Per axis (resize rules): only a changed device size or scale
    // reconfigures; a new arrangement at the same size signals nothing.
    const bool unchanged = thread_ && !environment_changed_ && pixel_width == pixel_width_ &&
                           pixel_height == pixel_height_ && scale == configured_scale_;
    if (unchanged) return;
    const Size logical{bounds.width, bounds.height};
    if (!thread_) {
        LiveSurfaceDescription description;
        description.width = pixel_width;
        description.height = pixel_height;
        description.opaque = opaque_;
        std::shared_ptr<LiveSurface> surface = LiveSurface::create(description);
        if (!surface) return;
        std::unique_ptr<PaintFramebuffer> text_measure = (*window).create_framebuffer(Size{1.0, 1.0}, scale);
        const std::shared_ptr<ApplicationResources> start_resources = (*window).resources();
        thread_ = std::make_unique<SubSurfaceThread>(std::move(content_), surface,
                                                     (*start_resources).theme_ptr(), std::move(text_measure));
        surface_ = std::move(surface);
        key_return_ = std::make_shared<KeyReturn>(*this);
        (*thread_).set_key_return((*window).dispatcher(), key_return_);
        keyboard_inside_ = false;
        update_keyboard_focus(SubSurfaceFocusEntry::restore);
    }
    ++content_epoch_;
    SubSurfaceConfiguration configuration;
    configuration.content_epoch = content_epoch_;
    configuration.logical_size = logical;
    configuration.scale = scale;
    configuration.pixel_width = pixel_width;
    configuration.pixel_height = pixel_height;
    configuration.opaque = opaque_;
    // A framebuffer only for a new device size: an environment change
    // repaints into the one the sub-surface has.
    const bool resized = pixel_width != pixel_width_ || pixel_height != pixel_height_ || scale != configured_scale_;
    if (resized) configuration.framebuffer = (*window).create_framebuffer(logical, scale);
    const std::shared_ptr<ApplicationResources> resources = (*window).resources();
    configuration.theme = (*resources).theme_ptr();
    configuration.presentation = (*window).presentation_settings();
    environment_changed_ = false;
    (*thread_).configure(std::move(configuration));
    pixel_width_ = pixel_width;
    pixel_height_ = pixel_height;
    configured_scale_ = scale;
    const Control::Ptr self = shared_from_this();
    static_cast<void>((*window).queue_live_surface_presentation(self, surface_));
    static_cast<void>((*window).require_live_surface_content(self, content_epoch_));
    invalidate(Dirty::paint);
}

void SubSurfaceHost::stop_thread() noexcept {
    if (!thread_) return;
    // Keys already posted back find the target revoked.
    (*key_return_).revoke();
    key_return_.reset();
    keyboard_inside_ = false;
    (*thread_).stop();
    thread_.reset();
    pixel_width_ = 0U;
    pixel_height_ = 0U;
    configured_scale_ = 0.0;
}

} // namespace gui_forms
