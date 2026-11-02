// GF_TABLE_CUSTOM_CONTROLS: a custom control's paint callback fills pixels
// at the window's scale, an invalidated control paints again with damage
// covering what was invalidated, and the control presents the pixels as a
// live surface over its client area.
//
// The program links the ABI's objects and the C++ library statically: it
// makes the control through gf_abi_get_table alone, then finds the C++
// control behind the handle and paints a window of it with a painter that
// records the live surface it is given.

#include "gui_forms/c/gf_abi.h"

#include "../../../src/abi/stable/engine/abi_engine.hpp"
#include "../../../src/abi/stable/registry/abi_registry.hpp"

#include "gui_forms/control.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/window.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

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

gf_utf8_view text_view(std::string_view text) {
    gf_utf8_view view{};
    view.data = text.data();
    view.size = text.size();
    return view;
}

// What the paint callback was given, and the byte it fills its damage with.
struct PaintRecord final {
    std::uint32_t paints{};
    std::uint32_t releases{};
    std::uint8_t fill{};
    gf_object control{};
    std::uint32_t width{};
    std::uint32_t height{};
    double scale{};
    gf_logical_rect damage{};
};

// Fills the damage's pixels with record.fill, every channel.
void GF_CALL paint_damage(void* context, gf_object control, const gf_paint_target* target) {
    PaintRecord& record = *static_cast<PaintRecord*>(context);
    record.paints = record.paints + 1U;
    record.control = control;
    record.width = (*target).width;
    record.height = (*target).height;
    record.scale = (*target).scale;
    record.damage = (*target).damage;
    const std::uint32_t left = static_cast<std::uint32_t>((*target).damage.x * (*target).scale);
    const std::uint32_t top = static_cast<std::uint32_t>((*target).damage.y * (*target).scale);
    const std::uint32_t right = static_cast<std::uint32_t>(((*target).damage.x + (*target).damage.width) *
                                                           (*target).scale);
    const std::uint32_t bottom = static_cast<std::uint32_t>(((*target).damage.y + (*target).damage.height) *
                                                            (*target).scale);
    for (std::uint32_t row = top; row < bottom && row < (*target).height; ++row) {
        std::uint8_t* line = (*target).pixels + static_cast<std::size_t>(row) * (*target).row_bytes;
        const std::uint32_t end = right < (*target).width ? right : (*target).width;
        if (end > left) {
            std::memset(line + static_cast<std::size_t>(left) * 4U, record.fill,
                        static_cast<std::size_t>(end - left) * 4U);
        }
    }
}

gf_status GF_CALL no_property(void*, gf_object, std::uint64_t, gf_value*) {
    return GF_STATUS_NOT_FOUND;
}

void GF_CALL count_release(void* context) {
    PaintRecord& record = *static_cast<PaintRecord*>(context);
    record.releases = record.releases + 1U;
}

// Records the live surface a paint presents and where; draws nothing else.
class SurfacePainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void clip_rounded_rect(Rect, double) override {}
    void begin_group(const PaintGroup&) override {}
    void end_group() override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override {}
    void fill_radial_gradient(Rect, Point, Size, std::span<const GradientStop>) override {}
    void draw_box_shadow(Rect, double, Point, double, double, Color) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_image_region(ImageId, Rect, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>&, Point, double) override {}
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>&, Rect, double) override {}
    void draw_live_surface(std::shared_ptr<LiveSurface> surface, Rect destination, double) override {
        surfaces = surfaces + 1U;
        last_surface = std::move(surface);
        last_destination = destination;
    }

    std::uint32_t surfaces{};
    std::shared_ptr<LiveSurface> last_surface;
    Rect last_destination{};
};

// The first channel of the pixel at (x, y) of the surface's newest frame.
std::uint8_t frame_byte(const LiveSurface& surface, std::uint32_t x, std::uint32_t y) {
    const LiveSurfaceFrame frame = surface.acquire_latest();
    if (!frame || x >= frame.width()) {
        return 0U;
    }
    const std::span<const std::byte> pixels = frame.pixels();
    const std::size_t offset = static_cast<std::size_t>(y) * frame.row_bytes() + static_cast<std::size_t>(x) * 4U;
    if (offset >= pixels.size()) {
        return 0U;
    }
    return static_cast<std::uint8_t>(pixels[offset]);
}

constexpr Size client_size{120.0, 80.0};
constexpr double window_scale = 2.0;

