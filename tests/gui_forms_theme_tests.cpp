// The default theme `gui-forms` (stage T4, phase 3): the stock anatomy
// table, the light sheet compiled at first use with no diagnostics and
// within its work bound, the sheet's byte-preserving round trip (T3 P11
// item 5), and the light contrast report passing every gated pair (design
// language 5.7). With GUI_FORMS_CONTRAST_REPORT=<path> it writes the
// theme's light report as tab-separated rows there, for the stage record.

#include "../src/core/theme/contrast/theme_contrast.hpp"
#include "../src/core/theme/dark_variant/dark_variant.hpp"
#include "../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../src/core/theme_language/tokenizer/theme_tokenizer.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/theme.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;
using gui_forms::detail::ContrastCounts;
using gui_forms::detail::ContrastRow;
using gui_forms::detail::ContrastVerdict;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool anatomy_named(std::span<const ControlAnatomy> anatomies, std::string_view id) {
    for (const ControlAnatomy& anatomy : anatomies) {
        if (anatomy.control_id == id) return true;
    }
    return false;
}

bool id_less(const ControlAnatomy& left, const ControlAnatomy& right) noexcept {
    return left.control_id < right.control_id;
}

bool id_equal(const ControlAnatomy& left, const ControlAnatomy& right) noexcept {
    return left.control_id == right.control_id;
}

void stock_anatomies() {
    const std::span<const ControlAnatomy> anatomies = stock_control_anatomies();
    require(anatomies.size() >= 90U, "the table holds every stock control type");
    require(anatomy_named(anatomies, "button"), "the table holds Button");
    require(anatomy_named(anatomies, "tab_control"), "the table holds TabControl");
    std::vector<ControlAnatomy> sorted(anatomies.begin(), anatomies.end());
    std::sort(sorted.begin(), sorted.end(), id_less);
    require(std::adjacent_find(sorted.begin(), sorted.end(), id_equal) == sorted.end(),
            "each control type appears once");
    for (const ControlAnatomy& anatomy : anatomies) {
        require(!anatomy.parts.empty(), "each stock control declares its parts");
    }
}

// The work bound (T3 P11 1a): at most 4 x maximum_variable_combinations
// operations per byte plus 4,096; the light sheet is far inside it, so the
// stated bound here is 64 operations per byte.
constexpr std::uint64_t operations_per_byte_bound = 64U;

void compiled_at_first_use() {
    const GuiFormsThemeBuild& build = gui_forms_theme_build();
    for (const ThemeCompileDiagnostic& diagnostic : build.diagnostics) {
        std::cerr << diagnostic.code << ' ' << diagnostic.name << ' ' << diagnostic.line << ':' << diagnostic.column
                  << ' ' << diagnostic.rule << ' ' << diagnostic.property << ' ' << diagnostic.detail << '\n';
    }
    require(build.diagnostics.empty(), "the light sheet compiles with no diagnostics");
    require(build.theme != nullptr, "the build makes a theme");
    require((*build.theme).id() == "gui-forms", "the theme's id is gui-forms (T4-2)");
    require(!(*build.theme).definition().rules.rules.empty(), "the theme holds the sheet's rules");
    require(&gui_forms_theme_build() == &build, "the theme is built once");
    require(gui_forms_theme() == build.theme, "gui_forms_theme() is the build's theme");
    const std::uint64_t bytes = gui_forms_light_sheet().size();
    require(build.compile_operations > 0U, "the compiler's work is counted");
    require(build.compile_operations <= bytes * operations_per_byte_bound, "the compile stays within its work bound");
    const double milliseconds = std::chrono::duration<double, std::milli>(build.build_time).count();
    char line[160]{};
    std::snprintf(line, sizeof(line), "gui-forms build: %llu sheet bytes, %llu operations, %.3f ms\n",
                  static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(build.compile_operations),
                  milliseconds);
    std::cout << line;
}

