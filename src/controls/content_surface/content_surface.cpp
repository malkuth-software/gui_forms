#include "gui_forms/controls/content_surface.hpp"

#include "gui_forms/events/key_events/key_events.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"
#include "gui_forms/window.hpp"
#include "../../core/content/channel/content_channel.hpp"
#include "../../core/surface_root/access/surface_schedule_access.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace gui_forms {

namespace {

// The device extent of a logical one at `scale`, rounded outward; zero when
// it is empty or does not fit (as the sub-surface host sizes its region).
[[nodiscard]] std::uint32_t content_device_extent(double logical, double scale) noexcept {
    const double extent = std::ceil(logical * scale);
    if (!std::isfinite(extent) || extent < 1.0) return 0U;
    if (extent > static_cast<double>(std::numeric_limits<std::int32_t>::max())) return 0U;
    return static_cast<std::uint32_t>(extent);
}

[[nodiscard]] std::size_t status_index(ContentSurfaceStatus status) noexcept {
    const std::size_t index = static_cast<std::size_t>(status);
    return index;
}

// The caption's inset from the region's top left corner. Its colours, and
// the veil's, are the theme's (the interrupted state's rules).
constexpr double content_caption_inset = 8.0;
// How often a running program is checked while the watchdog is off.
constexpr FrameInterval content_worker_check_period = std::chrono::milliseconds(100);

// The events ContentSurface adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 1> content_surface_interactions{{
    {.name = "status_changed", .payload = "ContentSurfaceStatus", .mutable_payload = false},
}};

constexpr std::array<PartField<ContentSurface::Parts>, 5> content_surface_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &ContentSurface::Parts::region},
    {.declaration = {.name = "content", .role = PartRole::image, .composed_from = {}},
     .member = &ContentSurface::Parts::content},
    {.declaration = {.name = "veil", .role = PartRole::scrim, .composed_from = {}},
     .member = &ContentSurface::Parts::veil},
    {.declaration = {.name = "caption", .role = PartRole::caption, .composed_from = {}},
     .member = &ContentSurface::Parts::caption},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &ContentSurface::Parts::focus},
}};

constexpr std::array<StateField<ContentSurface::States>, 6> content_surface_state_fields{{
    {.state = AnatomyState::normal, .member = &ContentSurface::States::normal},
    {.state = AnatomyState::enabled, .member = &ContentSurface::States::enabled},
    {.state = AnatomyState::disabled, .member = &ContentSurface::States::disabled},
    {.state = AnatomyState::focused, .member = &ContentSurface::States::focused},
    {.state = AnatomyState::focus_visible, .member = &ContentSurface::States::focus_visible},
    {.state = AnatomyState::interrupted, .member = &ContentSurface::States::interrupted},
}};

constexpr std::array<PartDeclaration, 5> content_surface_parts = part_declarations(content_surface_part_fields);
constexpr std::array<AnatomyState, 6> content_surface_states = state_declarations(content_surface_state_fields);

constexpr ControlAnatomy content_surface_anatomy{
    .control_id = "content_surface",
    .parts = content_surface_parts,
    .states = content_surface_states,
    .custom_states = {},
};

} // namespace

// The sink's notices arrive here on the UI thread (posted through the
// window's dispatcher). The surface owns it and is destroyed on the same
// thread, so the back reference is valid whenever a notice runs: a notice
// posted for a destroyed surface finds the listener expired.
class ContentSurface::Notices final : public detail::ContentChannelListener {
public:
    explicit Notices(ContentSurface& surface) noexcept : surface_(surface) {}
    void content_notice() override { surface_.take_notices(); }

private:
    ContentSurface& surface_;
};

// The watchdog timer's callback, holding the surface weakly: a timer that
// outlives it (the window cancels it with the tree) reaches nothing.
class ContentSurface::WatchdogTick final {
public:
    explicit WatchdogTick(std::weak_ptr<ContentSurface> surface) noexcept : surface_(std::move(surface)) {}
    void operator()(FrameTime now) const {
        const std::shared_ptr<ContentSurface> live = surface_.lock();
        if (live) (*live).watchdog_tick(now);
    }

private:
    std::weak_ptr<ContentSurface> surface_;
};

