// The single-surface window manager driven headless (owner decision,
// 2026-11-15): a test host keeps the surface in memory and posts the input
// records a browser or a framebuffer host would; real windows paint with
// the CPU Skia raster. Covers composing and damage limits, the drawn
// cursor, resizing by a frame grip, minimising, maximising and restoring
// by the title bar's boxes, the tab and a title bar double click, closing
// by the close box, focus order, modal blocking, and the window capsule's
// gestures as the manager carries them out: the three-click close, the
// left half's double click and downward drag, the right half's live move
// to a half of the surface and back to floating with its snap preview,
// the help call point and the title on hover, and the keyboard paths
// (src/host/surface/manager/surface_manager_internal.hpp).

#include "../src/host/surface/manager/surface_manager_internal.hpp"
#include "gui_forms/components/business_card/business_card.hpp"
#if defined(_WIN32)
#include "../src/host/windows/fonts/sealed_pages_windows.hpp"
#else
#include "../src/host/posix/fonts/sealed_pages_posix.hpp"
#endif

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;
using namespace gui_forms::host;
using namespace gui_forms::host::surface_detail;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// The surface in memory: every present is recorded and shown at once.
class TestHost final : public SurfaceHost {
public:
    [[nodiscard]] HostCapabilities capabilities() const override {
        return {HostCapabilities::current_protocol_version, "test-surface",
                HostCapability::lifecycle | HostCapability::scale_notifications | HostCapability::monitor_geometry |
                    HostCapability::occlusion | HostCapability::scheduled_wake | HostCapability::pointer_input |
                    HostCapability::keyboard_input | HostCapability::text_composition |
                    HostCapability::pointer_capture | HostCapability::cursor | HostCapability::clipboard |
                    HostCapability::dialogs | HostCapability::runtime_windows | HostCapability::window_frames |
                    HostCapability::screen_pointer | HostCapability::window_at_point |
                    HostCapability::window_ordering | HostCapability::transparent_windows |
                    HostCapability::compositor_notifications | HostCapability::transient_surfaces,
                pc_key_map()};
    }
    // As a framebuffer has none: the manager draws the cursor.
    [[nodiscard]] bool draws_cursor() const noexcept override { return false; }
    [[nodiscard]] std::string_view monitor_id() const noexcept override { return "test-surface"; }
    [[nodiscard]] std::filesystem::path fonts_directory() const override {
        return std::filesystem::path(GUI_FORMS_TEST_FONT_DIR);
    }
    [[nodiscard]] gui_forms::detail::SealedPageFunctions sealed_pages() const noexcept override {
#if defined(_WIN32)
        const gui_forms::detail::SealedPageFunctions pages = windows_sealed_pages();
#else
        const gui_forms::detail::SealedPageFunctions pages = posix_sealed_pages();
#endif
        return pages;
    }
    SurfaceHostStart start() override {
        SurfaceHostStart start{};
        start.extent = Size{800.0, 600.0};
        start.scale = 1.0;
        return start;
    }
    [[nodiscard]] SurfaceInputQueue& input() noexcept override { return queue; }
    SurfaceView acquire_surface(std::uint32_t width, std::uint32_t height) override {
        pixels.assign(static_cast<std::size_t>(width) * height * 4U, 0U);
        surface_width = width;
        SurfaceView view{};
        view.pixels = pixels.data();
        view.row_bytes = static_cast<std::size_t>(width) * 4U;
        view.width = width;
        view.height = height;
        ++acquired;
        return view;
    }
    SurfacePresentResult present(std::span<const SurfaceDeviceRect> damage) override {
        ++presents;
        last_damage.assign(damage.begin(), damage.end());
        return SurfacePresentResult::presented;
    }
    void want_frames(bool) override {}
    void set_title(std::string_view text) override { title.assign(text); }
    void set_cursor(CursorKind) override { ++host_cursor_calls; }
    void pointer_capture(bool) override {}
    void write_clipboard(std::string_view) override {}
    void place_text_input(Rect, bool) override {}
    void toggle_full_screen() override {}
    HostServiceStatus start_dialog(const HostDialogRequest&, std::uint64_t) override {
        return {HostServiceError::unsupported};
    }

