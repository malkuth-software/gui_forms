// The browser host's program-thread loop (web_host_internal.hpp): the run,
// its windows as desk layers, input from the page's records and frames handed
// to the page for presentation.
#include "web_host_internal.hpp"
#include "../fonts/sealed_pages_web.hpp"
#include "../../offscreen_font_lookup.hpp"
#include "../../../core/failure/fail_fast.hpp"
#include "../../../core/damage/device_damage/device_damage.hpp"
#include "../../../core/resources/fonts/bundled_font_set.hpp"
#include "../../../render/skia/raster/skia_splash_bake.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/text/types/text_types.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#if defined(__EMSCRIPTEN__)
#include <emscripten/threading.h>
#endif

#if defined(GUI_FORMS_PREPARED_TEXT)
#error "The browser host presents the Skia raster; the prepared-text raster is not supported here."
#endif

namespace gui_forms::host::web_detail {
namespace {

// The frame's colours (0xRRGGBBAA): the border quad, and the title strip's
// background and text. Fixed until the theme engine reaches host chrome.
constexpr std::uint32_t border_active = 0x3F3F46FFU;
constexpr std::uint32_t border_inactive = 0x2D2D30FFU;
constexpr Color title_active{45, 45, 48, 255};
constexpr Color title_inactive{60, 60, 64, 255};
constexpr Color title_text{240, 240, 240, 255};

// The present record shared with the page: static storage, so it outlives
// the run and the page may read it until the document goes away.
WebPresentFrame present_record{};

std::uint64_t now_ns() {
    const std::chrono::steady_clock::duration since = std::chrono::steady_clock::now().time_since_epoch();
    const std::uint64_t nanoseconds =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(since).count());
    return nanoseconds;
}

// The schedule-wake seam (SurfaceRoot::set_schedule_wake_handler) arrives
// with the macOS modal-wake change; until this branch is rebased onto it the
// handler is installed only where the model has it. Remove the test after
// the rebase.
template <typename Model>
void install_schedule_wake(Model& model, bool installed) {
    if constexpr (requires(Model& candidate) { candidate.set_schedule_wake_handler(std::function<void()>{}); }) {
        if (installed) {
            model.set_schedule_wake_handler(Wake{});
        } else {
            model.set_schedule_wake_handler(std::function<void()>{});
        }
    } else {
        static_cast<void>(model);
        static_cast<void>(installed);
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

// The cursor a frame part shows.
CursorKind frame_part_cursor(WebDeskPart part) noexcept {
    switch (part) {
    case WebDeskPart::edge_left:
    case WebDeskPart::edge_right:
        return CursorKind::resize_horizontal;
    case WebDeskPart::edge_top:
    case WebDeskPart::edge_bottom:
        return CursorKind::resize_vertical;
    case WebDeskPart::corner_top_left:
    case WebDeskPart::corner_bottom_right:
        return CursorKind::resize_diagonal_down;
    case WebDeskPart::corner_top_right:
    case WebDeskPart::corner_bottom_left:
        return CursorKind::resize_diagonal_up;
    default:
        return CursorKind::arrow;
    }
}

bool frame_part_moves(WebDeskPart part) noexcept {
    const bool moves = part != WebDeskPart::none && part != WebDeskPart::client && part != WebDeskPart::close_box;
    return moves;
}

struct Closing final {
    NativeWindow* window;
    void operator()(HostCloseRequest& request) const {
        if ((*window).entry.options.close_request) (*window).entry.options.close_request(request);
    }
};
struct Dialog final {
    NativeWindow* window;
    HostDialogResult operator()(const HostDialogRequest& request) const {
        const HostDialogResult result = (*(*window).services).show_dialog(request);
        return result;
    }
};
struct ReadText final {
    NativeWindow* window;
    HostClipboardTextResult operator()() const {
        const HostClipboardTextResult result = (*(*window).services).read_clipboard_text();
        return result;
    }
};
struct WriteText final {
    NativeWindow* window;
    HostServiceStatus operator()(std::string_view text) const {
        const HostServiceStatus status = (*(*window).services).write_clipboard_text(text);
        return status;
    }
};

// The primary window's title is the document's; another window's is its
// frame's.
struct WindowTitle final {
    std::weak_ptr<NativeWindow> owner{};
    const std::thread::id thread{std::this_thread::get_id()};
    HostServiceStatus operator()(const std::string_view text) const {
        if (thread != std::this_thread::get_id()) return {HostServiceError::wrong_thread};
        const std::shared_ptr<NativeWindow> window = owner.lock();
        if (!window || (*window).closed) return {HostServiceError::after_shutdown};
        try {
            if (text.size() > 65536U || text.find('\0') != std::string_view::npos || !validate_utf8(text).valid())
                return {HostServiceError::invalid_argument};
            const std::string title(text);
            NativeWindow& native = *window;
            native.entry.options.title = title;
            if (native.chrome) (*native.chrome).set_title(title);
            if (native.entry.owner_id.empty() && !native.entry.tool_window && !native.entry.popup.has_value())
                gf_web_js_set_title(title.c_str());
            return {};
        } catch (...) {
            return {HostServiceError::backend_failure};
        }
    }
};

// A framed window's client size is changed in the canvas; the primary
// window's is the canvas's, decided by the page.
struct WindowClientSize final {
    std::weak_ptr<NativeWindow> owner{};
    const std::thread::id thread{std::this_thread::get_id()};
    HostServiceStatus operator()(const Size client_size) const {
        if (thread != std::this_thread::get_id()) return {HostServiceError::wrong_thread};
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
        return {};
    }
};

// A show, hide, close or full-screen request from any thread: run at once
// on the program's thread, else queued for its next turn.
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

void Wake::operator()() const noexcept { page_queue().wake(); }

void ActionQueue::post(Action action) {
    {
        const std::lock_guard<std::mutex> guard(mutex);
        pending.push_back(std::move(action));
    }
    page_queue().wake();
}

std::filesystem::path fonts_directory() {
    const char* override_path = std::getenv("GUI_FORMS_FONT_DIR");
    std::filesystem::path directory =
        override_path != nullptr && override_path[0] != '\0' ? std::filesystem::path(override_path)
                                                             : std::filesystem::path("/gui_forms/fonts");
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) directory.clear();
    return directory;
}

void register_fonts(render::SkiaRaster& raster) {
    const std::filesystem::path directory = fonts_directory();
    if (directory.empty()) throw std::runtime_error(gui_forms::bundled_font_refusal());
    const std::shared_ptr<gui_forms::detail::BundledFontSet> set =
        gui_forms::detail::shared_bundled_font_set(directory, gui_forms::host::web_sealed_pages());
    if (!raster.register_bundled_fonts(set))
        throw std::runtime_error("Cannot load the bundled GUI.Forms fonts from " + directory.string());
}

// ---------------------------------------------------------------------------
// FrameChrome

FrameChrome::FrameChrome(Runtime& runtime, std::string_view title, double width, double scale)
    : size_{std::max(1.0, width), WebDesk::title_bar_height - WebDesk::border_width}, scale_(scale) {
    root_ = make_control<Panel>(StableId("host.frame.root"));
    (*root_).set_background(title_inactive);
    title_ = make_control<Label>(StableId("host.frame.title"), std::string(title));
    (*title_).font = FontSpec{FontRole::control, 13, 400, false};
    (*title_).fore_color = title_text;
    (*title_).vertical_alignment = VerticalAlignment::center;
    (*root_).add_child(title_);
    close_ = make_control<Label>(StableId("host.frame.close"), std::string("\xC3\x97"));
    (*close_).font = FontSpec{FontRole::control, 16, 400, false};
    (*close_).fore_color = title_text;
    (*close_).alignment = HorizontalAlignment::center;
    (*close_).vertical_alignment = VerticalAlignment::center;
    (*root_).add_child(close_);
    register_fonts(raster_);
    model_ = std::make_unique<Window>(root_, size_);
    (*model_).set_text_metrics_provider(&raster_);
    (*model_).set_framebuffer_painter(&raster_);
    session_ = std::make_unique<HostSession>(*model_, web_capabilities(), nullptr);
    if (!dispatch(HostAttachEvent{size_, scale_}).accepted()) throw std::runtime_error("Web frame attach rejected");
    resize(width, scale);
    surface_ = runtime.allocate_surface();
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

void FrameChrome::set_title(std::string_view title) { (*title_).text = std::string(title); }

void FrameChrome::set_active(bool active) {
    if (active == active_) return;
    active_ = active;
    (*root_).set_background(active ? title_active : title_inactive);
}

void FrameChrome::resize(double width, double scale) {
    const Size next{std::max(1.0, width), WebDesk::title_bar_height - WebDesk::border_width};
    const double close_width = std::min(WebDesk::close_box_width, next.width);
    (*title_).set_bounds({10.0, 0.0, std::max(0.0, next.width - close_width - 10.0), next.height});
    (*close_).set_bounds({next.width - close_width, 0.0, close_width, next.height});
    if (scale != scale_) {
        scale_ = scale;
        static_cast<void>(dispatch(HostScaleEvent{scale_}));
    }
    if (next != size_) {
        size_ = next;
        static_cast<void>(dispatch(HostResizeEvent{size_}));
    }
    damage_.add({0.0, 0.0, size_.width, size_.height});
}

bool FrameChrome::paint(std::vector<WebDeviceRect>& uploaded, bool& resized) {
    uploaded.clear();
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
    const std::optional<PaintReceipt> receipt = (*model_).paint(raster_, damage_.rectangles());
    raster_.end_frame();
    if (!receipt) return false;
    std::array<WebDeviceRect, WebPresentFrame::maximum_rects_per_surface> device{};
    const std::size_t count =
        web_device_rects(damage_.rectangles(), scale_, raster_.pixel_width(), raster_.pixel_height(), device);
    PresentedArea area;
    for (std::size_t index = 0U; index < count; ++index) {
        uploaded.push_back(device[index]);
        const std::uint64_t columns = static_cast<std::uint64_t>(device[index].right - device[index].left);
        const std::uint64_t rows = static_cast<std::uint64_t>(device[index].bottom - device[index].top);
        area.device_pixels += columns * rows;
    }
    area.rectangles = count;
    static_cast<void>((*model_).notify_presented(*receipt, 0U, area));
    damage_.clear();
    resized = raster_resized_;
    raster_resized_ = false;
    return true;
}

// ---------------------------------------------------------------------------
// NativeWindow

NativeWindow::NativeWindow(Runtime& host, WebApplicationWindow source, HostWindowId id, Rect area, bool kept)
    : runtime(host), entry(std::move(source)), window_id(id), initial(kept), content(area), scale(host.scale) {
    register_fonts(raster);
    (*entry.model).set_text_metrics_provider(&raster);
    (*entry.model).set_framebuffer_painter(&raster);
    (*entry.model).metrics().set_renderer("Skia CPU / HarfBuzz / FreeType / WebGL2", true);
    services = std::make_unique<Services>(*this);
    // The availability record (stage DP): the page uploads each damage
    // rectangle.
    PresentationDeclaration presentation;
    presentation.contract_version = paint_contract_version;
    presentation.extensions.partial_present = true;
    (*entry.model).declare_presentation(presentation);
    session = std::make_unique<HostSession>(*entry.model, web_capabilities(), services.get());
    closing_token = (*session).closing().subscribe(Closing{this});
    surface = runtime.allocate_surface();
    if (runtime.desk.framed(window_id)) {
        const std::optional<Rect> outer = runtime.desk.outer(window_id);
        const double width = outer.has_value() ? (*outer).width - 2.0 * WebDesk::border_width : content.width;
        chrome = std::make_unique<FrameChrome>(runtime, entry.options.title, width, scale);
    }
}

NativeWindow::~NativeWindow() {
    (*entry.model).set_dispatch_wake_handler({});
    (*entry.model).set_live_surface_idle_wake_handler({});
    (*entry.model).set_paint_wake_handler({});
    install_schedule_wake(*entry.model, false);
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
    (*entry.model).set_dispatch_wake_handler(Wake{});
    (*entry.model).set_paint_wake_handler(Wake{});
    (*entry.model).set_live_surface_idle_wake_handler(Wake{});
    install_schedule_wake(*entry.model, true);
    // The raster keeps its pixels between frames, so a scroll's pixels move
    // instead of repainting.
    (*entry.model).set_scroll_copies_supported(true);
    if (!dispatch(HostAttachEvent{Size{content.width, content.height}, scale}).accepted())
        throw std::runtime_error("Web host attach rejected");
    static_cast<void>(dispatch(HostCompositorEvent{web_compositor_state()}));
    if (entry.options.initially_visible) set_visible(true);
}

void NativeWindow::ready() {
    const WindowAction show{weak_from_this(), runtime.actions, ActionKind::show};
    const WindowAction hide{weak_from_this(), runtime.actions, ActionKind::hide};
    const WindowAction close_window{weak_from_this(), runtime.actions, ActionKind::close};
    if (entry.options.host_ready)
        entry.options.host_ready(Wake{}, close_window, Dialog{this}, ReadText{this}, WriteText{this});
    if (entry.options.full_screen_ready)
        entry.options.full_screen_ready(WindowAction{weak_from_this(), runtime.actions, ActionKind::fullscreen});
    if (entry.options.title_ready) entry.options.title_ready(WindowTitle{weak_from_this()});
    if (entry.options.client_size_ready) entry.options.client_size_ready(WindowClientSize{weak_from_this()});
    if (entry.options.visibility_ready) entry.options.visibility_ready(show, hide);
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
        // Only the primary window fills the canvas the page can enlarge.
        if (!runtime.desk.framed(window_id) && !entry.popup.has_value()) gf_web_js_full_screen();
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
    install_schedule_wake(*entry.model, false);
    (*session).shutdown();
    runtime.desk.remove(window_id);
    runtime.scene_changed();
    if (runtime.pressed_window == window_id) runtime.pressed_window = {};
    if (runtime.pointer_window == window_id) runtime.pointer_window = {};
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
    runtime.scene_changed();
    if (runtime.windowing) (*runtime.windowing).report_native_frame(*this);
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
    static_cast<void>(dispatch(HostOcclusionEvent{!shown || runtime.page_hidden}));
    if (closed) return;
    runtime.scene_changed();
    if (shown) {
        damage.add({0.0, 0.0, content.width, content.height});
        const bool takes_focus = !entry.popup.has_value() || (*entry.popup).focus == HostPopupFocus::takes_focus;
        if (takes_focus) runtime.activate(window_id, true);
    } else if (runtime.desk.active() == window_id || active) {
        runtime.activate_topmost();
    }
    if (runtime.windowing) (*runtime.windowing).report_native_frame(*this);
}

void NativeWindow::set_active(bool next) {
    if (closed) return;
    const bool keyed = next && runtime.page_focused;
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

bool NativeWindow::has_present_work() const noexcept {
    const bool work = !uploaded.empty() || !chrome_uploaded.empty() || raster_resized || chrome_resized;
    return work;
}

bool NativeWindow::wants_frames() const noexcept {
    const bool wanted = !closed && (*entry.model).has_live_surface_presentations() && !live_idle;
    return wanted;
}

void NativeWindow::update(bool may_paint) {
    if (closed) return;
    if (entry.options.dispatch_pending) entry.options.dispatch_pending();
    if (closed) return;
    static_cast<void>((*entry.model).drain_posted_work());
    static_cast<void>((*entry.model).poll_frame_schedule(std::chrono::steady_clock::now()));
    if (closed) return;
    // While the page holds a frame, every raster stays as it is; the model
    // keeps its damage and its live frames until the next turn.
    if (!may_paint) return;
    if (chrome && visible) {
        std::vector<WebDeviceRect> painted;
        bool resized = false;
        if ((*chrome).paint(painted, resized)) {
            chrome_uploaded = std::move(painted);
            chrome_resized = chrome_resized || resized;
        }
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
    for (std::size_t index = 0U; index < pending_copies.size(); ++index) screen.add(pending_copies[index].clip);
    if (!visible || screen.empty()) {
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
    bool copies_applied = true;
    for (std::size_t index = 0U; index < pending_copies.size(); ++index) {
        const ScrollCopy& copy = pending_copies[index];
        if (copies_applied) copies_applied = raster.scroll_copy(copy.clip, copy.offset);
        applied_copy_clips.push_back(copy.clip);
    }
    pending_copies.clear();
    if (!copies_applied) {
        for (std::size_t index = 0U; index < applied_copy_clips.size(); ++index) damage.add(applied_copy_clips[index]);
    }
    compose_live_frames();
    std::optional<PaintReceipt> receipt{};
    if (!damage.empty()) {
        receipt = (*entry.model).paint(raster, damage.rectangles());
        const std::span<const Rect> painted = damage.rectangles();
        for (std::size_t index = 0U; index < painted.size(); ++index) screen.add(painted[index]);
    }
    raster.end_frame();
    live_frames.clear();
    if (!damage.empty() && !receipt) {
        for (std::size_t index = 0U; index < applied_copy_clips.size(); ++index) damage.add(applied_copy_clips[index]);
        return;
    }
    // The rectangles the page uploads: the screen's, on whole device pixels.
    std::array<WebDeviceRect, WebPresentFrame::maximum_rects_per_surface> device{};
    const std::size_t count =
        web_device_rects(screen.rectangles(), scale, raster.pixel_width(), raster.pixel_height(), device);
    PresentedArea copied;
    for (std::size_t index = 0U; index < count; ++index) {
        uploaded.push_back(device[index]);
        const std::uint64_t columns = static_cast<std::uint64_t>(device[index].right - device[index].left);
        const std::uint64_t rows = static_cast<std::uint64_t>(device[index].bottom - device[index].top);
        copied.device_pixels += columns * rows;
    }
    copied.rectangles = count;
    const std::uint64_t elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
    if (regions_taken) live_region_lost = false;
    // Reported at hand-over, as the X11 host reports after XPutImage: the
    // frame goes to the page in this turn and nothing paints over it before
    // the page has presented it.
    const bool acknowledged = report_presented(receipt, elapsed, copied);
    if (!receipt) return;
    const PaintLeaseSnapshot lease = (*entry.model).paint_lease_snapshot();
    const bool shown = acknowledged || (lease.surface_epoch == (*receipt).surface_epoch &&
                                        lease.presented_revision == (*receipt).rendered_revision);
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
            continue;
        }
        live_regions[index] = {};
        damage.add(region);
    }
}

bool NativeWindow::report_presented(const std::optional<PaintReceipt>& receipt, std::uint64_t elapsed,
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
    if (receipt) {
        PresentedArea retained;
        retained.device_pixels = copied.device_pixels - live_pixels;
        retained.rectangles = copied.rectangles;
        retained.copy_nanoseconds = copied.copy_nanoseconds;
        live_elapsed = copied.device_pixels == 0U
                           ? 0U
                           : static_cast<std::uint64_t>(static_cast<double>(elapsed) *
                                                        static_cast<double>(live_pixels) /
                                                        static_cast<double>(copied.device_pixels));
        acknowledged = (*entry.model).notify_presented(*receipt, elapsed - live_elapsed, retained);
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

Runtime::Runtime() : queue(page_queue()), actions(std::make_shared<ActionQueue>()), frame(&present_record) {
    double width = 0.0;
    double height = 0.0;
    double ratio = 1.0;
    gf_web_js_attach(frame, &width, &height, &ratio);
    extent = Size{std::max(1.0, width), std::max(1.0, height)};
    scale = std::isfinite(ratio) && ratio > 0.0 ? ratio : 1.0;
    desk.set_extent(extent);
}

Runtime::~Runtime() {
    // A copy: a closed callback may open a replacement window.
    const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
    for (std::size_t index = snapshot.size(); index > 0U; --index)
        (*snapshot[index - 1U]).close(HostCloseReason::application, true);
    // The page may still hold the last frame: wait for it before the rasters
    // go, so it never reads freed pixels. The page always answers a frame
    // (web_host.js presents even while the document is hidden).
    while ((*frame).pending.load(std::memory_order_acquire) != 0U) {
        static_cast<void>(queue.take(records_, std::chrono::steady_clock::now() + std::chrono::milliseconds(50)));
        records_.clear();
    }
    windows.clear();
}

std::shared_ptr<NativeWindow> Runtime::add(WebApplicationWindow entry, HostWindowId window_id, Rect content,
                                           bool initial) {
    const std::shared_ptr<NativeWindow> window =
        std::make_shared<NativeWindow>(*this, std::move(entry), window_id, content, initial);
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
        if (!candidate.closed && candidate.initial && candidate.entry.owner_id.empty() &&
            !candidate.entry.tool_window && !candidate.entry.popup.has_value())
            open = true;
    }
    return open;
}

NativeWindow* Runtime::keyboard_window() noexcept {
    NativeWindow* found = window(desk.active());
    if (found != nullptr && desk.blocked((*found).window_id)) found = nullptr;
    return found;
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
    NativeWindow* lost = window(before);
    if (lost != nullptr) (*lost).set_active(false);
    NativeWindow* gained = window(after);
    if (gained != nullptr) (*gained).set_active(true);
}

void Runtime::activate_topmost() {
    const HostWindowId before = desk.active();
    const HostWindowId after = desk.activate_topmost();
    scene_dirty_ = true;
    if (before == after) return;
    NativeWindow* lost = window(before);
    if (lost != nullptr) (*lost).set_active(false);
    NativeWindow* gained = window(after);
    if (gained != nullptr) (*gained).set_active(true);
}

void Runtime::release_surface(std::uint32_t surface) {
    if (surface == 0U) return;
    released_.push_back(surface);
}

void Runtime::reclaim() {
    // The page reads rasters until it has presented the frame naming them.
    if ((*frame).pending.load(std::memory_order_acquire) != 0U) return;
    for (std::size_t index = windows.size(); index > 0U; --index) {
        const NativeWindow& candidate = *windows[index - 1U];
        if (candidate.initial || !candidate.closed) continue;
        release_surface(candidate.surface);
        if (candidate.chrome) release_surface((*candidate.chrome).surface());
        windows.erase(windows.begin() + static_cast<std::ptrdiff_t>(index - 1U));
        scene_dirty_ = true;
    }
}

std::optional<std::chrono::steady_clock::time_point> Runtime::next_deadline(bool& immediate) {
    immediate = scene_dirty_ && (*frame).pending.load(std::memory_order_acquire) == 0U;
    std::optional<std::chrono::steady_clock::time_point> deadline{};
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        const NativeWindow& candidate = *windows[index];
        if (candidate.closed) continue;
        const std::optional<FrameTime> wake = (*candidate.entry.model).next_wake();
        if (!wake) continue;
        if (!deadline.has_value() || *wake < *deadline) deadline = *wake;
    }
    return deadline;
}

PendingDialog Runtime::wait_for_dialog(std::uint64_t request) {
    PendingDialog pending{};
    pending.request = request;
    dialogs_.push_back(&pending);
    while (!pending.done && !quitting && primary_open()) step();
    for (std::size_t index = dialogs_.size(); index > 0U; --index) {
        if (dialogs_[index - 1U] == &pending) {
            dialogs_.erase(dialogs_.begin() + static_cast<std::ptrdiff_t>(index - 1U));
            break;
        }
    }
    if (!pending.done) pending.outcome = PageDialogOutcome::cancelled;
    return pending;
}

void Runtime::step() {
    bool immediate = false;
    const std::optional<std::chrono::steady_clock::time_point> deadline = next_deadline(immediate);
    std::optional<std::chrono::steady_clock::time_point> wait = deadline;
    if (immediate) wait = std::chrono::steady_clock::now();
    static_cast<void>(queue.take(records_, wait));
    // The queued show, hide, close and full-screen requests.
    std::deque<Action> queued;
    {
        const std::lock_guard<std::mutex> guard((*actions).mutex);
        queued.swap((*actions).pending);
    }
    for (std::size_t index = 0U; index < queued.size(); ++index) {
        const std::shared_ptr<NativeWindow> target = queued[index].window.lock();
        if (target) (*target).action(queued[index].kind);
    }
    // One record is the native callback of this loop: application code runs
    // under it, so it goes through the fail-fast boundary. Taken records
    // are moved out first, since a nested turn (a modal loop) takes again.
    std::deque<PageRecord> taken;
    taken.swap(records_);
    for (std::size_t index = 0U; index < taken.size(); ++index)
        gui_forms::detail::call_failing_fast("web.page_record", &Runtime::handle, *this, taken[index]);
    taken.clear();
    if (records_.empty()) records_.swap(taken);
    const bool may_paint = (*frame).pending.load(std::memory_order_acquire) == 0U;
    const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
    bool wanted = false;
    for (std::size_t index = 0U; index < snapshot.size(); ++index) {
        NativeWindow& candidate = *snapshot[index];
        candidate.update(may_paint);
        if (candidate.wants_frames()) wanted = true;
    }
    if (wanted != frames_wanted_) {
        frames_wanted_ = wanted;
        gf_web_js_want_frames(wanted ? 1 : 0);
    }
    if (may_paint) present();
    place_input();
}

void Runtime::present() {
    bool work = scene_dirty_ || !released_.empty();
    for (std::size_t index = 0U; index < windows.size() && !work; ++index) {
        if ((*windows[index]).has_present_work()) work = true;
    }
    if (!work) return;
    WebPresentFrame& record = *frame;
    record.canvas_width = device_extent(extent.width, scale);
    record.canvas_height = device_extent(extent.height, scale);
    record.surface_count = 0U;
    record.layer_count = 0U;
    record.rect_count = 0U;
    record.released_count = 0U;
    desk.scene(quads_);
    for (std::size_t index = 0U; index < quads_.size(); ++index) {
        const WebDeskQuad& quad = quads_[index];
        NativeWindow* found = window(quad.window);
        if (found == nullptr || (*found).raster.pixels() == nullptr) continue;
        NativeWindow& target = *found;
        if (record.surface_count + 2U > WebPresentFrame::maximum_surfaces ||
            record.layer_count + 3U > WebPresentFrame::maximum_layers)
            break;
        if (quad.framed) {
            WebPresentLayer& border = record.layers[record.layer_count];
            border = WebPresentLayer{};
            border.kind = WebPresentLayerKind::solid;
            border.color = quad.active ? border_active : border_inactive;
            border.x = static_cast<float>(std::round(quad.outer.x * scale));
            border.y = static_cast<float>(std::round(quad.outer.y * scale));
            border.width = static_cast<float>(std::round(quad.outer.width * scale));
            border.height = static_cast<float>(std::round(quad.outer.height * scale));
            ++record.layer_count;
            if (target.chrome && (*target.chrome).raster().pixels() != nullptr) {
                const render::SkiaRaster& strip = (*target.chrome).raster();
                WebPresentSurface& surface = record.surfaces[record.surface_count];
                surface = WebPresentSurface{};
                surface.id = (*target.chrome).surface();
                surface.pixels = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(strip.pixels()));
                surface.row_bytes = static_cast<std::uint32_t>(strip.row_bytes());
                surface.pixel_width = strip.pixel_width();
                surface.pixel_height = strip.pixel_height();
                surface.first_rect = record.rect_count;
                const bool whole = target.chrome_resized ||
                                   record.rect_count + target.chrome_uploaded.size() > WebPresentFrame::maximum_rects;
                if (whole) {
                    if (record.rect_count < WebPresentFrame::maximum_rects) {
                        const std::size_t at = static_cast<std::size_t>(record.rect_count) * 4U;
                        record.rects[at] = 0;
                        record.rects[at + 1U] = 0;
                        record.rects[at + 2U] = static_cast<std::int32_t>(surface.pixel_width);
                        record.rects[at + 3U] = static_cast<std::int32_t>(surface.pixel_height);
                        ++record.rect_count;
                        surface.rect_count = 1U;
                    }
                } else {
                    for (std::size_t rect = 0U; rect < target.chrome_uploaded.size(); ++rect) {
                        const std::size_t at = static_cast<std::size_t>(record.rect_count) * 4U;
                        record.rects[at] = target.chrome_uploaded[rect].left;
                        record.rects[at + 1U] = target.chrome_uploaded[rect].top;
                        record.rects[at + 2U] = target.chrome_uploaded[rect].right;
                        record.rects[at + 3U] = target.chrome_uploaded[rect].bottom;
                        ++record.rect_count;
                    }
                    surface.rect_count = static_cast<std::uint32_t>(target.chrome_uploaded.size());
                }
                ++record.surface_count;
                WebPresentLayer& title = record.layers[record.layer_count];
                title = WebPresentLayer{};
                title.kind = WebPresentLayerKind::surface;
                title.surface = surface.id;
                title.x = static_cast<float>(std::round(quad.content.x * scale));
                title.y = static_cast<float>(std::round((quad.outer.y + WebDesk::border_width) * scale));
                title.width = static_cast<float>(surface.pixel_width);
                title.height = static_cast<float>(surface.pixel_height);
                ++record.layer_count;
            }
        }
        WebPresentSurface& surface = record.surfaces[record.surface_count];
        surface = WebPresentSurface{};
        surface.id = target.surface;
        surface.pixels = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target.raster.pixels()));
        surface.row_bytes = static_cast<std::uint32_t>(target.raster.row_bytes());
        surface.pixel_width = target.raster.pixel_width();
        surface.pixel_height = target.raster.pixel_height();
        surface.alpha = target.entry.popup.has_value() || target.present_alpha ? 1U : 0U;
        surface.first_rect = record.rect_count;
        const bool whole =
            target.raster_resized || record.rect_count + target.uploaded.size() > WebPresentFrame::maximum_rects;
        if (whole) {
            if (record.rect_count < WebPresentFrame::maximum_rects) {
                const std::size_t at = static_cast<std::size_t>(record.rect_count) * 4U;
                record.rects[at] = 0;
                record.rects[at + 1U] = 0;
                record.rects[at + 2U] = static_cast<std::int32_t>(surface.pixel_width);
                record.rects[at + 3U] = static_cast<std::int32_t>(surface.pixel_height);
                ++record.rect_count;
                surface.rect_count = 1U;
            }
        } else {
            for (std::size_t rect = 0U; rect < target.uploaded.size(); ++rect) {
                const std::size_t at = static_cast<std::size_t>(record.rect_count) * 4U;
                record.rects[at] = target.uploaded[rect].left;
                record.rects[at + 1U] = target.uploaded[rect].top;
                record.rects[at + 2U] = target.uploaded[rect].right;
                record.rects[at + 3U] = target.uploaded[rect].bottom;
                ++record.rect_count;
            }
            surface.rect_count = static_cast<std::uint32_t>(target.uploaded.size());
        }
        ++record.surface_count;
        WebPresentLayer& client = record.layers[record.layer_count];
        client = WebPresentLayer{};
        client.kind = WebPresentLayerKind::surface;
        client.surface = surface.id;
        client.x = static_cast<float>(std::round(quad.content.x * scale));
        client.y = static_cast<float>(std::round(quad.content.y * scale));
        client.width = static_cast<float>(surface.pixel_width);
        client.height = static_cast<float>(surface.pixel_height);
        ++record.layer_count;
    }
    // Surfaces released beyond the record's room wait for the next frame.
    std::size_t released = 0U;
    for (; released < released_.size() && released < WebPresentFrame::maximum_released; ++released)
        record.released[released] = released_[released];
    record.released_count = static_cast<std::uint32_t>(released);
    released_.erase(released_.begin(), released_.begin() + static_cast<std::ptrdiff_t>(released));
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        NativeWindow& candidate = *windows[index];
        candidate.uploaded.clear();
        candidate.chrome_uploaded.clear();
        candidate.raster_resized = false;
        candidate.chrome_resized = false;
    }
    scene_dirty_ = !released_.empty();
    presented_once_ = true;
    record.pending.store(1U, std::memory_order_release);
    gf_web_js_request_present();
}

