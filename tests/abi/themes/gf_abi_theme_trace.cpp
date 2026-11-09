// The stage C4 gate: a theme package loaded through the C ABI paints a
// fixture exactly as the same package compiled and applied through the C++
// API does. Each path paints into a painter that records every command it
// is given (geometry, colours, groups, and the bytes of every part raster),
// and the two traces must be equal line for line. The trace is first shown
// to depend on the theme (the built-in theme paints otherwise), so equality
// is not vacuous; a refused package must leave the ABI's trace unchanged
// (an atomic replace); and a value published through the ABI must reach the
// running window as Window::publish_value does in C++.
//
// The program links the ABI's objects and the C++ library statically, so
// both paths share one process: it reaches the ABI only through
// gf_abi_get_table, except for the window a run would show, which it makes
// with the run's own function (make_run_window) instead of opening a
// native window.

#include "gui_forms/c/gf_abi.h"

#include "../../../src/abi/stable/engine/abi_engine.hpp"
#include "../../../src/core/theme_language/package/theme_manifest.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures = failures + 1;
    }
}

void require_ok(gf_status status, std::string_view call) {
    if (status != GF_STATUS_OK) {
        throw std::runtime_error(std::string(call) + " failed with status " + std::to_string(status));
    }
}

// A 1 x 1 RGBA PNG, the package's one image resource.
constexpr std::array<std::uint8_t, 70> one_pixel_png{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x56, 0xc7, 0x2f, 0x0d, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

constexpr std::string_view published_name = "level";

// The fixture's sheet: a part the engine draws with a primitive whose
// amplitude reads the published value, one with the package's image, and
// stock controls the sheet restyles.
constexpr std::string_view fixture_sheet =
    "#trace-pad::part(face) { primitive: bulge; amplitude: clamp(0, var(--level), 1);\n"
    "  light-colour: rgb(255 240 200); shade-colour: steelblue }\n"
    "#trace-mark::part(face) { background-image: url(\"resource:mark\") }\n"
    "button::part(face) { background-color: #3366cc }\n";

std::string sha256_of(std::string_view bytes) {
    const std::string hex = theme_language::package_sha256_hex(theme_language::package_sha256(bytes));
    return hex;
}

// The fixture package as the C++ API takes it.
ThemePackage fixture_package(std::string_view sheet) {
    const std::string image(reinterpret_cast<const char*>(one_pixel_png.data()), one_pixel_png.size());
    ThemePackage package;
    package.manifest = "{\"format\": \"gui-forms-theme/1\", \"name\": \"abi-trace\", \"version\": \"1.0.0\", "
                       "\"engine\": \">=0.3\", \"sheet\": {\"path\": \"theme.theme.css\", \"sha256\": \"" +
                       sha256_of(fixture_sheet) +
                       "\"}, \"resources\": [{\"name\": \"mark\", \"path\": \"mark.png\", \"sha256\": \"" +
                       sha256_of(image) + "\"}], \"published_values\": [\"level\"]}";
    package.files.push_back(ThemePackageFile{"mark.png", image});
    package.files.push_back(ThemePackageFile{"theme.theme.css", std::string(sheet)});
    return package;
}

gf_utf8_view view_of(std::string_view text) {
    gf_utf8_view view{};
    view.data = text.data();
    view.size = text.size();
    return view;
}

gf_bytes_view bytes_of(std::string_view bytes) {
    gf_bytes_view view{};
    view.data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    view.size = bytes.size();
    return view;
}

// Loads package through the ABI: the C form borrows the C++ package's
// storage for the length of the call.
gf_theme_report load_through_abi(const gf_themes_table& themes, const ThemePackage& package) {
    std::vector<gf_theme_file> files;
    files.reserve(package.files.size());
    for (const ThemePackageFile& file : package.files) {
        gf_theme_file entry{};
        entry.path = view_of(file.path);
        entry.bytes = bytes_of(file.bytes);
        files.push_back(entry);
    }
    gf_theme_package given{};
    given.struct_size = sizeof(gf_theme_package);
    given.manifest = bytes_of(package.manifest);
    given.files = files.data();
    given.file_count = files.size();
    gf_theme_report report{};
    report.struct_size = sizeof(gf_theme_report);
    require_ok(themes.load(&given, &report), "themes.load");
    return report;
}

// Records every command, one line each, numbers to four places.
class TracePainter final : public Painter {
public:
    void save() override { add("save"); }
    void restore() override { add("restore"); }
    void translate(Point offset) override { add("translate " + point(offset)); }
    void clip_rect(Rect rect) override { add("clip " + rectangle(rect)); }
    void clip_rounded_rect(Rect rect, double radius) override {
        add("clip-rounded " + rectangle(rect) + " " + number(radius));
    }
    void begin_group(const PaintGroup& group) override {
        add("group " + rectangle(group.bounds) + " " + number(group.opacity) + " " +
            std::to_string(static_cast<unsigned>(group.composite)) + (group.isolated ? " isolated" : ""));
    }
    void end_group() override { add("end-group"); }
    void fill_rect(Rect rect, Color color) override { add("fill " + rectangle(rect) + " " + colour(color)); }
    void fill_rounded_rect(Rect rect, double radius, Color color) override {
        add("fill-rounded " + rectangle(rect) + " " + number(radius) + " " + colour(color));
    }
    void stroke_rect(Rect rect, Color color, double width) override {
        add("stroke " + rectangle(rect) + " " + colour(color) + " " + number(width));
    }
    void stroke_rounded_rect(Rect rect, double radius, Color color, double width) override {
        add("stroke-rounded " + rectangle(rect) + " " + number(radius) + " " + colour(color) + " " + number(width));
    }
    void fill_linear_gradient(Rect rect, Point start, Point end, std::span<const GradientStop> stops) override {
        add("linear " + rectangle(rect) + " " + point(start) + " " + point(end) + " " + gradient(stops));
    }
    void fill_radial_gradient(Rect rect, Point center, Size radii, std::span<const GradientStop> stops) override {
        add("radial " + rectangle(rect) + " " + point(center) + " " + number(radii.width) + "," +
            number(radii.height) + " " + gradient(stops));
    }
    void draw_box_shadow(Rect rect, double radius, Point offset, double blur, double spread, Color color) override {
        add("shadow " + rectangle(rect) + " " + number(radius) + " " + point(offset) + " " + number(blur) + " " +
            number(spread) + " " + colour(color));
    }
    void draw_line(Point from, Point to, Color color, double width) override {
        add("line " + point(from) + " " + point(to) + " " + colour(color) + " " + number(width));
    }
    void draw_text_utf8(Point origin, std::string_view text, FontSpec font, Color color) override {
        add("text " + point(origin) + " " + std::string(text) + " " + number(font.size) + " " + colour(color));
    }
    // Image identities differ between registries; the destination and
    // opacity do not.
    void draw_image(ImageId, Rect destination, double opacity) override {
        add("image " + rectangle(destination) + " " + number(opacity));
    }
    void draw_image_region(ImageId, Rect source, Rect destination, double opacity) override {
        add("image-region " + rectangle(source) + " " + rectangle(destination) + " " + number(opacity));
    }
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double opacity) override {
        add("raster " + point(origin) + " " + number(opacity) + " " + raster_digest(raster));
    }
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect destination,
                                 double opacity) override {
        add("raster-scaled " + rectangle(destination) + " " + number(opacity) + " " + raster_digest(raster));
    }

    std::vector<std::string> lines;

