#include "windows_notify_icons.hpp"
#include "../../../core/failure/fail_fast.hpp"
#include "../../../core/host/image/host_image_pixels.hpp"

#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <cwchar>

namespace gui_forms::host::detail {
namespace {

constexpr wchar_t window_class_name[] = L"GUI.Forms.NotifyIcons";

// UTF-16 text from UTF-8 the base class already validated.
std::wstring wide_text(std::string_view text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

// Copies `text` into a fixed shell field of `capacity` characters, cut to
// fit and terminated.
void copy_field(wchar_t* field, std::size_t capacity, const std::wstring& text) noexcept {
    const std::size_t count = std::min(text.size(), capacity - 1U);
    if (count > 0U) std::memcpy(field, text.data(), count * sizeof(wchar_t));
    field[count] = L'\0';
}

NOTIFYICONDATAW base_data(HWND window, UINT shell_id) noexcept {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(NOTIFYICONDATAW);
    data.hWnd = window;
    data.uID = shell_id;
    return data;
}

} // namespace

HICON create_notify_icon_handle(std::span<const HostImage> images, int wanted_side) noexcept {
    if (images.empty()) return nullptr;
    const std::size_t chosen = select_tray_icon_image(images, static_cast<std::uint32_t>(std::max(1, wanted_side)));
    const HostImage& image = images[chosen];
    const int width = static_cast<int>(image.width);
    const int height = static_cast<int>(image.height);
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(BITMAPV5HEADER);
    header.bV5Width = width;
    header.bV5Height = -height;  // top-down rows
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000U;
    header.bV5GreenMask = 0x0000FF00U;
    header.bV5BlueMask = 0x000000FFU;
    header.bV5AlphaMask = 0xFF000000U;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP colour = CreateDIBSection(screen, reinterpret_cast<const BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits,
                                      nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (colour == nullptr || bits == nullptr) {
        if (colour != nullptr) DeleteObject(colour);
        return nullptr;
    }
    // Premultiplied BGRA rows to straight BGRA rows: icons take straight alpha.
    std::byte* const destination = static_cast<std::byte*>(bits);
    const std::size_t row_length = static_cast<std::size_t>(width) * 4U;
    for (int row = 0; row < height; ++row) {
        const std::span<const std::byte> source(
            image.pixels.data() + static_cast<std::size_t>(row) * image.row_bytes, row_length);
        const std::span<std::byte> target(destination + static_cast<std::size_t>(row) * row_length, row_length);
        gui_forms::detail::premultiplied_bgra_to_straight_bgra(source, target, static_cast<std::uint32_t>(width));
    }
    HBITMAP mask = CreateBitmap(width, height, 1, 1, nullptr);
    if (mask == nullptr) {
        DeleteObject(colour);
        return nullptr;
    }
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = colour;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(colour);
    return icon;
}

WindowsNotifyIcons::WindowsNotifyIcons(Sink sink) noexcept : sink_(sink) {}

WindowsNotifyIcons::~WindowsNotifyIcons() {
    for (const Icon& icon : icons_) {
        NOTIFYICONDATAW data = base_data(window_, icon.shell_id);
        Shell_NotifyIconW(NIM_DELETE, &data);
        if (icon.handle != nullptr) DestroyIcon(icon.handle);
    }
    icons_.clear();
    if (window_ != nullptr) {
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        DestroyWindow(window_);
    }
}

bool WindowsNotifyIcons::ensure_window() noexcept {
    if (window_ != nullptr) return true;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(WNDCLASSEXW);
    window_class.lpfnWndProc = &WindowsNotifyIcons::window_procedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = window_class_name;
    // Registering twice fails harmlessly; the class outlives every instance.
    RegisterClassExW(&window_class);
    taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
    // A hidden top-level window, never shown: it receives the broadcast
    // TaskbarCreated message that a message-only window would miss.
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW, window_class_name, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                              instance, this);
    return window_ != nullptr;
}

LRESULT CALLBACK WindowsNotifyIcons::window_procedure(HWND window, UINT message, WPARAM w_parameter,
                                                      LPARAM l_parameter) {
    const LRESULT result = gui_forms::detail::call_failing_fast(
        "windows.notify_icons.window_procedure", &WindowsNotifyIcons::answer_window_procedure, window, message,
        w_parameter, l_parameter);
    return result;
}

LRESULT WindowsNotifyIcons::answer_window_procedure(HWND window, UINT message, WPARAM w_parameter,
                                                    LPARAM l_parameter) {
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* const creation = reinterpret_cast<const CREATESTRUCTW*>(l_parameter);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>((*creation).lpCreateParams));
    }
    WindowsNotifyIcons* const owner = reinterpret_cast<WindowsNotifyIcons*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (owner == nullptr) return DefWindowProcW(window, message, w_parameter, l_parameter);
    if (message == callback_message) {
        (*owner).handle_callback(w_parameter, l_parameter);
        return 0;
    }
    if ((*owner).taskbar_created_ != 0U && message == (*owner).taskbar_created_) {
        // A restarted shell has no icons: show each one again.
        for (const Icon& icon : (*owner).icons_) static_cast<void>((*owner).show(icon));
        return 0;
    }
    return DefWindowProcW(window, message, w_parameter, l_parameter);
}

