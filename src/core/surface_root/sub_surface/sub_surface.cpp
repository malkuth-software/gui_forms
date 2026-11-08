#include "gui_forms/surface_root/sub_surface/sub_surface.hpp"

#include "sub_surface_alerts.hpp"
#include "sub_surface_channel.hpp"
#include "sub_surface_tool_tips.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace gui_forms {

void SubSurfaceContent::root_started(const WindowDispatcher&) {}

void SubSurfaceContent::apply_update(SubSurfaceContentUpdate&) {}

SubSurface::SubSurface(Control::Ptr root, std::shared_ptr<ApplicationResources> resources)
    : SurfaceRoot(std::move(root), Size{}, std::move(resources)) {
    if (!resources_) {
        throw std::invalid_argument("GUI.Forms sub-surface requires application resources");
    }
    if (!(*resources_).check_access()) {
        throw std::logic_error("GUI.Forms sub-surface resources belong to another thread");
    }
    if (!root_) {
        throw std::invalid_argument("GUI.Forms sub-surface requires a retained root control");
    }
    if ((*root_).parent()) {
        throw std::logic_error("GUI.Forms sub-surface root may not already have a parent");
    }
    if (!(*root_).is_alive()) {
        throw std::logic_error("GUI.Forms sub-surface root may not be disposed");
    }
    // Tooltips are asked of the parent (design note section 15).
    std::unique_ptr<detail::SubSurfaceToolTipRequester> requester =
        std::make_unique<detail::SubSurfaceToolTipRequester>(*this);
    requester_ = requester.get();
    tool_tips_ = std::move(requester);
    // So are alerts.
    std::unique_ptr<detail::SubSurfaceAlertRequester> alerts =
        std::make_unique<detail::SubSurfaceAlertRequester>(*this);
    alert_requester_ = alerts.get();
    alerts_ = std::move(alerts);
    // Inactive until keyboard focus enters from the host (enter_focus), and
    // no wrap-around: Tab past the last stop goes back to the parent.
    active_ = false;
    focus_wraps_ = false;
    // No damage yet: the root has no size until the first configuration
    // (apply_size damages the whole region then).
    attach_subtree(root_, {});
    // Last: a constructor that throws never leaves itself registered. The
    // resources are this sub-surface's own, so a theme set on them (a
    // configuration's) reaches this root alone.
    (*resources_).attach(*this);
}

SubSurface::~SubSurface() {
    // Nothing is asked of the parent while the tree goes.
    channel_.reset();
    (*tool_tips_).shutdown();
    (*alerts_).shutdown();
    release_tree();
    (*resources_).detach(*this);
}

void SubSurface::apply_size(Size logical_size) {
    require_root_thread("sub-surface size");
    const bool was_empty = client_size_.width <= 0.0 || client_size_.height <= 0.0;
    const bool changed = resize_surface(logical_size);
    if (!changed || !was_empty) return;
    // From no size, everything is exposed.
    const Rect whole{0.0, 0.0, client_size_.width, client_size_.height};
    add_damage_all_planes(whole);
    metrics_.record_dirty_mark(whole.area());
}

void SubSurface::apply_scale(double scale) {
    require_root_thread("sub-surface scale");
    static_cast<void>(set_surface_scale(scale));
}

void SubSurface::enter_focus(SubSurfaceFocusEntry entry) {
    require_root_thread("sub-surface focus");
    set_active(true);
    if (entry == SubSurfaceFocusEntry::restore) {
        const Control::Ptr current = focused_.lock();
        if (!current || !eligible(current)) static_cast<void>(move_focus(true));
    } else if (request_focus(Control::Ptr{})) {
        // Traversal from no focus starts at the first or the last stop. A
        // validation that refuses to let the current control go keeps it.
        static_cast<void>(move_focus(entry == SubSurfaceFocusEntry::first));
    }
    // Focus may have stayed where it was: the thing under it is asked for
    // again now that the sub-surface is active.
    (*requester_).key_handled();
}

void SubSurface::leave_focus() {
    require_root_thread("sub-surface focus");
    set_active(false);
    (*requester_).focus_left();
}

namespace {

// A rectangle of device pixels, right and bottom exclusive.
struct PixelRectangle final {
    std::uint32_t left{};
    std::uint32_t top{};
    std::uint32_t right{};
    std::uint32_t bottom{};

