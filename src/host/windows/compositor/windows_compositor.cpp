#include "windows_compositor.hpp"

#include <dwmapi.h>
#include <versionhelpers.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <cwchar>

namespace gui_forms::host::detail {
namespace {

// dwmapi.dll's entry points, typed from their declarations (house style
// section 2) and loaded once. A missing library or entry point leaves its
// pointer null.
using DwmIsCompositionEnabledFunction = decltype(&::DwmIsCompositionEnabled);
using DwmEnableBlurBehindWindowFunction = decltype(&::DwmEnableBlurBehindWindow);
using DwmSetWindowAttributeFunction = decltype(&::DwmSetWindowAttribute);
using DwmGetWindowAttributeFunction = decltype(&::DwmGetWindowAttribute);

struct DwmFunctions final {
    DwmIsCompositionEnabledFunction composition_enabled{};
    DwmEnableBlurBehindWindowFunction enable_blur_behind{};
    DwmSetWindowAttributeFunction set_attribute{};
    DwmGetWindowAttributeFunction get_attribute{};
};

template <typename Function>
Function dwm_function(HMODULE library, const char* name) noexcept {
    static_assert(sizeof(Function) == sizeof(FARPROC));
    const FARPROC raw = GetProcAddress(library, name);
    Function function{};
    std::memcpy(&function, &raw, sizeof(function));
    return function;
}

DwmFunctions load_dwm_functions() noexcept {
    DwmFunctions functions{};
    const HMODULE library = LoadLibraryW(L"dwmapi.dll");
    if (library == nullptr) return functions;
    functions.composition_enabled = dwm_function<DwmIsCompositionEnabledFunction>(library, "DwmIsCompositionEnabled");
    functions.enable_blur_behind =
        dwm_function<DwmEnableBlurBehindWindowFunction>(library, "DwmEnableBlurBehindWindow");
    functions.set_attribute = dwm_function<DwmSetWindowAttributeFunction>(library, "DwmSetWindowAttribute");
    functions.get_attribute = dwm_function<DwmGetWindowAttributeFunction>(library, "DwmGetWindowAttribute");
    return functions;
}

// Loaded on first use; the library stays loaded for the process.
const DwmFunctions& dwm() noexcept {
    static const DwmFunctions functions = load_dwm_functions();
    return functions;
}

// The transparency-effects setting: HKCU\...\Themes\Personalize
// EnableTransparency, on when the value is absent (as Windows treats it).
// MEASUREMENTS.md C2 found it agreeing with UISettings.AdvancedEffectsEnabled.
bool transparency_effects_enabled() noexcept {
    DWORD value = 1U;
    DWORD size = sizeof(value);
    const LSTATUS status =
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (status != ERROR_SUCCESS) return true;
    const bool enabled = value != 0U;
    return enabled;
}

// Whether the user chose dark applications (Settings, Colours, "Choose
// your default app mode"): AppsUseLightTheme 0. Light where the value is
// absent (before Windows 10 1809).
bool dark_applications() noexcept {
    DWORD value = 1U;
    DWORD size = sizeof(value);
    const LSTATUS status =
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (status != ERROR_SUCCESS) return false;
    const bool dark = value == 0U;
    return dark;
}

// The accent colour (Settings, Colours, "Accent colour"): DWM's
// AccentColor, a DWORD laid out 0xAABBGGRR; empty where it is absent.
std::optional<Color> accent_colour() noexcept {
    DWORD value = 0U;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor",
                                        RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (status != ERROR_SUCCESS) return std::nullopt;
    const Color colour = Color::rgba(static_cast<std::uint8_t>(value & 0xFFU),
                                     static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
                                     static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    return colour;
}

// The text-selection colour (COLOR_HIGHLIGHT), which GetSysColor always
// answers.
Color highlight_colour() noexcept {
    const COLORREF value = GetSysColor(COLOR_HIGHLIGHT);
    const Color colour = Color::rgba(GetRValue(value), GetGValue(value), GetBValue(value));
    return colour;
}

bool high_contrast_on() noexcept {
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) == FALSE) return false;
    const bool on = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0U;
    return on;
}

// Whether DWMWA_SYSTEMBACKDROP_TYPE exists: it reads back on build 22621
// and later (MEASUREMENTS.md C2), so reading it changes nothing.
bool backdrop_attribute_readable(HWND window) noexcept {
    const DwmFunctions& functions = dwm();
    if (functions.get_attribute == nullptr || window == nullptr) return false;
    int type = 0;
    const HRESULT read = functions.get_attribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
    const bool readable = SUCCEEDED(read);
    return readable;
}

HRESULT set_backdrop_type(HWND window, int type) noexcept {
    const HRESULT set = dwm().set_attribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
    return set;
}

// Blur behind on with an empty region (the client's alpha shown, nothing
// blurred), or off.
HRESULT set_client_alpha(HWND window, bool enabled) noexcept {
    DWM_BLURBEHIND blur{};
    blur.dwFlags = DWM_BB_ENABLE;
    blur.fEnable = enabled ? TRUE : FALSE;
    HRGN empty = nullptr;
    if (enabled) {
        empty = CreateRectRgn(0, 0, -1, -1);
        if (empty == nullptr) return E_OUTOFMEMORY;
        blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
        blur.hRgnBlur = empty;
    }
    const HRESULT applied = dwm().enable_blur_behind(window, &blur);
    if (empty != nullptr) DeleteObject(empty);
    return applied;
}

} // namespace

