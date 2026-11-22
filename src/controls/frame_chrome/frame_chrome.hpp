#pragma once

// The chrome of a frame drawn inside a window (DK decision 61): the in-window
// dialog's (TaskDialog) and an MDI document frame's (DockSite's document
// frames), and the caption buttons a frame shows on its caption or, while
// maximized, on the main menu strip (MenuStrip's document frame buttons).
// Drawn by the theme engine in one set of parts, so a theme styles every
// in-window frame alike: the face is the card family's, the caption band and
// its label the panel family's (the active frame's band `current`, with the
// accent keyline), the buttons the menu-item family's actuators and glyphs.
// Private to the controls' sources.

#include "gui_forms/control.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <string_view>

namespace gui_forms::frame_chrome {

// One frame's chrome, in the surface's coordinates. An empty caption draws
// no band (a frame whose caption is hidden); `edge` draws the face's edge
// (a document frame's border); `accent` draws the caption's accent keyline
// along its top, `accent_height` tall.
struct FrameChromePaint final {
    Rect face{};
    bool edge{};
    Rect caption{};
    Rect label{};
    std::string_view title{};
    FontSpec font{};
    AnatomyStateSet caption_states{};
    bool accent{};
    double accent_height{2.0};
};

void paint_frame_chrome(Painter& painter, const Control& surface, const FrameChromePaint& chrome);

// A caption button's glyph. The vocabulary has no window-state marks: the
// minimize button draws the underline mark, maximize the dock-shape mark
// (its whole-pane region), restore the dialog-launcher mark.
enum class FrameButtonGlyph : std::uint8_t { minimize, maximize, restore, close };

// One caption button: its plate (flat at rest, a plate when hot or pressed)
// and its glyph, centred.
void paint_frame_button(Painter& painter, const Control& surface, Rect button, Rect glyph, FrameButtonGlyph mark,
                        const AnatomyStateSet& states);

} // namespace gui_forms::frame_chrome
