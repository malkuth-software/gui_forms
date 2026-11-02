// Stage DK phase W0: application-scope images and theme (decision 8).
// Windows built with one ApplicationResources share one image registry and
// one theme, so a control subtree moved between them keeps its images; a
// window built alone keeps its own, as before.
#include "gui_forms/application.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/image_list.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/window.hpp"
#include "application/headless_application.hpp"
#include "../src/core/theme_language/package/theme_manifest.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::array<std::uint8_t, 70> one_pixel_png{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x56, 0xc7, 0x2f, 0x0d, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

std::span<const std::byte> png() {
    const std::span<const std::byte> bytes = std::as_bytes(std::span{one_pixel_png});
    return bytes;
}

// Records the images a control paints; every other operation is ignored.
class ImagePainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void clip_rounded_rect(Rect, double) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId image, Rect, double) override { images.push_back(image); }
    std::vector<ImageId> images{};
};

struct CountThemeChanges final {
    unsigned* count{};
    void operator()(const Theme&) const { ++*count; }
};

void test_a_window_alone_keeps_its_own_resources() {
    const std::shared_ptr<Panel> root_a = make_control<Panel>(StableId("alone.a"));
    const std::shared_ptr<Panel> root_b = make_control<Panel>(StableId("alone.b"));
    Window a(root_a, {200.0, 100.0});
    Window b(root_b, {200.0, 100.0});
    require(a.resources() && b.resources() && !a.shares_resources_with(b),
            "windows built alone share resources");
    require((*a.resources()).window_count() == 1U, "a window alone is not its resources' only window");
    const ImageId image = a.load_png(png()).image;
    require(image.value != 0U && a.image_resources().find(image).has_value() &&
                !b.image_resources().find(image).has_value(),
            "an image leaked into another window's registry");
    const std::shared_ptr<ImageList> list = std::make_shared<ImageList>(a);
    require((*list).belongs_to(a) && !(*list).belongs_to(b), "an image list belongs to a window that does not share");
    bool null_refused = false;
    try {
        const std::shared_ptr<Panel> root_c = make_control<Panel>(StableId("alone.c"));
        Window c(root_c, {10.0, 10.0}, std::shared_ptr<ApplicationResources>{});
    } catch (const std::invalid_argument&) {
        null_refused = true;
    }
    require(null_refused, "a window accepted null resources");
}

void test_shared_images_and_image_lists() {
    const std::shared_ptr<ApplicationResources> shared = std::make_shared<ApplicationResources>();
    const std::shared_ptr<Panel> root_a = make_control<Panel>(StableId("shared.a"));
    const std::shared_ptr<Panel> root_b = make_control<Panel>(StableId("shared.b"));
    Window a(root_a, {300.0, 100.0}, shared);
    Window b(root_b, {300.0, 100.0}, shared);
    require(a.shares_resources_with(b) && (*shared).window_count() == 2U, "windows do not share resources");
    const ImageId image = a.load_png(png()).image;
    require(b.image_resources().find(image).has_value(), "an image loaded through one window is missing in the other");

    // An ImageList made for one window is accepted by a control in the other.
    const std::shared_ptr<ImageList> list = std::make_shared<ImageList>(a, Size{16.0, 16.0});
    require((*list).add_png("open", png()).image.value != 0U, "image list add refused");
    require((*list).belongs_to(b), "an image list does not belong to a sharing window");
    const std::shared_ptr<Button> button = make_control<Button>(StableId("shared.b.button"), "Open");
    (*button).set_requested_bounds({0.0, 0.0, 120.0, 30.0});
    (*root_b).add_child(button);
    (*button).set_image_list(list);
    (*button).set_image_key("open");
    require((*button).image_list() == list, "a sharing window's button refused the image list");

    // Theme: one set reaches every sharing window.
    unsigned b_changes = 0U;
    SubscriptionToken token = b.theme_changed().subscribe(CountThemeChanges{&b_changes});
    const std::shared_ptr<const Theme> theme = std::make_shared<Theme>(*default_theme());
    a.set_theme(theme);
    require(a.theme_ptr() == theme && b.theme_ptr() == theme && (*shared).theme_ptr() == theme,
            "the theme was not shared");
    require(b_changes == 1U, "the sharing window did not publish its theme change");
    a.set_theme(theme);
    require(b_changes == 1U, "setting the same theme again published a change");
    // A window joining later takes the shared theme.
    const std::shared_ptr<Panel> root_c = make_control<Panel>(StableId("shared.c"));
    Window c(root_c, {10.0, 10.0}, shared);
    require(c.theme_ptr() == theme, "a window joining the resources did not take their theme");
}

