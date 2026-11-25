#include "file_manager_demoboard/demoboard.hpp"
#include "smoke_test.hpp"
#include "usage.hpp"
#include "gui_forms/platform/linux_host.hpp"

#include <clocale>
#include <memory>
#include <utility>
#include <vector>
#include <string_view>

int main(int argc, const char* argv[]) {
    // std-rules: allow(locale-change) the program, not the library, chooses its locale: the environment's character type, for X input methods and compose
    std::setlocale(LC_CTYPE, "");
    // --help prints the command line instead of starting the program.
    for (int index = 1; index < argc; ++index) {
        if (file_manager_demoboard::is_help_argument(argv[index])) {
            file_manager_demoboard::print_usage(argv[0]);
            return 0;
        }
    }
    std::unique_ptr<gui_forms::Window> product_model = file_manager_demoboard::make_product_window();
    bool product_only = false;
    bool smoke_test = false;
    for (int index = 1; index < argc; ++index) {
        constexpr std::string_view prefix = "--capture-state=";
        const std::string_view argument(argv[index]);
        if (argument.starts_with(prefix)) {
            static_cast<void>(file_manager_demoboard::apply_capture_state(
                *product_model, argument.substr(prefix.size())));
        } else if (argument == "--product-only") {
            product_only = true;
        } else if (argument == "--smoke-test") {
            smoke_test = true;
        }
    }
    gui_forms::host::LinuxApplicationWindow product;
    product.stable_id = "fm.window.primary";
    product.model = std::move(product_model);
    product.options.title = "File Manager — Demoboard";
    product.options.initial_size = {1450.0, 850.0};
    product.options.minimum_size = {150.0, 150.0};
    // --smoke-test: the packaged-program proof (see smoke_test.hpp).
    const std::shared_ptr<file_manager_demoboard::SmokeTest> smoke =
        file_manager_demoboard::arm_smoke_test(smoke_test, product.options, *product.model);

    std::vector<gui_forms::host::LinuxApplicationWindow> windows;
    windows.push_back(std::move(product));
    if (!product_only) {
        gui_forms::host::LinuxApplicationWindow controller;
        controller.stable_id = "demo.controller.window";
        controller.owner_id = windows.front().stable_id;
        controller.model =
            file_manager_demoboard::make_controller_window(windows.front().model.get());
        controller.options.title = "Demoboard Controller";
        controller.options.initial_size = {470.0, 700.0};
        controller.options.minimum_size = {360.0, 320.0};
        controller.tool_window = true;
        windows.push_back(std::move(controller));
    }
    return file_manager_demoboard::smoke_test_result(
        smoke, gui_forms::host::run_linux_application(std::move(windows)));
}
