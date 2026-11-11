// The X11 host at Xft.dpi 192 (audit N66, option a): the desktop's density
// from the root window's RESOURCE_MANAGER becomes every monitor's scale; a
// window opens at scale 2 with a raster and a native window of twice its
// logical size, draws its controls at twice their size on the server, takes
// pointer positions back to logical units at the boundary, publishes its
// frame in desktop logical units, and re-rasters through HostScaleEvent when
// the density changes during the run (to 96, then 144). The splash shown
// before the run opens at the same scale.
//
// Runs under Xvfb (CI's xvfb-run, or the Linux VM's). Serial: it rewrites the
// display's RESOURCE_MANAGER, which every other runtime on the display reads,
// and restores it before it returns.
#include "../src/core/application/splashscreen_surface.hpp"
#include "../src/host/linux/application/linux_host_internal.hpp"
#include "gui_forms/basic_controls.hpp"

#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <bit>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace gui_forms;
using namespace gui_forms::host::linux_detail;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

// The root window's RESOURCE_MANAGER, saved when made and restored (or
// deleted, if there was none) when destroyed, through its own connection.
class ResourceDatabase final {
public:
  ResourceDatabase() : display_(XOpenDisplay(nullptr)) {
    require(display_ != nullptr, "cannot connect to the X server");
    Atom type = None;
    int format = 0;
    unsigned long items = 0UL;
    unsigned long after = 0UL;
    unsigned char *data = nullptr;
    const int status = XGetWindowProperty(display_, DefaultRootWindow(display_), XA_RESOURCE_MANAGER, 0L,
                                          65536L, False, XA_STRING, &type, &format, &items, &after, &data);
    if (status == Success && data != nullptr && type == XA_STRING && format == 8) {
      had_previous_ = true;
      previous_.assign(reinterpret_cast<const char *>(data), static_cast<std::size_t>(items));
    }
    if (data != nullptr)
      XFree(data);
  }
  ResourceDatabase(const ResourceDatabase &) = delete;
  ResourceDatabase &operator=(const ResourceDatabase &) = delete;
  ~ResourceDatabase() {
    if (had_previous_)
      write_text(previous_);
    else
      XDeleteProperty(display_, DefaultRootWindow(display_), XA_RESOURCE_MANAGER);
    XSync(display_, False);
    XCloseDisplay(display_);
  }
  // The saved database with `entry` appended, so the entry is the last
  // Xft.dpi and counts.
  void write_entry(std::string_view entry) {
    std::string text = previous_;
    if (!text.empty() && text.back() != '\n')
      text.push_back('\n');
    text.append(entry);
    write_text(text);
  }

private:
  void write_text(const std::string &text) {
    XChangeProperty(display_, DefaultRootWindow(display_), XA_RESOURCE_MANAGER, XA_STRING, 8,
                    PropModeReplace, reinterpret_cast<const unsigned char *>(text.data()),
                    static_cast<int>(text.size()));
    XSync(display_, False);
  }

  Display *display_{};
  bool had_previous_{};
  std::string previous_;
};

// A block of one colour.
class ColorBlock final : public Control {
public:
  ColorBlock(StableId id, const Color color) : Control(std::move(id)), color_(color) {}
  void on_paint(Painter &painter, Rect) override { painter.fill_rect(client_rectangle(), color_); }

private:
  Color color_{};
};

// The window's root: a grey ground that records where the last press
// landed, in the window's logical units.
class PressProbe final : public Control {
public:
  explicit PressProbe(StableId id) : Control(std::move(id)) {}
  void on_paint(Painter &painter, Rect) override { painter.fill_rect(client_rectangle(), Color::rgba(128, 128, 128)); }
  void on_pointer(PointerEvent &event) override {
    if (event.action != PointerAction::down)
      return;
    pressed = event.position;
    ++presses;
  }
  Point pressed{};
  unsigned presses{};
};

// One 8-bit channel of `pixel` under `mask`.
unsigned channel(unsigned long pixel, unsigned long mask) {
  if (mask == 0UL)
    return 0U;
  const int shift = std::countr_zero(mask);
  const unsigned long value = (pixel & mask) >> shift;
  const unsigned long maximum = mask >> shift;
  const unsigned scaled = static_cast<unsigned>((value * 255UL + maximum / 2UL) / maximum);
  return scaled;
}

