#include "prepared_text_test_support.hpp"
#include "../src/host/linux/application/linux_host_internal.hpp"
#include "../src/core/text/prepared/prepared_storage.hpp"
#include <X11/Xutil.h>

#include <cstdlib>
#include <iostream>

namespace {
using namespace gui_forms;
using namespace gui_forms::host;
using namespace gui_forms::host::linux_detail;
using prepared_test::require;

class PreparedControl final : public Control {
public:
    PreparedControl() : Control(StableId("linux-prepared-fixture")) {}
    PreparedTextLayout layout{};
    Color background{255, 255, 255, 255};
    bool fail{false};
protected:
    void on_paint(Painter& painter, const Rect bounds) override {
        painter.fill_rect(bounds, background);
        if (fail) throw std::runtime_error("injected control paint refusal");
        const PreparedTextPaintResult result = painter.draw_prepared_text(layout, layout.authority(), {8, 32}, {0, 0, 0, 255});
        if (result.status != PreparedTextStatus::success) throw gui_forms::detail::PreparedTextPaintFailure(result.status);
    }
};
struct CapturedImage final {
    XImage* value{};
    CapturedImage() = default;
    ~CapturedImage() { if (value != nullptr) XDestroyImage(value); }
    CapturedImage(const CapturedImage&) = delete;
    CapturedImage& operator=(const CapturedImage&) = delete;
};
std::vector<unsigned long> capture(NativeWindow& window, const unsigned width, const unsigned height) {
    XSync(window.runtime.display, False);
    CapturedImage image{};
    image.value = XGetImage(window.runtime.display, window.xid, 0, 0, width, height, AllPlanes, ZPixmap);
    require(image.value != nullptr, "server image readable");
    std::vector<unsigned long> pixels(static_cast<std::size_t>(width) * height);
    for (unsigned row = 0; row < height; ++row) {
        const std::size_t offset = static_cast<std::size_t>(row) * width;
        for (unsigned column = 0; column < width; ++column) {
            pixels[offset + column] = XGetPixel(image.value, static_cast<int>(column), static_cast<int>(row));
        }
    }
    return pixels;
}
std::vector<std::uint8_t> front_pixels(const NativeWindow& window) {
    const std::uint8_t* const data = static_cast<const std::uint8_t*>(window.raster.pixels());
    require(data != nullptr, "coherent front available");
    const std::size_t bytes = window.raster.byte_size();
    std::vector<std::uint8_t> result(data, data + bytes);
    return result;
}
void require_preserved(const NativeWindow& window, const std::vector<std::uint8_t>& pixels,
                       const PaintReceipt receipt, const std::uint64_t presented) {
    require(window.raster.prepared_front_receipt() == receipt, "front keeps actual receipt");
    const std::vector<std::uint8_t> current = front_pixels(window);
    require(current == pixels, "failed candidate preserves front bytes and extent");
    const PaintLeaseSnapshot lease = (*window.entry.model).paint_lease_snapshot();
    require(lease.presented_revision == presented, "failed candidate does not acknowledge model");
}
void prepare(PreparedTextService& service, PreparedTextSession& session, const EncodedFontLease& bank,
             PreparedControl& control, const std::string_view text) {
    const PreparedTextKey key = prepared_test::make_key(service, bank, text);
    prepared_test::prepare(service, session, key, text, control.layout);
    control.invalidate(Dirty::paint);
}
void exercise(const std::span<const std::byte> bytes) {
    PreparedTextService service{};
    const EncodedFontLease bank = prepared_test::make_bank(service, bytes);
    std::unique_ptr<PreparedTextSession> session{};
    const PreparedTextStatus opened = service.open_session(bank, nullptr, session);
    require(opened == PreparedTextStatus::success, "prepared session opens");
    const std::shared_ptr<PreparedControl> control = std::make_shared<PreparedControl>();
    prepare(service, *session, bank, *control, "Linux prepared");
    LinuxApplicationWindow entry{};
    entry.stable_id = "linux-prepared-host";
    entry.model = std::make_unique<gui_forms::Window>(control, Size{180, 96});
    entry.options.initial_size = {180, 96};
    entry.options.minimum_size = {1, 1};
    entry.options.print_metrics_on_close = false;
    Runtime runtime{};
    const std::shared_ptr<NativeWindow> owner = runtime.add(std::move(entry));
    NativeWindow& window = *owner;
    XSync(runtime.display, False);
    // Explicitly process map/configure events; no readiness sleeps or guessed
    // timing. The mapped Xvfb drawable must exist before server capture.
    while (XPending(runtime.display) != 0) {
        XEvent event{};
        XNextEvent(runtime.display, &event);
        runtime.dispatch(event);
    }
    window.update();
    PaintLeaseSnapshot lease = (*window.entry.model).paint_lease_snapshot();
    require(lease.presented_revision != 0, "native host acknowledged first prepared frame");
    const PaintReceipt initial = window.raster.prepared_front_receipt();
    const std::vector<std::uint8_t> initial_pixels = front_pixels(window);
    const std::vector<unsigned long> initial_server = capture(window, 180, 96);
    bool ink = false;
    for (std::size_t index = 0; index < initial_pixels.size(); index += 4U) {
        if (initial_pixels[index] < 128) ink = true;
    }
    require(ink, "prepared frame contains ink");

    (*control).fail = true;
    (*control).background = {255, 0, 0, 255};
    (*control).invalidate(Dirty::paint);
    window.update();
    require_preserved(window, initial_pixels, initial, lease.presented_revision);
    require(capture(window, 180, 96) == initial_server, "exception exposure presents only previous coherent pixels");
    (*control).fail = false;
    (*control).background = {255, 255, 255, 255};

    (*session).cancel();
    (*control).invalidate(Dirty::paint);
    window.update();
    require_preserved(window, initial_pixels, initial, lease.presented_revision);

    // The host's device size moves with the logical one (scale 1 here).
    window.size = {16384, 16384};
    window.device_width = 16384;
    window.device_height = 16384;
    window.damage.add({0, 0, 16384, 16384});
    window.update();
    require_preserved(window, initial_pixels, initial, lease.presented_revision);
    require(capture(window, 180, 96) == initial_server, "oversized refused candidate uses bounded old-front presentation");

    window.size = {300, 120};
    window.device_width = 300;
    window.device_height = 120;
    XResizeWindow(runtime.display, window.xid, 300, 120);
    XSync(runtime.display, False);
    const HostDispatchResult resized = window.dispatch(HostResizeEvent{window.size});
    require(resized.accepted(), "model resize accepted");
    window.damage.add({0, 0, 300, 120});
    window.update();
    require_preserved(window, initial_pixels, initial, lease.presented_revision);
    require(capture(window, 180, 96) == initial_server, "resized drawable refusal preserves old-front intersection");

    prepare(service, *session, bank, *control, "Recovered");
    window.update();
    require(window.raster.pixel_width() == 300 && window.raster.pixel_height() == 120, "recovery publishes resized front");
    lease = (*window.entry.model).paint_lease_snapshot();
    require(lease.presented_revision > initial.rendered_revision, "recovery acknowledges new receipt");
    const PaintReceipt recovered = window.raster.prepared_front_receipt();
    const std::vector<std::uint8_t> recovered_pixels = front_pixels(window);
    (*window.entry.model).set_occluded(true, std::chrono::steady_clock::now());
    window.damage.add({0, 0, 300, 120});
    const std::uint64_t accepted_before = lease.presentation_receipts_accepted;
    window.update();
    require_preserved(window, recovered_pixels, recovered, lease.presented_revision);
    lease = (*window.entry.model).paint_lease_snapshot();
    require(lease.presentation_receipts_accepted == accepted_before, "null model receipt cannot invent acknowledgement");
    (*window.entry.model).set_occluded(false, std::chrono::steady_clock::now());
    window.update();

    window.damage.clear();
    const std::vector<std::uint8_t> before_partial = front_pixels(window);
    (*control).background = {0, 255, 0, 255};
    (*control).invalidate(Rect{270, 90, 10, 10});
    window.update();
    const std::vector<std::uint8_t> after_partial = front_pixels(window);
    require(before_partial != after_partial, "partial repaint changes requested region");
    for (std::size_t row = 0; row < 60; ++row) {
        const std::size_t offset = row * window.raster.row_bytes();
        const bool same = std::equal(before_partial.begin() + static_cast<std::ptrdiff_t>(offset),
            before_partial.begin() + static_cast<std::ptrdiff_t>(offset + 200U * 4U),
            after_partial.begin() + static_cast<std::ptrdiff_t>(offset));
        require(same, "partial candidate preserves undamaged content");
    }
    (*session).begin_close();
    (*session).join_and_release();
}
}

int main(const int argc, char** const argv) {
    try {
        require(argc == 2, "font directory argument");
        const int environment = setenv("GUI_FORMS_FONT_DIR", argv[1], 1);
        require(environment == 0, "font environment configured");
        const std::vector<std::byte> bytes = prepared_test::read_font(argv[1]);
        exercise(bytes);
        std::cout << "Linux prepared host: success, refusal, resize, exposure and partial checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