    [[nodiscard]] bool empty() const noexcept { return right <= left || bottom <= top; }
};

[[nodiscard]] PixelRectangle unite_pixels(PixelRectangle first, PixelRectangle second) noexcept {
    if (first.empty()) return second;
    if (second.empty()) return first;
    PixelRectangle result;
    result.left = std::min(first.left, second.left);
    result.top = std::min(first.top, second.top);
    result.right = std::max(first.right, second.right);
    result.bottom = std::max(first.bottom, second.bottom);
    return result;
}

// The device pixels `logical` touches at `scale`, rounded outward and
// clipped to a width by height surface.
[[nodiscard]] PixelRectangle device_pixels_of(Rect logical, double scale, std::uint32_t width,
                                              std::uint32_t height) noexcept {
    PixelRectangle result;
    if (logical.empty() || !logical.finite()) return result;
    const double left = std::clamp(std::floor(logical.x * scale), 0.0, static_cast<double>(width));
    const double top = std::clamp(std::floor(logical.y * scale), 0.0, static_cast<double>(height));
    const double right =
        std::clamp(std::ceil((logical.x + logical.width) * scale), 0.0, static_cast<double>(width));
    const double bottom =
        std::clamp(std::ceil((logical.y + logical.height) * scale), 0.0, static_cast<double>(height));
    result.left = static_cast<std::uint32_t>(left);
    result.top = static_cast<std::uint32_t>(top);
    result.right = static_cast<std::uint32_t>(right);
    result.bottom = static_cast<std::uint32_t>(bottom);
    return result;
}

// Copies the rows of `area` from one BGRA8 buffer to another. Both buffers
// hold at least area.bottom rows of at least area.right pixels; they do not
// overlap (two distinct allocations).
void copy_pixel_rows(std::span<const std::byte> source, std::size_t source_row_bytes,
                     std::span<std::byte> destination, std::size_t destination_row_bytes,
                     PixelRectangle area) noexcept {
    const std::size_t row_offset = static_cast<std::size_t>(area.left) * 4U;
    const std::size_t row_length = static_cast<std::size_t>(area.right - area.left) * 4U;
    for (std::uint32_t row = area.top; row < area.bottom; ++row) {
        const std::byte* const from = source.data() + static_cast<std::size_t>(row) * source_row_bytes + row_offset;
        std::byte* const to =
            destination.data() + static_cast<std::size_t>(row) * destination_row_bytes + row_offset;
        std::memcpy(to, from, row_length);
    }
}

// Posted to the parent's dispatcher with one unhandled key: delivers it to
// the key return target on the parent's thread, unless the parent released
// the target first.
class DeliverReturnedKey final {
public:
    DeliverReturnedKey(std::weak_ptr<SubSurfaceKeyReturn> target, const KeyEvent& event,
                       std::uint64_t sequence) noexcept
        : target_(std::move(target)), event_(event), sequence_(sequence) {}
    void operator()() const {
        const std::shared_ptr<SubSurfaceKeyReturn> target = target_.lock();
        if (!target) return;
        (*target).return_unhandled_key(event_, sequence_);
    }

private:
    std::weak_ptr<SubSurfaceKeyReturn> target_;
    KeyEvent event_{};
    std::uint64_t sequence_{};
};

// Posted to the parent's dispatcher when the sub-surface published a
// semantic snapshot: tells the listener on the parent's thread, unless the
// parent released it first.
class NoticeSemantics final {
public:
    explicit NoticeSemantics(std::weak_ptr<SubSurfaceSemanticsListener> target) noexcept
        : target_(std::move(target)) {}
    void operator()() const {
        const std::shared_ptr<SubSurfaceSemanticsListener> target = target_.lock();
        if (!target) return;
        (*target).sub_surface_semantics_changed();
    }

private:
    std::weak_ptr<SubSurfaceSemanticsListener> target_;
};

// How long a frame waits before trying again for a LiveSurface buffer when
// the presenter holds every one (rare: three buffers by default).
constexpr std::chrono::milliseconds publish_retry_interval{4};

} // namespace

namespace detail {

// One sub-surface's thread: everything here is that thread's, from the
// moment run() starts until it returns and destroys the loop.
class SubSurfaceLoop final {
public:
    SubSurfaceLoop(std::shared_ptr<detail::SubSurfaceChannel> channel,
                   std::unique_ptr<SubSurfaceContent> content, std::shared_ptr<LiveSurface> surface,
                   std::shared_ptr<const Theme> theme, std::shared_ptr<const Theme> user_theme,
                   std::unique_ptr<PaintFramebuffer> text_measure) noexcept
        : channel_(std::move(channel)), content_(std::move(content)), surface_(std::move(surface)),
          theme_(std::move(theme)), user_theme_(std::move(user_theme)), text_measure_(std::move(text_measure)),
          pacer_(surface_) {}