ContentSurface::ContentSurface(StableId stable_id)
    : Control(std::move(stable_id)), notices_(std::make_shared<Notices>(*this)) {
    focusable = true;
    captions_[status_index(ContentSurfaceStatus::stale)] = "Not responding";
    captions_[status_index(ContentSurfaceStatus::abandoned)] = "Stopped";
    captions_[status_index(ContentSurfaceStatus::failed)] = "Failed";
}

ContentSurface::~ContentSurface() {
    watchdog_timer_.disconnect();
    // A bounded wait, then abandonment: destruction never joins a hung
    // program.
    stop_worker(false);
    if (channel_) detail::revoke_content_channel(*channel_);
}

void ContentSurface::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), content_surface_interactions.begin(), content_surface_interactions.end());
}

ContentFrameSink ContentSurface::open_content(bool opaque) {
    require_mutable();
    // Made before anything changes, so a failure leaves the old channel.
    const ContentRequest current = request();
    // A running program is stopped (bounded) before its channel goes.
    stop_worker(false);
    failure_.clear();
    // New content ends any forwarding; start_hosted_content restores it.
    hosted_input_ = HostedInput{};
    forwarding_ = false;
    std::shared_ptr<detail::ContentChannel> fresh = detail::make_content_channel(current.size, opaque);
    if (!fresh) throw std::bad_alloc();
    if (channel_) detail::revoke_content_channel(*channel_);
    channel_ = std::move(fresh);
    surface_ = (*channel_).surface;
    opaque_ = opaque;
    Window* const window = attached_window();
    if (window != nullptr) {
        detail::set_content_listener(*channel_, (*window).dispatcher(), notices_);
    }
    semantics_generation_ = 0U;
    register_presentation();
    update_request();
    watchdog_.start(FrameClock::now(), 0U);
    checked_once_ = false;
    set_status(ContentSurfaceStatus::waiting);
    schedule_watchdog();
    invalidate(Dirty::paint | Dirty::semantics);
    ContentFrameSink sink = detail::ContentSinkAccess::make(channel_);
    return sink;
}

void ContentSurface::close_content() {
    stop_worker(false);
    failure_.clear();
    hosted_input_ = HostedInput{};
    forwarding_ = false;
    watchdog_timer_.disconnect();
    watchdog_.stop();
    if (channel_) detail::revoke_content_channel(*channel_);
    channel_.reset();
    surface_.reset();
    register_presentation();
    set_status(ContentSurfaceStatus::disconnected);
    invalidate(Dirty::paint | Dirty::semantics);
}

void ContentSurface::start_program(std::unique_ptr<ContentProgram> program, bool opaque) {
    require_mutable();
    if (!program) throw std::invalid_argument("ContentSurface::start_program needs a program");
    ContentFrameSink sink = open_content(opaque);
    try {
        worker_ = std::make_unique<ContentWorker>(std::move(program), std::move(sink));
    } catch (...) {
        close_content();
        throw;
    }
    // The checks now also reap the program.
    schedule_watchdog();
}

HostedInput ContentSurface::start_hosted_content(HostedContentOptions options, bool opaque) {
    require_mutable();
    // Made first: a refused option or platform changes nothing.
    HostedContent hosted = make_hosted_content(std::move(options));
    start_program(std::move(hosted.program), opaque);
    hosted_input_ = hosted.input;
    forwarding_ = true;
    // A surface that already has the focus tells the program at once.
    Window* const window = attached_window();
    if (window != nullptr && (*window).control_focused(shared_from_this())) {
        static_cast<void>(hosted_input_.send_focus(true));
    }
    return hosted.input;
}

void ContentSurface::forward_key(KeyEventArgs& arguments, KeyAction action) {
    if (!forwarding_ || arguments.handled || !is_alive()) return;
    const KeyEvent event = key_event_of(arguments, action);
    static_cast<void>(hosted_input_.send_key(event));
    arguments.handled = true;
}

void ContentSurface::forward_pointer(MouseEventArgs& arguments) {
    if (!forwarding_ || arguments.handled || !is_alive()) return;
    Window* const window = attached_window();
    const double scale = window != nullptr ? (*window).scale() : 1.0;
    const PointerEvent event = pointer_event_of(arguments);
    const Point device_position{arguments.location.x * scale, arguments.location.y * scale};
    static_cast<void>(hosted_input_.send_pointer(event, device_position));
    arguments.handled = true;
}

