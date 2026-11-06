// Exercise the actual Windows measurement and retained-button rendering path.
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/window.hpp"
#include "../src/host/windows/application/windows_host.cpp"
#include "text_identity_scene.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace gui_forms;

static void require_text(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

// The bundled faces, loaded as the host loads them (ADR-004).
static std::shared_ptr<gui_forms::detail::BundledFontSet> fixture_fonts;

static void check_caption(const std::string& text, double scale, std::uint16_t weight) {
    host::WindowsRaster painter;
    require_text(painter.resize({240, 32}, scale), "DIB allocation failed");
    painter.set_bundled_fonts(fixture_fonts);
    const FontSpec font{FontRole::control, 14, weight, false, 0.05};
    const ResolvedTextLayout layout = painter.painter().resolve_text_layout_utf8(text, font);
    std::cout << text << " scale=" << scale << " weight=" << weight
              << " width=" << layout.logical_size.width << " status=" << int(layout.status) << std::endl;
    require_text(layout.status == TextResolutionStatus::exact, "Fallback-only caption rejected");
    require_text(layout.logical_size.width > 0 && layout.logical_size.height > 0,
                 "Caption has empty layout");
    require_text(!layout.primary_family.empty() && !layout.runs.empty(), "Font provenance missing");
    const Size direct = painter.painter().measure_text_utf8(text, font);
    require_text(std::abs(layout.logical_size.width - direct.width) < 0.01,
                 "Retained and direct widths differ");
    const std::shared_ptr<Button> button = make_control<Button>(StableId("caption"), text);
    (*button).font = font;
    (*button).use_mnemonic = false;
    BasicControlStyle style = (*button).style();
    style.text = Color::rgba(255, 0, 255);
    (*button).set_style(style);
    Window window(button, {240, 32});
    window.set_text_metrics_provider(&painter.painter());
    window.perform_layout();
    painter.begin_frame();
    require_text(window.paint(painter.painter()).has_value(), "Retained caption did not paint");
    require_text(painter.save_bmp(L"windows-text-test.bmp"), "Capture failed");
    if (text == "Главная" && scale == 1.5 && weight == 700) {
        require_text(painter.save_bmp(L"windows-russian-tab.bmp"), "Russian evidence capture failed");
    }
    std::ifstream stream("windows-text-test.bmp", std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)), {});
    stream.close();
    host::WindowsRaster fresh;
    require_text(fresh.resize({240, 32}, scale), "Fresh DIB allocation failed");
    fresh.set_bundled_fonts(fixture_fonts);
    fresh.begin_frame();
    (*button).invalidate(Dirty::paint);
    require_text(window.paint(fresh.painter()).has_value(), "Cold caption did not paint");
    GdiFlush();
    require_text(fresh.save_bmp(L"windows-text-cold.bmp"), "Cold capture failed");
    std::ifstream cold_stream("windows-text-cold.bmp", std::ios::binary);
    const std::vector<unsigned char> cold_bytes((std::istreambuf_iterator<char>(cold_stream)), {});
    cold_stream.close();
    require_text(bytes == cold_bytes, "Cached and cold text pixels differ");
    std::filesystem::remove("windows-text-cold.bmp");
    require_text(bytes.size() > 54, "Empty capture");
    const std::size_t start = bytes[10] | (bytes[11] << 8) | (bytes[12] << 16) | (bytes[13] << 24);
    std::size_t ink{};
    for (std::size_t i = start; i + 3 < bytes.size(); i += 4) {
        if (bytes[i] > 170 && bytes[i + 2] > 170 && bytes[i + 1] < 100) ++ink;
    }
    require_text(ink >= 8, "Caption has no visible text pixels");
    stream.close();
    std::filesystem::remove("windows-text-test.bmp");
}