    // The surface pixel at x, y as 0xRRGGBB.
    [[nodiscard]] std::uint32_t rgb(std::uint32_t x, std::uint32_t y) const {
        const std::size_t at = (static_cast<std::size_t>(y) * surface_width + x) * 4U;
        const std::uint32_t value = (static_cast<std::uint32_t>(pixels[at + 2U]) << 16U) |
                                    (static_cast<std::uint32_t>(pixels[at + 1U]) << 8U) | pixels[at];
        return value;
    }

    SurfaceInputQueue queue;
    std::vector<std::uint8_t> pixels;
    std::uint32_t surface_width{};
    std::vector<SurfaceDeviceRect> last_damage;
    std::uint64_t presents{};
    std::uint64_t acquired{};
    std::uint64_t host_cursor_calls{};
    std::string title;
};

struct CountClicks final {
    int* count;
    void operator()(ButtonBase&) const { ++(*count); }
};

struct MarkClosed final {
    bool* closed;
    void operator()() const { *closed = true; }
};

// A window whose client area is one button at (20, 20, 120, 30).
struct TestWindow final {
    std::shared_ptr<Button> button;
    SubscriptionToken token;
    int clicks{};
    bool closed{};
};

SurfaceApplicationWindow window_entry(TestWindow& state, std::string stable_id, std::string owner, Size size) {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId(stable_id + ".root"));
    state.button = make_control<Button>(StableId(stable_id + ".button"), std::string("Press"));
    (*state.button).set_bounds({20.0, 20.0, 120.0, 30.0});
    (*root).add_child(state.button);
    state.token = (*state.button).clicked().subscribe(CountClicks{&state.clicks});
    SurfaceApplicationWindow entry;
    entry.stable_id = std::move(stable_id);
    entry.owner_id = std::move(owner);
    entry.options.title = entry.stable_id;
    entry.options.initial_size = size;
    entry.options.minimum_size = Size{120.0, 80.0};
    entry.options.print_metrics_on_close = false;
    entry.options.closed = MarkClosed{&state.closed};
    entry.model = std::make_unique<Window>(root, size);
    return entry;
}

// One run on the test host: the primary window and the windowing, as
// run_surface_application sets them up.
struct Harness final {
    TestHost host;
    std::shared_ptr<gui_forms::detail::BundledFontSet> fonts;
    std::unique_ptr<Runtime> runtime;
    TestWindow primary;

    Harness() {
        fonts = gui_forms::detail::shared_bundled_font_set(host.fonts_directory(), host.sealed_pages());
        runtime = std::make_unique<Runtime>(host, fonts);
        (*runtime).windowing = std::make_unique<SurfaceWindowing>(*runtime);
        const HostWindowAdmission admission =
            (*(*runtime).windowing).open(window_entry(primary, "main", "", Size{800.0, 600.0}), false);
        require(admission.status.accepted(), "the primary window opens");
        (*(*runtime).windowing).mark_started();
        for (std::size_t index = 0U; index < (*runtime).windows.size(); ++index) (*(*runtime).windows[index]).ready();
        settle();
    }
    ~Harness() {
        if (runtime) (*(*runtime).windowing).shutdown();
        runtime.reset();
    }
    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;

