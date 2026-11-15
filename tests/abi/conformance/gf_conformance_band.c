/* Kit part: the window outer band and shaping (since 1.5): windows.
 * declare_band, undeclare_band, set_band_appearance, band_report,
 * set_corner_preference, content_rect and shaping on a window from
 * windows.create, before any run (no host shows it, so the shaping report
 * names none). The band's reservation is fixed while declared: an
 * appearance changes the report's colour, never the content rectangle. */

#include "gf_conformance_internal.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* The windows table size that carries the band and shaping slots. */
static int windows_have_band(const gf_kit* kit) {
    const int present = (*kit).windows.struct_size >= offsetof(gf_windows_table, shaping) + sizeof(void*) &&
                        (*kit).windows.declare_band != NULL && (*kit).windows.undeclare_band != NULL &&
                        (*kit).windows.set_band_appearance != NULL && (*kit).windows.band_report != NULL &&
                        (*kit).windows.set_corner_preference != NULL && (*kit).windows.content_rect != NULL &&
                        (*kit).windows.shaping != NULL;
    return present;
}

static gf_window_band_options band_options(double width) {
    gf_window_band_options options;
    memset(&options, 0, sizeof(options));
    options.struct_size = (uint32_t)sizeof(options);
    options.full_screen = GF_WINDOW_BAND_FULL_SCREEN_STAYS;
    options.width = width;
    return options;
}

static gf_window_band_appearance band_appearance(gf_window_band_state state) {
    gf_window_band_appearance appearance;
    memset(&appearance, 0, sizeof(appearance));
    appearance.struct_size = (uint32_t)sizeof(appearance);
    appearance.state = state;
    appearance.lit_role = GF_WINDOW_BAND_ROLE_THEME;
    appearance.frequency = 1.0;
    appearance.texture = GF_WINDOW_BAND_TEXTURE_NONE;
    appearance.texture_opacity = 1.0;
    return appearance;
}

static gf_window_band_report band_report(gf_kit* kit, gf_object window, const char* name) {
    gf_window_band_report report;
    memset(&report, 0, sizeof(report));
    report.struct_size = (uint32_t)sizeof(report);
    gf_kit_status(kit, (*kit).windows.band_report(window, &report), GF_STATUS_OK, name);
    return report;
}

static gf_logical_rect content(gf_kit* kit, gf_object window, const char* name) {
    gf_logical_rect rect;
    memset(&rect, 0, sizeof(rect));
    gf_kit_status(kit, (*kit).windows.content_rect(window, &rect), GF_STATUS_OK, name);
    return rect;
}

static int near(double left, double right) {
    const int close = fabs(left - right) < 1.0e-9;
    return close;
}