void round_trip() {
    const std::string_view text = gui_forms_light_sheet();
    const theme_language::ThemeSource source{.file = "gui-forms/light.css", .text = text};
    theme_language::ThemeTokenBuffer buffer{};
    theme_language::LineStartTable lines{};
    theme_language::ThemeDiagnosticList diagnostics{};
    static_cast<void>(theme_language::tokenize_theme_sheet(source, buffer, lines, diagnostics));
    theme_language::ThemeSyntaxTree tree{};
    const theme_language::ParseOutcome parsed =
        theme_language::parse_theme_sheet(source, buffer, lines, tree, diagnostics);
    require(parsed == theme_language::ParseOutcome::tree_complete, "the light sheet parses completely");
    std::string written{};
    const theme_language::WriteOutcome write = theme_language::write_theme_sheet(tree, buffer, text, written);
    require(write == theme_language::WriteOutcome::written, "the light sheet's tree nests");
    require(written == text, "the light sheet round-trips to identical bytes");
}

void write_report(const std::vector<ContrastRow>& rows, const char* path) {
    std::ofstream out(path, std::ios::binary);
    require(static_cast<bool>(out), "the report file opens");
    out << "presentation\tfamily\tstate\tselected\tpair\tforeground\tbackground\tratio\tminimum\tverdict\n";
    for (const ContrastRow& row : rows) {
        char colours[40]{};
        std::snprintf(colours, sizeof(colours), "#%02X%02X%02X\t#%02X%02X%02X\t%.2f", row.foreground.red,
                      row.foreground.green, row.foreground.blue, row.background.red, row.background.green,
                      row.background.blue, row.ratio);
        const char* const presentation = row.high_contrast ? "high_contrast" : row.dark ? "dark" : "ordinary";
        out << presentation << '\t' << control_visual_role_name(row.family)
            << '\t' << detail::control_surface_state_name(row.state) << '\t' << (row.selected ? "yes" : "no") << '\t'
            << detail::contrast_pair_name(row.pair) << '\t' << colours << '\t' << row.minimum << '\t'
            << detail::contrast_verdict_name(row.verdict) << '\n';
    }
}

// Prints each failing row of `rows` and returns how many there are.
std::size_t print_failures(const std::vector<ContrastRow>& rows) {
    std::size_t failed = 0U;
    for (const ContrastRow& row : rows) {
        if (row.verdict != ContrastVerdict::fail) continue;
        ++failed;
        char line[200]{};
        std::snprintf(line, sizeof(line), "fails: %s %s %s %s #%02X%02X%02X on #%02X%02X%02X %.2f < %.1f\n",
                      std::string(control_visual_role_name(row.family)).c_str(),
                      std::string(detail::control_surface_state_name(row.state)).c_str(),
                      row.selected ? "selected" : "-", std::string(detail::contrast_pair_name(row.pair)).c_str(),
                      row.foreground.red, row.foreground.green, row.foreground.blue, row.background.red,
                      row.background.green, row.background.blue, row.ratio, row.minimum);
        std::cerr << line;
    }
    return failed;
}

// The rows of one presentation.
std::vector<ContrastRow> presentation_rows(const std::vector<ContrastRow>& rows, bool high_contrast, bool dark) {
    std::vector<ContrastRow> result{};
    for (const ContrastRow& row : rows) {
        if (row.high_contrast == high_contrast && row.dark == dark) result.push_back(row);
    }
    return result;
}

