/* The help table (since 1.2, roadmap row 3.7): a help_book_system's records
 * loaded from a language file, F1 resolution over a control's ancestors,
 * and the refusals. Opening the viewer, the popup and What's This need a
 * running window and are left to the C++ help system tests. */

#include "gf_conformance_internal.h"

#include <string.h>

static const char help_locale[] =
    "<locale xml:lang=\"en\">\n"
    "  <controls>\n"
    "    <control name=\"kit\" text=\"Kit\" help-line=\"The kit.\">\n"
    "      <control name=\"help\" text=\"Help\" help-line=\"The help panel.\">\n"
    "        <help keywords=\"help; panel\">The panel the help table reads.</help>\n"
    "      </control>\n"
    "    </control>\n"
    "  </controls>\n"
    "</locale>\n";

void gf_kit_help(gf_kit* kit) {
    const gf_object system = gf_kit_create(kit, "help_book_system", "kit.help.system");
    const gf_object panel = gf_kit_create(kit, "panel", "kit.help");
    const gf_object label = gf_kit_create(kit, "label", "kit.help.caption");
    gf_kit_status(kit, (*kit).objects.insert_child(panel, label, 0u), GF_STATUS_OK, "put the caption in the help panel");

    gf_kit_status(kit, (*kit).help.load_built_in(system, gf_kit_text("<locale")), GF_STATUS_INVALID_ARGUMENT,
                  "a malformed language file is refused");
    gf_kit_status(kit, (*kit).help.load_built_in(system, gf_kit_text(help_locale)), GF_STATUS_OK,
                  "load the help records");

    char page[64];
    uint64_t required = 0u;
    uint32_t home = 7u;
    gf_kit_status(kit, (*kit).help.resolve(system, label, page, sizeof(page), &required, &home), GF_STATUS_OK,
                  "resolve F1 on the caption");
    gf_kit_check(kit, home == 0u && required == 8u && memcmp(page, "kit.help", 8u) == 0,
                 "F1 on a control without help opens its nearest ancestor's record");
    gf_kit_status(kit, (*kit).help.resolve(system, label, page, 2u, &required, &home), GF_STATUS_BUFFER_TOO_SMALL,
                  "resolve follows the buffer rule");

    uint32_t shown = 7u;
    gf_kit_status(kit, (*kit).help.show_navigator(system, GF_HELP_NAVIGATOR_SEARCH + 1u, gf_kit_text(""), &shown),
                  GF_STATUS_INVALID_ARGUMENT, "an unknown navigator is refused");
    gf_kit_check(kit, shown == 7u, "a refused call leaves its output");
    gf_kit_status(kit, (*kit).help.show(label, gf_kit_text("kit.help"), &shown), GF_STATUS_WRONG_HANDLE_KIND,
                  "a control is not a help system");
    uint32_t entered = 7u;
    gf_kit_status(kit, (*kit).help.enter_help_mode(system, label, &entered), GF_STATUS_WRONG_HANDLE_KIND,
                  "help mode needs a window");
    gf_kit_status(kit, (*kit).help.attach(system, panel), GF_STATUS_WRONG_HANDLE_KIND,
                  "attach needs a window");
    gf_kit_status(kit, (*kit).help.detach(system, panel), GF_STATUS_WRONG_HANDLE_KIND,
                  "detach needs a window");
    gf_kit_status(kit, (*kit).help.show_popup(system, gf_kit_text("kit.help"), system, &shown),
                  GF_STATUS_WRONG_HANDLE_KIND, "a popup's anchor is a control");
    gf_kit_status(kit, (*kit).help.cancel_help_mode(system), GF_STATUS_OK, "cancelling no help mode is harmless");

    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the caption");
    gf_kit_status(kit, (*kit).objects.release(panel), GF_STATUS_OK, "release the help panel");
    gf_kit_status(kit, (*kit).objects.release(system), GF_STATUS_OK, "release the help system");
}
