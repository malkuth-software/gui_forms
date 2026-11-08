#include <gui_forms/application.hpp>
#include <gui_forms/collection_controls.hpp>
#include <gui_forms/range_controls.hpp>

#include <iostream>
#include <memory>
#include <string>

namespace gf = gui_forms;

class Gallery final : public gf::Component {
public:
    Gallery() {
        root = gf::make_control<gf::Panel>(gf::StableId("gallery"));
        commands = gf::make_control<gf::CommandBar>(gf::StableId("commands"));
        choices = gf::make_control<gf::ChoiceGroup>(gf::StableId("choices"));
        sections = gf::make_control<gf::ExpandableSections>(gf::StableId("sections"));
        status = gf::make_control<gf::Label>(gf::StableId("status"), "Ready — arrows navigate groups; Tab leaves the command bar.");
        (*commands).set_bounds({24.0, 20.0, 712.0, 40.0});
        (*choices).set_bounds({24.0, 220.0, 712.0, 52.0});
        (*sections).set_bounds({24.0, 300.0, 712.0, 220.0});
        (*status).set_bounds({24.0, 540.0, 712.0, 44.0});
        (*root).add_child(commands);
        (*root).add_child(choices);
        (*root).add_child(sections);
        (*root).add_child(status);
        rebuild_commands();
        (*choices).set_items({{.id = 1, .label = "First choice"},
                             {.id = 2, .label = "Second choice"},
                             {.id = 3, .label = "Third choice"}});
        (*choices).bind(selection);
        const std::shared_ptr<gf::TrackBar> first = gf::make_control<gf::TrackBar>(gf::StableId("volume.first"));
        const std::shared_ptr<gf::TrackBar> second = gf::make_control<gf::TrackBar>(gf::StableId("volume.second"));
        (*first).set_bounds({24.0, 84.0, 340.0, 42.0});
        (*second).set_bounds({396.0, 84.0, 340.0, 42.0});
        (*first).bind(volume);
        (*second).bind(volume);
        (*root).add_child(first);
        (*root).add_child(second);
        music.set_checked(true);
        const std::shared_ptr<gf::CheckBox> left = gf::make_control<gf::CheckBox>(gf::StableId("music.left"), "Music — shared command");
        const std::shared_ptr<gf::CheckBox> right = gf::make_control<gf::CheckBox>(gf::StableId("music.right"), "Music — second presentation");
        (*left).set_bounds({24.0, 150.0, 340.0, 36.0});
        (*right).set_bounds({396.0, 150.0, 340.0, 36.0});
        (*left).bind(music);
        (*right).bind(music);
        (*root).add_child(left);
        (*root).add_child(right);
        const std::shared_ptr<gf::Label> first_body = gf::make_control<gf::Label>(gf::StableId("first.body"), "Both sliders share one Value<double>. Neither needs a quiet flag.");
        const std::shared_ptr<gf::Label> second_body = gf::make_control<gf::Label>(gf::StableId("second.body"), "Rebuild replaces toolbar items without replacing its group subscription.");
        (*first_body).set_bounds({0.0, 0.0, 712.0, 64.0});
        (*second_body).set_bounds({0.0, 0.0, 712.0, 64.0});
        (*sections).set_single_open(true);
        (*sections).set_items({{.id = 1, .label = "Shared scalar state", .content = first_body, .expanded = &first_open},
                              {.id = 2, .label = "Collection lifetime", .content = second_body, .expanded = &second_open}});
        gf::on((*commands).invoked(), *this, &Gallery::on_command);
        gf::on((*choices).changed(), *this, &Gallery::on_choice);
        gf::on((*sections).toggled(), *this, &Gallery::on_section);
    }

    std::shared_ptr<gf::Panel> root{};
    std::shared_ptr<gf::CommandBar> commands{};
    std::shared_ptr<gf::ChoiceGroup> choices{};
    std::shared_ptr<gf::ExpandableSections> sections{};
    std::shared_ptr<gf::Label> status{};
    gf::Command run{"run", "Run"};
    gf::Command music{"music", "Music"};
    gf::Value<double> volume{40.0};
    gf::Value<int> selection{1};
    gf::Value<bool> first_open{true};
    gf::Value<bool> second_open{false};

private:
    void rebuild_commands() {
        const std::string run_label = alternate_ ? "Run again" : "Run";
        (*commands).set_items({{.id = 1, .label = run_label, .shortcut = gf::KeyGesture{gf::PhysicalKey::r, gf::Modifier::control}, .command = &run},
                              {.id = 2, .label = "Enable / disable Run"},
                              {.id = 3, .label = "Rebuild items"}});
    }
    void on_command(const gf::CommandItem& item) {
        if (item.id == 2) run.set_enabled(!run.state().enabled);
        if (item.id == 3) { alternate_ = !alternate_; rebuild_commands(); }
        const std::string text = "Command item " + std::to_string(item.id);
        (*status).text = text;
    }
    void on_choice(const int index) {
        const std::string text = "Selected choice " + std::to_string(index);
        (*status).text = text;
    }
    void on_section(const int index, const bool expanded) {
        const std::string state = expanded ? " opened" : " closed";
        const std::string text = "Section " + std::to_string(index) + state;
        (*status).text = text;
    }
    bool alternate_{};
};

int main() {
    // The program's application (APP_WINDOW_RULES_DESIGN A.2): windows made
    // from here join it, and it runs them.
    gf::ApplicationCreateResult created = gf::Application::create(gf::ApplicationConfig{});
    if (!created) return 1;
    gf::Application& application = *created.application;
    try {
        Gallery gallery{};
        std::unique_ptr<gf::Window> window = std::make_unique<gf::Window>(gallery.root, gf::Size{760.0, 610.0});
        const gf::ApplicationResult result = application.run(std::move(window),
            {.title = "GUI.Forms — shared state and collections",
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
