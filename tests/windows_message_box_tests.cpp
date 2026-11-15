// The Windows host's native message box shows GUI.Forms' picture for its
// icon (HostMessageDialogRequest::icon_image; docs/user/hosts-and-
// capabilities.md, "Message boxes"):
// - with a picture and an icon, the box's icon control shows the picture
//   (a solid red square here), at the control's size;
// - without a picture, the box keeps the system icon;
// - with a picture and no icon, the box has no icon control and the call
//   still answers.
// Each box is real: a thread timer, dispatched by the box's own modal loop,
// finds the box, reads its icon control's pixels and presses OK. A
// foreground test (gui_forms_foreground_test): it runs on a private
// desktop on a development machine.
#include "../src/surface/windows/application/windows_host.cpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace gui_forms::host {
namespace {

constexpr wchar_t probe_title[] = L"GUI.Forms message icon probe";
constexpr UINT probe_period_milliseconds = 50U;
// How many timer ticks the probe waits for the box before it gives up.
constexpr unsigned maximum_probe_ticks = 200U;

// What the probe found in the open box.
struct MessageBoxProbe final {
    bool box_found{};
    bool icon_control_found{};
    // The icon control's picture at its centre, straight BGRA.
    std::uint8_t blue{};
    std::uint8_t green{};
    std::uint8_t red{};
    std::uint8_t alpha{};
    int icon_width{};
    int icon_height{};
    unsigned ticks{};
};

// The probe the timer fills; the timer procedure has no context argument.
MessageBoxProbe* active_probe = nullptr;

// EnumThreadWindows target: stops at the probe's box and stores it.
BOOL CALLBACK find_probe_box(HWND window, LPARAM found) {
    std::array<wchar_t, 64> title{};
    const int length = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
    if (length <= 0 || std::wstring_view(title.data(), static_cast<std::size_t>(length)) != probe_title) {
        return TRUE;
    }
    *reinterpret_cast<HWND*>(found) = window;
    return FALSE;
}

// Reads `icon`'s centre pixel and size into `probe`.
void read_icon(HICON icon, MessageBoxProbe& probe) {
    ICONINFO info{};
    if (GetIconInfo(icon, &info) == FALSE) return;
    BITMAP colour{};
    const bool described = info.hbmColor != nullptr && GetObjectW(info.hbmColor, sizeof(colour), &colour) != 0;
    if (described && colour.bmWidth > 0 && colour.bmHeight > 0) {
        probe.icon_width = colour.bmWidth;
        probe.icon_height = colour.bmHeight;
        BITMAPINFO format{};
        format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        format.bmiHeader.biWidth = colour.bmWidth;
        format.bmiHeader.biHeight = -colour.bmHeight;  // top-down rows
        format.bmiHeader.biPlanes = 1;
        format.bmiHeader.biBitCount = 32;
        format.bmiHeader.biCompression = BI_RGB;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(colour.bmWidth) *
                                         static_cast<std::size_t>(colour.bmHeight) * 4U);
        HDC screen = GetDC(nullptr);
        const int rows = GetDIBits(screen, info.hbmColor, 0U, static_cast<UINT>(colour.bmHeight), pixels.data(),
                                   &format, DIB_RGB_COLORS);
        ReleaseDC(nullptr, screen);
        if (rows == colour.bmHeight) {
            const std::size_t centre = (static_cast<std::size_t>(colour.bmHeight / 2) *
                                            static_cast<std::size_t>(colour.bmWidth) +
                                        static_cast<std::size_t>(colour.bmWidth / 2)) * 4U;
            probe.blue = pixels[centre];
            probe.green = pixels[centre + 1U];
            probe.red = pixels[centre + 2U];
            probe.alpha = pixels[centre + 3U];
        }
    }
    if (info.hbmColor != nullptr) DeleteObject(info.hbmColor);
    if (info.hbmMask != nullptr) DeleteObject(info.hbmMask);
}