void test_moved_subtree_keeps_its_images() {
    const std::shared_ptr<ApplicationResources> shared = std::make_shared<ApplicationResources>();
    const std::shared_ptr<Panel> root_a = make_control<Panel>(StableId("move.a"));
    const std::shared_ptr<Panel> root_b = make_control<Panel>(StableId("move.b"));
    std::unique_ptr<Window> a = std::make_unique<Window>(root_a, Size{300.0, 200.0}, shared);
    Window b(root_b, {300.0, 200.0}, shared);

    // A panel with a keyed ImageList button and a picture box with a raw
    // image, built in window a.
    const std::shared_ptr<ImageList> list = std::make_shared<ImageList>(*a, Size{16.0, 16.0});
    const ImageId keyed = (*list).add_png("save", png()).image;
    const ImageId raw = (*a).load_png(png()).image;
    const std::shared_ptr<Panel> panel = make_control<Panel>(StableId("move.panel"));
    (*panel).set_requested_bounds({0.0, 0.0, 300.0, 200.0});
    const std::shared_ptr<Button> button = make_control<Button>(StableId("move.button"), "Save");
    (*button).set_requested_bounds({0.0, 0.0, 120.0, 30.0});
    (*button).set_image_list(list);
    (*button).set_image_key("save");
    (*button).set_text_image_relation(TextImageRelation::image_before_text);
    const std::shared_ptr<PictureBox> picture = make_control<PictureBox>(StableId("move.picture"));
    (*picture).set_requested_bounds({0.0, 40.0, 32.0, 32.0});
    (*picture).set_image(raw);
    (*panel).add_child(button);
    (*panel).add_child(picture);
    (*root_a).add_child(panel);
    (*a).perform_layout();
    ImagePainter before;
    (*button).on_paint(before, {0.0, 0.0, 120.0, 30.0});
    require(before.images.size() == 1U && before.images[0] == keyed, "button image not painted before the move");

    // Move the subtree to window b.
    const Control::Ptr moved = (*root_a).remove_child((*panel).runtime_id());
    require(moved == panel, "panel not removed");
    (*root_b).add_child(panel);
    b.perform_layout();
    require((*button).attached_window() == &b && (*button).image_list() == list,
            "the moved button lost its image list");
    require((*picture).image() == raw && b.image_resources().find(raw).has_value(),
            "the moved picture lost its image");
    ImagePainter after;
    (*button).on_paint(after, {0.0, 0.0, 120.0, 30.0});
    require(after.images.size() == 1U && after.images[0] == keyed, "the moved button no longer paints its image");
    ImagePainter picture_after;
    (*picture).on_paint(picture_after, {0.0, 0.0, 32.0, 32.0});
    require(picture_after.images.size() == 1U && picture_after.images[0] == raw,
            "the moved picture box no longer paints its image");

    // Window a closes: the images stay, and the ImageList made for it keeps
    // working through the resources.
    a.reset();
    require((*shared).window_count() == 1U && b.image_resources().find(keyed).has_value(),
            "images lost with the window they were loaded through");
    require((*list).bound_window() == nullptr && (*list).belongs_to(b), "image list lost after its window closed");
    require((*list).add_png("close", png()).image.value != 0U, "image list unusable after its window closed");
    ImagePainter late;
    (*button).on_paint(late, {0.0, 0.0, 120.0, 30.0});
    require(late.images.size() == 1U && late.images[0] == keyed, "button image lost after the source window closed");
    (*list).dispose();
    require(!b.image_resources().find(keyed).has_value(), "disposing the image list did not release its images");
    require(b.image_resources().find(raw).has_value(), "disposing the image list released another image");
}

// A run in which a floating window is built with the main window's
// resources and a subtree with images is moved into it.
struct ShareReady final {
    std::shared_ptr<ApplicationResources>* resources{};
    Window** model{};
    ApplicationWindowHandle* handle{};
    void operator()(Window& window, ApplicationWindowHandle value) const {
        *resources = window.resources();
        *model = &window;
        *handle = value;
    }
};

