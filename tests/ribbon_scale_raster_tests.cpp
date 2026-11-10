// The Ribbon's look through the CPU raster at device scales 1 and 2 (Plan
// Paint's report against 3ac1a577e: at scale 1 the rail, the tabs, File
// and the plane painted no faces, only the tapers and the text). Its
// capture paints a scale-2 window into a scale-1 framebuffer, and a part
// raster baked at the window's scale was dropped by the raster. Each case
// paints a Ribbon whole, in the stock theme and in Plan Paint's rules, and
// samples its faces' pixels.

#include "../src/core/theme_language/package/theme_manifest.hpp"
#include "gui_forms/gui_forms.hpp"
#include "demo/gallery.hpp"
#include "skia_raster.hpp"
#include "../src/core/resources/fonts/bundled_font_set.hpp"
#if defined(_WIN32)
#include "../src/host/windows/fonts/sealed_pages_windows.hpp"
#else
#include "../src/host/posix/fonts/sealed_pages_posix.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

int failures = 0;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

// A sampled pixel, straight from the raster's premultiplied BGRA (every
// face sampled is opaque).
struct Sample final {
    int red{};
    int green{};
    int blue{};
};

Sample sample(const render::SkiaRaster& raster, double scale, Point logical) {
    const std::size_t x = static_cast<std::size_t>(logical.x * scale);
    const std::size_t y = static_cast<std::size_t>(logical.y * scale);
    const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(raster.pixels());
    const std::uint8_t* const pixel = bytes + y * raster.row_bytes() + x * 4U;
    const Sample made{pixel[2], pixel[1], pixel[0]};
    return made;
}

bool near(Sample seen, int red, int green, int blue, int tolerance) {
    const bool result = std::abs(seen.red - red) <= tolerance && std::abs(seen.green - green) <= tolerance &&
                        std::abs(seen.blue - blue) <= tolerance;
    return result;
}

void print(const char* what, double scale, Sample seen) {
    std::cout << what << " at " << scale << ": " << seen.red << "," << seen.green << "," << seen.blue << '\n';
}


// Plan Paint's package's Ribbon rules (themes/plan-paint at its 2.3 head),
// the faces its report found missing at scale 1.
constexpr std::string_view plan_paint_sheet =
    "ribbon::part(command-face) { border-radius: 3px; border: 1px solid #6384b2;\n"
    "  background-image: linear-gradient(to bottom, #d2e4fb 0%, #a3c1ec 100%);\n"
    "  box-shadow: 0 1px 0 rgba(41, 75, 119, 0.33), inset 0 1px 0 rgba(255, 255, 255, 0.9); }\n"
    "ribbon::part(tab-rail) { background-color: #9bb5d8; height: 27px; }\n"
    "ribbon::part(tab) { background-color: #9bb6da;\n"
    "  background-image: linear-gradient(to bottom, rgba(34, 70, 116, 0.15) 0%, rgba(0, 0, 0, 0) 72%);\n"
    "  border: 0 solid transparent; border-top-left-radius: 4px; border-top-right-radius: 4px;\n"
    "  border-bottom-left-radius: 0; border-bottom-right-radius: 0;\n"
    "  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.49);\n"
    "  gap: 3px; margin-top: 3px; taper: 4px #496c9f; edge-shadow: 5px rgba(24, 56, 97, 0.3); }\n"
    "ribbon::part(tab):state(selected) { background-color: #e8f2ff; margin-top: 0; margin-bottom: -2px; }\n"
    "ribbon::part(application-button) { background-image: linear-gradient(to bottom, #528be0 0%, #173f88 100%);\n"
    "  border: 0 solid transparent; border-radius: 0; box-shadow: none; }\n"
    "ribbon::part(command-plane) {\n"
    "  background-image: linear-gradient(to bottom, #e8f2ff 0%, #cadefb 28%, #a5c4f1 62%, #769fdf 100%);\n"
    "  border-top: 0 solid transparent; border-bottom: 1px solid #91acca; border-left: 0 solid transparent;\n"
    "  border-right: 0 solid transparent; border-radius: 0; height: 116px; }\n";