// The server's pixel at device (`x`, `y`) of the window, as red, green and
// blue.
Color server_pixel(const NativeWindow &window, int x, int y) {
  Display *const display = window.runtime.display;
  XSync(display, False);
  XImage *const image = XGetImage(display, window.xid, x, y, 1U, 1U, AllPlanes, ZPixmap);
  require(image != nullptr, "XGetImage refused the window");
  const unsigned long pixel = XGetPixel(image, 0, 0);
  const Color color = Color::rgba(static_cast<std::uint8_t>(channel(pixel, (*image).red_mask)),
                                  static_cast<std::uint8_t>(channel(pixel, (*image).green_mask)),
                                  static_cast<std::uint8_t>(channel(pixel, (*image).blue_mask)));
  XDestroyImage(image);
  return color;
}

// Turns the run until `window` reaches `scale` with a native size of
// `width` x `height` device pixels, presented; at most 100 turns.
void settle(Runtime &runtime, const NativeWindow &window, double scale, int width, int height) {
  for (int turn = 0; turn < 100; ++turn) {
    runtime.step(10);
    const bool settled = window.scale == scale && window.device_width == width &&
                         window.device_height == height && window.damage.empty() &&
                         window.raster.pixel_width() == static_cast<std::uint32_t>(width);
    if (settled)
      return;
  }
  throw std::runtime_error("the window never settled at its scale");
}

// A primary press and release at device (`x`, `y`) of the window, as the
// server would deliver them.
void press(const NativeWindow &window, int x, int y) {
  Display *const display = window.runtime.display;
  XEvent event{};
  event.xbutton.type = ButtonPress;
  event.xbutton.display = display;
  event.xbutton.window = window.xid;
  event.xbutton.root = DefaultRootWindow(display);
  event.xbutton.x = x;
  event.xbutton.y = y;
  event.xbutton.button = Button1;
  event.xbutton.same_screen = True;
  event.xbutton.time = 1000;
  XSendEvent(display, window.xid, False, ButtonPressMask, &event);
  event.xbutton.type = ButtonRelease;
  event.xbutton.time = 1100;
  XSendEvent(display, window.xid, False, ButtonReleaseMask, &event);
  XFlush(display);
}

// The splash window on `display`: a child of the root typed
// _NET_WM_WINDOW_TYPE_SPLASH, or None.
::Window find_splash_window(Display *display) {
  const Atom type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
  const Atom splash = XInternAtom(display, "_NET_WM_WINDOW_TYPE_SPLASH", False);
  ::Window root_return = None;
  ::Window parent_return = None;
  ::Window *children = nullptr;
  unsigned count = 0U;
  ::Window found = None;
  if (XQueryTree(display, DefaultRootWindow(display), &root_return, &parent_return, &children, &count) == 0)
    return found;
  for (unsigned index = 0U; index < count && found == None; ++index) {
    Atom actual = None;
    int format = 0;
    unsigned long items = 0UL;
    unsigned long after = 0UL;
    unsigned char *data = nullptr;
    const int status = XGetWindowProperty(display, children[index], type, 0L, 1L, False, XA_ATOM, &actual,
                                          &format, &items, &after, &data);
    if (status == Success && data != nullptr && actual == XA_ATOM && format == 32 && items == 1UL) {
      Atom named = None;
      std::memcpy(&named, data, sizeof(named));
      if (named == splash)
        found = children[index];
    }
    if (data != nullptr)
      XFree(data);
  }
  if (children != nullptr)
    XFree(children);
  return found;
}

// The splash shown before any run takes the desktop's density too: at
// Xft.dpi 192 a 200 x 100 splash is a 400 x 200 device window, centred.
void check_splash_scale() {
  std::unique_ptr<gui_forms::detail::SplashscreenSurface> surface =
      gui_forms::host::open_platform_splashscreen(Size{200.0, 100.0});
  require(surface != nullptr, "the X11 splash opens");
  gui_forms::detail::SplashscreenFrame frame{};
  frame.size = Size{200.0, 100.0};
  frame.status = "Loading";
  frame.progress = 0.5;
  require((*surface).present(frame), "the splash presents a frame at scale 2");
  Display *const display = XOpenDisplay(nullptr);
  require(display != nullptr, "cannot connect to the X server");
  const ::Window splash = find_splash_window(display);
  XWindowAttributes attributes{};
  const bool measured = splash != None && XGetWindowAttributes(display, splash, &attributes) != 0;
  const int screen = DefaultScreen(display);
  const int centred_x = (DisplayWidth(display, screen) - 400) / 2;
  const int centred_y = (DisplayHeight(display, screen) - 200) / 2;
  XCloseDisplay(display);
  require(measured && attributes.width == 400 && attributes.height == 200,
          "the splash window is twice its logical size");
  require(attributes.x == centred_x && attributes.y == centred_y, "the splash is centred in device pixels");
  surface.reset();
}

