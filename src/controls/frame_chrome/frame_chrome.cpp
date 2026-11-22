#include "frame_chrome.hpp"

// The in-window frames' shared chrome (frame_chrome.hpp).

namespace gui_forms::frame_chrome {

void paint_frame_chrome(Painter& painter, const Control& surface, const FrameChromePaint& chrome) {
    PartPaint part{};
    part.font = chrome.font;
    part.family = ControlVisualRole::card;
    part.role = PartRole::face;
    part.bounds = chrome.face;
    paint_part(painter, surface, part);
    if (chrome.edge) {
        part.role = PartRole::edge;
        paint_part(painter, surface, part);
    }
    if (chrome.caption.empty()) return;
    part.family = ControlVisualRole::panel;
    part.role = PartRole::face;
    part.states = chrome.caption_states;
    part.bounds = chrome.caption;
    paint_part(painter, surface, part);
    if (chrome.accent) {
        part.role = PartRole::keyline;
        part.bounds = Rect{chrome.caption.x, chrome.caption.y, chrome.caption.width,
                           chrome.caption.height < chrome.accent_height ? chrome.caption.height : chrome.accent_height};
        part.accent.set(AccentIndex{0U});
        paint_part(painter, surface, part);
        part.accent.reset();
    }
    part.role = PartRole::label;
    part.bounds = chrome.label;
    part.text = lend_paint_text(chrome.title);
    paint_part(painter, surface, part);
    part.text = {};
}

void paint_frame_button(Painter& painter, const Control& surface, Rect button, Rect glyph, FrameButtonGlyph mark,
                        const AnatomyStateSet& states) {
    PartPaint part{};
    part.family = ControlVisualRole::menu_item;
    part.states = states;
    part.role = PartRole::actuator;
    part.bounds = button;
    paint_part(painter, surface, part);
    part.role = PartRole::glyph;
    part.bounds = glyph;
    switch (mark) {
    case FrameButtonGlyph::minimize:
        part.mark = PartMark::underline;
        break;
    case FrameButtonGlyph::maximize:
        part.mark = PartMark::dock_shape;
        part.dock_region = DockShapeRegion::tab_into;
        break;
    case FrameButtonGlyph::restore:
        part.mark = PartMark::dialog_launcher;
        break;
    case FrameButtonGlyph::close:
        part.mark = PartMark::close;
        break;
    default:
        // A value outside the enumeration paints the button without a mark.
        break;
    }
    paint_part(painter, surface, part);
}

} // namespace gui_forms::frame_chrome
