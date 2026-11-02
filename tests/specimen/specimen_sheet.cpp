// The specimen sheet (stage T4, phase 2): one instance of every stock control
// the Gallery shows, driven through its published states (rest, hot,
// pressed, keyboard focus, checked or selected, disabled, invalid), rendered
// with the CPU Skia raster and the bundled fonts in light, dark and high
// contrast at scales 1 and 2, written as contact sheets with a hash
// manifest, and with the contrast measured in pixels where text sits.
//
//   gui_forms_specimen_sheet --out <directory> [--repeat] [--theme gui-forms] [--light]
//   gui_forms_specimen_sheet --smoke
//
// The Gallery's own theme (windows-professional) is drawn unless --theme
// names the default theme `gui-forms` (stage T4, phase 3); --light
// captures light only, at scales 1 and 2.
//
// A full run writes, under <directory>, one PNG contact sheet per
// presentation, scale and group of rows (sheet-<presentation>-<scale>x-<n>.png),
// manifest.tsv (one row per cell: the cell's pixel hash and its smallest
// text contrast) and contrast.tsv (every text run whose pixel contrast is
// under its threshold). --repeat captures everything a second time and
// fails unless every cell's pixels are byte-identical. --smoke captures the
// first page's specimens in light at scale 1, twice, and fails on any
// difference; it is the ctest entry.
//
// Determinism: each specimen is captured in a Gallery window of its own,
// made fresh, so that no state driven on one control shows on another; the
// windows run with reduced motion and no ambient motion, so that no
// transition is caught midway; the month calendars' "today" is pinned.

#include "demo/gallery.hpp"
#include "demo/gallery_page_specs.hpp"
#include "gui_forms/gui_forms.hpp"
#include "skia_raster.hpp"
#include "../src/core/image/png_writer.hpp"
#include "../src/core/resources/fonts/bundled_font_set.hpp"
#if defined(_WIN32)
#include "../src/host/windows/fonts/sealed_pages_windows.hpp"
#else
#include "../src/host/posix/fonts/sealed_pages_posix.hpp"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

// -- what is captured ----------------------------------------------------------

enum class SpecimenState : std::uint8_t { rest, hot, toggled, disabled, invalid, focus, pressed, count };

constexpr std::size_t specimen_state_count = static_cast<std::size_t>(SpecimenState::count);

// The columns in capture order (each in a fresh window, so order matters
// only for reading the sheet). "toggled" is the checked or selected value
// other than the one the Gallery shows (see drive).
constexpr std::array<std::string_view, specimen_state_count> state_names{
    "rest", "hot", "toggled", "disabled", "invalid", "focus", "pressed"};

enum class Presentation : std::uint8_t { light, dark, high_contrast };

struct SheetConfig final {
    Presentation presentation{Presentation::light};
    double scale{1.0};
    // The theme the windows draw (null, or holding null, for the Gallery's
    // own); borrowed from the run, which holds it.
    const std::shared_ptr<const Theme>* theme{};
};

std::string_view presentation_name(Presentation presentation) noexcept {
    if (presentation == Presentation::dark) return "dark";
    if (presentation == Presentation::high_contrast) return "high-contrast";
    return "light";
}

// One stock control's representative: the first instance, in page order and
// then tree order, that shows whole inside the window. `path` is its child
// indices from the window's root, which find it again in a fresh Gallery.
struct Specimen final {
    std::string control_id;
    std::string page;
    std::vector<std::size_t> path;
};

// One captured cell: the crop's pixels (premultiplied BGRA, `width` by
// `height` device pixels, tightly packed), its hash, and the smallest text
// contrast measured in it (0 when no text sits in it, or the state does not
// apply).
struct Cell final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;
    std::uint64_t hash{};
    bool applies{};
    double smallest_contrast{};
};

// A text run below its threshold, for contrast.tsv.
struct ContrastFinding final {
    std::string config;
    std::string control_id;
    std::string_view state;
    Color text{};
    Color ground{};
    double ratio{};
    double threshold{};
};

// The largest crop a cell shows, in logical units: a larger control shows
// its top left corner. The margin covers focus rings and default rings drawn
// outside the bounds.
constexpr double cell_margin = 4.0;
constexpr double cell_width = 240.0;
constexpr double cell_height = 80.0;
constexpr double label_width = 200.0;
constexpr double gutter = 8.0;
constexpr std::size_t rows_per_sheet = 20U;

// -- colour arithmetic (WCAG 2, as src/core/theme/contrast) ----------------------

double channel_luminance(std::uint8_t value) noexcept {
    const double c = static_cast<double>(value) / 255.0;
    const double linear = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    return linear;
}

double relative_luminance(Color colour) noexcept {
    const double result = 0.2126 * channel_luminance(colour.red) + 0.7152 * channel_luminance(colour.green) +
                          0.0722 * channel_luminance(colour.blue);
    return result;
}