// The thread timer: finds the box, reads its icon control and presses OK;
// gives up after maximum_probe_ticks (the box stays, and the test's
// timeout ends the run).
void CALLBACK probe_message_box(HWND, UINT, UINT_PTR timer, DWORD) {
    MessageBoxProbe* const probe = active_probe;
    if (probe == nullptr) return;
    ++(*probe).ticks;
    HWND box = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), find_probe_box, reinterpret_cast<LPARAM>(&box));
    if (box == nullptr) {
        if ((*probe).ticks >= maximum_probe_ticks) KillTimer(nullptr, timer);
        return;
    }
    KillTimer(nullptr, timer);
    (*probe).box_found = true;
    HWND control = nullptr;
    EnumChildWindows(box, find_message_icon_control, reinterpret_cast<LPARAM>(&control));
    if (control != nullptr) {
        (*probe).icon_control_found = true;
        const HICON icon = reinterpret_cast<HICON>(SendMessageW(control, STM_GETICON, 0, 0));
        if (icon != nullptr) read_icon(icon, *probe);
    }
    PostMessageW(box, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
}

struct MessageBoxFixture final {
    static void require(bool condition, const std::string& message) {
        if (!condition) throw std::runtime_error(message);
    }

    // A solid red square, premultiplied BGRA, as stock_alert_icon_image
    // delivers its pictures.
    static HostImage red_square(std::uint32_t side) {
        HostImage image{};
        image.width = side;
        image.height = side;
        image.row_bytes = static_cast<std::uint64_t>(side) * 4U;
        image.pixels.resize(static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 4U);
        for (std::size_t offset = 0U; offset < image.pixels.size(); offset += 4U) {
            image.pixels[offset] = std::byte{0};
            image.pixels[offset + 1U] = std::byte{0};
            image.pixels[offset + 2U] = std::byte{255};
            image.pixels[offset + 3U] = std::byte{255};
        }
        return image;
    }

    // Shows `request` and returns what the probe found.
    static MessageBoxProbe show(HostMessageDialogRequest request) {
        request.title = "GUI.Forms message icon probe";
        request.message = "The probe presses OK.";
        MessageBoxProbe probe{};
        active_probe = &probe;
        const UINT_PTR timer = SetTimer(nullptr, 0U, probe_period_milliseconds, probe_message_box);
        require(timer != 0U, "the probe timer starts");
        const HostDialogResult result = native_message_dialog(nullptr, 7U, request);
        KillTimer(nullptr, timer);
        active_probe = nullptr;
        require(result.status.accepted(), "the box answers");
        const HostMessageDialogResult* const value = std::get_if<HostMessageDialogResult>(&result.payload);
        require(value != nullptr && (*value).choice == HostDialogChoice::ok, "the probe's OK is the choice");
        require(probe.box_found, "the probe found the box");
        return probe;
    }

    static void run() {
        HostMessageDialogRequest pictured{};
        pictured.icon = HostMessageIcon::information;
        pictured.icon_image = red_square(32U);
        const MessageBoxProbe shown = show(pictured);
        require(shown.icon_control_found, "an information box has an icon control");
        require(shown.red > 200U && shown.green < 40U && shown.blue < 40U && shown.alpha > 200U,
                "the icon control shows GUI.Forms' picture");
        require(shown.icon_width > 0 && shown.icon_width == shown.icon_height, "the picture is square");

        HostMessageDialogRequest system{};
        system.icon = HostMessageIcon::information;
        const MessageBoxProbe kept = show(system);
        require(kept.icon_control_found, "a box without a picture has its icon control");
        require(!(kept.red > 200U && kept.green < 40U && kept.blue < 40U), "a box without a picture keeps the system icon");

        HostMessageDialogRequest no_icon{};
        no_icon.icon = HostMessageIcon::none;
        no_icon.icon_image = red_square(32U);
        const MessageBoxProbe plain = show(no_icon);
        require(!plain.icon_control_found, "a box without an icon has no icon control");

        require(active_message_icon == nullptr, "every scope restored the thread's replacement");
    }
};

} // namespace
} // namespace gui_forms::host

int main() {
    try {
        gui_forms::host::MessageBoxFixture::run();
    } catch (const std::exception& error) {
        std::cerr << "windows message box failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "windows message box tests passed\n";
    return EXIT_SUCCESS;
}