    // The thread's entry. Owns the loop and destroys it before returning, so
    // the content, its controls and both framebuffers die on this thread.
    static void run(std::unique_ptr<SubSurfaceLoop> loop) noexcept;

private:
    // Builds the tree and runs the loop until stopped. Exceptions leave the
    // thread as `failed` through run().
    void run_tree();
    // Waits for a configuration, a stop request, a wake or the root's next
    // frame request. Returns false on a stop request.
    [[nodiscard]] bool wait(const SubSurface& root, std::optional<SubSurfaceConfiguration>& configuration);
    void apply(SubSurface& root, SubSurfaceConfiguration& configuration);
    // Dispatches the input taken by wait(), in posting order, and returns
    // the keys the tree left unhandled to the parent.
    void dispatch_inputs(SubSurface& root);
    // Hands the parent's answers taken by wait() to the root, in order.
    void deliver_outcomes(SubSurface& root);
    void produce(SubSurface& root);
    void publish();
    // Publishes the root's text input caret when it changed.
    void publish_caret(SubSurface& root);
    // Publishes a semantic snapshot of the tree when its semantic generation
    // moved since the last one, and tells the parent (design note section 5).
    void publish_semantics(SubSurface& root);
    void record_end(SubSurfaceState state, std::string failure) noexcept;

