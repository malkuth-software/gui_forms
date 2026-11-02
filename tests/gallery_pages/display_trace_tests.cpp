#include "support.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <string>
#include <string_view>

namespace gallery_pages_tests {
namespace {

// Every Gallery page's display commands, recorded as text and hashed (T2
// plan, phase E2: with no theme rules, painting through the theme engine
// records exactly the commands the built-in recipe recorded before it).
//
// The trace writes each command with its arguments. Lengths are written in
// 1/64 logical px, rounded, so that a last-bit difference between hosts in
// layout arithmetic does not change the trace; colours, images, text and
// fonts are written exactly. The expected table was recorded on the
// commit before the engine entry point (main at 13d1c7e0), re-recorded only
// under the rule above the table, and is checked on every CI platform.
// GUI_FORMS_GALLERY_TRACE_PRINT=1 prints the observed
// table instead of checking it, and GUI_FORMS_GALLERY_TRACE_DIR=<directory>
// writes each page's trace to <directory>/<page>.trace.
//
// The month calendars show "today", which the system clock supplies; the
// test pins it so that the trace does not depend on the date.
//
// live-surfaces reports its surface's pixel format, BGRA on every platform
// since SK2 (it had a second digest, for RGBA, before), so every page has one
// digest. One page shows a value that its own content does not fix:
// typography-and-inspection's inspector shows the inspected control's
// RuntimeId, a process-wide counter of every control made so far. The test
// therefore runs alone in a process of its own, its own ctest entry
// (gui_forms_gallery_display_trace_tests, running
// gui_forms_gallery_pages_tests --display-trace), so the counter has the
// same value on every run and host whatever other tests do.

class TracePainter final : public Painter {
public:
    void save() override { begin("save"); end(); }
    void restore() override { begin("restore"); end(); }
    void translate(Point offset) override {
        begin("translate");
        point(offset);
        end();
    }
    void clip_rect(Rect rect) override {
        begin("clip_rect");
        rectangle(rect);
        end();
    }
    void clip_rounded_rect(Rect rect, double radius) override {
        begin("clip_rounded_rect");
        rectangle(rect);
        length(radius);
        end();
    }
    void fill_rect(Rect rect, Color color) override {
        begin("fill_rect");
        rectangle(rect);
        colour(color);
        end();
    }
    void fill_rounded_rect(Rect rect, double radius, Color color) override {
        begin("fill_rounded_rect");
        rectangle(rect);
        length(radius);
        colour(color);
        end();
    }
    void stroke_rect(Rect rect, Color color, double width) override {
        begin("stroke_rect");
        rectangle(rect);
        colour(color);
        length(width);
        end();
    }
    void stroke_rounded_rect(Rect rect, double radius, Color color, double width) override {
        begin("stroke_rounded_rect");
        rectangle(rect);
        length(radius);
        colour(color);
        length(width);
        end();
    }
    void fill_linear_gradient(Rect rect, Point start, Point finish, std::span<const GradientStop> stops) override {
        begin("fill_linear_gradient");
        rectangle(rect);
        point(start);
        point(finish);
        gradient(stops);
        end();
    }
    void fill_linear_gradient_spread(Rect rect, Point start, Point finish, std::span<const GradientStop> stops,
                                     GradientSpreadMode spread) override {
        begin("fill_linear_gradient_spread");
        rectangle(rect);
        point(start);
        point(finish);
        gradient(stops);
        number(static_cast<long long>(spread));
        end();
    }
    void fill_radial_gradient(Rect rect, Point center, Size radii, std::span<const GradientStop> stops) override {
        begin("fill_radial_gradient");
        rectangle(rect);
        point(center);
        length(radii.width);
        length(radii.height);
        gradient(stops);
        end();
    }
    void draw_box_shadow(Rect rect, double corner_radius, Point offset, double blur_radius, double spread,
                         Color color) override {
        begin("draw_box_shadow");
        shadow(rect, corner_radius, offset, blur_radius, spread, color);
        end();
    }
    void draw_retained_box_shadow(Rect rect, double corner_radius, Point offset, double blur_radius, double spread,
                                  Color color, const std::shared_ptr<RetainedDrawCache>&) override {
        begin("draw_box_shadow");
        shadow(rect, corner_radius, offset, blur_radius, spread, color);
        end();
    }
    void draw_inset_box_shadow(Rect rect, double corner_radius, Point offset, double blur_radius, double spread,
                               Color color) override {
        begin("draw_inset_box_shadow");
        shadow(rect, corner_radius, offset, blur_radius, spread, color);
        end();
    }
    void draw_line(Point from, Point to, Color color, double width) override {
        begin("draw_line");
        point(from);
        point(to);
        colour(color);
        length(width);
        end();
    }
    void draw_text_utf8(Point origin, std::string_view text, FontSpec font, Color color) override {
        begin("draw_text");
        text_run(origin, text, font, color);
        end();
    }
    void draw_retained_text_utf8(Point origin, std::string_view text, FontSpec font, Color color,
                                 const std::shared_ptr<RetainedDrawCache>&) override {
        begin("draw_text");
        text_run(origin, text, font, color);
        end();
    }
    void draw_turned_text_utf8(Point origin, std::string_view text, FontSpec font, Color color,
                               TextOrientation orientation) override {
        begin("draw_turned_text");
        text_run(origin, text, font, color);
        number(static_cast<long long>(orientation));
        end();
    }
    void draw_image(ImageId image, Rect destination, double opacity) override {
        begin("draw_image");
        number(static_cast<long long>(image.value));
        rectangle(destination);
        length(opacity);
        end();
    }
    void draw_live_surface(std::shared_ptr<LiveSurface>, Rect destination, double opacity) override {
        begin("draw_live_surface");
        rectangle(destination);
        length(opacity);
        end();
    }
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double opacity) override {
        begin("draw_part_raster");
        if (raster) {
            const DeviceExtent extent = (*raster).extent();
            number(static_cast<long long>(extent.width));
            number(static_cast<long long>(extent.height));
            length((*raster).scale().value);
        }
        point(origin);
        length(opacity);
        end();
    }
    void draw_image_region(ImageId image, Rect source, Rect destination, double opacity) override {
        begin("draw_image_region");
        number(static_cast<long long>(image.value));
        rectangle(source);
        rectangle(destination);
        length(opacity);
        end();
    }
    void draw_image_region_sampled(ImageId image, Rect source, Rect destination, ImageSampling sampling,
                                   double opacity) override {
        begin("draw_image_region_sampled");
        number(static_cast<long long>(image.value));
        rectangle(source);
        rectangle(destination);
        number(static_cast<long long>(sampling));
        length(opacity);
        end();
    }
    void fill_image_pattern(ImageId image, Size source_pixel_size, Rect destination, Size logical_tile_size,
                            ImagePatternWrap wrap, double opacity) override {
        begin("fill_image_pattern");
        number(static_cast<long long>(image.value));
        length(source_pixel_size.width);
        length(source_pixel_size.height);
        rectangle(destination);
        length(logical_tile_size.width);
        length(logical_tile_size.height);
        number(static_cast<long long>(wrap));
        length(opacity);
        end();
    }

