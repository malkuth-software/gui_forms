// The single-surface window manager's run (surface_manager_internal.hpp):
// its windows as desk layers, input from the host's records, and the scene
// composed into the host's surface.
#include "surface_manager_internal.hpp"
#include "../../../core/failure/fail_fast.hpp"
#include "../../../core/damage/device_damage/device_damage.hpp"
#include "../../../render/skia/raster/skia_splash_bake.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/metrics/font_sources/bundled_font_sources.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <stxlib/optional.hpp>
#include <stxlib/threads.hpp>
#include <utility>

#if defined(GUI_FORMS_PREPARED_TEXT)
#error "The surface window manager composes the Skia raster; the prepared-text raster is not supported here."
#endif

namespace gui_forms::host::surface_detail {
namespace {

// Device rectangles one compose or one window's paint counts at most;
// more are united into their bounds (surface_device_rects).
constexpr std::size_t maximum_compose_rects = 32U;
// The capsule animation's frame interval while it runs (about 60 per
// second).
constexpr std::chrono::milliseconds capsule_frame_interval{16};

std::uint64_t now_ns() {
    const std::chrono::steady_clock::duration since = std::chrono::steady_clock::now().time_since_epoch();
    const std::uint64_t nanoseconds =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(since).count());
    return nanoseconds;
}

Color theme_color(std::uint32_t rgb) noexcept {
    const Color color{static_cast<std::uint8_t>((rgb >> 16U) & 0xFFU), static_cast<std::uint8_t>((rgb >> 8U) & 0xFFU),
                      static_cast<std::uint8_t>(rgb & 0xFFU), 255U};
    return color;
}

// The schedule-wake seam (SurfaceRoot::set_schedule_wake_handler) arrives
// with the macOS modal-wake change; until this branch is rebased onto it the
// handler is installed only where the model has it. Remove the test after
// the rebase.
template <typename Model>
void install_schedule_wake(Model& model, SurfaceInputQueue* queue) {
    if constexpr (requires(Model& candidate) { candidate.set_schedule_wake_handler(std::function<void()>{}); }) {
        if (queue != nullptr) {
            model.set_schedule_wake_handler(Wake{queue});
        } else {
            model.set_schedule_wake_handler(std::function<void()>{});
        }
    } else {
        static_cast<void>(model);
        static_cast<void>(queue);
    }
}

// The device pixels of a logical extent at `scale`, at least 1.
std::uint32_t device_extent(double logical, double scale) noexcept {
    const double device = std::round(logical * scale);
    if (!(device >= 1.0)) return 1U;
    if (device > 32768.0) return 32768U;
    const std::uint32_t pixels = static_cast<std::uint32_t>(device);
    return pixels;
}

// A logical coordinate on the surface's device pixel grid.
std::int32_t device_coordinate(double logical, double scale) noexcept {
    const double device = std::round(logical * scale);
    if (!(device > -32768.0)) return -32768;
    if (device > 32768.0) return 32768;
    const std::int32_t pixel = static_cast<std::int32_t>(device);
    return pixel;
}

SurfaceDeviceRect device_rect(Rect logical, double scale) noexcept {
    const SurfaceDeviceRect rect{device_coordinate(logical.x, scale), device_coordinate(logical.y, scale),
                                 device_coordinate(logical.right(), scale),
                                 device_coordinate(logical.bottom(), scale)};
    return rect;
}

// A raster's pixels as an image layer at device position left, top.
SurfaceComposeLayer image_layer(const render::SkiaRaster& raster, std::int32_t left, std::int32_t top,
                                bool blended) noexcept {
    SurfaceComposeLayer layer{};
    layer.kind = blended ? SurfaceLayerKind::blended_image : SurfaceLayerKind::opaque_image;
    layer.pixels = static_cast<const std::uint8_t*>(raster.pixels());
    layer.row_bytes = raster.row_bytes();
    layer.width = raster.pixel_width();
    layer.height = raster.pixel_height();
    layer.bounds = SurfaceDeviceRect{left, top, left + static_cast<std::int32_t>(layer.width),
                                     top + static_cast<std::int32_t>(layer.height)};
    return layer;
}

SurfaceComposeLayer solid_layer(SurfaceDeviceRect bounds, std::uint32_t rgb) noexcept {
    SurfaceComposeLayer layer{};
    layer.kind = SurfaceLayerKind::solid;
    layer.bounds = bounds;
    layer.color = surface_opaque_pixel(rgb);
    return layer;
}

// The cursor a frame part shows.
CursorKind frame_part_cursor(SurfaceDeskPart part) noexcept {
    switch (part) {
    case SurfaceDeskPart::edge_left:
    case SurfaceDeskPart::edge_right:
        return CursorKind::resize_horizontal;
    case SurfaceDeskPart::edge_top:
    case SurfaceDeskPart::edge_bottom:
        return CursorKind::resize_vertical;
    case SurfaceDeskPart::corner_top_left:
    case SurfaceDeskPart::corner_bottom_right:
        return CursorKind::resize_diagonal_down;
    case SurfaceDeskPart::corner_top_right:
    case SurfaceDeskPart::corner_bottom_left:
        return CursorKind::resize_diagonal_up;
    // Parts that do not resize (none, client, title_bar, the boxes, tabs, ...) show the arrow.
    default:
        return CursorKind::arrow;
    }
}

// The parts acted on at their release (a click), not dragged.
bool frame_part_clicks(SurfaceDeskPart part) noexcept {
    const bool clicks = part == SurfaceDeskPart::close_box || part == SurfaceDeskPart::minimize_box ||
                        part == SurfaceDeskPart::maximize_box || part == SurfaceDeskPart::minimized_tab;
    return clicks;
}

struct Closing final {
    NativeWindow* window;
    void operator()(HostCloseRequest& request) const {
        if ((*window).entry.options.close_request) (*window).entry.options.close_request(request);
    }
};
// The dialog and clipboard functions the program keeps (host_ready): like
// WindowTitle they observe their window weakly and answer after_shutdown
// once it has closed or been reclaimed, and wrong_thread off the thread
// that made them (A-gf-host-02-F12).
struct Dialog final {
    std::weak_ptr<NativeWindow> owner{};
    const stxlib::ThreadIdentity thread{stxlib::current_thread()};
    HostDialogResult operator()(const HostDialogRequest& request) const {
        HostDialogResult refused{};
        refused.request_id = request.request_id;
        if (thread != stxlib::current_thread()) {
            refused.status.error = HostServiceError::wrong_thread;
            return refused;
        }
        const std::shared_ptr<NativeWindow> window = owner.lock();
        if (!window || (*window).closed) {
            refused.status.error = HostServiceError::after_shutdown;
            return refused;
        }
        const HostDialogResult result = (*(*window).services).show_dialog(request);
        return result;
    }
};
struct ReadText final {
    std::weak_ptr<NativeWindow> owner{};
    const stxlib::ThreadIdentity thread{stxlib::current_thread()};
    HostClipboardTextResult operator()() const {
        HostClipboardTextResult refused{};
        if (thread != stxlib::current_thread()) {
            refused.status.error = HostServiceError::wrong_thread;
            return refused;
        }
        const std::shared_ptr<NativeWindow> window = owner.lock();
        if (!window || (*window).closed) {
            refused.status.error = HostServiceError::after_shutdown;
            return refused;
        }
        const HostClipboardTextResult result = (*(*window).services).read_clipboard_text();
        return result;
    }
};
struct WriteText final {
    std::weak_ptr<NativeWindow> owner{};
    const stxlib::ThreadIdentity thread{stxlib::current_thread()};
    HostServiceStatus operator()(std::string_view text) const {
        if (thread != stxlib::current_thread()) return {HostServiceError::wrong_thread};
        const std::shared_ptr<NativeWindow> window = owner.lock();
        if (!window || (*window).closed) return {HostServiceError::after_shutdown};
        const HostServiceStatus status = (*(*window).services).write_clipboard_text(text);
        return status;
    }
};

bool is_primary(const SurfaceApplicationWindow& entry) noexcept {
    const bool primary = entry.owner_id.empty() && !entry.tool_window && entry.popup.absent();
    return primary;
}

// The primary window's title is the host's; another window's is its
// frame's.
struct WindowTitle final {
    std::weak_ptr<NativeWindow> owner{};
    const stxlib::ThreadIdentity thread{stxlib::current_thread()};
    HostServiceStatus operator()(const std::string_view text) const {
        if (thread != stxlib::current_thread()) return {HostServiceError::wrong_thread};
        const std::shared_ptr<NativeWindow> window = owner.lock();
        if (!window || (*window).closed) return {HostServiceError::after_shutdown};
        try {
            if (text.size() > 65536U || text.find('\0') != std::string_view::npos || !validate_utf8(text).valid())
                return {HostServiceError::invalid_argument};
            const std::string title(text);
            NativeWindow& native = *window;
            native.entry.options.title = title;
            if (native.chrome) (*native.chrome).set_title(title);
            if (is_primary(native.entry)) native.runtime.host.set_title(title);
            return {};
        } catch (...) {
            return {HostServiceError::backend_failure};
        }
    }
};

// A framed window's client size is changed in the surface; the primary
// window's is the surface's, decided by the host.
struct WindowClientSize final {
    std::weak_ptr<NativeWindow> owner{};
    const stxlib::ThreadIdentity thread{stxlib::current_thread()};
    HostServiceStatus operator()(const Size client_size) const {
        if (thread != stxlib::current_thread()) return {HostServiceError::wrong_thread};
        const std::shared_ptr<NativeWindow> window = owner.lock();
        if (!window || (*window).closed) return {HostServiceError::after_shutdown};
        NativeWindow& native = *window;
        const Size minimum = native.minimum_size();
        if (!std::isfinite(client_size.width) || !std::isfinite(client_size.height) || client_size.width > 32768.0 ||
            client_size.height > 32768.0 || client_size.width < std::max(1.0, minimum.width) ||
            client_size.height < std::max(1.0, minimum.height))
            return {HostServiceError::invalid_argument};
        if (!native.runtime.desk.framed(native.window_id)) return {HostServiceError::unsupported};
        const Rect wanted{native.content.x, native.content.y, client_size.width, client_size.height};
        const std::optional<Rect> placed = native.runtime.desk.set_content(native.window_id, wanted);
        if (!placed.has_value()) return {HostServiceError::backend_failure};
        native.apply_content(*placed);
        native.refresh_chrome();
        return {};
    }
};

// A show, hide, close or full-screen request from any thread, queued for
// the run's next turn.
struct WindowAction final {
    std::weak_ptr<NativeWindow> window{};
    std::weak_ptr<ActionQueue> queue{};
    ActionKind kind{ActionKind::close};
    void operator()() const {
        const std::shared_ptr<ActionQueue> actions = queue.lock();
        if (!actions) return;
        (*actions).post(Action{window, kind});
    }
};

} // namespace

SurfaceHost::~SurfaceHost() = default;

presentation::PresentTrace* SurfaceHost::presentation_trace() noexcept { return nullptr; }

presentation::PresentPolicy SurfaceHost::presentation_policy() const {
    const presentation::PresentPolicy policy = presentation::present_policy_from_environment();
    return policy;
}

presentation::DirectAcquire SurfaceHostDirectPath::acquire(presentation::PresentExtent extent) {
    if (view_.pixels == nullptr || view_.width != extent.width || view_.height != extent.height) {
        view_ = host_.acquire_surface(extent.width, extent.height);
        const bool usable = view_.pixels != nullptr && view_.width == extent.width &&
                            view_.height == extent.height &&
                            view_.row_bytes >= static_cast<std::size_t>(extent.width) * 4U;
        if (!usable) {
            view_ = SurfaceView{};
            return presentation::DirectAcquire::refused(presentation::DirectRefusal::resources);
        }
        // A new surface holds nothing yet.
        age_ = 0U;
    }
    presentation::DirectBuffer buffer{};
    buffer.pixels = reinterpret_cast<std::byte*>(view_.pixels);
    buffer.row_bytes = view_.row_bytes;
    buffer.extent = extent;
    buffer.age = age_;
    return presentation::DirectAcquire::granted(buffer);
}