private:
    void add(std::string line) { lines.push_back(std::move(line)); }

    static std::string number(double value) {
        std::array<char, 48> text{};
        const int written = std::snprintf(text.data(), text.size(), "%.4f", value);
        const std::string result(text.data(), written > 0 ? static_cast<std::size_t>(written) : 0U);
        return result;
    }
    static std::string point(Point value) { return number(value.x) + "," + number(value.y); }
    static std::string rectangle(Rect value) {
        return number(value.x) + "," + number(value.y) + "," + number(value.width) + "," + number(value.height);
    }
    static std::string colour(Color value) {
        return std::to_string(value.red) + "." + std::to_string(value.green) + "." + std::to_string(value.blue) +
               "." + std::to_string(value.alpha);
    }
    static std::string gradient(std::span<const GradientStop> stops) {
        std::string text;
        for (const GradientStop& stop : stops) {
            text += number(stop.offset) + ":" + colour(stop.color) + ";";
        }
        return text;
    }
    // The raster's extent and an FNV-1a digest of its pixels.
    static std::string raster_digest(const std::shared_ptr<const PartRaster>& raster) {
        if (raster == nullptr) {
            return "null";
        }
        std::uint64_t digest = 14695981039346656037ULL;
        for (const std::uint8_t byte : (*raster).pixels()) {
            digest = (digest ^ byte) * 1099511628211ULL;
        }
        const DeviceExtent extent = (*raster).extent();
        return std::to_string(extent.width) + "x" + std::to_string(extent.height) + ":" + std::to_string(digest);
    }
};

