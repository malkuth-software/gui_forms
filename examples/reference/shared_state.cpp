#include <gui_forms/commands.hpp>
#include <gui_forms/range_controls.hpp>
#include <gui_forms/value.hpp>

namespace gf = gui_forms;

class Model final : public gf::Component {
public:
    Model() { gf::on(run.invoked(), *this, &Model::on_run); }
    gf::Command run{};
    gf::Value<double> volume{25.0};
    int runs{};
private:
    void on_run() { ++runs; }
};

int main() {
    Model model{};
    gf::Button button(gf::StableId("run"), "Run");
    gf::TrackBar first(gf::StableId("volume.first"));
    gf::TrackBar second(gf::StableId("volume.second"));
    button.bind(model.run);
    first.bind(model.volume);
    second.bind(model.volume);
    first.value = 60.0; // State change; no input-only scroll notification.
    button.perform_click();
    if (second.value.read() != 60.0 || model.volume.get() != 60.0 || model.runs != 1) return 1;
    model.run.set_enabled(false);
    if (button.perform_click()) return 1;
    return 0;
}