void ContentSurface::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    forward_key(arguments, KeyAction::down);
}

void ContentSurface::on_key_up(KeyEventArgs& arguments) {
    Control::on_key_up(arguments);
    forward_key(arguments, KeyAction::up);
}

void ContentSurface::on_text_input(TextInputEvent& event) {
    Control::on_text_input(event);
    if (!forwarding_ || event.handled || !is_alive()) return;
    static_cast<void>(hosted_input_.send_text(event.text_utf8, event.composing));
    event.handled = true;
}

void ContentSurface::on_mouse_down(MouseEventArgs& arguments) {
    Control::on_mouse_down(arguments);
    forward_pointer(arguments);
}

void ContentSurface::on_mouse_up(MouseEventArgs& arguments) {
    Control::on_mouse_up(arguments);
    forward_pointer(arguments);
}

void ContentSurface::on_mouse_move(MouseEventArgs& arguments) {
    Control::on_mouse_move(arguments);
    forward_pointer(arguments);
}

void ContentSurface::on_mouse_wheel(MouseEventArgs& arguments) {
    Control::on_mouse_wheel(arguments);
    forward_pointer(arguments);
}

void ContentSurface::on_got_focus() {
    Control::on_got_focus();
    if (forwarding_ && is_alive()) static_cast<void>(hosted_input_.send_focus(true));
}

void ContentSurface::on_lost_focus() {
    Control::on_lost_focus();
    if (forwarding_) static_cast<void>(hosted_input_.send_focus(false));
}

void ContentSurface::terminate() {
    require_mutable();
    if (!channel_) return;
    if (status_ == ContentSurfaceStatus::ended || status_ == ContentSurfaceStatus::abandoned ||
        status_ == ContentSurfaceStatus::failed || status_ == ContentSurfaceStatus::terminating) {
        return;
    }
    detail::request_content_stop(*channel_);
    watchdog_.stop();
    if (!worker_) {
        // The consumer's own threads: the surface cannot see them end, so it
        // stops reading the channel now.
        detail::revoke_content_channel(*channel_);
        set_status(ContentSurfaceStatus::ended);
        return;
    }
    if ((*worker_).done()) {
        reap_worker(true);
        return;
    }
    termination_deadline_ = FrameClock::now() + termination_grace_;
    set_status(ContentSurfaceStatus::terminating);
    schedule_watchdog();
}

void ContentSurface::set_termination_grace(std::chrono::milliseconds grace) {
    require_mutable();
    termination_grace_ = std::clamp(grace, std::chrono::milliseconds(0), maximum_content_termination_grace);
}

void ContentSurface::check_worker(FrameTime now) {
    if (!worker_) return;
    if ((*worker_).done()) {
        reap_worker(true);
        return;
    }
    if (status_ == ContentSurfaceStatus::terminating && now >= termination_deadline_) {
        // The grace passed: abandon without waiting (revokes the channel and
        // detaches the thread).
        (*worker_).abandon();
        worker_.reset();
        watchdog_timer_.disconnect();
        set_status(ContentSurfaceStatus::abandoned);
    }
}

void ContentSurface::reap_worker(bool publish) {
    if (!worker_) return;
    static_cast<void>((*worker_).join_if_done());
    const ContentWorkerState outcome = (*worker_).state();
    if (outcome == ContentWorkerState::failed) failure_ = (*worker_).failure();
    worker_.reset();
    watchdog_timer_.disconnect();
    watchdog_.stop();
    // The program is done: its channel refuses everything from now on, and
    // its last frame stays on screen.
    if (channel_) detail::revoke_content_channel(*channel_);
    ContentSurfaceStatus next = ContentSurfaceStatus::ended;
    if (outcome == ContentWorkerState::failed) next = ContentSurfaceStatus::failed;
    if (outcome == ContentWorkerState::abandoned) next = ContentSurfaceStatus::abandoned;
    if (publish) {
        set_status(next);
    } else {
        status_ = next;
    }
}