std::shared_ptr<const Theme> plan_paint_theme() {
    const std::string hash = theme_language::package_sha256_hex(theme_language::package_sha256(plan_paint_sheet));
    ThemePackage made{};
    made.manifest = "{\"format\": \"gui-forms-theme/1\", \"name\": \"plan-paint\", \"version\": \"1.0.0\", "
                    "\"engine\": \">=0.3\", \"sheet\": {\"path\": \"plan-paint.theme.css\", \"sha256\": \"" +
                    hash + "\"}}";
    made.files.push_back(ThemePackageFile{"plan-paint.theme.css", std::string(plan_paint_sheet)});
    ImageRegistry images{};
    const ThemeLibrary library{};
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    const ThemeCompileResult result = compile_theme_package(made, library, images, options);
    require(result.succeeded(), "Plan Paint's Ribbon rules compile");
    return result.theme();
}

// `scale` is the framebuffer's; `window_scale` the window's, which differ
// in an offscreen capture at another scale (Plan Paint's inspection frame).
void ribbon_faces_at(double scale, double window_scale, const std::shared_ptr<const Theme>& theme,
                     Sample rail_colour, double taper) {
    std::shared_ptr<Command> paste = std::make_shared<Command>("paste", "Paste");
    std::vector<RibbonTabSpec> tabs;
    RibbonTabSpec home{.stable_id = "home", .text = "Home"};
    RibbonGroupSpec clipboard{.stable_id = "clipboard", .caption = "Clipboard"};
    clipboard.commands.push_back({.stable_id = "paste", .command = paste});
    home.groups.push_back(std::move(clipboard));
    tabs.push_back(std::move(home));
    tabs.push_back(RibbonTabSpec{.stable_id = "view", .text = "View"});
    std::shared_ptr<Ribbon> ribbon = make_control<Ribbon>(StableId("ribbon"));
    (*ribbon).set_tabs(std::move(tabs));
    (*ribbon).set_theme_override(theme);
    const Size size{600.0, 200.0};
    Window window(ribbon, size);
    window.set_scale(window_scale);
    window.perform_layout();
    render::SkiaRaster raster{};
    require(raster.resize(size, scale), "the raster is allocated");
    DamageRegion damage = window.take_damage();
    damage.add(Rect{0.0, 0.0, size.width, size.height});
    raster.begin_frame(damage);
    raster.save();
    raster.clip_damage(damage);
    const std::optional<PaintReceipt> receipt = window.paint(raster, damage.rectangles());
    raster.restore();
    raster.end_frame();
    if (receipt) static_cast<void>(window.notify_presented(*receipt));
    const Ribbon::Parts parts = (*ribbon).parts();
    const std::vector<Ribbon::Tab> realized = (*ribbon).realized_tabs();
    require(realized.size() == 2U, "both tabs are realized");
    if (realized.size() != 2U) return;
    const Sample rail = sample(raster, scale, {560.0, 8.0});
    const Rect file = parts.application_button.bounds;
    const Sample file_face = sample(raster, scale, {file.x + 3.0, file.y + file.height - 4.0});
    const Rect view = realized[1].parts.tab.bounds;
    const Sample view_face = sample(raster, scale, {view.x + view.width - 10.0, view.y + view.height - 3.0});
    const Rect plane = parts.command_plane.bounds;
    const Sample plane_face = sample(raster, scale, {560.0, plane.y + plane.height * 0.5});
    print("rail", scale, rail);
    print("file", scale, file_face);
    print("view tab", scale, view_face);
    print("plane", scale, plane_face);
    require(near(rail, rail_colour.red, rail_colour.green, rail_colour.blue, 6), "the rail has its colour");
    require(file_face.blue > 100 && file_face.red < 80, "File is the blue gradient");
    require(view_face.blue > 180 && view_face.red < 200, "an unselected tab has its blue face");
    // The selected tab's pale face stops at its tapered edge's line: one
    // unit outside the line, down the lower half, is no lighter than the
    // shadow a unit further out, which only lightens as it falls off (the
    // pale fringe the owner would notice was lighter there).
    const Rect chosen = realized[0].parts.tab.bounds;
    const double right = chosen.x + chosen.width;
    bool fringe = false;
    for (double y = chosen.y + chosen.height * 0.5; y < chosen.y + chosen.height - 2.0; y += 1.0) {
        const double edge = right - taper * (1.0 - (y - chosen.y) / chosen.height);
        const Sample outside = sample(raster, scale, {edge + 1.0, y});
        const Sample beyond = sample(raster, scale, {edge + 2.0, y});
        if (outside.red > beyond.red + 20) fringe = true;
    }
    require(!fringe, "the selected tab's face does not run past its tapered edge");
    // At the top corner the shadow starts exactly where the last tab's face
    // ends, on the same rounded, tapered outline (radius 4 in both themes):
    // just outside the outline is shadow, never a gap of bare rail.
    const Rect last = realized[1].parts.tab.bounds;
    const double last_right = last.x + last.width;
    bool gap = false;
    for (double down = 0.5; down < 6.0; down += 1.0) {
        const double rise = std::max(0.0, 4.0 - down);
        const double inset = 4.0 - std::sqrt(std::max(0.0, 16.0 - rise * rise));
        const double outline = last_right - taper * (1.0 - down / last.height) - inset;
        const Sample outside = sample(raster, scale, {outline + 0.75, last.y + down});
        if (outside.red + 15 > rail_colour.red) gap = true;
    }
    require(!gap, "the shadow follows the tab's rounded, tapered outline from its top corner");
    require(plane_face.blue > 200 && plane_face.red < 220, "the plane has its blue gradient");
}

