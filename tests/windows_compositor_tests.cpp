// Stage T5, phase C2's gate (notes:planning/stages/T5-compositor/PLAN.md §5 C2):
// a GUI.Forms window on the Windows host, declared non-opaque, through each
// grant its machine allows, read off the composed screen.
//
// The window's theme has a clear window material, so that every pixel the
// window presents is clear and the screen shows what DWM puts behind it. A
// solid green Win32 window lies behind it. The test asks for a material
// with transparent as its fallback, then for transparent, then for opaque,
// and after each one reads the composed pixel at the client's centre:
// - transparent: the green behind the window;
// - a material: neither green nor black (DWM's material, or its solid
//   colour while the window is inactive);
// - opaque: not green, since DWM ignores the client's alpha again.
// The grant itself follows from the window's compositor state, which is
// checked against what the host must report. On a machine without the
// materials (Windows Server 2022 on CI) the first request falls back to
// transparent with the reason os_version; on Windows 11 22H2 and later it
// is granted. Transparency effects off or high contrast on make every
// grant opaque, and the test checks that instead.
//
// The test window and the backdrop are topmost while the test runs, so that
// another test's windows on the same desktop cannot cover them. Each
// reading is printed as a JSON line for the C2 record. The state line also
// carries the raw settings it came from, read independently of the host
// (the registry value, WinRT's UISettings, SPI_GETHIGHCONTRAST and
// DwmIsCompositionEnabled), so that a run explains itself; a failure is
// printed as a JSON line too.
#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/window.hpp"

#include "support/windows_own_window.hpp"

#include <windows.h>
#include <dwmapi.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.ui.viewmanagement.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <memory>
#include <string>

namespace {

using namespace gui_forms;

constexpr int channel_tolerance = 12;
constexpr std::array<int, 3> backdrop_rgb{0, 160, 0};
// Timer ticks (perceptual: 100 ms each) waited after each request, for the
// repaint and DWM's backdrop to settle.
constexpr unsigned settle_ticks = 6U;
constexpr UINT tick_milliseconds = 100U;

// One composed screen colour.
struct ScreenColour final {
    int red{};
    int green{};
    int blue{};
};

bool near_rgb(const ScreenColour& colour, int red, int green, int blue) noexcept {
    const bool near_enough = std::abs(colour.red - red) <= channel_tolerance &&
                             std::abs(colour.green - green) <= channel_tolerance &&
                             std::abs(colour.blue - blue) <= channel_tolerance;
    return near_enough;
}

// The composed pixel at a screen point (a one-pixel copy from the screen DC).
bool screen_colour(POINT point, ScreenColour& colour) {
    const HDC screen = GetDC(nullptr);
    if (screen == nullptr) return false;
    const HDC memory = CreateCompatibleDC(screen);
    const HBITMAP bitmap = CreateCompatibleBitmap(screen, 1, 1);
    bool copied = false;
    if (memory != nullptr && bitmap != nullptr) {
        const HGDIOBJ previous = SelectObject(memory, bitmap);
        copied = BitBlt(memory, 0, 0, 1, 1, screen, point.x, point.y, SRCCOPY | CAPTUREBLT) != FALSE;
        const COLORREF pixel = GetPixel(memory, 0, 0);
        SelectObject(memory, previous);
        colour.red = GetRValue(pixel);
        colour.green = GetGValue(pixel);
        colour.blue = GetBValue(pixel);
    }
    if (bitmap != nullptr) DeleteObject(bitmap);
    if (memory != nullptr) DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return copied;
}

LRESULT CALLBACK backdrop_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        const HBRUSH brush = CreateSolidBrush(RGB(backdrop_rgb[0], backdrop_rgb[1], backdrop_rgb[2]));
        FillRect(dc, &paint.rcPaint, brush);
        DeleteObject(brush);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

// The professional theme with a clear window material.
std::shared_ptr<const Theme> clear_theme() {
    const std::unique_ptr<ThemeDefinition> held(new ThemeDefinition(windows_professional_theme_definition()));
    ThemeDefinition& definition = *held;
    SurfaceMaterial clear{};
    clear.fills.clear();
    clear.fills.push_back(MaterialFillLayer::solid(Color::rgba(0, 0, 0, 0)));
    ControlRoleRecipes& window = definition.roles[static_cast<std::size_t>(ControlVisualRole::window)];
    for (ControlVisualRecipe& recipe : window.ordinary) recipe.material = clear;
    std::shared_ptr<const Theme> theme = Theme::create(definition);
    return theme;
}

// The steps the timer walks through.
enum class GateStep : std::uint8_t {
    material,
    transparent,
    opaque,
    finished,
};

const char* step_name(GateStep step) noexcept {
    switch (step) {
    case GateStep::material: return "material";
    case GateStep::transparent: return "transparent";
    case GateStep::opaque: return "opaque";
    case GateStep::finished: return "finished";
    }
    return "unknown";
}

// The gate's state, owned by main for the whole run; the window callbacks
// and the timer reach it through `active_gate`, which lives no longer.
struct GateContext final {
    std::wstring title{};
    HWND backdrop{};
    HWND window{};
    Window* model{};
    ApplicationWindowHandle handle{};
    UINT_PTR timer{};
    GateStep step{GateStep::material};
    unsigned waited{};
    unsigned readings{};
    const char* failure{};
};

GateContext* active_gate{};

void fail(GateContext& gate, const char* message) {
    if (gate.failure == nullptr) gate.failure = message;
}

// The raw settings behind the host's state, read here independently of it.
// `enable_transparency` is the registry value (1 on, 0 off, -1 absent) and
// `enable_transparency_status` the LSTATUS of reading it;
// `advanced_effects` is UISettings.AdvancedEffectsEnabled (-1 unavailable);
// `high_contrast_flags` is HIGHCONTRASTW::dwFlags (HCF_HIGHCONTRASTON is
// 1), 0 when unread; `composition` is what DwmIsCompositionEnabled answers
// (-1 when it fails).
struct SettingSources final {
    unsigned long build{};
    int enable_transparency{};
    long enable_transparency_status{};
    int advanced_effects{};
    bool high_contrast_read{};
    unsigned long high_contrast_flags{};
    int composition{};
    bool remote_session{};
};

// HKLM\...\Windows NT\CurrentVersion CurrentBuildNumber, for the record
// (GetVersionExW reports 9200 without a compatibility manifest); 0 unread.
unsigned long operating_system_build() {
    std::array<wchar_t, 16> text{};
    DWORD size = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                                        L"CurrentBuildNumber", RRF_RT_REG_SZ, nullptr, text.data(), &size);
    if (status != ERROR_SUCCESS) return 0UL;
    const unsigned long build = std::wcstoul(text.data(), nullptr, 10);
    return build;
}

