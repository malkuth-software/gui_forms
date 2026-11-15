#include "x11_tray_icons.hpp"
#include "x11_foreign_window.hpp"
#include "../../../core/host/image/host_image_pixels.hpp"

#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>

namespace gui_forms::host::linux_detail {
namespace {

// System Tray Protocol and XEmbed constants (freedesktop specifications).
constexpr long system_tray_request_dock = 0;
constexpr unsigned long xembed_version = 0;
constexpr unsigned long xembed_mapped = 1;
// The size an icon window starts at before the tray lays it out.
constexpr unsigned default_side = 22U;
// Two releases of the primary button closer than this are a double click.
constexpr Time double_click_milliseconds = 400U;

Atom tray_selection(Display* display) {
    const std::string name = "_NET_SYSTEM_TRAY_S" + std::to_string(DefaultScreen(display));
    const Atom atom = XInternAtom(display, name.c_str(), False);
    return atom;
}

// The position of the lowest set bit of a colour mask, and the mask's width.
unsigned mask_shift(unsigned long mask) noexcept {
    unsigned shift = 0U;
    while (mask != 0UL && (mask & 1UL) == 0UL) {
        mask >>= 1U;
        ++shift;
    }
    return shift;
}

unsigned mask_bits(unsigned long mask) noexcept {
    unsigned bits = 0U;
    while (mask != 0UL) {
        bits += static_cast<unsigned>(mask & 1UL);
        mask >>= 1U;
    }
    return bits;
}

unsigned long channel(unsigned value, unsigned long mask) noexcept {
    const unsigned bits = mask_bits(mask);
    const unsigned long scaled = bits >= 8U ? value : (value >> (8U - bits));
    return (scaled << mask_shift(mask)) & mask;
}

} // namespace

X11TrayIcons::X11TrayIcons(Display* display, Sink sink) noexcept : display_(display), sink_(sink) {}

X11TrayIcons::~X11TrayIcons() {
    for (const Icon& icon : icons_) {
        if (icon.window != None) XDestroyWindow(display_, icon.window);
    }
    icons_.clear();
    XFlush(display_);
}

bool X11TrayIcons::tray_present(Display* display) noexcept {
    if (display == nullptr) return false;
    const bool present = XGetSelectionOwner(display, tray_selection(display)) != None;
    return present;
}

HostServiceStatus X11TrayIcons::add(HostTrayIconId icon_id, const HostTrayIconRequest& request) {
    const ::Window tray = XGetSelectionOwner(display_, tray_selection(display_));
    if (tray == None) return {HostServiceError::backend_failure};
    const int screen = DefaultScreen(display_);
    const ::Window window = XCreateSimpleWindow(display_, RootWindow(display_, screen), 0, 0, default_side,
                                                default_side, 0, 0, 0);
    if (window == None) return {HostServiceError::backend_failure};
    // The tray's own background shows through where the icon is transparent.
    XSetWindowBackgroundPixmap(display_, window, ParentRelative);
    XSelectInput(display_, window, ExposureMask | ButtonPressMask | ButtonReleaseMask | StructureNotifyMask);
    const unsigned long xembed_info[2] = {xembed_version, xembed_mapped};
    XChangeProperty(display_, window, XInternAtom(display_, "_XEMBED_INFO", False),
                    XInternAtom(display_, "_XEMBED_INFO", False), 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(xembed_info), 2);
    XEvent dock{};
    dock.xclient.type = ClientMessage;
    dock.xclient.window = tray;
    dock.xclient.message_type = XInternAtom(display_, "_NET_SYSTEM_TRAY_OPCODE", False);
    dock.xclient.format = 32;
    dock.xclient.data.l[0] = CurrentTime;
    dock.xclient.data.l[1] = system_tray_request_dock;
    dock.xclient.data.l[2] = static_cast<long>(window);
    {
        // The tray may exit between the owner query and the request.
        const ForeignWindowScope guard(display_, tray);
        XSendEvent(display_, tray, False, NoEventMask, &dock);
    }
    XFlush(display_);
    Icon icon;
    icon.id = icon_id;
    icon.window = window;
    icon.images = request.images;
    icon.side = default_side;
    icons_.push_back(std::move(icon));
    return {};
}

HostServiceStatus X11TrayIcons::update(HostTrayIconId icon_id, const HostTrayIconRequest& request) {
    for (Icon& icon : icons_) {
        if (icon.id != icon_id) continue;
        // A tray that exited took the icon's window with it.
        if (icon.window == None) return {HostServiceError::backend_failure};
        icon.images = request.images;
        XClearWindow(display_, icon.window);
        draw(icon);
        XFlush(display_);
        return {};
    }
    return {HostServiceError::backend_failure};
}

void X11TrayIcons::remove(HostTrayIconId icon_id) noexcept {
    for (std::size_t index = 0; index < icons_.size(); ++index) {
        if (icons_[index].id != icon_id) continue;
        if (icons_[index].window != None) {
            XDestroyWindow(display_, icons_[index].window);
            XFlush(display_);
        }
        icons_.erase(icons_.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }
}

X11TrayIcons::Icon* X11TrayIcons::find_window(::Window window) noexcept {
    for (Icon& icon : icons_) {
        if (icon.window == window) return &icon;
    }
    return nullptr;
}

bool X11TrayIcons::handle_event(const XEvent& event) {
    Icon* const icon = find_window(event.xany.window);
    if (icon == nullptr) return false;
    if (event.type == DestroyNotify && event.xdestroywindow.window == (*icon).window) {
        // A tray that exits without a save-set destroys the docked window
        // (A-gf-host-01-F3); the icon stays listed, so remove still finds
        // it, but no request names the destroyed window again.
        (*icon).window = None;
    } else if (event.type == Expose && event.xexpose.count == 0) {
        draw(*icon);
    } else if (event.type == ConfigureNotify) {
        const int smaller = std::min(event.xconfigure.width, event.xconfigure.height);
        (*icon).side = static_cast<unsigned>(std::max(1, smaller));
    } else if (event.type == ButtonRelease) {
        HostTrayIconEvent published{};
        published.icon_id = (*icon).id;
        if (event.xbutton.button == Button3) {
            published.action = HostTrayIconAction::context;
        } else if (event.xbutton.button == Button1) {
            const Time elapsed = event.xbutton.time - (*icon).last_release;
            const bool second = (*icon).last_release != 0U && elapsed < double_click_milliseconds;
            published.action = second ? HostTrayIconAction::double_click : HostTrayIconAction::click;
            (*icon).last_release = second ? 0U : event.xbutton.time;
        } else {
            return true;
        }
        sink_(published);
    }
    return true;
}

// Draws the image nearest the window's size, centred, in the default visual.
// Pixels at least half opaque are drawn; the rest leave the tray's background,
// since a default-visual window has no alpha channel.
void X11TrayIcons::draw(const Icon& icon) const {
    if (icon.images.empty()) return;
    const HostImage& image = icon.images[select_tray_icon_image(icon.images, icon.side)];
    const unsigned width = std::min<unsigned>(image.width, icon.side);
    const unsigned height = std::min<unsigned>(image.height, icon.side);
    if (width == 0U || height == 0U) return;
    const int screen = DefaultScreen(display_);
    Visual* const visual = DefaultVisual(display_, screen);
    const int depth = DefaultDepth(display_, screen);
    // XDestroyImage frees the pixel buffer with free(), so it comes from malloc.
    char* const buffer = static_cast<char*>(std::malloc(static_cast<std::size_t>(width) * height * 4U));
    if (buffer == nullptr) return;
    XImage* const colour = XCreateImage(display_, visual, static_cast<unsigned>(depth), ZPixmap, 0, buffer, width,
                                        height, 32, 0);
    if (colour == nullptr) {
        std::free(buffer);
        return;
    }
    // One bit per pixel, rows padded to whole bytes, least significant bit first.
    const unsigned mask_row = (width + 7U) / 8U;
    std::vector<char> mask_bits_storage(static_cast<std::size_t>(mask_row) * height, 0);
    for (unsigned row = 0; row < height; ++row) {
        const std::byte* const source = image.pixels.data() + static_cast<std::size_t>(row) * image.row_bytes;
        for (unsigned column = 0; column < width; ++column) {
            const std::size_t offset = static_cast<std::size_t>(column) * 4U;
            // Premultiplied BGRA8: the drawn colour is the straight one.
            const std::uint8_t alpha = std::to_integer<std::uint8_t>(source[offset + 3U]);
            const unsigned blue =
                gui_forms::detail::straight_channel(std::to_integer<std::uint8_t>(source[offset + 0U]), alpha);
            const unsigned green =
                gui_forms::detail::straight_channel(std::to_integer<std::uint8_t>(source[offset + 1U]), alpha);
            const unsigned red =
                gui_forms::detail::straight_channel(std::to_integer<std::uint8_t>(source[offset + 2U]), alpha);
            const unsigned long pixel = channel(red, (*visual).red_mask) | channel(green, (*visual).green_mask) |
                                        channel(blue, (*visual).blue_mask);
            XPutPixel(colour, static_cast<int>(column), static_cast<int>(row), pixel);
            if (alpha >= 128U) {
                const std::size_t bit = static_cast<std::size_t>(row) * mask_row + column / 8U;
                mask_bits_storage[bit] = static_cast<char>(mask_bits_storage[bit] | (1 << (column % 8U)));
            }
        }
    }
    const Pixmap mask = XCreateBitmapFromData(display_, icon.window, mask_bits_storage.data(), width, height);
    const int x = static_cast<int>((icon.side - width) / 2U);
    const int y = static_cast<int>((icon.side - height) / 2U);
    GC gc = XCreateGC(display_, icon.window, 0, nullptr);
    XSetClipMask(display_, gc, mask);
    XSetClipOrigin(display_, gc, x, y);
    XPutImage(display_, icon.window, gc, colour, 0, 0, x, y, width, height);
    XFreeGC(display_, gc);
    XFreePixmap(display_, mask);
    XDestroyImage(colour);
}

} // namespace gui_forms::host::linux_detail