presentation::PresentOutcome SurfaceHostDirectPath::present(std::span<const presentation::PresentRect> damage) {
    rects_.clear();
    for (std::size_t index = 0U; index < damage.size(); ++index) {
        const presentation::PresentRect rect = damage[index];
        rects_.push_back(SurfaceDeviceRect{rect.left, rect.top, rect.right, rect.bottom});
    }
    // The surface keeps what the manager wrote whatever the host did with
    // it: it holds the frame composed last.
    age_ = 1U;
    const SurfacePresentResult result = host_.present(rects_);
    switch (result) {
    case SurfacePresentResult::presented:
        return presentation::PresentOutcome::presented;
    case SurfacePresentResult::pending:
        return presentation::PresentOutcome::pending;
    case SurfacePresentResult::inactive:
        return presentation::PresentOutcome::inactive;
    case SurfacePresentResult::failed:
    default:
        break;
    }
    return presentation::PresentOutcome::failed;
}

void SurfaceHostDirectPath::release() noexcept {
    view_ = SurfaceView{};
    age_ = 0U;
}

void Wake::operator()() const noexcept {
    if (queue != nullptr) (*queue).wake();
}

void ActionQueue::post(Action action) {
    {
        const stxlib::ScopedLock guard(mutex);
        pending.push_back(std::move(action));
    }
    (*input).wake();
}

void register_fonts(render::SkiaRaster& raster, const std::shared_ptr<gui_forms::detail::BundledFontSet>& fonts) {
    if (!fonts || !raster.register_bundled_fonts(fonts))
        throw std::runtime_error("Cannot load the bundled GUI.Forms fonts for the surface window manager");
}

// ---------------------------------------------------------------------------
// FrameChrome

FrameChrome::FrameChrome(Runtime& runtime, std::string_view title, double width, double scale)
    : runtime_(runtime),
      size_{std::max(1.0, width), runtime.desk.theme().title_bar_height - runtime.desk.theme().border_width},
      scale_(scale) {
    const SurfaceFrameTheme& theme = runtime_.desk.theme();
    const Color text = theme_color(theme.title_text);
    root_ = make_control<Panel>(StableId("host.frame.root"));
    (*root_).set_background(theme_color(theme.title_inactive));
    title_ = make_control<Label>(StableId("host.frame.title"), std::string(title));
    (*title_).font = stxlib::Optional<FontSpec>::of(FontSpec{FontRole::control, 13, 400, false});
    (*title_).fore_color = stxlib::Optional<Color>::of(text);
    (*title_).vertical_alignment = VerticalAlignment::center;
    (*root_).add_child(title_);
    // U+2013, U+25A1 and U+00D7: minimise, maximise and close.
    minimize_ = make_control<Label>(StableId("host.frame.minimize"), std::string("\xE2\x80\x93"));
    maximize_ = make_control<Label>(StableId("host.frame.maximize"), std::string("\xE2\x96\xA1"));
    close_ = make_control<Label>(StableId("host.frame.close"), std::string("\xC3\x97"));
    const std::array<Label*, 3> boxes{minimize_.get(), maximize_.get(), close_.get()};
    for (std::size_t index = 0U; index < boxes.size(); ++index) {
        Label& box = *boxes[index];
        box.font = stxlib::Optional<FontSpec>::of(FontSpec{FontRole::control, 15, 400, false});
        box.fore_color = stxlib::Optional<Color>::of(text);
        box.alignment = HorizontalAlignment::center;
        box.vertical_alignment = VerticalAlignment::center;
    }
    (*root_).add_child(minimize_);
    (*root_).add_child(maximize_);
    (*root_).add_child(close_);
    capsule_ = make_control<WindowCapsule>(StableId("host.frame.capsule"));
    (*capsule_).set_interactive(false);
    (*root_).add_child(capsule_);
    title_text_ = std::string(title);
    register_fonts(raster_, runtime_.fonts);
    model_ = std::make_unique<Window>(root_, size_);
    (*model_).set_text_metrics_provider(&raster_);
    (*model_).set_framebuffer_painter(&raster_);
    session_ = std::make_unique<HostSession>(*model_, runtime_.capabilities(), nullptr);
    if (!dispatch(HostAttachEvent{size_, scale_}).accepted()) throw std::runtime_error("Frame strip attach rejected");
    resize(width, scale);
}

FrameChrome::~FrameChrome() {
    if (model_) {
        (*model_).set_text_metrics_provider(nullptr);
        (*model_).set_framebuffer_painter(nullptr);
    }
    if (session_) (*session_).shutdown();
}

HostDispatchResult FrameChrome::dispatch(HostEventPayload payload) {
    ++sequence_;
    const HostDispatchResult result = (*session_).dispatch({sequence_, now_ns(), std::move(payload)});
    return result;
}

void FrameChrome::set_title(std::string_view title) {
    title_text_ = std::string(title);
    (*title_).text = title_shown_ ? title_text_ : std::string();
}

void FrameChrome::set_active(bool active) {
    if (active == active_) return;
    active_ = active;
    const SurfaceFrameTheme& theme = runtime_.desk.theme();
    (*root_).set_background(theme_color(active ? theme.title_active : theme.title_inactive));
}

void FrameChrome::set_boxes(bool minimize, bool maximize, bool maximized) {
    if (minimize == shows_minimize_ && maximize == shows_maximize_ && maximized == maximized_) return;
    shows_minimize_ = minimize;
    shows_maximize_ = maximize;
    maximized_ = maximized;
    // U+2750 restores a maximised window; U+25A1 maximises.
    (*maximize_).text = maximized ? std::string("\xE2\x9D\x90") : std::string("\xE2\x96\xA1");
    layout();
}

// The boxes in the desk's order (SurfaceDesk::box): close outermost at the
// theme's controls end, then maximise, then minimise; the title fills the
// rest. With the capsule, the capsule where the desk places it
// (SurfaceDesk::capsule, in strip units: the strip starts at the client
// area's left and below the frame's top edge) and the title beside it.
void FrameChrome::layout() {
    const SurfaceFrameTheme& theme = runtime_.desk.theme();
    if (capsule_shown_) {
        (*minimize_).set_bounds({0.0, 0.0, 0.0, 0.0});
        (*maximize_).set_bounds({0.0, 0.0, 0.0, 0.0});
        (*close_).set_bounds({0.0, 0.0, 0.0, 0.0});
        const double height = std::min(theme.capsule_height, size_.height);
        const double width = std::min(theme.capsule_width, size_.width);
        const double left = theme.controls == SurfaceControlsSide::left
                                ? theme.capsule_margin
                                : size_.width - theme.capsule_margin - width;
        (*capsule_).set_bounds({left, (size_.height - height) * 0.5, width, height});
        const double used = theme.capsule_margin + width;
        const double title_left = theme.controls == SurfaceControlsSide::left ? used + 10.0 : 10.0;
        (*title_).set_bounds({title_left, 0.0, std::max(0.0, size_.width - used - 20.0), size_.height});
        return;
    }
    (*capsule_).set_bounds({0.0, 0.0, 0.0, 0.0});
    const double box_width = std::min(theme.title_box_width, size_.width);
    const std::array<Label*, 3> order{close_.get(), maximize_.get(), minimize_.get()};
    const std::array<bool, 3> shown{true, shows_maximize_, shows_minimize_};
    double used = 0.0;
    for (std::size_t index = 0U; index < order.size(); ++index) {
        if (!shown[index]) {
            (*order[index]).set_bounds({0.0, 0.0, 0.0, 0.0});
            continue;
        }
        const double left =
            theme.controls == SurfaceControlsSide::left ? used : size_.width - used - box_width;
        (*order[index]).set_bounds({left, 0.0, box_width, size_.height});
        used += box_width;
    }
    const double title_left = theme.controls == SurfaceControlsSide::left ? used + 10.0 : 10.0;
    (*title_).set_bounds({title_left, 0.0, std::max(0.0, size_.width - used - 20.0), size_.height});
}

void FrameChrome::resize(double width, double scale) {
    const SurfaceFrameTheme& theme = runtime_.desk.theme();
    const Size next{std::max(1.0, width), std::max(1.0, theme.title_bar_height - theme.border_width)};
    if (scale != scale_) {
        scale_ = scale;
        static_cast<void>(dispatch(HostScaleEvent{scale_}));
    }
    if (next != size_) {
        size_ = next;
        static_cast<void>(dispatch(HostResizeEvent{size_}));
    }
    layout();
    damage_.add({0.0, 0.0, size_.width, size_.height});
}

bool FrameChrome::paint(std::vector<Rect>& painted, bool& resized) {
    resized = false;
    static_cast<void>((*model_).drain_posted_work());
    const DamageRegion fresh = (*model_).take_damage();
    for (std::size_t index = 0U; index < fresh.rectangles().size(); ++index) damage_.add(fresh.rectangles()[index]);
    if (damage_.empty()) return false;
    const void* before = raster_.pixels();
    const std::uint32_t before_width = raster_.pixel_width();
    const std::uint32_t before_height = raster_.pixel_height();
    raster_.resize(size_, scale_);
    raster_resized_ = raster_resized_ || before != raster_.pixels() || before_width != raster_.pixel_width() ||
                      before_height != raster_.pixel_height();
    static_cast<void>(raster_.synchronize_images((*model_).image_resources()));
    DamageRegion screen = damage_;
    raster_.begin_frame(screen);
    const stxlib::Optional<PaintReceipt> receipt = (*model_).paint(raster_, damage_.rectangles());
    raster_.end_frame();
    if (receipt.absent()) return false;
    const std::span<const Rect> changed = damage_.rectangles();
    PresentedArea area;
    for (std::size_t index = 0U; index < changed.size(); ++index) painted.push_back(changed[index]);
    area.rectangles = changed.size();
    static_cast<void>((*model_).notify_presented(*receipt.value(), 0U, area));
    damage_.clear();
    resized = raster_resized_;
    raster_resized_ = false;
    return true;
}

// ---------------------------------------------------------------------------
// NativeWindow

NativeWindow::NativeWindow(Runtime& host, SurfaceApplicationWindow source, HostWindowId id, Rect area, bool kept)
    : runtime(host), entry(std::move(source)), window_id(id), initial(kept), content(area), scale(host.scale) {
    register_fonts(raster, runtime.fonts);
    (*entry.model).set_text_metrics_provider(&raster);
    (*entry.model).set_framebuffer_painter(&raster);
    (*entry.model).metrics().set_renderer("Skia CPU / STXlib text / surface window manager", true);
    services = std::make_unique<Services>(*this);
    // The availability record (stage DP): the manager composes each damage
    // rectangle.
    PresentationDeclaration presentation;
    presentation.contract_version = paint_contract_version;
    presentation.extensions.partial_present = true;
    (*entry.model).declare_presentation(presentation);
    session = std::make_unique<HostSession>(*entry.model, runtime.capabilities(), services.get());
    closing_token = (*session).closing().subscribe(Closing{this});
    if (runtime.desk.framed(window_id)) {
        chrome = std::make_unique<FrameChrome>(runtime, entry.options.title, content.width, scale);
        refresh_chrome();
    }
}

NativeWindow::~NativeWindow() {
    (*entry.model).set_dispatch_wake_handler({});
    (*entry.model).set_live_surface_idle_wake_handler({});
    (*entry.model).set_paint_wake_handler({});
    install_schedule_wake(*entry.model, nullptr);
    (*entry.model).set_text_metrics_provider(nullptr);
    (*entry.model).set_framebuffer_painter(nullptr);
    if (session) (*session).shutdown();
}

HostDispatchResult NativeWindow::dispatch(HostEventPayload payload) {
    ++sequence;
    const HostDispatchResult result = (*session).dispatch({sequence, now_ns(), std::move(payload)});
    return result;
}

