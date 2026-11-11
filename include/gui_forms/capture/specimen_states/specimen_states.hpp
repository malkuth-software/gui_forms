#pragma once

// Showing a control in one of its declared states for a specimen (a theme
// palette, a contact sheet or a test; owner request 2026-11-11), without
// sending input. Each state takes one route, chosen here once for every
// control:
//
// - value: the control's own value or status, written as code would write
//   it: `enabled` false for disabled; the `read_only` property; the
//   `checked` property, else `check_state`, for checked; `three_state`
//   then `check_state` for indeterminate; the `selected` property, else
//   `selected_index` 0, for selected; set_visual_status for invalid and
//   pending. These raise the control's ordinary change events, as any
//   write does.
// - presentation: Control::set_specimen_states, for the states only input,
//   focus or a transition gives (hot, pressed, captured, focused,
//   focus_visible, preselect, current, active_tool, unavailable, the
//   disclosure, drag and participation states, deactivated) and for every
//   custom state. Nothing but the published states changes.
// - environment: high_contrast and reduced_motion belong to the window's
//   presentation settings; the caller draws them with those settings.
// - rest: normal and enabled are what a fresh control shows.
//
// A state reached through a value falls back to presentation when the
// control has no such property (a value state declared only for its
// look). Show one state on a fresh control: states shown one after another
// accumulate. UI thread.

#include "gui_forms/control.hpp"

#include <cstdint>
#include <string>

namespace gui_forms {

enum class SpecimenStateRoute : std::uint8_t {
    rest,
    value,
    presentation,
    environment,
    // The control does not declare the state; nothing changed.
    not_declared,
    // A value write refused; `detail` says which. The control may have
    // taken the writes before it (three_state before check_state).
    refused,
};

struct SpecimenStateResult final {
    SpecimenStateRoute route{SpecimenStateRoute::rest};
    // The property written ("checked"), or for refused the refusal's text;
    // empty otherwise.
    std::string detail{};
};

// Shows `state` on `control` by its route, above. Never throws for a state
// the control cannot take: that is not_declared or refused.
[[nodiscard]] SpecimenStateResult show_specimen_state(Control& control, AnatomyState state);
// Shows custom state `state` (a position in anatomy().custom_states) by
// presentation; not_declared past the declaration.
[[nodiscard]] SpecimenStateResult show_specimen_custom_state(Control& control, CustomStateIndex state);
// The route name ("value") for manifests and logs.
[[nodiscard]] std::string_view specimen_state_route_name(SpecimenStateRoute route) noexcept;

} // namespace gui_forms
