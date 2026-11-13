#include "support.hpp"

#include "gallery_theme_package.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/controls/date_time_value.hpp"
#include "gui_forms/controls/month_calendar.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <string>
#include <string_view>
#include <vector>

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
// The RightToLeft spelling (owner ruling of 2026-11-11) replaces that
// row with `right_to_left` (no, yes, inherit) in the Appearance category,
// hiding `layout_direction`: values-and-spinners' grid moves the row, and
// the same counts on states-and-diagnostics and typography-and-inspection
// may move with its editor.
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
// first (collections-and-overlays, values-and-spinners, value-editors); the
// overridden one on value-editors changed from the panel's flat face and
// relief lines to the theme's rounded well and button in the override's
// background. Those three pages were recorded again.
//
// W's MessageBox gaps (Abort/Retry/Ignore, Cancel/Try Again/Continue and
// the Help button) give dialogs-and-host-services six message buttons
// where it had four: they are narrower, labelled by their buttons alone
// ("OK/Cancel" for "Warning · OK/Cancel"), and the two new ones follow;
// states-and-diagnostics (1366 controls) and typography-and-inspection
// (#1405, generation 1082, 1382 controls) move by those two controls.
// Diffed against main's traces nothing else changed; those three pages
// were recorded again.
//
// The progress bar's animated effects draw through effect programs (C ABI
// stack, effect programs M2): ranges-and-progress was recorded again.
// Checkpoint 14 recorded the folded tree once; against main only these
// four pages differ.
//
//
// Range controls paint through the theme engine (T4 phase 4, range
// batch): ScrollBar's trough, track, arrow buttons, arrows and thumb,
// TrackBar's track, fill, ticks and thumb, and ProgressBar's track and
// fill are parts. The classic sunken troughs and bevelled thumbs (fill_rect
// and draw_line relief) became the theme's rounded track and thumb
// materials, the scroll arrows filled arrow marks for chevrons, the ticks
// rules in the theme's colour, and the progress fill's flat 3-unit top band
// went. Text did not change. The pages showing range controls were
// recorded again: live-surfaces, ranges-and-progress, responsive-layouts,
// retained-layout-panels, timing-and-tooltips, typography-and-inspection,
// values and values-and-spinners.
//
// Disabled labels take the theme's own disabled text (owner ruling,
// 2026-11-02, superseding the part path's muted ink): the recipe resolved
// in the disabled state gives the text colour. The 18 disabled labels on
// basics, commands-and-menus, control-spectrum, docking-workspace,
// responsive-layouts, states-and-diagnostics, text-and-input,
// timing-and-tooltips, value-editors and values-and-spinners returned to
// #848f99 from #76808a; those pages were recorded again.
//
// Lists, trees and the property list paint through the theme engine (T4
// phase 4, lists batch): ListBox's well, row plates, text and focus ring,
// CheckedListBox's check wells and marks, TreeView's plates, expanders,
// images, text and focus ring, and PropertyList's face, group bands, rules,
// expanders, titles, names, values and thumb are parts. Row text keeps its
// baseline and is clipped to its line box (save, clip, restore); check
// boxes became the theme's choice well and check mark, expanders chevron
// marks for the old glyphs and strokes, the property list's group bands and
// thumb the theme's panel face, and its property names the theme's label
// text in place of the muted ink. Recorded again: checked-collections,
// collections-and-overlays, responsive-layouts, tree-and-object-views,
// value-editors and values-and-spinners.
//
// ObjectView, CorrespondenceView and the plain BreadcrumbTrail paint
// through the theme engine (lists batch, second part): plates, text,
// rules, header band, sort arrows and focus rings are parts, and the item
// glyphs are a framing well with content fills. Text keeps its place and
// takes the theme's text; tree-and-object-views was recorded again.
//
// The text tone (owner, 2026-11-02): ObjectView's and CorrespondenceView's
// secondary text is a secondary-tone text part in the theme's muted ink;
// search matches are selection parts behind the text; the correspondence
// rail and state word take accent indices (pinned 0, stale 1) and the
// unavailable state (offline). tree-and-object-views was recorded again.
//
// ContextMenu and MenuStrip paint through the theme engine: bands, plates,
// labels, shortcuts (secondary tone), marks and separators are parts. The
// menu bar's labels are clipped to their part (save, clip, restore) at the
// same origins; commands-and-menus was recorded again.
//
// TextBox and RichTextBox paint through the theme engine: wells, text
// runs, selection, cue text (secondary tone), caret and focus are parts.
// Runs are clipped to their parts at the same origins and colours; recorded
// again: dialogs-and-host-services, docking-workspace, semantic-feedback,
// text-and-input, timing-and-tooltips, value-editors and values-and-spinners.
//
// Panel, GroupBox, Card, SplitterPanel, TableLayoutPanelCompat, the
// scrollable control's scroll bars and SplitterGrip paint through the theme
// engine. A panel with an authored background is the theme's face with that
// ground and keeps the material's border (T4-1), as a theme-coloured panel
// already did; GroupBox's border runs as keylines broken behind its caption
// in place of a plate under it; the scroll bars are the ScrollBar's track,
// buttons, arrow marks and thumb. Every page was recorded again.
//
// DateTimePicker and its calendar overlay paint through the theme engine: the
// drop-down button, divider and arrow mark (as the combo box's), the up-down
// pair, the check box and the value's text (clipped to its line box) are
// parts; dates-and-calendar was recorded again.
//
// Label and TextBlock lines are label parts placed by their baselines, in
// the theme's text or the authored foreground; each line is clipped to its
// line box (save, clip, restore) at the same origin and colour. Every page
// was recorded again.
//
// ErrorGlyph is the theme's round validation plate (the invalid recipe)
// with the error mark; ColorValueEditor's swatch is a framing well with
// content fills (the checkerboard and the value); FlagsValueEditor's
// drop-down is the combo box's parts. Recorded again: timing-and-tooltips
// and value-editors.
//
// The raised breadcrumb's segments are button faces in the engine's pointed
// shape (the current one selected, the edit terminal an accent button), with
// label and focus parts; hit testing uses the same shape. Recorded again:
// tree-and-object-views.
//
// FD6.3's Gallery project theme (demo/themes/gallery, selected by
// make_gallery) and the `classes` property: the page headings, leads and
// group faces moved from code and `fore-color` to classes the theme styles,
// with the same colours, and control-spectrum and semantic-feedback became
// form documents. Against cp16's traces only three pages differ:
// values-and-spinners' property grid gains the Classes row (its editor one
// control more, and the rows below move), states-and-diagnostics' live
// control count (1398) follows, and typography-and-inspection's inspector
// names the selected theme ("gallery · inherited" for
// "windows-professional"). No other page moved; those three were recorded
// again.
//
// The overlay rename (owner order, 2026-11-05) changed only wording: the
// navigation sidebar's "Collections and overlays", "Dialogs and overlays" and
// "Anchored overlays" entries on every page (their clip widths follow the
// longer words), and the renamed texts on anchored-overlays,
// collections-and-overlays, commands-and-menus and dates-and-calendar. Every
// page was recorded again.
//
// Folded together (cp18), the rename's traces hold except on FD6.3's three
// pages above and live-surfaces, which gains the SubSurfaceHost group (the
// live control count and the specimen's RuntimeId follow); those four were
// recorded again. M2's property capabilities moved no page.
//
// Control's justify_self and align_self (owner-approved) add two rows to
// values-and-spinners' property grid (the rows below move), and
// states-and-diagnostics' live control count follows; those two were
// recorded again.
// The dialogs page's native-popup toggle (gallery.host.task-dialog.native)
// is one control more in the window, which states-and-diagnostics' live
// control count follows. Recorded again: states-and-diagnostics.
// The diagnostics panel's invalidation overlay check box
// (gallery.diagnostics.invalidation) is one control more in the window:
// states-and-diagnostics' live control count follows, recorded again.
//
// responsive-layouts became a form document (FD6.3): its four track tiles
// name their faces by class, so the Gallery theme paints them as parts
// instead of the page's fills and strokes. Recorded again; a 1280 x 820
// capture at scale 1 is pixel-identical to the page before.
// FD6.3's nine further page documents (FD-4) move no page but one:
// values-and-spinners' property grid shows the inspected NumericUpDown's
// Bounds as the document declares them (194, 100, 260, 36) where the
// code-built page left the property at zero. Recorded again:
// values-and-spinners.
//
// Three Gallery pages (guided-tasks, the wizard and callout; help-book;
// instruments, the gauges and dials) lengthen the navigation list every page
// shows: its rows below them move and its scroll thumb shortens. Recorded
// again: every page, in both tables (the page bodies' text is unchanged).
//
// The windows-professional rework (2026-11-10) took the Gallery's inline
// styles and colours off its pages (colour lives in the theme: page labels
// take the package's text role classes, the navigation rows and menu type
// are classes) and gave menu drop-downs an icon gutter. Recorded again:
// every page.
// The cp28 assembly put the windows-professional rework beside the
// WheelScrolls binding descriptor: values-and-spinners' property grid gains
// the row (the rows below move), and states-and-diagnostics and
// images-and-drawing follow. Recorded again: those three, in both tables.
//
// cp29 (batch 1): tooltip text is black in every light theme (owner,
// 2026-11-11), which moves help-book, whose HelpBook text is the tool_tip
// family; and the dock caption grip paints by default (owner, 2026-11-11),
// which moves docking-workspace. Recorded again: those two, in both tables
// (tree-and-object-views and guided-tasks did not move).
//
// cp29 (block): help H1 gives Control the Help, HelpLine and Keywords
// properties, so values-and-spinners' property grid gains three rows (the
// rows below move) and states-and-diagnostics' live control count follows.
// Recorded again: values-and-spinners in both tables, states-and-diagnostics
// in the first (the second leaves it out).
//
// cp29 (block 2): the barber-pole rework (owner rulings 2026-11-11) draws
// the marching stripes as filled bands inside the fill's rim, anchored to
// the track start. Recorded again: ranges-and-progress, in both tables.
//
// cp29 2b fold A (VirtualRowsView on the Gallery): tree-and-object-views
// gains the VirtualRowsView group (its catalog caption and the page below
// it); states-and-diagnostics counts five controls more (1766 -> 1771);
// typography-and-inspection's inspected generation moves by one. Recorded
// again: those three pages in the first table, tree-and-object-views in the
// second.
//
// cp29 I3 (RightToLeft): values-and-spinners' property grid shows the
// RightToLeft row under Appearance where LayoutDirection stood under Layout
// (the same number of rows, so no control count moved). Recorded again:
// values-and-spinners, in both tables.
//
// cp29 I1 (F2, F9): each catalog card's caption names its control's header,
// now include/gui_forms/controls/<name>.hpp (caption text and clip width
// only); the seven lab and demo controls are not installed, so their cards
// go: EasingPreview (animation-laboratory, now a prose caption), ReviewCard
// (choices-and-sections), TypographySpecimen and VisualInspectorView
// (typography-and-inspection, one prose caption), CorrespondenceView
// (tree-and-object-views), InstrumentRack (value-editors), and those pages
// reflow; states-and-diagnostics counts five controls fewer. Recorded again:
// the 31 captioned pages in the first table, 29 in the second.
//
// cp29 (block 3): the basics page's lifecycle card paints its plate as its
// themed face part (dark-card-plate) where it filled a literal colour, and
// a part the built-in recipe draws keeps its authored material
// (part-rules-reach-paint). Recorded again: basics, in both tables; no page
// with fill or selection rules moved.
//
// cp31 (industrial surfaces, phase 5): the industrial-surfaces page
// lengthens the navigation list every page shows, so every page moves.
// Recorded again by the integrator (cp31 fold B, 2026-11-12): every page,
// in both tables. Against the fold A traces, each page's changes are the
// list's shorter scroll thumb, its new "Industrial surfaces" row and the
// rows after it moving down; pages further down the list, which scroll it
// further, move more.
//
// cp31 fold D return (NodeCanvas on the Gallery): the node-graphs page
// lengthens the navigation list again, so every page moves. Recorded again
// by the integrator (cp31 folds C-E, 2026-11-12) on the stack head: every
// page, in both tables; node-graphs for the first time.
//
// typography-and-inspection's entry is the trace with the RuntimeId the
// specimen has when this test runs first (#1328 since L8a on main); the digest
// changes whenever a page made before it makes a control more or less.
constexpr std::array<PageDigest, 41> expected_digests{{
    {"basics", 0xf4bace781043d62cULL},
    {"values", 0x7387a48539c61b96ULL},
    {"retained-rows", 0xd2fec65dc7808bdfULL},
    {"instrument", 0x4bc5fb500ea66b95ULL},
    {"control-spectrum", 0xb85d504908b33489ULL},
    {"ranges-and-progress", 0x4cd918a822323ba4ULL},
    {"containers-and-focus", 0x835b279f4eea0c87ULL},
    {"animation-laboratory", 0x0aa95b8e812e46c8ULL},
    {"states-and-diagnostics", 0x9e9852eed84178a2ULL},
    {"text-and-input", 0xa0cffe89f1cbfeeeULL},
    {"collections-and-overlays", 0x7e4340a9b96dd8efULL},
    {"values-and-spinners", 0xbf208de6fa2e7f09ULL},
    {"images-and-drawing", 0x5fab7ec36abae898ULL},
    {"tabs-and-pages", 0xfaad145f1cfa3617ULL},
    {"checked-collections", 0xad8e32146e49106cULL},
    {"timing-and-tooltips", 0xd2c29e62f417485cULL},
    {"dates-and-calendar", 0x65bd27373952d65bULL},
    {"dialogs-and-host-services", 0x5f7b3680de569e28ULL},
    {"css-layout-panels", 0x53b1cb225517f95bULL},
    {"retained-layout-panels", 0x8fb460d3707b5a9fULL},
    {"dock-and-anchor", 0x9f460e4250e228beULL},
    {"docking-workspace", 0xcceb99455ff13e11ULL},
    {"commands-and-menus", 0x7f82f068ef205eb7ULL},
    {"tree-and-object-views", 0x95b4bb72d252da9fULL},
    {"choices-and-sections", 0x6e3f44a56d80c537ULL},
    {"value-editors", 0x9c608cfb5d432dadULL},
    {"responsive-layouts", 0x26230a79a08de904ULL},
    {"anchored-overlays", 0xf0fcaeedbc1f7dadULL},
    {"live-surfaces", 0x92596475cd106d9cULL},
    {"typography-and-inspection", 0x160c350fa6a15156ULL},
    {"semantic-feedback", 0x558acb06ae154f28ULL},
    {"notices-and-badges", 0x8566226fb412ffcaULL},
    {"input-and-rules", 0x55828587a6c759eeULL},
    {"theme-settings", 0x31105962220425b9ULL},
    {"spinners-and-rings", 0x5914d1172a17b31bULL},
    {"instruments", 0x6e636ecd23003863ULL},
    {"guided-tasks", 0xaeda0e37c0f8adedULL},
    {"help-book", 0xe9e37e58b1606ab8ULL},
    {"industrial-surfaces", 0x77cd4185e6e25ed9ULL},
    {"node-graphs", 0x10ccd96ea70eecabULL},
    {"controls-inspection", 0x5f81a80defd94ca7ULL},
}};

