#pragma once

// The stock specimen catalogue (owner request 2026-11-11, the theme
// palette): one representative instance of every stock control type,
// showing its real look: realistic sample content (rows, columns, items,
// pages, menu items with shortcuts and submenus, expanded tree nodes,
// child controls inside containers, a plot with data), a size that suits
// its family, and fixed dates and values (no wall-clock "today"), made by
// name for a palette, a contact sheet or a test. It is complete by
// construction: tools/stock_specimens.py writes one row per control type
// of the control schema (manifests/control-schema.json, kind `control`) in
// schema order, the same types stock_control_anatomies() holds, and
// gui_forms_offscreen_capture_tests fails when an anatomy has no row or a
// row's capture at rest is near-uniform (stock_specimen_uniform_share)
// without a stated reason (StockSpecimen::blank_reason). A new control gets
// the generic row (constructed from its stable ID, its text set where it
// has one) when the tool runs, which that test then rejects until the
// control has content or a maker of its own in stock_specimens.cpp.
//
// Instances are fresh each time: a palette shows one state per instance,
// since states shown one after another accumulate (show_specimen_state).

#include "gui_forms/control.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms {

struct StockSpecimen;
struct WindowCapture;
class Window;

// What a window holds for one specimen. `subject` is the control of the
// row's type, which a palette captures and shows states on; `root` is what
// the window holds: the subject itself, or a holder that owns what the
// subject borrows (a PageSet, a DockWorkspace, an overlay's anchor) and
// lays the subject over its whole client area. Keep `root` alive while
// `subject` is used.
// `frame` is what a palette captures: the subject, or, for a part that
// only shows inside its container (a tab page, a wizard page, a plot's
// axis, a splitter between two panels), the container.
struct StockSpecimenInstance final {
    Control::Ptr root{};
    Control::Ptr subject{};
    Control::Ptr frame{};
};

// Makes `specimen`'s instance, its controls' stable IDs starting with `id`.
using StockSpecimenMaker = StockSpecimenInstance (*)(const StockSpecimen& specimen, StableId id);
// Finishes an instance once its root is in `window` and laid out: what
// needs the window (an image in its registry, a modal layer shown over the
// root). Throws what the window's own call throws, or std::runtime_error
// when the window refuses (an image not loaded).
using StockSpecimenPresenter = void (*)(const StockSpecimenInstance& instance, Window& window);

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
    // Null when the instance needs nothing from its window.
    StockSpecimenPresenter present{};
    // Why a near-uniform capture at rest is this control's real look (a
    // base that paints nothing of its own); empty for every other row.
    std::string_view blank_reason;
};

// A capture whose most frequent colour covers at least this share of the
// subject's pixels (stock_specimen_dominant_share) is near-uniform: an
// empty specimen.
inline constexpr double stock_specimen_uniform_share = 0.97;

// Every row, in schema order. Static storage.
[[nodiscard]] std::span<const StockSpecimen> stock_specimens() noexcept;
// The row for `control_id`, or null.
[[nodiscard]] const StockSpecimen* find_stock_specimen(std::string_view control_id) noexcept;
// `specimen`'s instance (specimen.make), IDs starting with `id`. Makers
// write only ordinary property values; a refused write is the write's own
// exception (std::invalid_argument or std::logic_error). UI thread.
// `frame` is the subject when the maker names none.
[[nodiscard]] StockSpecimenInstance make_stock_specimen(const StockSpecimen& specimen, StableId id);
// Runs `specimen.present` on `instance` (nothing when it has none), once
// `instance.root` is in `window` and laid out. UI thread.
void present_stock_specimen(const StockSpecimen& specimen, const StockSpecimenInstance& instance, Window& window);

// The share, in [0, 1], of the pixels of `capture` inside `inset_pixels`
// from each edge that have its most frequent BGRA value; 1 when that
// region is empty. `inset_pixels` leaves out a margin and the subject's
// own border. Allocates one copy of the region's pixels.
[[nodiscard]] double stock_specimen_dominant_share(const WindowCapture& capture, std::uint32_t inset_pixels);

} // namespace gui_forms
