/* Kit part: the application (GF_TABLE_APPLICATION, since 1.1). It runs
 * while the library holds no object, since an application is made before
 * the library's objects, and after the thread handover, since it binds the
 * UI thread for good. It closes the application again, so the parts after
 * it run with the process default. The fixture sheet's manifest carries
 * the SHA-256 of the sheet as written here. */

#include "gf_conformance_internal.h"

#include <stdio.h>
#include <string.h>

static const char application_sheet[] = "button::part(face) { background-color: #3366cc }\n";
static const char application_digest[] = "81e992b31c95ced1aefc8425f93f738c6205e05afa2e8522678d9e0bee3ee59d";

/* The most packages create is given below: one past the maximum. */
#define KIT_PACKAGES (GF_APPLICATION_MAXIMUM_THEME_PACKAGES + 1u)

/* A package of one sheet, built in caller storage. */
typedef struct kit_application_package {
    char manifest[640];
    gf_theme_file file;
    gf_theme_package package;
} kit_application_package;

static gf_bytes_view bytes_of(const char* text) {
    gf_bytes_view view;
    view.data = (const uint8_t*)text;
    view.size = (uint64_t)strlen(text);
    return view;
}

static void make_package(kit_application_package* made, const char* name) {
    memset(made, 0, sizeof(*made));
    snprintf((*made).manifest, sizeof((*made).manifest),
             "{\"format\": \"gui-forms-theme/1\", \"name\": \"%s\", \"version\": \"1.0.0\", \"engine\": \">=0.3\", "
             "\"sheet\": {\"path\": \"theme.theme.css\", \"sha256\": \"%s\"}}",
             name, application_digest);
    (*made).file.path = gf_kit_text("theme.theme.css");
    (*made).file.bytes = bytes_of(application_sheet);
    (*made).package.struct_size = (uint32_t)sizeof(gf_theme_package);
    (*made).package.manifest = bytes_of((*made).manifest);
    (*made).package.files = &(*made).file;
    (*made).package.file_count = 1u;
}

static gf_application_config config_of(const gf_theme_package* packages, uint64_t count) {
    gf_application_config config;
    memset(&config, 0, sizeof(config));
    config.struct_size = (uint32_t)sizeof(config);
    config.theme_base = GF_THEME_BASE_GUI_FORMS;
    config.packages = packages;
    config.package_count = count;
    config.presentation_source = GF_PRESENTATION_SOURCE_DEFAULTS;
    return config;
}

static gf_theme_report empty_report(void) {
    gf_theme_report report;
    memset(&report, 0, sizeof(report));
    report.struct_size = (uint32_t)sizeof(report);
    return report;
}

static gf_application_state state_of(gf_kit* kit) {
    gf_application_state state = GF_APPLICATION_STATE_CURRENT + 1u;
    gf_kit_status(kit, (*kit).application.state(&state), GF_STATUS_OK, "read the application state");
    return state;
}