    std::shared_ptr<detail::SubSurfaceChannel> channel_;
    std::unique_ptr<SubSurfaceContent> content_;
    std::shared_ptr<LiveSurface> surface_;
    std::shared_ptr<const Theme> theme_;
    // The parent's layered user theme, or null; immutable and shared.
    std::shared_ptr<const Theme> user_theme_;
    std::unique_ptr<PaintFramebuffer> text_measure_;
    std::unique_ptr<PaintFramebuffer> framebuffer_{};
    std::uint64_t content_epoch_{};
    double scale_{1.0};
    // The LiveSurface's description, as this thread last set it.
    std::uint32_t surface_width_{};
    std::uint32_t surface_height_{};
    bool surface_opaque_{};
    bool configured_{};
    // A configuration (a new content epoch) applied since the last frame:
    // its frame is produced at once, not at the pacer's next tick, so the
    // parent's frame painted with that epoch finds it at its present.
    bool epoch_due_{};
    // The framebuffer pixels painted but not yet published.
    PixelRectangle unpublished_{};
    // The next publication copies the whole framebuffer (first frame of a
    // LiveSurface epoch).
    bool publish_whole_{};
    std::optional<FrameTime> retry_at_{};
    // Publishes at most one frame per display frame: the next paint waits
    // until the window showed the last one, or one and a half refreshes.
    LiveSurfacePacer pacer_;
    std::optional<FrameTime> pacing_wake_{};
    // The display time the frame schedule was last polled at.
    FrameTime motion_time_{};
    // The input wait() took from the channel; empty between turns.
    std::vector<detail::SubSurfaceInputRecord> inputs_{};
    std::vector<SurfaceRequestOutcome> outcomes_{};
    std::optional<Rect> published_caret_{};
    // The tree's semantic generation at the last published snapshot; zero
    // before the first (generations start at one).
    std::uint64_t published_semantic_generation_{};
};

void SubSurfaceLoop::run(std::unique_ptr<SubSurfaceLoop> loop) noexcept {
    SubSurfaceState end = SubSurfaceState::stopped;
    std::string failure;
    try {
        (*loop).run_tree();
    } catch (const std::exception& error) {
        end = SubSurfaceState::failed;
        failure = error.what();
    } catch (...) {
        end = SubSurfaceState::failed;
        failure = "GUI.Forms sub-surface content threw a non-standard exception";
    }
    // The content and the framebuffers go here, on this thread; the channel
    // stays alive through the copy below.
    const std::shared_ptr<detail::SubSurfaceChannel> channel = (*loop).channel_;
    loop.reset();
    const std::lock_guard<std::mutex> lock((*channel).mutex);
    (*channel).state = end;
    (*channel).failure = std::move(failure);
}

void SubSurfaceLoop::run_tree() {
    std::shared_ptr<ApplicationResources> resources = std::make_shared<ApplicationResources>();
    // A user theme is chosen before the first root shares the resources.
    if (user_theme_) (*resources).set_user_theme(user_theme_);
    (*resources).set_theme(theme_);
    Control::Ptr built = (*content_).build_root();
    if (!built) {
        throw std::invalid_argument("GUI.Forms sub-surface content built no root control");
    }
    SubSurface root(std::move(built), std::move(resources));
    root.channel_ = channel_;
    if (text_measure_) root.set_text_metrics_provider(&(*text_measure_).painter());
    root.set_dispatch_wake_handler(detail::SignalSubSurfaceChannel(channel_));
    root.set_paint_wake_handler(detail::SignalSubSurfaceChannel(channel_));
    // The window's presentation reports wake the loop for a paced frame;
    // disconnected when this scope ends, before the channel can go.
    LiveSurfaceWakeConnection presented =
        (*surface_).connect_presented_wake(detail::SignalSubSurfaceChannel(channel_));
    {
        const std::lock_guard<std::mutex> lock((*channel_).mutex);
        (*channel_).dispatcher = root.dispatcher();
        (*channel_).state = SubSurfaceState::running;
    }
    (*content_).root_started(root.dispatcher());
    std::optional<SubSurfaceConfiguration> configuration;
    while (wait(root, configuration)) {
        const bool configured = static_cast<bool>(configuration);
        if (configured) {
            apply(root, *configuration);
            if ((*configuration).update) {
                (*content_).apply_update(*(*configuration).update);
                const std::lock_guard<std::mutex> lock((*channel_).mutex);
                ++(*channel_).statistics.updates_applied;
            }
            configuration.reset();
        }
        dispatch_inputs(root);
        deliver_outcomes(root);
        const DispatchDrainResult drained = root.drain_posted_work();
        if (drained.remaining != 0U) {
            const std::lock_guard<std::mutex> lock((*channel_).mutex);
            (*channel_).signalled = true;
        }
        // Motion ticks on display time (design note section 8): the
        // vertical blank the next frame would be shown at, never earlier
        // than the last one used (a host that re-arms its clock may move
        // the phase back).
        const FrameTime shown = pacer_.display_time(FrameClock::now());
        if (shown > motion_time_) motion_time_ = shown;
        static_cast<void>(root.poll_frame_schedule(motion_time_));
        root.flush();
        // After the flush, so the things are measured in the new
        // configuration.
        if (configured) (*root.requester_).content_epoch_changed();
        produce(root);
        publish_caret(root);
        publish_semantics(root);
    }
    // Leaving the scope releases the tree on this thread (SubSurface's
    // destructor), before run() releases the content.
}

bool SubSurfaceLoop::wait(const SubSurface& root, std::optional<SubSurfaceConfiguration>& configuration) {
    std::optional<FrameTime> deadline = root.next_wake();
    if (retry_at_ && (!deadline || *retry_at_ < *deadline)) deadline = retry_at_;
    if (pacing_wake_ && (!deadline || *pacing_wake_ < *deadline)) deadline = pacing_wake_;
    std::unique_lock<std::mutex> lock((*channel_).mutex);
    detail::SubSurfaceChannel& channel = *channel_;
    while (!channel.stop_requested && !channel.pending && !channel.signalled && channel.inputs.empty() &&
           channel.outcomes.empty()) {
        if (!deadline) {
            channel.wake.wait(lock);
            continue;
        }
        if (channel.wake.wait_until(lock, *deadline) == std::cv_status::timeout) break;
    }
    if (channel.stop_requested) return false;
    ++channel.statistics.loop_turns;
    configuration.swap(channel.pending);
    if (configuration) ++channel.statistics.configurations_applied;
    inputs_.swap(channel.inputs);
    outcomes_.swap(channel.outcomes);
    channel.signalled = false;
    return true;
}

void SubSurfaceLoop::dispatch_inputs(SubSurface& root) {
    if (inputs_.empty()) return;
    WindowDispatcher parent;
    std::weak_ptr<SubSurfaceKeyReturn> target;
    {
        const std::lock_guard<std::mutex> lock((*channel_).mutex);
        parent = (*channel_).key_return_dispatcher;
        target = (*channel_).key_return;
    }
    const bool returns_keys = !target.expired();
    std::uint64_t returned = 0U;
    std::uint64_t performed = 0U;
    std::uint64_t refused = 0U;
    for (const detail::SubSurfaceInputRecord& record : inputs_) {
        const SubSurfaceInput& input = record.input;
        // A request the tree makes now carries this sequence.
        root.input_sequence_ = record.sequence;
        switch (input.kind) {
        case SubSurfaceInputKind::pointer:
            static_cast<void>(root.dispatch_pointer(input.pointer));
            break;
        case SubSurfaceInputKind::key: {
            const bool handled = root.dispatch_key(input.key);
            if (!handled && returns_keys) {
                // Posted outside the channel's lock (lock order: the
                // parent's dispatcher is never taken under it).
                static_cast<void>(parent.begin_invoke(DeliverReturnedKey(target, input.key, record.sequence)));
                ++returned;
            }
            break;
        }
        case SubSurfaceInputKind::text:
            static_cast<void>(root.dispatch_text(input.text));
            break;
        case SubSurfaceInputKind::focus_enter:
            root.enter_focus(input.focus_entry);
            break;
        case SubSurfaceInputKind::focus_leave:
            root.leave_focus();
            break;
        case SubSurfaceInputKind::semantic_action: {
            const SubSurfaceSemanticAction& semantic = input.semantic;
            const bool done =
                root.perform_semantic_action(semantic.control, semantic.virtual_child, semantic.action, semantic.value);
            if (done) {
                ++performed;
            } else {
                ++refused;
            }
            break;
        }
        default:
            throw std::invalid_argument("GUI.Forms sub-surface input kind is not a defined value");
        }
    }
    const std::uint64_t dispatched = inputs_.size();
    const std::uint64_t last_sequence = inputs_.back().sequence;
    inputs_.clear();
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    (*channel_).statistics.inputs_dispatched += dispatched;
    (*channel_).statistics.last_input_sequence = last_sequence;
    (*channel_).statistics.keys_returned += returned;
    (*channel_).statistics.semantic_actions_performed += performed;
    (*channel_).statistics.semantic_actions_refused += refused;
}

void SubSurfaceLoop::deliver_outcomes(SubSurface& root) {
    if (outcomes_.empty()) return;
    std::uint64_t stale = 0U;
    const std::uint64_t received = outcomes_.size();
    for (const SurfaceRequestOutcome& outcome : outcomes_) {
        if ((*root.requester_).receive_outcome(outcome)) continue;
        if (!(*root.alert_requester_).receive_outcome(outcome)) ++stale;
    }
    outcomes_.clear();
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    (*channel_).statistics.outcomes_received += received;
    (*channel_).statistics.outcomes_stale += stale;
}

void SubSurfaceLoop::apply(SubSurface& root, SubSurfaceConfiguration& configuration) {
    root.set_presentation_settings(configuration.presentation);
    const std::shared_ptr<ApplicationResources> resources = root.resources();
    if (configuration.theme && configuration.theme != (*resources).theme_ptr()) {
        (*resources).set_theme(configuration.theme);
    }
    if (configuration.scale != root.scale()) root.apply_scale(configuration.scale);
    root.apply_size(configuration.logical_size);
    content_epoch_ = configuration.content_epoch;
    root.content_epoch_ = configuration.content_epoch;
    if (configuration.framebuffer) {
        // The producer carries its content (resize rule 5): at an unchanged
        // scale the pixels both sizes cover stay, so only what the resize
        // exposed is painted again; at a new scale apply_scale marked the
        // whole tree.
        if (framebuffer_ && configuration.scale == scale_) {
            PixelRectangle overlap;
            overlap.right = std::min((*framebuffer_).width(), (*configuration.framebuffer).width());
            overlap.bottom = std::min((*framebuffer_).height(), (*configuration.framebuffer).height());
            copy_pixel_rows((*framebuffer_).pixels(), (*framebuffer_).row_bytes(),
                            (*configuration.framebuffer).pixels(), (*configuration.framebuffer).row_bytes(),
                            overlap);
        }
        framebuffer_ = std::move(configuration.framebuffer);
    }
    scale_ = configuration.scale;
    const bool resized = configuration.pixel_width != surface_width_ ||
                         configuration.pixel_height != surface_height_ ||
                         configuration.opaque != surface_opaque_;
    if (resized) {
        const LiveSurfaceSnapshot current = (*surface_).snapshot();
        const bool same_description = current.description.width == configuration.pixel_width &&
                                      current.description.height == configuration.pixel_height &&
                                      current.description.opaque == configuration.opaque;
        if (!same_description) {
            LiveSurfaceDescription description = current.description;
            description.width = configuration.pixel_width;
            description.height = configuration.pixel_height;
            description.opaque = configuration.opaque;
            // No write lease is held between frames, so this only fails for
            // a description LiveSurface refuses; the frames then stay at the
            // old size and the copy below is clipped to it.
            if ((*surface_).reconfigure(description)) {
                const std::lock_guard<std::mutex> lock((*channel_).mutex);
                ++(*channel_).statistics.surface_reconfigures;
            }
        }
        surface_width_ = configuration.pixel_width;
        surface_height_ = configuration.pixel_height;
        surface_opaque_ = configuration.opaque;
        publish_whole_ = true;
    }
    // Every configuration is a redraw-all (design note section 2): its first
    // frame is painted whole, so the parent pairs each epoch it sends,
    // whether or not the size, the theme or the content changed.
    const Control::Ptr top = root.root();
    if (top) (*top).invalidate(Dirty::paint);
    configured_ = true;
    epoch_due_ = true;
}

void SubSurfaceLoop::produce(SubSurface& root) {
    if (!configured_ || !framebuffer_) return;
    if (root.needs_frame()) {
        const LivePacingResult pace = pacer_.plan(FrameClock::now());
        // A new epoch's frame does not wait for pacing (pairing, design note
        // section 6): the parent painted with it and presents at its next
        // tick. One frame per configuration, so the parent's paint rate
        // bounds it.
        if (pace.outcome == LivePacingOutcome::pending && !epoch_due_) {
            // The dirt stays; a presentation report or the deadline wakes
            // the loop for it.
            pacing_wake_ = pace.wake_at;
            // Only pixels already painted may go out meanwhile (a lease
            // that failed earlier). A frame of a new LiveSurface epoch waits
            // for its paint: published now it would carry the new content
            // tag over the pixels the resize exposed but nobody painted.
            if (unpublished_.empty() || publish_whole_) return;
            publish();
            return;
        }
        pacing_wake_.reset();
        epoch_due_ = false;
        if (pace.outcome == LivePacingOutcome::deadline) {
            const std::lock_guard<std::mutex> lock((*channel_).mutex);
            ++(*channel_).statistics.deadline_wakes;
            (*channel_).statistics.missed_frames += pace.missed_frames;
        }
        const DamageRegion taken = root.take_damage();
        if (!taken.empty() && (*framebuffer_).begin(root.image_resources(), taken.bounds())) {
            const std::optional<PaintReceipt> receipt = root.paint((*framebuffer_).painter(), taken.rectangles());
            (*framebuffer_).end();
            const PixelRectangle painted =
                device_pixels_of(taken.bounds(), scale_, (*framebuffer_).width(), (*framebuffer_).height());
            unpublished_ = unite_pixels(unpublished_, painted);
            // The framebuffer is this root's screen: the paint is presented
            // once it is there, whether or not a LiveSurface buffer is free.
            if (receipt) static_cast<void>(root.notify_presented(*receipt));
        }
    }
    if (unpublished_.empty() && !publish_whole_) return;
    publish();
}

void SubSurfaceLoop::publish_caret(SubSurface& root) {
    const std::optional<Rect> caret = root.text_input_caret();
    if (caret == published_caret_) return;
    published_caret_ = caret;
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    (*channel_).text_input_caret = caret;
}

void SubSurfaceLoop::publish_semantics(SubSurface& root) {
    if (!configured_) return;
    const std::uint64_t generation = root.semantic_generation();
    if (generation == published_semantic_generation_) return;
    // Built here, on the tree's thread, and never changed after: the parent
    // reads it on its own thread through the shared pointer.
    std::shared_ptr<const SemanticSnapshot> snapshot = std::make_shared<const SemanticSnapshot>(root.semantic_snapshot());
    published_semantic_generation_ = (*snapshot).generation;
    WindowDispatcher parent;
    std::weak_ptr<SubSurfaceSemanticsListener> target;
    bool notify = false;
    {
        const std::lock_guard<std::mutex> lock((*channel_).mutex);
        detail::SubSurfaceChannel& channel = *channel_;
        // The snapshot replaced here is released after the lock.
        std::swap(channel.semantics, snapshot);
        ++channel.statistics.semantic_snapshots_published;
        if (!channel.semantics_notice_pending && !channel.semantics_listener.expired()) {
            channel.semantics_notice_pending = true;
            parent = channel.semantics_dispatcher;
            target = channel.semantics_listener;
            notify = true;
        }
    }
    // Posted outside the channel's lock (lock order: the parent's dispatcher
    // is never taken under it). A parent that closed refuses the post; the
    // sub-surface is being stopped then.
    if (notify) static_cast<void>(parent.begin_invoke(NoticeSemantics(std::move(target))));
}

void SubSurfaceLoop::publish() {
    LiveSurfaceWriteLease lease = (*surface_).try_acquire_write(true);
    if (!lease) {
        retry_at_ = FrameClock::now() + publish_retry_interval;
        const std::lock_guard<std::mutex> lock((*channel_).mutex);
        ++(*channel_).statistics.publish_retries;
        return;
    }
    retry_at_.reset();
    const std::uint32_t width = std::min(lease.width(), (*framebuffer_).width());
    const std::uint32_t height = std::min(lease.height(), (*framebuffer_).height());
    PixelRectangle area = unpublished_;
    if (publish_whole_) area = PixelRectangle{0U, 0U, width, height};
    area.right = std::min(area.right, width);
    area.bottom = std::min(area.bottom, height);
    area.left = std::min(area.left, area.right);
    area.top = std::min(area.top, area.bottom);
    copy_pixel_rows((*framebuffer_).pixels(), (*framebuffer_).row_bytes(), lease.pixels(),
                    static_cast<std::size_t>(lease.row_bytes()), area);
    const Rect damage{static_cast<double>(area.left), static_cast<double>(area.top),
                      static_cast<double>(area.right - area.left), static_cast<double>(area.bottom - area.top)};
    static_cast<void>(lease.publish(damage, content_epoch_));
    unpublished_ = PixelRectangle{};
    publish_whole_ = false;
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    ++(*channel_).statistics.frames_published;
    (*channel_).statistics.published_content_epoch = content_epoch_;
}

} // namespace detail