// Light (the gate of phase 3) and dark (phase 5): every gated pair meets
// its minimum.
void light_contrast() {
    std::vector<ContrastRow> rows{};
    detail::measure_theme_contrast(*gui_forms_theme(), rows);
    const std::vector<ContrastRow> light = presentation_rows(rows, false, false);
    const std::size_t failed = print_failures(light);
    const std::vector<ContrastRow> dark = presentation_rows(rows, false, true);
    require(!dark.empty(), "the derived dark presentation is measured");
    const ContrastCounts dark_counts = detail::count_contrast_verdicts(dark);
    std::cout << "gui-forms dark contrast: " << dark_counts.passed << " pass, " << dark_counts.failed << " fail, "
              << dark_counts.not_measured << " not measured\n";
    const std::size_t dark_failed = print_failures(dark);
    const ContrastCounts counts = detail::count_contrast_verdicts(light);
    std::cout << "gui-forms light contrast: " << counts.passed << " pass, " << counts.failed << " fail, "
              << counts.not_measured << " not measured\n";
    const char* const path = std::getenv("GUI_FORMS_CONTRAST_REPORT");
    if (path != nullptr && path[0] != '\0') write_report(rows, path);
    require(failed == 0U, "every gated light pair meets its minimum");
    require(dark_failed == 0U, "every gated dark pair meets its minimum");
}

// The dark records (phase 5): gui-forms derives them, so a dark context
// resolves colours unlike the light ones with the same geometry; high
// contrast wins over dark; a theme without a dark variant resolves its
// ordinary records in a dark context.
void dark_records() {
    const std::shared_ptr<const Theme> theme = gui_forms_theme();
    ControlVisualContext light{};
    ControlVisualContext dark{};
    dark.dark = true;
    const ControlVisualRecipe& light_button = (*theme).resolve(ControlVisualRole::button, light);
    const ControlVisualRecipe& dark_button = (*theme).resolve(ControlVisualRole::button, dark);
    require(!(light_button.text == dark_button.text), "a dark button's text is derived");
    require(light_button.material.corner_radius == dark_button.material.corner_radius,
            "a dark button keeps its geometry");
    require(detail::relative_luminance(dark_button.text) > detail::relative_luminance(light_button.text),
            "dark text is lighter than light text");
    ControlVisualContext forced = dark;
    forced.high_contrast = true;
    ControlVisualContext forced_light{};
    forced_light.high_contrast = true;
    require((*theme).resolve(ControlVisualRole::button, forced) ==
                (*theme).resolve(ControlVisualRole::button, forced_light),
            "high contrast wins over dark");
    const std::shared_ptr<const Theme> professional = Theme::create(windows_professional_theme_definition());
    require((*professional).resolve(ControlVisualRole::button, dark) ==
                (*professional).resolve(ControlVisualRole::button, light),
            "a theme without a dark variant keeps its ordinary records");
}

// A control that paints nothing and presents itself as a ToolStrip, whose
// face the light sheet styles (tool-strip::part(face)); its parts are
// planned and painted by the engine.
constexpr ControlAnatomy probe_anatomy{
    .control_id = "tool_strip",
    .parts = {},
    .states = {},
    .custom_states = {},
};

class Probe final : public Control {
public:
    explicit Probe(StableId stable_id) : Control(std::move(stable_id)) {}
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override { return probe_anatomy; }
};

// A painter that keeps the part rasters drawn and draws nothing.
class RasterWatch final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point, double) override {
        rasters.push_back(raster);
    }
    std::vector<std::shared_ptr<const PartRaster>> rasters;
};

// The coverage-weighted mean of the green channel (premultiplied BGRA8),
// over the rasters `painter` kept, with their total coverage; a stand-in
// for lightness that a darkened material lowers.
struct RasterMean final {
    double green{};
    double coverage{};
};

RasterMean raster_mean(const RasterWatch& painter) {
    RasterMean result{};
    double green = 0.0;
    for (const std::shared_ptr<const PartRaster>& raster : painter.rasters) {
        const std::span<const std::uint8_t> pixels = (*raster).pixels();
        const std::size_t width = (*raster).extent().width;
        const std::size_t height = (*raster).extent().height;
        for (std::size_t row = 0U; row < height; ++row) {
            const std::size_t start = row * (*raster).row_bytes();
            for (std::size_t column = 0U; column < width; ++column) {
                const std::size_t at = start + column * 4U;
                green = green + static_cast<double>(pixels[at + 1U]);
                result.coverage = result.coverage + static_cast<double>(pixels[at + 3U]);
            }
        }
    }
    if (result.coverage > 0.0) result.green = green / result.coverage;
    return result;
}