// The same pages under the Gallery theme compiled onto windows-professional,
// as recorded on cp22 (c9b11f0a), when windows-professional was the default
// base and the table above was this one. Nothing the Gallery paints changed
// between cp22 and the default switch but the base, so a window switched to
// windows-professional must still record them. Recorded again for the
// windows-professional rework (2026-11-10; candidate DX's palette, light and
// dark, and the pages' inline styles taken off), and again for the cp28
// assembly: a box shadow reaches the 3.5 deviations it draws (1.75 blur
// radii, owner 2026-11-10) where it reached 3 radii, which moves every page's
// cards; values-and-spinners gains the WheelScrolls row; docking-workspace's
// dock site parts carry their names. typography-and-inspection is
// left out (zero): it shows a RuntimeId, which counts every control made
// before it in the process, and this pass runs after the one above.
constexpr std::array<PageDigest, 41> professional_digests{{
    {"basics", 0xbf920060ba74fc0cULL},
    {"values", 0x50e1d99cadf1ee8cULL},
    {"retained-rows", 0xe74d6168806f0c23ULL},
    {"instrument", 0x8bedc13a4e2f87f5ULL},
    {"control-spectrum", 0xb53e356dc0f6db6aULL},
    {"ranges-and-progress", 0x4f89975f1034df65ULL},
    {"containers-and-focus", 0x6ebc645545d2ac99ULL},
    {"animation-laboratory", 0xe1932b6d9198753eULL},
    {"states-and-diagnostics", 0x0000000000000000ULL},
    {"text-and-input", 0x37f2ee4609898519ULL},
    {"collections-and-overlays", 0x34063655249ba3beULL},
    {"values-and-spinners", 0x0c8a7b01dac98df2ULL},
    {"images-and-drawing", 0xcd017ae4187ac65eULL},
    {"tabs-and-pages", 0x1c12ee132f56c330ULL},
    {"checked-collections", 0xc07862cd9e2888abULL},
    {"timing-and-tooltips", 0x7be4f5ad4d8437f8ULL},
    {"dates-and-calendar", 0x67cb900d8709713bULL},
    {"dialogs-and-host-services", 0xf0e0be7fd9657d65ULL},
    {"css-layout-panels", 0x951f64c4d75f4639ULL},
    {"retained-layout-panels", 0xbb721f9dc7775af7ULL},
    {"dock-and-anchor", 0x3498f88e8afc29b0ULL},
    {"docking-workspace", 0x078b7846c68c8c06ULL},
    {"commands-and-menus", 0x74ab61e365f77569ULL},
    {"tree-and-object-views", 0x91e442d091deef65ULL},
    {"choices-and-sections", 0xa9fdc069384c55f2ULL},
    {"value-editors", 0x1e6cfea86c38dba5ULL},
    {"responsive-layouts", 0x46fe99fb71a09c87ULL},
    {"anchored-overlays", 0x3c6c136a56bb2b9cULL},
    {"live-surfaces", 0xe2b21700a567a168ULL},
    {"typography-and-inspection", 0x0000000000000000ULL},
    {"semantic-feedback", 0x0f519af05d4b88bfULL},
    {"notices-and-badges", 0x5fbf53fd6842978bULL},
    {"input-and-rules", 0xd268ec3e4ecb1121ULL},
    {"theme-settings", 0x4040a60a3c4c6866ULL},
    {"spinners-and-rings", 0x640b79bb75ae88fcULL},
    {"instruments", 0xb9a0b3d8c15cbb23ULL},
    {"guided-tasks", 0x6131be6ac066ba7eULL},
    {"help-book", 0xe0839fce0e9f5986ULL},
    {"industrial-surfaces", 0x963bde189cf893f7ULL},
    {"node-graphs", 0x758b009bfe3fe582ULL},
    {"controls-inspection", 0x00d37b337a4bb079ULL},
}};

