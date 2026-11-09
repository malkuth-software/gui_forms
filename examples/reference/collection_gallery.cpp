#include "collection_gallery_form.hpp"   // generated from forms/collection_gallery.form
#include "collection_gallery_theme.hpp"  // generated from themes/collection_gallery

#include <gui_forms/application.hpp>
#include <gui_forms/collection_controls.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace gf = gui_forms;

namespace {

// The window's behaviour: the collections' items (data), the choices' and
// sections' Value bindings, and the handlers. Declared after the window and
// the model, so it ends first and its subscriptions go with it.
class Gallery final : public gf::Component {
public:
    Gallery(reference::GalleryWindow& window, reference::GalleryModel& model) : window_(window), model_(model) {
        rebuild_commands();
        (*window_.choices).set_items({{.id = 1, .label = "First choice"},
                                      {.id = 2, .label = "Second choice"},
                                      {.id = 3, .label = "Third choice"}});
        (*window_.choices).bind(model_.selection);
        model_.music.set_checked(true);
        first_body_ = gf::make_control<gf::Label>(gf::StableId("first.body"),
                                                  "Both sliders share one Value<double>. Neither needs a quiet flag.");
        second_body_ = gf::make_control<gf::Label>(gf::StableId("second.body"),
                                                   "Rebuild replaces toolbar items without replacing its group subscription.");
        (*first_body_).set_bounds({0.0, 0.0, 712.0, 64.0});
        (*second_body_).set_bounds({0.0, 0.0, 712.0, 64.0});
        (*window_.sections).single_open = true;
        (*window_.sections).set_items(
            {{.id = 1, .label = "Shared scalar state", .content = first_body_, .expanded = &model_.first_open},
             {.id = 2, .label = "Collection lifetime", .content = second_body_, .expanded = &model_.second_open}});
        gf::on((*window_.commands).invoked(), *this, &Gallery::on_command);
        gf::on((*window_.choices).changed(), *this, &Gallery::on_choice);
        gf::on((*window_.sections).toggled(), *this, &Gallery::on_section);
    }

private:
    void rebuild_commands() {
        const std::string run_label = alternate_ ? "Run again" : "Run";
        (*window_.commands).set_items(
            {{.id = 1, .label = run_label, .shortcut = gf::KeyGesture{gf::PhysicalKey::r, gf::Modifier::control},
              .command = &model_.run},
             {.id = 2, .label = "Enable / disable Run"},
             {.id = 3, .label = "Rebuild items"}});
    }
    void on_command(const gf::CommandItem& item) {
        if (item.id == 2) model_.run.set_enabled(!model_.run.state().enabled);
        if (item.id == 3) {
            alternate_ = !alternate_;
            rebuild_commands();
        }
        const std::string text = "Command item " + std::to_string(item.id);
        (*window_.status).text = text;
    }
    void on_choice(const int index) {
        const std::string text = "Selected choice " + std::to_string(index);
        (*window_.status).text = text;
    }
    void on_section(const int index, const bool expanded) {
        const std::string state = expanded ? " opened" : " closed";
        const std::string text = "Section " + std::to_string(index) + state;
        (*window_.status).text = text;
    }

    reference::GalleryWindow& window_;
    reference::GalleryModel& model_;
    std::shared_ptr<gf::Label> first_body_{};
    std::shared_ptr<gf::Label> second_body_{};
    bool alternate_{};
};

void report_findings(const std::vector<gf::ThemeCompileDiagnostic>& findings) {
    for (const gf::ThemeCompileDiagnostic& finding : findings) {
        std::cerr << finding.code << ' ' << finding.file << ':' << finding.line << ':' << finding.column << ' '
                  << finding.detail << '\n';
    }
}

} // namespace

int main() {
    try {
        // The program's theme is compiled once, as the application is made.
        gf::ApplicationConfig config{};
        config.theme.base = gf::ThemeBase::gui_forms;
        config.theme.packages.push_back(reference::collection_gallery_theme());
        gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
        report_findings(created.findings);
        if (!created) return 1;
        gf::Application& application = *created.application;
        {
            reference::GalleryModel model{};   // declared before the window it binds
            std::unique_ptr<reference::GalleryWindow> window =
                std::make_unique<reference::GalleryWindow>(application, model);
            Gallery gallery(*window, model);
            gf::ApplicationWindowOptions options{};
            options.title = std::string(reference::GalleryWindow::window_title);
            options.initial_size = {760.0, 610.0};
            options.minimum_size = {760.0, 610.0};
            const gf::ApplicationResult result = application.run(std::move(window), options);
            if (!result.accepted()) return 1;
        }
        // Once, after the last run: ends the rendering library for the process.
        const gf::ApplicationResult ended = gf::Application::shutdown();
        return ended.accepted() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
