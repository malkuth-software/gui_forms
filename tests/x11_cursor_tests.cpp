#include "../src/surface/linux/services/x11_cursor.hpp"
#include <X11/extensions/Xfixes.h>
#include <iostream>
int main() {
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr || !XcursorSupportsARGB(display)) return 1;
    const gui_forms::CursorImagesPtr images = gui_forms::CursorImages::create(
        {{32, 32, 1, std::vector<gui_forms::Color>(1024, {200, 100, 50, 128})}}, .25, .75);
    const gui_forms::CursorImage raster = (*images).rasterize(1.25);
    const ::Cursor cursor = gui_forms::host::linux_detail::create_image_cursor(display, *images, raster);
    if (cursor == None) return 2;
    const ::Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 100, 100, 0, 0, 0);
    XDefineCursor(display, window, cursor);
    XMapWindow(display, window);
    XWarpPointer(display, None, window, 0, 0, 0, 0, 50, 50);
    XSync(display, False);
    XFixesCursorImage* actual = XFixesGetCursorImage(display);
    const bool valid = actual != nullptr && (*actual).width == 40 && (*actual).height == 40 &&
        (*actual).xhot == 10 && (*actual).yhot == 30 && ((*actual).pixels[0] & 0xffffffffUL) == 0x80643219UL;
    if (actual != nullptr) XFree(actual);
    XUndefineCursor(display, window);
    XFreeCursor(display, cursor);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    if (!valid) return 3;
    std::cout << "X11 native cursor pixels, premultiplication, fractional DPI and hotspot passed\n";
}
