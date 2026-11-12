#include "../src/host/linux/application/linux_host_internal.hpp"
#include "../src/core/host/image/host_image_pixels.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/scrollable_control/scrollable_control.hpp"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <optional>
#include <poll.h>
#include <span>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
using namespace gui_forms;
using namespace gui_forms::host::linux_detail;
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
struct Root final : Control {
  explicit Root(StableId id) : Control(std::move(id)) {
    allow_drop = true;
    focusable = true;
  }
  unsigned keys{};
  bool dropped{};
  void on_key_preview(KeyEvent &) override { ++keys; }
  void on_drag(DragEvent &event) override {
    if (event.items.size() != 1)
      return;
    const DragFileListData *files =
        std::get_if<DragFileListData>(&event.items[0]);
    if (files == nullptr ||
        (*files).paths_utf8 !=
            std::vector<std::string>{"/tmp/a b.png", "/tmp/café.png"})
      return;
    event.accepted_effect = DragEffect::copy;
    event.handled = true;
    if (event.action == DragAction::drop)
      dropped = true;
  }
};
struct Peer final {
  Display *display;
  ::Window window;
  Peer() : display(XOpenDisplay(nullptr)), window(0) {
    require(display != nullptr, "Peer cannot connect to X11");
    window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 2,
                                 2, 0, 0, 0);
    XSelectInput(display, window, PropertyChangeMask);
  }
  ~Peer() {
    XDestroyWindow(display, window);
    XCloseDisplay(display);
  }
  Atom atom(const char *name) const {
    return XInternAtom(display, name, False);
  }
  XEvent next() {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (XPending(display) == 0 &&
           std::chrono::steady_clock::now() < deadline) {
      pollfd fd{ConnectionNumber(display), POLLIN, 0};
      static_cast<void>(poll(&fd, 1, 20));
    }
    require(XPending(display) != 0, "Peer event timed out");
    XEvent event{};
    XNextEvent(display, &event);
    return event;
  }
  std::vector<std::byte> read(const char *target_name) {
    const Atom target = atom(target_name), property = atom("PEER_READ");
    XConvertSelection(display, atom("CLIPBOARD"), target, property, window,
                      CurrentTime);
    XFlush(display);
    bool incremental = false;
    std::vector<std::byte> result;
    for (;;) {
      XEvent event = next();
      if (event.type != SelectionNotify &&
          !(incremental && event.type == PropertyNotify &&
            event.xproperty.state == PropertyNewValue &&
            event.xproperty.atom == property))
        continue;
      if (event.type == SelectionNotify)
        require(event.xselection.property != None, "Clipboard target refused");
      Atom type{};
      int format{};
      unsigned long count{}, left{};
      unsigned char *data = nullptr;
      require(XGetWindowProperty(display, window, property, 0, 20000000, True,
                                 AnyPropertyType, &type, &format, &count, &left,
                                 &data) == Success &&
                  left == 0,
              "Peer property read failed");
      if (type == atom("INCR")) {
        incremental = true;
        XFree(data);
        XFlush(display);
        continue;
      }
      require(type == target && format == 8,
              "Peer received wrong selection type");
      const std::byte *first = reinterpret_cast<const std::byte *>(data);
      if (count != 0)
        result.insert(result.end(), first, first + count);
      XFree(data);
      XFlush(display);
      if (!incremental || count == 0)
        return result;
    }
  }
  void send(::Window target, const char *kind, long one, long two = 0,
            long three = 0, long four = 0) {
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = target;
    event.xclient.message_type = atom(kind);
    event.xclient.format = 32;
    event.xclient.data.l[0] = static_cast<long>(window);
    event.xclient.data.l[1] = one;
    event.xclient.data.l[2] = two;
    event.xclient.data.l[3] = three;
    event.xclient.data.l[4] = four;
    XSendEvent(display, target, False, NoEventMask, &event);
    XFlush(display);
  }
  void key(::Window target, KeySym symbol, unsigned state = 0) {
    XEvent event{};
    event.xkey.type = KeyPress;
    event.xkey.display = display;
    event.xkey.window = target;
    event.xkey.root = DefaultRootWindow(display);
    event.xkey.same_screen = True;
    event.xkey.state = state;
    event.xkey.keycode = XKeysymToKeycode(display, symbol);
    XSendEvent(display, target, False, KeyPressMask, &event);
    event.xkey.type = KeyRelease;
    XSendEvent(display, target, False, KeyReleaseMask, &event);
    XFlush(display);
  }
  // Serve either a direct or ICCCM INCR payload until the receiver completes
  // it.
  void serve(const char *selection, const char *target_name,
             const std::vector<std::byte> &bytes, int ready_fd,
             ::Window inject = 0) {
    XSetSelectionOwner(display, atom(selection), window, CurrentTime);
    XSync(display, False);
    require(XGetSelectionOwner(display, atom(selection)) == window,
            "Peer selection ownership failed");
    const char ready = 'R';
    require(write(ready_fd, &ready, 1) == 1, "Peer ready pipe failed");
    close(ready_fd);
    if (inject != 0)
      key(inject, XK_F2);
    bool transferring = false;
    ::Window receiver = 0;
    Atom property = None;
    std::size_t offset = 0;
    for (;;) {
      XEvent event = next();
      if (event.type == SelectionRequest) {
        const XSelectionRequestEvent request = event.xselectionrequest;
        XEvent response{};
        response.xselection.type = SelectionNotify;
        response.xselection.display = display;
        response.xselection.requestor = request.requestor;
        response.xselection.selection = request.selection;
        response.xselection.target = request.target;
        response.xselection.time = request.time;
        response.xselection.property = None;
        if (request.target == atom(target_name)) {
          receiver = request.requestor;
          property = request.property;
          if (bytes.size() > 64000) {
            XSelectInput(display, receiver, PropertyChangeMask);
            const unsigned long length = bytes.size();
            XChangeProperty(
                display, receiver, property, atom("INCR"), 32, PropModeReplace,
                reinterpret_cast<const unsigned char *>(&length), 1);
            transferring = true;
          } else
            XChangeProperty(
                display, receiver, property, request.target, 8, PropModeReplace,
                reinterpret_cast<const unsigned char *>(bytes.data()),
                static_cast<int>(bytes.size()));
          response.xselection.property = property;
        }
        XSendEvent(display, request.requestor, False, NoEventMask, &response);
        XFlush(display);
        if (response.xselection.property != None && !transferring) {
          XSync(display, False);
          return;
        }
      } else if (transferring && event.type == PropertyNotify &&
                 event.xproperty.window == receiver &&
                 event.xproperty.atom == property &&
                 event.xproperty.state == PropertyDelete) {
        const std::size_t length =
            std::min<std::size_t>(32768, bytes.size() - offset);
        XChangeProperty(
            display, receiver, property, atom(target_name), 8, PropModeReplace,
            reinterpret_cast<const unsigned char *>(bytes.data() + offset),
            static_cast<int>(length));
        offset += length;
        XFlush(display);
        if (length == 0) {
          XSync(display, False);
          return;
        }
      }
    }
  }
};
void child_result(bool pass) { _exit(pass ? 0 : 1); }
void wait_child(Runtime &runtime, pid_t child) {
  int status = 0;
  const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(12);
  for (;;) {
    const pid_t result = waitpid(child, &status, WNOHANG);
    if (result == child)
      break;
    if (result < 0 || std::chrono::steady_clock::now() >= deadline) {
      kill(child, SIGKILL);
      waitpid(child, &status, 0);
      throw std::runtime_error("Peer did not finish");
    }
    runtime.step(20);
  }
  require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "Peer protocol assertion failed");
}
std::vector<std::byte> patterned(std::size_t count) {
  std::vector<std::byte> bytes(count);
  for (std::size_t i = 0; i < count; ++i)
    bytes[i] = static_cast<std::byte>('a' + i % 26);
  return bytes;
}
// With `expected_pixels`, an image read must give exactly those pixels.
void incoming(Runtime &runtime, NativeWindow &window, const char *type,
              const std::vector<std::byte> &bytes, bool image,
              const std::vector<std::byte> *expected_pixels = nullptr) {
  int ready[2];
  require(pipe(ready) == 0, "Cannot create peer pipe");
  const pid_t child = fork();
  require(child >= 0, "Cannot fork peer");
  if (child == 0) {
    close(ready[0]);
    try {
      Peer peer;
      peer.serve("CLIPBOARD", type, bytes, ready[1], window.xid);
      child_result(true);
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
      child_result(false);
    }
  }
  close(ready[1]);
  char value{};
  require(read(ready[0], &value, 1) == 1, "Peer never ready");
  close(ready[0]);
  if (image) {
    const HostClipboardImageResult result =
        (*window.services).read_clipboard_image();
    require(result.image() != nullptr && (*result.image()).width == 256 &&
                (*result.image()).height == 256,
            "External PNG clipboard decode failed");
    require(expected_pixels == nullptr ||
                (*result.image()).pixels == *expected_pixels,
            "External clipboard image was not read exactly");
  } else if (std::string_view(type) == "text/uri-list" ||
             std::string_view(type) == "x-special/gnome-copied-files") {
    const HostClipboardFilesResult result = (*window.services).read_clipboard_files();
    require(result.status.accepted() && result.paths_utf8.size() == 1 &&
                result.paths_utf8.front() == "/tmp/Rainstar copied Ω.png",
            "External file clipboard did not decode its local URI");
  } else {
    const HostClipboardTextResult result =
        (*window.services).read_clipboard_text();
    require(result.has_text && result.text_utf8.size() == bytes.size() &&
                std::memcmp(result.text_utf8.data(), bytes.data(),
                            bytes.size()) == 0,
            "External INCR text decode failed");
  }
  wait_child(runtime, child);
}
::Window find_window(Display *display, ::Window root, const std::string &name) {
  char *title = nullptr;
  if (XFetchName(display, root, &title) != 0 && title != nullptr) {
    const bool equal = name == title;
    XFree(title);
    if (equal)
      return root;
  }
  ::Window returned{}, parent{}, *children = nullptr;
  unsigned count{};
  if (XQueryTree(display, root, &returned, &parent, &children, &count) == 0)
    return 0;
  ::Window found = 0;
  for (unsigned i = 0; i < count && found == 0; ++i)
    found = find_window(display, children[i], name);
  if (children != nullptr)
    XFree(children);
  return found;
}
void dialog(Runtime &runtime, NativeWindow &window,
            const std::string &directory, bool save) {
  const pid_t child = fork();
  require(child >= 0, "Cannot fork dialog peer");
  if (child == 0) {
    try {
      Peer peer;
      ::Window target = 0;
      for (unsigned i = 0; i < 300 && target == 0; ++i) {
        target = find_window(peer.display, DefaultRootWindow(peer.display),
                             "GUI.Forms service test");
        if (target == 0)
          usleep(10000);
      }
      require(target != 0, "Dialog did not appear");
      usleep(50000);
      peer.key(target, XK_Return);
      usleep(100000);
      child_result(true);
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
      child_result(false);
    }
  }
  HostDialogRequest request;
  request.request_id = save ? 2 : 1;
  if (save) {
    HostSaveFileDialogRequest payload;
    payload.title = "GUI.Forms service test";
    payload.initial_directory = directory;
    payload.suggested_name = "new-picture";
    payload.default_extension = "png";
    payload.confirm_overwrite = true;
    request.payload = payload;
  } else {
    HostOpenFileDialogRequest payload;
    payload.title = "GUI.Forms service test";
    payload.initial_directory = directory;
    payload.suggested_name = "existing.png";
    payload.filters = {{"PNG", {"png"}}};
    request.payload = payload;
  }
  const HostDialogResult result = (*window.services).show_dialog(request);
  const HostPathDialogResult *paths =
      std::get_if<HostPathDialogResult>(&result.payload);
  require(
      paths != nullptr && (*paths).outcome == HostDialogOutcome::accepted &&
          (*paths).paths ==
              std::vector<std::string>{
                  directory + (save ? "/new-picture.png" : "/existing.png")},
      "Retained file dialog returned wrong path");
  wait_child(runtime, child);
}
} // namespace
// The frame sequence (stage DP): an opaque live frame is drawn into the
// raster directly, and an overlay above a changed frame keeps its pixels
// through retained painting of its share.
class SolidControl final : public Control {
public:
  SolidControl(StableId id, const Color color) : Control(std::move(id)), color_(color) {}
  void on_paint(Painter &painter, Rect) override { painter.fill_rect(client_rectangle(), color_); }

private:
  Color color_{};
};
class LiveControl final : public Control {
public:
  LiveControl(StableId id, std::shared_ptr<LiveSurface> surface)
      : Control(std::move(id)), surface_(std::move(surface)) {}
  void on_paint(Painter &painter, Rect) override {
    painter.draw_live_surface(surface_, client_rectangle(), 1.0);
  }

private:
  std::shared_ptr<LiveSurface> surface_;
};
// Fills `area` of a 100 x 80 BGRA frame with `color` and publishes it as
// the frame's damage; the rest of the frame keeps its published pixels.
void publish_color(LiveSurface &surface, const Rect area, const Color color) {
  LiveSurfaceWriteLease write = surface.try_acquire_write(true);
  require(static_cast<bool>(write), "Live write lease refused");
  const std::size_t stride = static_cast<std::size_t>(write.row_bytes());
  std::span<std::byte> pixels = write.pixels();
  for (int y = static_cast<int>(area.y); y < static_cast<int>(area.bottom()); ++y) {
    for (int x = static_cast<int>(area.x); x < static_cast<int>(area.right()); ++x) {
      const std::size_t offset = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4U;
      pixels[offset] = static_cast<std::byte>(color.blue);
      pixels[offset + 1U] = static_cast<std::byte>(color.green);
      pixels[offset + 2U] = static_cast<std::byte>(color.red);
      pixels[offset + 3U] = std::byte{255};
    }
  }
  require(write.publish(area) != 0U, "Live frame not published");
}
// Reads one pixel of the window's raster (premultiplied BGRA8).
Color raster_pixel(const NativeWindow &window, const std::size_t x, const std::size_t y) {
  const unsigned char *row = static_cast<const unsigned char *>(window.raster.pixels()) +
                             y * window.raster.row_bytes();
  return Color::rgba(row[x * 4U + 2U], row[x * 4U + 1U], row[x * 4U], row[x * 4U + 3U]);
}
void live_frame_sequence(Runtime &runtime) {
  const Color red = Color::rgba(255, 0, 0);
  const Color green = Color::rgba(0, 255, 0);
  const Color blue = Color::rgba(0, 0, 255);
  const Color white = Color::rgba(255, 255, 255);
  const Color yellow = Color::rgba(255, 255, 0);
  const std::shared_ptr<LiveSurface> surface = LiveSurface::create(
      {.width = 100U, .height = 80U,
       .pixel_format = LiveSurfacePixelFormat::bgra32_premultiplied_srgb, .opaque = true});
  require(static_cast<bool>(surface), "Live surface not created");
  publish_color(*surface, {0.0, 0.0, 100.0, 80.0}, blue);
  const std::shared_ptr<SolidControl> root = make_control<SolidControl>(StableId("live.root"), red);
  const std::shared_ptr<LiveControl> scene = make_control<LiveControl>(StableId("live.scene"), surface);
  const std::shared_ptr<SolidControl> capsule = make_control<SolidControl>(StableId("live.capsule"), green);
  (*scene).set_requested_bounds({0.0, 0.0, 100.0, 80.0});
  (*capsule).set_requested_bounds({10.0, 10.0, 20.0, 10.0});
  (*capsule).set_paint_plane(PaintPlane::overlay);
  (*root).add_child(scene);
  (*root).add_child(capsule);
  host::LinuxApplicationWindow entry;
  entry.stable_id = "live";
  entry.options.title = "GUI.Forms X11 frame sequence check";
  entry.options.initial_size = {100, 80};
  entry.model = std::make_unique<gui_forms::Window>(root, entry.options.initial_size);
  const std::shared_ptr<NativeWindow> window = runtime.add(std::move(entry));
  (*window).ready();
  gui_forms::Window &model = *(*window).entry.model;
  require(model.queue_live_surface_presentation(scene, surface), "Live surface not registered");
  for (int tick = 0; tick < 8; ++tick) runtime.step(10);
  require((*window).visible, "Frame sequence window is not shown");
  require(raster_pixel(*window, 60U, 60U) == blue && raster_pixel(*window, 15U, 15U) == green,
          "First frame and overlay not composed");
  // Audit E4: with nothing published since, the live clock rests.
  runtime.step(0);
  require((*window).live_idle, "An idle live surface keeps the 16 ms clock running");

  // A change away from the overlay: a live-only tick copies the changed
  // region, widened by one surface pixel on each side (live_damage_in_window),
  // so 7 x 7 for a 5 x 5 change, and paints nothing retained.
  const MetricsSnapshot before = model.metrics_snapshot();
  publish_color(*surface, {50.0, 50.0, 5.0, 5.0}, white);
  runtime.step(0);
  const MetricsSnapshot moved = model.metrics_snapshot();
  require(moved.live_frames_presented == before.live_frames_presented + 1U,
          "Live-only tick did not present one frame");
  require(moved.live_pixels_presented == before.live_pixels_presented + 49U,
          "Live-only tick copied more than the changed region");
  require(moved.paint_passes == before.paint_passes &&
              moved.retained_pixels_presented == before.retained_pixels_presented,
          "Live-only tick painted retained content");
  require(raster_pixel(*window, 52U, 52U) == white && raster_pixel(*window, 60U, 60U) == blue,
          "Changed region not drawn directly");

  // A change beneath the overlay: retained painting repaints the overlay's
  // share over the new frame.
  publish_color(*surface, {5.0, 5.0, 30.0, 10.0}, yellow);
  runtime.step(0);
  const MetricsSnapshot covered = model.metrics_snapshot();
  require(covered.live_frames_presented == moved.live_frames_presented + 1U,
          "Overlay tick did not present one frame");
  require(covered.retained_pixels_presented > moved.retained_pixels_presented,
          "Overlay share was not repainted");
  require(raster_pixel(*window, 6U, 6U) == yellow && raster_pixel(*window, 15U, 12U) == green,
          "Overlay lost its pixels over a changed frame");
  (*window).close(HostCloseReason::application, true);
  runtime.step(0);
}
// Stage DP gate on X11: two distant damaged rectangles paint their sum, not
// their bounding box; the host copies exactly that rectangle set to the
// window; and no pixel outside them changes, in the raster or on the X
// server. The record is notes:results/2026-10-25-dp-gate-x11.
class ColorBlock final : public Control {
public:
  ColorBlock(StableId id, const Color color) : Control(std::move(id)), color_(color) {}
  void on_paint(Painter &painter, Rect) override { painter.fill_rect(client_rectangle(), color_); }
  void set_color(const Color color) {
    color_ = color;
    invalidate(Dirty::paint);
  }

private:
  Color color_{};
};
// Pixels that differ between two copies of a window, counted by whether the
// pixel's centre lies in one of the damaged rectangles.
struct ChangedPixels {
  std::size_t inside{};
  std::size_t outside{};
};
bool centre_inside(const std::size_t x, const std::size_t y, const std::span<const Rect> rectangles) {
  const Point centre{static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5};
  for (std::size_t index = 0; index < rectangles.size(); ++index) {
    if (rectangles[index].contains(centre)) return true;
  }
  return false;
}
// `before` and `after` hold `width` x `height` pixels, row by row.
template <typename Pixel>
ChangedPixels changed_pixels(const std::vector<Pixel> &before, const std::vector<Pixel> &after,
                             const std::size_t width, const std::size_t height,
                             const std::span<const Rect> rectangles) {
  require(before.size() == width * height && after.size() == width * height, "Pixel copies differ in size");
  ChangedPixels changed{};
  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width; ++x) {
      const std::size_t index = y * width + x;
      if (before[index] == after[index]) continue;
      if (centre_inside(x, y, rectangles)) {
        ++changed.inside;
      } else {
        ++changed.outside;
      }
    }
  }
  return changed;
}
// The raster's first `width` x `height` pixels, each BGRA pixel in one word.
std::vector<std::uint32_t> raster_copy(const NativeWindow &window, const std::size_t width, const std::size_t height) {
  require(window.raster.pixel_width() >= width && window.raster.pixel_height() >= height,
          "Raster smaller than the window");
  std::vector<std::uint32_t> pixels(width * height);
  const unsigned char *const base = static_cast<const unsigned char *>(window.raster.pixels());
  for (std::size_t y = 0; y < height; ++y) {
    const unsigned char *const row = base + y * window.raster.row_bytes();
    std::memcpy(pixels.data() + y * width, row, width * 4U);
  }
  return pixels;
}
// The window's pixels as the X server holds them: what the screen shows.
std::vector<unsigned long> screen_copy(const NativeWindow &window, const std::size_t width, const std::size_t height) {
  Display *const display = window.runtime.display;
  XSync(display, False);
  XImage *const image = XGetImage(display, window.xid, 0, 0, static_cast<unsigned>(width),
                                  static_cast<unsigned>(height), AllPlanes, ZPixmap);
  require(image != nullptr, "XGetImage refused the window");
  std::vector<unsigned long> pixels(width * height);
  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width; ++x) {
      pixels[y * width + x] = XGetPixel(image, static_cast<int>(x), static_cast<int>(y));
    }
  }
  XDestroyImage(image);
  return pixels;
}
void damage_rectangles_gate(Runtime &runtime) {
  const Color rest = Color::rgba(90, 90, 90);
  const std::shared_ptr<ColorBlock> root = make_control<ColorBlock>(StableId("dp.root"), Color::rgba(230, 230, 230));
  const std::shared_ptr<ColorBlock> top_left = make_control<ColorBlock>(StableId("dp.top-left"), rest);
  const std::shared_ptr<ColorBlock> bottom_right = make_control<ColorBlock>(StableId("dp.bottom-right"), rest);
  const std::array<Rect, 2> blocks{Rect{10.0, 10.0, 40.0, 30.0}, Rect{340.0, 250.0, 40.0, 30.0}};
  (*top_left).set_requested_bounds(blocks[0]);
  (*bottom_right).set_requested_bounds(blocks[1]);
  (*root).add_child(top_left);
  (*root).add_child(bottom_right);
  host::LinuxApplicationWindow entry;
  entry.stable_id = "dp";
  entry.options.title = "GUI.Forms X11 damage rectangles check";
  entry.options.initial_size = {400, 300};
  entry.model = std::make_unique<gui_forms::Window>(root, entry.options.initial_size);
  const std::shared_ptr<NativeWindow> window = runtime.add(std::move(entry));
  (*window).ready();
  gui_forms::Window &model = *(*window).entry.model;
  for (int tick = 0; tick < 8; ++tick) runtime.step(10);
  require((*window).visible, "Damage rectangles window is not shown");
  const std::size_t width = 400U;
  const std::size_t height = 300U;
  const std::vector<std::uint32_t> raster_before = raster_copy(*window, width, height);
  const std::vector<unsigned long> screen_before = screen_copy(*window, width, height);

  const MetricsSnapshot before = model.metrics_snapshot();
  (*top_left).set_color(Color::rgba(200, 40, 40));
  (*bottom_right).set_color(Color::rgba(40, 40, 200));
  for (int tick = 0; tick < 4; ++tick) runtime.step(10);
  const MetricsSnapshot after = model.metrics_snapshot();
  const std::vector<std::uint32_t> raster_after = raster_copy(*window, width, height);
  const std::vector<unsigned long> screen_after = screen_copy(*window, width, height);

  const double sum = blocks[0].area() + blocks[1].area();
  const double painted = after.painted_damage_area - before.painted_damage_area;
  const double bounding = after.paint_bounding_area - before.paint_bounding_area;
  const std::uint64_t presents = after.frames_presented - before.frames_presented;
  const std::uint64_t rectangles = after.retained_rectangles_presented - before.retained_rectangles_presented;
  const std::uint64_t pixels = after.retained_pixels_presented - before.retained_pixels_presented;
  const ChangedPixels raster = changed_pixels(raster_before, raster_after, width, height, blocks);
  const ChangedPixels screen = changed_pixels(screen_before, screen_after, width, height, blocks);
  std::cout << "dp-gate|host=x11|sum_area=" << sum << "|painted_area=" << painted << "|bounding_area=" << bounding
            << "|presents=" << presents << "|rectangles_presented=" << rectangles << "|pixels_presented=" << pixels
            << "|raster_changed_inside=" << raster.inside << "|raster_changed_outside=" << raster.outside
            << "|screen_changed_inside=" << screen.inside << "|screen_changed_outside=" << screen.outside << '\n';
  require(presents == 1U, "Two distant changes did not present one frame");
  const double difference = painted - sum;
  require(difference < 1.0e-6 && difference > -1.0e-6, "Painted area differs from the rectangles' sum");
  require(bounding > painted * 10.0, "The rectangles are not distant: the test cannot tell a sum from a box");
  require(rectangles == 2U, "The host did not copy the two-rectangle set");
  require(pixels == static_cast<std::uint64_t>(sum), "The host copied more than the two rectangles");
  require(raster.outside == 0U && screen.outside == 0U, "A pixel outside the damaged rectangles changed");
  require(raster.inside == static_cast<std::size_t>(sum) && screen.inside == static_cast<std::size_t>(sum),
          "The damaged rectangles did not change in the raster and on the screen");
  (*window).close(HostCloseReason::application, true);
  runtime.step(0);
}
// A window of 200 x 120 with a 100 x 80 viewport over a column of twenty
// distinctly coloured stripes (400 tall): the scroll copy gate's scene.
struct ScrollCopyScene final {
  std::shared_ptr<Panel> root;
  std::shared_ptr<ScrollableControl> viewport;
};
ScrollCopyScene scroll_copy_scene() {
  ScrollCopyScene scene{};
  scene.root = make_control<Panel>(StableId("copy.root"));
  // An opaque solid ground whose paint does not depend on its size: the
  // copy's precondition beneath the viewport.
  (*scene.root).set_background(Color::rgba(240, 240, 240));
  scene.viewport = make_control<ScrollableControl>(StableId("copy.viewport"));
  (*scene.viewport).auto_scroll = true;
  (*scene.viewport).set_requested_bounds({10.0, 10.0, 100.0, 80.0});
  const std::shared_ptr<Panel> column = make_control<Panel>(StableId("copy.column"));
  (*column).set_requested_bounds({0.0, 0.0, 80.0, 400.0});
  (*column).set_background(Color::rgba(250, 250, 250));
  for (std::size_t index = 0U; index < 20U; ++index) {
    const std::shared_ptr<Panel> stripe = make_control<Panel>(StableId("copy.stripe." + std::to_string(index)));
    (*stripe).set_requested_bounds({4.0, static_cast<double>(index) * 20.0 + 2.0, 72.0, 16.0});
    (*stripe).set_background(Color::rgba(static_cast<std::uint8_t>(40U + index * 10U),
                                         static_cast<std::uint8_t>(200U - index * 8U),
                                         static_cast<std::uint8_t>((index * 37U) % 256U)));
    (*column).add_child(stripe);
  }
  (*scene.viewport).add_child(column);
  (*scene.root).add_child(scene.viewport);
  return scene;
}
// A fresh whole-window paint of the scene scrolled to `position`.
std::vector<std::uint32_t> scroll_copy_reference(const Point position, const std::size_t width,
                                                 const std::size_t height) {
  const ScrollCopyScene scene = scroll_copy_scene();
  gui_forms::Window window(scene.root, {static_cast<double>(width), static_cast<double>(height)});
  window.perform_layout();
  static_cast<void>((*scene.viewport).scroll_to(position));
  window.perform_layout();
  render::SkiaRaster raster;
  require(raster.resize({static_cast<double>(width), static_cast<double>(height)}, 1.0),
          "Reference raster not allocated");
  require(raster.synchronize_images(window.image_resources()), "Reference images not synchronized");
  raster.begin_frame(DamageRegion{});
  require(window.paint(raster).has_value(), "Reference did not paint");
  raster.end_frame();
  std::vector<std::uint32_t> pixels(width * height);
  const unsigned char *const base = static_cast<const unsigned char *>(raster.pixels());
  for (std::size_t y = 0; y < height; ++y) {
    std::memcpy(pixels.data() + y * width, base + y * raster.row_bytes(), width * 4U);
  }
  return pixels;
}
// Scroll copies: a scroll that only moves the viewport's children by whole
// pixels moves the raster's pixels and paints only the band it exposed; the
// raster then equals a fresh whole-window paint, and the screen the raster,
// for one scroll per tick and for two scrolls taken in one tick.
void scroll_copy_gate(Runtime &runtime) {
  const ScrollCopyScene scene = scroll_copy_scene();
  host::LinuxApplicationWindow entry;
  entry.stable_id = "copy";
  entry.options.title = "GUI.Forms X11 scroll copy check";
  entry.options.initial_size = {200, 120};
  entry.model = std::make_unique<gui_forms::Window>(scene.root, entry.options.initial_size);
  const std::shared_ptr<NativeWindow> window = runtime.add(std::move(entry));
  (*window).ready();
  gui_forms::Window &model = *(*window).entry.model;
  for (int tick = 0; tick < 8; ++tick) runtime.step(10);
  require((*window).visible, "Scroll copy window is not shown");
#if defined(GUI_FORMS_PREPARED_TEXT)
  require(!model.scroll_copies_supported(), "The prepared raster turned scroll copies on");
#else
  require(model.scroll_copies_supported(), "The Linux raster did not turn scroll copies on");
  const std::size_t width = 200U;
  const std::size_t height = 120U;
  require((*scene.viewport).scroll_by({0.0, 10.0}), "The viewport did not scroll");
  (*window).update();
  require((*window).scroll_copies_applied == 1U, "One scroll did not apply one copy");
  require((*window).damage.empty(), "The copied frame was not presented");
  const std::vector<std::uint32_t> raster_once = raster_copy(*window, width, height);
  require(raster_once == scroll_copy_reference({0.0, 10.0}, width, height),
          "After one scroll the raster differs from a whole-window paint");
  const std::vector<unsigned long> screen_once = screen_copy(*window, width, height);
  for (std::size_t index = 0; index < raster_once.size(); ++index) {
    require((screen_once[index] & 0x00ffffffUL) == (raster_once[index] & 0x00ffffffU),
            "After one scroll the screen differs from the raster");
  }

  // Two scrolls taken before one tick draws: the band the first exposed
  // moves with the second copy.
  require((*scene.viewport).scroll_by({0.0, 14.0}), "The viewport did not scroll again");
  // Taken as the window's own update would, without drawing: the damage
  // joins what the window holds (nothing, after a presented tick) and the
  // copy waits for the next tick.
  const DamageRegion first_damage = model.take_damage();
  std::vector<ScrollCopy> first;
  model.take_scroll_copies(first);
  require(first.size() == 1U, "The second scroll did not record a copy");
  for (const Rect rectangle : first_damage.rectangles()) (*window).damage.add(rectangle);
  (*window).pending_copies.push_back(first[0]);
  require((*scene.viewport).scroll_by({0.0, -6.0}), "The viewport did not scroll back");
  (*window).update();
  require((*window).scroll_copies_applied == 3U, "Two scrolls did not apply two copies");
  const std::vector<std::uint32_t> raster_twice = raster_copy(*window, width, height);
  require(raster_twice == scroll_copy_reference({0.0, 18.0}, width, height),
          "After two scrolls the raster differs from a whole-window paint");
  const std::vector<unsigned long> screen_twice = screen_copy(*window, width, height);
  for (std::size_t index = 0; index < raster_twice.size(); ++index) {
    require((screen_twice[index] & 0x00ffffffUL) == (raster_twice[index] & 0x00ffffffU),
            "After two scrolls the screen differs from the raster");
  }
#endif
  (*window).close(HostCloseReason::application, true);
  runtime.step(0);
}
struct KeepClientSize final {
  std::function<HostServiceStatus(Size)> *retained{};
  void operator()(std::function<HostServiceStatus(Size)> request) const { *retained = std::move(request); }
};
struct SizeOnWorker final {
  const std::function<HostServiceStatus(Size)> *request{};
  HostServiceStatus *result{};
  void operator()() const { *result = (*request)({300.0, 200.0}); }
};
// A size request reaches the native window, and the model follows only
// through ConfigureNotify, as a user resize does.
void client_size_request(Runtime &runtime) {
  std::function<HostServiceStatus(Size)> request{};
  host::LinuxApplicationWindow entry;
  entry.stable_id = "sized";
  entry.options.title = "GUI.Forms X11 client size check";
  entry.options.initial_size = {320, 240};
  entry.options.minimum_size = {160, 120};
  entry.options.client_size_ready = KeepClientSize{&request};
  entry.model = std::make_unique<gui_forms::Window>(
      make_control<Control>(StableId("sized.root")), entry.options.initial_size);
  const std::shared_ptr<NativeWindow> window = runtime.add(std::move(entry));
  (*window).ready();
  require(static_cast<bool>(request), "Client size request not published");
  gui_forms::Window &model = *(*window).entry.model;
  for (int tick = 0; tick < 4; ++tick) runtime.step(10);
  require(request({100.0, 100.0}).error == HostServiceError::invalid_argument,
          "Size below the window minimum accepted");
  HostServiceStatus worker{};
  std::thread thread(SizeOnWorker{&request, &worker});
  thread.join();
  require(worker.error == HostServiceError::wrong_thread, "Foreign-thread size request accepted");
  require(request({300.0, 200.0}).accepted(), "Client size request refused");
  require(model.client_size() == Size{320.0, 240.0}, "Model resized before the native window");
  for (int tick = 0; tick < 50 && model.client_size() != Size{300.0, 200.0}; ++tick) runtime.step(10);
  require(model.client_size() == Size{300.0, 200.0}, "Native resize did not reach the model");
  (*window).close(HostCloseReason::application, true);
  runtime.step(0);
  require(request({300.0, 200.0}).error == HostServiceError::after_shutdown,
          "Size request outlived the window");
}
// One native image layout for present_kernels: a visual's masks, its depth,
// and the window's alpha mask and grant.
struct PresentLayout final {
  unsigned long red_mask;
  unsigned long green_mask;
  unsigned long blue_mask;
  int depth;
  unsigned long alpha_mask;
  bool present_alpha;
  NativePresentKernel kernel;
};