void NativeWindow::attach() {
    SurfaceInputQueue* queue = &runtime.queue;
    (*entry.model).set_dispatch_wake_handler(Wake{queue});
    (*entry.model).set_paint_wake_handler(Wake{queue});
    (*entry.model).set_live_surface_idle_wake_handler(Wake{queue});
    install_schedule_wake(*entry.model, queue);
    // The raster keeps its pixels between frames, so a scroll's pixels move
    // instead of repainting.
    (*entry.model).set_scroll_copies_supported(true);
    if (!dispatch(HostAttachEvent{Size{content.width, content.height}, scale}).accepted())
        throw std::runtime_error("Surface window attach rejected");
    static_cast<void>(dispatch(HostCompositorEvent{surface_compositor_state()}));
    report_safe_insets();
    if (entry.options.initially_visible) set_visible(true);
}

void NativeWindow::ready() {
    const WindowAction show{weak_from_this(), runtime.actions, ActionKind::show};
    const WindowAction hide{weak_from_this(), runtime.actions, ActionKind::hide};
    const WindowAction close_window{weak_from_this(), runtime.actions, ActionKind::close};
    if (entry.options.host_ready)
        entry.options.host_ready(Wake{&runtime.queue}, close_window, Dialog{weak_from_this()},
                                 ReadText{weak_from_this()}, WriteText{weak_from_this()});
    if (entry.options.full_screen_ready)
        entry.options.full_screen_ready(WindowAction{weak_from_this(), runtime.actions, ActionKind::fullscreen});
    if (entry.options.title_ready) entry.options.title_ready(WindowTitle{weak_from_this()});
    if (entry.options.client_size_ready) entry.options.client_size_ready(WindowClientSize{weak_from_this()});
    if (entry.options.visibility_ready) entry.options.visibility_ready(show, hide);
    if (is_primary(entry)) runtime.host.set_title(entry.options.title);
}

void NativeWindow::action(ActionKind kind) {
    if (closed) return;
    switch (kind) {
    case ActionKind::close:
        close(HostCloseReason::application);
        break;
    case ActionKind::show:
        set_visible(true);
        break;
    case ActionKind::hide:
        set_visible(false);
        break;
    case ActionKind::fullscreen:
        // Only the primary window fills the surface the host can enlarge.
        if (is_primary(entry)) runtime.host.toggle_full_screen();
        break;
    default:
        break;
    }
}

Size NativeWindow::minimum_size() const noexcept {
    const Size minimum = entry.options.minimum_size;
    return minimum;
}

void NativeWindow::close(HostCloseReason reason, bool force) {
    if (closed) return;
    if (force && reason == HostCloseReason::session_end)
        static_cast<void>(dispatch(HostCloseRequest{reason, false, false}));
    if (!force) {
        const HostDispatchResult result = dispatch(HostCloseRequest{reason, false, entry.options.hide_on_close});
        if (!result.accepted() || !result.close_allowed) return;
        if (entry.options.hide_on_close) {
            set_visible(false);
            if (runtime.windowing && (*runtime.windowing).live(window_id))
                (*runtime.windowing).report_hidden_on_close(window_id);
            return;
        }
    }
    closed = true;
    const bool was_active = runtime.desk.active() == window_id;
    if (visible) runtime.desk.set_visible(window_id, false);
    visible = false;
    static_cast<void>(dispatch(HostClosedEvent{reason}));
    (*entry.model).set_dispatch_wake_handler({});
    (*entry.model).set_live_surface_idle_wake_handler({});
    (*entry.model).set_paint_wake_handler({});
    install_schedule_wake(*entry.model, nullptr);
    (*session).shutdown();
    runtime.desk.remove(window_id);
    runtime.scene_changed();
    if (runtime.pressed_window == window_id) runtime.pressed_window = {};
    if (runtime.pointer_window == window_id) runtime.pointer_window = {};
    if (runtime.pressed_part.window == window_id) runtime.pressed_part = {};
    runtime.forget_capsule(window_id);
    if (entry.options.print_metrics_on_close)
        std::cout << "{\"window\":" << (*entry.model).metrics_snapshot().to_json()
                  << ",\"host\":" << (*session).snapshot().to_json() << "}\n";
    // Retired before the closed callback, so the callback may open a
    // replacement under the same stable id. A host dialog has no identity
    // in the windowing table.
    if (runtime.windowing && (*runtime.windowing).live(window_id)) (*runtime.windowing).retire(window_id);
    if (was_active) runtime.activate_topmost();
    if (entry.options.closed) entry.options.closed();
}

void NativeWindow::apply_content(Rect next) {
    if (closed) return;
    const Size before{content.width, content.height};
    content = next;
    const Size after{content.width, content.height};
    if (after != before) {
        static_cast<void>(dispatch(HostResizeEvent{after}));
        damage.add({0.0, 0.0, after.width, after.height});
        if (chrome) (*chrome).resize(after.width, scale);
    }
    report_safe_insets();
    runtime.scene_changed();
    if (runtime.windowing) (*runtime.windowing).report_native_frame(*this);
}

void NativeWindow::report_safe_insets() {
    if (closed) return;
    const SafeInsets& surface = runtime.safe_insets;
    const Size extent = runtime.extent;
    // How far each hidden band reaches into the client area.
    const double left = std::clamp(surface.left - content.x, 0.0, content.width);
    const double top = std::clamp(surface.top - content.y, 0.0, content.height);
    const double right = std::clamp(content.x + content.width - (extent.width - surface.right), 0.0, content.width);
    const double bottom =
        std::clamp(content.y + content.height - (extent.height - surface.bottom), 0.0, content.height);
    const SafeInsets insets{left, top, right, bottom};
    if (safe_insets_reported && reported_safe_insets == insets) return;
    if (!dispatch(HostSafeInsetsEvent{insets}).accepted()) return;
    safe_insets_reported = true;
    reported_safe_insets = insets;
}

void NativeWindow::apply_scale(double next) {
    if (closed || !std::isfinite(next) || next <= 0.0 || next == scale) return;
    scale = next;
    static_cast<void>(dispatch(HostScaleEvent{scale}));
    if (closed) return;
    if (chrome) (*chrome).resize(content.width, scale);
    // The whole window re-rasters at the new scale on the next frame.
    damage.add({0.0, 0.0, content.width, content.height});
    runtime.scene_changed();
}

void NativeWindow::set_visible(bool shown) {
    if (closed || shown == visible) return;
    visible = shown;
    runtime.desk.set_visible(window_id, shown);
    static_cast<void>(dispatch(HostOcclusionEvent{!shown || runtime.host_hidden}));
    if (closed) return;
    runtime.scene_changed();
    if (shown) {
        damage.add({0.0, 0.0, content.width, content.height});
        const bool takes_focus = entry.popup.absent() || ((*entry.popup.value())).focus == HostPopupFocus::takes_focus;
        if (takes_focus) runtime.activate(window_id, true);
    } else if (runtime.desk.active() == window_id || active) {
        runtime.activate_topmost();
    }
    if (runtime.windowing) (*runtime.windowing).report_native_frame(*this);
}

void NativeWindow::set_active(bool next) {
    if (closed) return;
    const bool keyed = next && runtime.host_focused;
    if (chrome) (*chrome).set_active(next);
    if (keyed == active) return;
    active = keyed;
    if (!active && composing) {
        composing = false;
        TextInputEvent cancel;
        cancel.composing = true;
        static_cast<void>(dispatch(std::move(cancel)));
    }
    static_cast<void>(dispatch(HostActivationEvent{active}));
}

void NativeWindow::refresh_chrome() {
    if (!chrome) return;
    const SurfaceWindowState state = runtime.desk.state(window_id);
    const bool minimize = runtime.desk.box(window_id, SurfaceDeskPart::minimize_box).width > 0.0;
    const bool maximize = runtime.desk.box(window_id, SurfaceDeskPart::maximize_box).width > 0.0;
    (*chrome).set_boxes(minimize, maximize, state == SurfaceWindowState::maximized);
    const bool capsule = runtime.desk.capsule(window_id).width > 0.0;
    const bool title = runtime.capsule.window() == window_id && runtime.capsule.title_shown();
    (*chrome).set_capsule(capsule, window_capsule_look(runtime.capsule, window_id), title);
}

void NativeWindow::update_cursor(Point position) {
    const Control::Ptr captured = (*entry.model).captured_control();
    const Control::Ptr target = captured ? captured : (*entry.model).hit_test(position);
    static_cast<void>((*services).set_custom_cursor(target ? (*target).effective_cursor_images() : CursorImagesPtr{},
                                                    scale, target ? (*target).effective_cursor() : CursorKind::arrow));
}

void NativeWindow::pointer(PointerEvent event) {
    if (closed) return;
    const Point position = event.position;
    const PointerAction action = event.action;
    const PointerButton button = event.button;
    static_cast<void>(dispatch(std::move(event)));
    if (closed) return;
    if (action == PointerAction::down && button == PointerButton::secondary)
        static_cast<void>(dispatch(HostContextRequestEvent{ContextRequestOrigin::pointer, position}));
    if (closed) return;
    update_cursor(position);
}

HostWindowFrame NativeWindow::frame() const {
    HostWindowFrame result;
    result.content = content;
    const std::optional<Rect> outer = runtime.desk.outer(window_id);
    result.outer = outer.has_value() ? *outer : content;
    result.outer_measured = runtime.desk.framed(window_id);
    return result;
}

bool NativeWindow::has_compose_work() const noexcept {
    const bool work = !composed.empty() || !chrome_composed.empty() || raster_resized || chrome_resized;
    return work;
}

bool NativeWindow::wants_frames() const noexcept {
    // Live surfaces that are not resting, or display-paced motion and
    // animation timers (Window::wants_display_frames): either wants the
    // host's display frames, which also time the window (handle's frame
    // record).
    const bool live = (*entry.model).has_live_surface_presentations() && !live_idle;
    const bool wanted = !closed && (live || (*entry.model).wants_display_frames());
    return wanted;
}

void NativeWindow::update() {
    if (closed) return;
    if (entry.options.dispatch_pending) entry.options.dispatch_pending();
    if (closed) return;
    static_cast<void>((*entry.model).drain_posted_work());
    static_cast<void>((*entry.model).poll_frame_schedule(std::chrono::steady_clock::now()));
    if (closed) return;
    if (chrome && visible) {
        bool resized = false;
        if ((*chrome).paint(chrome_composed, resized)) chrome_resized = chrome_resized || resized;
    }
    // Gather before taking damage: a new frame beneath an overlay damages the
    // overlay's share, and that damage is taken with the rest.
    const bool regions_taken = gather_live_frames();
    const DamageRegion fresh = (*entry.model).take_damage();
    live_idle = (*entry.model).has_live_surface_presentations() && live_updates.empty() && fresh.empty();
    if (live_idle) (*entry.model).set_live_surface_idle_waiting(true);
    taken_copies.clear();
    (*entry.model).take_scroll_copies(taken_copies);
    for (std::size_t index = 0U; index < taken_copies.size(); ++index) {
        const ScrollCopy& copy = taken_copies[index];
        if (!damage.empty()) damage = damage_after_scroll_copy(damage, copy.clip, copy.offset);
        pending_copies.push_back(copy);
    }
    taken_copies.clear();
    for (std::size_t index = 0U; index < fresh.rectangles().size(); ++index) damage.add(fresh.rectangles()[index]);
    if (regions_taken) live_region_lost = true;
    if (!pending_copies.empty()) {
        for (std::size_t index = 0U; index < live_regions.size(); ++index) {
            if (!live_regions[index].empty()) live_regions[index] = live_updates[index].clip;
        }
    }
    DamageRegion screen = damage;
    for (std::size_t index = 0U; index < live_regions.size(); ++index) {
        if (!live_regions[index].empty()) screen.add(live_regions[index]);
    }
    // The screen without the copies' clips, for the reference host scroll.
    screen_painted = screen;
    for (std::size_t index = 0U; index < pending_copies.size(); ++index) screen.add(pending_copies[index].clip);
    // A hidden or minimised window keeps its damage until it is shown.
    if (!visible || !runtime.desk.shown(window_id) || screen.empty()) {
        live_frames.clear();
        return;
    }
    paint(screen, regions_taken);
}

