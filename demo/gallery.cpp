#include "gallery.hpp"

#include "gallery_assets.hpp"
#include "gallery_controls.hpp"
#include "gallery_pages.hpp"
#include "gallery_theme_package.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace gui_forms::gallery {
namespace {

// Deterministic in-memory fixtures for the image pages (moved unchanged from
// the retired Complete Showcase): a test card, folder icons for the ImageList
// button, a nine-patch surface and a tile.
constexpr std::uint32_t test_card_width = 128U;
constexpr std::uint32_t test_card_height = 80U;
constexpr std::uint32_t test_icon_extent = 16U;
constexpr std::uint32_t test_patch_extent = 18U;
constexpr std::uint32_t test_tile_extent = 8U;

void set_test_pixel(std::vector<std::byte>& pixels,
                    std::uint32_t width,
                    std::uint32_t x,
                    std::uint32_t y,
                    Color color) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * width + x) * 4U;
    pixels[offset] = static_cast<std::byte>(color.blue);
    pixels[offset + 1U] = static_cast<std::byte>(color.green);
    pixels[offset + 2U] = static_cast<std::byte>(color.red);
    pixels[offset + 3U] = static_cast<std::byte>(color.alpha);
}

std::vector<std::byte> make_test_card() {
    std::vector<std::byte> pixels(
        static_cast<std::size_t>(test_card_width) * test_card_height * 4U);
    for (std::uint32_t y = 0U; y < test_card_height; ++y) {
        for (std::uint32_t x = 0U; x < test_card_width; ++x) {
            const bool checker = ((x / 12U) + (y / 12U)) % 2U == 0U;
            const std::uint8_t lift = checker ? 12U : 0U;
            set_test_pixel(pixels, test_card_width, x, y, Color::rgba(
                static_cast<std::uint8_t>(40U + x * 52U / test_card_width + lift),
                static_cast<std::uint8_t>(67U + y * 55U / test_card_height + lift),
                static_cast<std::uint8_t>(91U + x * 42U / test_card_width + lift)));
        }
    }
    for (std::uint32_t y = 0U; y < 14U; ++y) {
        for (std::uint32_t x = 0U; x < test_card_width; ++x) {
            set_test_pixel(pixels, test_card_width, x, y,
                           Color::rgba(28U, 52U, 75U));
        }
    }
    constexpr std::array<std::array<std::uint8_t, 3>, 4> swatches{{
        {{39U, 116U, 184U}}, {{49U, 139U, 94U}},
        {{211U, 121U, 42U}}, {{116U, 82U, 153U}},
    }};
    for (std::uint32_t block = 0U; block < swatches.size(); ++block) {
        for (std::uint32_t y = 20U; y < 39U; ++y) {
            for (std::uint32_t x = 8U + block * 30U;
                 x < 31U + block * 30U; ++x) {
                set_test_pixel(pixels, test_card_width, x, y,
                               Color::rgba(swatches[block][0], swatches[block][1],
                                           swatches[block][2]));
            }
        }
    }
    for (std::uint32_t x = 4U; x < test_card_width - 4U; ++x) {
        const double phase = static_cast<double>(x) * 0.17;
        const std::uint32_t y = static_cast<std::uint32_t>(
            std::clamp(59.0 + std::sin(phase) * 9.0, 48.0, 72.0));
        set_test_pixel(pixels, test_card_width, x, y,
                       Color::rgba(232U, 241U, 247U));
        if (y + 1U < test_card_height) {
            set_test_pixel(pixels, test_card_width, x, y + 1U,
                           Color::rgba(169U, 207U, 229U));
        }
    }
    return pixels;
}

std::vector<std::byte> make_folder_icon(Color body, Color edge) {
    std::vector<std::byte> pixels(test_icon_extent * test_icon_extent * 4U);
    for (std::uint32_t y = 5U; y < 14U; ++y) {
        for (std::uint32_t x = 1U; x < 15U; ++x) {
            const bool border = x == 1U || x == 14U || y == 5U || y == 13U;
            set_test_pixel(pixels, test_icon_extent, x, y, border ? edge : body);
        }
    }
    for (std::uint32_t y = 2U; y < 6U; ++y) {
        for (std::uint32_t x = 2U; x < 8U; ++x) {
            const bool border = x == 2U || x == 7U || y == 2U;
            set_test_pixel(pixels, test_icon_extent, x, y, border ? edge : body);
        }
    }
    return pixels;
}

std::vector<std::byte> make_nine_patch_surface() {
    std::vector<std::byte> pixels(test_patch_extent * test_patch_extent * 4U);
    for (std::uint32_t y = 0U; y < test_patch_extent; ++y) {
        for (std::uint32_t x = 0U; x < test_patch_extent; ++x) {
            const bool outer = x == 0U || y == 0U ||
                               x + 1U == test_patch_extent ||
                               y + 1U == test_patch_extent;
            const bool highlight = x == 1U || y == 1U;
            const bool inner_rule = x == 4U || y == 4U ||
                                    x + 5U == test_patch_extent ||
                                    y + 5U == test_patch_extent;
            const Color color = outer ? Color::rgba(42, 70, 94)
                : highlight ? Color::rgba(245, 249, 252)
                : inner_rule ? Color::rgba(146, 173, 195)
                : Color::rgba(216, 230, 241);
            set_test_pixel(pixels, test_patch_extent, x, y, color);
        }
    }
    return pixels;
}