// SK2: the present copies the raster's BGRA8 rows as they are for the common
// visual, and the fallback kernels give the same colours for other layouts.
// The images are client-side, so every layout is checked whatever the
// server's own visual.
void present_kernels(Display *display) {
  // Three premultiplied BGRA8 pixels: red 200, green 100, blue 50 at alpha
  // 255; red 255, green 0, blue 0 at alpha 255; red 64, green 32, blue 16 at
  // alpha 128.
  const std::array<unsigned char, 12> source{50, 100, 200, 255, 0, 0, 255, 255, 16, 32, 64, 128};
  const NativePresentSource bgra{source.data(), source.size(), 0UL, false};
  const std::array<PresentLayout, 6> layouts{{
      {0xff0000UL, 0xff00UL, 0xffUL, 24, 0UL, false, NativePresentKernel::plain_copy},
      {0xff0000UL, 0xff00UL, 0xffUL, 32, 0xff000000UL, true, NativePresentKernel::plain_copy},
      {0xff0000UL, 0xff00UL, 0xffUL, 32, 0xff000000UL, false, NativePresentKernel::byte_channels},
      {0xffUL, 0xff00UL, 0xff0000UL, 24, 0UL, false, NativePresentKernel::byte_channels},
      {0x3ff00000UL, 0xffc00UL, 0x3ffUL, 30, 0UL, false, NativePresentKernel::scaled32},
      {0xf800UL, 0x7e0UL, 0x1fUL, 16, 0UL, false, NativePresentKernel::generic},
  }};
  for (const PresentLayout &layout : layouts) {
    Visual visual{};
    visual.c_class = TrueColor;
    visual.red_mask = layout.red_mask;
    visual.green_mask = layout.green_mask;
    visual.blue_mask = layout.blue_mask;
    visual.bits_per_rgb = 8;
    XImage *const image = XCreateImage(display, &visual, static_cast<unsigned>(layout.depth), ZPixmap, 0,
                                       nullptr, 3, 1, 32, 0);
    require(image != nullptr, "Cannot make a present image");
    std::vector<char> data(static_cast<std::size_t>((*image).bytes_per_line), 0);
    (*image).data = data.data();
    NativePresentSource present = bgra;
    present.alpha_mask = layout.alpha_mask;
    present.present_alpha = layout.present_alpha;
    const std::optional<NativePresentKernel> kernel = convert_present_rectangle(*image, present, 0, 0, 3, 1);
    require(kernel == layout.kernel, "The present chose the wrong kernel");
    for (int column = 0; column < 3; ++column) {
      const unsigned char *const pixel = source.data() + static_cast<std::size_t>(column) * 4U;
      const unsigned long value = XGetPixel(image, column, 0);
      const std::array<unsigned long, 3> masks{layout.red_mask, layout.green_mask, layout.blue_mask};
      const std::array<unsigned, 3> channels{pixel[2], pixel[1], pixel[0]};
      for (std::size_t channel = 0; channel < masks.size(); ++channel) {
        const unsigned shift = static_cast<unsigned>(std::countr_zero(masks[channel]));
        const unsigned long maximum = masks[channel] >> shift;
        const unsigned long expected = (channels[channel] * maximum + 127U) / 255U;
        require(((value & masks[channel]) >> shift) == expected, "A present changed a colour channel");
      }
      if (layout.alpha_mask != 0) {
        const unsigned long alpha = (value & layout.alpha_mask) >> 24U;
        require(alpha == (layout.present_alpha ? pixel[3] : 255U), "A present changed alpha");
      }
    }
    (*image).data = nullptr;
    XDestroyImage(image);
  }
}

