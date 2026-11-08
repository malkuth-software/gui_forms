// The collection gallery (stage B reference): the window is the form. The
// build generates CollectionGalleryWindow from collection_gallery.form; it
// makes the controls as its members and binds them to the model at
// creation. The program attaches what a form does not describe: the
// collections' items and the behaviour.

#include "collection_gallery_window.hpp"

#include <gui_forms/application.hpp>
#include <gui_forms/collection_controls.hpp>

#include <iostream>
#include <memory>
#include <string>

namespace gf = gui_forms;

// The gallery's behaviour over its window's controls. Holds the controls it
// changes; declared after the model, so it is released first.
class GalleryBehaviour final : public gf::Component {
public:
    GalleryBehaviour(CollectionGalleryWindow& window, GalleryModel& model)
        : model_(model), commands_(window.commands), choices_(window.choices), sections_(window.sections),
          status_(window.status) {
        rebuild_commands();
        (*choices_).set_items({{.id = 1, .label = "First choice"},
                               {.id = 2, .label = "Second choice"},
                               {.id = 3, .label = "Third choice"}});
        (*choices_).bind(model_.selection);
        const std::shared_ptr<gf::Label> first_body = gf::make_control<gf::Label>(gf::StableId("first.body"), "Both sliders share one Value<double>. Neither needs a quiet flag.");
        const std::shared_ptr<gf::Label> second_body = gf::make_control<gf::Label>(gf::StableId("second.body"), "Rebuild replaces toolbar items without replacing its group subscription.");
        (*first_body).set_bounds({0.0, 0.0, 712.0, 64.0});
        (*second_body).set_bounds({0.0, 0.0, 712.0, 64.0});
        (*sections_).single_open = true;
        (*sections_).set_items({{.id = 1, .label = "Shared scalar state", .content = first_body, .expanded = &model_.first_open},
                                {.id = 2, .label = "Collection lifetime", .content = second_body, .expanded = &model_.second_open}});
        gf::on((*commands_).invoked(), *this, &GalleryBehaviour::on_command);
        gf::on((*choices_).changed(), *this, &GalleryBehaviour::on_choice);
        gf::on((*sections_).toggled(), *this, &GalleryBehaviour::on_section);
    }

private:
    void rebuild_commands() {
        const std::string run_label = alternate_ ? "Run again" : "Run";
        (*commands_).set_items({{.id = 1, .label = run_label, .shortcut = gf::KeyGesture{gf::PhysicalKey::r, gf::Modifier::control}, .command = &model_.run},
                                {.id = 2, .label = "Enable / disable Run"},
                                {.id = 3, .label = "Rebuild items"}});
    }
    void on_command(const gf::CommandItem& item) {
        if (item.id == 2) model_.run.set_enabled(!model_.run.state().enabled);
        if (item.id == 3) { alternate_ = !alternate_; rebuild_commands(); }
        const std::string text = "Command item " + std::to_string(item.id);
        (*status_).text = text;
    }
    void on_choice(const int index) {
        const std::string text = "Selected choice " + std::to_string(index);
        (*status_).text = text;
    }
    void on_section(const int index, const bool expanded) {
        const std::string state = expanded ? " opened" : " closed";
        const std::string text = "Section " + std::to_string(index) + state;
        (*status_).text = text;
    }

    GalleryModel& model_;
    std::shared_ptr<gf::CommandBar> commands_{};
    std::shared_ptr<gf::ChoiceGroup> choices_{};
    std::shared_ptr<gf::ExpandableSections> sections_{};
    std::shared_ptr<gf::Label> status_{};
    bool alternate_{};
};

int main() {
    // The program's application (APP_WINDOW_RULES_DESIGN A.2): windows made
    // from here join it, and it runs them.
    gf::ApplicationCreateResult created = gf::Application::create(gf::ApplicationConfig{});
    if (!created) return 1;
    gf::Application& application = *created.application;
    try {
        // The model first: the window's controls keep their bindings to it.
        GalleryModel model{};
        std::unique_ptr<CollectionGalleryWindow> window = std::make_unique<CollectionGalleryWindow>(model);
        GalleryBehaviour behaviour(*window, model);
        const gf::ApplicationResult result = application.run(std::move(window),
            {.title = std::string(CollectionGalleryWindow::window_title),
             .initial_size = {760.0, 610.0}, .minimum_size = {760.0, 610.0}});
        // Once, after the last run: ends the rendering library for the process.
        const gf::ApplicationResult ended = gf::Application::shutdown();
        if (!result.accepted() || !ended.accepted()) return 1;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
