// Window's chrome properties (gui_forms/window/chrome): dot-syntax writes
// and reads, validation, what reaches the host and when, and the icon's
// decode, on the headless reference host.
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Runs `write` and reports whether it threw std::invalid_argument.
template <typename Write>
bool refuses(Write write) {
    try {
        write();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

struct WriteOpacity final {
    Window* window{};
    double value{};
    void operator()() const { (*window).opacity = value; }
};
struct WriteText final {
    Window* window{};
    std::string value{};
    void operator()() const { (*window).text = value; }
};
struct WriteState final {
    Window* window{};
    FormWindowState value{};
    void operator()() const { (*window).window_state = value; }
};
struct WriteBorder final {
    Window* window{};
    FormBorderStyle value{};
    void operator()() const { (*window).form_border_style = value; }
};
struct WriteMinimum final {
    Window* window{};
    Size value{};
    void operator()() const { (*window).minimum_size = value; }
};
struct WriteIcon final {
    Window* window{};
    ImageId value{};
    void operator()() const { (*window).icon = value; }
};

void test_defaults_and_validation() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("chrome.root"));
    Window window(root, Size{200.0, 100.0});
    require(window.text.read().empty() && window.icon.read().value == 0U, "text and icon start empty");
    require(window.form_border_style.read() == FormBorderStyle::sizable, "the frame starts sizable");
    require(window.minimize_box.read() && window.maximize_box.read() && window.control_box.read(),
            "the boxes start shown");
    require(window.window_state.read() == FormWindowState::normal && !window.top_most.read(),
            "normal and not top-most");
    require(window.opacity.read() == 1.0 && window.show_in_taskbar.read(), "opaque, on the taskbar");
    require(window.start_position.read() == FormStartPosition::windows_default_location,
            "the platform places the window by default");
    require(window.written_chrome().empty(), "nothing is written at first");

    require(refuses(WriteOpacity{&window, 1.5}), "opacity above 1 accepted");
    require(refuses(WriteOpacity{&window, std::numeric_limits<double>::quiet_NaN()}), "NaN opacity accepted");
    require(refuses(WriteText{&window, std::string("a\0b", 3U)}), "text with NUL accepted");
    require(refuses(WriteText{&window, std::string("\xff")}), "malformed UTF-8 accepted");
    require(refuses(WriteState{&window, static_cast<FormWindowState>(7)}), "undefined state accepted");
    require(refuses(WriteBorder{&window, static_cast<FormBorderStyle>(9)}), "undefined border style accepted");
    require(refuses(WriteMinimum{&window, Size{-1.0, 10.0}}), "negative minimum accepted");
    require(refuses(WriteMinimum{&window, Size{40000.0, 10.0}}), "minimum past 32768 accepted");
    require(refuses(WriteIcon{&window, ImageId{999U}}), "unknown icon image accepted");
    require(window.opacity.read() == 1.0 && window.text.read().empty() && window.written_chrome().empty(),
            "a refused write changes nothing");

    window.text = "Settings";
    window.opacity = 0.5;
    require(window.text.read() == "Settings" && window.opacity.read() == 0.5, "writes read back");
    require(window.written_chrome().contains(WindowChromeField::text) &&
                window.written_chrome().contains(WindowChromeField::opacity) &&
                !window.written_chrome().contains(WindowChromeField::top_most),
            "the written set names exactly the written properties");
    require(window.applied_chrome().empty() && window.supported_chrome().empty(), "nothing applies without a host");
}

void test_host_receives_written_properties() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("chrome.host.root"));
    Window window(root, Size{200.0, 100.0});
    window.text = "Before the host";
    window.form_border_style = FormBorderStyle::fixed_dialog;
    window.maximize_box = false;

    host::HeadlessHost host(window);
    require(host.dispatch(HostAttachEvent{{200.0, 100.0}, 1.0}, 1).accepted(), "attach refused");
    host::HeadlessHostServices& services = host.headless_services();
    const WindowChrome& native = services.native_chrome();
    require(native.text == "Before the host" && native.form_border_style == FormBorderStyle::fixed_dialog &&
                !native.maximize_box,
            "attaching sends the properties written before");
    require(native.opacity == 1.0 && native.minimize_box, "unwritten properties keep their native values");
    require(services.chrome_requests() == 1U, "one request at attach");

    window.top_most = true;
    window.window_state = FormWindowState::maximized;
    window.minimum_size = Size{320.0, 240.0};
    require(native.top_most && native.window_state == FormWindowState::maximized &&
                native.minimum_size == Size{320.0, 240.0},
            "writes while attached reach the host at once");
    require(services.chrome_requests() == 4U, "one request per write");
    window.top_most = true;
    require(services.chrome_requests() == 4U, "an unchanged write sends nothing");
    require(window.applied_chrome().contains(WindowChromeField::top_most) &&
                window.applied_chrome().contains(WindowChromeField::text),
            "the applied set follows the host's answers");

    // A host without a taskbar: the write is kept but not applied.
    WindowChromeFields without_taskbar{WindowChromeFields::all_bits &
                                       ~static_cast<std::uint32_t>(WindowChromeField::show_in_taskbar)};
    services.set_supported_chrome(without_taskbar);
    window.show_in_taskbar = false;
    require(!window.show_in_taskbar.read(), "the property keeps the program's value");
    require(native.show_in_taskbar, "an unsupported property does not reach the host");
    require(!window.applied_chrome().contains(WindowChromeField::show_in_taskbar),
            "an unsupported property is not applied");
    require(window.supported_chrome() == without_taskbar, "the window reports what the host supports");
}

void test_icon_decode() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("chrome.icon.root"));
    Window window(root, Size{200.0, 100.0});
    // A 2 x 2 premultiplied BGRA8 image with padded rows.
    std::array<std::byte, 24> pixels{};
    for (std::size_t index = 0U; index < pixels.size(); ++index) {
        pixels[index] = static_cast<std::byte>(index);
    }
    const ImageLoadResult loaded = window.load_bgra32_premultiplied(2U, 2U, 12U, pixels);
    require(static_cast<bool>(loaded), "the icon image was refused");
    window.icon = loaded.image;
    host::HeadlessHost host(window);
    require(host.dispatch(HostAttachEvent{{200.0, 100.0}, 1.0}, 1).accepted(), "attach refused");
    const HostImage& icon = host.headless_services().native_chrome().icon;
    require(icon.width == 2U && icon.height == 2U && icon.row_bytes == 8U && icon.pixels.size() == 16U,
            "the icon reaches the host as tight BGRA8");
    require(icon.pixels[0] == std::byte{0} && icon.pixels[8] == std::byte{12}, "the icon's rows are copied");
    window.icon = ImageId{};
    require(host.headless_services().native_chrome().icon.width == 0U, "no icon sends an empty image");
}
} // namespace

int main() {
    try {
        test_defaults_and_validation();
        test_host_receives_written_properties();
        test_icon_decode();
        std::cout << "gui_forms_window_chrome_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_window_chrome_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