    // The trace so far, one command per line.
    std::string trace;

private:
    void begin(const char* name) { trace += name; }
    void end() { trace += '\n'; }
    void number(long long value) {
        trace += ' ';
        trace += std::to_string(value);
    }
    // A length in 1/64 logical px, rounded; non-finite values are written
    // by name.
    void length(double value) {
        if (!std::isfinite(value)) {
            trace += value != value ? " nan" : (value > 0.0 ? " inf" : " -inf");
            return;
        }
        const double scaled = value * 64.0;
        number(std::llround(scaled));
    }
    void point(Point value) {
        length(value.x);
        length(value.y);
    }
    void rectangle(Rect value) {
        length(value.x);
        length(value.y);
        length(value.width);
        length(value.height);
    }
    void colour(Color value) {
        const unsigned int packed = (static_cast<unsigned int>(value.red) << 24U) |
                                    (static_cast<unsigned int>(value.green) << 16U) |
                                    (static_cast<unsigned int>(value.blue) << 8U) | value.alpha;
        std::array<char, 12> text{};
        std::snprintf(text.data(), text.size(), " #%08x", packed);
        trace += text.data();
    }
    void gradient(std::span<const GradientStop> stops) {
        number(static_cast<long long>(stops.size()));
        for (const GradientStop& stop : stops) {
            length(stop.offset);
            colour(stop.color);
        }
    }
    void shadow(Rect rect, double corner_radius, Point offset, double blur_radius, double spread, Color color) {
        rectangle(rect);
        length(corner_radius);
        point(offset);
        length(blur_radius);
        length(spread);
        colour(color);
    }
    void text_run(Point origin, std::string_view text, FontSpec font, Color color) {
        point(origin);
        trace += " \"";
        trace += text;
        trace += '"';
        number(static_cast<long long>(font.role));
        length(font.size);
        number(static_cast<long long>(font.weight));
        number(font.italic ? 1 : 0);
        length(font.letter_spacing);
        colour(color);
    }
};

std::uint64_t trace_hash(std::string_view trace) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char character : trace) {
        hash = hash ^ static_cast<unsigned char>(character);
        hash = hash * 1099511628211ULL;
    }
    return hash;
}