    // A few turns, each woken, so painting and composing finish.
    void settle() {
        for (int turn = 0; turn < 4; ++turn) {
            host.queue.wake();
            (*runtime).step();
            (*runtime).reclaim();
        }
    }
    HostWindowId open(TestWindow& state, std::string stable_id) {
        const HostWindowAdmission admission =
            (*(*runtime).windowing).open(window_entry(state, std::move(stable_id), "main", Size{300.0, 200.0}), true);
        require(admission.status.accepted(), "an owned window opens");
        settle();
        return admission.window;
    }
    void post_pointer(SurfacePointerAction action, Point position, double time) {
        SurfaceInputRecord record;
        record.kind = SurfaceInputKind::pointer;
        record.action = action;
        record.position = position;
        record.button = action == SurfacePointerAction::move ? PointerButton::none : PointerButton::primary;
        record.time = time;
        static_cast<void>(host.queue.post(record));
    }
    // The capsule (the default) or the classic title bar boxes, for windows
    // opened after.
    void use_capsule() {
        SurfaceFrameTheme theme = (*runtime).desk.theme();
        theme.window_controls = SurfaceWindowControls::capsule;
        (*runtime).desk.set_theme(theme);
    }
    void use_boxes() {
        SurfaceFrameTheme theme = (*runtime).desk.theme();
        theme.window_controls = SurfaceWindowControls::boxes;
        (*runtime).desk.set_theme(theme);
    }
    void post_key(std::uint32_t physical_key, Modifier modifiers) {
        SurfaceInputRecord record;
        record.kind = SurfaceInputKind::key;
        record.physical_key = physical_key;
        record.modifiers = modifiers;
        record.down = true;
        static_cast<void>(host.queue.post(record));
        record.down = false;
        static_cast<void>(host.queue.post(record));
        settle();
    }
    void click(Point position, double time = 0.0) {
        post_pointer(SurfacePointerAction::move, position, time);
        post_pointer(SurfacePointerAction::down, position, time);
        post_pointer(SurfacePointerAction::up, position, time);
        settle();
    }
    [[nodiscard]] Rect content(HostWindowId window) const { return *(*runtime).desk.content(window); }
};

// An owned 300 by 200 window opens centred: client (250, 200, 300, 200),
// frame (249, 172, 302, 229); its title bar spans y 173 to 200. With the
// classic boxes (Harness::use_boxes), the close box is at x 510 to 550,
// maximise 470 to 510 and minimise 430 to 470.
void test_compose_and_damage() {
    Harness harness;
    TestWindow owned;
    const HostWindowId window = harness.open(owned, "owned");
    require(harness.host.presents > 0U && harness.host.acquired == 1U, "the surface was acquired once and presented");
    require(harness.host.rgb(249U, 300U) == (*harness.runtime).desk.theme().border_active,
            "the active window's edge is composed");
    require(harness.content(window) == (Rect{250.0, 200.0, 300.0, 200.0}), "centred over its owner");
    const std::uint64_t settled = harness.host.presents;
    harness.settle();
    require(harness.host.presents == settled, "nothing changed, nothing is composed");
    harness.post_pointer(SurfacePointerAction::move, Point{700.0, 100.0}, 0.0);
    harness.settle();
    require(harness.host.presents == settled + 1U, "a pointer move composes the drawn cursor");
    for (std::size_t index = 0U; index < harness.host.last_damage.size(); ++index) {
        const SurfaceDeviceRect rect = harness.host.last_damage[index];
        require(rect.right - rect.left < 64 && rect.bottom - rect.top < 64, "only the cursor's places are composed");
    }
    require(harness.host.host_cursor_calls == 0U, "the host without a cursor is never asked for one");
    require(harness.host.title == "main", "the primary window's title is the host's");
}