ImageLoadResult load_folder_icon(Window& window, Color body, Color edge) {
    const std::vector<std::byte> icon = make_folder_icon(body, edge);
    return window.load_bgra32_premultiplied(
        test_icon_extent, test_icon_extent, test_icon_extent * 4U, icon);
}

std::vector<std::byte> make_tile_surface() {
    std::vector<std::byte> pixels(test_tile_extent * test_tile_extent * 4U);
    for (std::uint32_t y = 0U; y < test_tile_extent; ++y) {
        for (std::uint32_t x = 0U; x < test_tile_extent; ++x) {
            const bool stripe = ((x + y) % test_tile_extent) < 2U;
            const Color color = stripe ? Color::rgba(44, 91, 128)
                                       : Color::rgba(212, 226, 237);
            const std::size_t offset =
                (static_cast<std::size_t>(y) * test_tile_extent + x) * 4U;
            pixels[offset] = static_cast<std::byte>(color.blue);
            pixels[offset + 1U] = static_cast<std::byte>(color.green);
            pixels[offset + 2U] = static_cast<std::byte>(color.red);
            pixels[offset + 3U] = std::byte{0xff};
        }
    }
    return pixels;
}

// The project grid's image column: the test card previews each PNG file.
void install_grid_previews(Window& window, ImageId preview) {
    const std::shared_ptr<DataGridView> grid =
        std::dynamic_pointer_cast<DataGridView>(window.find("gallery.tree.grid"));
    if (!grid) return;
    const std::optional<std::size_t> kind = (*grid).column_index("kind");
    const std::optional<std::size_t> image = (*grid).column_index("preview");
    if (!kind || !image) return;
    for (std::size_t position = 0U; position < (*grid).row_count(); ++position) {
        const DataGridViewRowPosition row{position};
        if ((*grid).cell_text(row, *kind) != "PNG image") continue;
        static_cast<void>((*grid).set_cell_value(row, *image, BindingValue{preview}));
    }
}

