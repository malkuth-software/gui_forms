#include "pages.hpp"

#include "live-surfaces_form.hpp"

#include "gui_forms/live_surface.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"
#include "gui_forms/controls/drawing_surface.hpp"
#include "gui_forms/timer/timer/timer.hpp"

namespace gui_forms::gallery::pages {

// DrawingSurface paint callback: the presenter replays the newest published
// frame through the renderer-neutral Painter vocabulary.
class LiveSurfacePaint final {
public:
    explicit LiveSurfacePaint(std::shared_ptr<LiveSurface> surface) noexcept
        : surface_(std::move(surface)) {}

    void operator()(Painter& painter, const Rect bounds, Rect) const {
        painter.fill_rect(bounds, Color::rgba(18, 28, 38));
        painter.draw_live_surface(surface_, bounds, 1.0);
    }

private:
    std::shared_ptr<LiveSurface> surface_;
};

// The sub-surface's tree, built on its own thread: a caption and a button
// that counts its own presses there, so the window's tree never sees them.
class SubSurfaceSample final : public SubSurfaceContent {
public:
    Control::Ptr build_root() override {
        std::shared_ptr<Panel> root = make_control<Panel>(StableId("gallery.live-surfaces.sub-surface.root"));
        (*root).set_background(Color::rgba(236, 242, 248));
        std::shared_ptr<Label> caption = make_control<Label>(
            StableId("gallery.live-surfaces.sub-surface.caption"), "This tree runs on its own thread");
        (*caption).set_requested_bounds({12.0, 10.0, 300.0, 22.0});
        (*root).add_child(caption);
        std::shared_ptr<Button> press = make_control<Button>(
            StableId("gallery.live-surfaces.sub-surface.press"), "Press inside the sub-surface");
        (*press).set_requested_bounds({12.0, 40.0, 220.0, 32.0});
        (*root).add_child(press);
        return root;
    }
};

// Owns the live-surfaces page that demo/pages/live-surfaces.form describes:
// one LiveSurface, the page Timer whose tick
// writes a frame through a write lease on the UI thread, the Commands its
// producer buttons bind, and the interval Value its TrackBar binds. Each tick
// repaints one column band and publishes exactly that damage, so a presenter
// that still holds the previous presentation copies only the band.
class LiveSurfacesPage final : public gui_forms::Component {
public:
    explicit LiveSurfacesPage(const std::shared_ptr<Surface>& body);

private:
    // The document binds the Commands and the interval Value (FD-6).
    template <typename> friend class gui_forms::gallery::pages::LiveSurfacesDocument;

    static constexpr std::uint32_t band_count = 8U;

    void build_presenter();
    void build_producer();
    [[nodiscard]] LiveSurfaceDescription description() const noexcept;
    [[nodiscard]] bool ensure_timer();
    void on_start();
    void on_stop();
    void on_publish_once();
    void on_reconfigure();
    void on_interval_changed(double milliseconds);
    void on_tick();
    void publish_frame();
    void paint_band(LiveSurfaceWriteLease& lease, std::uint32_t band,
                    Color color) const;
    void refresh_status(std::string_view event, Rect damage);

