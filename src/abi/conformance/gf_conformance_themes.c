/* Kit part: theme packages and plain sheets (load_sheet), their findings,
 * the installed library and published values (GF_TABLE_THEMES), and
 * tooltips as a property. Each fixture sheet's manifest carries the
 * SHA-256 of the sheet as written
 * here; a sheet changed without its digest is refused, which a check
 * below relies on. */

#include "gf_conformance_internal.h"

#include <stdio.h>
#include <string.h>

/* Sheets and the SHA-256 of each, as the manifests list them. */
static const char good_sheet[] = "button::part(face) { background-color: #3366cc }\n";
static const char good_digest[] = "81e992b31c95ced1aefc8425f93f738c6205e05afa2e8522678d9e0bee3ee59d";
static const char reading_sheet[] =
    "#kit-pad::part(face) { primitive: bulge; amplitude: clamp(0, var(--kit-level), 1) }\n";
static const char reading_digest[] = "90fdc1dfcffb02ddb8f84e62c364d83c9161bfbaa3e43223730e0d74a7e3d840";
static const char warning_sheet[] = "no-such-control { opacity: 0.5 }\nbutton { opacity: 0.9 }\n";
static const char warning_digest[] = "d7dfbd004e009ec7c4dfea16467fb94fcfcee6e907e0a41110c98a9e0dfce81b";

/* A package of one sheet, built in caller storage. */
typedef struct kit_package {
    char manifest[640];
    gf_theme_file file;
    gf_theme_package package;
} kit_package;

static gf_bytes_view bytes_of(const char* text) {
    gf_bytes_view view;
    view.data = (const uint8_t*)text;
    view.size = (uint64_t)strlen(text);
    return view;
}

/* published, when not null, is the one value the manifest lists. */
static void make_package(kit_package* made, const char* name, const char* sheet, const char* digest,
                         const char* published) {
    memset(made, 0, sizeof(*made));
    char listed[96] = "";
    if (published != NULL) {
        snprintf(listed, sizeof(listed), ", \"published_values\": [\"%s\"]", published);
    }
    snprintf((*made).manifest, sizeof((*made).manifest),
             "{\"format\": \"gui-forms-theme/1\", \"name\": \"%s\", \"version\": \"1.0.0\", \"engine\": \">=0.3\", "
             "\"sheet\": {\"path\": \"theme.theme.css\", \"sha256\": \"%s\"}%s}",
             name, digest, listed);
    (*made).file.path = gf_kit_text("theme.theme.css");
    (*made).file.bytes = bytes_of(sheet);
    (*made).package.struct_size = (uint32_t)sizeof(gf_theme_package);
    (*made).package.manifest = bytes_of((*made).manifest);
    (*made).package.files = &(*made).file;
    (*made).package.file_count = 1u;
}

static gf_theme_report empty_report(void) {
    gf_theme_report report;
    memset(&report, 0, sizeof(report));
    report.struct_size = (uint32_t)sizeof(report);
    return report;
}

/* Whether the theme in use has the id expected. */
static int theme_is(gf_kit* kit, const char* expected) {
    char id[128];
    uint64_t required = 0u;
    const gf_status read = (*kit).themes.theme_id(id, sizeof(id) - 1u, &required);
    if (read != GF_STATUS_OK) {
        return 0;
    }
    id[required] = '\0';
    return strcmp(id, expected) == 0;
}