void ContentSurface::stop_worker(bool publish) noexcept {
    if (!worker_) return;
    const ContentWorkerState outcome = (*worker_).stop(termination_grace_);
    if (outcome == ContentWorkerState::failed) {
        try {
            failure_ = (*worker_).failure();
        } catch (...) {
            failure_.clear();
        }
    }
    worker_.reset();
    watchdog_timer_.disconnect();
    watchdog_.stop();
    if (channel_) detail::revoke_content_channel(*channel_);
    ContentSurfaceStatus next = ContentSurfaceStatus::ended;
    if (outcome == ContentWorkerState::failed) next = ContentSurfaceStatus::failed;
    if (outcome == ContentWorkerState::abandoned) next = ContentSurfaceStatus::abandoned;
    // A surface leaving its window or being destroyed raises nothing: its
    // status is recorded for whoever reads it next.
    if (publish) {
        try {
            set_status(next);
        } catch (...) {
            status_ = next;
        }
    } else {
        status_ = next;
    }
}

void ContentSurface::set_stale_interval(FrameInterval interval) {
    require_mutable();
    watchdog_ = ContentWatchdog(interval);
    if (channel_) {
        const std::uint64_t activity = (*channel_).activity.load(std::memory_order_acquire);
        watchdog_.start(FrameClock::now(), activity);
        checked_once_ = false;
        // Stale ends with the restart; the next check decides again.
        if (status_ == ContentSurfaceStatus::stale) {
            const bool framed = (*channel_).frames_published.load(std::memory_order_relaxed) != 0U;
            set_status(framed ? ContentSurfaceStatus::live : ContentSurfaceStatus::waiting);
        }
    }
    schedule_watchdog();
}

void ContentSurface::check_watchdog(FrameTime now) {
    // A program that finished is reaped, and one whose termination's grace
    // passed is abandoned, before the watchdog looks.
    check_worker(now);
    if (!channel_) return;
    detail::ContentChannel& channel = *channel_;
    // Only a connected, unterminated channel is watched.
    if (status_ != ContentSurfaceStatus::waiting && status_ != ContentSurfaceStatus::live &&
        status_ != ContentSurfaceStatus::stale) {
        return;
    }
    const std::uint64_t activity = channel.activity.load(std::memory_order_acquire);
    const bool framed = channel.frames_published.load(std::memory_order_relaxed) != 0U;
    const ContentWatchdogVerdict verdict = watchdog_.observe(now, activity);
    if (verdict == ContentWatchdogVerdict::became_stale) {
        channel.stale_transitions.fetch_add(1U, std::memory_order_relaxed);
        set_status(ContentSurfaceStatus::stale);
        return;
    }
    if (verdict == ContentWatchdogVerdict::recovered) {
        channel.recoveries.fetch_add(1U, std::memory_order_relaxed);
        set_status(framed ? ContentSurfaceStatus::live : ContentSurfaceStatus::waiting);
        return;
    }
    if (status_ == ContentSurfaceStatus::waiting && framed) set_status(ContentSurfaceStatus::live);
}

void ContentSurface::watchdog_tick(FrameTime now) {
    const FrameInterval period = watchdog_.check_period();
    // A gap of more than two periods since the last check means the timer
    // rested (the window was occluded) or the UI thread was busy: content
    // paced by presentation publishes nothing meanwhile, so the interval
    // restarts instead of calling it stale.
    const bool gap = checked_once_ && now - last_check_ > period * 2;
    last_check_ = now;
    checked_once_ = true;
    if (gap && channel_) {
        check_worker(now);
        const std::uint64_t activity = (*channel_).activity.load(std::memory_order_acquire);
        watchdog_.start(now, activity);
        return;
    }
    check_watchdog(now);
}

void ContentSurface::set_status_caption(ContentSurfaceStatus status, std::string caption) {
    require_mutable();
    const std::size_t index = status_index(status);
    if (index >= captions_.size()) throw std::invalid_argument("ContentSurface caption for an unknown status");
    captions_[index] = std::move(caption);
    if (status == status_) invalidate(Dirty::paint);
}