// The interface identifier windows.ui.viewmanagement.h declares for
// IUISettings4 (Windows 10 1803).
constexpr GUID ui_settings4_interface{0x52bb3002, 0x919b, 0x4d6b, {0x9b, 0x78, 0x8d, 0xd6, 0x6f, 0xf4, 0xb9, 0x3b}};

int advanced_effects_setting() {
    const HRESULT initialized = RoInitialize(RO_INIT_SINGLETHREADED);
    const bool uninitialize = SUCCEEDED(initialized);
    int setting = -1;
    HSTRING_HEADER header{};
    HSTRING class_name = nullptr;
    const wchar_t* const name = RuntimeClass_Windows_UI_ViewManagement_UISettings;
    if (SUCCEEDED(WindowsCreateStringReference(name, static_cast<UINT32>(std::wcslen(name)), &header, &class_name))) {
        IInspectable* instance = nullptr;
        if (SUCCEEDED(RoActivateInstance(class_name, &instance)) && instance != nullptr) {
            void* queried = nullptr;
            if (SUCCEEDED((*instance).QueryInterface(ui_settings4_interface, &queried)) && queried != nullptr) {
                ABI::Windows::UI::ViewManagement::IUISettings4* const settings =
                    static_cast<ABI::Windows::UI::ViewManagement::IUISettings4*>(queried);
                boolean enabled = 0;
                if (SUCCEEDED((*settings).get_AdvancedEffectsEnabled(&enabled))) setting = enabled != 0 ? 1 : 0;
                (*settings).Release();
            }
            (*instance).Release();
        }
    }
    if (uninitialize) RoUninitialize();
    return setting;
}