CompositorState read_windows_compositor_state(HWND window, CompositorGeneration generation) noexcept {
    CompositorState state{};
    state.generation = generation;
    const DwmFunctions& functions = dwm();
    // DWM composes every desktop from Windows 8 on and cannot be turned
    // off; DwmIsCompositionEnabled answers FALSE under a contrast theme
    // there all the same (the owner's run, MEASUREMENTS.md C2), so it is
    // asked only on an older system.
    state.compositing = CompositorPresence::unknown;
    if (IsWindows8OrGreater()) {
        state.compositing = CompositorPresence::present;
    } else {
        BOOL composing = FALSE;
        const bool known =
            functions.composition_enabled != nullptr && SUCCEEDED(functions.composition_enabled(&composing));
        if (known) state.compositing = composing != FALSE ? CompositorPresence::present : CompositorPresence::absent;
    }
    const bool alpha_available = functions.enable_blur_behind != nullptr;
    const bool materials_available = functions.set_attribute != nullptr && backdrop_attribute_readable(window);
    const WindowBackdropSupport transparent_support =
        alpha_available ? WindowBackdropSupport::supported : WindowBackdropSupport::host_unsupported;
    WindowBackdropSupport material_support = WindowBackdropSupport::os_version;
    if (!alpha_available || functions.set_attribute == nullptr) material_support = WindowBackdropSupport::host_unsupported;
    if (materials_available) material_support = WindowBackdropSupport::supported;
    state.support[static_cast<std::size_t>(WindowBackdrop::transparent)] = transparent_support;
    state.support[static_cast<std::size_t>(WindowBackdrop::window_material)] = material_support;
    state.support[static_cast<std::size_t>(WindowBackdrop::transient_material)] = material_support;
    // Windows has one switch for both: transparency effects off is also
    // reduced transparency for what the window draws (PLAN.md §4.5).
    const bool effects = transparency_effects_enabled();
    state.transparency_enabled = effects;
    state.reduced_transparency = !effects;
    state.high_contrast = high_contrast_on();
    // T4 theme variants. Windows' own high-contrast palette is not taken:
    // GUI.Forms' high contrast uses its own (a recorded divergence).
    state.dark_scheme = dark_applications();
    state.system_colours.accent = accent_colour();
    state.system_colours.highlight = highlight_colour();
    return state;
}

bool windows_compositor_message(UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    if (message == WM_THEMECHANGED || message == WM_DWMCOMPOSITIONCHANGED) return true;
    if (message != WM_SETTINGCHANGE) return false;
    if (wparam == SPI_SETHIGHCONTRAST) return true;
    if (lparam == 0) return false;
    const wchar_t* const area = reinterpret_cast<const wchar_t*>(lparam);
    const bool colour_set = std::wcscmp(area, L"ImmersiveColorSet") == 0;
    return colour_set;
}

