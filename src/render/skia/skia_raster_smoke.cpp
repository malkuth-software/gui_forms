#include "skia_raster.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "../text/harfbuzz/bundled_face_loader.hpp"
#include "stx_zlib/checksum.hpp"
#if defined(_WIN32)
#include "../../host/windows/fonts/sealed_pages_windows.hpp"
#else
#include "../../host/posix/fonts/sealed_pages_posix.hpp"
#endif

#include <array>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Byte offsets of the channels in a premultiplied BGRA8 pixel.
constexpr std::size_t blue = 0U;
constexpr std::size_t green = 1U;
constexpr std::size_t red = 2U;
constexpr std::size_t alpha = 3U;


void write_u32(std::span<std::byte> destination, std::uint32_t value) {
    destination[0] = static_cast<std::byte>(value >> 24U);
    destination[1] = static_cast<std::byte>(value >> 16U);
    destination[2] = static_cast<std::byte>(value >> 8U);
    destination[3] = static_cast<std::byte>(value);
}

std::vector<std::byte> read_file(const char* path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return {};
    const std::streamsize length = stream.tellg();
    if (length <= 0) return {};
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), length);
    return stream ? bytes : std::vector<std::byte>{};
}

std::size_t ink_pixels(const std::uint8_t* pixels, std::size_t row_bytes,
                       int scale, int left, int top, int right, int bottom) {
    std::size_t count{};
    for (int y = top * scale; y < bottom * scale; ++y) {
        const std::uint8_t* row = pixels + static_cast<std::size_t>(y) * row_bytes;
        for (int x = left * scale; x < right * scale; ++x) {
            const std::uint8_t* pixel = row + static_cast<std::size_t>(x) * 4U;
            if (pixel[red] != 241U || pixel[green] != 238U || pixel[blue] != 226U) {
                ++count;
            }
        }
    }
    return count;
}

const std::uint8_t* pixel_at(const std::uint8_t* pixels, std::size_t row_bytes,
                             int scale, int x, int y) {
    return pixels + static_cast<std::size_t>(y * scale) * row_bytes +
           static_cast<std::size_t>(x * scale) * 4U;
}

// Registration from a file refuses a null path, a missing file, a directory and an empty
// file, and leaves the raster able to register a readable file afterwards.
bool unreadable_font_files_are_refused() {
    gui_forms::render::SkiaRaster raster;
    const std::filesystem::path empty_path =
        std::filesystem::temp_directory_path() / "gui_forms_skia_smoke_empty_font.ttf";
    {
        std::ofstream empty_file(empty_path, std::ios::binary | std::ios::trunc);
        if (!empty_file) return false;
    }
    const std::string empty_name = empty_path.string();
    const std::string directory_name = std::filesystem::temp_directory_path().string();
    const bool refused_null = !raster.register_typeface_file(gui_forms::FontRole::control, 400, false, nullptr);
    const bool refused_missing =
        !raster.register_typeface_file(gui_forms::FontRole::control, 400, false, "/gui-forms-missing-font.otf");
    const bool refused_directory =
        !raster.register_typeface_file(gui_forms::FontRole::control, 400, false, directory_name.c_str());
    const bool refused_empty =
        !raster.register_fallback_typeface_file(400, false, empty_name.c_str());
    std::error_code removal_error;
    std::filesystem::remove(empty_path, removal_error);
    const bool accepted_readable =
        raster.register_typeface_file(gui_forms::FontRole::control, 400, false, GUI_FORMS_TEST_CONTROL_FONT);
    // The path is UTF-8, so a copy under a non-ASCII name must register too.
    const std::filesystem::path utf8_path =
        std::filesystem::temp_directory_path() / std::u8string_view(u8"gui_forms_skia_smoke_\u00e9t\u00e9.ttf");
    std::error_code copy_error;
    std::filesystem::copy_file(GUI_FORMS_TEST_CONTROL_FONT, utf8_path,
                               std::filesystem::copy_options::overwrite_existing, copy_error);
    if (copy_error) return false;
    const std::u8string utf8_name = utf8_path.u8string();
    const std::string utf8_bytes(reinterpret_cast<const char*>(utf8_name.data()), utf8_name.size());
    const bool accepted_utf8 =
        raster.register_fallback_typeface_file(400, false, utf8_bytes.c_str());
    std::filesystem::remove(utf8_path, removal_error);
    return refused_null && refused_missing && refused_directory && refused_empty &&
        accepted_readable && accepted_utf8;
}