void NativeWindow::paint(DamageRegion& screen, bool regions_taken) {
    const Size logical{content.width, content.height};
    const void* before = raster.pixels();
    const std::uint32_t before_width = raster.pixel_width();
    const std::uint32_t before_height = raster.pixel_height();
    raster.resize(logical, scale);
    raster_resized = raster_resized || before != raster.pixels() || before_width != raster.pixel_width() ||
                     before_height != raster.pixel_height();
    static_cast<void>(raster.synchronize_images((*entry.model).image_resources()));
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    raster.begin_frame(screen);
    applied_copy_clips.clear();
    applied_copies.clear();
    bool copies_applied = true;
    for (std::size_t index = 0U; index < pending_copies.size(); ++index) {
        const ScrollCopy& copy = pending_copies[index];
        if (copies_applied) copies_applied = raster.scroll_copy(copy.clip, copy.offset);
        applied_copy_clips.push_back(copy.clip);
        applied_copies.push_back(copy);
    }
    pending_copies.clear();
    if (!copies_applied) {
        for (std::size_t index = 0U; index < applied_copy_clips.size(); ++index) damage.add(applied_copy_clips[index]);
    }
    compose_live_frames();
    stxlib::Optional<PaintReceipt> receipt{};
    if (!damage.empty()) {
        receipt = (*entry.model).paint(raster, damage.rectangles());
        const std::span<const Rect> painted = damage.rectangles();
        for (std::size_t index = 0U; index < painted.size(); ++index) {
            screen.add(painted[index]);
            screen_painted.add(painted[index]);
        }
    }
    raster.end_frame();
    live_frames.clear();
    if (!damage.empty() && receipt.absent()) {
        for (std::size_t index = 0U; index < applied_copy_clips.size(); ++index) damage.add(applied_copy_clips[index]);
        return;
    }
    // The rectangles the next compose copies: the screen's, on whole device
    // pixels for the count.
    std::array<SurfaceDeviceRect, maximum_compose_rects> device{};
    const std::size_t count =
        surface_device_rects(screen.rectangles(), scale, raster.pixel_width(), raster.pixel_height(), device);
    PresentedArea copied;
    for (std::size_t index = 0U; index < count; ++index) {
        const std::uint64_t columns = static_cast<std::uint64_t>(device[index].right - device[index].left);
        const std::uint64_t rows = static_cast<std::uint64_t>(device[index].bottom - device[index].top);
        copied.device_pixels += columns * rows;
    }
    copied.rectangles = count;
    // The copies this paint applied may be composed by moving the surface's
    // pixels only when nothing painted before them waits to be composed
    // (those rectangles are where the pixels were before the copies) and no
    // earlier copies wait either.
    const bool copied_now = copies_applied && !applied_copies.empty();
    if (copied_now) {
        copies_composable = composed.empty() && composed_copies.empty() && !raster_resized;
        composed_copies = applied_copies;
    } else if (!applied_copies.empty()) {
        copies_composable = false;
    }
    const std::span<const Rect> changed = screen.rectangles();
    for (std::size_t index = 0U; index < changed.size(); ++index) composed.push_back(changed[index]);
    const std::span<const Rect> painted_only = screen_painted.rectangles();
    for (std::size_t index = 0U; index < painted_only.size(); ++index) composed_painted.push_back(painted_only[index]);
    const std::uint64_t elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
    if (regions_taken) live_region_lost = false;
    // Reported when the raster holds the frame, as the X11 host reports
    // after XPutImage: the manager composes it into the surface from here.
    const bool acknowledged = report_presented(receipt, elapsed, copied);
    if (receipt.absent()) return;
    const PaintLeaseSnapshot lease = (*entry.model).paint_lease_snapshot();
    const bool shown = acknowledged || (lease.surface_epoch == (*receipt.value()).surface_epoch &&
                                        lease.presented_revision == (*receipt.value()).rendered_revision);
    if (shown) damage.clear();
}

bool NativeWindow::gather_live_frames() {
    live_updates.clear();
    live_frames.clear();
    live_frame_sources.clear();
    live_regions.clear();
    live_frame_pixels.clear();
    live_frame_rectangles.clear();
    if (!(*entry.model).has_live_surface_presentations()) return false;
    live_updates = (*entry.model).take_live_surface_presentations();
    const bool regions_usable = !live_region_lost;
    bool regions_taken = false;
    std::vector<Rect> changed_regions;
    changed_regions.reserve(live_updates.size());
    for (std::size_t index = 0U; index < live_updates.size(); ++index) {
        const LiveSurfacePresentation& presentation = live_updates[index];
        std::size_t source = live_frames.size();
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (live_updates[earlier].surface == presentation.surface) {
                source = live_frame_sources[earlier];
                break;
            }
        }
        if (source == live_frames.size()) {
            LiveSurfaceFrame taken{};
            if (presentation.surface) taken = (*presentation.surface).acquire_for_presentation(this);
            if (taken) regions_taken = true;
            Rect changed = presentation.clip;
            if (!taken) {
                changed = {};
            } else if (presentation.damage_limited && regions_usable) {
                changed = gui_forms::detail::live_damage_in_window(taken.damage(), presentation.destination,
                                                                   taken.width(), taken.height(), scale);
            }
            live_frames.push_back(std::move(taken));
            changed_regions.push_back(changed);
            live_frame_pixels.push_back(0U);
            live_frame_rectangles.push_back(0U);
        }
        live_frame_sources.push_back(source);
        live_regions.push_back(Rect{});
        const Rect changed = changed_regions[source];
        const Rect region = presentation.damage_limited && regions_usable
                                ? Rect::intersection(presentation.clip, changed)
                                : (changed.empty() ? Rect{} : presentation.clip);
        if (region.empty()) continue;
        if (live_frames[source].opaque()) {
            live_regions[index] = region;
        } else {
            damage.add(region);
        }
        const Rect aligned = gui_forms::detail::align_damage_outward(region, scale);
        const std::uint64_t columns = static_cast<std::uint64_t>(std::lround(aligned.width * scale));
        const std::uint64_t rows = static_cast<std::uint64_t>(std::lround(aligned.height * scale));
        live_frame_pixels[source] += columns * rows;
        ++live_frame_rectangles[source];
    }
    return regions_taken;
}

void NativeWindow::compose_live_frames() {
    live_reported.clear();
    for (std::size_t index = 0U; index < live_regions.size(); ++index) {
        const Rect region = live_regions[index];
        if (region.empty()) continue;
        raster.save();
        raster.clip_rect(region);
        const bool drawn = raster.draw_live_surface_frame(live_frames[live_frame_sources[index]],
                                                          live_updates[index].destination, scale);
        raster.restore();
        if (drawn) {
            (*entry.model).note_live_surface_presented(live_updates[index], region);
            report_live_shown(live_frame_sources[index]);
            continue;
        }
        live_regions[index] = {};
        damage.add(region);
    }
}

// Reports a live frame the compose showed to its surface, once per source
// per tick, with the window's display timing (Window::
// report_live_surface_shown): the surface's producer paces to the window's
// frame source.
void NativeWindow::report_live_shown(std::size_t source) {
    if (source >= live_frames.size() || !live_frames[source]) return;
    if (live_reported.size() < live_frames.size()) live_reported.resize(live_frames.size(), false);
    if (live_reported[source]) return;
    live_reported[source] = true;
    for (std::size_t index = 0U; index < live_updates.size(); ++index) {
        if (live_frame_sources[index] != source || !live_updates[index].surface) continue;
        (*entry.model).report_live_surface_shown(*live_updates[index].surface, live_frames[source].generation());
        return;
    }
}

bool NativeWindow::report_presented(const stxlib::Optional<PaintReceipt>& receipt, std::uint64_t elapsed,
                                    PresentedArea copied) {
    std::uint64_t live_pixels = 0U;
    std::uint64_t live_frames_copied = 0U;
    for (std::size_t index = 0U; index < live_frame_rectangles.size(); ++index) {
        if (live_frame_rectangles[index] == 0U) continue;
        live_pixels += live_frame_pixels[index];
        ++live_frames_copied;
    }
    if (live_pixels > copied.device_pixels) live_pixels = copied.device_pixels;
    bool acknowledged = false;
    std::uint64_t live_elapsed = elapsed;
    if (receipt.present()) {
        PresentedArea retained;
        retained.device_pixels = copied.device_pixels - live_pixels;
        retained.rectangles = copied.rectangles;
        retained.copy_nanoseconds = copied.copy_nanoseconds;
        live_elapsed = copied.device_pixels == 0U
                           ? 0U
                           : static_cast<std::uint64_t>(static_cast<double>(elapsed) *
                                                        static_cast<double>(live_pixels) /
                                                        static_cast<double>(copied.device_pixels));
        acknowledged = (*entry.model).notify_presented(*receipt.value(), elapsed - live_elapsed, retained);
    }
    const std::uint64_t live_duration = live_frames_copied == 0U ? 0U : live_elapsed / live_frames_copied;
    for (std::size_t index = 0U; index < live_frame_rectangles.size(); ++index) {
        if (live_frame_rectangles[index] == 0U) continue;
        PresentedArea live;
        live.device_pixels = live_frame_pixels[index];
        live.rectangles = live_frame_rectangles[index];
        (*entry.model).notify_live_presented(live_duration, live);
    }
    live_frame_rectangles.clear();
    return acknowledged;
}

// ---------------------------------------------------------------------------
// Runtime

Runtime::Runtime(SurfaceHost& surface_host, std::shared_ptr<gui_forms::detail::BundledFontSet> run_fonts)
    : host(surface_host), queue(surface_host.input()), fonts(std::move(run_fonts)),
      actions(gui_forms::detail::make_library_shared<ActionQueue>(surface_host.input())) {
    const SurfaceHostStart start = host.start();
    extent = Size{std::max(1.0, start.extent.width), std::max(1.0, start.extent.height)};
    scale = std::isfinite(start.scale) && start.scale > 0.0 ? start.scale : 1.0;
    desk.set_extent(extent);
    layers_.reserve(4U * HostWindowing::maximum_windows + 1U);
    device_rects_.reserve(maximum_compose_rects);
    policy_ = host.presentation_policy();
}

HostCapabilities Runtime::capabilities() const {
    HostCapabilities report = host.capabilities();
    report.available = report.available | HostCapability::reference_presentation | HostCapability::direct_buffers;
    if (policy_.host_scroll) report.available = report.available | HostCapability::host_scroll;
    return report;
}

Runtime::~Runtime() {
    // A copy: a closed callback may open a replacement window.
    const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
    for (std::size_t index = snapshot.size(); index > 0U; --index)
        (*snapshot[index - 1U]).close(HostCloseReason::application, true);
    // The host's surface is its own, so nothing it may still read goes with
    // the windows.
    windows.clear();
}

std::shared_ptr<NativeWindow> Runtime::add(SurfaceApplicationWindow entry, HostWindowId window_id, Rect content,
                                           bool initial) {
    const std::shared_ptr<NativeWindow> window =
        gui_forms::detail::make_library_shared<NativeWindow>(*this, std::move(entry), window_id, content, initial);
    windows.push_back(window);
    (*window).attach();
    scene_dirty_ = true;
    return window;
}

NativeWindow* Runtime::window(HostWindowId window_id) noexcept {
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        NativeWindow& candidate = *windows[index];
        if (!candidate.closed && candidate.window_id == window_id) return &candidate;
    }
    return nullptr;
}

bool Runtime::primary_open() const noexcept {
    bool open = false;
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        const NativeWindow& candidate = *windows[index];
        if (!candidate.closed && candidate.initial && is_primary(candidate.entry)) open = true;
    }
    return open;
}

NativeWindow* Runtime::keyboard_window() noexcept {
    NativeWindow* found = window(desk.active());
    if (found != nullptr && desk.blocked((*found).window_id)) found = nullptr;
    return found;
}

void Runtime::activation_moved(HostWindowId before) {
    const HostWindowId after = desk.active();
    scene_dirty_ = true;
    if (before == after) return;
    // A capsule the keyboard held, or one armed, belongs to the window that
    // lost the keyboard.
    const HostWindowId held = capsule.window();
    if (held.valid() && held != after && (capsule.keyboard() || capsule.arm() != WindowCapsuleArm::idle))
        forget_capsule(held);
    NativeWindow* lost = window(before);
    if (lost != nullptr) (*lost).set_active(false);
    NativeWindow* gained = window(after);
    if (gained != nullptr) (*gained).set_active(true);
}

