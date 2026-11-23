// The documentation's control previews (D2, owner decision 2026-11-21):
// one PNG per stock control, drawn headless from the stock specimen
// catalogue (gui_forms/controls/stock_specimens.hpp) through an
// OffscreenRaster, in the windows-professional theme, light, at a scale
// (2 unless named) and with motion reduced, so the picture is a still that
// no screen, desktop or personal detail can reach.
//
//   gui_forms_preview_capture --out <directory> [--scale <f>] [--margin <units>]
//                             [--only <control id>]...
//
// Each row of the catalogue is laid out at its own size in a window of the
// program's application (ThemeBase::windows_professional), presented,
// painted until no procedural pattern or finish tile waits, and its `frame`
// captured with the window's backplane and a margin around it, then written
// as <directory>/<control id>.png through GUI.Forms' PNG encoder
// (encode_window_capture_png). A row with a blank_reason (an abstract base
// that paints nothing of its own) is skipped. tools/capture_previews.py
// runs this program, makes palette images where they fit, enforces the
// size budget and lays the set out by commit; run it from there.
//
// Prints one line per row to stdout:
//   {"preview":"controls/<id>","saved":true|false,"path":"..."} or
//   {"preview":"controls/<id>","skipped":"<blank reason>"}
// and returns 0 when every row it took was written, 1 when one was not (the
// reason goes to stderr), 2 for a usage error or no raster.

#include "gui_forms/application.hpp"
#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/controls/stock_specimens.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "gui_forms/window.hpp"
#include "../../src/platform/library_files.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <stxlib/optional.hpp>

