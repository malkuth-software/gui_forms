// Stage T5, phase C4's gate (notes:planning/stages/T5-compositor/PLAN.md §5 C4):
// a GUI.Forms window on the X11 host, declared non-opaque, under Xvfb with
// no compositing manager, then with xcompmgr running, stopped and started
// again, read back from the X server.
//
// The window's theme has a clear window material, so that every pixel the
// window presents is clear once it is granted `transparent`. A solid green
// X window lies behind it. The window asks for `transparent` once, in its
// ready callback; the steps then follow the compositor:
// - none: no compositing manager owns _NET_WM_CM_S<screen>, so the grant
//   is opaque with the reason no_compositor, the window still has the
//   32-bit visual it was made with, and its pixels carry alpha 255;
// - started: xcompmgr takes the selection, the window is told through
//   XFixes, and the grant becomes transparent: its own pixel is clear and
//   the composed root shows the green behind it;
// - stopped: the grant returns to opaque, no_compositor, alpha 255;
// - restarted: transparent again.
// backdrop_changed is raised once for the request and once per change, in
// that order. Each reading is printed as a JSON line for the C4 record.
//
// Without xcompmgr, or with a compositor already running, the steps that
// need it are reported as not exercised; under CI (the CI variable set)
// a missing xcompmgr fails the gate, since the CI packages install it.
#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/window.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

extern char **environ;

