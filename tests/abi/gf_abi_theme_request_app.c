/*
 * A C program over the GUI.Forms C ABI with no theme code of its own,
 * started by CTest with --theme <name|built-in|path.css> or
 * GUI_FORMS_THEME_SHEET: it makes one window and checks what the request
 * did (themes.theme_request). Its expectations come from the environment,
 * so its command line holds only what a user would type:
 *   GF_EXPECT_THEME_REQUEST  none, built_in, applied, ignored_by_policy,
 *                            unknown_name, refused, unreadable or malformed;
 *   GF_TEST_POLICY           refused or replaces, chosen before the window
 *                            (themes.set_user_theme_policy; layered unset);
 *   GF_TEST_APPLICATION      when set, the window is an application's
 *                            (application.create first); otherwise it
 *                            shares the library's own resources.
 * Exit 0 when the request did what was expected.
 */
#include "gui_forms/c/gf_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void require(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "gf_abi_theme_request_app: %s\n", message);
        failures = failures + 1;
    }
}

static gf_utf8_view text_of(const char* text) {
    gf_utf8_view view;
    view.data = text;
    view.size = (uint64_t)strlen(text);
    return view;
}

static const char* environment(const char* name) {
    const char* value = getenv(name);
    return value == NULL ? "" : value;
}

/* The outcome GF_EXPECT_THEME_REQUEST names; NONE + 99 for an unknown name. */
static gf_theme_request_outcome expected_outcome(const char* name) {
    static const char* const names[] = {"none",         "pending",  "built_in",   "applied",  "ignored_by_policy",
                                        "unknown_name", "refused",  "unreadable", "malformed"};
    for (uint32_t index = 0u; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (strcmp(name, names[index]) == 0) {
            return (gf_theme_request_outcome)index;
        }
    }
    return GF_THEME_REQUEST_NONE + 99u;
}

int main(void) {
    gf_themes_table themes;
    gf_objects_table objects;
    gf_windows_table windows;
    gf_application_table application;
    memset(&themes, 0, sizeof(themes));
    memset(&objects, 0, sizeof(objects));
    memset(&windows, 0, sizeof(windows));
    memset(&application, 0, sizeof(application));
    require(gf_abi_get_table(GF_TABLE_THEMES, GF_ABI_MAJOR, GF_ABI_MINOR, &themes, sizeof(themes)) == GF_STATUS_OK,
            "the themes table negotiates");
    require(gf_abi_get_table(GF_TABLE_OBJECTS, GF_ABI_MAJOR, GF_ABI_MINOR, &objects, sizeof(objects)) == GF_STATUS_OK,
            "the objects table negotiates");
    require(gf_abi_get_table(GF_TABLE_WINDOWS, GF_ABI_MAJOR, GF_ABI_MINOR, &windows, sizeof(windows)) == GF_STATUS_OK,
            "the windows table negotiates");
    require(gf_abi_get_table(GF_TABLE_APPLICATION, GF_ABI_MAJOR, GF_ABI_MINOR, &application, sizeof(application)) ==
                GF_STATUS_OK,
            "the application table negotiates");
    if (failures != 0) {
        return 1;
    }
    const gf_theme_request_outcome expected = expected_outcome(environment("GF_EXPECT_THEME_REQUEST"));
    require(expected != GF_THEME_REQUEST_NONE + 99u, "GF_EXPECT_THEME_REQUEST names an outcome");
    const int with_application = environment("GF_TEST_APPLICATION")[0] != '\0';
    if (with_application) {
        gf_application_config config;
        memset(&config, 0, sizeof(config));
        config.struct_size = (uint32_t)sizeof(config);
        gf_theme_report report;
        memset(&report, 0, sizeof(report));
        report.struct_size = (uint32_t)sizeof(report);
        require(application.create(&config, &report) == GF_STATUS_OK && report.outcome == GF_THEME_OUTCOME_APPLIED,
                "the application is made");
    }
    const char* policy = environment("GF_TEST_POLICY");
    if (strcmp(policy, "refused") == 0) {
        require(themes.set_user_theme_policy(GF_USER_THEME_POLICY_REFUSED) == GF_STATUS_OK, "the policy is refused");
    }
    if (strcmp(policy, "replaces") == 0) {
        require(themes.set_user_theme_policy(GF_USER_THEME_POLICY_REPLACES_PROGRAM_SHEET) == GF_STATUS_OK,
                "the policy is replaces_program_sheet");
    }
    gf_theme_request_outcome outcome = GF_THEME_REQUEST_NONE + 99u;
    require(themes.theme_request(&outcome) == GF_STATUS_OK, "theme_request reads before the window");
    const gf_theme_request_outcome before = expected == GF_THEME_REQUEST_NONE ? GF_THEME_REQUEST_NONE
                                                                              : GF_THEME_REQUEST_PENDING;
    require(outcome == before, "the request is pending until the first window");

    gf_object root = 0u;
    gf_object window = 0u;
    require(objects.create(text_of("panel"), text_of("theme-request-app.root"), &root) == GF_STATUS_OK,
            "the window's root is made");
    require(windows.create(root, &window) == GF_STATUS_OK, "the window is made");
    outcome = GF_THEME_REQUEST_NONE + 99u;
    require(themes.theme_request(&outcome) == GF_STATUS_OK, "theme_request reads after the window");
    if (outcome != expected) {
        fprintf(stderr, "gf_abi_theme_request_app: the request was %u, expected %u\n", (unsigned)outcome,
                (unsigned)expected);
        failures = failures + 1;
    }
    require(themes.set_user_theme_policy(GF_USER_THEME_POLICY_LAYERED) == GF_STATUS_INVALID_STATE,
            "the policy is chosen before the first window");

    require(objects.release(window) == GF_STATUS_OK, "the window is released");
    require(objects.release(root) == GF_STATUS_OK, "the root is released");
    if (with_application) {
        uint64_t alive = 0u;
        require(application.close(&alive) == GF_STATUS_OK, "the application closes");
    }
    if (failures != 0) {
        return 1;
    }
    printf("gf_abi_theme_request_app: %s\n", environment("GF_EXPECT_THEME_REQUEST"));
    return 0;
}