void test_resize_by_grip() {
    Harness harness;
    TestWindow owned;
    const HostWindowId window = harness.open(owned, "owned");
    harness.post_pointer(SurfacePointerAction::move, Point{552.0, 300.0}, 0.0);
    harness.post_pointer(SurfacePointerAction::down, Point{552.0, 300.0}, 0.0);
    harness.post_pointer(SurfacePointerAction::move, Point{602.0, 330.0}, 10.0);
    harness.post_pointer(SurfacePointerAction::up, Point{602.0, 330.0}, 20.0);
    harness.settle();
    require(harness.content(window) == (Rect{250.0, 200.0, 350.0, 200.0}), "the right grip widens the window");
    const NativeWindow* native = (*harness.runtime).window(window);
    require(native != nullptr && (*native).content.width == 350.0, "and the window follows");
    harness.post_pointer(SurfacePointerAction::down, Point{248.0, 403.0}, 100.0);
    harness.post_pointer(SurfacePointerAction::move, Point{240.0, 100.0}, 110.0);
    harness.post_pointer(SurfacePointerAction::up, Point{240.0, 100.0}, 120.0);
    harness.settle();
    require(harness.content(window).height == 80.0, "the bottom left corner stops at the minimum height");
}

void test_minimize_maximize_restore() {
    Harness harness;
    harness.use_boxes();
    TestWindow owned;
    const HostWindowId window = harness.open(owned, "owned");
    harness.click(Point{490.0, 186.0});
    require((*harness.runtime).desk.state(window) == SurfaceWindowState::maximized, "the maximise box maximises");
    require(harness.content(window) == (Rect{1.0, 28.0, 798.0, 571.0}), "to the whole surface");
    harness.click(Point{740.0, 14.0});
    require((*harness.runtime).desk.state(window) == SurfaceWindowState::normal, "the same box restores");
    require(harness.content(window) == (Rect{250.0, 200.0, 300.0, 200.0}), "the rectangle it had");
    harness.click(Point{450.0, 186.0});
    require((*harness.runtime).desk.state(window) == SurfaceWindowState::minimized, "the minimise box minimises");
    require((*harness.runtime).desk.active() != window, "the keyboard leaves it");
    harness.click(Point{50.0, 580.0});
    require((*harness.runtime).desk.state(window) == SurfaceWindowState::normal, "its tab restores it");
    require((*harness.runtime).desk.active() == window, "and gives it the keyboard");
    harness.click(Point{350.0, 186.0}, 1000.0);
    harness.click(Point{350.0, 186.0}, 1100.0);
    require((*harness.runtime).desk.state(window) == SurfaceWindowState::maximized,
            "a double click on the title bar maximises");
}

void test_close_box() {
    Harness harness;
    harness.use_boxes();
    TestWindow owned;
    const HostWindowId window = harness.open(owned, "owned");
    harness.post_pointer(SurfacePointerAction::down, Point{530.0, 186.0}, 0.0);
    harness.post_pointer(SurfacePointerAction::up, Point{100.0, 100.0}, 10.0);
    harness.settle();
    require(!owned.closed, "a press on the close box released elsewhere does not close");
    harness.click(Point{530.0, 186.0});
    require(owned.closed && (*harness.runtime).window(window) == nullptr, "a click on the close box closes");
    require((*harness.runtime).desk.active().valid(), "the keyboard returns to the primary window");
}

void test_focus_order() {
    Harness harness;
    TestWindow first;
    TestWindow second;
    const HostWindowId a = harness.open(first, "first");
    const HostWindowId b = harness.open(second, "second");
    require((*harness.runtime).desk.active() == b, "the window opened last has the keyboard");
    require((*harness.runtime).desk.order().back() == b, "and is in front");
    // The second window cascades 24 to the right and down; the first's
    // client area still shows at its top left.
    harness.click(Point{260.0, 210.0});
    require((*harness.runtime).desk.active() == a, "a click on the first window activates it");
    require((*harness.runtime).desk.order().back() == a, "and raises it");
    NativeWindow* keyboard = (*harness.runtime).keyboard_window();
    require(keyboard != nullptr && (*keyboard).window_id == a, "keys go to it");
}