const std::string& ContentSurface::status_caption(ContentSurfaceStatus status) const noexcept {
    const std::size_t index = status_index(status);
    // An unknown status reads the disconnected caption, which is empty.
    if (index >= captions_.size()) return captions_[0];
    return captions_[index];
}

ContentSurfaceStatistics ContentSurface::statistics() const noexcept {
    if (!channel_) return ContentSurfaceStatistics{};
    const ContentSurfaceStatistics result = detail::content_channel_statistics(*channel_);
    return result;
}

ContentRequest ContentSurface::request() const noexcept {
    if (!channel_) return ContentRequest{};
    const std::scoped_lock lock((*channel_).request_mutex);
    const ContentRequest result = (*channel_).request;
    return result;
}

void ContentSurface::set_content_semantics(ContentSemantics semantics) {
    require_mutable();
    const ContentSemanticsResult validity = validate_content_semantics(semantics);
    if (validity != ContentSemanticsResult::published) {
        throw std::invalid_argument("ContentSurface semantic text must be valid UTF-8 within the limit");
    }
    if (semantics == semantics_) return;
    semantics_ = std::move(semantics);
    invalidate(Dirty::semantics);
}

void ContentSurface::take_notices() {
    if (!channel_) return;
    ContentSemantics arrived{};
    const bool fresh = detail::take_content_semantics(*channel_, semantics_generation_, arrived);
    if (fresh && !(arrived == semantics_)) {
        semantics_ = std::move(arrived);
        invalidate(Dirty::semantics);
    }
    const bool framed = (*channel_).frames_published.load(std::memory_order_relaxed) != 0U;
    if (status_ == ContentSurfaceStatus::waiting && framed) set_status(ContentSurfaceStatus::live);
    // A program's end posts a notice too.
    check_worker(FrameClock::now());
    invalidate(Dirty::paint);
}

void ContentSurface::set_status(ContentSurfaceStatus status) {
    if (status == status_) return;
    status_ = status;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(status_changed_, status_);
}

void ContentSurface::schedule_watchdog() {
    watchdog_timer_.disconnect();
    Window* const window = attached_window();
    if (window == nullptr || !channel_) return;
    // A running program is checked (reaped, or abandoned after its grace)
    // even when the watchdog is off.
    const bool watching = watchdog_.enabled() && watchdog_.watching();
    if (!watching && !worker_) return;
    const FrameInterval period = watching ? watchdog_.check_period() : content_worker_check_period;
    const FrameTime now = FrameClock::now();
    const std::shared_ptr<ContentSurface> self = std::static_pointer_cast<ContentSurface>(shared_from_this());
    const WatchdogTick tick{std::weak_ptr<ContentSurface>(self)};
    checked_once_ = false;
    watchdog_timer_ = detail::SurfaceScheduleAccess::schedule_resting_ui_timer(*window, *this, period, now + period, tick);
}

void ContentSurface::update_request() {
    if (!channel_) return;
    Window* const window = attached_window();
    if (window == nullptr) return;
    const Rect bounds = committed_arranged_bounds();
    const double scale = (*window).scale();
    const ContentPixelSize size{content_device_extent(bounds.width, scale), content_device_extent(bounds.height, scale)};
    static_cast<void>(detail::set_content_request(*channel_, size, scale));
}

void ContentSurface::register_presentation() {
    Window* const window = attached_window();
    if (window == nullptr) return;
    std::shared_ptr<LiveSurface> shown = surface_;
    if (!shown) {
        // The window keeps a control's registration until the control
        // leaves it, so a closed surface registers an empty one: it holds no
        // frame, and the window presents nothing for the region.
        if (!placeholder_) {
            LiveSurfaceDescription description;
            description.width = 1U;
            description.height = 1U;
            placeholder_ = LiveSurface::create(description);
        }
        shown = placeholder_;
    }
    if (!shown) return;
    const Control::Ptr self = shared_from_this();
    // The window's host samples the newest frame of a registered surface
    // itself; a publication posts nothing to the UI thread.
    static_cast<void>((*window).queue_live_surface_presentation(self, std::move(shown)));
}

bool ContentSurface::veiled() const noexcept {
    const bool result = status_ == ContentSurfaceStatus::stale || status_ == ContentSurfaceStatus::abandoned ||
                        status_ == ContentSurfaceStatus::failed;
    return result;
}