HostServiceStatus apply_windows_backdrop(HWND window, WindowBackdrop backdrop) noexcept {
    const DwmFunctions& functions = dwm();
    if (window == nullptr) return {HostServiceError::backend_failure};
    const bool materials = functions.set_attribute != nullptr && backdrop_attribute_readable(window);
    if (backdrop == WindowBackdrop::opaque) {
        // Nothing was applied without the library.
        if (functions.enable_blur_behind == nullptr) return {};
        const HRESULT unblurred = set_client_alpha(window, false);
        const HRESULT automatic = materials ? set_backdrop_type(window, DWMSBT_AUTO) : S_OK;
        if (FAILED(unblurred) || FAILED(automatic)) return {HostServiceError::backend_failure};
        return {};
    }
    if (functions.enable_blur_behind == nullptr) return {HostServiceError::backend_failure};
    int type = DWMSBT_AUTO;
    if (backdrop == WindowBackdrop::window_material) type = DWMSBT_MAINWINDOW;
    if (backdrop == WindowBackdrop::transient_material) type = DWMSBT_TRANSIENTWINDOW;
    if (type != DWMSBT_AUTO && !materials) return {HostServiceError::backend_failure};
    const HRESULT alpha = set_client_alpha(window, true);
    if (FAILED(alpha)) return {HostServiceError::backend_failure};
    const HRESULT material = materials ? set_backdrop_type(window, type) : S_OK;
    if (FAILED(material)) return {HostServiceError::backend_failure};
    return {};
}

void apply_windows_popup_frame(HWND window) noexcept {
    const DwmFunctions& functions = dwm();
    if (window == nullptr || functions.set_attribute == nullptr) return;
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
    static_cast<void>(functions.set_attribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners)));
    const COLORREF border = DWMWA_COLOR_NONE;
    static_cast<void>(functions.set_attribute(window, DWMWA_BORDER_COLOR, &border, sizeof(border)));
}

HostWindowCornersResult apply_windows_corners(HWND window, WindowCornerPreference preference) noexcept {
    HostWindowCornersResult result{};
    const DwmFunctions& functions = dwm();
    if (window == nullptr || functions.set_attribute == nullptr) {
        result.status.error = HostServiceError::backend_failure;
        return result;
    }
    DWM_WINDOW_CORNER_PREFERENCE value = DWMWCP_DEFAULT;
    if (preference == WindowCornerPreference::square) value = DWMWCP_DONOTROUND;
    if (preference == WindowCornerPreference::round) value = DWMWCP_ROUND;
    const HRESULT set = functions.set_attribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &value, sizeof(value));
    if (FAILED(set)) {
        // Before Windows 11 the attribute is refused: windows are square,
        // and cannot be rounded.
        result.report.shape = WindowCornerShape::square;
        result.report.complied = preference != WindowCornerPreference::round;
        return result;
    }
    const WindowsWindowState state = read_windows_window_state(window);
    if (preference == WindowCornerPreference::square) {
        result.report.shape = WindowCornerShape::square;
        result.report.complied = true;
    } else if (state.squares_corners()) {
        // DWM keeps the preference and applies it once the window is
        // restored; until then its corners are square.
        result.report.shape = WindowCornerShape::square;
        result.report.complied = preference != WindowCornerPreference::round;
    } else {
        result.report.shape = WindowCornerShape::rounded;
        result.report.radius = windows_corner_radius;
        result.report.complied = true;
    }
    return result;
}

WindowsWindowState read_windows_window_state(HWND window) noexcept {
    WindowsWindowState state{};
    if (window == nullptr || IsIconic(window) != FALSE) return state;
    state.maximized = IsZoomed(window) != FALSE;
    RECT frame{};
    if (GetWindowRect(window, &frame) == FALSE) return state;
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    const bool captioned = (style & WS_CAPTION) == WS_CAPTION;
    if (!captioned && !state.maximized) {
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (monitor != nullptr && GetMonitorInfoW(monitor, &info) != FALSE) {
            const RECT& screen = info.rcMonitor;
            state.full_screen = frame.left <= screen.left && frame.top <= screen.top &&
                                frame.right >= screen.right && frame.bottom >= screen.bottom;
        }
    }
    if (state.maximized || state.full_screen) return state;
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(window, &placement) == FALSE) return state;
    // The restore rectangle is in workspace coordinates, which a taskbar on
    // the top or left edge offsets from the screen's: only sizes compare.
    const RECT& normal = placement.rcNormalPosition;
    const LONG frame_width = frame.right - frame.left;
    const LONG frame_height = frame.bottom - frame.top;
    const LONG normal_width = normal.right - normal.left;
    const LONG normal_height = normal.bottom - normal.top;
    state.snapped = frame_width != normal_width || frame_height != normal_height;
    return state;
}

} // namespace gui_forms::host::detail
