// The orbit toy as a program (orbit_toy.hpp). Headless, it prints what it
// is and ends; with GUI_FORMS_EXAMPLE_NATIVE it opens the native window:
// press on the view to orbit (the pointer is kept in the view and hidden),
// turn the wheel to move in and out, click the capsule at the top left at
// any time, and press Escape to give the pointer back. --visible keeps the
// pointer shown and free (the visible-cursor variant).

#include "orbit_toy.hpp"

#include <gui_forms/application/application.hpp>

#include <cstring>
#include <iostream>
#include <utility>

int main(int argc, char** argv) {
    using namespace gui_forms;
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    if (!created) return 1;
    Application& application = *created.application;
    relative_pointer_example::OrbitToy toy(&application);
    const bool visible = argc > 1 && std::strcmp(argv[1], "--visible") == 0;
    (*toy.view).visible = visible;
#if GUI_FORMS_EXAMPLE_NATIVE
    ApplicationWindowOptions options;
    options.title = "Orbit toy (relative pointer)";
    options.initial_size = {480, 320};
    options.print_metrics_on_close = false;
    const ApplicationResult result = application.run(std::move(toy.window), std::move(options));
    const ApplicationResult ended = Application::shutdown();
    return result.accepted() && ended.accepted() ? 0 : 1;
#else
    std::cout << "orbit toy: build with GUI_FORMS_EXAMPLE_NATIVE=ON to play; the headless run is "
                 "gui_forms_relative_pointer_example_tests\n";
    return 0;
#endif
}