// Image fixtures and dialog buttons for the migrated pages; byte-for-byte the
// setup the Complete Showcase performed, addressed by the renamed stable ids.
void install_page_fixtures(Window& window) {
    if (const Control::Ptr accept = window.find("gallery.controls.button.default")) {
        window.set_accept_button(accept);
    }
    if (const Control::Ptr cancel = window.find("gallery.controls.button.3")) {
        (*cancel).causes_validation = false;
        window.set_cancel_button(cancel);
    }
    const std::vector<std::byte> pixels = make_test_card();
    const ImageLoadResult loaded = window.load_bgra32_premultiplied(
        test_card_width, test_card_height, test_card_width * 4U, pixels);
    if (loaded) {
        for (std::size_t index = 0U; index < 5U; ++index) {
            if (const std::shared_ptr<gui_forms::PictureBox> picture = std::dynamic_pointer_cast<PictureBox>(
                    window.find("gallery.images.picture." +
                                 std::to_string(index)))) {
                (*picture).set_image(loaded.image);
            }
        }
        for (std::size_t index = 0U; index < 3U; ++index) {
            if (const std::shared_ptr<gui_forms::PictureBox> picture = std::dynamic_pointer_cast<PictureBox>(
                    window.find("gallery.images.opacity." +
                                 std::to_string(index)))) {
                (*picture).set_image(loaded.image);
            }
        }
        install_grid_previews(window, loaded.image);
    }
    const ImageLoadResult icon_normal =
        load_folder_icon(window, Color::rgba(72, 132, 183),
                         Color::rgba(30, 70, 105));
    const ImageLoadResult icon_hot =
        load_folder_icon(window, Color::rgba(101, 164, 214),
                         Color::rgba(32, 93, 139));
    const ImageLoadResult icon_disabled =
        load_folder_icon(window, Color::rgba(180, 187, 193),
                         Color::rgba(116, 126, 134));
    if (icon_normal && icon_hot && icon_disabled) {
        std::shared_ptr<gui_forms::ImageList> icons = std::make_shared<ImageList>(window, Size{16.0, 16.0});
        (*icons).add_image("folder", icon_normal.image);
        (*icons).set_variant_image("folder", ImageVisualState::hot, 1.0,
                                 icon_hot.image);
        (*icons).set_variant_image("folder", ImageVisualState::disabled, 1.0,
                                 icon_disabled.image);
        if (const std::shared_ptr<gui_forms::Button> button = std::dynamic_pointer_cast<Button>(
                window.find("gallery.controls.button.0"))) {
            (*button).set_image_list(std::move(icons));
            (*button).set_image_key("folder");
            (*button).set_text_image_relation(TextImageRelation::image_before_text);
            (*button).set_image_gap(6.0);
        }
        // The document PageView's pinned page collapses to this icon.
        if (const std::shared_ptr<gui_forms::PageView> documents = std::dynamic_pointer_cast<PageView>(
                window.find("gallery.tabs.documents"))) {
            std::shared_ptr<gui_forms::ImageList> document_icons =
                std::make_shared<ImageList>(window, Size{16.0, 16.0});
            (*document_icons).add_image("folder", icon_normal.image);
            (*(*documents).strip()).set_image_list(std::move(document_icons));
        }
        // The primary TabControl's first and third tabs show this icon.
        if (const std::shared_ptr<gui_forms::TabControl> tabs = std::dynamic_pointer_cast<TabControl>(
                window.find("gallery.tabs.primary"))) {
            std::shared_ptr<gui_forms::ImageList> tab_icons =
                std::make_shared<ImageList>(window, Size{16.0, 16.0});
            (*tab_icons).add_image("folder", icon_normal.image);
            (*tabs).set_image_list(std::move(tab_icons));
        }
    }
    const std::vector<std::byte> patch_pixels = make_nine_patch_surface();
    const ImageLoadResult patch = window.load_bgra32_premultiplied(
        test_patch_extent, test_patch_extent, test_patch_extent * 4U,
        patch_pixels);
    if (patch) {
        if (const std::shared_ptr<gui_forms::MaterialPanel> panel = std::dynamic_pointer_cast<MaterialPanel>(
                window.find("gallery.images.material.nine-patch"))) {
            SurfaceMaterial material;
            material.fills = {MaterialFillLayer::nine_patch(
                patch.image,
                {static_cast<double>(test_patch_extent),
                 static_cast<double>(test_patch_extent)},
                {5.0, 5.0, 5.0, 5.0})};
            material.shadows = {{{0.0, 2.0}, 3.0, 0.0,
                                 Color::rgba(25, 40, 55, 70)}};
            (*panel).set_material(std::move(material));
        }
    }
    const std::vector<std::byte> tile_pixels = make_tile_surface();
    const ImageLoadResult tile = window.load_bgra32_premultiplied(
        test_tile_extent, test_tile_extent, test_tile_extent * 4U,
        tile_pixels);
    if (tile) {
        if (const std::shared_ptr<gui_forms::MaterialPanel> panel = std::dynamic_pointer_cast<MaterialPanel>(
                window.find("gallery.images.material.tile"))) {
            SurfaceMaterial material;
            material.fills = {
                MaterialFillLayer::solid(Color::rgba(229, 237, 243)),
                MaterialFillLayer::tiled_image(
                    tile.image,
                    {static_cast<double>(test_tile_extent),
                     static_cast<double>(test_tile_extent)},
                    1.0, 0.42)};
            material.border = MaterialBorder{Color::rgba(76, 105, 129), 1.0};
            (*panel).set_material(std::move(material));
        }
    }
}

// Compiles the Gallery's project theme (demo/themes/gallery) into the
// window's resources and selects it for every window sharing them. The
// package is embedded and checked at build time, so a refusal or any
// finding is a defect of the build: std::logic_error, naming the first.
void select_gallery_theme(Window& window) {
    const std::shared_ptr<ApplicationResources> resources = window.resources();
    const ThemeLibrary library{};
    // The stock anatomies name the parts the sheet selects (::part(face)).
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    const ThemeCompileResult result =
        (*resources).compile_theme_package(gallery_theme_package(), library, options);
    if (!result.succeeded() || !result.diagnostics().empty()) {
        std::string finding = "the Gallery theme package was not compiled cleanly";
        if (!result.diagnostics().empty()) {
            const ThemeCompileDiagnostic& first = result.diagnostics().front();
            finding += ": " + first.code + " " + first.name + " in " + first.file;
        }
        throw std::logic_error(finding);
    }
    (*resources).set_theme(result.theme());
}

}  // namespace

std::unique_ptr<Window> make_gallery()
{
    GalleryTree tree = build_gallery_tree();
    GalleryShell shell = build_gallery_shell(tree);
    std::unique_ptr<gui_forms::Window> window =
        std::make_unique<Window>(std::move(shell.root), gallery_default_size);
    select_gallery_theme(*window);
    (*tree.context).window = window.get();
    (*tree.context).synchronize("gallery.initial");
    const ImageLoadResult status_badge =
        (*window).load_png(std::as_bytes(std::span{assets::validated_status_png}));
    if (!status_badge) {
        throw std::logic_error("compiled Gallery PNG failed core validation");
    }
    (*(*tree.context).document.gallery_command_status_badge).set_image(status_badge.image);
    (*window).perform_layout();
    initialize_gallery_runtime(*window);
    install_page_fixtures(*window);
    (*window).perform_layout();
    return window;
}

}  // namespace gui_forms::gallery