void run() {
    gf_custom_controls_table custom{};
    require_ok(gf_abi_get_table(GF_TABLE_CUSTOM_CONTROLS, GF_ABI_MAJOR, GF_ABI_MINOR, &custom, sizeof(custom)),
               "custom controls table");
    gf_controls_table controls{};
    require_ok(gf_abi_get_table(GF_TABLE_CONTROLS, GF_ABI_MAJOR, GF_ABI_MINOR, &controls, sizeof(controls)),
               "controls table");
    gf_objects_table objects{};
    require_ok(gf_abi_get_table(GF_TABLE_OBJECTS, GF_ABI_MAJOR, GF_ABI_MINOR, &objects, sizeof(objects)),
               "objects table");

    PaintRecord record{};
    gf_custom_control_spec spec{};
    spec.struct_size = sizeof(gf_custom_control_spec);
    spec.base = GF_CUSTOM_BASE_SCROLLABLE_CONTROL;
    spec.property_size = sizeof(gf_custom_property);
    spec.callbacks.struct_size = sizeof(gf_custom_control_callbacks);
    spec.callbacks.context = &record;
    spec.callbacks.paint = &paint_damage;
    spec.callbacks.get_property = &no_property;
    spec.callbacks.release = &count_release;
    gf_object object = 0U;
    require_ok(custom.create(&spec, text_view("custom.paint"), &object), "create");

    Control::Ptr control;
    require_ok(abi1::registry().find_object(object, abi1::SlotIdentity{GF_TABLE_OBJECTS, 0U}, control),
               "find_object");
    std::unique_ptr<Window> window = abi1::make_run_window(control, client_size);
    (*window).set_scale(window_scale);

    record.fill = 0x11U;
    SurfacePainter first;
    static_cast<void>((*window).paint(first, Rect{0.0, 0.0, client_size.width, client_size.height}));
    require(record.paints == 1U, "the first paint calls the paint callback");
    require(record.control == object, "the callback receives the control's identity");
    require(record.width == 240U && record.height == 160U, "the pixels are the client area at the window's scale");
    require(record.scale == window_scale, "the callback is told the scale");
    require(record.damage.x == 0.0 && record.damage.y == 0.0 && record.damage.width == client_size.width &&
                record.damage.height == client_size.height,
            "a new surface is damaged whole");
    require(first.surfaces == 1U && first.last_surface != nullptr, "the paint presents one live surface");
    require(first.last_destination.width == client_size.width &&
                first.last_destination.height == client_size.height,
            "the surface covers the client area");
    if (first.last_surface != nullptr) {
        require(frame_byte(*first.last_surface, 10U, 10U) == 0x11U, "the published frame holds the paint");
    }

    record.fill = 0x22U;
    const gf_logical_rect damage{40.0, 20.0, 20.0, 10.0};
    require_ok(controls.invalidate(object, &damage), "invalidate");
    SurfacePainter second;
    static_cast<void>((*window).paint(second, Rect{40.0, 20.0, 20.0, 10.0}));
    require(record.paints == 2U, "a damaged paint calls the paint callback again");
    // A window records a control's paint whole, so the damage may be the
    // whole control; it covers what was invalidated.
    require(record.damage.x <= 40.0 && record.damage.y <= 20.0 && record.damage.x + record.damage.width >= 60.0 &&
                record.damage.y + record.damage.height >= 30.0,
            "the callback's damage covers what was invalidated");
    if (second.last_surface != nullptr) {
        require(frame_byte(*second.last_surface, 100U, 50U) == 0x22U, "the frame holds the second paint");
    }

    // Where the library has the Windows paint endpoint, a bound endpoint's
    // surface is shown in place of the paint callback, until it is released.
    gf_windows_paint_endpoint_table endpoints{};
    if (gf_abi_get_table(GF_TABLE_WINDOWS_PAINT_ENDPOINT, GF_ABI_MAJOR, GF_ABI_MINOR, &endpoints,
                         sizeof(endpoints)) == GF_STATUS_OK) {
        gf_paint_endpoint endpoint = 0U;
        std::uint64_t handle = 0U;
        require_ok(endpoints.acquire(object, 240U, 160U, &endpoint, &handle), "acquire");
        const std::uint32_t painted = record.paints;
        SurfacePainter bound;
        static_cast<void>((*window).paint(bound, Rect{0.0, 0.0, client_size.width, client_size.height}));
        require(record.paints == painted, "a bound endpoint replaces the paint callback");
        require(bound.surfaces == 1U && bound.last_surface != nullptr && bound.last_surface != second.last_surface,
                "the control shows the endpoint's surface");
        require_ok(endpoints.release(endpoint), "release the endpoint");
        SurfacePainter unbound;
        static_cast<void>((*window).paint(unbound, Rect{0.0, 0.0, client_size.width, client_size.height}));
        require(record.paints == painted + 1U, "a released endpoint gives the paint callback back");
    }

    window.reset();
    control.reset();
    require(record.releases == 0U, "the callbacks live while the caller holds the control");
    require_ok(objects.release(object), "release");
    require(record.releases == 1U, "the last reference releases the callbacks once");
}

} // namespace

int main() {
    try {
        run();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " checks failed\n";
        return 1;
    }
    std::cout << "custom control paint: all checks passed\n";
    return 0;
}