bool file_fonts_match_copied_fonts() {
    gui_forms::render::SkiaRaster copied;
    gui_forms::render::SkiaRaster from_file;
    if (!copied.register_typeface(gui_forms::FontRole::control, 400, false,
            read_file(GUI_FORMS_TEST_CONTROL_FONT)) ||
        !copied.register_fallback_typeface(400, false, read_file(GUI_FORMS_TEST_CJK_FONT)) ||
        !from_file.register_typeface_file(gui_forms::FontRole::control, 400, false, GUI_FORMS_TEST_CONTROL_FONT) ||
        !from_file.register_fallback_typeface_file(400, false, GUI_FORMS_TEST_CJK_FONT) ||
        from_file.register_fallback_typeface_file(400, false, nullptr) ||
        from_file.register_fallback_typeface_file(400, false, "/gui-forms-missing-font.otf")) return false;
    const std::string_view text = "Plan Paint — 漢字 日本語 中文";
    for (gui_forms::render::SkiaRaster* raster : {&copied, &from_file}) {
        if (!(*raster).resize({440, 60}, 2)) return false;
        gui_forms::DamageRegion damage;
        damage.add({0, 0, 440, 60});
        (*raster).begin_frame(damage);
        (*raster).fill_rect({0, 0, 440, 60}, gui_forms::Color::rgba(255, 255, 255));
        (*raster).draw_text_utf8({8, 36}, text, {gui_forms::FontRole::control, 20, 400, false},
                               gui_forms::Color::rgba(0, 0, 0));
        (*raster).end_frame();
    }
    return copied.byte_size() == from_file.byte_size() &&
        std::memcmp(copied.pixels(), from_file.pixels(), copied.byte_size()) == 0;
}