// The Gallery's commands page (the coordinator's request): its Ribbon,
// scrolled into view, painted at framebuffer scale `scale` from a window at
// `window_scale`, keeps its rail and plane faces.
void gallery_ribbon_at(double scale, double window_scale) {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    window.set_scale(window_scale);
    window.perform_layout();
    const std::shared_ptr<Button> navigation =
        std::dynamic_pointer_cast<Button>(window.find("gallery.page.commands-and-menus"));
    require(navigation != nullptr, "the commands page's navigation row exists");
    if (!navigation) return;
    static_cast<void>((*navigation).perform_click());
    window.perform_layout();
    const std::shared_ptr<Ribbon> ribbon =
        std::dynamic_pointer_cast<Ribbon>(window.find("gallery.commands.ribbon.bar"));
    require(ribbon != nullptr, "the commands page holds its Ribbon");
    if (!ribbon) return;
    for (Control::Ptr current = (*ribbon).parent(); current; current = (*current).parent()) {
        ScrollableControl* const scroller = dynamic_cast<ScrollableControl*>(current.get());
        if (scroller == nullptr) continue;
        (*scroller).scroll_control_into_view(ribbon);
        window.perform_layout();
    }
    const Size size = window.client_size();
    render::SkiaRaster raster{};
    require(raster.resize(size, scale), "the Gallery raster is allocated");
    DamageRegion damage = window.take_damage();
    damage.add(Rect{0.0, 0.0, size.width, size.height});
    raster.begin_frame(damage);
    raster.save();
    raster.clip_damage(damage);
    const std::optional<PaintReceipt> receipt = window.paint(raster, damage.rectangles());
    raster.restore();
    raster.end_frame();
    if (receipt) static_cast<void>(window.notify_presented(*receipt));
    const Rect placed = (*ribbon).absolute_bounds();
    const Ribbon::Parts parts = (*ribbon).parts();
    const Point rail{placed.x + placed.width - 12.0, placed.y + 8.0};
    const Point plane{placed.x + placed.width - 12.0, placed.y + parts.command_plane.bounds.y + 20.0};
    const bool in_view = rail.y >= 0.0 && plane.y < size.height && rail.x < size.width;
    require(in_view, "the Ribbon is in view");
    if (!in_view) return;
    const Sample rail_face = sample(raster, scale, rail);
    const Sample plane_face = sample(raster, scale, plane);
    print("gallery rail", scale, rail_face);
    print("gallery plane", scale, plane_face);
    require(near(rail_face, 0x93, 0xad, 0xd0, 6), "the Gallery's Ribbon rail has its colour");
    require(plane_face.blue > 200 && plane_face.blue - plane_face.red > 12,
            "the Gallery's Ribbon plane has its blue gradient");
}