void test_modal_blocking() {
    Harness harness;
    harness.use_boxes();
    TestWindow dialog;
    const HostWindowId window = harness.open(dialog, "dialog");
    // Queued for the modal loop: a click on the primary window's button,
    // then the dialog's close box.
    harness.post_pointer(SurfacePointerAction::move, Point{50.0, 30.0}, 0.0);
    harness.post_pointer(SurfacePointerAction::down, Point{50.0, 30.0}, 0.0);
    harness.post_pointer(SurfacePointerAction::up, Point{50.0, 30.0}, 10.0);
    harness.post_pointer(SurfacePointerAction::move, Point{530.0, 186.0}, 20.0);
    harness.post_pointer(SurfacePointerAction::down, Point{530.0, 186.0}, 20.0);
    harness.post_pointer(SurfacePointerAction::up, Point{530.0, 186.0}, 30.0);
    const HostServiceStatus status = (*(*harness.runtime).windowing).run_modal(window);
    require(status.accepted(), "the modal loop ran");
    require(dialog.closed, "the dialog's close box ended it");
    require(harness.primary.clicks == 0, "the blocked primary window took no click");
    require((*harness.runtime).desk.modal_depth() == 0U, "the modal session ended");
    harness.click(Point{50.0, 30.0}, 1000.0);
    require(harness.primary.clicks == 1, "afterwards the primary window takes clicks again");
}

// With the capsule, the owned window's capsule lies at x 468 to 544, y
// 176.5 to 196.5: its left half to 496, the question mark to 516, then its
// right half.
const Point capsule_left{480.0, 186.0};
const Point capsule_help{506.0, 186.0};
const Point capsule_right{530.0, 186.0};

void test_capsule_three_click_close() {
    Harness harness;
    harness.use_capsule();
    TestWindow owned;
    const HostWindowId window = harness.open(owned, "owned");
    require((*harness.runtime).desk.capsule(window) == (Rect{468.0, 176.5, 76.0, 20.0}),
            "the capsule sits at the title bar's right end");
    harness.click(capsule_left, 0.0);
    require((*harness.runtime).capsule.arm() == WindowCapsuleArm::left, "the first click arms the left half");
    harness.click(capsule_right, 100.0);
    require((*harness.runtime).capsule.arm() == WindowCapsuleArm::close, "the second makes the X");
    require(!owned.closed, "two clicks do not close");
    harness.click(capsule_help, 200.0);
    require(owned.closed && (*harness.runtime).window(window) == nullptr, "the third click closes the window");
    require(!(*harness.runtime).capsule.window().valid(), "and the capsule forgets it");

    TestWindow kept;
    const HostWindowId second = harness.open(kept, "kept");
    harness.click(capsule_left, 1000.0);
    harness.post_pointer(SurfacePointerAction::move, Point{300.0, 300.0}, 1050.0);
    harness.settle();
    require((*harness.runtime).capsule.arm() == WindowCapsuleArm::idle, "leaving the capsule disarms it");
    harness.click(capsule_right, 1100.0);
    require((*harness.runtime).capsule.arm() == WindowCapsuleArm::right,
            "so the next click arms one half again, not the X");
    require(!kept.closed && (*harness.runtime).window(second) != nullptr, "and nothing closed");
}