SettingSources read_setting_sources() {
    SettingSources sources{};
    sources.build = operating_system_build();
    DWORD value = 0U;
    DWORD size = sizeof(value);
    const LSTATUS status =
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &value, &size);
    sources.enable_transparency_status = static_cast<long>(status);
    sources.enable_transparency = -1;
    if (status == ERROR_SUCCESS) sources.enable_transparency = value != 0U ? 1 : 0;
    sources.advanced_effects = advanced_effects_setting();
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    sources.high_contrast_read = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE;
    if (sources.high_contrast_read) sources.high_contrast_flags = static_cast<unsigned long>(contrast.dwFlags);
    BOOL composition = FALSE;
    sources.composition = -1;
    if (SUCCEEDED(DwmIsCompositionEnabled(&composition))) sources.composition = composition != FALSE ? 1 : 0;
    sources.remote_session = GetSystemMetrics(SM_REMOTESESSION) != 0;
    return sources;
}

// What the window's compositor state must be on this machine: present,
// transparent supported, both materials supported or os_version together.
void check_state(GateContext& gate) {
    const CompositorState& state = (*gate.model).compositor_state();
    const SettingSources sources = read_setting_sources();
    std::printf("{\"gate\":\"windows_compositor\",\"state\":%s,\"sources\":{\"build\":%lu,"
                "\"enable_transparency\":%d,\"enable_transparency_status\":%ld,\"advanced_effects\":%d,"
                "\"high_contrast_read\":%s,\"high_contrast_flags\":%lu,\"composition\":%d,\"remote_session\":%s}}\n",
                state.to_json().c_str(), sources.build, sources.enable_transparency,
                sources.enable_transparency_status, sources.advanced_effects,
                sources.high_contrast_read ? "true" : "false", sources.high_contrast_flags, sources.composition,
                sources.remote_session ? "true" : "false");
    if (state.generation.value == 0U) fail(gate, "the host reports its compositor state after attach");
    if (state.compositing != CompositorPresence::present) fail(gate, "DWM composes on every supported Windows");
    if (state.support_of(WindowBackdrop::transparent) != WindowBackdropSupport::supported) {
        fail(gate, "the client's alpha is supported");
    }
    const WindowBackdropSupport main_material = state.support_of(WindowBackdrop::window_material);
    const WindowBackdropSupport transient = state.support_of(WindowBackdrop::transient_material);
    const bool material_known =
        main_material == WindowBackdropSupport::supported || main_material == WindowBackdropSupport::os_version;
    if (!material_known || transient != main_material) fail(gate, "both materials share the attribute's support");
    if (state.reduced_transparency == state.transparency_enabled) {
        fail(gate, "transparency effects off is reduced transparency on Windows");
    }
}

void request_for(GateContext& gate, GateStep step) {
    WindowBackdropRequest request{};
    if (step == GateStep::material) {
        request = WindowBackdropRequest::with_fallback(WindowBackdrop::window_material, WindowBackdrop::transparent);
    } else if (step == GateStep::transparent) {
        request = WindowBackdropRequest::preferring(WindowBackdrop::transparent);
    } else {
        // The opaque step, and any later one: withdraw the backdrop.
        request = WindowBackdropRequest::preferring(WindowBackdrop::opaque);
    }
    static_cast<void>((*gate.model).request_backdrop(request));
}

