#include "gui_forms/docking/types/dock_types.hpp"

#include <utility>

namespace gui_forms {

DockEdit DockEdit::dock_into(DockSubject subject, DockNodeId target, std::uint32_t index) {
    DockEdit edit{};
    edit.kind = DockEditKind::dock_into;
    edit.subject = std::move(subject);
    edit.target = target;
    edit.index = index;
    return edit;
}

DockEdit DockEdit::dock_beside(DockSubject subject, DockNodeId target, DockSide side,
                               double ratio) {
    DockEdit edit{};
    edit.kind = DockEditKind::dock_beside;
    edit.subject = std::move(subject);
    edit.target = target;
    edit.side = side;
    edit.ratio = ratio;
    return edit;
}

DockEdit DockEdit::dock_at_edge(DockSubject subject, DockSide side, double ratio) {
    DockEdit edit{};
    edit.kind = DockEditKind::dock_at_edge;
    edit.subject = std::move(subject);
    edit.side = side;
    edit.ratio = ratio;
    return edit;
}

DockEdit DockEdit::float_subject(DockSubject subject, Rect frame, std::string monitor_id) {
    DockEdit edit{};
    edit.kind = DockEditKind::float_subject;
    edit.subject = std::move(subject);
    edit.frame = frame;
    edit.monitor_id = std::move(monitor_id);
    return edit;
}

DockEdit DockEdit::auto_hide(DockSubject subject, DockSide edge, double extent) {
    DockEdit edit{};
    edit.kind = DockEditKind::auto_hide;
    edit.subject = std::move(subject);
    edit.side = edge;
    edit.extent = extent;
    return edit;
}

DockEdit DockEdit::unpin(DockNodeId stack) {
    DockEdit edit{};
    edit.kind = DockEditKind::unpin;
    edit.subject = DockSubject::of_stack(stack);
    return edit;
}

DockEdit DockEdit::hide(DockPanelId panel) {
    DockEdit edit{};
    edit.kind = DockEditKind::hide;
    edit.subject = DockSubject::of_panel(std::move(panel));
    return edit;
}

DockEdit DockEdit::show(DockPanelId panel) {
    DockEdit edit{};
    edit.kind = DockEditKind::show;
    edit.subject = DockSubject::of_panel(std::move(panel));
    return edit;
}

DockEdit DockEdit::close(DockPanelId panel) {
    DockEdit edit{};
    edit.kind = DockEditKind::close;
    edit.subject = DockSubject::of_panel(std::move(panel));
    return edit;
}

DockEdit DockEdit::move(DockPanelId panel, std::uint32_t index) {
    DockEdit edit{};
    edit.kind = DockEditKind::move;
    edit.subject = DockSubject::of_panel(std::move(panel));
    edit.index = index;
    return edit;
}

DockEdit DockEdit::select(DockPanelId panel) {
    DockEdit edit{};
    edit.kind = DockEditKind::select;
    edit.subject = DockSubject::of_panel(std::move(panel));
    return edit;
}

DockEdit DockEdit::set_ratio(DockNodeId split, double ratio) {
    DockEdit edit{};
    edit.kind = DockEditKind::set_ratio;
    edit.target = split;
    edit.ratio = ratio;
    return edit;
}

DockEdit DockEdit::set_floating_frame(DockSiteId site, Rect frame, std::string monitor_id) {
    DockEdit edit{};
    edit.kind = DockEditKind::set_floating_frame;
    edit.site = site;
    edit.frame = frame;
    edit.monitor_id = std::move(monitor_id);
    return edit;
}

DockEdit DockEdit::set_document_presentation(DockDocumentPresentation presentation) {
    DockEdit edit{};
    edit.kind = DockEditKind::set_document_presentation;
    edit.presentation = presentation;
    return edit;
}

DockEdit DockEdit::set_frame_bounds(DockPanelId panel, Rect bounds) {
    DockEdit edit{};
    edit.kind = DockEditKind::set_frame_bounds;
    edit.subject = DockSubject::of_panel(std::move(panel));
    edit.frame = bounds;
    return edit;
}

DockEdit DockEdit::set_frame_state(DockPanelId panel, DockFrameState state) {
    DockEdit edit{};
    edit.kind = DockEditKind::set_frame_state;
    edit.subject = DockSubject::of_panel(std::move(panel));
    edit.frame_state = state;
    return edit;
}

DockEdit DockEdit::set_frame_icon_slot(DockPanelId panel, std::uint32_t slot) {
    DockEdit edit{};
    edit.kind = DockEditKind::set_frame_icon_slot;
    edit.subject = DockSubject::of_panel(std::move(panel));
    edit.index = slot;
    return edit;
}

} // namespace gui_forms