double contrast_ratio(Color first, Color second) noexcept {
    const double a = relative_luminance(first);
    const double b = relative_luminance(second);
    const double lighter = std::max(a, b);
    const double darker = std::min(a, b);
    const double ratio = (lighter + 0.05) / (darker + 0.05);
    return ratio;
}

// `top` over the opaque `ground`.
Color composite_over(Color top, Color ground) noexcept {
    const double alpha = static_cast<double>(top.alpha) / 255.0;
    const double red = top.red * alpha + ground.red * (1.0 - alpha);
    const double green = top.green * alpha + ground.green * (1.0 - alpha);
    const double blue = top.blue * alpha + ground.blue * (1.0 - alpha);
    const Color result = Color::rgba(static_cast<std::uint8_t>(std::lround(red)),
                                     static_cast<std::uint8_t>(std::lround(green)),
                                     static_cast<std::uint8_t>(std::lround(blue)));
    return result;
}

std::string colour_text(Color colour) {
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", colour.red, colour.green, colour.blue, colour.alpha);
    return std::string(buffer);
}

std::uint64_t fnv1a(std::uint64_t value, std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t result = value;
    for (const std::uint8_t byte : bytes) {
        result ^= byte;
        result *= 1099511628211ULL;
    }
    return result;
}

constexpr std::uint64_t fnv_basis = 1469598103934665603ULL;

// -- text runs -------------------------------------------------------------------

// A text run as painted, in window logical coordinates.
struct TextRun final {
    Rect bounds{};
    Color color{};
};

// The recorder's saved state: the translation and the clip, in window
// logical coordinates.
struct RecorderState final {
    Point offset{};
    Rect clip{};
    bool clipped{false};
};

Rect intersection(Rect left, Rect right) noexcept {
    const double x = std::max(left.x, right.x);
    const double y = std::max(left.y, right.y);
    const double far_x = std::min(left.x + left.width, right.x + right.width);
    const double far_y = std::min(left.y + left.height, right.y + right.height);
    const Rect result{x, y, std::max(0.0, far_x - x), std::max(0.0, far_y - y)};
    return result;
}

// Records where text is painted: the translation and clip stack and every
// text call, measured by `metrics` (the raster the window paints into,
// borrowed for the recorder's life). A run is cut to the clip in force, and
// a run the clip hides entirely is not recorded. Paints nothing.
class TextRecorder final : public Painter {
public:
    explicit TextRecorder(Painter& metrics) : metrics_(metrics) {}

    void save() override { saved_.push_back(state_); }
    void restore() override {
        if (saved_.empty()) return;
        state_ = saved_.back();
        saved_.pop_back();
    }
    void translate(Point offset) override {
        state_.offset.x = state_.offset.x + offset.x;
        state_.offset.y = state_.offset.y + offset.y;
    }
    void clip_rect(Rect rect) override {
        const Rect placed{state_.offset.x + rect.x, state_.offset.y + rect.y, rect.width, rect.height};
        state_.clip = state_.clipped ? intersection(state_.clip, placed) : placed;
        state_.clipped = true;
    }
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_text_utf8(Point origin, std::string_view text, FontSpec font, Color color) override {
        if (text.empty()) return;
        const Size measured = metrics_.measure_text_utf8(text, font);
        // The baseline sits at `origin`; ascent and descent are taken as
        // 0.8 and 0.25 of the size, which covers the bundled faces' Latin.
        TextRun run{};
        run.bounds = Rect{state_.offset.x + origin.x, state_.offset.y + origin.y - font.size * 0.8, measured.width,
                          font.size * 1.05};
        if (state_.clipped) run.bounds = intersection(run.bounds, state_.clip);
        if (run.bounds.width <= 0.0 || run.bounds.height <= 0.0) return;
        run.color = color;
        runs.push_back(run);
    }
    [[nodiscard]] Size measure_text_utf8(std::string_view text, FontSpec font) override {
        const Size result = metrics_.measure_text_utf8(text, font);
        return result;
    }

    std::vector<TextRun> runs;

private:
    Painter& metrics_;
    RecorderState state_{};
    std::vector<RecorderState> saved_;
};

// -- the Gallery window ----------------------------------------------------------

void pin_today(const Control::Ptr& control) {
    const std::shared_ptr<MonthCalendar> calendar = std::dynamic_pointer_cast<MonthCalendar>(control);
    if (calendar != nullptr) (*calendar).set_today_date(DateTimeValue{2026, 10, 19});
    for (const Control::Ptr& child : (*control).children()) pin_today(child);
}

