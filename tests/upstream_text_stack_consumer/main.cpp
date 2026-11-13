// A consumer with its own HarfBuzz and FreeType beside GUI.Forms (owner
// decisions 2026-11-13: interface text uses GUI.Forms' bundled fonts and
// pinned text stack; content may use any engine, the consumer's own). It
// shapes a string with the upstream libraries, through FreeType, and renders
// a label through GUI.Forms' offscreen raster, whose text engine links the
// pinned copies privately. The upstream calls must reach the upstream
// libraries: the version each reports at run time equals the version of the
// headers this file was compiled against.
//
// Usage: upstream_text_stack_consumer (the font directory is compiled in).

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb-ft.h>
#include <hb.h>

#include <gui_forms/capture/offscreen_raster/offscreen_raster.hpp>
#include <gui_forms/controls/label.hpp>
#include <gui_forms/controls/panel.hpp>
#include <gui_forms/window.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr const char* sample_text = "Harbour";
constexpr unsigned int sample_glyph_count = 7U;

// The upstream handles; release_upstream releases the ones set, in reverse
// order of creation (the HarfBuzz font holds its own face reference).
struct UpstreamStack final {
    FT_Library library{};
    FT_Face face{};
    hb_font_t* font{};
    hb_buffer_t* buffer{};
};

void release_upstream(UpstreamStack& stack) {
    if (stack.buffer != nullptr) hb_buffer_destroy(stack.buffer);
    if (stack.font != nullptr) hb_font_destroy(stack.font);
    if (stack.face != nullptr) FT_Done_Face(stack.face);
    if (stack.library != nullptr) FT_Done_FreeType(stack.library);
    stack = UpstreamStack{};
}

// Shapes sample_text with the upstream libraries; 0 on success, 1 after
// printing the failed step.
int shape_upstream(UpstreamStack& stack, const std::string& font_path) {
    const FT_Error initialised = FT_Init_FreeType(&stack.library);
    if (initialised != 0) {
        std::fprintf(stderr, "upstream FT_Init_FreeType failed: %d\n", initialised);
        return 1;
    }
    FT_Int major = 0;
    FT_Int minor = 0;
    FT_Int patch = 0;
    FT_Library_Version(stack.library, &major, &minor, &patch);
    const char* const harfbuzz_version = hb_version_string();
    std::printf("upstream FreeType %d.%d.%d (headers %d.%d.%d), HarfBuzz %s (headers %s)\n", major, minor, patch,
                FREETYPE_MAJOR, FREETYPE_MINOR, FREETYPE_PATCH, harfbuzz_version, HB_VERSION_STRING);
    // A call bound to GUI.Forms' pinned copy would report the pinned version.
    if (major != FREETYPE_MAJOR || minor != FREETYPE_MINOR || patch != FREETYPE_PATCH) {
        std::fprintf(stderr, "FreeType calls did not reach the upstream library\n");
        return 1;
    }
    if (std::strcmp(harfbuzz_version, HB_VERSION_STRING) != 0) {
        std::fprintf(stderr, "HarfBuzz calls did not reach the upstream library\n");
        return 1;
    }
    const FT_Error opened = FT_New_Face(stack.library, font_path.c_str(), 0, &stack.face);
    if (opened != 0) {
        std::fprintf(stderr, "upstream FT_New_Face(%s) failed: %d\n", font_path.c_str(), opened);
        return 1;
    }
    const FT_Error sized = FT_Set_Char_Size(stack.face, 0, 16 * 64, 72, 72);
    if (sized != 0) {
        std::fprintf(stderr, "upstream FT_Set_Char_Size failed: %d\n", sized);
        return 1;
    }
    stack.font = hb_ft_font_create_referenced(stack.face);
    stack.buffer = hb_buffer_create();
    if (!hb_buffer_allocation_successful(stack.buffer)) {
        std::fprintf(stderr, "upstream hb_buffer_create failed\n");
        return 1;
    }
    hb_buffer_add_utf8(stack.buffer, sample_text, -1, 0, -1);
    hb_buffer_guess_segment_properties(stack.buffer);
    hb_shape(stack.font, stack.buffer, nullptr, 0U);
    unsigned int glyph_count = 0U;
    const hb_glyph_info_t* const glyphs = hb_buffer_get_glyph_infos(stack.buffer, &glyph_count);
    if (glyph_count != sample_glyph_count) {
        std::fprintf(stderr, "upstream shaped %u glyphs, expected %u\n", glyph_count, sample_glyph_count);
        return 1;
    }
    for (unsigned int index = 0U; index < glyph_count; ++index) {
        if (glyphs[index].codepoint == 0U) {
            std::fprintf(stderr, "upstream glyph %u is .notdef\n", index);
            return 1;
        }
    }
    return 0;
}

// Renders sample_text in a label through GUI.Forms' raster and requires
// pixels of more than one value (the text over its ground); 0 on success.
int render_through_gui_forms(const std::filesystem::path& font_directory) {
    gui_forms::OffscreenRasterOptions options{};
    options.font_directory = font_directory;
    gui_forms::OffscreenRasterCreateResult made = gui_forms::OffscreenRaster::create(options);
    std::unique_ptr<gui_forms::OffscreenRaster> raster = made.take_raster();
    if (!raster) {
        std::fprintf(stderr, "GUI.Forms raster refused: %s\n", made.detail().c_str());
        return 1;
    }
    const std::shared_ptr<gui_forms::Panel> root =
        gui_forms::make_control<gui_forms::Panel>(gui_forms::StableId("upstream_consumer"));
    const std::shared_ptr<gui_forms::Label> label =
        gui_forms::make_control<gui_forms::Label>(gui_forms::StableId("upstream_consumer.label"), sample_text);
    (*label).set_requested_bounds({8.0, 8.0, 160.0, 32.0});
    (*root).add_child(label);
    int status = 0;
    {
        gui_forms::Window window(root, gui_forms::Size{200.0, 60.0});
        gui_forms::OffscreenAttachment attached(*raster, window);
        const gui_forms::OffscreenCaptureOptions capture_options{};
        const std::optional<gui_forms::WindowCapture> picture = attached.capture_control(label, capture_options);
        if (!picture || (*picture).bgra.size() < 8U) {
            std::fprintf(stderr, "GUI.Forms captured no pixels\n");
            status = 1;
        } else {
            const std::vector<std::byte>& bytes = (*picture).bgra;
            bool varied = false;
            for (std::size_t offset = 4U; offset + 4U <= bytes.size() && !varied; offset += 4U) {
                varied = std::memcmp(&bytes[offset], &bytes[0], 4U) != 0;
            }
            if (!varied) {
                std::fprintf(stderr, "GUI.Forms drew no text\n");
                status = 1;
            }
        }
    }
    return status;
}

} // namespace

int main() {
    const std::filesystem::path font_directory(GUI_FORMS_CONSUMER_FONT_DIR);
    const std::filesystem::path font_path = font_directory / "Arimo-Regular.ttf";
    UpstreamStack stack{};
    const int shaped = shape_upstream(stack, font_path.string());
    // GUI.Forms renders while the upstream libraries are loaded and in use.
    const int rendered = shaped == 0 ? render_through_gui_forms(font_directory) : 1;
    release_upstream(stack);
    if (shaped != 0 || rendered != 0) return 1;
    std::printf("upstream HarfBuzz and FreeType work beside GUI.Forms' own\n");
    return 0;
}
