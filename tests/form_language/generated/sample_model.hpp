// The model the derived classes of sample_dialog.form and derived_panel.form
// bind (stage B): the sources their bindings name, and counts of the
// commands' invocations. Declare it before the window or control it binds:
// their bound controls keep their bindings to it.

#pragma once

#include "gui_forms/gui_forms.hpp"

namespace form_generated {

class SampleModel final {
public:
    SampleModel() {
        using Invoked = gui_forms::Delegate<const gui_forms::CommandInvocation&>;
        accepted_ = accept_.invoked().subscribe(Invoked::bind<SampleModel, &SampleModel::count_accept>(*this));
        dismissed_ = dismiss_.invoked().subscribe(Invoked::bind<SampleModel, &SampleModel::count_dismiss>(*this));
    }

    void count_accept(const gui_forms::CommandInvocation& invocation) {
        static_cast<void>(invocation);
        ++accept_calls;
    }
    void count_dismiss(const gui_forms::CommandInvocation& invocation) {
        static_cast<void>(invocation);
        ++dismiss_calls;
    }

    gui_forms::Value<bool> auto_save_{true};
    gui_forms::Value<double> level_{25.0};
    gui_forms::Command accept_{};
    gui_forms::Command dismiss_{};
    int accept_calls{};
    int dismiss_calls{};

private:
    gui_forms::SubscriptionToken accepted_{};
    gui_forms::SubscriptionToken dismissed_{};
};

} // namespace form_generated
