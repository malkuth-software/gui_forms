// The X11 splashscreen (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part S,
// D5 b): an override-redirect window typed _NET_WM_WINDOW_TYPE_SPLASH with
// an empty input region, centred on the default screen and shown before
// Application::create, on a connection of its own (no run exists yet).
// paint_splashscreen draws each frame into a CPU raster with the bundled
// fonts; present puts it with XPutImage and syncs. Included by
// linux_host.cpp inside gui_forms::host::linux_detail.

namespace {

class X11SplashscreenSurface final : public gui_forms::detail::SplashscreenSurface {
public:
  X11SplashscreenSurface() = default;
  ~X11SplashscreenSurface() override {
    if (image_ != nullptr) {
      // The pixels are frame_'s; XDestroyImage must not free them.
      (*image_).data = nullptr;
      XDestroyImage(image_);
    }
    if (display_ == nullptr) return;
    if (gc_ != nullptr) XFreeGC(display_, gc_);
    if (window_ != 0) XDestroyWindow(display_, window_);
    if (colormap_ != 0) XFreeColormap(display_, colormap_);
    XSync(display_, False);
    XCloseDisplay(display_);
  }
  X11SplashscreenSurface(const X11SplashscreenSurface &) = delete;
  X11SplashscreenSurface &operator=(const X11SplashscreenSurface &) = delete;

  // Connects, registers the fonts and maps the window; false when any of
  // them fails.
  [[nodiscard]] bool open(const Size size) {
    display_ = XOpenDisplay(nullptr);
    if (display_ == nullptr) return false;
    try {
      register_fonts(raster_);
    } catch (const std::exception &) {
      return false;
    }
    const int screen = DefaultScreen(display_);
    XVisualInfo visual{};
    if (XMatchVisualInfo(display_, screen, 24, TrueColor, &visual) == 0) return false;
    visual_ = visual.visual;
    depth_ = visual.depth;
    width_ = static_cast<unsigned>(std::ceil(size.width));
    height_ = static_cast<unsigned>(std::ceil(size.height));
    const int x = (DisplayWidth(display_, screen) - static_cast<int>(width_)) / 2;
    const int y = (DisplayHeight(display_, screen) - static_cast<int>(height_)) / 2;
    const Window root = RootWindow(display_, screen);
    colormap_ = XCreateColormap(display_, root, visual_, AllocNone);
    XSetWindowAttributes attributes{};
    attributes.override_redirect = True;
    attributes.background_pixel = 0;
    attributes.border_pixel = 0;
    attributes.colormap = colormap_;
    attributes.event_mask = ExposureMask;
    window_ = XCreateWindow(display_, root, x, y, width_, height_, 0, depth_, InputOutput, visual_,
                            CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWColormap | CWEventMask,
                            &attributes);
    if (window_ == 0) return false;
    const Atom type = XInternAtom(display_, "_NET_WM_WINDOW_TYPE", False);
    const Atom splash = XInternAtom(display_, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    XChangeProperty(display_, window_, type, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(&splash), 1);
    // No input: an empty input region passes the pointer to what lies
    // beneath.
    const XserverRegion empty = XFixesCreateRegion(display_, nullptr, 0);
    XFixesSetWindowShapeRegion(display_, window_, ShapeInput, 0, 0, empty);
    XFixesDestroyRegion(display_, empty);
    gc_ = XCreateGC(display_, window_, 0, nullptr);
    if (gc_ == nullptr) return false;
    XMapRaised(display_, window_);
    return true;
  }

  bool present(const gui_forms::detail::SplashscreenFrame& frame) override {
    if (display_ == nullptr || window_ == 0) return false;
    if (frame.images != nullptr && !raster_.synchronize_images(*frame.images)) return false;
    static_cast<void>(raster_.resize(frame.size, 1.0));
    DamageRegion whole;
    whole.add(Rect{0.0, 0.0, frame.size.width, frame.size.height});
    raster_.begin_frame(whole);
    gui_forms::detail::paint_splashscreen(raster_, frame);
    raster_.end_frame();
    const std::byte *pixels = static_cast<const std::byte *>(raster_.pixels());
    if (pixels == nullptr || raster_.pixel_width() < width_ || raster_.pixel_height() < height_) return false;
    const std::size_t row_pixels = width_;
    if (frame_.size() != row_pixels * height_) {
      // Made once at the window's size; the image borrows frame_.
      frame_.assign(row_pixels * height_, 0U);
      if (image_ != nullptr) {
        (*image_).data = nullptr;
        XDestroyImage(image_);
      }
      image_ = XCreateImage(display_, visual_, static_cast<unsigned>(depth_), ZPixmap, 0,
                            reinterpret_cast<char *>(frame_.data()), width_, height_, 32,
                            static_cast<int>(row_pixels * 4U));
      if (image_ == nullptr) return false;
      // The raster's BGRA bytes are little-endian 32-bit words.
      (*image_).byte_order = LSBFirst;
    }
    for (unsigned row = 0; row < height_; ++row) {
      std::memcpy(frame_.data() + static_cast<std::size_t>(row) * row_pixels,
                  pixels + static_cast<std::size_t>(row) * raster_.row_bytes(), row_pixels * 4U);
    }
    // Exposures since the last frame are answered by this one.
    XEvent event{};
    bool exposed = XCheckWindowEvent(display_, window_, ExposureMask, &event) == True;
    while (exposed) exposed = XCheckWindowEvent(display_, window_, ExposureMask, &event) == True;
    XPutImage(display_, window_, gc_, image_, 0, 0, 0, 0, width_, height_);
    XSync(display_, False);
    return true;
  }

private:
  Display *display_{};
  Visual *visual_{};
  int depth_{};
  Colormap colormap_{};
  Window window_{};
  GC gc_{};
  XImage *image_{};
  unsigned width_{};
  unsigned height_{};
  std::vector<std::uint32_t> frame_{};
  render::SkiaRaster raster_{};
};

} // namespace

std::unique_ptr<gui_forms::detail::SplashscreenSurface> open_x11_splashscreen(const Size size) {
  std::unique_ptr<gui_forms::detail::SplashscreenSurface> result;
  std::unique_ptr<X11SplashscreenSurface> surface = std::make_unique<X11SplashscreenSurface>();
  if (!(*surface).open(size)) return result;
  result = std::move(surface);
  return result;
}