// The composed pixel at the client's centre after a step, against the grant.
void read_step(GateContext& gate) {
    RECT client{};
    GetClientRect(gate.window, &client);
    POINT centre{(client.right - client.left) / 2, (client.bottom - client.top) / 2};
    ClientToScreen(gate.window, &centre);
    ScreenColour colour{};
    if (!screen_colour(centre, colour)) {
        fail(gate, "the composed screen is readable");
        return;
    }
    const WindowBackdropGrant grant = (*gate.model).backdrop_grant();
    const bool hits_window = WindowFromPoint(centre) == gate.window;
    // What DWM applied (stage gate, PLAN.md §6): the system backdrop type
    // as read back, -1 where the attribute does not exist (before build
    // 22621), and the extended frame bounds in screen pixels.
    DWORD backdrop_type = 0U;
    const HRESULT type_read = DwmGetWindowAttribute(gate.window, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop_type,
                                                    sizeof(backdrop_type));
    const long applied_type = SUCCEEDED(type_read) ? static_cast<long>(backdrop_type) : -1L;
    RECT frame{};
    const HRESULT frame_read = DwmGetWindowAttribute(gate.window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame));
    if (FAILED(frame_read)) frame = RECT{};
    std::printf("{\"gate\":\"windows_compositor\",\"step\":\"%s\",\"granted\":\"%s\",\"reason\":\"%s\","
                "\"clear\":[%d,%d,%d],\"clear_hits_window\":%s,\"dwm_backdrop_type\":%ld,"
                "\"extended_frame\":[%ld,%ld,%ld,%ld]}\n",
                step_name(gate.step), window_backdrop_name(grant.granted), window_backdrop_reason_name(grant.reason),
                colour.red, colour.green, colour.blue, hits_window ? "true" : "false", applied_type,
                static_cast<long>(frame.left), static_cast<long>(frame.top), static_cast<long>(frame.right),
                static_cast<long>(frame.bottom));
    gate.readings = gate.readings + 1U;
    if (!hits_window) fail(gate, "a clear pixel still hits the window (no click-through)");
    const bool green = near_rgb(colour, backdrop_rgb[0], backdrop_rgb[1], backdrop_rgb[2]);
    const bool black = near_rgb(colour, 0, 0, 0);
    const CompositorState& state = (*gate.model).compositor_state();
    const PresentationSettings& settings = (*gate.model).presentation_settings();
    const bool withdrawn = !state.transparency_enabled || settings.high_contrast || settings.reduced_transparency;
    if (withdrawn) {
        if (grant.granted != WindowBackdrop::opaque) fail(gate, "the user's settings withdraw every backdrop");
        if (green) fail(gate, "a withdrawn backdrop shows nothing behind the window");
        // The setting that withdrew it is the reason: high contrast first
        // (owner decision T5-Q7), then reduced transparency, which Windows'
        // transparency-effects switch also sets.
        WindowBackdropReason expected = WindowBackdropReason::transparency_disabled;
        if (settings.reduced_transparency) expected = WindowBackdropReason::reduced_transparency;
        if (settings.high_contrast) expected = WindowBackdropReason::high_contrast;
        if (gate.step != GateStep::opaque && grant.reason != expected) {
            fail(gate, "a withdrawn backdrop gives the user's setting as its reason");
        }
        return;
    }
    switch (grant.granted) {
    case WindowBackdrop::transparent:
        if (!green) fail(gate, "a transparent window shows what lies behind it");
        break;
    case WindowBackdrop::window_material:
    case WindowBackdrop::transient_material:
        if (green || black) fail(gate, "a material shows behind the clear pixels");
        break;
    case WindowBackdrop::opaque:
        if (green) fail(gate, "an opaque window shows nothing behind it");
        break;
    default:
        // Every WindowBackdrop is listed above. A value outside them is no
        // grant the window gives, so there is no screen reading to judge; the
        // step checks below still compare the grant with the step.
        break;
    }
    if (gate.step == GateStep::material) {
        const bool materials =
            state.support_of(WindowBackdrop::window_material) == WindowBackdropSupport::supported;
        const WindowBackdrop expected = materials ? WindowBackdrop::window_material : WindowBackdrop::transparent;
        if (grant.granted != expected) fail(gate, "the material, or its transparent fallback, is granted");
        if (!materials && grant.reason != WindowBackdropReason::os_version) {
            fail(gate, "a missing material is refused with os_version");
        }
    }
    if (gate.step == GateStep::opaque && grant.granted != WindowBackdrop::opaque) fail(gate, "opaque is granted");
}

void CALLBACK gate_tick(HWND, UINT, UINT_PTR, DWORD) {
    GateContext* const held = active_gate;
    if (held == nullptr) return;
    GateContext& gate = *held;
    static_cast<void>(DwmFlush());
    if (gate.step == GateStep::finished) return;
    gate.waited = gate.waited + 1U;
    if (gate.waited < settle_ticks) return;
    gate.waited = 0U;
    read_step(gate);
    if (gate.step == GateStep::material) {
        gate.step = GateStep::transparent;
        request_for(gate, gate.step);
        return;
    }
    if (gate.step == GateStep::transparent) {
        gate.step = GateStep::opaque;
        request_for(gate, gate.step);
        return;
    }
    gate.step = GateStep::finished;
    KillTimer(nullptr, gate.timer);
    gate.timer = 0U;
    static_cast<void>(gate.handle.request_close());
}