constexpr Size fixture_size{240.0, 120.0};

// A control whose face the theme engine paints (paint_part), as the
// engine's own tests paint theirs.
class Pad final : public Control {
public:
    explicit Pad(StableId stable_id) : Control(std::move(stable_id)) {}

    void on_paint(Painter& painter, Rect) override {
        PartPaint part{};
        part.role = PartRole::face;
        part.family = ControlVisualRole::button;
        part.bounds = Rect{0.0, 0.0, 48.0, 32.0};
        part.key = PartKey{"face", {}, {}};
        paint_part(painter, *this, part);
    }
};

// The fixture: two engine-painted pads, a button and a check box on a panel.
Control::Ptr make_fixture() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("trace.root"));
    const std::shared_ptr<Button> button = make_control<Button>(StableId("trace.button"), "Apply");
    (*button).set_requested_bounds({12.0, 12.0, 120.0, 32.0});
    const std::shared_ptr<CheckBox> check = make_control<CheckBox>(StableId("trace.check"), "Wrap");
    (*check).set_requested_bounds({12.0, 56.0, 120.0, 24.0});
    (*check).checked = true;
    const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("trace-pad"));
    (*pad).set_requested_bounds({150.0, 12.0, 48.0, 32.0});
    const std::shared_ptr<Pad> mark = make_control<Pad>(StableId("trace-mark"));
    (*mark).set_requested_bounds({150.0, 56.0, 48.0, 32.0});
    (*root).add_child(button);
    (*root).add_child(check);
    (*root).add_child(pad);
    (*root).add_child(mark);
    return root;
}

std::vector<std::string> trace_of(Window& window) {
    TracePainter painter;
    window.perform_layout();
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, fixture_size.width, fixture_size.height}));
    return painter.lines;
}

// Compares two traces; with report, prints the first difference.
bool same_trace(const std::vector<std::string>& left, const std::vector<std::string>& right, std::string_view what,
                bool report) {
    const std::size_t shared = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t index = 0U; index < shared; ++index) {
        if (left[index] != right[index]) {
            if (!report) return false;
            std::cerr << what << ": line " << index + 1U << " differs\n  abi: " << left[index]
                      << "\n  c++: " << right[index] << '\n';
            return false;
        }
    }
    if (left.size() != right.size()) {
        if (!report) return false;
        std::cerr << what << ": " << left.size() << " lines against " << right.size() << '\n';
        return false;
    }
    return true;
}

// The C++ path: the package compiled through ApplicationResources against
// the fixture's own control types and applied with set_theme.
// The C++ path's trace of the fixture with `level` published. With
// `earlier`, the window first paints with `earlier` published and then
// publishes `level`, as a running window does: a second paint leaves out
// what the first one kept (the backplane plane, when the theme draws one).
std::vector<std::string> cpp_trace(const ThemePackage& package, double level,
                                   std::optional<double> earlier = std::nullopt) {
    const std::shared_ptr<ApplicationResources> resources = std::make_shared<ApplicationResources>();
    const ThemeLibrary library{};
    const Control::Ptr root = make_fixture();
    std::vector<ControlAnatomy> controls;
    const std::shared_ptr<Button> button = make_control<Button>(StableId("anatomy.button"));
    const std::shared_ptr<CheckBox> check = make_control<CheckBox>(StableId("anatomy.check"));
    controls.push_back((*button).anatomy());
    controls.push_back((*check).anatomy());
    const std::array<std::string, 1> published{std::string(published_name)};
    ThemeCompileOptions options;
    options.controls = controls;
    options.published_values = published;
    const ThemeCompileResult compiled = (*resources).compile_theme_package(package, library, options);
    require(compiled.succeeded(), "the C++ path compiles the fixture package");
    if (!compiled.succeeded()) {
        for (const ThemeCompileDiagnostic& finding : compiled.diagnostics()) {
            std::cerr << "  " << finding.code << " " << finding.file << ":" << finding.line << " " << finding.detail
                      << '\n';
        }
        return {};
    }
    (*resources).set_theme(compiled.theme());
    Window window(root, fixture_size, resources);
    if (earlier) {
        require(window.publish_value(published_name, *earlier), "the C++ path publishes the earlier value");
        static_cast<void>(trace_of(window));
    }
    require(window.publish_value(published_name, level), "the C++ path publishes the value");
    std::vector<std::string> trace = trace_of(window);
    return trace;
}