// Two rasters borrow one bundled set: registration reads every face except the fallbacks with
// coverage tables, each once; a fallback is read when text first needs it, once for every
// raster and framebuffer; and the borrowed faces draw what copies of the same files draw. A set
// over a directory without the fonts registers nothing required.
bool bundled_fonts_are_shared() {
#if defined(_WIN32)
    const gui_forms::detail::SealedPageFunctions pages = gui_forms::host::windows_sealed_pages();
#else
    const gui_forms::detail::SealedPageFunctions pages = gui_forms::host::posix_sealed_pages();
#endif
    const std::filesystem::path directory =
        std::filesystem::path(GUI_FORMS_TEST_CONTROL_FONT).parent_path();
    const std::shared_ptr<gui_forms::detail::BundledFontSet> set =
        gui_forms::detail::shared_bundled_font_set(directory, pages);
    gui_forms::render::SkiaRaster first;
    gui_forms::render::SkiaRaster second;
    if (!first.register_bundled_fonts(set) || !second.register_bundled_fonts(set)) return false;
    std::size_t eager_faces = 0;
    std::uint64_t eager_bytes = 0;
    std::uint64_t cjk_bytes = 0;
    for (const gui_forms::detail::BundledFaceUse& use : gui_forms::detail::bundled_face_uses()) {
        for (const gui_forms::detail::BundledFaceRecord& record : gui_forms::detail::bundled_face_manifest()) {
            if (record.file_name != use.file_name) continue;
            if (use.file_name == "NotoSansCJKjp-Regular.otf") cjk_bytes = record.size_bytes;
            if (gui_forms::render::text::lazy_bundled_coverage(use)) continue;
            ++eager_faces;
            eager_bytes += record.size_bytes;
        }
    }
    if (eager_faces != 10U || cjk_bytes == 0U || (*set).loaded_face_count() != eager_faces ||
        (*set).loaded_bytes() != eager_bytes) return false;

    gui_forms::render::SkiaRaster copied;
    if (!copied.register_typeface(gui_forms::FontRole::control, 400, false,
            read_file(GUI_FORMS_TEST_CONTROL_FONT)) ||
        !copied.register_fallback_typeface(400, false, read_file(GUI_FORMS_TEST_CJK_FONT))) return false;
    const std::string_view text = "Plan Paint 漢字 日本語 中文";
    for (gui_forms::render::SkiaRaster* raster : {&first, &copied}) {
        if (!(*raster).resize({440, 60}, 2)) return false;
        gui_forms::DamageRegion damage;
        damage.add({0, 0, 440, 60});
        (*raster).begin_frame(damage);
        (*raster).fill_rect({0, 0, 440, 60}, gui_forms::Color::rgba(255, 255, 255));
        (*raster).draw_text_utf8({8, 36}, text, {gui_forms::FontRole::control, 20, 400, false},
                               gui_forms::Color::rgba(0, 0, 0));
        (*raster).end_frame();
    }
    if (first.byte_size() != copied.byte_size() ||
        std::memcmp(first.pixels(), copied.pixels(), first.byte_size()) != 0) return false;
    if ((*set).loaded_face_count() != eager_faces + 1U || (*set).loaded_bytes() != eager_bytes + cjk_bytes)
        return false;
    // The second raster, and a framebuffer made from the first, borrow the face already read.
    if (!second.resize({440, 60}, 2)) return false;
    gui_forms::DamageRegion damage;
    damage.add({0, 0, 440, 60});
    second.begin_frame(damage);
    second.fill_rect({0, 0, 440, 60}, gui_forms::Color::rgba(255, 255, 255));
    second.draw_text_utf8({8, 36}, text, {gui_forms::FontRole::control, 20, 400, false},
                          gui_forms::Color::rgba(0, 0, 0));
    second.end_frame();
    if (second.byte_size() != copied.byte_size() ||
        std::memcmp(second.pixels(), copied.pixels(), copied.byte_size()) != 0) return false;
    const std::unique_ptr<gui_forms::PaintFramebuffer> framebuffer = first.create_framebuffer({440, 60}, 2);
    const gui_forms::ImageRegistry images;
    if (!framebuffer || !(*framebuffer).begin(images, {0, 0, 440, 60})) return false;
    (*framebuffer).painter().fill_rect({0, 0, 440, 60}, gui_forms::Color::rgba(255, 255, 255));
    (*framebuffer).painter().draw_text_utf8({8, 36}, text, {gui_forms::FontRole::control, 20, 400, false},
                                            gui_forms::Color::rgba(0, 0, 0));
    (*framebuffer).end();
    const std::span<std::byte> drawn = (*framebuffer).pixels();
    if (drawn.size() != copied.byte_size() || std::memcmp(drawn.data(), copied.pixels(), drawn.size()) != 0)
        return false;
    if ((*set).loaded_face_count() != eager_faces + 1U || (*set).loaded_bytes() != eager_bytes + cjk_bytes)
        return false;

    gui_forms::render::SkiaRaster missing;
    const std::shared_ptr<gui_forms::detail::BundledFontSet> empty =
        gui_forms::detail::shared_bundled_font_set(directory / "no-fonts-here", pages);
    const bool refused = !missing.register_bundled_fonts(empty) && !missing.register_bundled_fonts(nullptr);
    return refused;
}

} // namespace