/* The theme in use, written into id (NUL-terminated); empty on failure. */
static void read_theme(gf_kit* kit, char* id, uint64_t capacity) {
    uint64_t required = 0u;
    const gf_status read = (*kit).themes.theme_id(id, capacity - 1u, &required);
    id[read == GF_STATUS_OK ? required : 0u] = '\0';
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

/* Invalid configurations are refused before anything is made. */
static void check_invalid(gf_kit* kit, const gf_theme_package* package) {
    gf_theme_report report = empty_report();
    gf_kit_status(kit, (*kit).application.create(NULL, &report), GF_STATUS_INVALID_ARGUMENT,
                  "create needs a configuration");
    gf_application_config config = config_of(package, 1u);
    gf_kit_status(kit, (*kit).application.create(&config, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "create needs a report");
    config.reserved = 1u;
    gf_kit_status(kit, (*kit).application.create(&config, &report), GF_STATUS_INVALID_ARGUMENT,
                  "a reserved field must be zero");
    config = config_of(package, 1u);
    config.theme_base = 99u;
    gf_kit_status(kit, (*kit).application.create(&config, &report), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown theme base is refused");
    config = config_of(package, 1u);
    config.presentation_source = 99u;
    gf_kit_status(kit, (*kit).application.create(&config, &report), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown presentation source is refused");
    config = config_of(NULL, 1u);
    gf_kit_status(kit, (*kit).application.create(&config, &report), GF_STATUS_INVALID_ARGUMENT,
                  "packages are needed for a non-zero count");
    gf_kit_check(kit, state_of(kit) == GF_APPLICATION_STATE_NONE, "a refused configuration makes nothing");
}

void gf_kit_application(gf_kit* kit) {
    char builtin[128];
    read_theme(kit, builtin, sizeof(builtin));
    gf_kit_check(kit, state_of(kit) == GF_APPLICATION_STATE_NONE, "no application before create");
    static kit_application_package packages[KIT_PACKAGES];
    static gf_theme_package listed[KIT_PACKAGES];
    for (uint32_t index = 0u; index < KIT_PACKAGES; ++index) {
        char name[32];
        snprintf(name, sizeof(name), "kit-application-%u", index);
        make_package(&packages[index], name);
        listed[index] = packages[index].package;
    }
    check_invalid(kit, listed);

    /* More packages than the maximum: the compile refuses (E601). */
    gf_application_config config = config_of(listed, KIT_PACKAGES);
    gf_theme_report report = empty_report();
    gf_kit_status(kit, (*kit).application.create(&config, &report), GF_STATUS_OK,
                  "create with too many packages");
    gf_kit_check(kit, report.outcome == GF_THEME_OUTCOME_REFUSED && has_finding(kit, report.diagnostic_count, "E601"),
                 "too many packages are refused with E601");
    gf_kit_check(kit, state_of(kit) == GF_APPLICATION_STATE_NONE, "a refused theme makes no application");

    /* The application: the GUI.Forms base, then one package. */
    config = config_of(listed, 1u);
    report = empty_report();
    gf_kit_status(kit, (*kit).application.create(&config, &report), GF_STATUS_OK, "make the application");
    gf_kit_check(kit, report.outcome == GF_THEME_OUTCOME_APPLIED, "the application's theme compiles");
    gf_kit_check(kit, state_of(kit) == GF_APPLICATION_STATE_CURRENT, "the application is current");
    char theme[128];
    read_theme(kit, theme, sizeof(theme));
    gf_kit_check(kit, strcmp(theme, "kit-application-0") == 0, "the application's package is the theme");
    report = empty_report();
    gf_kit_status(kit, (*kit).application.create(&config, &report), GF_STATUS_INVALID_STATE,
                  "one application at a time");

    /* A window of the application keeps it from closing. */
    const gf_object root = gf_kit_create(kit, "panel", "kit.application.root");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "make an application window");
    uint64_t alive = 0u;
    gf_kit_status(kit, (*kit).application.close(&alive), GF_STATUS_INVALID_STATE,
                  "close is refused while a window lives");
    gf_kit_check(kit, alive == 1u && state_of(kit) == GF_APPLICATION_STATE_CURRENT,
                 "the refusal counts the window and changes nothing");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the application window");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the window's root");
    gf_kit_status(kit, (*kit).application.close(&alive), GF_STATUS_OK, "close the application");
    gf_kit_check(kit, alive == 0u && state_of(kit) == GF_APPLICATION_STATE_NONE, "the application is closed");
    read_theme(kit, theme, sizeof(theme));
    gf_kit_check(kit, strcmp(theme, builtin) == 0, "closing returns the built-in theme");
    gf_kit_status(kit, (*kit).application.close(&alive), GF_STATUS_OK, "closing again does nothing");

    /* A zero base is the default (GF_THEME_BASE_DEFAULT): with no packages
     * the application takes the built-in theme, not compiled again. */
    gf_application_config zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    zeroed.struct_size = (uint32_t)sizeof(zeroed);
    report = empty_report();
    gf_kit_status(kit, (*kit).application.create(&zeroed, &report), GF_STATUS_OK,
                  "make an application from a zero-initialised configuration");
    gf_kit_check(kit, report.outcome == GF_THEME_OUTCOME_APPLIED && state_of(kit) == GF_APPLICATION_STATE_CURRENT,
                 "the default base makes the application");
    read_theme(kit, theme, sizeof(theme));
    gf_kit_check(kit, strcmp(theme, builtin) == 0, "the default base alone is the built-in theme");
    gf_kit_status(kit, (*kit).application.close(&alive), GF_STATUS_OK, "close the default application");

    /* Objects made without an application keep create from making one. */
    const gf_object loose = gf_kit_create(kit, "button", "kit.application.loose");
    report = empty_report();
    gf_kit_status(kit, (*kit).application.create(&config, &report), GF_STATUS_INVALID_STATE,
                  "no application while objects made without one live");
    gf_kit_status(kit, (*kit).objects.release(loose), GF_STATUS_OK, "release the loose object");
}