// ADR-004: Windows text is byte-identical to macOS and Linux. The region
// case needs a damage region, which a framebuffer frame does not take.
static void check_cross_platform_identity() {
    bool mismatch = false;
    for (std::size_t kind = 0; kind < text_identity::cases.size(); ++kind) {
        if (text_identity::cases[kind] == text_identity::Case::damage_region) continue;
        for (std::size_t scale = 0; scale < text_identity::scales.size(); ++scale) {
            host::WindowsRaster painter;
            painter.set_bundled_fonts(fixture_fonts);
            const std::unique_ptr<PaintFramebuffer> framebuffer =
                painter.painter().create_framebuffer(text_identity::scene_size, text_identity::scales[scale]);
            require_text(framebuffer != nullptr, "Identity framebuffer allocation failed");
            const ImageRegistry images{};
            const Rect whole{0.0, 0.0, text_identity::scene_size.width, text_identity::scene_size.height};
            require_text((*framebuffer).begin(images, whole), "Identity framebuffer did not begin");
            text_identity::paint_scene((*framebuffer).painter(), text_identity::cases[kind]);
            (*framebuffer).end();
            const std::uint8_t* const bytes = reinterpret_cast<const std::uint8_t*>((*framebuffer).pixels().data());
            text_identity::dump_pixels(bytes, (*framebuffer).width(), (*framebuffer).height(), (*framebuffer).row_bytes(),
                                       kind, scale);
            const std::uint64_t observed = text_identity::pixel_hash(
                bytes, (*framebuffer).width(), (*framebuffer).height(), (*framebuffer).row_bytes());
            std::printf("text-identity %s %.1f 0x%016llxULL\n", text_identity::case_names[kind],
                        text_identity::scales[scale], static_cast<unsigned long long>(observed));
            if (observed != text_identity::expected[kind][scale]) mismatch = true;
        }
    }
    require_text(!mismatch, "Windows text differs from the cross-platform expectation");
}

// Turned text (owner decision DK-35, J4): the window's raster draws a run
// turned a quarter about its origin, its ink taller than wide and on the
// side of the baseline the glyph tops face.
static void check_turned_text() {
    const FontSpec font{FontRole::control, 14, 400, false, 0.0};
    for (const TextOrientation orientation : {TextOrientation::bottom_to_top, TextOrientation::top_to_bottom}) {
        host::WindowsRaster painter;
        painter.set_bundled_fonts(fixture_fonts);
        const std::unique_ptr<PaintFramebuffer> framebuffer = painter.painter().create_framebuffer({80, 200}, 1.0);
        require_text(framebuffer != nullptr, "Turned-text framebuffer allocation failed");
        const ImageRegistry images{};
        require_text((*framebuffer).begin(images, {0, 0, 80, 200}), "Turned-text framebuffer did not begin");
        Painter& target = (*framebuffer).painter();
        target.fill_rect({0, 0, 80, 200}, Color::rgba(255, 255, 255));
        const bool up = orientation == TextOrientation::bottom_to_top;
        const Point origin{40, up ? 180.0 : 20.0};
        target.draw_turned_text_utf8(origin, "Solution Explorer", font, Color::rgba(0, 0, 0), orientation);
        (*framebuffer).end();
        const std::uint8_t* const bytes = reinterpret_cast<const std::uint8_t*>((*framebuffer).pixels().data());
        std::uint32_t left = (*framebuffer).width(), top = (*framebuffer).height(), right = 0, bottom = 0;
        std::size_t ink = 0;
        for (std::uint32_t y = 0; y < (*framebuffer).height(); ++y) {
            for (std::uint32_t x = 0; x < (*framebuffer).width(); ++x) {
                const std::uint8_t blue = bytes[static_cast<std::size_t>(y) * (*framebuffer).row_bytes() + x * 4U];
                if (blue >= 128) continue;
                ++ink;
                left = std::min(left, x); right = std::max(right, x + 1U);
                top = std::min(top, y); bottom = std::max(bottom, y + 1U);
            }
        }
        require_text(ink >= 40, "Turned text drew no ink");
        require_text(bottom - top > 3U * (right - left), "Turned text is not taller than wide");
        const double centre = (static_cast<double>(left) + static_cast<double>(right)) * 0.5;
        if (up) {
            require_text(bottom <= 182U && centre < 40.0, "Bottom-to-top text must read up from its origin, tops left");
        } else {
            require_text(top >= 18U && centre > 40.0, "Top-to-bottom text must read down from its origin, tops right");
        }
    }
}