namespace {

using namespace gui_forms;

constexpr const char *gate_title = "GUI.Forms compositor gate";
constexpr int channel_tolerance = 12;
constexpr std::array<int, 3> backdrop_rgb{0, 160, 0};
constexpr double window_width = 240.0;
constexpr double window_height = 160.0;
// The backdrop extends this far past the window on every side.
constexpr int backdrop_margin = 30;
// Timer ticks (perceptual: 100 ms each): settling after a change, and the
// longest wait for a change to arrive.
constexpr FrameInterval tick_interval = std::chrono::milliseconds(100);
constexpr unsigned settle_ticks = 5U;
constexpr unsigned change_ticks = 50U;
// Ticks allowed for the host's window to reach the server.
constexpr unsigned find_ticks = 30U;

struct Sample final {
  int red{};
  int green{};
  int blue{};
  int alpha{};
  bool read{};
};

bool near_backdrop(const Sample &sample) noexcept {
  const bool within = std::abs(sample.red - backdrop_rgb[0]) <= channel_tolerance &&
                      std::abs(sample.green - backdrop_rgb[1]) <= channel_tolerance &&
                      std::abs(sample.blue - backdrop_rgb[2]) <= channel_tolerance;
  return sample.read && within;
}

int channel_of(unsigned long pixel, unsigned long mask) noexcept {
  if (mask == 0UL)
    return 0;
  unsigned shift = 0U;
  while (((mask >> shift) & 1UL) == 0UL)
    shift = shift + 1U;
  const unsigned long maximum = mask >> shift;
  const unsigned long value = (pixel & mask) >> shift;
  const int scaled = static_cast<int>((value * 255UL + maximum / 2UL) / maximum);
  return scaled;
}

// X errors met while reading, through a handler installed only around the
// read; Xlib's default handler would end the process.
unsigned read_errors{};

int count_read_error(Display *display, XErrorEvent *error) {
  static_cast<void>(display);
  static_cast<void>(error);
  read_errors = read_errors + 1U;
  return 0;
}

// The pixel at (x, y) of a drawable; alpha_mask is the pixel bits holding
// alpha, 0 for a drawable without them.
Sample read_pixel(Display *display, Drawable drawable, int x, int y, unsigned long alpha_mask) {
  Sample sample{};
  XSync(display, False);
  const XErrorHandler previous = XSetErrorHandler(&count_read_error);
  const unsigned errors_before = read_errors;
  XImage *const image = XGetImage(display, drawable, x, y, 1U, 1U, AllPlanes, ZPixmap);
  XSync(display, False);
  static_cast<void>(XSetErrorHandler(previous));
  if (image == nullptr || read_errors != errors_before) {
    if (image != nullptr)
      XDestroyImage(image);
    return sample;
  }
  const unsigned long pixel = XGetPixel(image, 0, 0);
  sample.red = channel_of(pixel, (*image).red_mask);
  sample.green = channel_of(pixel, (*image).green_mask);
  sample.blue = channel_of(pixel, (*image).blue_mask);
  sample.alpha = alpha_mask != 0UL ? channel_of(pixel, alpha_mask) : 255;
  sample.read = true;
  XDestroyImage(image);
  return sample;
}

// xcompmgr, started without shadows; 0 when it cannot be started.
pid_t start_xcompmgr() {
  pid_t child = 0;
  std::array<char *, 3> arguments{const_cast<char *>("xcompmgr"), const_cast<char *>("-n"), nullptr};
  const int started = posix_spawnp(&child, "xcompmgr", nullptr, nullptr, arguments.data(), environ);
  if (started != 0)
    return 0;
  return child;
}

void stop_process(pid_t &child) {
  if (child <= 0)
    return;
  kill(child, SIGTERM);
  int status = 0;
  waitpid(child, &status, 0);
  child = 0;
}

// The professional theme with a clear window material.
std::shared_ptr<const Theme> clear_theme() {
  const std::unique_ptr<ThemeDefinition> held(new ThemeDefinition(windows_professional_theme_definition()));
  ThemeDefinition &definition = *held;
  SurfaceMaterial clear{};
  clear.fills.clear();
  clear.fills.push_back(MaterialFillLayer::solid(Color::rgba(0, 0, 0, 0)));
  ControlRoleRecipes &window = definition.roles[static_cast<std::size_t>(ControlVisualRole::window)];
  for (ControlVisualRecipe &recipe : window.ordinary)
    recipe.material = clear;
  std::shared_ptr<const Theme> theme = Theme::create(definition);
  return theme;
}

enum class GateStep : std::uint8_t {
  none,
  started,
  stopped,
  restarted,
  finished,
};

const char *step_name(GateStep step) noexcept {
  switch (step) {
  case GateStep::none: return "none";
  case GateStep::started: return "started";
  case GateStep::stopped: return "stopped";
  case GateStep::restarted: return "restarted";
  case GateStep::finished: return "finished";
  }
  return "unknown";
}

// The grant each step waits for.
WindowBackdrop expected_backdrop(GateStep step) noexcept {
  if (step == GateStep::started || step == GateStep::restarted)
    return WindowBackdrop::transparent;
  return WindowBackdrop::opaque;
}

// The gate's state, owned by main for the whole run; the ready callback,
// the timer and the event handler reach it through `active_gate`, which
// lives no longer.
struct GateContext final {
  // The test's own connection, for reading and for the backdrop.
  Display *display{};
  ::Window root{};
  ::Window backdrop{};
  ::Window window{};
  Atom selection{};
  gui_forms::Window *model{};
  std::shared_ptr<Control> root_control{};
  ApplicationWindowHandle handle{};
  FrameRequestToken timer{};
  SubscriptionToken changes{};
  std::vector<WindowBackdropGrant> grants{};
  GateStep step{GateStep::none};
  // Ticks spent finding the window, waiting for the step's grant, then
  // settling after it.
  unsigned finding{};
  unsigned waited{};
  unsigned settled{};
  unsigned readings{};
  pid_t xcompmgr{};
  bool exercised{};
  const char *failure{};
};

GateContext *active_gate{};

void fail(GateContext &gate, const char *message) {
  if (gate.failure == nullptr)
    gate.failure = message;
}

void finish(GateContext &gate) {
  gate.step = GateStep::finished;
  stop_process(gate.xcompmgr);
  gate.timer.disconnect();
  static_cast<void>(gate.handle.request_close());
}

// The GUI.Forms window, a child of the root (Xvfb runs no window manager)
// named with the gate's title.
::Window find_gate_window(Display *display, ::Window root) {
  ::Window found = None;
  ::Window root_return = None;
  ::Window parent = None;
  ::Window *children = nullptr;
  unsigned count = 0U;
  if (XQueryTree(display, root, &root_return, &parent, &children, &count) == 0)
    return found;
  for (unsigned index = 0U; index < count; ++index) {
    char *name = nullptr;
    if (XFetchName(display, children[index], &name) != 0 && name != nullptr) {
      if (std::strcmp(name, gate_title) == 0)
        found = children[index];
      XFree(name);
    }
  }
  if (children != nullptr)
    XFree(children);
  return found;
}

// The window's own pixel and the composed root at its centre, against the
// grant the step waited for.
void read_step(GateContext &gate) {
  XWindowAttributes attributes{};
  if (XGetWindowAttributes(gate.display, gate.window, &attributes) == 0) {
    fail(gate, "the gate window's attributes are readable");
    return;
  }
  int root_x = 0;
  int root_y = 0;
  ::Window child = None;
  XTranslateCoordinates(gate.display, gate.window, gate.root, 0, 0, &root_x, &root_y, &child);
  const int centre_x = attributes.width / 2;
  const int centre_y = attributes.height / 2;
  const unsigned long colour = (*attributes.visual).red_mask | (*attributes.visual).green_mask |
                               (*attributes.visual).blue_mask;
  const unsigned long alpha_mask = attributes.depth == 32 ? (0xFFFFFFFFUL & ~colour) : 0UL;
  const Sample own = read_pixel(gate.display, gate.window, centre_x, centre_y, alpha_mask);
  const Sample composed = read_pixel(gate.display, gate.root, root_x + centre_x, root_y + centre_y, 0UL);
  const WindowBackdropGrant grant = (*gate.model).backdrop_grant();
  const CompositorState &state = (*gate.model).compositor_state();
  std::printf("{\"gate\":\"linux_compositor\",\"step\":\"%s\",\"state\":%s,\"granted\":\"%s\",\"reason\":\"%s\","
              "\"depth\":%d,\"own\":[%d,%d,%d,%d],\"composed\":[%d,%d,%d]}\n",
              step_name(gate.step), state.to_json().c_str(), window_backdrop_name(grant.granted),
              window_backdrop_reason_name(grant.reason), attributes.depth, own.red, own.green, own.blue, own.alpha,
              composed.red, composed.green, composed.blue);
  std::fflush(stdout);
  gate.readings = gate.readings + 1U;
  if (attributes.depth != 32)
    fail(gate, "a window declared non-opaque is made with the 32-bit visual");
  if (!own.read)
    fail(gate, "the window's own pixel is readable");
  if (grant.granted != expected_backdrop(gate.step))
    fail(gate, "the grant follows the compositor");
  if (grant.granted == WindowBackdrop::opaque) {
    if (grant.reason != WindowBackdropReason::no_compositor)
      fail(gate, "without a compositor the reason is no_compositor");
    if (state.compositing != CompositorPresence::absent)
      fail(gate, "the window is told that no compositor runs");
    if (own.alpha != 255)
      fail(gate, "an opaque grant presents alpha 255");
  } else {
    if (state.compositing != CompositorPresence::present)
      fail(gate, "the window is told that a compositor runs");
    if (own.alpha > 2)
      fail(gate, "a transparent grant presents the clear theme's alpha");
    if (!near_backdrop(composed))
      fail(gate, "the composed screen shows what lies behind a transparent window");
  }
}

// Moves to the next step: starts or stops xcompmgr.
void advance(GateContext &gate) {
  if (gate.step == GateStep::none) {
    gate.xcompmgr = start_xcompmgr();
    if (gate.xcompmgr == 0) {
      std::printf("{\"gate\":\"linux_compositor\",\"compositor_steps\":\"not exercised: xcompmgr cannot be "
                  "started\"}\n");
      if (std::getenv("CI") != nullptr)
        fail(gate, "CI installs xcompmgr (tools/install_linux_ci_packages.sh)");
      finish(gate);
      return;
    }
    gate.step = GateStep::started;
  } else if (gate.step == GateStep::started) {
    stop_process(gate.xcompmgr);
    gate.step = GateStep::stopped;
  } else if (gate.step == GateStep::stopped) {
    gate.xcompmgr = start_xcompmgr();
    gate.step = GateStep::restarted;
  } else {
    gate.exercised = true;
    finish(gate);
    return;
  }
  gate.waited = 0U;
  gate.settled = 0U;
}

// The host's window has reached the server: put the green backdrop
// behind it. False, finishing the gate, when the steps cannot run.
bool arrange(GateContext &gate) {
  if (XGetSelectionOwner(gate.display, gate.selection) != None) {
    std::printf("{\"gate\":\"linux_compositor\",\"compositor_steps\":\"not exercised: a compositor already "
                "runs\"}\n");
    finish(gate);
    return false;
  }
  int root_x = 0;
  int root_y = 0;
  ::Window child = None;
  XTranslateCoordinates(gate.display, gate.window, gate.root, 0, 0, &root_x, &root_y, &child);
  const unsigned long green = static_cast<unsigned long>(backdrop_rgb[1]) << 8U;
  const unsigned width = static_cast<unsigned>(window_width) + 2U * static_cast<unsigned>(backdrop_margin);
  const unsigned height = static_cast<unsigned>(window_height) + 2U * static_cast<unsigned>(backdrop_margin);
  gate.backdrop = XCreateSimpleWindow(gate.display, gate.root, root_x - backdrop_margin, root_y - backdrop_margin,
                                      width, height, 0U, 0UL, green);
  XSetWindowAttributes attributes{};
  attributes.override_redirect = True;
  XChangeWindowAttributes(gate.display, gate.backdrop, CWOverrideRedirect, &attributes);
  XMapWindow(gate.display, gate.backdrop);
  XRaiseWindow(gate.display, gate.window);
  XSync(gate.display, False);
  return true;
}

void tick(GateContext &gate) {
  if (gate.step == GateStep::finished)
    return;
  if (gate.window == None) {
    XSync(gate.display, False);
    gate.window = find_gate_window(gate.display, gate.root);
    if (gate.window == None) {
      gate.finding = gate.finding + 1U;
      if (gate.finding >= find_ticks) {
        fail(gate, "the test finds its own window");
        finish(gate);
      }
      return;
    }
    if (!arrange(gate))
      return;
  }
  if ((*gate.model).backdrop_grant().granted != expected_backdrop(gate.step)) {
    gate.waited = gate.waited + 1U;
    if (gate.waited >= change_ticks) {
      fail(gate, "the window is told when the compositor starts or stops");
      finish(gate);
    }
    return;
  }
  gate.settled = gate.settled + 1U;
  if (gate.settled < settle_ticks)
    return;
  read_step(gate);
  advance(gate);
}

struct GateTick final {
  void operator()(FrameTime) const {
    GateContext *const held = active_gate;
    if (held == nullptr)
      return;
    tick(*held);
  }
};

struct GrantRecorder final {
  void record(const WindowBackdropGrant &grant) {
    GateContext *const held = active_gate;
    if (held == nullptr)
      return;
    (*held).grants.push_back(grant);
  }
};

GrantRecorder grant_recorder{};

// The window is ready: give it the clear theme, ask for transparent and
// start the timer, which finds the native window once it reaches the
// server (the host's requests may still be buffered here).
struct GateReady final {
  void operator()(gui_forms::Window &model, ApplicationWindowHandle handle) const {
    GateContext *const held = active_gate;
    if (held == nullptr)
      return;
    GateContext &gate = *held;
    gate.model = &model;
    gate.handle = handle;
    if (!model.non_opaque_declared())
      fail(gate, "the window option declares the window non-opaque");
    model.set_theme(clear_theme());
    gate.changes = model.backdrop_changed().subscribe(
        Delegate<const WindowBackdropGrant &>::bind<GrantRecorder, &GrantRecorder::record>(grant_recorder));
    static_cast<void>(model.request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::transparent)));
    gate.timer = model.schedule_ui_timer(*gate.root_control, tick_interval, FrameClock::now() + tick_interval,
                                         GateTick{});
  }
};

