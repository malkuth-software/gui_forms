#pragma once

#include "gui_forms/controls/help_book.hpp"
#include "gui_forms/drawing.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace gui_forms {

// Printing help pages (help system study, section 8). The page is laid out
// for paper (every section open, links followed by their target's title in
// brackets), broken into sheets only between lines, and each sheet carries
// a header: the program's name at the left and the page's title at the
// right, over a rule, and a footer with the sheet number. Sending sheets to
// a printer is the host's print path, which GUI.Forms does not have yet:
// the program paints each sheet onto the painter its print path gives it.
struct HelpPrintOptions final {
    // The sheet in logical units (US Letter at 96 per inch by default).
    Size sheet{816.0, 1056.0};
    double margin{72.0};
    double header_height{28.0};
    double footer_height{24.0};
    GUI_FORMS_LOCALIZABLE(text) std::string program_title{};
    // "{0}" is replaced by the sheet number and "{1}" by the sheet count.
    GUI_FORMS_LOCALIZABLE(text) std::string footer_format{"Page {0} of {1}"};
};

// One sheet: the band of the print layout it shows, in content coordinates.
struct HelpPrintSheet final {
    double top{};
    double height{};
};

struct HelpPrintPlan final {
    double content_width{};
    double content_height{};
    std::vector<HelpPrintSheet> sheets{};
};

// Lays `book`'s page out for print and breaks it into sheets. A line taller
// than a sheet is cut at the sheet's foot. `plan` is replaced. The book
// must be attached to a window (its text needs the window's fonts).
void plan_help_print(HelpBook& book, const HelpPrintOptions& options, HelpPrintPlan& plan);

// Paints sheet `sheet` of `plan` with its top left at `origin`: header,
// content band and footer. Nothing for a sheet out of range.
void paint_help_print_sheet(Painter& painter, const HelpBook& book, const HelpPrintPlan& plan, std::size_t sheet,
                            const HelpPrintOptions& options, Point origin);

} // namespace gui_forms