    std::shared_ptr<LiveSurface> surface_;
    std::unique_ptr<Timer> timer_;
    Command start_{};
    Command stop_{};
    Command publish_once_{};
    Command reconfigure_{};
    Value<double> interval_{120.0};
    // Declared after the Commands and the Value its controls bind, so it is
    // released first.
    LiveSurfacesDocument<LiveSurfacesPage> document_{};
    std::shared_ptr<DrawingSurface> view_;
    std::shared_ptr<Label> status_;
    std::shared_ptr<Label> telemetry_;
    std::shared_ptr<Button> reconfigure_button_;
    std::uint64_t frames_{};
    bool large_{};
    bool needs_full_frame_{true};
};

LiveSurfacesPage::LiveSurfacesPage(const std::shared_ptr<Surface>& body) {
    // description() reads large_, so the surface is created after every member
    // has its initializer, not in the initializer list ahead of them.
    surface_ = LiveSurface::create(description());
    // The Commands carry their text before the document binds them.
    build_producer();
    document_.initialize_component(*this);
    mount_form_children(*document_.gallery_live_surfaces_document, *body);
    build_presenter();
    reconfigure_button_ = document_.gallery_live_surfaces_reconfigure;
    (*document_.gallery_live_surfaces_sub_surface_host).set_content(std::make_unique<SubSurfaceSample>(), true);
    on(start_.invoked(), *this, &LiveSurfacesPage::on_start);
    on(stop_.invoked(), *this, &LiveSurfacesPage::on_stop);
    on(publish_once_.invoked(), *this, &LiveSurfacesPage::on_publish_once);
    on(reconfigure_.invoked(), *this, &LiveSurfacesPage::on_reconfigure);
    on(interval_.changed(), *this, &LiveSurfacesPage::on_interval_changed);
    refresh_status("No frame published yet", {});
}

LiveSurfaceDescription LiveSurfacesPage::description() const noexcept {
    LiveSurfaceDescription result;
    result.width = large_ ? 240U : 160U;
    result.height = large_ ? 150U : 100U;
    result.pixel_format = native_live_surface_pixel_format();
    result.buffer_count = default_live_surface_buffer_count;
    result.opaque = true;
    return result;
}

// The presenter replays the newest published frame of the page's surface.
void LiveSurfacesPage::build_presenter() {
    view_ = document_.gallery_live_surfaces_view;
    (*view_).set_paint_callback(LiveSurfacePaint(surface_));
    status_ = document_.gallery_live_surfaces_status;
    telemetry_ = document_.gallery_live_surfaces_telemetry;
}

// The page's Commands, which the document's producer buttons bind (the
// track bar binds the interval Value there too).
void LiveSurfacesPage::build_producer() {
    start_.set_text("Start producer");
    start_.set_description("Start the page Timer; each tick publishes one band");
    stop_.set_text("Stop producer");
    stop_.set_description("Stop the page Timer; zero idle wakeups");
    publish_once_.set_text("Publish one frame");
    publish_once_.set_description("Write and publish a single frame now");
    reconfigure_.set_text("Reconfigure 240×150");
    reconfigure_.set_description("Replace the buffer pool at another size");
}

bool LiveSurfacesPage::ensure_timer() {
    if (timer_) return true;
    Window* const window = (*view_).attached_window();
    if (window == nullptr) {
        (*status_).text = "Producer needs an attached Window";
        return false;
    }
    timer_ = std::make_unique<Timer>(
        *window, std::chrono::milliseconds(
                     static_cast<int>(std::round(interval_.get()))));
    on((*timer_).tick(), *this, &LiveSurfacesPage::on_tick);
    return true;
}

void LiveSurfacesPage::on_start() {
    if (!surface_ || !ensure_timer()) return;
    (*timer_).start();
    (*status_).text = "Producer running · " +
                        std::to_string((*timer_).interval().count()) +
                        " ms deadline on the UI queue";
}

void LiveSurfacesPage::on_stop() {
    if (timer_) (*timer_).stop();
    (*status_).text = "Producer stopped · zero idle wakeups";
}

void LiveSurfacesPage::on_publish_once() {
    if (!surface_) return;
    publish_frame();
}

void LiveSurfacesPage::on_reconfigure() {
    if (!surface_) return;
    large_ = !large_;
    const bool reconfigured = (*surface_).reconfigure(description());
    if (!reconfigured) {
        large_ = !large_;
        (*status_).text = "Reconfigure refused · a write lease is active";
        return;
    }
    needs_full_frame_ = true;
    const std::string next = large_ ? "Reconfigure 160×100" : "Reconfigure 240×150";
    reconfigure_.set_text(next);
    (*reconfigure_button_).text = next;
    refresh_status("Reconfigured · new epoch · previous frame carried until the next publish",
                   {});
}

void LiveSurfacesPage::on_interval_changed(const double milliseconds) {
    if (!timer_) return;
    (*timer_).set_interval(
        std::chrono::milliseconds(static_cast<int>(std::round(milliseconds))));
}

void LiveSurfacesPage::on_tick() {
    if (!(*view_).effectively_visible()) {
        (*timer_).stop();
        (*status_).text = "Producer suspended · page hidden · zero wakeups";
        return;
    }
    publish_frame();
}

void LiveSurfacesPage::paint_band(LiveSurfaceWriteLease& lease,
                                  const std::uint32_t band, const Color color) const {
    const std::uint32_t width = lease.width();
    const std::uint32_t height = lease.height();
    const std::uint32_t band_width = width / band_count;
    const std::uint32_t x0 = band * band_width;
    const std::uint32_t x1 = band + 1U == band_count ? width : x0 + band_width;
    // Premultiplied BGRA8, the one live-surface format.
    const std::byte first = std::byte{color.blue};
    const std::byte third = std::byte{color.red};
    const std::span<std::byte> pixels = lease.pixels();
    const std::uint64_t row_bytes = lease.row_bytes();
    for (std::uint32_t y = 0U; y < height; ++y) {
        // A shade gradient down the band keeps every published frame distinct.
        const std::uint8_t shade = static_cast<std::uint8_t>(
            255U - (y * 96U) / std::max<std::uint32_t>(1U, height));
        const std::byte green_byte = std::byte{
            static_cast<std::uint8_t>((color.green * shade) / 255U)};
        for (std::uint32_t x = x0; x < x1; ++x) {
            const std::uint64_t offset = y * row_bytes + static_cast<std::uint64_t>(x) * 4U;
            pixels[offset] = first;
            pixels[offset + 1U] = green_byte;
            pixels[offset + 2U] = third;
            pixels[offset + 3U] = std::byte{255U};
        }
    }
}

void LiveSurfacesPage::publish_frame() {
    LiveSurfaceWriteLease lease = (*surface_).try_acquire_write(!needs_full_frame_);
    if (!lease) {
        refresh_status("Frame dropped · every buffer is held by a reader", {});
        return;
    }
    constexpr std::array<Color, 4> palette{accent, green, orange, violet};
    const Color color = palette[static_cast<std::size_t>(frames_ % palette.size())];
    Rect damage;
    if (needs_full_frame_) {
        for (std::uint32_t band = 0U; band < band_count; ++band) {
            paint_band(lease, band, Color::rgba(43, 62, 80));
        }
        needs_full_frame_ = false;
    } else {
        const std::uint32_t band = static_cast<std::uint32_t>(frames_ % band_count);
        paint_band(lease, band, color);
        const double band_width = static_cast<double>(lease.width() / band_count);
        damage = {static_cast<double>(band) * band_width, 0.0,
                  band + 1U == band_count ? static_cast<double>(lease.width()) -
                                                static_cast<double>(band) * band_width
                                          : band_width,
                  static_cast<double>(lease.height())};
    }
    const std::uint64_t generation = lease.publish(damage);
    ++frames_;
    if (Window* const window = (*view_).attached_window()) {
        static_cast<void>((*window).queue_live_surface_presentation(view_, surface_));
    }
    (*view_).invalidate(Dirty::paint);
    refresh_status("Published generation " + std::to_string(generation), damage);
}

void LiveSurfacesPage::refresh_status(const std::string_view event, const Rect damage) {
    if (!surface_) {
        (*status_).text = "LiveSurface::create refused the description";
        return;
    }
    const LiveSurfaceSnapshot snapshot = (*surface_).snapshot();
    const std::string damage_text = damage.empty()
        ? std::string("whole surface")
        : std::to_string(static_cast<int>(damage.x)) + "," +
              std::to_string(static_cast<int>(damage.y)) + " " +
              std::to_string(static_cast<int>(damage.width)) + "×" +
              std::to_string(static_cast<int>(damage.height));
    (*status_).text = std::string(event) + " · damage " + damage_text;
    (*telemetry_).text =
        std::to_string(snapshot.description.width) + "×" +
        std::to_string(snapshot.description.height) + " BGRA · " + std::to_string(snapshot.description.buffer_count) + " buffers" +
        "\nepoch " + std::to_string(snapshot.epoch) +
        " · generation " + std::to_string(snapshot.published_generation) +
        " · publishes " + std::to_string(snapshot.publishes) +
        "\ndropped acquires " + std::to_string(snapshot.dropped_acquires) +
        " · read acquires " + std::to_string(snapshot.read_acquires) +
        " · last read generation " + std::to_string(snapshot.last_read_generation) +
        (snapshot.carries_previous_frame ? "\ncarrying the previous epoch's frame" : "");
}

void add_live_surfaces(const std::shared_ptr<Surface>& page,
                       const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<LiveSurfacesPage>(page));
}

}  // namespace gui_forms::gallery::pages