namespace {

using gui_forms::Application;
using gui_forms::ApplicationConfig;
using gui_forms::ApplicationCreateResult;
using gui_forms::ColourScheme;
using gui_forms::FrameClock;
using gui_forms::FrameTime;
using gui_forms::OffscreenAttachment;
using gui_forms::OffscreenCaptureOptions;
using gui_forms::OffscreenRaster;
using gui_forms::OffscreenRasterCreateResult;
using gui_forms::PaintFramebuffer;
using gui_forms::PaintReceipt;
using gui_forms::PresentationOverrides;
using gui_forms::Rect;
using gui_forms::Size;
using gui_forms::StableId;
using gui_forms::StockSpecimen;
using gui_forms::StockSpecimenInstance;
using gui_forms::ThemeBase;
using gui_forms::Window;
using gui_forms::WindowCapture;

// Idle frames a specimen may take to settle, as the Gallery's headless
// capture allows (demo/gallery_headless_capture.hpp).
constexpr std::size_t maximum_settle_frames = 64U;

struct CaptureOptions final {
    std::filesystem::path out{};
    double scale{2.0};
    // Logical units around the frame: focus rings and shadows drawn outside it.
    double margin{8.0};
    std::vector<std::string> only{};
};

void print_usage() {
    std::fprintf(stderr, "usage: gui_forms_preview_capture --out <directory> [--scale <f>] [--margin <units>] "
                         "[--only <control id>]...\n");
}

// The value after `argv[index]`, or null when there is none.
const char* option_value(int argc, char** argv, int index) {
    const int next = index + 1;
    if (next >= argc) return nullptr;
    return argv[next];
}

// Parses the command line; none (after printing usage) when it is refused.
std::optional<CaptureOptions> parse_options(int argc, char** argv) {
    CaptureOptions options{};
    for (int index = 1; index < argc; index = index + 2) {
        const std::string_view name(argv[index]);
        const char* const value = option_value(argc, argv, index);
        if (value == nullptr) {
            print_usage();
            return std::nullopt;
        }
        if (name == "--out") {
            options.out = std::filesystem::path(value);
        } else if (name == "--scale") {
            options.scale = std::strtod(value, nullptr);
        } else if (name == "--margin") {
            options.margin = std::strtod(value, nullptr);
        } else if (name == "--only") {
            options.only.emplace_back(value);
        } else {
            print_usage();
            return std::nullopt;
        }
    }
    const bool scale_valid = options.scale > 0.0 && options.scale <= 8.0;
    const bool margin_valid = options.margin >= 0.0 && options.margin <= 64.0;
    if (options.out.empty() || !scale_valid || !margin_valid) {
        print_usage();
        return std::nullopt;
    }
    return options;
}

// `text` with '"' and '\\' escaped and control characters dropped, for a JSON string.
std::string json_escaped(std::string_view text) {
    std::string escaped{};
    escaped.reserve(text.size());
    for (const char character : text) {
        if (character == '"' || character == '\\') {
            escaped.push_back('\\');
            escaped.push_back(character);
        } else if (static_cast<unsigned char>(character) >= 0x20U) {
            escaped.push_back(character);
        }
    }
    return escaped;
}

void print_saved_line(std::string_view control_id, bool saved, const std::filesystem::path& path) {
    const std::string id = json_escaped(control_id);
    const std::string where = json_escaped(path.generic_string());
    std::fprintf(stdout, "{\"preview\":\"controls/%s\",\"saved\":%s,\"path\":\"%s\"}\n", id.c_str(),
                 saved ? "true" : "false", where.c_str());
    std::fflush(stdout);
}

void print_skipped_line(std::string_view control_id, std::string_view reason) {
    const std::string id = json_escaped(control_id);
    const std::string why = json_escaped(reason);
    std::fprintf(stdout, "{\"preview\":\"controls/%s\",\"skipped\":\"%s\"}\n", id.c_str(), why.c_str());
    std::fflush(stdout);
}

// A stable ID for a specimen of `control_id`: anatomy ids join words with
// '_', which stable IDs refuse, so they become '-'.
StableId specimen_id(std::string_view control_id) {
    std::string segment(control_id);
    std::replace(segment.begin(), segment.end(), '_', '-');
    StableId id("preview." + segment);
    return id;
}

bool is_selected(const CaptureOptions& options, std::string_view control_id) {
    if (options.only.empty()) return true;
    const std::vector<std::string>::const_iterator found =
        std::find(options.only.begin(), options.only.end(), control_id);
    const bool selected = found != options.only.end();
    return selected;
}

// Paints the whole window into `framebuffer`, as a host's frame does. False
// when the frame could not begin or the paint was declined.
bool paint_frame(Window& window, PaintFramebuffer& framebuffer) {
    window.perform_layout();
    static_cast<void>(window.take_damage());
    const Rect whole{0.0, 0.0, window.client_size().width, window.client_size().height};
    if (!framebuffer.begin(window.image_resources(), whole)) return false;
    const stxlib::Optional<PaintReceipt> receipt = window.paint(framebuffer.painter(), whole);
    framebuffer.end();
    const bool painted = receipt.present();
    return painted;
}

bool engine_work_waiting(const Window& window) {
    const bool waiting = window.engine_parts().patterns_waiting() || window.engine_parts().finish_tiles_waiting();
    return waiting;
}

// Writes `bytes` to `path`, replacing the file. False when it cannot be
// opened or written whole; a partial file may then be left behind.
bool write_file(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    gui_forms::platform::OutputFile file = gui_forms::platform::OutputFile::create(path);
    if (!file.is_open()) return false;
    static_cast<void>(file.write(std::span<const std::byte>(bytes)));
    const bool written = file.close();
    return written;
}

// One row: its window, settled and captured. Returns whether the file was
// written; the reason for a refusal goes to stderr.
bool capture_specimen(const StockSpecimen& row, OffscreenRaster& raster, const CaptureOptions& options,
                      const std::filesystem::path& path) {
    const StockSpecimenInstance instance = gui_forms::make_stock_specimen(row, specimen_id(row.control_id));
    // The window joins the program's application and takes its theme.
    Window window(instance.root, row.size);
    window.set_active(true);
    PresentationOverrides still{};
    still.colour_scheme.set(ColourScheme::light);
    still.reduced_motion.set(true);
    still.ambient_motion.set(false);
    window.set_presentation_overrides(still);
    // Declared after the window it borrows, so it ends first.
    OffscreenAttachment attached(raster, window);
    window.set_scale(options.scale);
    window.perform_layout();
    gui_forms::present_stock_specimen(row, instance, window);
    const std::unique_ptr<PaintFramebuffer> framebuffer = window.create_framebuffer(window.client_size(), options.scale);
    if (framebuffer == nullptr) {
        std::fprintf(stderr, "%.*s: the raster made no framebuffer\n", static_cast<int>(row.control_id.size()),
                     row.control_id.data());
        return false;
    }
    FrameTime now = FrameClock::now();
    std::size_t frame = 0U;
    bool painted = paint_frame(window, *framebuffer);
    while (painted && frame < maximum_settle_frames && engine_work_waiting(window)) {
        now += std::chrono::milliseconds(16);
        static_cast<void>(window.poll_frame_schedule(now));
        painted = paint_frame(window, *framebuffer);
        frame = frame + 1U;
    }
    if (engine_work_waiting(window)) {
        std::fprintf(stderr, "%.*s: patterns or finish tiles still waiting after %zu frames\n",
                     static_cast<int>(row.control_id.size()), row.control_id.data(), frame);
    }
    OffscreenCaptureOptions capture_options{};
    capture_options.scale = options.scale;
    capture_options.colour_scheme.set(ColourScheme::light);
    capture_options.margin = options.margin;
    capture_options.include_backplane = true;
    capture_options.include_overlays = true;
    const std::optional<WindowCapture> capture = attached.capture_control(instance.frame, capture_options);
    std::vector<std::byte> png{};
    bool saved = false;
    if (!capture.has_value()) {
        std::fprintf(stderr, "%.*s: the capture was refused\n", static_cast<int>(row.control_id.size()),
                     row.control_id.data());
    } else if (!gui_forms::encode_window_capture_png(*capture, png)) {
        std::fprintf(stderr, "%.*s: the PNG encoding was refused\n", static_cast<int>(row.control_id.size()),
                     row.control_id.data());
    } else if (!write_file(path, png)) {
        std::fprintf(stderr, "cannot write %s\n", path.string().c_str());
    } else {
        saved = true;
    }
    return saved;
}

std::unique_ptr<Application> create_preview_application() {
    gui_forms::install_built_in_effect_registry();
    ApplicationConfig config{};
    config.theme.base = ThemeBase::windows_professional;
    config.theme.effects = &gui_forms::built_in_effect_registry();
    ApplicationCreateResult created = Application::create(std::move(config));
    std::unique_ptr<Application> application = std::move(created.application);
    return application;
}

int run(const CaptureOptions& options) {
    if (!gui_forms::platform::create_directories(options.out)) {
        std::fprintf(stderr, "cannot make %s\n", options.out.string().c_str());
        return 2;
    }
    // The application before any window: each specimen window joins it.
    const std::unique_ptr<Application> application = create_preview_application();
    if (application == nullptr) {
        std::fprintf(stderr, "the preview application was refused (windows-professional theme)\n");
        return 2;
    }
    OffscreenRasterCreateResult made = OffscreenRaster::create();
    if (!made.succeeded()) {
        const std::string reason = made.detail();
        std::fprintf(stderr, "no raster: %s\n", reason.c_str());
        return 2;
    }
    // Declared before every attachment, which borrow it and end first.
    const std::unique_ptr<OffscreenRaster> raster = made.take_raster();
    const std::span<const StockSpecimen> rows = gui_forms::stock_specimens();
    std::size_t taken = 0U;
    std::size_t failed = 0U;
    for (const StockSpecimen& row : rows) {
        if (!is_selected(options, row.control_id)) continue;
        taken = taken + 1U;
        if (!row.blank_reason.empty()) {
            print_skipped_line(row.control_id, row.blank_reason);
            continue;
        }
        const std::filesystem::path path = options.out / (std::string(row.control_id) + ".png");
        bool saved = false;
        try {
            saved = capture_specimen(row, *raster, options, path);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "%.*s: %s\n", static_cast<int>(row.control_id.size()), row.control_id.data(),
                         error.what());
        }
        print_saved_line(row.control_id, saved, path);
        if (!saved) failed = failed + 1U;
    }
    if (taken < options.only.size()) {
        std::fprintf(stderr, "an --only id names no row of the stock specimen catalogue\n");
        failed = failed + 1U;
    }
    const int exit_code = failed == 0U ? 0 : 1;
    return exit_code;
}

} // namespace

int main(int argc, char** argv) {
    const std::optional<CaptureOptions> options = parse_options(argc, argv);
    if (!options) return 2;
    const int exit_code = run(*options);
    return exit_code;
}