void Runtime::place_input() {
    NativeWindow* target = keyboard_window();
    std::optional<Rect> caret{};
    if (target != nullptr && !(*target).closed) {
        const std::optional<Rect> local = (*(*target).entry.model).text_input_caret();
        if (local.has_value())
            caret = Rect{(*local).x + (*target).content.x, (*local).y + (*target).content.y, (*local).width,
                         (*local).height};
    }
    const bool editing = caret.has_value();
    if (caret == placed_caret_ && editing == placed_editing_) return;
    placed_caret_ = caret;
    placed_editing_ = editing;
    const Rect box = caret.has_value() ? *caret : Rect{};
    gf_web_js_place_input(box.x, box.y, box.width, box.height, editing ? 1 : 0);
}

WebDeskHit Runtime::pointer_target(Point point) const noexcept {
    WebDeskHit hit = desk.hit(point);
    if (!pressed_window.valid()) return hit;
    const std::optional<Rect> content = desk.content(pressed_window);
    if (!content.has_value()) return hit;
    hit.window = pressed_window;
    hit.part = WebDeskPart::client;
    hit.local = Point{point.x - (*content).x, point.y - (*content).y};
    return hit;
}

void Runtime::handle(PageRecord& record) {
    switch (record.kind) {
    case PageRecordKind::pointer:
        handle_pointer(record);
        break;
    case PageRecordKind::wheel:
        handle_wheel(record);
        break;
    case PageRecordKind::key:
        handle_key(record);
        break;
    case PageRecordKind::text:
        handle_text(record, false);
        break;
    case PageRecordKind::composition:
        handle_text(record, true);
        break;
    case PageRecordKind::clipboard:
        clipboard_text = std::move(record.text);
        ++clipboard_generation;
        break;
    case PageRecordKind::resize:
        handle_resize(record);
        break;
    case PageRecordKind::focus: {
        page_focused = record.down;
        NativeWindow* active_window = window(desk.active());
        if (active_window != nullptr) (*active_window).set_active(true);
        if (windowing) (*windowing).report_application_active(page_focused);
        break;
    }
    case PageRecordKind::visibility: {
        page_hidden = record.down;
        const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
        for (std::size_t index = 0U; index < snapshot.size(); ++index) {
            NativeWindow& candidate = *snapshot[index];
            if (candidate.closed || !candidate.visible) continue;
            static_cast<void>(candidate.dispatch(HostOcclusionEvent{page_hidden}));
        }
        break;
    }
    case PageRecordKind::presented:
    case PageRecordKind::frame:
        // The turn's update follows: painting resumes, live surfaces sample.
        break;
    case PageRecordKind::dialog:
        for (std::size_t index = 0U; index < dialogs_.size(); ++index) {
            PendingDialog& pending = *dialogs_[index];
            if (pending.request != record.request) continue;
            pending.done = true;
            pending.outcome = static_cast<PageDialogOutcome>(record.mode);
            pending.text = std::move(record.text);
            pending.paths = std::move(record.paths);
            break;
        }
        break;
    case PageRecordKind::drop:
        handle_drop(record);
        break;
    case PageRecordKind::fonts: {
        // A font group arrived: text that fell back may now find its face.
        const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
        for (std::size_t index = 0U; index < snapshot.size(); ++index) {
            NativeWindow& candidate = *snapshot[index];
            if (candidate.closed) continue;
            candidate.damage.add({0.0, 0.0, candidate.content.width, candidate.content.height});
        }
        break;
    }
    case PageRecordKind::close_page: {
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

void Runtime::handle_pointer(const PageRecord& record) {
    const Point point{record.x, record.y};
    pointer_position = point;
    const PagePointerAction action = static_cast<PagePointerAction>(record.action);
    // A frame drag owns the pointer until its release.
    if (desk.frame_dragging()) {
        if (action == PagePointerAction::move) {
            const HostWindowId dragged = desk.frame_drag_window();
            const std::optional<Rect> moved = desk.frame_drag_to(point);
            NativeWindow* target = window(dragged);
            if (moved.has_value() && target != nullptr) (*target).apply_content(*moved);
            return;
        }
        if (action == PagePointerAction::up || action == PagePointerAction::cancel) {
            desk.end_frame_drag();
            gf_web_js_pointer_capture(0, record.pointer);
        }
        return;
    }
    if (action == PagePointerAction::leave) {
        NativeWindow* left = pressed_window.valid() ? nullptr : window(pointer_window);
        if (left != nullptr) {
            PointerEvent event;
            event.action = PointerAction::leave;
            event.pointer_id = 1U;
            event.position = Point{point.x - (*left).content.x, point.y - (*left).content.y};
            event.modifiers = web_modifiers(record.modifiers);
            (*left).pointer(std::move(event));
            pointer_window = {};
        }
        return;
    }
    const WebDeskHit hit = pointer_target(point);
    if (action == PagePointerAction::down) {
        if (!hit.window.valid()) return;
        // A passive popup never takes the keyboard from its owner.
        const NativeWindow* pressed = window(hit.window);
        const bool passive = pressed != nullptr && (*pressed).entry.popup.has_value() &&
                             (*(*pressed).entry.popup).focus != HostPopupFocus::takes_focus;
        activate(hit.window, !passive);
        if (desk.blocked(hit.window)) return;
        if (frame_part_moves(hit.part)) {
            if (desk.begin_frame_drag(hit.window, hit.part, point)) gf_web_js_pointer_capture(1, record.pointer);
            return;
        }
        if (hit.part == WebDeskPart::close_box) {
            close_pressed = hit.window;
            return;
        }
    }
    if (action == PagePointerAction::up && close_pressed.valid()) {
        const HostWindowId pressed = close_pressed;
        close_pressed = {};
        NativeWindow* target = window(pressed);
        if (hit.window == pressed && hit.part == WebDeskPart::close_box && target != nullptr)
            (*target).close(HostCloseReason::user);
        return;
    }
    // Frame parts: their cursor, and the client window under the pointer
    // left.
    if (hit.part != WebDeskPart::client || desk.blocked(hit.window)) {
        NativeWindow* left = window(pointer_window);
        if (left != nullptr && !pressed_window.valid()) {
            PointerEvent event;
            event.action = PointerAction::leave;
            event.pointer_id = 1U;
            event.position = Point{point.x - (*left).content.x, point.y - (*left).content.y};
            (*left).pointer(std::move(event));
            pointer_window = {};
        }
        gf_web_js_set_cursor(static_cast<int>(frame_part_cursor(hit.part)), 0);
        return;
    }
    NativeWindow* target = window(hit.window);
    if (target == nullptr) return;
    if (pointer_window != hit.window) {
        NativeWindow* left = window(pointer_window);
        if (left != nullptr) {
            PointerEvent leave;
            leave.action = PointerAction::leave;
            leave.pointer_id = 1U;
            leave.position = Point{point.x - (*left).content.x, point.y - (*left).content.y};
            (*left).pointer(std::move(leave));
        }
        pointer_window = hit.window;
        PointerEvent enter;
        enter.action = PointerAction::enter;
        enter.pointer_id = 1U;
        enter.position = hit.local;
        enter.modifiers = web_modifiers(record.modifiers);
        (*target).pointer(std::move(enter));
        if ((*target).closed) return;
    }
    PointerEvent event;
    event.pointer_id = 1U;
    event.position = hit.local;
    event.modifiers = web_modifiers(record.modifiers);
    switch (action) {
    case PagePointerAction::down:
        event.action = PointerAction::down;
        event.button = web_pointer_button(record.button);
        static_cast<void>(next_web_click_count((*target).click_history, event.button, record.time, hit.local,
                                               default_web_double_click_milliseconds,
                                               default_web_double_click_distance));
        event.click_count = (*target).click_history.count;
        pressed_window = hit.window;
        break;
    case PagePointerAction::up:
    case PagePointerAction::cancel:
        event.action = PointerAction::up;
        event.button = web_pointer_button(record.button);
        event.click_count = (*target).click_history.count;
        pressed_window = {};
        break;
    case PagePointerAction::enter:
    case PagePointerAction::move:
        event.action = PointerAction::move;
        event.click_count = 0U;
        break;
    default:
        return;
    }
    (*target).pointer(std::move(event));
}

void Runtime::handle_wheel(const PageRecord& record) {
    const Point point{record.x, record.y};
    const WebDeskHit hit = desk.hit(point);
    if (!hit.window.valid() || hit.part != WebDeskPart::client || desk.blocked(hit.window)) return;
    NativeWindow* target = window(hit.window);
    if (target == nullptr) return;
    const WebWheel wheel =
        web_wheel(record.delta_x, record.delta_y, static_cast<WebWheelMode>(record.mode), (*target).content.height);
    PointerEvent event;
    event.action = PointerAction::wheel;
    event.pointer_id = 1U;
    event.position = hit.local;
    event.modifiers = web_modifiers(record.modifiers);
    event.wheel_delta = wheel.delta;
    event.wheel_unit = wheel.unit;
    event.click_count = 0U;
    (*target).pointer(std::move(event));
}

void Runtime::handle_key(const PageRecord& record) {
    NativeWindow* target = keyboard_window();
    if (target == nullptr) return;
    KeyEvent key;
    key.action = record.down ? KeyAction::down : KeyAction::up;
    key.physical_key = web_physical_key(record.text);
    key.modifiers = web_modifiers(record.modifiers);
    key.repeat = record.repeat;
    const HostDispatchResult result = (*target).dispatch(key);
    if ((*target).closed) return;
    const bool context_key = record.down && !result.handled && !key.repeat &&
                             web_requests_keyboard_context(key.physical_key, key.modifiers);
    if (context_key)
        static_cast<void>((*target).dispatch(HostContextRequestEvent{ContextRequestOrigin::keyboard, Point{}}));
}

void Runtime::handle_text(const PageRecord& record, bool composition) {
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

void Runtime::handle_resize(const PageRecord& record) {
    const Size next{std::max(1.0, record.x), std::max(1.0, record.y)};
    const double ratio = std::isfinite(record.scale) && record.scale > 0.0 ? record.scale : scale;
    const bool rescaled = ratio != scale;
    extent = next;
    scale = ratio;
    desk.set_extent(extent);
    scene_dirty_ = true;
    const std::vector<std::shared_ptr<NativeWindow>> snapshot = windows;
    for (std::size_t index = 0U; index < snapshot.size(); ++index) {
        NativeWindow& candidate = *snapshot[index];
        if (candidate.closed) continue;
        if (rescaled) candidate.apply_scale(scale);
        if (candidate.closed) continue;
        // The desk keeps every layer's title bar reachable; the primary
        // window fills the canvas.
        const std::optional<Rect> placed = desk.content(candidate.window_id);
        if (placed.has_value() && *placed != candidate.content) {
            candidate.apply_content(*placed);
        } else if (windowing) {
            (*windowing).report_native_frame(candidate);
        }
    }
}

void Runtime::handle_drop(const PageRecord& record) {
    const Point point{record.x, record.y};
    const WebDeskHit hit = desk.hit(point);
    if (!hit.window.valid() || hit.part != WebDeskPart::client || desk.blocked(hit.window)) return;
    NativeWindow* target = window(hit.window);
    if (target == nullptr || record.paths.empty()) return;
    DragFileListData files;
    for (std::size_t index = 0U; index < record.paths.size() && index < DragLimits::maximum_paths; ++index)
        files.paths_utf8.push_back(record.paths[index]);
    // The browser reads the files only after the drop, so the program sees
    // the whole gesture at once: an enter, then the drop or a leave.
    DragEvent drag;
    drag.session_id = ++drag_sessions_;
    drag.allowed_effects = DragEffect::copy;
    drag.items.push_back(std::move(files));
    drag.position = hit.local;
    drag.modifiers = web_modifiers(record.modifiers);
    drag.action = DragAction::enter;
    const bool accepted = (*target).dispatch(drag).drag_effect == DragEffect::copy;
    if ((*target).closed) return;
    drag.handled = false;
    drag.accepted_effect = DragEffect::none;
    drag.action = accepted ? DragAction::drop : DragAction::leave;
    static_cast<void>((*target).dispatch(drag));
}

// ---------------------------------------------------------------------------
// The run

HostWindowAdmission RunOpen::operator()(WebApplicationWindow entry) const {
    HostWindowAdmission refused;
    const std::shared_ptr<RunOpenState> state = owner.lock();
    if (!state || (*state).windowing == nullptr) {
        refused.status.error = HostServiceError::after_shutdown;
        return refused;
    }
    if ((*state).thread != std::this_thread::get_id()) {
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

CompositorState web_compositor_state() {
    // The page composites every layer with premultiplied alpha.
    // A window's layer is transparent when its backdrop is granted; the
    // materials are not offered.
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

} // namespace gui_forms::host::web_detail

namespace gui_forms::host {

std::filesystem::path offscreen_bundled_font_directory() {
    const std::filesystem::path directory = web_detail::fonts_directory();
    return directory;
}

// What a page offers, and what it does not: no accessibility publication
// yet, no frame capture, no sound cues, no clipboard images, no tray, no
// window backdrops beyond the layer's alpha, and no window state reports.
// The key map is the PC convention's in every browser (an open decision for
// Apple keyboards, which a page cannot tell apart reliably).
HostCapabilities web_capabilities() {
    return {HostCapabilities::current_protocol_version, "web-emscripten",
            HostCapability::lifecycle | HostCapability::scale_notifications | HostCapability::monitor_geometry |
                HostCapability::occlusion | HostCapability::scheduled_wake | HostCapability::pointer_input |
                HostCapability::keyboard_input | HostCapability::text_composition |
                HostCapability::pointer_capture | HostCapability::cursor | HostCapability::clipboard |
                HostCapability::dialogs | HostCapability::typed_drag_destination |
                HostCapability::runtime_windows | HostCapability::window_frames |
                HostCapability::screen_pointer | HostCapability::window_at_point |
                HostCapability::window_ordering | HostCapability::transparent_windows |
                HostCapability::compositor_notifications | HostCapability::transient_surfaces,
            pc_key_map()};
}

int run_web_application(std::vector<WebApplicationWindow> windows, WebApplicationOptions application) {
#if defined(__EMSCRIPTEN__)
    if (emscripten_is_main_browser_thread()) return 3;
#endif
    const std::shared_ptr<web_detail::RunOpenState> open_state = std::make_shared<web_detail::RunOpenState>();
    try {
        // The run holds the bundled font set, so windows opened and closed
        // during it share one copy of each face.
        const std::filesystem::path font_directory = web_detail::fonts_directory();
        if (font_directory.empty()) throw std::runtime_error(gui_forms::bundled_font_refusal());
        const std::shared_ptr<gui_forms::detail::BundledFontSet> run_fonts =
            gui_forms::detail::shared_bundled_font_set(font_directory, web_sealed_pages());
        // The run's Splash baker, after the fonts it paints with and before
        // every window, so it outlives each tooltip that reads it.
        const gui_forms::render::SkiaSplashBakeRun splash_baking(run_fonts);
        web_detail::Runtime runtime;
        runtime.windowing = std::make_unique<web_detail::WebWindowing>(runtime);
        const web_detail::WindowingEndGuard end_guard{open_state.get(), &application, runtime.windowing.get()};
        (*open_state).windowing = runtime.windowing.get();
        if (application.windowing_ready)
            application.windowing_ready(*runtime.windowing, web_detail::RunOpen{open_state});
        // Owners first; a cycle or an unknown owner leaves windows unopened.
        std::vector<bool> opened(windows.size(), false);
        std::size_t remaining = windows.size();
        bool progress = true;
        while (remaining > 0U && progress) {
            progress = false;
            for (std::size_t index = 0U; index < windows.size(); ++index) {
                if (opened[index]) continue;
                if (!windows[index].owner_id.empty() &&
                    !(*runtime.windowing).find(windows[index].owner_id).has_value())
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
        for (std::size_t index = 0U; index < runtime.windows.size(); ++index) (*runtime.windows[index]).ready();
        while (runtime.primary_open() && !runtime.quitting) {
            runtime.step();
            runtime.reclaim();
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GUI.Forms web host: " << error.what() << '\n';
        return 1;
    }
}

int run_web(std::unique_ptr<Window> window, WebHostOptions options) {
    std::vector<WebApplicationWindow> windows;
    WebApplicationWindow entry;
    entry.stable_id = "main";
    entry.model = std::move(window);
    entry.options = std::move(options);
    windows.push_back(std::move(entry));
    const int code = run_web_application(std::move(windows));
    return code;
}

} // namespace gui_forms::host