SubSurfaceThread::SubSurfaceThread(std::unique_ptr<SubSurfaceContent> content,
                                   std::shared_ptr<LiveSurface> surface,
                                   std::shared_ptr<const Theme> theme,
                                   std::unique_ptr<PaintFramebuffer> text_measure,
                                   std::shared_ptr<const Theme> layered_user_theme)
    : channel_(std::make_shared<detail::SubSurfaceChannel>()) {
    if (!content || !surface || !theme) {
        throw std::invalid_argument("GUI.Forms sub-surface requires content, a LiveSurface and a theme");
    }
    // Checked here, so the thread's set_user_theme cannot refuse it.
    if (layered_user_theme && (*layered_user_theme).definition().rule_set != ThemeRuleSet::user) {
        throw std::invalid_argument("a sub-surface's layered user theme is compiled as a user theme");
    }
    std::unique_ptr<detail::SubSurfaceLoop> loop = std::make_unique<detail::SubSurfaceLoop>(
        channel_, std::move(content), std::move(surface), std::move(theme), std::move(layered_user_theme),
        std::move(text_measure));
    thread_ = std::thread(&detail::SubSurfaceLoop::run, std::move(loop));
}

SubSurfaceThread::~SubSurfaceThread() {
    stop();
}

void SubSurfaceThread::configure(SubSurfaceConfiguration configuration) {
    if (configuration.content_epoch <= last_content_epoch_) {
        throw std::invalid_argument("GUI.Forms sub-surface content epochs must grow");
    }
    if (!std::isfinite(configuration.scale) || configuration.scale <= 0.0 ||
        !std::isfinite(configuration.logical_size.width) || !std::isfinite(configuration.logical_size.height) ||
        configuration.logical_size.width <= 0.0 || configuration.logical_size.height <= 0.0 ||
        configuration.pixel_width == 0U || configuration.pixel_height == 0U) {
        throw std::invalid_argument("GUI.Forms sub-surface configuration needs a positive size and scale");
    }
    last_content_epoch_ = configuration.content_epoch;
    // A configuration replaced here is destroyed after the lock is released,
    // so its framebuffer never dies under the channel's lock.
    std::optional<SubSurfaceConfiguration> replaced;
    {
        const std::lock_guard<std::mutex> lock((*channel_).mutex);
        detail::SubSurfaceChannel& channel = *channel_;
        if (channel.stop_requested || channel.state == SubSurfaceState::failed) return;
        ++channel.statistics.configurations_received;
        if (channel.pending) {
            ++channel.statistics.configurations_coalesced;
            replaced.swap(channel.pending);
            // A framebuffer comes with a new device size only, so a newer
            // configuration of the same size has none: it takes the
            // replaced one's, which is of that size. Without it a first
            // configuration replaced before the loop applied it would leave
            // the loop with no framebuffer, and nothing would be produced.
            if (!configuration.framebuffer) configuration.framebuffer = std::move((*replaced).framebuffer);
            // An update is complete: the newer one wins, and a configuration
            // without one keeps the replaced one's.
            if ((*replaced).update) {
                if (configuration.update) {
                    ++channel.statistics.updates_coalesced;
                } else {
                    configuration.update = std::move((*replaced).update);
                }
            }
        }
        channel.pending.emplace(std::move(configuration));
        channel.wake.notify_one();
    }
}

