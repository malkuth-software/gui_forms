/* The help table (since 1.2, roadmap rows 3.7 and 2.13): a
 * help_book_system's records loaded from a language file, F1 resolution
 * over a control's ancestors, and the refusals; then the language files: a
 * control's help fields as text properties, the template export, a user
 * override read back with its origin, the way back, a translation and the
 * language rule for user files. Opening the viewer, the popup and What's
 * This need a running window and are left to the C++ help system tests. */

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

static const char user_locale[] =
    "<locale xml:lang=\"en\">\n"
    "  <controls>\n"
    "    <control name=\"kit\">\n"
    "      <control name=\"help\" help-line=\"My own words for the panel.\"/>\n"
    "    </control>\n"
    "  </controls>\n"
    "</locale>\n";

static const char french_locale[] =
    "<locale xml:lang=\"fr\" name=\"Fran\xc3\xa7" "ais\">\n"
    "  <controls>\n"
    "    <control name=\"kit\">\n"
    "      <control name=\"help\" help-line=\"Le panneau d'aide.\"/>\n"
    "    </control>\n"
    "  </controls>\n"
    "</locale>\n";

/* Reads one field of a record into text (terminated) and its origin. */
static gf_status read_help_field(gf_kit* kit, gf_object system, const char* stable_id, gf_locale_field field,
                                 char* text, uint64_t capacity, gf_locale_origin* origin) {
    uint64_t required = 0u;
    const gf_status read =
        (*kit).help.read_field(system, gf_kit_text(stable_id), field, text, capacity - 1u, &required, origin);
    if (read == GF_STATUS_OK) {
        text[required] = '\0';
    }
    return read;
}

/* The language files through the help table and the property table. The
 * panel becomes a window's root so its controls' program values count. */