// Pins every month calendar's "today" in the subtree, so that no trace
// depends on the date the test runs.
void pin_today(const Control::Ptr& control) {
    if (const std::shared_ptr<MonthCalendar> calendar = std::dynamic_pointer_cast<MonthCalendar>(control)) {
        (*calendar).set_today_date(DateTimeValue{2026, 10, 19});
    }
    for (const Control::Ptr& child : (*control).children()) pin_today(child);
}

struct PageDigest final {
    std::string_view page;
    std::uint64_t hash;
};

// Recorded on main at 13d1c7e0, before the theme engine's entry point, and
// re-recorded when stage L, phase L3 added the css-layout-panels page.
//
// Re-recording is allowed only when the page-by-page trace diffs
// (GUI_FORMS_GALLERY_TRACE_DIR, before and after) show intended changes
// alone, and the pull request's body must list them. L3's diffs: the new
// page; on every page the navigation sidebar's new entry, its taller paper
// and its scroll; on states-and-diagnostics the live control count and the
// paint chunks reused; on typography-and-inspection the inspected
// specimen's RuntimeId, its generation and the control count. Nothing else
// changed. Phase L4's grid specimens changed css-layout-panels, and the
// same counts on states-and-diagnostics and typography-and-inspection;
// phase L5's block specimens (and the page header's BlockPanel entry)
// did the same. Phase L6's `layout_direction` property added a row to
// values-and-spinners' property grid (its editor two controls more), and
// so the same counts on states-and-diagnostics and
// typography-and-inspection.
// The owner's container rename (FlexPanel, GridPanel, BlockPanel,
// FlowLayoutPanelCompat, TableLayoutPanelCompat) changed every page's
// navigation sidebar (the layout pages' order and the compatibility page's
// new title), the layout pages' group captions and the compatibility
// page's header; nothing else.
//
// Main's ThreadExceptionDialog, OpenFileDialog and SaveFileDialog lines on
// dialogs-and-host-services, and its TabControl images on tabs-and-pages,
// were recorded on main as well; with L3 rebased onto them, those two
// pages, states-and-diagnostics (1247 controls) and
// typography-and-inspection were recorded again, and every other page kept
// L3's digest.
//
// Main's ListView insertion-mark toggle on checked-collections adds one
// control: merged with main, against L8a's traces only
// states-and-diagnostics (1285 controls) and typography-and-inspection
// (#1318) differ, in those numbers alone, and were recorded again.
// Main's FolderBrowserDialog and ColorDialog rows on
// dialogs-and-host-services add two more: merged again, that page (the
// two rows, and the later rows moved down), states-and-diagnostics (1287
// controls) and typography-and-inspection (#1320 and two layout
// generations) differ and were recorded again; no other page moved.
// Main's Splitter specimen on dock-and-anchor adds eight more: merged
// again, that page (the Splitter group's caption and the page header's
// Splitter entry), states-and-diagnostics (1295 controls) and
// typography-and-inspection (#1328 and one layout generation) differ and
// were recorded again; no other page moved.
// W's RichTextBox specimen on text-and-input adds five more: that page
// (the new group below the TokenBox group), states-and-diagnostics (1300
// controls) and typography-and-inspection (#1333) differ and were recorded
// again; no other page moved.
// The docking workspace page (DK D8) adds a navigation row: on every page
// the sidebar gains the row, its list grows and its scroll thumb and the
// rows below move; containers-and-focus also loses its header's DockSite
// and DockWorkspace entries (now the new page's), and its content moves up.
// Diffed against the previous traces, nothing else changed; every page was
// recorded again.
//
// Each later phase, rebased onto those main pages, recorded again the
// pages whose digests the combination moved: the counts on
// states-and-diagnostics and the RuntimeId on typography-and-inspection,
// and, where the phase had recorded every page, the two main pages too.
//
// The opaque cover hint (hot paths, owner 2026-10-28) leaves out the
// window's backplane where the root panel's current chunk covers it: on
// every page but basics, whose first paint rebuilds the root's chunk, the
// trace lost those five commands (save, clip_rect, fill_rect,
// stroke_rounded_rect, restore) and nothing else, and the paint workload's
// pixels did not change. The Windows live-surfaces digest is that trace
// with its pixel format word changed, which reproduces the previous one.
//
// The accent button's ordinary plate in windows-professional (T4) starts
// its gradient darker, so that white text meets 4.5:1 on every stop: on
// the eleven pages that show an accent button, nineteen gradient fills
// changed their first colour and nothing else; those pages were recorded
// again.
//
// Buttons with a style override paint through the theme engine (T4 phase
// 4, hot states option 1): the override supplies the face's authored
// colour, and the theme supplies the shape, edge, states and cues. On every
// page the navigation buttons, and the page's own overridden buttons,
// changed from the classic relief (a flat fill, its light and dark lines
// and an inner highlight, and the command style's left accent bar) to the
// theme's rounded face in the override's face colour with the theme's
// edge: 1690 fill_rect, 849 stroke_rect and 325 draw_line commands became
// 848 rounded faces (save, clip, fill, restore) and 849 rounded edges. The
// text of six disabled overridden buttons on text-and-input and
// commands-and-menus took the theme's disabled ink (#848f99 for #858f98),
// as authored colours leave disabled text to the theme (T4-1). Nothing
// else changed; every page was recorded again.
//
// Check boxes and radio buttons paint through the theme engine (T4 phase
// 4, hot states option 1): the well, the mark and the focus cue are parts,
// and a style override supplies the well's ground and the ink of the mark
// and label. Themed choices drew the same commands as before; the
// overridden ones on basics, choices-and-sections and value-editors changed
// from the classic boxes and stepped discs (fill_rect rows and relief
// lines) to the theme's rounded wells in the override's paper with the
// theme's edge and marks. Their text did not change; those three pages
// were recorded again.
//
// ComboBox paints through the theme engine and declares hot (T4 phase 4):
// the well, drop-down button, divider, arrow and ring are parts. Themed
// combo boxes drew the same pixels, the arrow's four rows now listed tip
// first (collections-and-popups, values-and-spinners, value-editors); the
// overridden one on value-editors changed from the panel's flat face and
// relief lines to the theme's rounded well and button in the override's
// background. Those three pages were recorded again.
//
// typography-and-inspection's entry is the trace with the RuntimeId the
// specimen has when this test runs first (#1328 since L8a on main); the digest
// changes whenever a page made before it makes a control more or less.
constexpr std::array<PageDigest, 31> expected_digests{{
    {"basics", 0x90bb349431563af2ULL},
    {"values", 0xbb4905f0d13fa1fdULL},
    {"retained-rows", 0xeaac0b7007c0dc4eULL},
    {"instrument", 0x9175d9af67d4c1cfULL},
    {"control-spectrum", 0x707a9a903373c445ULL},
    {"ranges-and-progress", 0x64b83d8bf224433dULL},
    {"containers-and-focus", 0x6c48cee0339300d1ULL},
    {"animation-laboratory", 0x542b8d0c21121e6dULL},
    {"states-and-diagnostics", 0xa8f61fde123824f9ULL},
    {"text-and-input", 0x7e98b7301c190d6fULL},
    {"collections-and-popups", 0xc5ea7a18af64e3efULL},
    {"values-and-spinners", 0xa2e2122dcec535baULL},
    {"images-and-drawing", 0x638c11f89b3be0c6ULL},
    {"tabs-and-pages", 0x70e16b01b3d22cdfULL},
    {"checked-collections", 0x4c2aeda56938097aULL},
    {"timing-and-tooltips", 0xdec1abb7511851f6ULL},
    {"dates-and-calendar", 0x6d237c1f3f462e4cULL},
    {"dialogs-and-host-services", 0x55c611ffa8b50d03ULL},
    {"css-layout-panels", 0x1481013d6683373bULL},
    {"retained-layout-panels", 0x8affcbbf0ac3f3a5ULL},
    {"dock-and-anchor", 0x1dfb92ebe158d490ULL},
    {"docking-workspace", 0x61e086ed3a9f1c6bULL},
    {"commands-and-menus", 0x90d7dd6d7df9085dULL},
    {"tree-and-object-views", 0xf438014e8b43f0c1ULL},
    {"choices-and-sections", 0xbe17ba0824754a9fULL},
    {"value-editors", 0xa9cc9e42a5872cc3ULL},
    {"responsive-layouts", 0x0165a2122f612232ULL},
    {"anchored-popups", 0x2fa75c97d98572dbULL},
    {"live-surfaces", 0xa03a5dddc0929445ULL},
    {"typography-and-inspection", 0x0e5cca0acf8cca3fULL},
    {"semantic-feedback", 0x68026f28dcaf1914ULL},
}};