PresentationSettings settings_for(const PresentationSettings& base, Presentation presentation) {
    PresentationSettings settings = base;
    settings.reduced_motion = true;
    settings.ambient_motion = false;
    settings.high_contrast = presentation == Presentation::high_contrast;
    settings.colour_scheme = presentation == Presentation::dark ? ColourScheme::dark : ColourScheme::light;
    return settings;
}

// A fresh Gallery on `page`, painting through `raster` at `config`. The
// raster is declared before the window by every caller, so it outlives it.
std::unique_ptr<Window> gallery_on(render::SkiaRaster& raster, std::string_view page, SheetConfig config) {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    // A headless window starts inactive; the sheet shows an active one.
    window.set_active(true);
    if (config.theme != nullptr && *config.theme != nullptr) window.set_theme(*config.theme);
    window.set_presentation_settings(settings_for(window.presentation_settings(), config.presentation));
    window.set_scale(config.scale);
    window.set_text_metrics_provider(&raster);
    window.set_framebuffer_painter(&raster);
    window.perform_layout();
    static_cast<void>(gallery::select_gallery_page(window, page));
    pin_today(window.root());
    window.perform_layout();
    return owned;
}

Control::Ptr control_at(const Window& window, const std::vector<std::size_t>& path) {
    Control::Ptr control = window.root();
    for (const std::size_t index : path) {
        const std::span<const Control::Ptr> children = (*control).children();
        if (index >= children.size()) return nullptr;
        control = children[index];
    }
    return control;
}

bool inside(Rect inner, Rect outer) noexcept {
    const bool result = inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
                        inner.y + inner.height <= outer.y + outer.height;
    return result;
}

// Whether `control` shows whole: visible and enabled, at least 8 units each
// way, and inside the window and every ancestor.
bool shows_whole(const Window& window, const Control& control) {
    if (!control.effectively_visible() || !control.enabled()) return false;
    const Rect bounds = control.absolute_bounds();
    if (bounds.width < 8.0 || bounds.height < 8.0) return false;
    const Rect client{0.0, 0.0, window.client_size().width, window.client_size().height};
    if (!inside(bounds, client)) return false;
    Control::Ptr parent = control.parent();
    while (parent != nullptr) {
        if (!inside(bounds, (*parent).absolute_bounds())) return false;
        parent = (*parent).parent();
    }
    return true;
}

void collect_specimens(const Window& window, const Control::Ptr& control, std::string_view page,
                       std::vector<std::size_t>& path, std::vector<Specimen>& specimens) {
    const std::string_view id = (*control).anatomy().control_id;
    bool known = id == "control" || id.empty();
    for (const Specimen& specimen : specimens) known = known || specimen.control_id == id;
    if (!known && shows_whole(window, *control)) {
        Specimen specimen{};
        specimen.control_id = std::string(id);
        specimen.page = std::string(page);
        specimen.path = path;
        specimens.push_back(specimen);
    }
    const std::span<const Control::Ptr> children = (*control).children();
    for (std::size_t index = 0; index < children.size(); ++index) {
        path.push_back(index);
        collect_specimens(window, children[index], page, path, specimens);
        path.pop_back();
    }
}

// -- driving a state ---------------------------------------------------------------

bool declares(const Control& control, AnatomyState state) {
    for (const AnatomyState declared : control.anatomy().states) {
        if (declared == state) return true;
    }
    return false;
}

void send_pointer(Window& window, PointerAction action, PointerButton button, Point position) {
    PointerEvent event{};
    event.action = action;
    event.button = button;
    event.position = position;
    static_cast<void>(window.dispatch_pointer(event));
}