static void help_language_files(gf_kit* kit, gf_object system, gf_object panel, gf_object label) {
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(panel, &window), GF_STATUS_OK, "make the help panel a window's root");
    gf_kit_status(kit, (*kit).help.attach(system, window), GF_STATUS_OK, "attach the window to the help system");

    /* A control's help fields are ordinary text properties. */
    gf_kit_status(kit, gf_kit_set_text(kit, label, "help_line", "The caption, set in code."), GF_STATUS_OK,
                  "a control's help line is a text property");
    char text[2048];
    uint64_t required = 0u;
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(gf_value);
    gf_kit_status(kit, (*kit).properties.get(label, gf_kit_text("help_line"), &value, text, sizeof(text), &required),
                  GF_STATUS_OK, "read the help line back");
    gf_kit_check(kit, value.kind == GF_VALUE_KIND_TEXT && gf_kit_view_equals(value.text, "The caption, set in code."),
                 "the property reads the program's value");
    gf_kit_status(kit, gf_kit_set_text(kit, label, "keywords", "caption;;help"), GF_STATUS_INVALID_ARGUMENT,
                  "a keyword list with an empty keyword is refused");
    gf_kit_status(kit, gf_kit_set_text(kit, label, "keywords", "caption; help, panel"), GF_STATUS_OK,
                  "a well-formed keyword list is kept");

    gf_locale_origin origin = 99u;
    gf_kit_status(kit, read_help_field(kit, system, "kit.help.caption", GF_LOCALE_FIELD_HELP_LINE, text, sizeof(text),
                                       &origin),
                  GF_STATUS_OK, "read the caption's help line through the help system");
    gf_kit_check(kit, origin == GF_LOCALE_ORIGIN_PROGRAM && strcmp(text, "The caption, set in code.") == 0,
                 "a value set in code is the program layer");

    /* The template export: two calls, the size first. */
    required = 0u;
    gf_kit_status(kit, (*kit).help.export_template(system, gf_kit_text(""), text, 1u, &required),
                  GF_STATUS_BUFFER_TOO_SMALL, "the template follows the buffer rule");
    gf_kit_check(kit, required > 1u && required < sizeof(text), "the template's size is reported");
    gf_kit_status(kit, (*kit).help.export_template(system, gf_kit_text(""), text, sizeof(text) - 1u, &required),
                  GF_STATUS_OK, "export the template");
    text[required] = '\0';
    gf_kit_check(kit, strstr(text, "xml:lang=\"en\"") != NULL && strstr(text, "help-line=\"The help panel.\"") != NULL &&
                          strstr(text, "help-line=\"The caption, set in code.\"") != NULL &&
                          strstr(text, "keywords=\"caption; help, panel\"") != NULL,
                 "the template lists the built-in file's and the program's help fields");
    gf_kit_status(kit, (*kit).help.export_template(system, gf_kit_text("Not A Tag"), text, sizeof(text), &required),
                  GF_STATUS_INVALID_ARGUMENT, "a template language must be a language tag");

    /* A user override wins, and the way back sets it aside. */
    gf_kit_status(kit, (*kit).help.load_user_overrides(system, gf_kit_text(user_locale)), GF_STATUS_OK,
                  "load the user's own file");
    gf_kit_status(kit, read_help_field(kit, system, "kit.help", GF_LOCALE_FIELD_HELP_LINE, text, sizeof(text), &origin),
                  GF_STATUS_OK, "read the overridden help line");
    gf_kit_check(kit, origin == GF_LOCALE_ORIGIN_USER && strcmp(text, "My own words for the panel.") == 0,
                 "the user's override is read back");
    gf_kit_status(kit, read_help_field(kit, system, "kit.help", GF_LOCALE_FIELD_TEXT, text, sizeof(text), &origin),
                  GF_STATUS_OK, "read a field the user file leaves alone");
    gf_kit_check(kit, origin == GF_LOCALE_ORIGIN_BUILT_IN && strcmp(text, "Help") == 0,
                 "fields the override leaves out come from the built-in text");
    gf_kit_status(kit, (*kit).help.use_built_in_language(system, 1u), GF_STATUS_OK, "take the way back");
    gf_kit_status(kit, read_help_field(kit, system, "kit.help", GF_LOCALE_FIELD_HELP_LINE, text, sizeof(text), &origin),
                  GF_STATUS_OK, "read the help line on the way back");
    gf_kit_check(kit, origin == GF_LOCALE_ORIGIN_BUILT_IN && strcmp(text, "The help panel.") == 0,
                 "the way back shows the built-in text");
    gf_kit_status(kit, (*kit).help.use_built_in_language(system, 2u), GF_STATUS_INVALID_ARGUMENT,
                  "built_in_only is 0 or 1");
    gf_kit_status(kit, (*kit).help.use_built_in_language(system, 0u), GF_STATUS_OK, "leave the way back");
    gf_kit_status(kit, (*kit).help.export_template(system, gf_kit_text(""), text, sizeof(text) - 1u, &required),
                  GF_STATUS_OK, "export the template again");
    text[required] = '\0';
    gf_kit_check(kit, strstr(text, "My own words") == NULL, "the template never carries the user's overrides");
    gf_kit_status(kit, (*kit).help.clear_user_overrides(system), GF_STATUS_OK, "drop the user's file");
    gf_kit_status(kit, read_help_field(kit, system, "kit.help", GF_LOCALE_FIELD_HELP_LINE, text, sizeof(text), &origin),
                  GF_STATUS_OK, "read the help line without the user's file");
    gf_kit_check(kit, origin == GF_LOCALE_ORIGIN_BUILT_IN, "without the user's file the built-in text shows");

    /* A translation; a user file must be for the language in force. */
    gf_kit_status(kit, (*kit).help.load_translation(system, gf_kit_text("<locale/>")), GF_STATUS_INVALID_ARGUMENT,
                  "a translation without a language is refused");
    gf_kit_status(kit, (*kit).help.load_translation(system, gf_kit_text(french_locale)), GF_STATUS_OK,
                  "load the French translation");
    gf_kit_status(kit, read_help_field(kit, system, "kit.help", GF_LOCALE_FIELD_HELP_LINE, text, sizeof(text), &origin),
                  GF_STATUS_OK, "read the translated help line");
    gf_kit_check(kit, origin == GF_LOCALE_ORIGIN_TRANSLATION && strcmp(text, "Le panneau d'aide.") == 0,
                 "the translation's field shows");
    gf_kit_status(kit, (*kit).help.load_user_overrides(system, gf_kit_text(user_locale)), GF_STATUS_INVALID_ARGUMENT,
                  "an English user file is refused while French is in force");
    gf_kit_status(kit, read_help_field(kit, system, "kit.help", GF_LOCALE_FIELD_KEYWORDS + 1u, text, sizeof(text),
                                       &origin),
                  GF_STATUS_INVALID_ARGUMENT, "an unknown field is refused");
    gf_kit_status(kit, read_help_field(kit, system, "kit.nothing", GF_LOCALE_FIELD_HELP, text, sizeof(text), &origin),
                  GF_STATUS_OK, "read a record no layer holds");
    gf_kit_check(kit, origin == GF_LOCALE_ORIGIN_NONE && text[0] == '\0', "a missing field is empty, from no layer");

    gf_kit_status(kit, (*kit).help.detach(system, window), GF_STATUS_OK, "detach the window");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the help window");
}

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

    help_language_files(kit, system, panel, label);

    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the caption");
    gf_kit_status(kit, (*kit).objects.release(panel), GF_STATUS_OK, "release the help panel");
    gf_kit_status(kit, (*kit).objects.release(system), GF_STATUS_OK, "release the help system");
}