std::uint64_t expected_digest(std::string_view page) {
    for (const PageDigest& digest : expected_digests) {
        if (digest.page == page) return digest.hash;
    }
    return 0U;
}

} // namespace

void test_display_commands_unchanged() {
    const char* const print_value = std::getenv("GUI_FORMS_GALLERY_TRACE_PRINT");
    const bool printing = print_value != nullptr && std::strcmp(print_value, "1") == 0;
    const char* const directory = std::getenv("GUI_FORMS_GALLERY_TRACE_DIR");
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    window.perform_layout();
    std::string mismatches;
    for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
        select_page(window, page.id);
        pin_today(window.root());
        window.perform_layout();
        TracePainter painter;
        window.paint(painter, {0.0, 0.0, 1280.0, 820.0});
        const std::uint64_t hash = trace_hash(painter.trace);
        if (directory != nullptr) {
            const std::string path = std::string(directory) + "/" + std::string(page.id) + ".trace";
            std::ofstream file(path, std::ios::binary);
            file << painter.trace;
        }
        if (printing) {
            std::cout << "    {\"" << page.id << "\", 0x" << std::hex << std::setw(16) << std::setfill('0') << hash
                      << std::dec << "ULL},\n";
            continue;
        }
        if (hash != expected_digest(page.id)) {
            mismatches += std::string(page.id) + " ";
        }
    }
    if (!mismatches.empty()) {
        std::cerr << "display commands differ on pages: " << mismatches << '\n';
    }
    require(mismatches.empty(), "every Gallery page records the commands it recorded before the theme engine");
}

} // namespace gallery_pages_tests