void ContentSurface::arrange(Rect final_bounds) {
    Control::arrange(final_bounds);
    update_request();
}

void ContentSurface::on_paint(Painter& painter, Rect) {
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width, committed_arranged_bounds().height};
    const AnatomyStateSet states = anatomy_states();
    if (surface_) {
        PartPaint frame{};
        frame.role = PartRole::image;
        frame.family = ControlVisualRole::panel;
        frame.bounds = bounds;
        frame.states = states;
        frame.live_surface = surface_;
        paint_part(painter, *this, frame);
    }
    if (!veiled()) return;
    PartPaint veil{};
    veil.role = PartRole::scrim;
    veil.family = ControlVisualRole::panel;
    veil.bounds = bounds;
    veil.states = states;
    paint_part(painter, *this, veil);
    const std::string& caption = status_caption(status_);
    if (caption.empty()) return;
    PartPaint label{};
    label.role = PartRole::caption;
    label.family = ControlVisualRole::panel;
    label.font = effective_font(FontSpec{});
    label.bounds = caption_bounds(label.font);
    label.states = states;
    label.text = lend_paint_text(caption);
    label.alignment = PartTextAlignment::start;
    paint_part(painter, *this, label);
}

Rect ContentSurface::caption_bounds(const FontSpec& font) const noexcept {
    const Rect bounds = client_rectangle();
    const double width = std::max(0.0, bounds.width - 2.0 * content_caption_inset);
    const double height = std::min(std::max(0.0, bounds.height - content_caption_inset), font.size * 1.5);
    const Rect result{content_caption_inset, content_caption_inset, width, height};
    return result;
}

AnatomyStateSet ContentSurface::anatomy_states() const {
    AnatomyStateSet states = Control::anatomy_states();
    if (veiled()) states.insert(AnatomyState::interrupted);
    return states;
}

const ControlAnatomy& ContentSurface::anatomy() const noexcept {
    return content_surface_anatomy;
}

SemanticDescriptor ContentSurface::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::figure;
    if (!semantics_.name.empty()) {
        descriptor.name = semantics_.name;
    } else if (!accessible_name.read().empty()) {
        descriptor.name = accessible_name.read();
    } else {
        descriptor.name = "Content";
    }
    descriptor.value = semantics_.text;
    descriptor.description = semantics_.description.empty() ? accessible_description.read() : semantics_.description;
    if (status_ == ContentSurfaceStatus::stale || status_ == ContentSurfaceStatus::terminating) {
        descriptor.states |= SemanticState::busy;
    }
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

void ContentSurface::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = client_rectangle();
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    if (surface_) record.content = PartGeometry::from_bounds(bounds);
    if (veiled()) record.veil = PartGeometry::from_bounds(bounds);
    if (veiled() && !status_caption(status_).empty()) {
        record.caption = PartGeometry::from_bounds(caption_bounds(effective_font(FontSpec{})));
    }
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), content_surface_part_fields, record, geometry);
}

ContentSurface::Parts ContentSurface::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), content_surface_part_fields, published.parts(), record);
    return record;
}

ContentSurface::States ContentSurface::states() const {
    States record;
    read_states(content_surface_state_fields, anatomy_states(), record);
    return record;
}

void ContentSurface::on_attached_to_window() {
    Control::on_attached_to_window();
    Window* const window = attached_window();
    if (window != nullptr && channel_) {
        detail::set_content_listener(*channel_, (*window).dispatcher(), notices_);
        // A frame or text that arrived while detached posted nothing.
        take_notices();
    }
    if (window != nullptr && surface_) register_presentation();
    update_request();
    schedule_watchdog();
}

void ContentSurface::on_detaching_from_window(SurfaceRoot& former_window) noexcept {
    watchdog_timer_.disconnect();
    // Window close and application shutdown detach the surface: a running
    // program gets the bounded wait, then is abandoned.
    stop_worker(false);
    if (channel_) detail::set_content_listener(*channel_, WindowDispatcher{}, std::weak_ptr<detail::ContentChannelListener>{});
    Control::on_detaching_from_window(former_window);
}

} // namespace gui_forms
