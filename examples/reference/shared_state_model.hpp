#pragma once

// The state forms/shared_state.form binds: `run` and `volume`.

#include <gui_forms/commands.hpp>
#include <gui_forms/component.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/value.hpp>

namespace reference {

class SharedModel final : public gui_forms::Component {
public:
    SharedModel() { gui_forms::on(run.invoked(), *this, &SharedModel::on_run); }
    gui_forms::Command run{};
    gui_forms::Value<double> volume{25.0};
    int runs{};

private:
    void on_run() { ++runs; }
};

} // namespace reference