// The point the pointer is driven to for hot and pressed: the control's
// centre when the control itself is hit there, else the first point of a
// grid over its bounds (row by row, nine by nine) that hits the control
// itself rather than a child or a sibling over it, else the centre. A
// composite control's centre often lies on a child (a tab control's page,
// a panel's label), which would make the child hot instead.
Point own_point(Window& window, const Control::Ptr& control) {
    const Rect bounds = (*control).absolute_bounds();
    const Point centre{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    if (window.hit_test(centre) == control) return centre;
    constexpr int grid = 9;
    for (int row = 0; row < grid; ++row) {
        for (int column = 0; column < grid; ++column) {
            const Point point{bounds.x + bounds.width * (static_cast<double>(column) + 0.5) / grid,
                              bounds.y + bounds.height * (static_cast<double>(row) + 0.5) / grid};
            if (window.hit_test(point) == control) return point;
        }
    }
    return centre;
}

// Puts `control` in `state`. False when the state does not apply to it: a
// control that declares neither checked nor selected has no toggled column,
// and one that refuses keyboard focus has no focus column. Toggled is the
// other value from the one the Gallery shows: a press for a control that
// declares checked (a check box shown checked shows unchecked), a select
// for one that declares selected.
bool drive(Window& window, const Control::Ptr& control, SpecimenState state) {
    const Point target = own_point(window, control);
    switch (state) {
    case SpecimenState::rest:
        return true;
    case SpecimenState::hot:
        send_pointer(window, PointerAction::move, PointerButton::none, target);
        return true;
    case SpecimenState::pressed:
        send_pointer(window, PointerAction::move, PointerButton::none, target);
        send_pointer(window, PointerAction::down, PointerButton::primary, target);
        return true;
    case SpecimenState::focus: {
        const bool focused =
            window.perform_semantic_action((*control).runtime_id(), std::string_view(), SemanticAction::focus);
        return focused;
    }
    case SpecimenState::toggled:
        if (declares(*control, AnatomyState::checked)) {
            const bool pressed =
                window.perform_semantic_action((*control).runtime_id(), std::string_view(), SemanticAction::press);
            return pressed;
        }
        if (declares(*control, AnatomyState::selected)) {
            const bool selected =
                window.perform_semantic_action((*control).runtime_id(), std::string_view(), SemanticAction::select);
            return selected;
        }
        return false;
    case SpecimenState::disabled:
        (*control).set_enabled(false);
        return true;
    case SpecimenState::invalid:
        (*control).set_visual_status(ControlVisualStatus::invalid);
        return true;
    case SpecimenState::count:
        return false;
    }
    // A state outside the enumeration does not apply.
    return false;
}

// -- capture -----------------------------------------------------------------------

// The logical rect a cell crops: the control's bounds grown by the margin,
// at most cell_width by cell_height, inside the window.
Rect cell_rect(const Window& window, const Control& control) {
    const Rect bounds = control.absolute_bounds();
    const double left = std::max(0.0, bounds.x - cell_margin);
    const double top = std::max(0.0, bounds.y - cell_margin);
    const double right = std::min(window.client_size().width, bounds.x + bounds.width + cell_margin);
    const double bottom = std::min(window.client_size().height, bounds.y + bounds.height + cell_margin);
    const Rect result{left, top, std::min(cell_width, right - left), std::min(cell_height, bottom - top)};
    return result;
}

std::uint32_t device(double logical, double scale) noexcept {
    const std::uint32_t result = static_cast<std::uint32_t>(std::lround(logical * scale));
    return result;
}

// The opaque colour of the pixel at (x, y) of the raster, unpremultiplied.
Color raster_colour(const render::SkiaRaster& raster, std::uint32_t x, std::uint32_t y) noexcept {
    const std::uint8_t* const row = static_cast<const std::uint8_t*>(raster.pixels()) + raster.row_bytes() * y;
    const std::uint8_t* const pixel = row + static_cast<std::size_t>(x) * 4U;
    const Color result = Color::rgba(pixel[2], pixel[1], pixel[0], pixel[3]);
    return result;
}

// Whether `colour` is within a small distance of `text`'s opaque colour:
// a glyph pixel rather than ground.
bool near_text(Color colour, Color text) noexcept {
    const int distance = std::abs(static_cast<int>(colour.red) - static_cast<int>(text.red)) +
                         std::abs(static_cast<int>(colour.green) - static_cast<int>(text.green)) +
                         std::abs(static_cast<int>(colour.blue) - static_cast<int>(text.blue));
    const bool result = distance <= 24;
    return result;
}

// The most frequent colour in `area` (device pixels) of the raster, glyph
// pixels near `text` left out: the ground a text run sits on. When every
// pixel is near the text colour, the text shows on its own colour and that
// colour is the ground. `scratch` is reused storage.
Color ground_colour(const render::SkiaRaster& raster, std::uint32_t left, std::uint32_t top, std::uint32_t right,
                    std::uint32_t bottom, Color text, std::vector<std::uint32_t>& scratch) {
    scratch.clear();
    for (std::uint32_t y = top; y < bottom; ++y) {
        for (std::uint32_t x = left; x < right; ++x) {
            const Color colour = raster_colour(raster, x, y);
            if (near_text(colour, text)) continue;
            const std::uint32_t packed = (static_cast<std::uint32_t>(colour.red) << 24U) |
                                         (static_cast<std::uint32_t>(colour.green) << 16U) |
                                         (static_cast<std::uint32_t>(colour.blue) << 8U) | colour.alpha;
            scratch.push_back(packed);
        }
    }
    if (scratch.empty()) return Color::rgba(text.red, text.green, text.blue);
    std::sort(scratch.begin(), scratch.end());
    std::uint32_t best = scratch.front();
    std::size_t best_count = 0U;
    std::size_t start = 0U;
    while (start < scratch.size()) {
        std::size_t end = start;
        while (end < scratch.size() && scratch[end] == scratch[start]) ++end;
        if (end - start > best_count) {
            best_count = end - start;
            best = scratch[start];
        }
        start = end;
    }
    const Color result = Color::rgba(static_cast<std::uint8_t>(best >> 24U), static_cast<std::uint8_t>(best >> 16U),
                                     static_cast<std::uint8_t>(best >> 8U), static_cast<std::uint8_t>(best));
    return result;
}

// The deepest effectively visible control under `point` (window logical
// coordinates), starting from `control`, which contains it.
const Control* control_under(const Control& control, Point point) {
    const std::span<const Control::Ptr> children = control.children();
    for (std::size_t index = children.size(); index > 0U; --index) {
        const Control& child = *children[index - 1U];
        if (!child.effectively_visible()) continue;
        const Rect bounds = child.absolute_bounds();
        const bool within = point.x >= bounds.x && point.x < bounds.x + bounds.width && point.y >= bounds.y &&
                            point.y < bounds.y + bounds.height;
        if (within) return control_under(child, point);
    }
    return &control;
}

// 7:1 in high contrast; otherwise 3:1 for text a disabled control paints
// (the cell's state, or a disabled control inside the specimen), 4.5:1 else.
double threshold_for(Presentation presentation, bool disabled) noexcept {
    if (presentation == Presentation::high_contrast) return 7.0;
    if (disabled) return 3.0;
    return 4.5;
}

struct CaptureContext final {
    const std::shared_ptr<detail::BundledFontSet>* fonts{};
    SheetConfig config{};
    std::string config_name;
    std::vector<ContrastFinding>* findings{};
    std::vector<std::uint32_t> scratch;
};

// Paints the whole window into `raster` and copies `crop` out as `cell`;
// measures each text run whose centre lies in `bounds`.
void capture(Window& window, render::SkiaRaster& raster, Rect crop, Rect bounds, SpecimenState state,
             const Specimen& specimen, CaptureContext& context, Cell& cell) {
    window.perform_layout();
    const Rect whole{0.0, 0.0, window.client_size().width, window.client_size().height};
    DamageRegion damage;
    damage.add(whole);
    static_cast<void>(raster.synchronize_images(window.image_resources()));
    raster.begin_frame(damage);
    static_cast<void>(window.paint(raster, whole));
    raster.end_frame();
    const double scale = context.config.scale;
    const std::uint32_t left = device(crop.x, scale);
    const std::uint32_t top = device(crop.y, scale);
    cell.width = std::min(device(crop.width, scale), raster.pixel_width() - left);
    cell.height = std::min(device(crop.height, scale), raster.pixel_height() - top);
    const std::size_t row_bytes = static_cast<std::size_t>(cell.width) * 4U;
    cell.pixels.assign(row_bytes * cell.height, 0U);
    for (std::uint32_t row = 0U; row < cell.height; ++row) {
        const std::uint8_t* const source =
            static_cast<const std::uint8_t*>(raster.pixels()) + raster.row_bytes() * (top + row) + left * 4U;
        std::memcpy(cell.pixels.data() + row_bytes * row, source, row_bytes);
    }
    cell.hash = fnv1a(fnv_basis, cell.pixels);

    TextRecorder recorder(raster);
    static_cast<void>(window.paint(recorder, whole));
    cell.smallest_contrast = 0.0;
    for (const TextRun& run : recorder.runs) {
        const double centre_x = run.bounds.x + run.bounds.width * 0.5;
        const double centre_y = run.bounds.y + run.bounds.height * 0.5;
        const bool within = centre_x >= bounds.x && centre_x < bounds.x + bounds.width && centre_y >= bounds.y &&
                            centre_y < bounds.y + bounds.height;
        if (!within || run.color.alpha == 0U || run.bounds.width <= 0.0) continue;
        const std::uint32_t run_left = std::min(device(std::max(0.0, run.bounds.x), scale), raster.pixel_width());
        const std::uint32_t run_top = std::min(device(std::max(0.0, run.bounds.y), scale), raster.pixel_height());
        const std::uint32_t run_right =
            std::min(device(run.bounds.x + run.bounds.width, scale), raster.pixel_width());
        const std::uint32_t run_bottom =
            std::min(device(run.bounds.y + run.bounds.height, scale), raster.pixel_height());
        if (run_right <= run_left || run_bottom <= run_top) continue;
        const Color ground =
            ground_colour(raster, run_left, run_top, run_right, run_bottom, run.color, context.scratch);
        const Color text = composite_over(run.color, ground);
        const double ratio = contrast_ratio(text, ground);
        if (cell.smallest_contrast == 0.0 || ratio < cell.smallest_contrast) cell.smallest_contrast = ratio;
        const Control* const owner = control_under(*window.root(), Point{centre_x, centre_y});
        const bool disabled = state == SpecimenState::disabled || !(*owner).effectively_enabled();
        const double threshold = threshold_for(context.config.presentation, disabled);
        if (ratio >= threshold) continue;
        ContrastFinding finding{};
        finding.config = context.config_name;
        finding.control_id = specimen.control_id;
        finding.state = state_names[static_cast<std::size_t>(state)];
        finding.text = run.color;
        finding.ground = ground;
        finding.ratio = ratio;
        finding.threshold = threshold;
        (*context.findings).push_back(finding);
    }
}

// Captures one specimen in every state, each in a fresh Gallery window.
void capture_specimen(const Specimen& specimen, CaptureContext& context,
                      std::array<Cell, specimen_state_count>& cells) {
    for (std::size_t index = 0; index < specimen_state_count; ++index) {
        const SpecimenState state = static_cast<SpecimenState>(index);
        Cell& cell = cells[index];
        cell = Cell{};
        // Declared before the window, which borrows it.
        render::SkiaRaster raster{};
        const bool ready = raster.resize(Size{1280.0, 820.0}, context.config.scale) &&
                           raster.register_bundled_fonts(*context.fonts);
        if (!ready) continue;
        std::unique_ptr<Window> owned = gallery_on(raster, specimen.page, context.config);
        Window& window = *owned;
        // resize refuses (false) the size the raster already has.
        static_cast<void>(raster.resize(window.client_size(), context.config.scale));
        const Control::Ptr control = control_at(window, specimen.path);
        if (control == nullptr) continue;
        const Rect bounds = (*control).absolute_bounds();
        const Rect crop = cell_rect(window, *control);
        cell.applies = drive(window, control, state);
        if (!cell.applies) continue;
        capture(window, raster, crop, bounds, state, specimen, context, cell);
        owned.reset();
    }
}

// -- the contact sheet ---------------------------------------------------------------

// A BGRA premultiplied image the sheet is assembled in, `width` by `height`.
struct SheetImage final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;
};