void test_runtime_window_shares_the_main_window_resources() {
    std::shared_ptr<ApplicationResources> resources{};
    Window* main_model = nullptr;
    ApplicationWindowHandle main{};
    const std::shared_ptr<Panel> main_root = make_control<Panel>(StableId("run.main"));
    std::vector<ApplicationWindow> windows;
    ApplicationWindow primary{};
    primary.stable_id = "main";
    primary.model = std::make_unique<Window>(main_root, Size{400.0, 300.0});
    primary.options.initial_size = {400.0, 300.0};
    primary.options.ready = ShareReady{&resources, &main_model, &main};
    windows.push_back(std::move(primary));
    host::HeadlessApplication application(std::move(windows));
    require(resources && main_model != nullptr, "main window resources unavailable");

    const ImageId image = (*main_model).load_png(png()).image;
    const std::shared_ptr<PictureBox> picture = make_control<PictureBox>(StableId("run.picture"));
    (*picture).set_requested_bounds({0.0, 0.0, 32.0, 32.0});
    (*picture).set_image(image);
    (*main_root).add_child(picture);

    // The floating window's model is built with the main window's resources.
    const std::shared_ptr<Panel> floating_root = make_control<Panel>(StableId("run.floating"));
    ApplicationWindow floating{};
    floating.stable_id = "floating";
    floating.owner_id = "main";
    floating.floating = HostWindowFloating::tool;
    floating.model = std::make_unique<Window>(floating_root, Size{200.0, 200.0}, resources);
    floating.options.initial_size = {200.0, 200.0};
    const ApplicationWindowOpenResult opened = main.windows().open_window(std::move(floating));
    require(opened.status.accepted(), "floating window refused");
    Window& floating_model = *application.model(opened.window.id());
    require(floating_model.shares_resources_with(*main_model), "runtime window does not share the main resources");
    static_cast<void>((*main_root).remove_child((*picture).runtime_id()));
    (*floating_root).add_child(picture);
    require((*picture).attached_window() == &floating_model &&
                floating_model.image_resources().find(image).has_value(),
            "the moved picture's image is missing in the floating window");
    floating_model.perform_layout();
    ImagePainter painted;
    (*picture).on_paint(painted, {0.0, 0.0, 32.0, 32.0});
    require(painted.images.size() == 1U && painted.images[0] == image,
            "the picture moved into the floating window does not paint its image");
}
} // namespace

// A program theme package compiled through the resources loads its image
// into their registry, so every sharing window draws it; releasing the theme
// removes the image, and a refused package leaves none.
void test_a_package_theme_compiles_into_the_shared_registry() {
    const std::shared_ptr<ApplicationResources> resources = std::make_shared<ApplicationResources>();
    const std::string sheet = "button::part(face) { background-image: url(\"resource:icon.check\") }";
    const std::string image(reinterpret_cast<const char*>(one_pixel_png.data()), one_pixel_png.size());
    ThemePackage package{};
    package.manifest =
        "{\"format\": \"gui-forms-theme/1\", \"name\": \"shared\", \"version\": \"1.0.0\", \"engine\": \">=0.3\", "
        "\"sheet\": {\"path\": \"theme.theme.css\", \"sha256\": \"" +
        theme_language::package_sha256_hex(theme_language::package_sha256(sheet)) +
        "\"}, \"resources\": [{\"name\": \"icon.check\", \"path\": \"check.png\", \"sha256\": \"" +
        theme_language::package_sha256_hex(theme_language::package_sha256(image)) + "\"}]}";
    package.files.push_back(ThemePackageFile{"check.png", image});
    package.files.push_back(ThemePackageFile{"theme.theme.css", sheet});
    const ThemeLibrary library{};
    const std::size_t before = (*resources).images().image_ids().size();
    ThemeCompileResult compiled = (*resources).compile_theme_package(package, library, ThemeCompileOptions{});
    require(compiled.succeeded(), "the package compiles through the resources");
    require((*resources).images().image_ids().size() == before + 1U, "its image is in the shared registry");
    package.files.back().bytes = sheet + "\nbutton { opacity: 2 }";
    const ThemeCompileResult refused = (*resources).compile_theme_package(package, library, ThemeCompileOptions{});
    require(!refused.succeeded(), "a package whose sheet no longer matches its manifest is refused");
    require((*resources).images().image_ids().size() == before + 1U, "a refusal leaves no image");
    compiled = ThemeCompileResult::refused({});
    require((*resources).images().image_ids().size() == before, "releasing the theme removes its image");
}

int main() {
    try {
        test_a_window_alone_keeps_its_own_resources();
        test_a_package_theme_compiles_into_the_shared_registry();
        test_shared_images_and_image_lists();
        test_moved_subtree_keeps_its_images();
        test_runtime_window_shares_the_main_window_resources();
        std::cout << "gui_forms_shared_resources_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_shared_resources_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