/* The declaration refusals: each changes nothing. */
static void band_refusals(gf_kit* kit, gf_object window) {
    gf_kit_status(kit, (*kit).windows.declare_band(window, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "declare_band refuses a null record");
    gf_window_band_options options = band_options(4.0);
    options.struct_size = 8u;
    gf_kit_status(kit, (*kit).windows.declare_band(window, &options), GF_STATUS_INVALID_ARGUMENT,
                  "declare_band refuses a short record");
    options = band_options(33.0);
    gf_kit_status(kit, (*kit).windows.declare_band(window, &options), GF_STATUS_INVALID_ARGUMENT,
                  "declare_band refuses a width past the maximum");
    options = band_options(NAN);
    gf_kit_status(kit, (*kit).windows.declare_band(window, &options), GF_STATUS_INVALID_ARGUMENT,
                  "declare_band refuses a width that is not finite");
    options = band_options(2.0);
    options.full_screen = 9u;
    gf_kit_status(kit, (*kit).windows.declare_band(window, &options), GF_STATUS_INVALID_ARGUMENT,
                  "declare_band refuses an unknown full-screen choice");
    options = band_options(2.0);
    options.reserved = 1u;
    gf_kit_status(kit, (*kit).windows.declare_band(window, &options), GF_STATUS_INVALID_ARGUMENT,
                  "declare_band refuses a nonzero reserved field");
    options = band_options(0.0);
    options.flags = GF_WINDOW_BAND_PER_EDGE;
    options.edges.right = -1.0;
    gf_kit_status(kit, (*kit).windows.declare_band(window, &options), GF_STATUS_INVALID_ARGUMENT,
                  "declare_band refuses a negative edge");
    const gf_window_band_report report = band_report(kit, window, "band_report after the refusals");
    gf_kit_check(kit, (report.status & GF_WINDOW_BAND_DECLARED) == 0u, "the refusals declared nothing");
}

/* The appearance: paint only, the content rectangle unchanged. */
static void band_appearances(gf_kit* kit, gf_object window, gf_logical_rect reserved) {
    gf_window_band_appearance appearance = band_appearance(GF_WINDOW_BAND_STATE_LIT);
    appearance.flags = GF_WINDOW_BAND_LIT_COLOUR;
    appearance.lit_colour = 0x0050C8FFu;
    gf_kit_status(kit, (*kit).windows.set_band_appearance(window, &appearance), GF_STATUS_OK, "light the band");
    gf_window_band_report report = band_report(kit, window, "band_report lit");
    gf_kit_check(kit, report.drawn_colour == 0x0050C8FFu, "lit, the band draws its lit colour");
    const gf_logical_rect lit_content = content(kit, window, "content_rect lit");
    gf_kit_check(kit, near(lit_content.x, reserved.x) && near(lit_content.y, reserved.y) &&
                          near(lit_content.width, reserved.width) && near(lit_content.height, reserved.height),
                 "lighting the band keeps its reservation");

    appearance = band_appearance(GF_WINDOW_BAND_STATE_CYCLING);
    appearance.flags = GF_WINDOW_BAND_LIT_COLOUR | GF_WINDOW_BAND_REST_COLOUR;
    appearance.lit_colour = 0x0050C8FFu;
    appearance.rest_colour = 0x202020FFu;
    appearance.frequency = 4.0;
    gf_kit_status(kit, (*kit).windows.set_band_appearance(window, &appearance), GF_STATUS_OK, "cycle the band");
    report = band_report(kit, window, "band_report cycling");
    gf_kit_check(kit, (report.status & GF_WINDOW_BAND_FLASHING) == 0u, "a cycle is not a flash");
    gf_kit_check(kit, (report.status & GF_WINDOW_BAND_CYCLING) == 0u || near(report.frequency, 1.0),
                 "a running cycle is limited to one cycle a second");

    appearance = band_appearance(GF_WINDOW_BAND_STATE_REST);
    appearance.texture = GF_WINDOW_BAND_TEXTURE_PROCEDURAL;
    appearance.texture_kind = GF_PROCEDURAL_PATTERN_PINSTRIPE;
    appearance.texture_primary = 0x808080FFu;
    appearance.texture_secondary = 0x606060FFu;
    appearance.texture_tile = 4.0;
    appearance.texture_strength = 1.0;
    gf_kit_status(kit, (*kit).windows.set_band_appearance(window, &appearance), GF_STATUS_OK, "texture the band");
    report = band_report(kit, window, "band_report textured");
    gf_kit_check(kit, (report.status & GF_WINDOW_BAND_TEXTURED) != 0u, "the band draws its texture");

    gf_window_band_appearance refused = appearance;
    refused.texture_kind = 99u;
    gf_kit_status(kit, (*kit).windows.set_band_appearance(window, &refused), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown pattern is refused");
    refused = appearance;
    refused.texture = GF_WINDOW_BAND_TEXTURE_FINISH;
    refused.texture_kind = 8u;
    gf_kit_status(kit, (*kit).windows.set_band_appearance(window, &refused), GF_STATUS_INVALID_ARGUMENT,
                  "the glint is refused");
    refused = appearance;
    refused.texture_tile = 1.0;
    gf_kit_status(kit, (*kit).windows.set_band_appearance(window, &refused), GF_STATUS_INVALID_ARGUMENT,
                  "a tile below the minimum is refused");
    refused = band_appearance(7u);
    gf_kit_status(kit, (*kit).windows.set_band_appearance(window, &refused), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown state is refused");
    refused = band_appearance(GF_WINDOW_BAND_STATE_LIT);
    refused.lit_role = 40u;
    gf_kit_status(kit, (*kit).windows.set_band_appearance(window, &refused), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown role is refused");
    refused = band_appearance(GF_WINDOW_BAND_STATE_FLASHING);
    refused.frequency = -1.0;
    gf_kit_status(kit, (*kit).windows.set_band_appearance(window, &refused), GF_STATUS_INVALID_ARGUMENT,
                  "a negative frequency is refused");
    gf_kit_status(kit, (*kit).windows.set_band_appearance(window, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "set_band_appearance refuses a null record");
    report = band_report(kit, window, "band_report after the refused appearances");
    gf_kit_check(kit, (report.status & GF_WINDOW_BAND_TEXTURED) != 0u, "the refusals kept the appearance");
}

/* The corner preference and the shaping report of a window no host shows. */
static void band_shaping(gf_kit* kit, gf_object window) {
    gf_kit_status(kit, (*kit).windows.set_corner_preference(window, GF_WINDOW_CORNER_PREFERENCE_SQUARE),
                  GF_STATUS_OK, "ask for square corners");
    gf_kit_status(kit, (*kit).windows.set_corner_preference(window, 7u), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown corner preference is refused");
    gf_window_shaping shaping;
    memset(&shaping, 0, sizeof(shaping));
    shaping.struct_size = (uint32_t)sizeof(shaping);
    gf_kit_status(kit, (*kit).windows.shaping(window, &shaping), GF_STATUS_OK, "shaping of a window");
    gf_kit_check(kit, shaping.preference == GF_WINDOW_CORNER_PREFERENCE_SQUARE, "the preference is kept");
    gf_kit_check(kit, (shaping.flags & GF_WINDOW_SHAPING_HOST_ATTACHED) == 0u && shaping.corner_shapes == 0u &&
                          shaping.corner_shape == 0u,
                 "a window no host shows reports no host and unknown corners");
    gf_kit_check(kit, (shaping.flags & GF_WINDOW_SHAPING_NATIVE_BORDER) == 0u, "no host draws a native border");
    shaping.struct_size = 8u;
    gf_kit_status(kit, (*kit).windows.shaping(window, &shaping), GF_STATUS_INVALID_ARGUMENT,
                  "shaping refuses a short record");
}

void gf_kit_band(gf_kit* kit) {
    gf_kit_check(kit, windows_have_band(kit), "the windows table has the band and shaping slots");
    if (!windows_have_band(kit)) {
        return;
    }
    const gf_object loose = gf_kit_create(kit, "Button", "kit.band.loose");
    gf_window_band_options options = band_options(2.0);
    gf_kit_status(kit, (*kit).windows.declare_band(loose, &options), GF_STATUS_INVALID_STATE,
                  "a control not in a window names no band");

    const gf_object root = gf_kit_create(kit, "Panel", "kit.band");
    const gf_object child = gf_kit_create(kit, "Button", "kit.band.child");
    gf_kit_status(kit, (*kit).objects.insert_child(root, child, 0u), GF_STATUS_OK, "put the child in the root");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "create a window for the band");
    const gf_logical_rect client = content(kit, window, "content_rect without a band");
    gf_kit_check(kit, near(client.x, 0.0) && near(client.y, 0.0), "without a band the content is the client area");
    gf_kit_status(kit, (*kit).windows.content_rect(window, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "content_rect needs an output");
    band_refusals(kit, window);

    options = band_options(4.0);
    gf_kit_status(kit, (*kit).windows.declare_band(window, &options), GF_STATUS_OK, "declare a band");
    gf_window_band_report report = band_report(kit, child, "band_report through a control in the window");
    gf_kit_check(kit, (report.status & GF_WINDOW_BAND_DECLARED) != 0u && (report.status & GF_WINDOW_BAND_RESERVED) != 0u,
                 "the band is declared and reserved");
    gf_kit_check(kit, near(report.edges.left, 4.0) && near(report.edges.bottom, 4.0), "every edge reserves 4");
    const gf_logical_rect reserved = content(kit, window, "content_rect with a band");
    gf_kit_check(kit, near(reserved.x, 4.0) && near(reserved.y, 4.0) && near(reserved.width, client.width - 8.0) &&
                          near(reserved.height, client.height - 8.0),
                 "the content rectangle is the client area less the band");
    band_appearances(kit, window, reserved);

    options = band_options(0.0);
    options.flags = GF_WINDOW_BAND_PER_EDGE;
    options.full_screen = GF_WINDOW_BAND_FULL_SCREEN_HIDES;
    options.edges.top = 6.0;
    options.edges.bottom = 2.0;
    gf_kit_status(kit, (*kit).windows.declare_band(window, &options), GF_STATUS_OK, "declare per-edge widths");
    report = band_report(kit, window, "band_report per edge");
    gf_kit_check(kit, near(report.edges.top, 6.0) && near(report.edges.bottom, 2.0) && near(report.edges.left, 0.0) &&
                          near(report.edges.right, 0.0),
                 "each edge reserves its own width");
    gf_kit_check(kit, report.full_screen == GF_WINDOW_BAND_FULL_SCREEN_HIDES, "the full-screen choice is kept");
    const gf_logical_rect edged = content(kit, root, "content_rect through the root");
    gf_kit_check(kit, near(edged.x, 0.0) && near(edged.y, 6.0) && near(edged.height, client.height - 8.0),
                 "the content rectangle follows the edges");

    band_shaping(kit, window);

    gf_kit_status(kit, (*kit).windows.undeclare_band(window), GF_STATUS_OK, "undeclare the band");
    report = band_report(kit, window, "band_report undeclared");
    gf_kit_check(kit, (report.status & (GF_WINDOW_BAND_DECLARED | GF_WINDOW_BAND_RESERVED)) == 0u,
                 "undeclared, nothing is reserved");
    const gf_logical_rect after = content(kit, window, "content_rect undeclared");
    gf_kit_check(kit, near(after.x, 0.0) && near(after.y, 0.0), "undeclaring returns the client area");
    gf_kit_status(kit, (*kit).windows.undeclare_band(window), GF_STATUS_OK, "undeclaring twice does nothing");

    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the band's window");
    gf_kit_status(kit, (*kit).objects.release(child), GF_STATUS_OK, "release the band's child");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the band's root");
    gf_kit_status(kit, (*kit).objects.release(loose), GF_STATUS_OK, "release the loose control");
}
