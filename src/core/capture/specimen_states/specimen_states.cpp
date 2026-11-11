#include "gui_forms/capture/specimen_states/specimen_states.hpp"

#include <exception>
#include <optional>
#include <string_view>
#include <utility>

namespace gui_forms {
namespace {

bool declares(const ControlAnatomy& anatomy, AnatomyState state) noexcept {
    for (const AnatomyState declared : anatomy.states) {
        if (declared == state) return true;
    }
    return false;
}

// Whether `control` has a writable property `name`.
bool writable_property(const Control& control, std::string_view name) {
    const std::optional<PropertyDescriptor> descriptor = control.property_descriptor(name);
    const bool writable = descriptor.has_value() && (*descriptor).writable;
    return writable;
}

SpecimenStateResult routed(SpecimenStateRoute route, std::string detail) {
    SpecimenStateResult result{};
    result.route = route;
    result.detail = std::move(detail);
    return result;
}

// Adds `state` to the control's specimen states (declared: checked by the
// caller).
SpecimenStateResult present(Control& control, AnatomyState state) {
    AnatomyStateSet shown = control.specimen_states();
    shown.insert(state);
    control.set_specimen_states(shown);
    SpecimenStateResult result = routed(SpecimenStateRoute::presentation, std::string());
    return result;
}

// One property write; refused with the write's own text.
SpecimenStateResult write_value(Control& control, std::string_view name, BindingValue value) {
    try {
        control.set_property_value(name, std::move(value));
    } catch (const std::exception& failure) {
        SpecimenStateResult refusal = routed(SpecimenStateRoute::refused, std::string(failure.what()));
        return refusal;
    }
    SpecimenStateResult result = routed(SpecimenStateRoute::value, std::string(name));
    return result;
}

SpecimenStateResult show_status(Control& control, ControlVisualStatus status, std::string_view name) {
    try {
        control.set_visual_status(status);
    } catch (const std::exception& failure) {
        SpecimenStateResult refusal = routed(SpecimenStateRoute::refused, std::string(failure.what()));
        return refusal;
    }
    SpecimenStateResult result = routed(SpecimenStateRoute::value, std::string(name));
    return result;
}

SpecimenStateResult show_checked(Control& control) {
    if (writable_property(control, "checked")) {
        SpecimenStateResult result = write_value(control, "checked", BindingValue(true));
        return result;
    }
    if (writable_property(control, "check_state")) {
        SpecimenStateResult result = write_value(control, "check_state", BindingValue(std::string("checked")));
        return result;
    }
    SpecimenStateResult result = present(control, AnatomyState::checked);
    return result;
}

SpecimenStateResult show_indeterminate(Control& control) {
    if (!writable_property(control, "check_state")) {
        SpecimenStateResult result = present(control, AnatomyState::indeterminate);
        return result;
    }
    // A two-state box coerces indeterminate to checked (CheckBox's write).
    if (writable_property(control, "three_state")) {
        const SpecimenStateResult three = write_value(control, "three_state", BindingValue(true));
        if (three.route == SpecimenStateRoute::refused) return three;
    }
    SpecimenStateResult result = write_value(control, "check_state", BindingValue(std::string("indeterminate")));
    return result;
}

SpecimenStateResult show_selected(Control& control) {
    if (writable_property(control, "selected")) {
        SpecimenStateResult result = write_value(control, "selected", BindingValue(true));
        return result;
    }
    if (writable_property(control, "selected_index")) {
        SpecimenStateResult result = write_value(control, "selected_index", BindingValue(std::int64_t{0}));
        return result;
    }
    SpecimenStateResult result = present(control, AnatomyState::selected);
    return result;
}

SpecimenStateResult show_read_only(Control& control) {
    if (writable_property(control, "read_only")) {
        SpecimenStateResult result = write_value(control, "read_only", BindingValue(true));
        return result;
    }
    SpecimenStateResult result = present(control, AnatomyState::read_only);
    return result;
}

SpecimenStateResult show_disabled(Control& control) {
    try {
        control.enabled = false;
    } catch (const std::exception& failure) {
        SpecimenStateResult refusal = routed(SpecimenStateRoute::refused, std::string(failure.what()));
        return refusal;
    }
    SpecimenStateResult result = routed(SpecimenStateRoute::value, std::string("enabled"));
    return result;
}

} // namespace

SpecimenStateResult show_specimen_state(Control& control, AnatomyState state) {
    if (!declares(control.anatomy(), state)) {
        SpecimenStateResult result = routed(SpecimenStateRoute::not_declared, std::string());
        return result;
    }
    switch (state) {
    case AnatomyState::normal:
    case AnatomyState::enabled:
        return routed(SpecimenStateRoute::rest, std::string());
    case AnatomyState::disabled:
        return show_disabled(control);
    case AnatomyState::read_only:
        return show_read_only(control);
    case AnatomyState::invalid:
        return show_status(control, ControlVisualStatus::invalid, "visual_status");
    case AnatomyState::pending:
        return show_status(control, ControlVisualStatus::pending, "visual_status");
    case AnatomyState::checked:
        return show_checked(control);
    case AnatomyState::indeterminate:
        return show_indeterminate(control);
    case AnatomyState::selected:
        return show_selected(control);
    case AnatomyState::high_contrast:
    case AnatomyState::reduced_motion:
        return routed(SpecimenStateRoute::environment, std::string());
    case AnatomyState::hot:
    case AnatomyState::pressed:
    case AnatomyState::captured:
    case AnatomyState::focused:
    case AnatomyState::focus_visible:
    case AnatomyState::preselect:
    case AnatomyState::current:
    case AnatomyState::active_tool:
    case AnatomyState::unavailable:
    case AnatomyState::open:
    case AnatomyState::closed:
    case AnatomyState::expanding:
    case AnatomyState::collapsing:
    case AnatomyState::interrupted:
    case AnatomyState::drop_accepted:
    case AnatomyState::drop_rejected:
    case AnatomyState::key:
    case AnatomyState::secondary_participating:
    case AnatomyState::drag_proximate:
    case AnatomyState::deactivated:
        return present(control, state);
    case AnatomyState::count:
        break;
    }
    // A value outside the vocabulary: not a state any control declares.
    return routed(SpecimenStateRoute::not_declared, std::string());
}

SpecimenStateResult show_specimen_custom_state(Control& control, CustomStateIndex state) {
    if (static_cast<std::size_t>(state.value) >= control.anatomy().custom_states.size()) {
        SpecimenStateResult result = routed(SpecimenStateRoute::not_declared, std::string());
        return result;
    }
    AnatomyStateSet shown = control.specimen_states();
    shown.insert(state);
    control.set_specimen_states(shown);
    SpecimenStateResult result = routed(SpecimenStateRoute::presentation, std::string());
    return result;
}

std::string_view specimen_state_route_name(SpecimenStateRoute route) noexcept {
    switch (route) {
    case SpecimenStateRoute::rest:
        return "rest";
    case SpecimenStateRoute::value:
        return "value";
    case SpecimenStateRoute::presentation:
        return "presentation";
    case SpecimenStateRoute::environment:
        return "environment";
    case SpecimenStateRoute::not_declared:
        return "not_declared";
    case SpecimenStateRoute::refused:
        return "refused";
    }
    return "unknown";
}

} // namespace gui_forms