void fill_white(SheetImage& image) {
    image.pixels.assign(static_cast<std::size_t>(image.width) * image.height * 4U, 255U);
}

void place(SheetImage& image, std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height,
           const std::uint8_t* source, std::size_t source_row_bytes) {
    const std::uint32_t copied_width = std::min(width, image.width > left ? image.width - left : 0U);
    const std::uint32_t copied_height = std::min(height, image.height > top ? image.height - top : 0U);
    const std::size_t image_row_bytes = static_cast<std::size_t>(image.width) * 4U;
    for (std::uint32_t row = 0U; row < copied_height; ++row) {
        std::uint8_t* const target = image.pixels.data() + image_row_bytes * (top + row) + left * 4U;
        std::memcpy(target, source + source_row_bytes * row, static_cast<std::size_t>(copied_width) * 4U);
    }
}

// Renders `text` into `labels` (a raster label_width by the row height) and
// places it at (left, top) of the sheet.
void place_label(SheetImage& image, render::SkiaRaster& labels, std::string_view first, std::string_view second,
                 std::uint32_t left, std::uint32_t top) {
    const Rect area{0.0, 0.0, label_width, cell_height};
    DamageRegion damage;
    damage.add(area);
    labels.begin_frame(damage);
    labels.fill_rect(area, Color::rgba(255, 255, 255));
    const FontSpec bold{FontRole::control, 12.0, 700, false};
    const FontSpec plain{FontRole::control, 11.0, 400, false};
    labels.draw_text_utf8(Point{4.0, 18.0}, first, bold, Color::rgba(0, 0, 0));
    labels.draw_text_utf8(Point{4.0, 36.0}, second, plain, Color::rgba(60, 60, 60));
    labels.end_frame();
    place(image, left, top, labels.pixel_width(), labels.pixel_height(),
          static_cast<const std::uint8_t*>(labels.pixels()), labels.row_bytes());
}