// The Gallery theme compiled onto `base`, as the Theme settings page
// compiles it (ThemeSettingsPage::build_theme).
std::shared_ptr<const Theme> gallery_theme_on(Window& window, ThemeBase base) {
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    options.effects = &built_in_effect_registry();
    options.base = base;
    const ThemeLibrary library{};
    const ThemeCompileResult result =
        (*window.resources()).compile_theme_package(gallery::gallery_theme_package(), library, options);
    require(result.succeeded(), "the Gallery theme compiles onto either base");
    return result.theme();
}

// What `window` shows now, as a display trace.
std::string shown_trace(Window& window) {
    window.perform_layout();
    TracePainter painter;
    window.paint(painter, {0.0, 0.0, 1280.0, 820.0});
    return painter.trace;
}

// `trace` without the window's backplane, the block a full paint opens with
// when the backplane is drawn again: after a theme change, or on the
// window's first paint, and not otherwise (the backplane plane is kept).
// Its five lines follow the opening save: the clip, the fill, the edge, a
// restore and the next save.
std::string without_backplane(const std::string& trace) {
    std::vector<std::size_t> line_starts;
    std::size_t position = 0U;
    while (position < trace.size() && line_starts.size() < 7U) {
        line_starts.push_back(position);
        const std::size_t end = trace.find('\n', position);
        if (end == std::string::npos) return trace;
        position = end + 1U;
    }
    if (line_starts.size() < 7U) return trace;
    const std::string_view text(trace);
    const std::string_view clip = text.substr(line_starts[1], line_starts[2] - line_starts[1]);
    const std::string_view fill = text.substr(line_starts[2], line_starts[3] - line_starts[2]);
    const std::string_view restore = text.substr(line_starts[4], line_starts[5] - line_starts[4]);
    const std::string_view save = text.substr(line_starts[5], line_starts[6] - line_starts[5]);
    const bool backplane = clip.starts_with("clip_rect 0 0 ") && fill.starts_with("fill_rect 0 0 ") &&
                           restore == "restore\n" && save == "save\n";
    if (!backplane) return trace;
    std::string stripped = trace.substr(0U, line_starts[1]);
    stripped += trace.substr(line_starts[6]);
    return stripped;
}