/* Whether any held finding has code. */
static int has_finding(gf_kit* kit, uint64_t count, const char* code) {
    for (uint64_t index = 0u; index < count; ++index) {
        char text[32];
        uint64_t required = 0u;
        if ((*kit).themes.diagnostic_text(index, GF_THEME_DIAGNOSTIC_TEXT_CODE, text, sizeof(text) - 1u,
                                          &required) == GF_STATUS_OK) {
            text[required] = '\0';
            if (strcmp(text, code) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

static void check_load(gf_kit* kit) {
    char builtin[128];
    uint64_t builtin_size = 0u;
    gf_kit_status(kit, (*kit).themes.theme_id(builtin, sizeof(builtin) - 1u, &builtin_size), GF_STATUS_OK,
                  "read the built-in theme's id");
    builtin[builtin_size < sizeof(builtin) ? builtin_size : 0u] = '\0';

    kit_package good;
    make_package(&good, "kit-good", good_sheet, good_digest, NULL);
    gf_theme_report report = empty_report();
    gf_kit_status(kit, (*kit).themes.load(&good.package, &report), GF_STATUS_OK, "load a package");
    gf_kit_check(kit, report.outcome == GF_THEME_OUTCOME_APPLIED && report.diagnostic_count == 0u,
                 "a clean package is applied without findings");
    gf_kit_check(kit, theme_is(kit, "kit-good"), "the loaded package is the theme");

    /* A refusal: the sheet no longer matches its digest. Nothing changes. */
    kit_package tampered;
    make_package(&tampered, "kit-tampered", reading_sheet, good_digest, NULL);
    report = empty_report();
    gf_kit_status(kit, (*kit).themes.load(&tampered.package, &report), GF_STATUS_OK, "load a tampered package");
    gf_kit_check(kit, report.outcome == GF_THEME_OUTCOME_REFUSED && report.diagnostic_count > 0u,
                 "a tampered package is refused with findings");
    gf_kit_check(kit, theme_is(kit, "kit-good"), "a refused package leaves the theme in use");
    gf_theme_diagnostic finding;
    memset(&finding, 0, sizeof(finding));
    finding.struct_size = (uint32_t)sizeof(finding);
    gf_kit_status(kit, (*kit).themes.diagnostic_at(0u, &finding), GF_STATUS_OK, "read a finding");
    gf_kit_check(kit, finding.severity == GF_THEME_SEVERITY_ERROR, "a refusal's finding is an error");
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).themes.diagnostic_text(0u, GF_THEME_DIAGNOSTIC_TEXT_CODE, NULL, 0u, &required),
                  GF_STATUS_BUFFER_TOO_SMALL, "measure a finding's code");
    gf_kit_check(kit, required >= 4u, "a finding has a code");
    gf_kit_status(kit, (*kit).themes.diagnostic_at(report.diagnostic_count, &finding), GF_STATUS_INVALID_ARGUMENT,
                  "a finding past the end is refused");
    gf_kit_status(kit, (*kit).themes.diagnostic_text(0u, 99u, NULL, 0u, &required), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown finding text is refused");

    /* A warning drops one rule and the rest applies. */
    kit_package warned;
    make_package(&warned, "kit-warned", warning_sheet, warning_digest, NULL);
    report = empty_report();
    gf_kit_status(kit, (*kit).themes.load(&warned.package, &report), GF_STATUS_OK, "load a package with a warning");
    gf_kit_check(kit, report.outcome == GF_THEME_OUTCOME_APPLIED && report.diagnostic_count > 0u,
                 "a package with a warning is applied with its findings");
    memset(&finding, 0, sizeof(finding));
    finding.struct_size = (uint32_t)sizeof(finding);
    gf_kit_status(kit, (*kit).themes.diagnostic_at(0u, &finding), GF_STATUS_OK, "read the warning");
    gf_kit_check(kit, finding.severity == GF_THEME_SEVERITY_WARNING && finding.line == 1u,
                 "the warning is on the sheet's first line");

    /* Invalid packages are refused before anything is read. */
    gf_theme_package bad = good.package;
    bad.reserved = 1u;
    report = empty_report();
    gf_kit_status(kit, (*kit).themes.load(&bad, &report), GF_STATUS_INVALID_ARGUMENT, "reserved must be zero");
    bad = good.package;
    bad.files = NULL;
    gf_kit_status(kit, (*kit).themes.load(&bad, &report), GF_STATUS_INVALID_ARGUMENT, "files are needed for a count");
    bad = good.package;
    bad.file_count = GF_THEME_PACKAGE_MAXIMUM_FILES + 1u;
    gf_kit_status(kit, (*kit).themes.load(&bad, &report), GF_STATUS_LIMIT_EXCEEDED, "too many files are refused");
    gf_theme_report small = empty_report();
    small.struct_size = 8u;
    gf_kit_status(kit, (*kit).themes.load(&good.package, &small), GF_STATUS_INVALID_ARGUMENT,
                  "a short report record is refused");
    gf_kit_check(kit, theme_is(kit, "kit-warned"), "refused calls leave the theme in use");

    gf_kit_status(kit, (*kit).themes.reset(), GF_STATUS_OK, "reset to the built-in theme");
    gf_kit_check(kit, theme_is(kit, builtin), "reset restores the built-in theme");
    gf_kit_status(kit, (*kit).themes.diagnostic_at(0u, &finding), GF_STATUS_INVALID_ARGUMENT,
                  "reset clears the findings");
}

/* load_sheet: a plain sheet, its package made by the library. A sheet that
 * does not parse is refused with its finding on its own line; a
 * well-formed one is the theme under the name given. */
static void check_load_sheet(gf_kit* kit) {
    char builtin[128];
    uint64_t builtin_size = 0u;
    gf_kit_status(kit, (*kit).themes.theme_id(builtin, sizeof(builtin) - 1u, &builtin_size), GF_STATUS_OK,
                  "read the theme's id before a sheet");
    builtin[builtin_size < sizeof(builtin) ? builtin_size : 0u] = '\0';

    gf_theme_report report = empty_report();
    gf_kit_status(kit, (*kit).themes.load_sheet(gf_kit_text(good_sheet), gf_kit_text("kit-sheet"), &report),
                  GF_STATUS_OK, "load a sheet");
    gf_kit_check(kit, report.outcome == GF_THEME_OUTCOME_APPLIED && report.diagnostic_count == 0u,
                 "a clean sheet is applied without findings");
    gf_kit_check(kit, theme_is(kit, "kit-sheet"), "the sheet is the theme under its name");

    static const char broken_sheet[] = "button { opacity: 0.9 }\n/* never closed\n";
    report = empty_report();
    gf_kit_status(kit, (*kit).themes.load_sheet(gf_kit_text(broken_sheet), gf_kit_text("kit-broken"), &report),
                  GF_STATUS_OK, "load a broken sheet");
    gf_kit_check(kit, report.outcome == GF_THEME_OUTCOME_REFUSED && report.diagnostic_count > 0u,
                 "a broken sheet is refused with findings");
    gf_kit_check(kit, theme_is(kit, "kit-sheet"), "a refused sheet leaves the theme in use");
    gf_theme_diagnostic finding;
    memset(&finding, 0, sizeof(finding));
    finding.struct_size = (uint32_t)sizeof(finding);
    gf_kit_status(kit, (*kit).themes.diagnostic_at(0u, &finding), GF_STATUS_OK, "read the sheet's finding");
    gf_kit_check(kit, finding.severity == GF_THEME_SEVERITY_ERROR && finding.line == 2u,
                 "the sheet's error is placed on its line");

    gf_utf8_view no_data;
    no_data.data = NULL;
    no_data.size = 4u;
    report = empty_report();
    gf_kit_status(kit, (*kit).themes.load_sheet(no_data, gf_kit_text("kit-none"), &report),
                  GF_STATUS_INVALID_ARGUMENT, "a sheet with a size and no data is refused");
    gf_kit_status(kit, (*kit).themes.load_sheet(gf_kit_text(good_sheet), gf_kit_text("kit-sheet"), NULL),
                  GF_STATUS_INVALID_ARGUMENT, "load_sheet needs its report");
    gf_kit_status(kit, (*kit).themes.reset(), GF_STATUS_OK, "reset after the sheet");
    gf_kit_check(kit, theme_is(kit, builtin), "reset restores the theme before the sheet");
}

static void check_published(gf_kit* kit) {
    kit_package reading;
    make_package(&reading, "kit-reading", reading_sheet, reading_digest, "kit-level");
    gf_theme_report report = empty_report();
    gf_kit_status(kit, (*kit).themes.load(&reading.package, &report), GF_STATUS_OK, "load a package reading a value");
    gf_kit_check(kit, report.outcome == GF_THEME_OUTCOME_REFUSED && has_finding(kit, report.diagnostic_count, "E606"),
                 "a value nobody publishes is refused (E606)");
    gf_kit_status(kit, (*kit).themes.publish_value(gf_kit_text("kit-level"), 0.5), GF_STATUS_OK, "publish a value");
    report = empty_report();
    gf_kit_status(kit, (*kit).themes.load(&reading.package, &report), GF_STATUS_OK, "load it again");
    gf_kit_check(kit, report.outcome == GF_THEME_OUTCOME_APPLIED, "a published value is admitted");

    double value = -1.0;
    uint32_t present = 9u;
    gf_kit_status(kit, (*kit).themes.published_value(gf_kit_text("kit-level"), &value, &present), GF_STATUS_OK,
                  "read the value");
    gf_kit_check(kit, present == 1u && value == 0.5, "the value reads back");
    gf_kit_status(kit, (*kit).themes.withdraw_value(gf_kit_text("kit-level")), GF_STATUS_OK, "withdraw the value");
    value = -1.0;
    gf_kit_status(kit, (*kit).themes.published_value(gf_kit_text("kit-level"), &value, &present), GF_STATUS_OK,
                  "read the withdrawn value");
    gf_kit_check(kit, present == 0u && value == -1.0, "a withdrawn value is absent and the output untouched");
    gf_kit_status(kit, (*kit).themes.withdraw_value(gf_kit_text("kit-never")), GF_STATUS_OK,
                  "withdrawing a name never published changes nothing");
    report = empty_report();
    gf_kit_status(kit, (*kit).themes.load(&reading.package, &report), GF_STATUS_OK, "load after the withdrawal");
    gf_kit_check(kit, report.outcome == GF_THEME_OUTCOME_APPLIED, "a withdrawn name stays admitted");

    gf_kit_status(kit, (*kit).themes.publish_value(gf_kit_text("Kit"), 1.0), GF_STATUS_INVALID_ARGUMENT,
                  "an upper-case name is refused");
    gf_kit_status(kit, (*kit).themes.publish_value(gf_kit_text("9kit"), 1.0), GF_STATUS_INVALID_ARGUMENT,
                  "a name starting with a digit is refused");
    const double zero = 0.0;
    gf_kit_status(kit, (*kit).themes.publish_value(gf_kit_text("kit-level"), 1.0 / zero), GF_STATUS_INVALID_ARGUMENT,
                  "an infinite value is refused");
    gf_kit_status(kit, (*kit).themes.published_value(gf_kit_text("kit-level"), NULL, &present),
                  GF_STATUS_INVALID_ARGUMENT, "published_value needs its outputs");
    gf_kit_status(kit, (*kit).themes.reset(), GF_STATUS_OK, "reset after the published values");
}

static void check_library(gf_kit* kit) {
    kit_package good;
    make_package(&good, "kit-base", good_sheet, good_digest, NULL);
    gf_theme_report report = empty_report();
    gf_kit_status(kit, (*kit).themes.install(&good.package, &report), GF_STATUS_OK, "install a package");
    gf_kit_check(kit, report.outcome == GF_THEME_OUTCOME_APPLIED, "a clean package installs");
    kit_package tampered;
    make_package(&tampered, "kit-broken", reading_sheet, good_digest, NULL);
    report = empty_report();
    gf_kit_status(kit, (*kit).themes.install(&tampered.package, &report), GF_STATUS_OK, "install a tampered package");
    gf_kit_check(kit, report.outcome == GF_THEME_OUTCOME_REFUSED && report.diagnostic_count > 0u,
                 "a tampered package is not installed");
    uint32_t removed = 9u;
    gf_kit_status(kit, (*kit).themes.remove(gf_kit_text("kit-base"), &removed), GF_STATUS_OK, "remove a package");
    gf_kit_check(kit, removed == 1u, "the installed package was removed");
    gf_kit_status(kit, (*kit).themes.remove(gf_kit_text("kit-broken"), &removed), GF_STATUS_OK,
                  "remove a package never installed");
    gf_kit_check(kit, removed == 0u, "nothing was removed");
    gf_kit_status(kit, (*kit).themes.remove(gf_kit_text("kit-base"), NULL), GF_STATUS_INVALID_ARGUMENT,
                  "remove needs its output");
}

/* Tooltips are a property: set and read back on a button. */
static void check_tooltip(gf_kit* kit) {
    const gf_object button = gf_kit_create(kit, "Button", "kit.themes.tooltip");
    gf_kit_status(kit, gf_kit_set_text(kit, button, "tool_tip_text", "Applies the change"), GF_STATUS_OK,
                  "set a tooltip");
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    char text[64];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).properties.get(button, gf_kit_text("tool_tip_text"), &value, text, sizeof(text), &required),
                  GF_STATUS_OK, "read the tooltip");
    gf_kit_check(kit, value.kind == GF_VALUE_KIND_TEXT && gf_kit_view_equals(value.text, "Applies the change"),
                 "the tooltip reads back");
    gf_kit_status(kit, (*kit).objects.release(button), GF_STATUS_OK, "release the tooltip's button");
}

/* The theme the process was started with (--theme, GUI_FORMS_THEME_SHEET):
 * the kit is started with none, so the request reads NONE. The launched
 * cases are gf_abi_theme_request_app.c's. A policy is checked before the
 * resources are touched, and chosen only before their first window. */
static void check_theme_request(gf_kit* kit) {
    gf_theme_request_outcome outcome = GF_THEME_REQUEST_MALFORMED + 1u;
    gf_kit_status(kit, (*kit).themes.theme_request(&outcome), GF_STATUS_OK, "read the theme request");
    gf_kit_check(kit, outcome == GF_THEME_REQUEST_NONE, "a process started without a theme has no request");
    gf_kit_status(kit, (*kit).themes.theme_request(NULL), GF_STATUS_INVALID_ARGUMENT,
                  "theme_request needs its output");
    gf_kit_status(kit, (*kit).themes.set_user_theme_policy(GF_USER_THEME_POLICY_REFUSED + 1u),
                  GF_STATUS_INVALID_ARGUMENT, "an unknown policy is refused");
    const gf_status chosen = (*kit).themes.set_user_theme_policy(GF_USER_THEME_POLICY_LAYERED);
    gf_kit_check(kit, chosen == GF_STATUS_OK || chosen == GF_STATUS_INVALID_STATE,
                 "the layered policy is chosen, or refused once a window shares the resources");
}

void gf_kit_themes(gf_kit* kit) {
    check_load(kit);
    check_load_sheet(kit);
    check_published(kit);
    check_library(kit);
    check_tooltip(kit);
    check_theme_request(kit);
}
