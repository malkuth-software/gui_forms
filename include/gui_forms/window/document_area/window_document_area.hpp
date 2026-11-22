#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/docking/types/dock_types.hpp"

#include <cstddef>
#include <cstdint>

#include <stxlib/optional.hpp>

namespace gui_forms {

class Window;

// The multiple document interface of a Window (DK decisions 60 to 63): the
// WinForms surface (IsMdiContainer, MdiChildren, ActiveMdiChild,
// ActivateMdiChild, LayoutMdi, MdiChildActivate) mapped onto a docking
// document area. An MDI child is a document panel of the area, named by its
// DockPanelId, never a second Window: there is no MDICLIENT and no
// WM_MDI* message.

// The arrangements Window::layout_mdi offers (WinForms' MdiLayout): the
// docking planner's DockFrameArrangement under its WinForms names.
enum class MdiLayout : std::uint8_t { cascade, tile_horizontal, tile_vertical, arrange_icons };

// What Window::mdi_child_activate reports: the active child before and
// after (none when there was none, or none is left).
struct GUI_FORMS_API MdiChildActivation final {
    stxlib::Optional<DockPanelId> previous{};
    stxlib::Optional<DockPanelId> current{};
};

// The document area a Window presents. The docking workspace implements it
// (gui_forms_controls), and its main DockSite binds it to the window it is
// attached to (Window::bind_document_area) and unbinds it when it leaves;
// the window's MDI calls go through it. A window holds at most one area,
// and an area is bound to at most one window: binding elsewhere unbinds it
// first. Destroying either side unbinds them. UI thread only.
class GUI_FORMS_API WindowDocumentArea {
public:
    WindowDocumentArea() = default;
    WindowDocumentArea(const WindowDocumentArea&) = delete;
    WindowDocumentArea& operator=(const WindowDocumentArea&) = delete;
    virtual ~WindowDocumentArea();

    // The window this area is bound to, or null.
    [[nodiscard]] Window* bound_window() const noexcept { return window_; }

    // Whether the area shows its documents as frames (the MDI look) rather
    // than as tabs, and switching between the two.
    [[nodiscard]] virtual bool document_frames_shown() const = 0;
    virtual void show_document_frames(bool frames) = 0;
    // The documents of the area (the MDI children), in tree and tab order.
    [[nodiscard]] virtual std::size_t document_count() const = 0;
    // Out of range: an empty identity.
    [[nodiscard]] virtual DockPanelId document_at(std::size_t index) const = 0;
    // The title the area shows for a document (its window-list row); empty
    // for one it does not hold. Valid until the area's titles change.
    [[nodiscard]] virtual BorrowedText document_title(const DockPanelId& document) const = 0;
    // The active document of the area, or none.
    [[nodiscard]] virtual stxlib::Optional<DockPanelId> active_document() const = 0;
    // Activates a document of the area (focus moves into it); false for one
    // the area does not hold.
    virtual bool activate_document(const DockPanelId& document) = 0;
    // Arranges the frames; false when there is nothing to arrange or the
    // area is not shown.
    virtual bool arrange_documents(MdiLayout layout) = 0;
    // Whether minimized frames line the bottom of the area, else its top.
    virtual void set_icons_at_bottom(bool bottom) = 0;

protected:
    // Raises the bound window's mdi_child_activate; nothing while unbound.
    void raise_document_activated(const MdiChildActivation& activation);

private:
    friend class Window;
    Window* window_{};
};

} // namespace gui_forms