// Pages whose trace changes from one paint to the next whatever the theme:
// states-and-diagnostics shows the window's paint and input counters, and
// typography-and-inspection a RuntimeId.
bool page_counts_itself(std::string_view page) {
    const bool counts = page == "states-and-diagnostics" || page == "typography-and-inspection";
    return counts;
}

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

// Switching the Gallery's theme gui-forms -> windows-professional ->
// gui-forms (as the Theme settings page does, for every window) on each
// page in turn: under windows-professional the page records what it
// recorded when that was the default base (professional_digests), and back
// on gui-forms what it recorded before the switch, so nothing either theme
// drew or measured is kept across a switch. A theme change draws the
// backplane again, so the comparisons leave it out, except on the first
// page, whose cp22 trace is a window's first paint and has it.
void test_theme_switch_round_trip() {
    // GUI_FORMS_GALLERY_TRACE_PRINT=1 prints the observed windows-professional
    // table instead of checking it.
    const char* const print_value = std::getenv("GUI_FORMS_GALLERY_TRACE_PRINT");
    const bool printing = print_value != nullptr && std::strcmp(print_value, "1") == 0;
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    window.perform_layout();
    const std::shared_ptr<ApplicationResources> resources = window.resources();
    const std::shared_ptr<const Theme> gui_forms = gallery_theme_on(window, ThemeBase::gui_forms);
    const std::shared_ptr<const Theme> professional = gallery_theme_on(window, ThemeBase::windows_professional);
    (*resources).set_theme(gui_forms);
    std::string professional_mismatches;
    std::string return_mismatches;
    bool first_page = true;
    for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
        select_page(window, page.id);
        pin_today(window.root());
        const std::string before = without_backplane(shown_trace(window));
        (*resources).set_theme(professional);
        const std::string switched_trace = shown_trace(window);
        const std::string switched = first_page ? switched_trace : without_backplane(switched_trace);
        (*resources).set_theme(gui_forms);
        const std::string back = without_backplane(shown_trace(window));
        first_page = false;
        if (printing) {
            const std::uint64_t shown = page_counts_itself(page.id) ? 0U : trace_hash(switched);
            std::cout << "    {\"" << page.id << "\", 0x" << std::hex << std::setw(16) << std::setfill('0') << shown
                      << std::dec << "ULL},\n";
            continue;
        }
        if (page_counts_itself(page.id)) continue;
        std::uint64_t expected = 0U;
        for (const PageDigest& digest : professional_digests) {
            if (digest.page == page.id) expected = digest.hash;
        }
        if (trace_hash(switched) != expected) professional_mismatches += std::string(page.id) + " ";
        if (back != before) return_mismatches += std::string(page.id) + " ";
    }
    if (!professional_mismatches.empty()) {
        std::cerr << "windows-professional differs from cp22 on pages: " << professional_mismatches << '\n';
    }
    if (!return_mismatches.empty()) {
        std::cerr << "gui-forms differs after a round trip on pages: " << return_mismatches << '\n';
    }
    require(professional_mismatches.empty(), "a window switched to windows-professional paints as it did on cp22");
    require(return_mismatches.empty(), "a window switched back to gui-forms paints as it did before");
}

} // namespace gallery_pages_tests