void Runtime::activate(HostWindowId target, bool takes_focus) {
    const HostWindowId before = desk.active();
    const HostWindowId after = desk.activate(target, takes_focus);
    scene_dirty_ = true;
    if (before == after) {
        NativeWindow* same = window(after);
        if (same != nullptr) (*same).set_active(true);
        return;
    }
    activation_moved(before);
}

void Runtime::activate_topmost() {
    const HostWindowId before = desk.active();
    static_cast<void>(desk.activate_topmost());
    activation_moved(before);
}

bool Runtime::minimize_window(HostWindowId target) {
    NativeWindow* found = window(target);
    if (found == nullptr) return false;
    const HostWindowId before = desk.active();
    if (!desk.minimize(target)) return false;
    if (pressed_window.valid() && !desk.shown(pressed_window)) pressed_window = {};
    forget_capsule(target);
    activation_moved(before);
    static_cast<void>((*found).dispatch(HostOcclusionEvent{true}));
    if (windowing && (*windowing).live(target)) (*windowing).report_minimized(target, true);
    return true;
}

bool Runtime::maximize_window(HostWindowId target) {
    NativeWindow* found = window(target);
    if (found == nullptr || !desk.maximize(target)) return false;
    const std::optional<Rect> placed = desk.content(target);
    if (placed.has_value()) (*found).apply_content(*placed);
    (*found).refresh_chrome();
    scene_dirty_ = true;
    return true;
}

bool Runtime::restore_window(HostWindowId target) {
    NativeWindow* found = window(target);
    if (found == nullptr) return false;
    const bool was_minimized = desk.state(target) == SurfaceWindowState::minimized;
    if (!desk.restore(target)) return false;
    const std::optional<Rect> placed = desk.content(target);
    if (placed.has_value()) (*found).apply_content(*placed);
    (*found).refresh_chrome();
    scene_dirty_ = true;
    if (was_minimized) {
        static_cast<void>((*found).dispatch(HostOcclusionEvent{host_hidden}));
        if (windowing && (*windowing).live(target)) (*windowing).report_minimized(target, false);
        activate(target, true);
    }
    return true;
}

bool Runtime::toggle_maximized(HostWindowId target) {
    // A tiled window maximises; a maximised one restores.
    const SurfaceWindowState state = desk.state(target);
    const bool changed = state == SurfaceWindowState::maximized ? restore_window(target) : maximize_window(target);
    return changed;
}

void Runtime::show_cursor(CursorKind cursor) {
    if (host.draws_cursor()) {
        host.set_cursor(cursor);
        return;
    }
    if (cursor == cursor_kind_) return;
    cursor_kind_ = cursor;
    scene_dirty_ = true;
}

void Runtime::damage_surface() noexcept {
    surface_damage_.add({0.0, 0.0, extent.width, extent.height});
    scene_dirty_ = true;
}

void Runtime::reclaim() {
    // The host never reads a window's raster (the surface holds copies), so
    // a closed runtime window goes at once.
    for (std::size_t index = windows.size(); index > 0U; --index) {
        const NativeWindow& candidate = *windows[index - 1U];
        if (candidate.initial || !candidate.closed) continue;
        windows.erase(windows.begin() + static_cast<std::ptrdiff_t>(index - 1U));
        scene_dirty_ = true;
    }
}

std::optional<std::chrono::steady_clock::time_point> Runtime::next_deadline(bool& immediate) {
    bool work = scene_dirty_;
    for (std::size_t index = 0U; index < windows.size() && !work; ++index) {
        if ((*windows[index]).has_compose_work()) work = true;
    }
    immediate = work && !present_pending_ && !surface_inactive_;
    std::optional<std::chrono::steady_clock::time_point> deadline{};
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        const NativeWindow& candidate = *windows[index];
        if (candidate.closed) continue;
        const stxlib::Optional<FrameTime> wake = (*candidate.entry.model).next_wake();
        if (wake.absent()) continue;
        const FrameTime armed = *wake.value();
        if (!deadline.has_value() || armed < *deadline) deadline = armed;
    }
    // The capsule's animation, only while it runs.
    if (capsule.animating()) {
        const std::chrono::steady_clock::time_point frame = std::chrono::steady_clock::now() + capsule_frame_interval;
        if (!deadline.has_value() || frame < *deadline) deadline = frame;
    }
    return deadline;
}

PendingDialog Runtime::wait_for_dialog(std::uint64_t ticket) {
    PendingDialog pending{};
    pending.ticket = ticket;
    dialogs_.push_back(&pending);
    while (!pending.done && !quitting && primary_open()) step();
    for (std::size_t index = dialogs_.size(); index > 0U; --index) {
        if (dialogs_[index - 1U] == &pending) {
            dialogs_.erase(dialogs_.begin() + static_cast<std::ptrdiff_t>(index - 1U));
            break;
        }
    }
    if (!pending.done) pending.outcome = SurfaceDialogOutcome::cancelled;
    return pending;
}

void Runtime::step() {
    bool immediate = false;
    const std::optional<std::chrono::steady_clock::time_point> deadline = next_deadline(immediate);
    std::optional<std::chrono::steady_clock::time_point> wait = deadline;
    // Records an outer turn took and has not handled yet (a nested loop
    // began under one of them) are handled now, so nothing waits for them.
    if (immediate || !records_.empty()) wait = std::chrono::steady_clock::now();
    static_cast<void>(queue.take(records_, wait));
    // The queued show, hide, close and full-screen requests.
    std::deque<Action> queued;
    {
        const stxlib::ScopedLock guard((*actions).mutex);
        queued.swap((*actions).pending);
    }
    for (std::size_t index = 0U; index < queued.size(); ++index) {
        const std::shared_ptr<NativeWindow> target = queued[index].window.lock();
        if (target) (*target).action(queued[index].kind);
    }
    // One record is the native callback of this loop: application code runs
    // under it, so it goes through the fail-fast boundary. Records are taken
    // off the member queue one at a time: a nested turn (a modal loop, a
    // dialog) begun under one continues with the records behind it, in
    // order, and its newer records join behind those. Taking the batch into
    // a local once left them waiting, a presented record among them, until
    // the nested loop ended (A-gf-host-02-F3).
    while (!records_.empty()) {
        SurfaceInputRecord record = std::move(records_.front());
        records_.pop_front();
        gui_forms::detail::call_failing_fast("surface.input_record", &Runtime::handle, *this, record);
    }
    advance_capsule();
    const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
    bool wanted = false;
    for (std::size_t index = 0U; index < snapshot.size(); ++index) {
        NativeWindow& candidate = *snapshot[index];
        candidate.update();
        if (candidate.wants_frames()) wanted = true;
    }
    if (wanted != frames_wanted_) {
        frames_wanted_ = wanted;
        host.want_frames(wanted);
    }
    compose();
    place_input();
}

Rect Runtime::cursor_rect() const noexcept {
    if (host.draws_cursor() || !cursor_inside_ || cursor_sprite_.width == 0U) return Rect{};
    const Rect rect{pointer_position.x - static_cast<double>(cursor_sprite_.hotspot_x) / scale,
                    pointer_position.y - static_cast<double>(cursor_sprite_.hotspot_y) / scale,
                    static_cast<double>(cursor_sprite_.width) / scale,
                    static_cast<double>(cursor_sprite_.height) / scale};
    return rect;
}

// The surface's damage since the last compose: the scene's differences
// (layers moved, sized, restacked, shown, hidden or changed state; tabs
// changed), each shown window's painted rectangles, its strip's, and the
// drawn cursor's old and new places.
void Runtime::gather_damage() {
    desk.scene(quads_);
    desk.tabs(tabs_);
    const std::size_t count = std::max(quads_.size(), composed_quads_.size());
    for (std::size_t index = 0U; index < count; ++index) {
        const bool before = index < composed_quads_.size();
        const bool now = index < quads_.size();
        if (before && now && composed_quads_[index] == quads_[index]) continue;
        if (before) surface_damage_.add(composed_quads_[index].outer);
        if (now) surface_damage_.add(quads_[index].outer);
    }
    bool tabs_changed = tabs_.size() != composed_tabs_.size();
    for (std::size_t index = 0U; index < tabs_.size() && !tabs_changed; ++index) {
        tabs_changed = tabs_[index].window != composed_tabs_[index].window ||
                       tabs_[index].bounds != composed_tabs_[index].bounds;
    }
    const SurfaceFrameTheme& theme = desk.theme();
    const Rect tab_band{0.0, extent.height - theme.tab_height - 2.0 * theme.tab_gap, extent.width,
                        theme.tab_height + 2.0 * theme.tab_gap};
    if (tabs_changed) surface_damage_.add(tab_band);
    // A window's rectangles land a device pixel either way of where its
    // logical origin rounds, so they are widened by one.
    const double slack = 1.0 / scale;
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        NativeWindow& candidate = *windows[index];
        if (candidate.closed || !desk.shown(candidate.window_id)) {
            if (candidate.chrome && !candidate.chrome_composed.empty() && !candidate.closed &&
                desk.state(candidate.window_id) == SurfaceWindowState::minimized)
                surface_damage_.add(tab_band);
            candidate.chrome_composed.clear();
            continue;
        }
        const Rect client = candidate.content;
        // A window whose scroll copies may move the surface's pixels
        // (reference host scroll) gives its painted rectangles without the
        // copies' clips, apart from the rest, for compose to decide.
        const bool scrolling = policy_.host_scroll && candidate.copies_composable &&
                               !candidate.composed_copies.empty() && !candidate.raster_resized;
        if (candidate.raster_resized) {
            surface_damage_.add(client);
        } else if (scrolling) {
            scrolling_.push_back(&candidate);
            for (std::size_t rect = 0U; rect < candidate.composed_painted.size(); ++rect) {
                const Rect local = candidate.composed_painted[rect];
                scrolling_damage_.add(Rect{client.x + local.x - slack, client.y + local.y - slack,
                                           local.width + 2.0 * slack, local.height + 2.0 * slack});
            }
        } else {
            for (std::size_t rect = 0U; rect < candidate.composed.size(); ++rect) {
                const Rect local = candidate.composed[rect];
                surface_damage_.add(Rect{client.x + local.x - slack, client.y + local.y - slack,
                                         local.width + 2.0 * slack, local.height + 2.0 * slack});
            }
        }
        candidate.composed.clear();
        candidate.composed_painted.clear();
        if (!scrolling) {
            candidate.composed_copies.clear();
            candidate.copies_composable = false;
        }
        candidate.raster_resized = false;
        if (candidate.chrome) {
            const std::optional<Rect> outer = desk.outer(candidate.window_id);
            const double strip_top = outer.has_value() ? (*outer).y + theme.border_width : client.y;
            if (candidate.chrome_resized) {
                surface_damage_.add(Rect{client.x, strip_top, client.width, client.y - strip_top});
            } else {
                for (std::size_t rect = 0U; rect < candidate.chrome_composed.size(); ++rect) {
                    const Rect local = candidate.chrome_composed[rect];
                    surface_damage_.add(Rect{client.x + local.x - slack, strip_top + local.y - slack,
                                             local.width + 2.0 * slack, local.height + 2.0 * slack});
                }
            }
            candidate.chrome_composed.clear();
            candidate.chrome_resized = false;
        }
    }
    // The capsule's snap preview.
    const Rect snap = snap_preview_rect();
    if (snap != snap_composed_) {
        if (!snap_composed_.empty()) surface_damage_.add(snap_composed_);
        if (!snap.empty()) surface_damage_.add(snap);
    }
    // The drawn cursor: a new sprite when its kind or the scale changed.
    if (!host.draws_cursor()) {
        const std::uint32_t factor = static_cast<std::uint32_t>(std::max(1.0, std::round(scale)));
        if (cursor_sprite_.width == 0U || cursor_sprite_kind_ != cursor_kind_ || cursor_sprite_factor_ != factor) {
            surface_cursor_sprite(cursor_kind_, factor, cursor_sprite_);
            cursor_sprite_kind_ = cursor_kind_;
            cursor_sprite_factor_ = factor;
        }
        const Rect now = cursor_rect();
        if (now != cursor_composed_) {
            if (!cursor_composed_.empty()) surface_damage_.add(cursor_composed_);
            if (!now.empty()) surface_damage_.add(now);
        }
    }
}