void WindowsNotifyIcons::handle_callback(WPARAM, LPARAM l_parameter) {
    const UINT event = LOWORD(static_cast<DWORD>(l_parameter));
    const UINT shell_id = HIWORD(static_cast<DWORD>(l_parameter));
    // Shell identities are unique among live icons (next_shell_id).
    const Icon* icon = nullptr;
    for (const Icon& candidate : icons_) {
        if (candidate.shell_id != shell_id) continue;
        icon = &candidate;
        break;
    }
    if (icon == nullptr) return;
    HostTrayIconEvent published{};
    published.icon_id = (*icon).id;
    if (event == NIN_SELECT || event == NIN_KEYSELECT) {
        published.action = HostTrayIconAction::click;
    } else if (event == WM_LBUTTONDBLCLK) {
        published.action = HostTrayIconAction::double_click;
    } else if (event == WM_CONTEXTMENU) {
        published.action = HostTrayIconAction::context;
    } else if (event == NIN_BALLOONUSERCLICK) {
        published.action = HostTrayIconAction::notification_clicked;
    } else if (event == NIN_BALLOONTIMEOUT || event == NIN_BALLOONHIDE) {
        published.action = HostTrayIconAction::notification_closed;
    } else {
        return;
    }
    sink_(published);
}

WindowsNotifyIcons::Icon* WindowsNotifyIcons::find(HostTrayIconId icon_id) noexcept {
    for (Icon& icon : icons_) {
        if (icon.id == icon_id) return &icon;
    }
    return nullptr;
}

std::optional<UINT> WindowsNotifyIcons::shell_id(HostTrayIconId icon_id) const noexcept {
    for (const Icon& icon : icons_) {
        if (icon.id == icon_id) return icon.shell_id;
    }
    return std::nullopt;
}

// The lowest 16-bit shell identity no live icon uses (at most 16 are live).
UINT WindowsNotifyIcons::next_shell_id() const noexcept {
    for (UINT candidate = 1U; candidate < 0xFFFFU; ++candidate) {
        bool used = false;
        for (const Icon& icon : icons_) {
            if (icon.shell_id != candidate) continue;
            used = true;
            break;
        }
        if (!used) return candidate;
    }
    return 0U;
}

bool WindowsNotifyIcons::show(const Icon& icon) noexcept {
    NOTIFYICONDATAW data = base_data(window_, icon.shell_id);
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = callback_message;
    data.hIcon = icon.handle;
    copy_field(data.szTip, sizeof(data.szTip) / sizeof(data.szTip[0]), icon.tooltip);
    // A shell that still has the icon refuses NIM_ADD; then it is modified.
    BOOL shown = Shell_NotifyIconW(NIM_ADD, &data);
    if (!shown) shown = Shell_NotifyIconW(NIM_MODIFY, &data);
    if (!shown) return false;
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
    return true;
}

HostServiceStatus WindowsNotifyIcons::add(HostTrayIconId icon_id, const HostTrayIconRequest& request) {
    if (!ensure_window()) return {HostServiceError::backend_failure};
    Icon icon;
    icon.id = icon_id;
    icon.shell_id = next_shell_id();
    icon.tooltip = wide_text(request.tooltip);
    icon.handle = create_notify_icon_handle(request.images, GetSystemMetrics(SM_CXSMICON));
    if (icon.shell_id == 0U || icon.handle == nullptr) {
        if (icon.handle != nullptr) DestroyIcon(icon.handle);
        return {HostServiceError::backend_failure};
    }
    if (!show(icon)) {
        DestroyIcon(icon.handle);
        return {HostServiceError::backend_failure};
    }
    icons_.push_back(std::move(icon));
    return {};
}

