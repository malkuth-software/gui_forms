#pragma once

// The stock specimen catalogue (owner request 2026-11-11, the theme
// palette): one representative instance of every stock control type, with
// modest sample content, made by name for a palette, a contact sheet or a
// test. It is complete by construction: tools/stock_specimens.py writes
// one row per control type of the control schema
// (manifests/control-schema.json, kind `control`) in schema order, the same
// types stock_control_anatomies() holds, and gui_forms_offscreen_capture_tests
// fails when an anatomy has no row. A new control gets the generic row
// (constructed from its stable ID, its text set where it has one) when the
// tool runs; a type that needs items or borrows something gets a maker of
// its own in stock_specimens.cpp.
//
// Instances are fresh each time: a palette shows one state per instance,
// since states shown one after another accumulate (show_specimen_state).

#include "gui_forms/control.hpp"

#include <span>
#include <string_view>

namespace gui_forms {

struct StockSpecimen;

// What a window holds for one specimen. `subject` is the control of the
// row's type, which a palette captures and shows states on; `root` is what
// the window holds: the subject itself, or a holder that owns what the
// subject borrows (a PageSet, a DockWorkspace, an overlay's anchor) and
// lays the subject over its whole client area. Keep `root` alive while
// `subject` is used.
struct StockSpecimenInstance final {
    Control::Ptr root{};
    Control::Ptr subject{};
};

// Makes `specimen`'s instance, its controls' stable IDs starting with `id`.
using StockSpecimenMaker = StockSpecimenInstance (*)(const StockSpecimen& specimen, StableId id);

struct StockSpecimen final {
    // The subject's control id, as stock_control_anatomies() names it
    // ("check_box"). A type without an anatomy of its own (a plot item)
    // publishes the base anatomy, "control", at run time.
    std::string_view control_id;
    // The registry's category (manifests/controls.json), a palette's family
    // ("Value setting").
    std::string_view family;
    // The logical size the subject is laid out at.
    Size size{};
    // The text set on a type with a writable `text` property ("Check box").
    std::string_view sample_text;
    StockSpecimenMaker make{};
};

// Every row, in schema order. Static storage.
[[nodiscard]] std::span<const StockSpecimen> stock_specimens() noexcept;
// The row for `control_id`, or null.
[[nodiscard]] const StockSpecimen* find_stock_specimen(std::string_view control_id) noexcept;
// `specimen`'s instance (specimen.make), IDs starting with `id`. Makers
// write only ordinary property values; a refused write is the write's own
// exception (std::invalid_argument or std::logic_error). UI thread.
[[nodiscard]] StockSpecimenInstance make_stock_specimen(const StockSpecimen& specimen, StableId id);

} // namespace gui_forms