// Where the traces are written when the program is given a directory, so
// a failure can be compared line by line; empty for none.
std::string dump_directory;

void dump(const std::vector<std::string>& lines, std::string_view name) {
    if (dump_directory.empty()) {
        return;
    }
    std::ofstream file(dump_directory + "/" + std::string(name) + ".txt", std::ios::binary);
    for (const std::string& line : lines) {
        file << line << '\n';
    }
}

void run_gate() {
    gf_themes_table themes{};
    require_ok(gf_abi_get_table(GF_TABLE_THEMES, GF_ABI_MAJOR, GF_ABI_MINOR, &themes, sizeof(themes)),
               "gf_abi_get_table(themes)");
    const ThemePackage package = fixture_package(fixture_sheet);

    // The built-in theme, through the run's own window.
    const Control::Ptr builtin_root = make_fixture();
    std::unique_ptr<Window> builtin_window = abi1::make_run_window(builtin_root, fixture_size);
    const std::vector<std::string> builtin = trace_of(*builtin_window);
    builtin_window.reset();

    // The fixture theme, loaded through the ABI with the value published first.
    require_ok(themes.publish_value(view_of(published_name), 0.5), "themes.publish_value");
    const gf_theme_report loaded = load_through_abi(themes, package);
    require(loaded.outcome == GF_THEME_OUTCOME_APPLIED, "the ABI applies the fixture package");
    const Control::Ptr abi_root = make_fixture();
    std::unique_ptr<Window> abi_window = abi1::make_run_window(abi_root, fixture_size);
    const std::vector<std::string> through_abi = trace_of(*abi_window);
    const std::vector<std::string> through_cpp = cpp_trace(package, 0.5);
    dump(builtin, "trace-builtin");
    dump(through_abi, "trace-abi");
    dump(through_cpp, "trace-cpp");
    require(!through_abi.empty(), "the fixture paints");
    require(!same_trace(through_abi, builtin, "fixture against built-in", false),
            "the trace depends on the theme");
    require(same_trace(through_abi, through_cpp, "fixture theme", true),
            "the theme loaded through the ABI paints as the C++ path does");

    // A refused package changes nothing: the next window paints the same.
    const ThemePackage broken = fixture_package(std::string(fixture_sheet) + "button { opacity: 2 }\n");
    const gf_theme_report refused = load_through_abi(themes, broken);
    require(refused.outcome == GF_THEME_OUTCOME_REFUSED && refused.diagnostic_count > 0U,
            "a package whose sheet no longer matches its manifest is refused with findings");
    const Control::Ptr after_root = make_fixture();
    std::unique_ptr<Window> after_window = abi1::make_run_window(after_root, fixture_size);
    require(same_trace(trace_of(*after_window), through_abi, "after a refusal", true),
            "a refused package leaves the theme as it was");
    after_window.reset();

    // A value published during a run reaches the running window.
    abi1::engine().attach_window(*abi_window);
    require_ok(themes.publish_value(view_of(published_name), 0.25), "themes.publish_value (running)");
    const std::vector<std::string> republished = trace_of(*abi_window);
    abi1::engine().detach_window();
    require(same_trace(republished, cpp_trace(package, 0.25, 0.5), "after publishing 0.25", true),
            "a value published through the ABI repaints as Window::publish_value does");
    require(!same_trace(republished, through_abi, "0.25 against 0.5", false),
            "the trace depends on the published value");
    abi_window.reset();
    require_ok(themes.reset(), "themes.reset");
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        dump_directory = argv[1];
    }
    try {
        run_gate();
    } catch (const std::exception& error) {
        std::cerr << "gf_abi_theme_trace: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << "gf_abi_theme_trace: " << failures << " failed\n";
        return 1;
    }
    std::cout << "gf_abi_theme_trace: the ABI's theme paints as the C++ path's\n";
    return 0;
}