bool write_file(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    const bool written = static_cast<bool>(file);
    return written;
}

// Writes the rows [first, last) of `cells` as one PNG sheet.
bool write_sheet(const std::filesystem::path& path, const std::vector<Specimen>& specimens,
                 const std::vector<std::array<Cell, specimen_state_count>>& cells, std::size_t first,
                 std::size_t last, double scale, render::SkiaRaster& labels) {
    const std::uint32_t row_height = device(cell_height + gutter, scale);
    const std::uint32_t column_width = device(cell_width + gutter, scale);
    const std::uint32_t label_columns = device(label_width, scale);
    SheetImage image{};
    image.width = label_columns + column_width * static_cast<std::uint32_t>(specimen_state_count);
    image.height = row_height * static_cast<std::uint32_t>(last - first + 1U);
    fill_white(image);
    for (std::size_t column = 0; column < specimen_state_count; ++column) {
        const std::uint32_t left = label_columns + column_width * static_cast<std::uint32_t>(column);
        place_label(image, labels, state_names[column], std::string_view(), left, 0U);
    }
    for (std::size_t row = first; row < last; ++row) {
        const std::uint32_t top = row_height * static_cast<std::uint32_t>(row - first + 1U);
        place_label(image, labels, specimens[row].control_id, specimens[row].page, 0U, top);
        for (std::size_t column = 0; column < specimen_state_count; ++column) {
            const Cell& cell = cells[row][column];
            if (!cell.applies || cell.pixels.empty()) continue;
            const std::uint32_t left = label_columns + column_width * static_cast<std::uint32_t>(column);
            place(image, left, top, cell.width, cell.height, cell.pixels.data(),
                  static_cast<std::size_t>(cell.width) * 4U);
        }
    }
    image::PngEncodeInput input{};
    input.width = image.width;
    input.height = image.height;
    input.row_bytes = static_cast<std::uint64_t>(image.width) * 4U;
    input.pixels = std::as_bytes(std::span<const std::uint8_t>(image.pixels));
    input.order = image::PngChannelOrder::bgra;
    input.alpha = image::PngSourceAlpha::premultiplied;
    std::vector<std::byte> encoded{};
    if (!image::encode_png(input, encoded)) return false;
    const bool written = write_file(path, encoded);
    return written;
}