std::uint64_t SubSurfaceThread::post_input(const SubSurfaceInput& input) {
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    detail::SubSurfaceChannel& channel = *channel_;
    if (channel.stop_requested || channel.state == SubSurfaceState::failed) return 0U;
    detail::SubSurfaceInputRecord record;
    record.sequence = channel.next_input_sequence;
    record.input = input;
    channel.inputs.push_back(std::move(record));
    ++channel.next_input_sequence;
    ++channel.statistics.inputs_received;
    channel.wake.notify_one();
    const std::uint64_t sequence = channel.inputs.back().sequence;
    return sequence;
}

void SubSurfaceThread::set_key_return(WindowDispatcher parent, std::weak_ptr<SubSurfaceKeyReturn> target) {
    if (!target.expired() && !parent.window_alive()) {
        throw std::invalid_argument("GUI.Forms sub-surface key return needs a live parent dispatcher");
    }
    // The previous dispatcher and target are released after the lock.
    WindowDispatcher previous_parent = std::move(parent);
    std::weak_ptr<SubSurfaceKeyReturn> previous_target = std::move(target);
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    std::swap((*channel_).key_return_dispatcher, previous_parent);
    std::swap((*channel_).key_return, previous_target);
}

void SubSurfaceThread::set_request_target(WindowDispatcher parent,
                                          std::weak_ptr<SubSurfaceRequestTarget> target) {
    if (!target.expired() && !parent.window_alive()) {
        throw std::invalid_argument("GUI.Forms sub-surface request target needs a live parent dispatcher");
    }
    // The previous dispatcher and target are released after the lock.
    WindowDispatcher previous_parent = std::move(parent);
    std::weak_ptr<SubSurfaceRequestTarget> previous_target = std::move(target);
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    std::swap((*channel_).request_dispatcher, previous_parent);
    std::swap((*channel_).request_target, previous_target);
}