// The window is ready: find it, put it over the backdrop and above other
// windows, give it the clear theme and make the first request.
struct GateReady final {
    void operator()(Window& model, ApplicationWindowHandle handle) const {
        GateContext* const held = active_gate;
        if (held == nullptr) return;
        GateContext& gate = *held;
        gate.model = &model;
        gate.handle = handle;
        gate.window = test_support::own_gui_forms_window(gate.title.c_str());
        if (gate.window == nullptr) {
            fail(gate, "the test finds its own window");
            static_cast<void>(handle.request_close());
            return;
        }
        RECT backdrop{};
        GetWindowRect(gate.backdrop, &backdrop);
        SetWindowPos(gate.window, HWND_TOPMOST, backdrop.left + 60, backdrop.top + 60, 0, 0,
                     SWP_NOSIZE | SWP_SHOWWINDOW);
        SetWindowPos(gate.backdrop, gate.window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        model.set_theme(clear_theme());
        if (!model.non_opaque_declared()) fail(gate, "the window option declares the window non-opaque");
        check_state(gate);
        request_for(gate, GateStep::material);
        gate.timer = SetTimer(nullptr, 0U, tick_milliseconds, &gate_tick);
        if (gate.timer == 0U) {
            fail(gate, "the gate's timer starts");
            static_cast<void>(handle.request_close());
        }
    }
};

HWND make_backdrop() {
    WNDCLASSEXW backdrop_class{};
    backdrop_class.cbSize = sizeof(backdrop_class);
    backdrop_class.lpfnWndProc = &backdrop_procedure;
    backdrop_class.hInstance = GetModuleHandleW(nullptr);
    backdrop_class.lpszClassName = L"GuiFormsCompositorGateBackdrop";
    if (RegisterClassExW(&backdrop_class) == 0) return nullptr;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                        L"GuiFormsCompositorGateBackdrop", L"", WS_POPUP, work.left + 80,
                                        work.top + 80, 520, 420, nullptr, nullptr, GetModuleHandleW(nullptr),
                                        nullptr);
    if (window == nullptr) return nullptr;
    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);
    return window;
}

} // namespace

int main() {
    // The program's application (APP_WINDOW_RULES_DESIGN A.2): windows made
    // from here join it, and it runs them.
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    if (!created) return 1;
    Application& application = *created.application;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HostCapabilities capabilities = Application::capabilities();
    if (!capabilities.supports(HostCapability::transparent_windows) ||
        !capabilities.supports(HostCapability::window_backdrops) ||
        !capabilities.supports(HostCapability::compositor_notifications) ||
        !capabilities.supports(HostCapability::presentation_preferences)) {
        std::fprintf(stderr, "the Windows host advertises the compositor capabilities\n");
        return 1;
    }
    GateContext gate{};
    gate.title = test_support::process_title_wide("GUI.Forms compositor gate");
    gate.backdrop = make_backdrop();
    if (gate.backdrop == nullptr) {
        std::fprintf(stderr, "the backdrop window is made\n");
        return 1;
    }
    active_gate = &gate;
    const std::shared_ptr<Control> root = make_control<Control>(StableId("compositor.gate.root"));
    std::unique_ptr<Window> model = std::make_unique<Window>(root, Size{240.0, 160.0});
    ApplicationWindowOptions options{};
    options.title = test_support::process_title("GUI.Forms compositor gate");
    options.initial_size = Size{240.0, 160.0};
    options.non_opaque = true;
    options.ready = GateReady{};
    const ApplicationResult result = application.run(std::move(model), std::move(options));
    active_gate = nullptr;
    if (gate.timer != 0U) KillTimer(nullptr, gate.timer);
    DestroyWindow(gate.backdrop);
    std::fflush(stdout);
    if (!result.accepted()) {
        std::fprintf(stderr, "the application runs\n");
        return 1;
    }
    if (gate.failure != nullptr) {
        std::printf("{\"gate\":\"windows_compositor\",\"failed\":\"%s\"}\n", gate.failure);
        std::fprintf(stderr, "%s\n", gate.failure);
        return 1;
    }
    if (gate.readings != 3U) {
        std::fprintf(stderr, "every step was read\n");
        return 1;
    }
    std::printf("windows compositor tests passed\n");
    return 0;
}