static void check_cache_lifetime() {
    const DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    {
        host::WindowsRaster painter;
        require_text(painter.resize({320, 80}, 1.0), "Cache test DIB allocation failed");
        painter.set_bundled_fonts(fixture_fonts);
        const FontSpec font{FontRole::content, 13, 400, false, 0.0};
        const ResolvedTextLayout original = painter.painter().resolve_text_layout_utf8("File Manager", font);
        for (int index = 0; index < 520; ++index) {
            require_text(painter.painter().resolve_text_layout_utf8("Folder " + std::to_string(index), font).status ==
                         TextResolutionStatus::exact, "Text cache rollover lost coverage");
        }
        require_text(painter.painter().resolve_text_layout_utf8("File Manager", font).logical_size.width ==
                     original.logical_size.width, "Text cache rollover changed width");
        for (int size = 8; size < 80; ++size) {
            FontSpec varied = font;
            varied.size = size;
            require_text(painter.painter().resolve_text_layout_utf8("Files", varied).status ==
                         TextResolutionStatus::exact, "Bounded font cache fallback failed");
        }
        for (const double scale : {1.5, 2.0, 1.0}) {
            painter.resize({320, 80}, scale);
            host::WindowsRaster fresh;
            fresh.resize({320, 80}, scale);
            fresh.set_bundled_fonts(fixture_fonts);
            const FontSpec varied{FontRole::control, 14, 700, false, 0.5};
            const ResolvedTextLayout reused = painter.painter().resolve_text_layout_utf8("Файл 日本語", varied);
            const ResolvedTextLayout expected = fresh.painter().resolve_text_layout_utf8("Файл 日本語", varied);
            require_text(reused.status == expected.status && reused.logical_size.width == expected.logical_size.width,
                         "DPI change reused stale text metrics");
        }
    }
    GdiFlush();
    require_text(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= before,
                 "Painter leaked cached native fonts or bitmaps");
}

static void benchmark_captions() {
    host::WindowsRaster painter;
    require_text(painter.resize({1340, 850}, 1.0), "DIB allocation failed");
    painter.set_bundled_fonts(fixture_fonts);
    const FontSpec font{FontRole::content, 13, 400, false, 0.0};
    for (int pass = 0; pass < 4; ++pass) {
        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 5; ++frame) {
            painter.begin_frame();
            for (int index = 0; index < 100; ++index) {
                const std::string caption = "File Manager folder " + std::to_string(index);
                const ResolvedTextLayout layout = painter.painter().resolve_text_layout_utf8(caption, font);
                require_text(layout.status == TextResolutionStatus::exact, "Benchmark text unavailable");
                painter.painter().draw_text_utf8({double(index % 5 * 260), double(index / 5 * 35 + 20)},
                                      caption, font, Color::rgba(20, 30, 40));
            }
        }
        GdiFlush();
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        std::cout << "caption_benchmark pass=" << pass << " labels=100 frames=5 ms_per_frame="
                  << milliseconds / 5.0 << std::endl;
    }
}

int main(int argc, char** argv) {
    try {
        require_text(argc == 2 || argc == 3, "Pass fixture font directory");
        fixture_fonts = host::load_bundled_fonts(std::filesystem::path(argv[1]));
        require_text(fixture_fonts != nullptr, "Could not load fixture fonts");
        if (argc == 3 && std::string_view(argv[2]) == "--benchmark") {
            benchmark_captions();
            return 0;
        }
        check_cross_platform_identity();
        check_turned_text();
        check_cache_lifetime();
        for (const double scale : {1.0, 1.5, 2.0}) {
            for (const std::uint16_t weight : {400, 700}) {
                for (const std::string& text : std::vector<std::string>{
                         "Главная", "File", "Файл 123", "Українська", "Вид", "Материалы", "Инструменты", "Вставить",
                         "Основной", "日本語", "العربية", "עברית", "हिन्दी", "বাংলা", "ਪੰਜਾਬੀ"}) {
                    check_caption(text, scale, weight);
                }
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