// backdrop_changed, in order: the request (opaque, no_compositor), then
// transparent, opaque and transparent as the compositor started, stopped
// and started again.
bool grants_in_order(const std::vector<WindowBackdropGrant> &grants) {
  const std::array<WindowBackdrop, 4> expected{WindowBackdrop::opaque, WindowBackdrop::transparent,
                                               WindowBackdrop::opaque, WindowBackdrop::transparent};
  if (grants.size() < expected.size())
    return false;
  for (std::size_t index = 0U; index < expected.size(); ++index)
    if (grants[index].granted != expected[index])
      return false;
  return true;
}

} // namespace

int main() {
  // The program's application (APP_WINDOW_RULES_DESIGN A.2): windows made
  // from here join it, and it runs them.
  ApplicationCreateResult created = Application::create(ApplicationConfig{});
  if (!created) return 1;
  Application& application = *created.application;
  const HostCapabilities capabilities = Application::capabilities();
  if (!capabilities.supports(HostCapability::transparent_windows) ||
      !capabilities.supports(HostCapability::compositor_notifications)) {
    std::fprintf(stderr, "the X11 host advertises transparent windows and compositor notifications\n");
    return 1;
  }
  if (capabilities.supports(HostCapability::window_backdrops) ||
      capabilities.supports(HostCapability::presentation_preferences)) {
    std::fprintf(stderr, "the X11 host offers no material and no preference signal (T5-Q10)\n");
    return 1;
  }
  GateContext gate{};
  gate.display = XOpenDisplay(nullptr);
  if (gate.display == nullptr) {
    std::fprintf(stderr, "the gate opens the display\n");
    return 1;
  }
  const int screen = DefaultScreen(gate.display);
  gate.root = RootWindow(gate.display, screen);
  const std::string selection_name = "_NET_WM_CM_S" + std::to_string(screen);
  gate.selection = XInternAtom(gate.display, selection_name.c_str(), False);
  active_gate = &gate;
  gate.root_control = make_control<Control>(StableId("compositor.gate.root"));
  std::unique_ptr<gui_forms::Window> model =
      std::make_unique<gui_forms::Window>(gate.root_control, Size{window_width, window_height});
  ApplicationWindowOptions options{};
  options.title = gate_title;
  options.initial_size = Size{window_width, window_height};
  options.non_opaque = true;
  options.ready = GateReady{};
  const ApplicationResult result = application.run(std::move(model), std::move(options));
  stop_process(gate.xcompmgr);
  gate.timer.disconnect();
  gate.changes.disconnect();
  active_gate = nullptr;
  if (gate.backdrop != None)
    XDestroyWindow(gate.display, gate.backdrop);
  XCloseDisplay(gate.display);
  std::fflush(stdout);
  if (!result.accepted()) {
    std::fprintf(stderr, "the application runs\n");
    return 1;
  }
  if (gate.failure != nullptr) {
    std::fprintf(stderr, "%s\n", gate.failure);
    return 1;
  }
  if (gate.exercised) {
    if (gate.readings != 4U) {
      std::fprintf(stderr, "every step was read\n");
      return 1;
    }
    if (!grants_in_order(gate.grants)) {
      std::fprintf(stderr, "backdrop_changed follows the compositor in order\n");
      return 1;
    }
  }
  std::printf("linux compositor tests passed\n");
  return 0;
}