int main() {
  try {
    require(host::linux_capabilities().supports(HostCapability::accessibility),
            "the Linux host advertises accessibility");
    Runtime runtime;
    present_kernels(runtime.display);
    const std::shared_ptr<Root> root =
        make_control<Root>(StableId("linux.services"));
    host::LinuxApplicationWindow entry;
    entry.stable_id = "services";
    entry.options.title = "GUI.Forms Linux protocol check";
    entry.options.initial_size = {640, 480};
    entry.model =
        std::make_unique<gui_forms::Window>(root, entry.options.initial_size);
    const std::shared_ptr<NativeWindow> window = runtime.add(std::move(entry));
    (*window).ready();
    require((*(*window).entry.model).request_focus(root), "Cannot focus probe");
    runtime.step(0);
    const std::vector<std::byte> large = patterned(180000);
    require((*(*window).services)
                .write_clipboard_text(std::string_view(
                    reinterpret_cast<const char *>(large.data()), large.size()))
                .accepted(),
            "Cannot own text clipboard");
    pid_t child = fork();
    require(child >= 0, "Cannot fork outgoing peer");
    if (child == 0) {
      try {
        Peer peer;
        require(peer.read("UTF8_STRING") == large,
                "Outgoing INCR text mismatch");
        child_result(true);
      } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        child_result(false);
      }
    }
    wait_child(runtime, child);
    incoming(runtime, *window, "UTF8_STRING", large, false);
    HostImage image;
    image.width = 256;
    image.height = 256;
    image.row_bytes = 1024;
    image.pixels.resize(256 * 256 * 4);
    // Random valid premultiplied BGRA8: each colour channel at most alpha.
    std::uint32_t random = 91;
    for (std::size_t i = 0; i < image.pixels.size(); i += 4U) {
      random = random * 1664525U + 1013904223U;
      const unsigned alpha = random >> 24U;
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        random = random * 1664525U + 1013904223U;
        image.pixels[i + channel] =
            static_cast<std::byte>((random >> 16U) % (alpha + 1U));
      }
      image.pixels[i + 3U] = static_cast<std::byte>(alpha);
    }
    require(
        (*(*window).services).write_clipboard_image(image.view()).accepted(),
        "Cannot own image clipboard");
    const HostClipboardImageResult owned =
        (*(*window).services).read_clipboard_image();
    require(owned.image() != nullptr && (*owned.image()).pixels == image.pixels,
            "Owned clipboard image did not round-trip exactly");
    const std::vector<std::byte> png = runtime.clipboard_png;
    require(png.size() > 65536, "PNG workload does not cover INCR");
    child = fork();
    require(child >= 0, "Cannot fork image peer");
    if (child == 0) {
      try {
        Peer peer;
        require(peer.read("image/png") == png, "Outgoing INCR PNG mismatch");
        child_result(true);
      } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        child_result(false);
      }
    }
    wait_child(runtime, child);
    // A valid premultiplied image survives PNG's straight alpha exactly.
    incoming(runtime, *window, "image/png", png, true, &image.pixels);
    // A version-1 private image (straight RGBA8) from an older GUI.Forms
    // peer is read through the input kernel into the same pixels.
    std::vector<std::byte> version_one(16U + image.pixels.size());
    const std::array<unsigned char, 16> header{'G', 'F', 'I', 'M', 1, 0, 0, 0,
                                               0, 1, 0, 0, 0, 1, 0, 0};
    for (std::size_t i = 0; i < header.size(); ++i)
      version_one[i] = static_cast<std::byte>(header[i]);
    for (std::size_t i = 0; i < image.pixels.size(); i += 4U) {
      const std::uint8_t alpha = std::to_integer<std::uint8_t>(image.pixels[i + 3U]);
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        const std::uint8_t premultiplied =
            std::to_integer<std::uint8_t>(image.pixels[i + 2U - channel]);
        version_one[16U + i + channel] =
            std::byte{detail::straight_channel(premultiplied, alpha)};
      }
      version_one[16U + i + 3U] = std::byte{alpha};
    }
    incoming(runtime, *window, "application/x-gui-forms-rgba", version_one, true,
             &image.pixels);
    const std::string file_uri = "file:///tmp/Rainstar%20copied%20%CE%A9.png\r\n";
    const std::span<const std::byte> uri_bytes = std::as_bytes(std::span(file_uri));
    incoming(runtime, *window, "text/uri-list",
             std::vector<std::byte>(uri_bytes.begin(), uri_bytes.end()), false);
    const std::string gnome_uri = "copy\n" + file_uri;
    const std::span<const std::byte> gnome_bytes = std::as_bytes(std::span(gnome_uri));
    incoming(runtime, *window, "x-special/gnome-copied-files",
             std::vector<std::byte>(gnome_bytes.begin(), gnome_bytes.end()), false);
    runtime.step(0);
    require((*root).keys >= 2, "Selection wait discarded keyboard input");
    child = fork();
    require(child >= 0, "Cannot fork drop peer");
    if (child == 0) {
      try {
        Peer peer;
        XSetSelectionOwner(peer.display, peer.atom("XdndSelection"),
                           peer.window, CurrentTime);
        peer.send((*window).xid, "XdndEnter", 5L << 24,
                  peer.atom("text/uri-list"));
        int x{}, y{};
        ::Window unused{};
        XTranslateCoordinates(peer.display, (*window).xid,
                              DefaultRootWindow(peer.display), 50, 50, &x, &y,
                              &unused);
        peer.send((*window).xid, "XdndPosition", 0,
                  (static_cast<long>(x) << 16) | (y & 65535), CurrentTime,
                  peer.atom("XdndActionCopy"));
        bool done = false;
        while (!done) {
          XEvent event = peer.next();
          if (event.type == SelectionRequest) {
            const std::string uri =
                "# "
                "comment\r\nfile:///tmp/a%20b.png\r\nfile://localhost/tmp/"
                "caf%C3%A9.png\r\nfile://remote/tmp/not-local.png\r\n";
            XChangeProperty(peer.display, event.xselectionrequest.requestor,
                            event.xselectionrequest.property,
                            peer.atom("text/uri-list"), 8, PropModeReplace,
                            reinterpret_cast<const unsigned char *>(uri.data()),
                            static_cast<int>(uri.size()));
            XEvent response{};
            response.xselection.type = SelectionNotify;
            response.xselection.display = peer.display;
            response.xselection.requestor = event.xselectionrequest.requestor;
            response.xselection.selection = event.xselectionrequest.selection;
            response.xselection.target = event.xselectionrequest.target;
            response.xselection.property = event.xselectionrequest.property;
            response.xselection.time = event.xselectionrequest.time;
            XSendEvent(peer.display, response.xselection.requestor, False,
                       NoEventMask, &response);
            XFlush(peer.display);
          } else if (event.type == ClientMessage &&
                     event.xclient.message_type == peer.atom("XdndStatus")) {
            require((event.xclient.data.l[1] & 1) != 0,
                    "Drop target rejected valid local files");
            peer.send((*window).xid, "XdndDrop", 0, CurrentTime);
          } else if (event.type == ClientMessage &&
                     event.xclient.message_type == peer.atom("XdndFinished")) {
            require((event.xclient.data.l[1] & 1) != 0,
                    "Drop completion rejected");
            done = true;
          }
        }
        child_result(true);
      } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        child_result(false);
      }
    }
    wait_child(runtime, child);
    require((*root).dropped, "Native drop did not reach retained control");
    const std::string directory =
        "/tmp/gui-forms-dialog-" + std::to_string(getpid());
    std::filesystem::create_directory(directory);
    std::ofstream(directory + "/existing.png").put('x');
    dialog(runtime, *window, directory, false);
    dialog(runtime, *window, directory, true);
    std::filesystem::remove_all(directory);
    live_frame_sequence(runtime);
    damage_rectangles_gate(runtime);
    scroll_copy_gate(runtime);
    client_size_request(runtime);
    (*window).close(HostCloseReason::application, true);
    std::cout << "X11 services: cross-process text and PNG INCR, preserved "
                 "input, local file drops, open/save dialogs, live frame sequence, client size request passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