HostServiceStatus WindowsNotifyIcons::update(HostTrayIconId icon_id, const HostTrayIconRequest& request) {
    Icon* const icon = find(icon_id);
    if (icon == nullptr) return {HostServiceError::backend_failure};
    HICON replacement = create_notify_icon_handle(request.images, GetSystemMetrics(SM_CXSMICON));
    if (replacement == nullptr) return {HostServiceError::backend_failure};
    const std::wstring tooltip = wide_text(request.tooltip);
    NOTIFYICONDATAW data = base_data(window_, (*icon).shell_id);
    data.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.hIcon = replacement;
    copy_field(data.szTip, sizeof(data.szTip) / sizeof(data.szTip[0]), tooltip);
    if (!Shell_NotifyIconW(NIM_MODIFY, &data)) {
        DestroyIcon(replacement);
        return {HostServiceError::backend_failure};
    }
    // Publish the replacement only once the shell took it.
    DestroyIcon((*icon).handle);
    (*icon).handle = replacement;
    (*icon).tooltip = tooltip;
    return {};
}

void WindowsNotifyIcons::remove(HostTrayIconId icon_id) noexcept {
    for (std::size_t index = 0; index < icons_.size(); ++index) {
        if (icons_[index].id != icon_id) continue;
        NOTIFYICONDATAW data = base_data(window_, icons_[index].shell_id);
        Shell_NotifyIconW(NIM_DELETE, &data);
        if (icons_[index].handle != nullptr) DestroyIcon(icons_[index].handle);
        icons_.erase(icons_.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }
}

HostServiceStatus WindowsNotifyIcons::notify(HostTrayIconId icon_id, const HostTrayNotificationRequest& request) {
    Icon* const icon = find(icon_id);
    if (icon == nullptr) return {HostServiceError::backend_failure};
    NOTIFYICONDATAW data = base_data(window_, (*icon).shell_id);
    data.uFlags = NIF_INFO;
    copy_field(data.szInfoTitle, sizeof(data.szInfoTitle) / sizeof(data.szInfoTitle[0]), wide_text(request.title));
    copy_field(data.szInfo, sizeof(data.szInfo) / sizeof(data.szInfo[0]), wide_text(request.text));
    data.uTimeout = request.timeout_milliseconds;
    if (request.icon == HostTrayNotificationIcon::information) {
        data.dwInfoFlags = NIIF_INFO;
    } else if (request.icon == HostTrayNotificationIcon::warning) {
        data.dwInfoFlags = NIIF_WARNING;
    } else if (request.icon == HostTrayNotificationIcon::error) {
        data.dwInfoFlags = NIIF_ERROR;
    } else {
        data.dwInfoFlags = NIIF_NONE;
    }
    const BOOL shown = Shell_NotifyIconW(NIM_MODIFY, &data);
    if (!shown) return {HostServiceError::backend_failure};
    return {};
}

HostTrayMenuResult WindowsNotifyIcons::show_menu(HostTrayIconId icon_id, const HostTrayMenuRequest& request) {
    HostTrayMenuResult result;
    if (find(icon_id) == nullptr || window_ == nullptr) {
        result.status = {HostServiceError::backend_failure};
        return result;
    }
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        result.status = {HostServiceError::backend_failure};
        return result;
    }
    for (std::size_t index = 0; index < request.items.size(); ++index) {
        const HostTrayMenuItem& item = request.items[index];
        if (item.separator) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        UINT flags = MF_STRING;
        if (!item.enabled) flags |= MF_GRAYED;
        if (item.checked) flags |= MF_CHECKED;
        const std::wstring text = wide_text(item.text);
        // Command identities start at 1: TrackPopupMenuEx returns 0 for none.
        AppendMenuW(menu, flags, static_cast<UINT_PTR>(index + 1U), text.c_str());
    }
    POINT position{};
    GetCursorPos(&position);
    // The menu closes on a click elsewhere only while its window is in the
    // foreground (Shell_NotifyIcon documentation).
    SetForegroundWindow(window_);
    const BOOL command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, position.x,
                                          position.y, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (command > 0 && static_cast<std::size_t>(command) <= request.items.size()) {
        result.chosen_id = request.items[static_cast<std::size_t>(command) - 1U].id;
    }
    return result;
}

} // namespace gui_forms::host::detail