// The bundled faces, registered once with every raster that measures or
// paints text here; without them the raster measures nothing and a fit
// check would prove nothing.
std::shared_ptr<detail::BundledFontSet> bundled_fonts() {
#if defined(_WIN32)
    const detail::SealedPageFunctions pages = host::windows_sealed_pages();
#else
    const detail::SealedPageFunctions pages = host::posix_sealed_pages();
#endif
    std::shared_ptr<detail::BundledFontSet> set = detail::shared_bundled_font_set(GUI_FORMS_TEST_FONT_DIRECTORY, pages);
    return set;
}

void paint_whole(Window& window, render::SkiaRaster& raster, Size size) {
    DamageRegion damage = window.take_damage();
    damage.add(Rect{0.0, 0.0, size.width, size.height});
    raster.begin_frame(damage);
    raster.save();
    raster.clip_damage(damage);
    const std::optional<PaintReceipt> receipt = window.paint(raster, damage.rectangles());
    raster.restore();
    raster.end_frame();
    if (receipt) static_cast<void>(window.notify_presented(*receipt));
}

// Every label of the selected tab's commands fits its part, measured by
// the raster that paints it: a command's label in the Ribbon's label font,
// a check box's in the CheckBox's own font, its label part inside the
// command's cell. `where` names the case in a failure.
void labels_fit(const Ribbon& ribbon, render::SkiaRaster& raster, const std::string& where) {
    const std::optional<std::size_t> selected = ribbon.selected_tab.read();
    require(selected.has_value(), "a tab is selected");
    if (!selected) return;
    const std::span<const RibbonTabSpec> tabs = ribbon.tabs();
    const FontSpec label_font = ribbon.text_font(ribbon.font.read());
    std::size_t checked = 0U;
    for (const Ribbon::CommandItem& item : ribbon.realized_commands()) {
        const RibbonCommandSpec& spec = tabs[*selected].groups[item.group].commands[item.index];
        std::string authored = spec.text;
        if (authored.empty() && spec.command) authored = (*spec.command).state().text;
        const std::string shown = parse_mnemonic_text(authored).display_text;
        if (shown.empty()) continue;
        if (item.parts.command_label.present) {
            const Rect bounds = item.parts.command_label.bounds;
            const Size measured = raster.measure_text_utf8(shown, label_font);
            const bool fits = measured.width > 0.0 && measured.width <= bounds.width + 0.5 &&
                              measured.height <= bounds.height + 0.5;
            if (!fits) {
                std::cerr << where << ": label '" << shown << "' " << measured.width << "x" << measured.height
                          << " in " << bounds.width << "x" << bounds.height << '\n';
            }
            require(fits, "a command's label fits its part");
            ++checked;
        }
        if (spec.kind == RibbonCommandKind::check_box && item.parts.command_check_box.present) {
            const std::shared_ptr<CheckBox> box = ribbon.check_box(spec.stable_id);
            const FontSpec box_font = (*box).text_font((*box).font.read());
            const Size measured = raster.measure_text_utf8(shown, box_font);
            const Rect label = (*box).parts().label.bounds;
            const Rect cell = item.parts.command_check_box.bounds;
            const Rect placed = (*box).committed_arranged_bounds();
            const bool fits = measured.width > 0.0 && measured.width <= label.width + 0.5 &&
                              measured.height <= label.height + 0.5 && placed.width <= cell.width + 0.5;
            if (!fits) {
                std::cerr << where << ": check box '" << shown << "' " << measured.width << "x" << measured.height
                          << " in " << label.width << "x" << label.height << '\n';
            }
            require(fits, "a check box's label fits in its own font");
            ++checked;
        }
    }
    require(checked > 0U, "some labels were checked");
}