// The scene back to front, in device pixels: each layer's edge, title strip
// and client raster, then the tabs, then the drawn cursor.
void Runtime::build_layers() {
    layers_.clear();
    const SurfaceFrameTheme& theme = desk.theme();
    for (std::size_t index = 0U; index < quads_.size(); ++index) {
        const SurfaceDeskQuad& quad = quads_[index];
        NativeWindow* found = window(quad.window);
        if (found == nullptr || (*found).raster.pixels() == nullptr) continue;
        const NativeWindow& target = *found;
        if (quad.framed) {
            layers_.push_back(
                solid_layer(device_rect(quad.outer, scale), quad.active ? theme.border_active : theme.border_inactive));
            if (target.chrome && (*target.chrome).raster().pixels() != nullptr) {
                const std::int32_t left = device_coordinate(quad.content.x, scale);
                const std::int32_t top = device_coordinate(quad.outer.y + theme.border_width, scale);
                SurfaceComposeLayer strip = image_layer((*target.chrome).raster(), left, top, false);
                // The strip never covers the client area beneath it.
                strip.bounds.bottom = std::min(strip.bounds.bottom, device_coordinate(quad.content.y, scale));
                layers_.push_back(strip);
            }
        }
        const bool blended = target.entry.popup.present() || target.present_alpha;
        layers_.push_back(image_layer(target.raster, device_coordinate(quad.content.x, scale),
                                      device_coordinate(quad.content.y, scale), blended));
    }
    // A tab is an edge around the left part of the window's title strip,
    // until the theme engine supplies a tab part (an owner choice of
    // 2026-11-15).
    for (std::size_t index = 0U; index < tabs_.size(); ++index) {
        const SurfaceDeskTab& tab = tabs_[index];
        NativeWindow* found = window(tab.window);
        if (found == nullptr) continue;
        const SurfaceDeviceRect bounds = device_rect(tab.bounds, scale);
        layers_.push_back(solid_layer(bounds, theme.border_inactive));
        if ((*found).chrome && (*(*found).chrome).raster().pixels() != nullptr) {
            const std::int32_t inset = std::max<std::int32_t>(1, device_coordinate(theme.border_width, scale));
            SurfaceComposeLayer strip = image_layer((*(*found).chrome).raster(), bounds.left + inset,
                                                    bounds.top + inset, false);
            strip.bounds.right = std::min(strip.bounds.right, bounds.right - inset);
            strip.bounds.bottom = std::min(strip.bounds.bottom, bounds.bottom - inset);
            layers_.push_back(strip);
        }
    }
    const Rect snap = snap_preview_rect();
    if (!snap.empty()) {
        SurfaceComposeLayer preview{};
        preview.kind = SurfaceLayerKind::tint;
        preview.bounds = device_rect(snap, scale);
        preview.color = surface_tint_pixel(theme.snap_preview, theme.snap_preview_alpha);
        layers_.push_back(preview);
    }
    const Rect cursor = cursor_rect();
    if (!cursor.empty()) {
        SurfaceComposeLayer sprite{};
        sprite.kind = SurfaceLayerKind::blended_image;
        sprite.pixels = cursor_sprite_.pixels.data();
        sprite.row_bytes = static_cast<std::size_t>(cursor_sprite_.width) * 4U;
        sprite.width = cursor_sprite_.width;
        sprite.height = cursor_sprite_.height;
        const std::int32_t left = device_coordinate(pointer_position.x, scale) - cursor_sprite_.hotspot_x;
        const std::int32_t top = device_coordinate(pointer_position.y, scale) - cursor_sprite_.hotspot_y;
        sprite.bounds = SurfaceDeviceRect{left, top, left + static_cast<std::int32_t>(sprite.width),
                                          top + static_cast<std::int32_t>(sprite.height)};
        layers_.push_back(sprite);
    }
}

void Runtime::compose() {
    // While the host reads the last present, or another surface holds the
    // screen, the surface is left alone; the damage waits.
    if (present_pending_ || surface_inactive_) return;
    scrolling_.clear();
    scrolling_damage_.clear();
    moved_areas_.clear();
    gather_damage();
    scene_dirty_ = false;
    if (surface_damage_.empty() && scrolling_.empty()) {
        composed_quads_ = quads_;
        composed_tabs_ = tabs_;
        snap_composed_ = snap_preview_rect();
        return;
    }
    const std::uint32_t width = device_extent(extent.width, scale);
    const std::uint32_t height = device_extent(extent.height, scale);
    // The surface is the shim's direct path (the reference): acquired and
    // presented through it, so that its trace is the reference trace.
    if (!presenter_) {
        presenter_ = std::make_unique<presentation::PresentationShim>(
            nullptr, std::make_unique<SurfaceHostDirectPath>(host), host.presentation_trace());
    }
    const presentation::DirectAcquire acquired =
        (*presenter_).acquire_for_writing(presentation::PresentExtent{width, height});
    const presentation::DirectBuffer* const granted = acquired.buffer();
    if (granted == nullptr) {
        // Tried again on the next turn; the scrolling windows' rectangles and
        // clips wait with the rest.
        for (std::size_t index = 0U; index < scrolling_.size(); ++index) {
            NativeWindow& window = *scrolling_[index];
            for (std::size_t copy = 0U; copy < window.composed_copies.size(); ++copy) {
                const Rect clip = window.composed_copies[copy].clip;
                surface_damage_.add(Rect{window.content.x + clip.x, window.content.y + clip.y, clip.width,
                                         clip.height});
            }
            window.composed_copies.clear();
            window.copies_composable = false;
        }
        const std::span<const Rect> waiting = scrolling_damage_.rectangles();
        for (std::size_t index = 0U; index < waiting.size(); ++index) surface_damage_.add(waiting[index]);
        view_ = SurfaceView{};
        scene_dirty_ = true;
        return;
    }
    view_ = SurfaceView{reinterpret_cast<std::uint8_t*>((*granted).pixels), (*granted).row_bytes, width, height};
    // A new surface holds nothing yet.
    if ((*granted).age == 0U) surface_damage_.add({0.0, 0.0, extent.width, extent.height});
    build_layers();
    // The reference host scroll: what everything but the scrolling windows
    // damaged, then each scrolling window's copies moved or composed.
    if (!scrolling_.empty()) {
        std::array<SurfaceDeviceRect, maximum_compose_rects> foreign{};
        const std::size_t foreign_count =
            surface_device_rects(surface_damage_.rectangles(), scale, width, height, foreign);
        const std::span<const SurfaceDeviceRect> foreign_rects(foreign.data(), foreign_count);
        for (std::size_t index = 0U; index < scrolling_.size(); ++index)
            compose_scrolls(*scrolling_[index], foreign_rects);
        const std::span<const Rect> painted = scrolling_damage_.rectangles();
        for (std::size_t index = 0U; index < painted.size(); ++index) surface_damage_.add(painted[index]);
    }
    std::array<SurfaceDeviceRect, maximum_compose_rects> device{};
    const std::size_t count = surface_device_rects(surface_damage_.rectangles(), scale, width, height, device);
    composed_quads_ = quads_;
    composed_tabs_ = tabs_;
    cursor_composed_ = cursor_rect();
    snap_composed_ = snap_preview_rect();
    device_rects_.assign(device.begin(), device.begin() + static_cast<std::ptrdiff_t>(count));
    const SurfaceTarget target{view_.pixels, view_.row_bytes, view_.width, view_.height};
    const SurfacePixel background = surface_opaque_pixel(desk.theme().background);
    for (std::size_t index = 0U; index < device_rects_.size(); ++index)
        surface_compose(target, device_rects_[index], background, layers_);
    // The present: what was composed, and every area moved in place (the
    // screen changed there too).
    present_rects_.clear();
    for (std::size_t index = 0U; index < device_rects_.size(); ++index) {
        const SurfaceDeviceRect rect = device_rects_[index];
        present_rects_.push_back(presentation::PresentRect{rect.left, rect.top, rect.right, rect.bottom});
    }
    for (std::size_t index = 0U; index < moved_areas_.size(); ++index) {
        const SurfaceDeviceRect rect = moved_areas_[index];
        present_rects_.push_back(presentation::PresentRect{rect.left, rect.top, rect.right, rect.bottom});
    }
    // Nothing to show ends the grant unpresented (present_written with no
    // rectangle does not reach the host).
    const presentation::PresentOutcome outcome = (*presenter_).present_written(present_rects_);
    if (present_rects_.empty()) {
        surface_damage_.clear();
        return;
    }
    switch (outcome) {
    case presentation::PresentOutcome::presented:
        surface_damage_.clear();
        break;
    case presentation::PresentOutcome::pending:
        surface_damage_.clear();
        present_pending_ = true;
        break;
    case presentation::PresentOutcome::inactive:
        surface_inactive_ = true;
        break;
    case presentation::PresentOutcome::failed:
    case presentation::PresentOutcome::lost:
    default:
        // Kept for the next turn, with the areas moved in place: the host
        // never showed them, so they are composed again from the rasters.
        for (std::size_t index = 0U; index < moved_areas_.size(); ++index) {
            const SurfaceDeviceRect area = moved_areas_[index];
            surface_damage_.add(Rect{static_cast<double>(area.left) / scale, static_cast<double>(area.top) / scale,
                                     static_cast<double>(area.right - area.left) / scale,
                                     static_cast<double>(area.bottom - area.top) / scale});
        }
        scene_dirty_ = true;
        break;
    }
}

void Runtime::compose_scrolls(NativeWindow& window, std::span<const SurfaceDeviceRect> foreign) {
    const SurfaceDeviceRect surface{0, 0, static_cast<std::int32_t>(view_.width),
                                    static_cast<std::int32_t>(view_.height)};
    // The window's client layer: an opaque copy of its raster, and the
    // layers composed after it (which may cover the copies' areas).
    std::size_t client_layer = layers_.size();
    for (std::size_t index = 0U; index < layers_.size(); ++index) {
        const SurfaceComposeLayer& layer = layers_[index];
        if (layer.pixels == static_cast<const std::uint8_t*>(window.raster.pixels())) client_layer = index;
    }
    const bool opaque = client_layer < layers_.size() &&
                        layers_[client_layer].kind == SurfaceLayerKind::opaque_image;
    const std::int32_t origin_left = opaque ? layers_[client_layer].bounds.left : 0;
    const std::int32_t origin_top = opaque ? layers_[client_layer].bounds.top : 0;
    // Once one copy cannot move, the later ones cannot either: their source
    // pixels would be the ones that copy should have moved.
    bool movable = opaque;
    for (std::size_t copy_index = 0U; copy_index < window.composed_copies.size(); ++copy_index) {
        const ScrollCopy& copy = window.composed_copies[copy_index];
        // The copy in surface device pixels: request_scroll_copy admits
        // only clips and offsets on whole device pixels.
        const SurfaceDeviceRect area{origin_left + device_coordinate(copy.clip.x, scale),
                                     origin_top + device_coordinate(copy.clip.y, scale),
                                     origin_left + device_coordinate(copy.clip.right(), scale),
                                     origin_top + device_coordinate(copy.clip.bottom(), scale)};
        const presentation::PresentScroll scroll{
            presentation::PresentRect{area.left, area.top, area.right, area.bottom},
            device_coordinate(copy.offset.x, scale), device_coordinate(copy.offset.y, scale)};
        bool eligible = movable && !area.empty();
        if (eligible) {
            const presentation::PresentRect whole{surface.left, surface.top, surface.right, surface.bottom};
            const presentation::PresentRect layer_bounds{
                layers_[client_layer].bounds.left, layers_[client_layer].bounds.top,
                layers_[client_layer].bounds.right, layers_[client_layer].bounds.bottom};
            eligible = presentation::present_rect_contains(whole, scroll.area) &&
                       presentation::present_rect_contains(layer_bounds, scroll.area);
        }
        for (std::size_t index = client_layer + 1U; eligible && index < layers_.size(); ++index) {
            const SurfaceDeviceRect bounds = layers_[index].bounds;
            const presentation::PresentRect above{bounds.left, bounds.top, bounds.right, bounds.bottom};
            if (!presentation::present_rect_intersection(above, scroll.area).empty()) eligible = false;
        }
        for (std::size_t index = 0U; eligible && index < foreign.size(); ++index) {
            const presentation::PresentRect damaged{foreign[index].left, foreign[index].top, foreign[index].right,
                                                    foreign[index].bottom};
            if (!presentation::present_rect_intersection(damaged, scroll.area).empty()) eligible = false;
        }
        bool moved = false;
        if (eligible) {
            const presentation::PresentScrollResult result = (*presenter_).scroll_written(scroll);
            moved = result.moved;
            if (moved) {
                moved_areas_.push_back(area);
                for (std::size_t band = 0U; band < result.plan.exposed_count; ++band) {
                    const presentation::PresentRect exposed = result.plan.exposed[band];
                    surface_damage_.add(Rect{static_cast<double>(exposed.left) / scale,
                                             static_cast<double>(exposed.top) / scale,
                                             static_cast<double>(exposed.right - exposed.left) / scale,
                                             static_cast<double>(exposed.bottom - exposed.top) / scale});
                }
            }
        } else {
            (*presenter_).decline_scroll(scroll);
        }
        if (!moved) {
            movable = false;
            surface_damage_.add(Rect{window.content.x + copy.clip.x, window.content.y + copy.clip.y, copy.clip.width,
                                     copy.clip.height});
        }
    }
    window.composed_copies.clear();
    window.copies_composable = false;
}

