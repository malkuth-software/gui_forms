#include "gui_forms/host.hpp"
#include "gui_forms/control.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
using namespace gui_forms;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
CursorImage pixels(int size, double scale) {
    return {size, size, scale, std::vector<Color>(static_cast<std::size_t>(size * size), {12, 34, 56, 128})};
}
class Services final : public HostServices {
public:
    Services() : HostServices({HostCapabilities::current_protocol_version, "cursor-test", HostCapability::cursor}) {}
    bool fail{};
    int custom_calls{};
    int stock_calls{};
    int width{};
    CursorKind stock{CursorKind::arrow};
protected:
    HostMonitorResult query_monitors_impl() override { return {}; }
    HostServiceStatus set_cursor_impl(CursorKind value) override { ++stock_calls; stock = value; return {}; }
    HostServiceStatus set_custom_cursor_impl(const CursorImagesPtr&, const CursorImage& image) override {
        ++custom_calls; width = image.width;
        return {fail ? HostServiceError::backend_failure : HostServiceError::none};
    }
    HostServiceStatus set_pointer_capture_impl(bool, std::uint64_t) override { return {}; }
    HostClipboardTextResult read_clipboard_text_impl() override { return {}; }
    HostServiceStatus write_clipboard_text_impl(std::string_view) override { return {}; }
    HostDialogResult show_dialog_impl(const HostDialogRequest&) override { return {}; }
    HostServiceStatus play_sound_cue_impl(const HostSoundCueRequest&) override { return {}; }
};
struct WrongThread {
    Services& services;
    CursorImagesPtr images;
    void operator()() const { require(!services.set_custom_cursor(images, 1).accepted(), "wrong thread accepted"); }
};
void rejected(std::vector<CursorImage> images, double x, double y) {
    bool caught = false;
    try { static_cast<void>(CursorImages::create(std::move(images), x, y)); }
    catch (const std::invalid_argument&) { caught = true; }
    require(caught, "invalid cursor accepted");
}
}
int main() {
    try {
        const CursorImagesPtr images = CursorImages::create({pixels(16, 1), pixels(24, 1.5), pixels(32, 2), pixels(64, 4)}, .25, .75);
        require((*images).select(1.25).width == 24 && (*images).select(8).width == 64, "scale variant selection");
        const CursorImage fractional = (*images).rasterize(1.25);
        require(fractional.width == 20 && fractional.height == 20 &&
                (*images).hotspot_x(fractional) == 5 && (*images).hotspot_y(fractional) == 15,
                "fractional DPI must preserve logical size and contact");
        require((*images).hotspot_x((*images).select(2)) == 8 && (*images).hotspot_y((*images).select(2)) == 24, "hotspot alignment");
        require((*images).select(std::numeric_limits<double>::quiet_NaN()).width == 16, "nonfinite host scale");
        rejected({}, 0, 0); rejected({pixels(16, 1)}, 1, 0);
        rejected({pixels(16, 1)}, 0, std::numeric_limits<double>::infinity());
        rejected({pixels(16, 0)}, 0, 0); rejected({pixels(16, 1), pixels(32, 1)}, 0, 0);
        rejected({pixels(16, 1), pixels(24, 2)}, 0, 0);
        CursorImage short_image = pixels(16, 1); short_image.rgba.pop_back();
        rejected({short_image}, 0, 0); rejected({pixels(257, 1)}, 0, 0);
        Services services;
        require(services.set_custom_cursor(images, 2, CursorKind::crosshair).accepted() && services.width == 32, "native dispatch");
        require(services.set_cursor(CursorKind::crosshair).accepted() && services.stock_calls == 1, "same fallback must clear custom cursor");
        services.fail = true;
        require(services.set_custom_cursor(images, 4, CursorKind::text).accepted() && services.stock == CursorKind::text, "native failure fallback");
        const int attempts = services.custom_calls;
        static_cast<void>(services.set_custom_cursor(images, 4, CursorKind::text));
        require(services.custom_calls == attempts, "failed native cursor retried on every move");
        std::thread worker(WrongThread{services, images}); worker.join();
        const Control::Ptr parent = make_control<Control>(StableId("parent"));
        const Control::Ptr child = make_control<Control>(StableId("child"));
        (*parent).add_child(child);
        (*parent).set_custom_cursor(images, CursorKind::crosshair);
        require((*child).effective_cursor_images() == images && (*child).effective_cursor() == CursorKind::crosshair, "custom inheritance");
        (*child).cursor = CursorKind::hand;
        require(!(*child).effective_cursor_images(), "stock override leaks parent image");
        (*child).cursor = std::nullopt;
        require((*child).effective_cursor_images() == images, "inherit reset");
        (*parent).cursor = CursorKind::crosshair;
        require(!(*child).effective_cursor_images(), "same stock fallback fails to reset");
        services.shutdown();
        require(!services.set_custom_cursor(images, 1).accepted(), "shutdown accepted mutation");
        std::cout << "Cursor validation, scaling, inheritance, fallback and lifetime passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