// A Plan Paint-like View tab (the owner's report: Show/Hide cut off) and
// paper fields (spin buttons clickable but unpainted), through the CPU
// raster with the bundled faces as the window's text metrics.
void hosted_commands_at(double scale, const std::shared_ptr<const Theme>& theme, const std::string& where) {
    std::shared_ptr<Command> zoom_in = std::make_shared<Command>("zoom-in", "Zoom in");
    std::shared_ptr<Command> zoom_out = std::make_shared<Command>("zoom-out", "Zoom out");
    std::shared_ptr<Command> actual = std::make_shared<Command>("actual-size", "100%");
    std::shared_ptr<Command> full = std::make_shared<Command>("full-screen", "Full screen");
    RibbonTabSpec view{.stable_id = "view", .text = "View"};
    RibbonGroupSpec zoom{.stable_id = "zoom", .caption = "Zoom"};
    zoom.commands.push_back({.stable_id = "zoom-in", .command = zoom_in});
    zoom.commands.push_back({.stable_id = "zoom-out", .size = RibbonCommandSize::small, .command = zoom_out});
    zoom.commands.push_back({.stable_id = "actual-size", .size = RibbonCommandSize::small, .command = actual});
    zoom.commands.push_back({.stable_id = "full-screen", .size = RibbonCommandSize::small, .command = full});
    RibbonGroupSpec show{.stable_id = "show-hide", .caption = "Show/Hide"};
    show.commands.push_back({.stable_id = "rulers", .kind = RibbonCommandKind::check_box, .text = "Rulers"});
    show.commands.push_back({.stable_id = "gridlines", .kind = RibbonCommandKind::check_box, .text = "Gridlines"});
    show.commands.push_back({.stable_id = "status-bar", .kind = RibbonCommandKind::check_box, .text = "Status bar"});
    show.commands.push_back({.stable_id = "fill", .kind = RibbonCommandKind::check_box, .text = "Fill"});
    show.commands.push_back(
        {.stable_id = "edge", .kind = RibbonCommandKind::check_box, .text = "Edge", .beside_previous = true});
    RibbonGroupSpec paper{.stable_id = "paper", .caption = "Paper"};
    paper.commands.push_back({.stable_id = "grain-label", .kind = RibbonCommandKind::label, .text = "Grain scale"});
    paper.commands.push_back({.stable_id = "grain", .kind = RibbonCommandKind::numeric_up_down, .text = "Grain scale"});
    paper.commands.push_back({.stable_id = "load-label", .kind = RibbonCommandKind::label, .text = "Paint load"});
    paper.commands.push_back({.stable_id = "load", .kind = RibbonCommandKind::numeric_up_down, .text = "Paint load"});
    view.groups.push_back(std::move(zoom));
    view.groups.push_back(std::move(show));
    view.groups.push_back(std::move(paper));
    std::vector<RibbonTabSpec> tabs;
    tabs.push_back(std::move(view));
    std::shared_ptr<Ribbon> ribbon = make_control<Ribbon>(StableId("ribbon"));
    (*ribbon).set_tabs(std::move(tabs));
    (*ribbon).set_theme_override(theme);
    const Size size{900.0, 200.0};
    render::SkiaRaster raster{};
    require(raster.resize(size, scale), "the raster is allocated");
    require(raster.register_bundled_fonts(bundled_fonts()), "the bundled faces are registered");
    Window window(ribbon, size);
    window.set_scale(scale);
    window.set_text_metrics_provider(&raster);
    window.perform_layout();
    paint_whole(window, raster, size);
    labels_fit(*ribbon, raster, where);
    // Each hosted number field paints its whole bounds: its spin column
    // shows its arrow glyphs (darker than the field and the plane), so
    // what the pointer can press is what is drawn.
    for (const std::string_view id : {std::string_view("grain"), std::string_view("load")}) {
        const std::shared_ptr<NumericUpDown> field = (*ribbon).numeric_up_down(id);
        const Rect placed = (*field).absolute_bounds();
        const NumericUpDown::Parts parts = (*field).parts();
        require(parts.increment_glyph.present && parts.decrement_glyph.present, "the field has its spin glyphs");
        const Rect glyph = parts.increment_glyph.bounds;
        int darkest = 255;
        for (double y = glyph.y; y < glyph.y + glyph.height; y += 0.5) {
            for (double x = glyph.x; x < glyph.x + glyph.width; x += 0.5) {
                const Sample seen = sample(raster, scale, {placed.x + x, placed.y + y});
                darkest = std::min(darkest, seen.red);
            }
        }
        if (darkest >= 140) std::cerr << where << ": spin glyph darkest red " << darkest << '\n';
        require(darkest < 140, "a hosted number field paints its spin arrows");
        // The column sits inside the field's drawn bounds, at its right end.
        const Rect arrow = parts.increment_arrow.bounds;
        require(arrow.x + arrow.width <= placed.width + 0.5 && arrow.x > placed.width * 0.5,
                "the spin column is the field's right end");
        const Point press{placed.x + arrow.x + arrow.width * 0.5, placed.y + arrow.y + arrow.height * 0.5};
        Control::Ptr hit = window.hit_test(press);
        bool inside = false;
        for (Control::Ptr current = hit; current; current = (*current).parent()) {
            if (current.get() == field.get()) inside = true;
        }
        require(inside, "a press on the painted spin arrow reaches the field");
    }
    window.set_text_metrics_provider(nullptr);
}