void run() {
  ResourceDatabase resources;
  resources.write_entry("Xft.dpi:\t192\n");
  check_splash_scale();
  Runtime runtime;
  require(runtime.desktop_scale() == 2.0, "Xft.dpi 192 is desktop scale 2");
  require(!runtime.device_monitors.empty(), "the runtime knows its monitors");
  for (const HostMonitor &monitor : runtime.device_monitors)
    require(monitor.scale == 2.0, "every monitor carries the desktop's scale");

  const Color red = Color::rgba(255, 0, 0);
  const Color grey = Color::rgba(128, 128, 128);
  const std::shared_ptr<PressProbe> root = make_control<PressProbe>(StableId("hidpi.root"));
  const std::shared_ptr<ColorBlock> block = make_control<ColorBlock>(StableId("hidpi.block"), red);
  (*block).set_requested_bounds({10.0, 10.0, 40.0, 30.0});
  (*root).add_child(block);
  host::LinuxApplicationWindow entry;
  entry.stable_id = "hidpi";
  entry.options.title = "GUI.Forms X11 HiDPI check";
  entry.options.initial_size = {200, 150};
  entry.options.print_metrics_on_close = false;
  entry.model = std::make_unique<gui_forms::Window>(root, entry.options.initial_size);
  const std::shared_ptr<NativeWindow> window = runtime.add(std::move(entry));
  (*window).ready();
  gui_forms::Window &model = *(*window).entry.model;

  // Attach at scale 2: the logical size stays, the device size doubles.
  require((*window).scale == 2.0 && model.scale() == 2.0, "the window attaches at scale 2");
  settle(runtime, *window, 2.0, 400, 300);
  require((*window).visible, "the window is shown");
  require((*window).size == Size{200.0, 150.0}, "the logical size is the one asked for");
  XWindowAttributes attributes{};
  require(XGetWindowAttributes(runtime.display, (*window).xid, &attributes) != 0 && attributes.width == 400 &&
              attributes.height == 300,
          "the X window is twice the logical size");
  require((*window).raster.pixel_height() == 300U, "the raster is twice the logical size");

  // The block covers logical (10, 10) to (50, 40): device (20, 20) to (100, 80).
  require(server_pixel(*window, 50, 40) == red, "the block is drawn at twice its size");
  require(server_pixel(*window, 98, 78) == red, "the block's last device pixels are its own");
  require(server_pixel(*window, 102, 82) == grey, "the ground begins past the block's doubled edge");
  require(server_pixel(*window, 19, 19) == grey, "the ground lies before the block's doubled origin");

  // Pointer positions convert at the boundary: device (300, 200) is logical
  // (150, 100).
  press(*window, 300, 200);
  for (int turn = 0; turn < 20 && (*root).presses == 0U; ++turn)
    runtime.step(10);
  require((*root).presses == 1U, "the press reached the window");
  require((*root).pressed == Point{150.0, 100.0}, "the press arrives in logical units");

  // The frame is in desktop logical units: without a window manager the
  // window stays at device (60, 60) on a monitor at the origin, logical
  // (30, 30), with its logical size.
  const HostWindowFrame frame = (*window).frame();
  require(frame.content.width == 200.0 && frame.content.height == 150.0, "the frame has the logical size");
  require(frame.content.x == 30.0 && frame.content.y == 30.0, "the frame's origin is in desktop logical units");

  // A new density during the run: HostScaleEvent, the native size follows
  // so the logical size stays, and the raster re-rasters at the new scale.
  resources.write_entry("Xft.dpi:\t96\n");
  settle(runtime, *window, 1.0, 200, 150);
  require(model.scale() == 1.0, "the window took the new scale");
  require((*window).size == Size{200.0, 150.0}, "the logical size survives the scale change");
  require(!runtime.device_monitors.empty() && runtime.device_monitors.front().scale == 1.0,
          "the monitors carry the new scale");
  require(server_pixel(*window, 25, 20) == red && server_pixel(*window, 51, 41) == grey,
          "the block is drawn at its logical size after the change");

  // A fractional density: 144 dots per inch, scale 1.5.
  resources.write_entry("Xft.dpi:\t144\n");
  settle(runtime, *window, 1.5, 300, 225);
  require(model.scale() == 1.5, "the window took scale 1.5");
  require(server_pixel(*window, 74, 59) == red && server_pixel(*window, 76, 61) == grey,
          "the block is drawn at one and a half times its size");

  (*window).close(HostCloseReason::application, true);
  runtime.step(0);
}

} // namespace

int main() {
  try {
    run();
    std::cout << "X11 HiDPI: the splash and a window at Xft.dpi 192 present twice the logical size, take input "
                 "in logical units, and re-rasters on a density change\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "gui_forms_linux_hidpi_tests: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
