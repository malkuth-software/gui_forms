// The LinkPage example as a program (link_page.hpp). Headless, it lays the
// sample document out in a window model, prints what it flowed and ends;
// with GUI_FORMS_EXAMPLE_NATIVE it opens the page in a native window: scroll,
// resize and hover the links, and the page flows its text only on resize.

#include "link_page.hpp"
#include "sample_document.hpp"

#include <gui_forms/window/window.hpp>
#if GUI_FORMS_EXAMPLE_NATIVE
#include <gui_forms/application/application.hpp>
#endif

#include <iostream>
#include <memory>
#include <utility>

int main() {
    using namespace gui_forms;
    std::shared_ptr<link_page_example::LinkPage> page =
        make_control<link_page_example::LinkPage>(StableId("link-page.page"));
    (*page).set_paragraphs(link_page_example::sample_document(2000U));
    const Size client{640.0, 480.0};
#if GUI_FORMS_EXAMPLE_NATIVE
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    if (!created) return 1;
    Application& application = *created.application;
    std::unique_ptr<Window> window = std::make_unique<Window>(application, page, client);
    ApplicationWindowOptions options;
    options.title = "LinkPage (retained custom control)";
    options.initial_size = {640, 480};
    options.print_metrics_on_close = true;
    const ApplicationResult result = application.run(std::move(window), std::move(options));
    const ApplicationResult ended = Application::shutdown();
    return result.accepted() && ended.accepted() ? 0 : 1;
#else
    const std::unique_ptr<Window> window = std::make_unique<Window>(page, client);
    (*window).perform_layout();
    std::cout << "link page: " << (*page).paragraph_count() << " paragraphs, " << (*page).link_count()
              << " links, " << (*page).content_height() << " px tall, flowed " << (*page).flow_count()
              << " times; build with GUI_FORMS_EXAMPLE_NATIVE=ON to open it\n";
    return 0;
#endif
}