void test_capsule_left_half() {
    Harness harness;
    harness.use_capsule();
    TestWindow owned;
    const HostWindowId window = harness.open(owned, "owned");
    harness.click(capsule_left, 1000.0);
    harness.click(capsule_left, 1100.0);
    require((*harness.runtime).desk.state(window) == SurfaceWindowState::maximized,
            "a double click on the left half maximises");
    require((*harness.runtime).capsule.arm() == WindowCapsuleArm::idle, "and leaves nothing armed");
    // Maximised, the frame is the surface: the capsule at x 717, y 4.5.
    harness.click(Point{729.0, 14.0}, 2000.0);
    harness.click(Point{729.0, 14.0}, 2100.0);
    require((*harness.runtime).desk.state(window) == SurfaceWindowState::normal, "and another restores");
    require(harness.content(window) == (Rect{250.0, 200.0, 300.0, 200.0}), "to the rectangle it had");
    harness.post_pointer(SurfacePointerAction::move, capsule_left, 3000.0);
    harness.post_pointer(SurfacePointerAction::down, capsule_left, 3000.0);
    harness.post_pointer(SurfacePointerAction::move, Point{481.0, 200.0}, 3010.0);
    harness.post_pointer(SurfacePointerAction::up, Point{481.0, 200.0}, 3020.0);
    harness.settle();
    require((*harness.runtime).desk.state(window) == SurfaceWindowState::normal,
            "a short drag down does not minimise");
    harness.post_pointer(SurfacePointerAction::move, capsule_left, 4000.0);
    harness.post_pointer(SurfacePointerAction::down, capsule_left, 4000.0);
    harness.post_pointer(SurfacePointerAction::move, Point{481.0, 240.0}, 4010.0);
    harness.post_pointer(SurfacePointerAction::up, Point{481.0, 240.0}, 4020.0);
    harness.settle();
    require((*harness.runtime).desk.state(window) == SurfaceWindowState::minimized,
            "dragged down far enough, the left half minimises");
    require((*harness.runtime).desk.active() != window, "and the keyboard leaves the window");
}

void test_capsule_right_half_moves() {
    Harness harness;
    harness.use_capsule();
    TestWindow owned;
    const HostWindowId window = harness.open(owned, "owned");
    const std::uint32_t before = harness.host.rgb(100U, 500U);
    harness.post_pointer(SurfacePointerAction::move, capsule_right, 0.0);
    harness.post_pointer(SurfacePointerAction::down, capsule_right, 0.0);
    harness.post_pointer(SurfacePointerAction::move, Point{560.0, 200.0}, 10.0);
    harness.settle();
    require(harness.content(window) == (Rect{280.0, 214.0, 300.0, 200.0}), "the window moves with the pointer");
    harness.post_pointer(SurfacePointerAction::move, Point{5.0, 300.0}, 20.0);
    harness.settle();
    require((*harness.runtime).capsule.snap() == WindowCapsuleSnap::left_half, "the left edge arms the left half");
    require(harness.host.rgb(100U, 500U) != before, "and its snap preview is composed");
    harness.post_pointer(SurfacePointerAction::up, Point{5.0, 300.0}, 30.0);
    harness.settle();
    require((*harness.runtime).desk.state(window) == SurfaceWindowState::tiled_left, "released, it tiles left");
    require(harness.content(window) == (Rect{1.0, 28.0, 398.0, 571.0}), "filling the left half");
    // Tiled, the frame is (0, 0, 400, 600): the right half at x 365 to 393.
    harness.post_pointer(SurfacePointerAction::move, Point{380.0, 14.0}, 100.0);
    harness.post_pointer(SurfacePointerAction::down, Point{380.0, 14.0}, 100.0);
    harness.post_pointer(SurfacePointerAction::move, Point{400.0, 30.0}, 110.0);
    harness.post_pointer(SurfacePointerAction::move, Point{420.0, 300.0}, 120.0);
    harness.post_pointer(SurfacePointerAction::up, Point{420.0, 300.0}, 130.0);
    harness.settle();
    require((*harness.runtime).desk.state(window) == SurfaceWindowState::normal, "dragged to the middle, it floats");
    require(harness.content(window).width == 300.0 && harness.content(window).height == 200.0,
            "at its normal size");
    require(harness.host.rgb(100U, 500U) == before, "and the preview is gone");
}

int help_calls = 0;
HostWindowId help_window{};

void count_help(void* context, HostWindowId window) {
    int* calls = static_cast<int*>(context);
    ++(*calls);
    help_window = window;
}