void SubSurfaceThread::post_request_outcome(const SurfaceRequestOutcome& outcome) {
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    detail::SubSurfaceChannel& channel = *channel_;
    if (channel.stop_requested || channel.state == SubSurfaceState::failed) return;
    channel.outcomes.push_back(outcome);
    channel.wake.notify_one();
}

void SubSurfaceThread::set_semantics_listener(WindowDispatcher parent,
                                              std::weak_ptr<SubSurfaceSemanticsListener> target) {
    if (!target.expired() && !parent.window_alive()) {
        throw std::invalid_argument("GUI.Forms sub-surface semantics listener needs a live parent dispatcher");
    }
    // The previous dispatcher and target are released after the lock.
    WindowDispatcher previous_parent = std::move(parent);
    std::weak_ptr<SubSurfaceSemanticsListener> previous_target = std::move(target);
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    std::swap((*channel_).semantics_dispatcher, previous_parent);
    std::swap((*channel_).semantics_listener, previous_target);
    // A notice owed to the previous target is owed to this one.
    (*channel_).semantics_notice_pending = false;
}

std::shared_ptr<const SemanticSnapshot> SubSurfaceThread::take_semantics() {
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    (*channel_).semantics_notice_pending = false;
    return (*channel_).semantics;
}

void SubSurfaceThread::stop() noexcept {
    std::optional<SubSurfaceConfiguration> abandoned;
    std::vector<detail::SubSurfaceInputRecord> discarded;
    std::vector<SurfaceRequestOutcome> unanswered;
    {
        const std::lock_guard<std::mutex> lock((*channel_).mutex);
        (*channel_).stop_requested = true;
        abandoned.swap((*channel_).pending);
        discarded.swap((*channel_).inputs);
        unanswered.swap((*channel_).outcomes);
        (*channel_).wake.notify_one();
    }
    if (!thread_.joinable()) return;
    try {
        thread_.join();
    } catch (const std::system_error&) {
        // join refuses only a thread joining itself or a detached one, and
        // neither can happen: this object's thread never holds it.
    }
}

WindowDispatcher SubSurfaceThread::dispatcher() const {
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    return (*channel_).dispatcher;
}

SubSurfaceState SubSurfaceThread::state() const {
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    return (*channel_).state;
}

std::string SubSurfaceThread::failure() const {
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    return (*channel_).failure;
}

SubSurfaceStatistics SubSurfaceThread::statistics() const {
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    return (*channel_).statistics;
}

std::optional<Rect> SubSurfaceThread::text_input_caret() const {
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    return (*channel_).text_input_caret;
}

} // namespace gui_forms