void set_scheme(Window& window, ColourScheme scheme) {
    PresentationSettings settings = window.presentation_settings();
    settings.colour_scheme = scheme;
    window.set_presentation_settings(settings);
}

// T4 phase 5c: in a dark window the engine darkens, by rule, what the
// sheet writes in light terms. Each part of the sheet drawn by a primitive
// over its recipe's material with fields the rules set is painted light and
// dark: the dark raster is darker, and its colour parameters are the rule's
// (detail::dark_parameter_colour) of the light ones.
void sheet_darkened() {
    const std::shared_ptr<Probe> root = make_control<Probe>(StableId("root"));
    const std::shared_ptr<Probe> probe = make_control<Probe>(StableId("probe"));
    (*probe).set_requested_bounds(Rect{10.0, 10.0, 120.0, 40.0});
    (*root).add_child(probe);
    Window window(root, Size{200.0, 100.0});
    window.set_theme(gui_forms_theme());
    window.perform_layout();
    const ThemeEngine& engine = window.theme().engine();
    std::size_t parts = 0U;
    std::size_t parameters = 0U;
    for (std::size_t family = 0U; family < control_visual_role_count; ++family) {
        for (std::size_t role = 0U; role < part_role_count; ++role) {
            PartPaint part{};
            part.family = static_cast<ControlVisualRole>(family);
            part.role = static_cast<PartRole>(role);
            part.bounds = Rect{0.0, 0.0, 80.0, 24.0};
            part.key = PartKey{part_role_name(part.role), {}, {}};
            set_scheme(window, ColourScheme::light);
            const ThemeEngine::PartPlan light = engine.plan(*probe, part);
            if (light.drawing != ThemeEngine::PartDrawing::primitive || light.resolution.primitive == nullptr) {
                continue;
            }
            RasterWatch light_painter{};
            engine.paint(light_painter, *probe, part);
            set_scheme(window, ColourScheme::dark);
            const ThemeEngine::PartPlan dark = engine.plan(*probe, part);
            require(dark.resolution.primitive == light.resolution.primitive, "a dark window plans the same primitive");
            const PrimitiveDescriptor& described = (*light.resolution.primitive).descriptor();
            for (std::size_t index = 0U; index < light.resolution.parameters.count; ++index) {
                if (index >= described.parameters.size()) break;
                if (described.parameters[index].kind != PrimitiveParameterKind::colour) continue;
                const Color expected = detail::dark_parameter_colour(light.resolution.parameters.values[index].colour,
                                                                     light.resolution.parameter_origins[index],
                                                                     part.family);
                require(dark.resolution.parameters.values[index].colour == expected,
                        "a colour parameter darkens by the rule");
                parameters = parameters + 1U;
            }
            if (!light.resolution.materials_authored) continue;
            RasterWatch dark_painter{};
            engine.paint(dark_painter, *probe, part);
            const RasterMean lit = raster_mean(light_painter);
            const RasterMean darkened = raster_mean(dark_painter);
            if (lit.coverage <= 0.0) continue;
            require(darkened.coverage > 0.0 && darkened.green < lit.green, "an authored material darkens by the rule");
            parts = parts + 1U;
        }
    }
    set_scheme(window, ColourScheme::light);
    std::cout << "gui-forms darkened sheet: " << parts << " authored materials, " << parameters
              << " colour parameters\n";
    require(parts > 0U, "the sheet has authored materials to darken");
}

} // namespace

int main() {
    stock_anatomies();
    compiled_at_first_use();
    round_trip();
    light_contrast();
    dark_records();
    sheet_darkened();
    std::cout << "gui-forms theme: anatomies, first-use compile, round trip, light and dark contrast, dark records and the darkened sheet passed\n";
    return 0;
}