void test_capsule_help_and_keyboard() {
    Harness harness;
    harness.use_capsule();
    (*harness.runtime).capsule_help = SurfaceCapsuleHelpTarget{&help_calls, &count_help};
    TestWindow owned;
    const HostWindowId window = harness.open(owned, "owned");
    harness.post_pointer(SurfacePointerAction::move, capsule_help, 0.0);
    harness.settle();
    require((*harness.runtime).capsule.title_shown(), "hovering the question mark shows the title");
    harness.click(capsule_help, 10.0);
    require(help_calls == 1 && help_window == window, "a click on it reaches the help call point");
    harness.post_pointer(SurfacePointerAction::move, Point{300.0, 300.0}, 20.0);
    harness.settle();

    harness.post_key(PhysicalKey::space, Modifier::alt);
    require((*harness.runtime).capsule.keyboard(), "Alt+Space gives the capsule the keyboard");
    harness.post_key(PhysicalKey::right, Modifier::shift);
    require((*harness.runtime).desk.state(window) == SurfaceWindowState::tiled_right, "Shift+Right tiles right");
    harness.post_key(PhysicalKey::down, Modifier::shift);
    require((*harness.runtime).desk.state(window) == SurfaceWindowState::normal, "Shift+Down floats it again");
    harness.post_key(PhysicalKey::up, Modifier::none);
    require((*harness.runtime).desk.state(window) == SurfaceWindowState::maximized, "Up maximises");
    harness.post_key(PhysicalKey::up, Modifier::none);
    const Rect normal = harness.content(window);
    harness.post_key(PhysicalKey::right, Modifier::control);
    require(harness.content(window).x == normal.x + 16.0, "Control+Right moves the window");
    harness.post_key(PhysicalKey::f1, Modifier::none);
    require(help_calls == 2, "F1 opens the help");
    harness.post_key(PhysicalKey::enter, Modifier::none);
    harness.post_key(PhysicalKey::enter, Modifier::none);
    require(!owned.closed, "two Enters only arm the close");
    harness.post_key(PhysicalKey::enter, Modifier::none);
    require(owned.closed, "the third Enter closes, as the third click does");
    (*harness.runtime).capsule_help = SurfaceCapsuleHelpTarget{};
}

// Without a help target of the program's, the capsule's question mark
// shows the business card in the hand over the window (the one card every
// program shows); a press beside the card, or Escape, puts it away, and
// the window takes clicks again.
void test_capsule_help_shows_business_card() {
    Harness harness;
    harness.use_capsule();
    TestWindow owned;
    const HostWindowId window = harness.open(owned, "owned");
    NativeWindow* const native = (*harness.runtime).window(window);
    require(native != nullptr && (*native).entry.model != nullptr, "the owned window has its model");
    Window& model = *(*native).entry.model;
    require(!business_card_shown(model), "no card before the question mark");
    harness.click(capsule_help, 0.0);
    require(business_card_shown(model), "the question mark shows the business card");
    const std::shared_ptr<BusinessCard> card = shown_business_card(model);
    require(card != nullptr && (*card).presentation() == BusinessCardPresentation::hand, "held in the hand");
    harness.click(Point{256.0, 206.0}, 1000.0);
    require(!business_card_shown(model), "a press beside the card puts it away");
    require(owned.clicks == 0, "and reaches nothing beneath");
    harness.click(capsule_help, 2000.0);
    require(business_card_shown(model), "the card comes out again");
    harness.post_key(PhysicalKey::escape, Modifier::none);
    require(!business_card_shown(model), "Escape puts it away");
}

} // namespace

int main() {
    try {
        test_compose_and_damage();
        test_resize_by_grip();
        test_minimize_maximize_restore();
        test_close_box();
        test_focus_order();
        test_modal_blocking();
        test_capsule_three_click_close();
        test_capsule_left_half();
        test_capsule_right_half_moves();
        test_capsule_help_and_keyboard();
        test_capsule_help_shows_business_card();
    } catch (const std::exception& error) {
        std::cerr << "surface window manager tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "surface window manager tests passed\n";
    return EXIT_SUCCESS;
}