// -- the run -----------------------------------------------------------------------

struct RunOptions final {
    std::filesystem::path out;
    bool smoke{};
    bool repeat{};
    bool light_only{};
    // The theme --theme names, or null for the Gallery's own.
    std::shared_ptr<const Theme> theme{};
};

std::string config_name(SheetConfig config) {
    std::ostringstream text;
    text << presentation_name(config.presentation) << '-' << static_cast<int>(config.scale) << 'x';
    return text.str();
}

// The specimens, chosen in a light Gallery at scale 1 measured by `raster`,
// drawing `config`'s theme.
std::vector<Specimen> choose_specimens(render::SkiaRaster& raster, bool smoke, SheetConfig config) {
    std::vector<Specimen> specimens;
    for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
        std::unique_ptr<Window> owned = gallery_on(raster, page.id, config);
        std::vector<std::size_t> path;
        collect_specimens(*owned, (*owned).root(), page.id, path, specimens);
        if (smoke) break;
    }
    return specimens;
}

using Captured = std::vector<std::array<Cell, specimen_state_count>>;

void capture_config(const std::vector<Specimen>& specimens, CaptureContext& context, Captured& captured) {
    captured.assign(specimens.size(), std::array<Cell, specimen_state_count>{});
    for (std::size_t index = 0; index < specimens.size(); ++index) {
        capture_specimen(specimens[index], context, captured[index]);
    }
}

// The number of cells whose pixels differ between two captures.
std::size_t differing_cells(const Captured& first, const Captured& second) {
    std::size_t differing = 0U;
    for (std::size_t row = 0; row < first.size(); ++row) {
        for (std::size_t column = 0; column < specimen_state_count; ++column) {
            if (first[row][column].pixels != second[row][column].pixels) ++differing;
        }
    }
    return differing;
}

