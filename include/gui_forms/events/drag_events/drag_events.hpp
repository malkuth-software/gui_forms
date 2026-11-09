#pragma once
// The typed drag interactions (roadmap 2.1, phase E4; the declarative
// dragging study, section 5.3): the record a control raises at each step of
// a drag the window's drag engine runs for it, and the four events that
// carry it. A control that is a drag client lists them in its interaction
// schema (drag_started, drag_target_changed, drag_dropped, drag_ended), so
// rules reach them as interaction triggers (trigger::interaction) and the C
// ABI by their schema names; a program action reads the record through
// RuleFiring::drag.
//
// Order within one session: drag_started once, when the drag is planned
// (past the press threshold, or at once for a keyboard drag); then
// drag_target_changed each time the selected drop candidate changes
// (continuous drags, such as a splitter's, raise none: their value has its
// own event); at the end, after the control has committed or restored its
// model, drag_dropped for a committing end and then drag_ended for every
// end. A handler may cancel the drag or start another; the engine raises
// nothing further for a session that has ended.

#include "gui_forms/event/event/event.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cstdint>

namespace gui_forms {

// How a drag ended. Only dropped (onto a candidate) and free (a continuous
// value or a free placement) commit; every other end restores the model.
enum class DragEnd : std::uint8_t {
    // The session is still running (the record of a start or a target).
    none,
    dropped,
    free,
    // Released where nothing is accepted.
    rejected,
    // Escape.
    escaped,
    capture_lost,
    // The window was deactivated.
    deactivated,
    // The client found its model changed under the drag.
    model_changed,
    // The dragged control went away.
    subject_gone,
};

// Whether the pointer or the keyboard drives the drag.
enum class DragInputMode : std::uint8_t { pointer, keyboard };

// One drag interaction. `candidate` is the client's token for the selected
// drop candidate (an index into its own table) when `has_candidate`;
// `value` is a continuous drag's value when `has_value`; an end that
// restores carries neither (zero and false). `pointer` is in the
// window's coordinates (the virtual pointer for a keyboard drag). `end` is
// none except in drag_dropped and drag_ended.
struct DragInteraction final {
    DragInputMode mode{DragInputMode::pointer};
    bool has_candidate{};
    std::uint32_t candidate{};
    bool has_value{};
    double value{};
    Point pointer{};
    DragEnd end{DragEnd::none};
};

// The four events of a drag client, held by the control and raised by the
// window's drag engine on the window's thread. The control exposes each by
// its schema name.
struct DragInteractionEvents final {
    Event<const DragInteraction&> started;
    Event<const DragInteraction&> target_changed;
    Event<const DragInteraction&> dropped;
    Event<const DragInteraction&> ended;
};

} // namespace gui_forms