// The Gallery's commands page Ribbon: every label fits, at `scale`.
void gallery_labels_at(double scale) {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    const Size size = window.client_size();
    render::SkiaRaster raster{};
    require(raster.resize(size, scale), "the Gallery raster is allocated");
    require(raster.register_bundled_fonts(bundled_fonts()), "the bundled faces are registered");
    window.set_scale(scale);
    window.set_text_metrics_provider(&raster);
    window.perform_layout();
    const std::shared_ptr<Button> navigation =
        std::dynamic_pointer_cast<Button>(window.find("gallery.page.commands-and-menus"));
    if (navigation) static_cast<void>((*navigation).perform_click());
    window.perform_layout();
    const std::shared_ptr<Ribbon> ribbon =
        std::dynamic_pointer_cast<Ribbon>(window.find("gallery.commands.ribbon.bar"));
    require(ribbon != nullptr, "the commands page holds its Ribbon");
    if (ribbon) {
        paint_whole(window, raster, size);
        labels_fit(*ribbon, raster, "gallery at " + std::to_string(scale));
    }
    window.set_text_metrics_provider(nullptr);
}

} // namespace


int main() {
    const std::shared_ptr<const Theme> stock = gui_forms_theme();
    const std::shared_ptr<const Theme> plan_paint = plan_paint_theme();
    for (const double scale : {1.0, 2.0}) {
        ribbon_faces_at(scale, scale, stock, Sample{0x93, 0xad, 0xd0}, 5.0);
        if (plan_paint) ribbon_faces_at(scale, scale, plan_paint, Sample{0x9b, 0xb5, 0xd8}, 4.0);
        // The other scale's window into this scale's framebuffer.
        const double other = scale == 1.0 ? 2.0 : 1.0;
        ribbon_faces_at(scale, other, stock, Sample{0x93, 0xad, 0xd0}, 5.0);
        if (plan_paint) ribbon_faces_at(scale, other, plan_paint, Sample{0x9b, 0xb5, 0xd8}, 4.0);
        gallery_ribbon_at(scale, scale);
        gallery_ribbon_at(scale, other);
        hosted_commands_at(scale, stock, "stock at " + std::to_string(scale));
        if (plan_paint) hosted_commands_at(scale, plan_paint, "Plan Paint at " + std::to_string(scale));
        gallery_labels_at(scale);
    }
    if (failures != 0) {
        std::cerr << failures << " ribbon scale raster check(s) failed\n";
        return 1;
    }
    std::cout << "Ribbon faces through the CPU raster at scales 1 and 2, the Gallery commands page included, passed\n";
    return 0;
}