int run(const RunOptions& options) {
#if defined(_WIN32)
    const detail::SealedPageFunctions pages = host::windows_sealed_pages();
#else
    const detail::SealedPageFunctions pages = host::posix_sealed_pages();
#endif
    const std::shared_ptr<detail::BundledFontSet> fonts =
        detail::shared_bundled_font_set(std::filesystem::path(GUI_FORMS_SPECIMEN_FONT_DIR), pages);
    render::SkiaRaster chooser{};
    if (!chooser.resize(Size{1280.0, 820.0}, 1.0) || !chooser.register_bundled_fonts(fonts)) {
        std::cerr << "specimen sheet: the bundled fonts did not register\n";
        return 1;
    }
    SheetConfig chosen{};
    chosen.theme = &options.theme;
    const std::vector<Specimen> specimens = choose_specimens(chooser, options.smoke, chosen);
    std::vector<SheetConfig> configs;
    const std::array<Presentation, 3> presentations{Presentation::light, Presentation::dark,
                                                    Presentation::high_contrast};
    const std::array<double, 2> scales{1.0, 2.0};
    for (const Presentation presentation : presentations) {
        if (options.light_only && presentation != Presentation::light) continue;
        for (const double scale : scales) configs.push_back(SheetConfig{presentation, scale, &options.theme});
    }
    if (options.smoke) configs.resize(1U);
    std::vector<ContrastFinding> findings;
    std::ostringstream manifest;
    manifest << "config\tcontrol\tpage\tstate\tapplies\twidth\theight\thash\tsame_as_rest\tsmallest_contrast\n";
    std::size_t cell_count = 0U;
    std::size_t applied = 0U;
    std::size_t unchanged = 0U;
    std::size_t differing = 0U;
    for (const SheetConfig config : configs) {
        CaptureContext context{};
        context.fonts = &fonts;
        context.config = config;
        context.config_name = config_name(config);
        context.findings = &findings;
        Captured captured;
        capture_config(specimens, context, captured);
        if (options.smoke || options.repeat) {
            std::vector<ContrastFinding> again_findings;
            context.findings = &again_findings;
            Captured again;
            capture_config(specimens, context, again);
            differing += differing_cells(captured, again);
        }
        for (std::size_t row = 0; row < specimens.size(); ++row) {
            for (std::size_t column = 0; column < specimen_state_count; ++column) {
                const Cell& cell = captured[row][column];
                ++cell_count;
                if (cell.applies) ++applied;
                // A state that applies but paints exactly what rest paints.
                const bool same_as_rest = column != 0U && cell.applies && cell.pixels == captured[row][0].pixels;
                if (same_as_rest) ++unchanged;
                char hash[24]{};
                std::snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(cell.hash));
                manifest << context.config_name << '\t' << specimens[row].control_id << '\t' << specimens[row].page
                         << '\t' << state_names[column] << '\t' << (cell.applies ? 1 : 0) << '\t' << cell.width
                         << '\t' << cell.height << '\t' << hash << '\t' << (same_as_rest ? 1 : 0) << '\t' << cell.smallest_contrast << '\n';
            }
        }
        if (options.out.empty()) continue;
        render::SkiaRaster labels{};
        if (!labels.resize(Size{label_width, cell_height}, config.scale) ||
            !labels.register_bundled_fonts(fonts)) {
            return 1;
        }
        std::size_t sheet = 0U;
        for (std::size_t first = 0; first < specimens.size(); first += rows_per_sheet) {
            const std::size_t last = std::min(specimens.size(), first + rows_per_sheet);
            std::ostringstream name;
            name << "sheet-" << context.config_name << '-' << sheet << ".png";
            if (!write_sheet(options.out / name.str(), specimens, captured, first, last, config.scale, labels)) {
                std::cerr << "specimen sheet: could not write " << name.str() << '\n';
                return 1;
            }
            ++sheet;
        }
    }
    std::cout << "specimen sheet: " << specimens.size() << " controls, " << configs.size() << " configurations, "
              << cell_count << " cells (" << applied << " applicable, " << unchanged
              << " of the states beyond rest painting what rest paints), " << findings.size()
              << " text runs under their contrast threshold";
    if (options.smoke || options.repeat) std::cout << ", " << differing << " cells differ between two captures";
    std::cout << '\n';
    if (!options.out.empty()) {
        const std::string manifest_text = manifest.str();
        std::ofstream(options.out / "manifest.tsv", std::ios::binary) << manifest_text;
        std::ofstream contrast(options.out / "contrast.tsv", std::ios::binary);
        contrast << "config\tcontrol\tstate\ttext\tground\tratio\tthreshold\n";
        for (const ContrastFinding& finding : findings) {
            contrast << finding.config << '\t' << finding.control_id << '\t' << finding.state << '\t'
                     << colour_text(finding.text) << '\t' << colour_text(finding.ground) << '\t' << finding.ratio
                     << '\t' << finding.threshold << '\n';
        }
    }
    if (options.smoke && specimens.empty()) {
        std::cerr << "specimen sheet: the first page has no specimen\n";
        return 1;
    }
    const int result = differing == 0U ? 0 : 1;
    return result;
}

} // namespace

int main(int argument_count, char** arguments) {
    RunOptions options{};
    for (int index = 1; index < argument_count; ++index) {
        const std::string_view argument(arguments[index]);
        if (argument == "--smoke") {
            options.smoke = true;
        } else if (argument == "--repeat") {
            options.repeat = true;
        } else if (argument == "--light") {
            options.light_only = true;
        } else if (argument == "--theme" && index + 1 < argument_count && std::string_view(arguments[index + 1]) == "gui-forms") {
            options.theme = gui_forms_theme();
            ++index;
        } else if (argument == "--out" && index + 1 < argument_count) {
            options.out = std::filesystem::path(arguments[index + 1]);
            ++index;
        } else {
            std::cerr << "usage: gui_forms_specimen_sheet (--out <directory> [--repeat] [--theme gui-forms] [--light] | --smoke)\n";
            return 2;
        }
    }
    if (!options.smoke && options.out.empty()) {
        std::cerr << "usage: gui_forms_specimen_sheet (--out <directory> [--repeat] [--theme gui-forms] [--light] | --smoke)\n";
        return 2;
    }
    if (!options.out.empty()) std::filesystem::create_directories(options.out);
    const int result = run(options);
    return result;
}
