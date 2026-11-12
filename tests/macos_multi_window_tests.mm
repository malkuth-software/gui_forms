#import <AppKit/AppKit.h>

#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/platform/macos_host.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using VoidHostCallback = std::function<void()>;
using ShowDialogCallback = std::function<gui_forms::HostDialogResult(
    const gui_forms::HostDialogRequest&)>;
using ReadClipboardCallback =
    std::function<gui_forms::HostClipboardTextResult()>;
using WriteClipboardCallback =
    std::function<gui_forms::HostServiceStatus(std::string_view)>;

std::unique_ptr<gui_forms::Window> make_window(std::string stable_id) {
    std::shared_ptr<gui_forms::Panel> root = std::make_shared<gui_forms::Panel>(
        gui_forms::StableId(std::move(stable_id)));
    (*root).set_requested_bounds({0.0, 0.0, 240.0, 140.0});
    return std::make_unique<gui_forms::Window>(root,
                                               gui_forms::Size{240.0, 140.0});
}

class CountClosed final {
public:
    explicit CountClosed(std::uint64_t& count) noexcept : count_(count) {}

    void operator()() const { ++count_; }

private:
    std::uint64_t& count_;
};

class CaptureCloseRequest final {
public:
    explicit CaptureCloseRequest(std::function<void()>& close_request) noexcept
        : close_request_(close_request) {}

    void operator()(
        VoidHostCallback,
        VoidHostCallback request_close,
        ShowDialogCallback,
        ReadClipboardCallback,
        WriteClipboardCallback) const {
        close_request_ = std::move(request_close);
    }

private:
    std::function<void()>& close_request_;
};

class StoreCombinedSnapshot final {
public:
    explicit StoreCombinedSnapshot(std::string& snapshot) noexcept
        : snapshot_(snapshot) {}

    void operator()(std::string_view metrics, std::string_view host) const {
        snapshot_ = std::string(metrics) + std::string(host);
    }

private:
    std::string& snapshot_;
};

// A close callback run later on the main queue without an Objective-C
// block (owner decision V2-1); close_on_main adopts and deletes it.
struct CloseCallbackWork final {
    std::function<void()> close;
};

void close_on_main(void* context) {
    const std::unique_ptr<CloseCallbackWork> work(static_cast<CloseCallbackWork*>(context));
    if ((*work).close) (*work).close();
}

class ExerciseReusableWindow final {
public:
    ExerciseReusableWindow(const std::function<void()>& close_controller,
                           const std::function<void()>& close_product,
                           std::uint64_t& visibility_ready) noexcept
        : close_controller_(close_controller), close_product_(close_product),
          visibility_ready_(visibility_ready) {}

    void operator()(VoidHostCallback show, VoidHostCallback) const {
        ++visibility_ready_;
        show();
        CloseCallbackWork* const controller = new CloseCallbackWork{close_controller_};
        dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_MSEC), dispatch_get_main_queue(),
                         controller, &close_on_main);
        CloseCallbackWork* const product = new CloseCallbackWork{close_product_};
        dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC), dispatch_get_main_queue(),
                         product, &close_on_main);
    }

private:
    const std::function<void()>& close_controller_;
    const std::function<void()>& close_product_;
    std::uint64_t& visibility_ready_;
};

} // namespace

int main() {
    using gui_forms::host::MacApplicationWindow;

    if (gui_forms::host::run_macos_application({}) != 2) return 1;

    std::uint64_t product_closed{};
    std::uint64_t controller_closed{};
    std::string product_snapshot;
    std::string controller_snapshot;
    std::function<void()> close_product;
    std::function<void()> close_controller;
    std::uint64_t visibility_ready{};

    MacApplicationWindow product;
    product.stable_id = "test.window.product";
    product.model = make_window("test.root.product");
    product.options.title = "GUI.Forms Product Root";
    product.options.initial_size = {240.0, 140.0};
    product.options.minimum_size = {160.0, 100.0};
    product.options.titlebar_presentation =
        gui_forms::host::MacTitlebarPresentation::
            transparent_full_size_content;
    product.options.window_drag_region_id = "test.root.product";
    product.options.closed = CountClosed(product_closed);
    product.options.host_ready = CaptureCloseRequest(close_product);
    product.options.final_snapshot = StoreCombinedSnapshot(product_snapshot);

    MacApplicationWindow controller;
    controller.stable_id = "test.window.controller";
    controller.owner_id = product.stable_id;
    controller.model = make_window("test.root.controller");
    controller.options.title = "GUI.Forms Tool Root";
    controller.options.initial_size = {220.0, 130.0};
    controller.options.minimum_size = {160.0, 100.0};
    controller.options.closed = CountClosed(controller_closed);
    controller.options.initially_visible = false;
    controller.options.hide_on_close = true;
    controller.options.host_ready = CaptureCloseRequest(close_controller);
    controller.options.visibility_ready = ExerciseReusableWindow(
        close_controller, close_product, visibility_ready);
    controller.options.final_snapshot =
        StoreCombinedSnapshot(controller_snapshot);
    controller.tool_window = true;

    std::vector<MacApplicationWindow> windows;
    windows.push_back(std::move(product));
    windows.push_back(std::move(controller));
    const int result = gui_forms::host::run_macos_application(std::move(windows));
    if (result != 0 || product_closed != 1U || controller_closed != 1U ||
        visibility_ready != 1U ||
        product_snapshot.empty() || controller_snapshot.empty()) {
        return 2;
    }
    if (product_snapshot.find(
            "\"titlebar_presentation\":\"transparent_full_size_content\"") ==
            std::string::npos ||
        product_snapshot.find("\"native_full_size_content\":true") ==
            std::string::npos ||
        product_snapshot.find("\"native_close_button_present\":true") ==
            std::string::npos ||
        product_snapshot.find("\"native_minimize_button_present\":true") ==
            std::string::npos ||
        product_snapshot.find("\"native_zoom_button_present\":true") ==
            std::string::npos ||
        product_snapshot.find("\"native_system_title_present\":true") ==
            std::string::npos ||
        product_snapshot.find("\"window_drag_region_resolved\":true") ==
            std::string::npos ||
        product_snapshot.find("\"window_drag_region_count\":1") ==
            std::string::npos ||
        controller_snapshot.find("\"titlebar_presentation\":\"standard\"") ==
            std::string::npos ||
        controller_snapshot.find("\"native_full_size_content\":false") ==
            std::string::npos) {
        return 3;
    }
    return 0;
}