int main() {
    if (!bundled_fonts_are_shared()) {
        std::fputs("Bundled fonts were not shared once, drew differently, or a missing set was accepted\n", stderr);
        return 23;
    }
    if (!file_fonts_match_copied_fonts()) {
        std::fputs("Fonts read from files changed rendered text or accepted an invalid path\n", stderr);
        return 20;
    }
    if (!unreadable_font_files_are_refused()) {
        std::fputs("Font registration accepted an unreadable file\n", stderr);
        return 21;
    }

    constexpr std::array<std::uint8_t, 70> png = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x56, 0xc7, 0x2f, 0x0d, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };
    gui_forms::render::SkiaRaster raster;
    gui_forms::ImageRegistry images;

    std::vector<std::byte> broken_payload(
        std::as_bytes(std::span{png}).begin(), std::as_bytes(std::span{png}).end());
    broken_payload[41] = std::byte{0};
    write_u32(std::span<std::byte>(broken_payload.data() + 54, 4),
              stx_zlib::crc32(std::span<const std::byte>(broken_payload.data() + 37, 17)));
    gui_forms::ImageRegistry broken_images;
    const gui_forms::ImageLoadResult structurally_valid =
        broken_images.load_png(broken_payload);
    gui_forms::render::SkiaRaster broken_raster;
    if (!structurally_valid || broken_raster.synchronize_images(broken_images)) {
        std::fputs("malformed compressed payload crossed the eager decoder boundary\n", stderr);
        return 1;
    }

    const std::vector<std::byte> font_bytes =
        read_file(GUI_FORMS_TEST_CONTROL_FONT);
    const std::vector<std::byte> cjk_bytes =
        read_file(GUI_FORMS_TEST_CJK_FONT);
    const std::vector<std::byte> emoji_bytes =
        read_file(GUI_FORMS_TEST_EMOJI_FONT);
    if (font_bytes.empty() || cjk_bytes.empty() || emoji_bytes.empty()) {
        std::fputs("failed to open bundled control font\n", stderr);
        return 2;
    }
    if (!raster.register_typeface(
            gui_forms::FontRole::control, 400, false, font_bytes) ||
        !raster.register_fallback_typeface(400, false, cjk_bytes) ||
        !raster.register_fallback_typeface(400, false, emoji_bytes)) {
        std::fputs("Skia rejected the bundled Portsmouth Rapids face\n", stderr);
        return 4;
    }
    if (!raster.resize(gui_forms::Size{220.0, 120.0}, 2.0)) {
        std::fputs("failed to allocate Skia raster surface\n", stderr);
        return 5;
    }
    const gui_forms::ImageLoadResult loaded =
        images.load_png(std::as_bytes(std::span{png}));
    constexpr std::array<std::byte, 4> bgra{
        std::byte{0}, std::byte{128}, std::byte{255}, std::byte{255}};
    const gui_forms::ImageLoadResult raw =
        images.load_bgra32_premultiplied(1, 1, 4, bgra);
    constexpr std::array<std::byte, 8> strip_bgra{
        std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
        std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}};
    const gui_forms::ImageLoadResult strip =
        images.load_bgra32_premultiplied(2, 1, 8, strip_bgra);
    if (!loaded || !raw || !strip || !raster.synchronize_images(images)) {
        std::fputs("validated PNG registry resource failed eager decode\n", stderr);
        return 6;
    }
    gui_forms::DamageRegion damage;
    damage.add(gui_forms::Rect{0.0, 0.0, 220.0, 120.0});
    raster.begin_frame(damage);
    raster.fill_rect(gui_forms::Rect{0.0, 0.0, 220.0, 120.0},
                     gui_forms::Color::rgba(241, 238, 226));
    raster.fill_rect(gui_forms::Rect{8.0, 8.0, 74.0, 30.0},
                     gui_forms::Color::rgba(57, 93, 155));
    raster.stroke_rect(gui_forms::Rect{8.5, 8.5, 73.0, 29.0},
                       gui_forms::Color::rgba(255, 210, 122), 1.0);
    raster.draw_line(gui_forms::Point{8.0, 47.0}, gui_forms::Point{150.0, 47.0},
                     gui_forms::Color::rgba(53, 107, 98), 1.0);
    raster.draw_text_utf8(gui_forms::Point{12.0, 29.0}, "GUI.Forms CPU",
                          gui_forms::FontSpec{}, gui_forms::Color::rgba(255, 255, 255));
    raster.draw_image(loaded.image, gui_forms::Rect{130.0, 8.0, 20.0, 20.0}, 1.0);
    raster.draw_image(raw.image, gui_forms::Rect{130.0, 32.0, 20.0, 20.0}, 1.0);
    raster.draw_image_region(strip.image, {1.0, 0.0, 1.0, 1.0},
                             {92.0, 84.0, 8.0, 8.0}, 1.0);
    raster.fill_image_pattern(strip.image, {2.0, 1.0},
                              {94.0, 98.0, 48.0, 16.0}, {16.0, 8.0},
                              gui_forms::ImagePatternWrap::tile, 1.0);
    raster.draw_text_utf8(gui_forms::Point{8.0, 82.0}, "日本語",
                          {gui_forms::FontRole::control, 24.0, 400, false},
                          gui_forms::Color::rgba(30, 42, 54));
    raster.draw_text_utf8(gui_forms::Point{112.0, 82.0}, "🚀",
                          {gui_forms::FontRole::control, 24.0, 400, false},
                          gui_forms::Color::rgba(30, 42, 54));
    constexpr std::array<gui_forms::GradientStop, 3> title_gradient{{
        {0.0, gui_forms::Color::rgba(18, 55, 140)},
        {0.55, gui_forms::Color::rgba(55, 140, 215)},
        {1.0, gui_forms::Color::rgba(220, 76, 105)}}};
    constexpr std::array<gui_forms::GradientStop, 2> radial_glow{{
        {0.0, gui_forms::Color::rgba(255, 255, 255, 230)},
        {1.0, gui_forms::Color::rgba(75, 110, 180, 25)}}};
    const gui_forms::Rect title{165.0, 10.0, 45.0, 36.0};
    raster.draw_box_shadow(title, 7.0, {0.0, 3.0}, 5.0, 0.0,
                           gui_forms::Color::rgba(18, 28, 48, 110));
    raster.save();
    raster.clip_rounded_rect(title, 7.0);
    raster.fill_linear_gradient(title, {165.0, 10.0}, {210.0, 10.0},
                                title_gradient);
    raster.restore();
    raster.stroke_rounded_rect({165.5, 10.5, 44.0, 35.0}, 6.5,
                               gui_forms::Color::rgba(20, 36, 70), 1.0);
    const gui_forms::Rect glow{165.0, 70.0, 45.0, 36.0};
    raster.fill_radial_gradient(glow, {187.5, 88.0}, {22.5, 18.0},
                                radial_glow);
    raster.fill_rounded_rect({125.0, 96.0, 25.0, 18.0}, 7.0,
                             gui_forms::Color::rgba(210, 65, 80));
    constexpr std::array<gui_forms::GradientStop, 2> repeating_gradient{{
        {0.0, gui_forms::Color::rgba(12, 28, 44)},
        {1.0, gui_forms::Color::rgba(225, 236, 246)}}};
    raster.fill_linear_gradient_spread(
        {8.0, 100.0, 80.0, 14.0}, {8.0, 100.0}, {16.0, 100.0},
        repeating_gradient, gui_forms::GradientSpreadMode::repeat);
    const gui_forms::Rect inset_panel{154.0, 96.0, 56.0, 18.0};
    raster.fill_rounded_rect(inset_panel, 5.0,
                             gui_forms::Color::rgba(244U, 247U, 250U));
    raster.draw_inset_box_shadow(
        inset_panel, 5.0, {0.0, 2.0}, 4.0, 0.0,
        gui_forms::Color::rgba(12U, 24U, 36U, 180U));
    raster.end_frame();

    const std::uint8_t* pixels = static_cast<const std::uint8_t*>(raster.pixels());
    if (pixels == nullptr || raster.byte_size() == 0) {
        std::fputs("Skia raster surface exposed no pixels\n", stderr);
        return 7;
    }
    if (pixels[red] != 241U || pixels[green] != 238U || pixels[blue] != 226U ||
        pixels[alpha] != 255U) {
        std::fputs("Skia raster BGRA byte-order contract changed\n", stderr);
        return 8;
    }
    const std::size_t cjk_ink = ink_pixels(
        pixels, raster.row_bytes(), 2, 6, 55, 104, 92);
    const std::size_t emoji_ink = ink_pixels(
        pixels, raster.row_bytes(), 2, 108, 55, 158, 92);
    if (cjk_ink < 50U || emoji_ink < 20U) {
        std::fprintf(stderr,
                     "registered fallback shaped without raster ink: cjk=%zu emoji=%zu\n",
                     cjk_ink, emoji_ink);
        return 9;
    }
    const std::uint8_t* rounded_corner = pixel_at(
        pixels, raster.row_bytes(), 2, 125, 96);
    const std::uint8_t* title_left = pixel_at(
        pixels, raster.row_bytes(), 2, 170, 28);
    const std::uint8_t* title_right = pixel_at(
        pixels, raster.row_bytes(), 2, 204, 28);
    const std::uint8_t* glow_center = pixel_at(
        pixels, raster.row_bytes(), 2, 187, 88);
    const std::uint8_t* glow_edge = pixel_at(
        pixels, raster.row_bytes(), 2, 166, 88);
    const std::uint8_t* repeat_first = pixel_at(
        pixels, raster.row_bytes(), 2, 10, 106);
    const std::uint8_t* repeat_second = pixel_at(
        pixels, raster.row_bytes(), 2, 18, 106);
    const std::uint8_t* repeat_contrast = pixel_at(
        pixels, raster.row_bytes(), 2, 14, 106);
    const std::uint8_t* cropped_green = pixel_at(
        pixels, raster.row_bytes(), 2, 95, 87);
    const std::uint8_t* pattern_red = pixel_at(
        pixels, raster.row_bytes(), 2, 98, 104);
    const std::uint8_t* pattern_green = pixel_at(
        pixels, raster.row_bytes(), 2, 106, 104);
    const std::uint8_t* pattern_repeat = pixel_at(
        pixels, raster.row_bytes(), 2, 114, 104);
    const std::uint8_t* inset_outside = pixel_at(
        pixels, raster.row_bytes(), 2, 153, 104);
    const std::uint8_t* inset_edge = pixel_at(
        pixels, raster.row_bytes(), 2, 180, 97);
    const std::uint8_t* inset_center = pixel_at(
        pixels, raster.row_bytes(), 2, 180, 105);
    if (rounded_corner[red] != 241U || rounded_corner[green] != 238U ||
        rounded_corner[blue] != 226U || title_left[blue] <= title_left[red] ||
        title_right[red] <= title_right[blue] || glow_center[red] <= glow_edge[red] ||
        glow_center[green] <= glow_edge[green]) {
        std::fputs("retained material raster contract changed\n", stderr);
        return 11;
    }
    if (cropped_green[red] > 8U || cropped_green[green] < 240U ||
        cropped_green[blue] > 8U || cropped_green[alpha] != 255U) {
        std::fputs("Skia source-region image crop contract changed\n", stderr);
        return 13;
    }
    if (pattern_red[red] < 220U || pattern_red[green] > 24U ||
        pattern_green[red] > 24U || pattern_green[green] < 220U ||
        pattern_repeat[red] < 220U || pattern_repeat[green] > 24U) {
        std::fputs("Skia exact image-pattern period contract changed\n", stderr);
        return 14;
    }
    if (inset_outside[red] != 241U || inset_outside[green] != 238U ||
        inset_outside[blue] != 226U ||
        inset_edge[red] >= inset_center[red] ||
        inset_edge[green] >= inset_center[green] ||
        inset_edge[blue] >= inset_center[blue]) {
        std::fputs("Skia inset shadow clipping/gradient contract changed\n", stderr);
        std::fprintf(stderr, "inset outside %u %u %u %u, edge %u %u %u %u, centre %u %u %u %u\n",
                     inset_outside[red], inset_outside[green], inset_outside[blue], inset_outside[alpha],
                     inset_edge[red], inset_edge[green], inset_edge[blue], inset_edge[alpha],
                     inset_center[red], inset_center[green], inset_center[blue], inset_center[alpha]);
        return 15;
    }
    if (repeat_first[red] != repeat_second[red] ||
        repeat_first[green] != repeat_second[green] ||
        repeat_first[blue] != repeat_second[blue] ||
        repeat_contrast[red] <= repeat_first[red] + 50U) {
        std::fputs("Skia repeating-gradient period contract changed\n", stderr);
        return 12;
    }
    std::uint64_t checksum = 1469598103934665603ULL;
    for (std::size_t offset = 0; offset < raster.byte_size(); offset += 97) {
        checksum ^= pixels[offset];
        checksum *= 1099511628211ULL;
    }
    if (checksum == 1469598103934665603ULL) {
        std::fputs("Skia raster surface remained empty\n", stderr);
        return 10;
    }
    // The PNG export sizes the surface through checkedByteSize; a frame must encode.
    const std::vector<std::byte> encoded_frame = raster.encode_png();
    if (encoded_frame.size() < 8 || encoded_frame[1] != std::byte{0x50} ||
        encoded_frame[2] != std::byte{0x4e} || encoded_frame[3] != std::byte{0x47}) {
        std::fputs("Skia raster frame did not encode as PNG\n", stderr);
        return 22;
    }
    std::printf("{\"renderer\":\"skia-cpu\",\"width\":%u,\"height\":%u,"
                "\"bytes\":%zu,\"checksum\":%llu}\n",
                raster.pixel_width(), raster.pixel_height(), raster.byte_size(),
                static_cast<unsigned long long>(checksum));
    return 0;
}