void Runtime::place_input() {
    NativeWindow* target = keyboard_window();
    std::optional<Rect> caret{};
    if (target != nullptr && !(*target).closed) {
        const stxlib::Optional<Rect> local = (*(*target).entry.model).text_input_caret();
        if (local.present()) {
            const Rect& own = *local.value();
            caret = Rect{own.x + (*target).content.x, own.y + (*target).content.y, own.width, own.height};
        }
    }
    const bool editing = caret.has_value();
    if (caret == placed_caret_ && editing == placed_editing_) return;
    placed_caret_ = caret;
    placed_editing_ = editing;
    const Rect box = caret.has_value() ? *caret : Rect{};
    host.place_text_input(box, editing);
}

void Runtime::begin_modal_scope(HostWindowId modal) {
    desk.begin_modal(modal);
    if (pressed_window.valid() && desk.blocked(pressed_window)) pressed_window = {};
    if (pressed_part.window.valid() && desk.blocked(pressed_part.window)) pressed_part = {};
}

SurfaceDeskHit Runtime::pointer_target(Point point) const noexcept {
    SurfaceDeskHit hit = desk.hit(point);
    // A pressed window a modal loop blocked keeps nothing (A-gf-host-02-F2).
    if (!pressed_window.valid() || desk.blocked(pressed_window)) return hit;
    const std::optional<Rect> content = desk.content(pressed_window);
    if (!content.has_value()) return hit;
    hit.window = pressed_window;
    hit.part = SurfaceDeskPart::client;
    hit.local = Point{point.x - (*content).x, point.y - (*content).y};
    return hit;
}

void Runtime::handle(SurfaceInputRecord& record) {
    switch (record.kind) {
    case SurfaceInputKind::pointer:
        handle_pointer(record);
        break;
    case SurfaceInputKind::wheel:
        handle_wheel(record);
        break;
    case SurfaceInputKind::key:
        handle_key(record);
        break;
    case SurfaceInputKind::text:
        handle_text(record, false);
        break;
    case SurfaceInputKind::composition:
        handle_text(record, true);
        break;
    case SurfaceInputKind::clipboard:
        clipboard_text = std::move(record.text);
        ++clipboard_generation;
        break;
    case SurfaceInputKind::resize:
        handle_resize(record);
        break;
    case SurfaceInputKind::safe_insets:
        handle_safe_insets(record);
        break;
    case SurfaceInputKind::focus: {
        host_focused = record.down;
        NativeWindow* active_window = window(desk.active());
        if (active_window != nullptr) (*active_window).set_active(true);
        if (windowing) (*windowing).report_application_active(host_focused);
        break;
    }
    case SurfaceInputKind::visibility: {
        host_hidden = record.down;
        const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
        for (std::size_t index = 0U; index < snapshot.size(); ++index) {
            NativeWindow& candidate = *snapshot[index];
            if (candidate.closed || !candidate.visible) continue;
            static_cast<void>(candidate.dispatch(HostOcclusionEvent{host_hidden}));
        }
        break;
    }
    case SurfaceInputKind::presented:
        present_pending_ = false;
        if (presenter_) (*presenter_).notify_released();
        break;
    case SurfaceInputKind::activation:
        surface_inactive_ = !record.down;
        if (record.down) damage_surface();
        break;
    case SurfaceInputKind::frame: {
        // The turn's update follows: live surfaces sample. The host reports
        // no display timing, so the frames' arrival times are each window's
        // presentation completions: the estimator measures the display's
        // period and phase from them (SurfaceRoot::
        // note_presentation_completed), on its own thread.
        const FrameTime arrived = FrameClock::now();
        const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
        for (std::size_t index = 0U; index < snapshot.size(); ++index) {
            NativeWindow& candidate = *snapshot[index];
            if (candidate.closed || !candidate.entry.model) continue;
            static_cast<void>((*candidate.entry.model).note_presentation_completed(arrived));
        }
        break;
    }
    case SurfaceInputKind::dialog:
        for (std::size_t index = 0U; index < dialogs_.size(); ++index) {
            PendingDialog& pending = *dialogs_[index];
            if (pending.ticket != record.request) continue;
            pending.done = true;
            pending.outcome = record.outcome;
            pending.text = std::move(record.text);
            pending.paths = std::move(record.paths);
            break;
        }
        break;
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
    // Drops from other programs: not compiled in a high-integrity build
    // (cmake/HighIntegrity.cmake), where a drop record takes the default.
    case SurfaceInputKind::drop:
        handle_drop(record);
        break;
#endif
    case SurfaceInputKind::fonts: {
        // A font group arrived: text that fell back may now find its face.
        const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
        for (std::size_t index = 0U; index < snapshot.size(); ++index) {
            NativeWindow& candidate = *snapshot[index];
            if (candidate.closed) continue;
            candidate.damage.add({0.0, 0.0, candidate.content.width, candidate.content.height});
        }
        break;
    }
    case SurfaceInputKind::close: {
        quitting = true;
        const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
        for (std::size_t index = snapshot.size(); index > 0U; --index)
            (*snapshot[index - 1U]).close(HostCloseReason::session_end, true);
        break;
    }
    default:
        break;
    }
}

void Runtime::leave_pointer_window(Point point) {
    NativeWindow* left = window(pointer_window);
    if (left == nullptr || pressed_window.valid()) return;
    PointerEvent event;
    event.action = PointerAction::leave;
    event.pointer_id = 1U;
    event.position = Point{point.x - (*left).content.x, point.y - (*left).content.y};
    pointer_window = {};
    (*left).pointer(std::move(event));
}

// A press on a frame: a box or tab is acted on at its release over the same
// part; a capsule section goes to the capsule's gestures; a double click on
// the title bar maximises or restores (the desk refuses a window that
// cannot); the title bar and the grips start a frame drag.
void Runtime::handle_frame_press(const SurfaceInputRecord& record, const SurfaceDeskHit& hit) {
    if (record.button != PointerButton::primary) return;
    if (frame_part_clicks(hit.part)) {
        pressed_part = PressedPart{hit.window, hit.part};
        return;
    }
    const WindowCapsuleSection section = capsule_section(hit.part);
    if (section != WindowCapsuleSection::none) {
        const std::uint32_t clicks =
            next_surface_click_count(title_clicks_, record.button, record.time, record.position,
                                     default_double_click_milliseconds, default_double_click_distance);
        const WindowCapsuleOutcome outcome = capsule.press(hit.window, section, record.position, clicks);
        if (capsule.pressing()) host.pointer_capture(true);
        capsule_outcome(outcome);
        return;
    }
    if (hit.part == SurfaceDeskPart::title_bar) {
        const std::uint32_t clicks =
            next_surface_click_count(title_clicks_, record.button, record.time, record.position,
                                     default_double_click_milliseconds, default_double_click_distance);
        if (clicks == 2U) {
            static_cast<void>(toggle_maximized(hit.window));
            return;
        }
    }
    if (desk.begin_frame_drag(hit.window, hit.part, record.position)) host.pointer_capture(true);
}

void Runtime::handle_pointer(const SurfaceInputRecord& record) {
    const Point point = record.position;
    const SurfacePointerAction action = record.action;
    pointer_position = point;
    if (!host.draws_cursor()) {
        cursor_inside_ = action != SurfacePointerAction::leave;
        scene_dirty_ = true;
    }
    // A capsule press owns the pointer until its release, a capsule move
    // included (it is a frame drag the capsule steers).
    if (handle_capsule_pointer(record)) return;
    // A frame drag owns the pointer until its release.
    if (desk.frame_dragging()) {
        if (action == SurfacePointerAction::move) {
            const HostWindowId dragged = desk.frame_drag_window();
            const std::optional<Rect> moved = desk.frame_drag_to(point);
            NativeWindow* target = window(dragged);
            if (moved.has_value() && target != nullptr) (*target).apply_content(*moved);
            return;
        }
        if (action == SurfacePointerAction::up || action == SurfacePointerAction::cancel) {
            desk.end_frame_drag();
            host.pointer_capture(false);
        }
        return;
    }
    if (action == SurfacePointerAction::leave) {
        if (capsule.hovered() != WindowCapsuleSection::none)
            capsule_outcome(capsule.hover(HostWindowId{}, WindowCapsuleSection::none));
        leave_pointer_window(point);
        return;
    }
    const SurfaceDeskHit hit = pointer_target(point);
    if (action == SurfacePointerAction::move || action == SurfacePointerAction::enter) {
        // The capsule under the pointer, if any: hovering its question mark
        // shows the title, and leaving it disarms it.
        const WindowCapsuleSection hovered =
            desk.blocked(hit.window) ? WindowCapsuleSection::none : capsule_section(hit.part);
        const bool changed = hovered != capsule.hovered() ||
                             (hovered != WindowCapsuleSection::none && hit.window != capsule.window());
        if (changed) capsule_outcome(capsule.hover(hit.window, hovered));
    }
    if (action == SurfacePointerAction::down) {
        if (!hit.window.valid()) return;
        if (hit.part == SurfaceDeskPart::minimized_tab) {
            pressed_part = PressedPart{hit.window, hit.part};
            return;
        }
        // A passive popup never takes the keyboard from its owner.
        const NativeWindow* pressed = window(hit.window);
        const bool passive = pressed != nullptr && (*pressed).entry.popup.present() &&
                             ((*(*pressed).entry.popup.value())).focus != HostPopupFocus::takes_focus;
        activate(hit.window, !passive);
        if (desk.blocked(hit.window)) return;
        if (hit.part != SurfaceDeskPart::client) {
            handle_frame_press(record, hit);
            return;
        }
    }
    if ((action == SurfacePointerAction::up || action == SurfacePointerAction::cancel) && pressed_part.window.valid()) {
        const PressedPart pressed = pressed_part;
        pressed_part = {};
        const bool over = action == SurfacePointerAction::up && hit.window == pressed.window &&
                          hit.part == pressed.part;
        if (!over) return;
        switch (pressed.part) {
        case SurfaceDeskPart::close_box: {
            NativeWindow* target = window(pressed.window);
            if (target != nullptr) (*target).close(HostCloseReason::user);
            break;
        }
        case SurfaceDeskPart::minimize_box:
            static_cast<void>(minimize_window(pressed.window));
            break;
        case SurfaceDeskPart::maximize_box:
            static_cast<void>(toggle_maximized(pressed.window));
            break;
        case SurfaceDeskPart::minimized_tab:
            static_cast<void>(restore_window(pressed.window));
            break;
        // The parts not acted on at their release (frame_part_clicks): their release does nothing.
        default:
            break;
        }
        return;
    }
    // Frame parts and tabs: their cursor, and the client window under the
    // pointer left.
    if (hit.part != SurfaceDeskPart::client || desk.blocked(hit.window)) {
        leave_pointer_window(point);
        show_cursor(frame_part_cursor(hit.part));
        return;
    }
    NativeWindow* target = window(hit.window);
    if (target == nullptr) return;
    if (pointer_window != hit.window) {
        leave_pointer_window(point);
        pointer_window = hit.window;
        PointerEvent enter;
        enter.action = PointerAction::enter;
        enter.pointer_id = 1U;
        enter.position = hit.local;
        enter.modifiers = record.modifiers;
        (*target).pointer(std::move(enter));
        if ((*target).closed) return;
        show_cursor((*target).current_cursor);
    }
    PointerEvent event;
    event.pointer_id = 1U;
    event.position = hit.local;
    event.modifiers = record.modifiers;
    switch (action) {
    case SurfacePointerAction::down:
        event.action = PointerAction::down;
        event.button = record.button;
        static_cast<void>(next_surface_click_count((*target).click_history, event.button, record.time, hit.local,
                                                   default_double_click_milliseconds,
                                                   default_double_click_distance));
        event.click_count = (*target).click_history.count;
        pressed_window = hit.window;
        break;
    case SurfacePointerAction::up:
    case SurfacePointerAction::cancel:
        event.action = PointerAction::up;
        event.button = record.button;
        event.click_count = (*target).click_history.count;
        pressed_window = {};
        break;
    case SurfacePointerAction::enter:
    case SurfacePointerAction::move:
        event.action = PointerAction::move;
        event.click_count = 0U;
        break;
    // leave returned above (leave_pointer_window); it and an out-of-range action send the window nothing.
    default:
        return;
    }
    (*target).pointer(std::move(event));
}

void Runtime::handle_wheel(const SurfaceInputRecord& record) {
    const SurfaceDeskHit hit = desk.hit(record.position);
    if (!hit.window.valid() || hit.part != SurfaceDeskPart::client || desk.blocked(hit.window)) return;
    NativeWindow* target = window(hit.window);
    if (target == nullptr) return;
    const SurfaceWheel wheel = surface_wheel(record.wheel_delta, record.wheel_unit, (*target).content.height);
    PointerEvent event;
    event.action = PointerAction::wheel;
    event.pointer_id = 1U;
    event.position = hit.local;
    event.modifiers = record.modifiers;
    event.wheel_delta = wheel.delta;
    event.wheel_unit = wheel.unit;
    event.click_count = 0U;
    (*target).pointer(std::move(event));
}

void Runtime::handle_key(const SurfaceInputRecord& record) {
    NativeWindow* target = keyboard_window();
    if (target == nullptr) return;
    // The capsule's keyboard: Alt+Space focuses the active window's capsule;
    // while it has the keyboard it takes each press it knows, and the
    // releases.
    if (capsule.keyboard() && capsule.window() == (*target).window_id) {
        if (!record.down) return;
        const WindowCapsuleOutcome outcome = capsule.key(record.physical_key, record.modifiers);
        capsule_outcome(outcome);
        if (outcome.handled) return;
        target = keyboard_window();
        if (target == nullptr) return;
    }
    const bool capsule_key = record.down && !record.repeat && record.physical_key == PhysicalKey::space &&
                             record.modifiers == Modifier::alt &&
                             desk.capsule((*target).window_id).width > 0.0;
    if (capsule_key) {
        capsule_outcome(capsule.focus((*target).window_id));
        return;
    }
    KeyEvent key;
    key.action = record.down ? KeyAction::down : KeyAction::up;
    key.physical_key = record.physical_key;
    key.modifiers = record.modifiers;
    key.repeat = record.repeat;
    const HostDispatchResult result = (*target).dispatch(key);
    if ((*target).closed) return;
    const bool context_key = record.down && !result.handled && !key.repeat &&
                             surface_requests_keyboard_context(key.physical_key, key.modifiers);
    if (context_key)
        static_cast<void>((*target).dispatch(HostContextRequestEvent{ContextRequestOrigin::keyboard, Point{}}));
}

void Runtime::handle_text(const SurfaceInputRecord& record, bool composition) {
    NativeWindow* target = keyboard_window();
    if (target == nullptr) return;
    if (!validate_utf8(record.text).valid()) return;
    TextInputEvent text;
    text.text_utf8 = record.text;
    text.composing = composition;
    if (composition) {
        (*target).composing = !record.text.empty();
    } else {
        (*target).composing = false;
        if (text.text_utf8.empty()) return;
    }
    static_cast<void>((*target).dispatch(std::move(text)));
}

void Runtime::handle_resize(const SurfaceInputRecord& record) {
    const Size next{std::max(1.0, record.extent.width), std::max(1.0, record.extent.height)};
    const double ratio = std::isfinite(record.scale) && record.scale > 0.0 ? record.scale : scale;
    const bool rescaled = ratio != scale;
    extent = next;
    scale = ratio;
    desk.set_extent(extent);
    damage_surface();
    const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
    for (std::size_t index = 0U; index < snapshot.size(); ++index) {
        NativeWindow& candidate = *snapshot[index];
        if (candidate.closed) continue;
        if (rescaled) candidate.apply_scale(scale);
        if (candidate.closed) continue;
        // The desk keeps every layer's frame top reachable; the primary
        // window fills the surface, a maximised one all of it.
        const std::optional<Rect> placed = desk.content(candidate.window_id);
        if (placed.has_value() && *placed != candidate.content) {
            candidate.apply_content(*placed);
        } else if (windowing) {
            (*windowing).report_native_frame(candidate);
        }
    }
}

void Runtime::handle_safe_insets(const SurfaceInputRecord& record) {
    // A report that is not valid_safe_insets is dropped: the last one stands.
    if (!valid_safe_insets(record.insets)) return;
    safe_insets = record.insets;
    const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
    for (std::size_t index = 0U; index < snapshot.size(); ++index) {
        NativeWindow& candidate = *snapshot[index];
        if (candidate.closed) continue;
        candidate.report_safe_insets();
    }
}

#if !defined(GUI_FORMS_HIGH_INTEGRITY)
// Not compiled in a high-integrity build (cmake/HighIntegrity.cmake).
void Runtime::handle_drop(const SurfaceInputRecord& record) {
    const SurfaceDeskHit hit = desk.hit(record.position);
    if (!hit.window.valid() || hit.part != SurfaceDeskPart::client || desk.blocked(hit.window)) return;
    NativeWindow* target = window(hit.window);
    if (target == nullptr || record.paths.empty()) return;
    DragFileListData files;
    for (std::size_t index = 0U; index < record.paths.size() && index < DragLimits::maximum_paths; ++index)
        files.paths_utf8.push_back(record.paths[index]);
    // The host delivers the files after the drop, so the program sees the
    // whole gesture at once: an enter, then the drop or a leave.
    DragEvent drag;
    drag.session_id = ++drag_sessions_;
    drag.allowed_effects = DragEffect::copy;
    drag.items.push_back(std::move(files));
    drag.position = hit.local;
    drag.modifiers = record.modifiers;
    drag.action = DragAction::enter;
    const bool accepted = (*target).dispatch(drag).drag_effect == DragEffect::copy;
    if ((*target).closed) return;
    drag.handled = false;
    drag.accepted_effect = DragEffect::none;
    drag.action = accepted ? DragAction::drop : DragAction::leave;
    static_cast<void>((*target).dispatch(drag));
}
#endif

// ---------------------------------------------------------------------------
// The run

HostWindowAdmission RunOpen::operator()(SurfaceApplicationWindow entry) const {
    HostWindowAdmission refused;
    const std::shared_ptr<RunOpenState> state = owner.lock();
    if (!state || (*state).windowing == nullptr) {
        refused.status.error = HostServiceError::after_shutdown;
        return refused;
    }
    if ((*state).thread != stxlib::current_thread()) {
        refused.status.error = HostServiceError::wrong_thread;
        return refused;
    }
    const HostWindowAdmission admission = (*(*state).windowing).open(std::move(entry), true);
    return admission;
}

WindowingEndGuard::~WindowingEndGuard() {
    (*state_).windowing = nullptr;
    try {
        if ((*options_).windowing_ended) (*options_).windowing_ended();
    } catch (...) {
    }
    (*windowing_).shutdown();
}

CompositorState surface_compositor_state() {
    // The manager composes every layer with premultiplied alpha. A window's
    // layer is transparent when its backdrop is granted; the materials are
    // not offered.
    CompositorState state{};
    state.generation = CompositorGeneration{1U};
    state.compositing = CompositorPresence::present;
    state.support[static_cast<std::size_t>(WindowBackdrop::transparent)] = WindowBackdropSupport::supported;
    state.support[static_cast<std::size_t>(WindowBackdrop::window_material)] = WindowBackdropSupport::host_unsupported;
    state.support[static_cast<std::size_t>(WindowBackdrop::transient_material)] =
        WindowBackdropSupport::host_unsupported;
    state.transparency_enabled = true;
    state.reduced_transparency = false;
    state.high_contrast = false;
    return state;
}

int run_surface_application(SurfaceHost& host, std::vector<SurfaceApplicationWindow> windows,
                            SurfaceApplicationOptions application) {
    const std::shared_ptr<RunOpenState> open_state = gui_forms::detail::make_library_shared<RunOpenState>();
    try {
        // The run holds the bundled font set, so windows opened and closed
        // during it share one copy of each face.
        const std::filesystem::path font_directory = host.fonts_directory();
        if (font_directory.empty()) throw std::runtime_error(gui_forms::bundled_font_refusal());
        const std::shared_ptr<gui_forms::detail::BundledFontSet> run_fonts =
            gui_forms::detail::shared_bundled_font_set(font_directory, host.sealed_pages());
        // The run's Splash baker, after the fonts it paints with and before
        // every window, so it outlives each tooltip that reads it.
        const gui_forms::render::SkiaSplashBakeRun splash_baking(run_fonts);
        Runtime runtime(host, run_fonts);
        runtime.windowing = std::make_unique<SurfaceWindowing>(runtime);
        const WindowingEndGuard end_guard{open_state.get(), &application, runtime.windowing.get()};
        (*open_state).windowing = runtime.windowing.get();
        if (application.windowing_ready) application.windowing_ready(*runtime.windowing, RunOpen{open_state});
        // Owners first; a cycle or an unknown owner leaves windows unopened.
        std::vector<bool> opened(windows.size(), false);
        std::size_t remaining = windows.size();
        bool progress = true;
        while (remaining > 0U && progress) {
            progress = false;
            for (std::size_t index = 0U; index < windows.size(); ++index) {
                if (opened[index]) continue;
                if (!windows[index].owner_id.empty() &&
                    (*runtime.windowing).find(windows[index].owner_id).absent())
                    continue;
                const HostWindowAdmission admission = (*runtime.windowing).open(std::move(windows[index]), false);
                if (!admission.status.accepted()) return 2;
                opened[index] = true;
                --remaining;
                progress = true;
            }
        }
        if (remaining > 0U) return 2;
        (*runtime.windowing).mark_started();
        // The initial windows only: a ready callback may open a runtime
        // window, which open made ready itself and appended; reaching it
        // here would make it ready twice (A-gf-host-02-F11).
        const std::size_t initial_windows = runtime.windows.size();
        for (std::size_t index = 0U; index < initial_windows; ++index) (*runtime.windows[index]).ready();
        while (runtime.primary_open() && !runtime.quitting) {
            runtime.step();
            runtime.reclaim();
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GUI.Forms surface window manager: " << error.what() << '\n';
        return 1;
    }
}

} // namespace gui_forms::host::surface_detail
