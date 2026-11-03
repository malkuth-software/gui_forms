#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/host/compositor/window_backdrop.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

// The presented frame of a window by its backdrop grant (stage T5, phase
// C1; notes:planning/stages/T5-compositor/PLAN.md §3), through a host painter's
// framebuffer: part_composite_identity_tests runs it on the Skia raster,
// which every host draws with (stage SK3).
//
// The theme's window material is a translucent tint, so that the frame shows
// what it starts from. Each frame first fills its framebuffer with a stale
// opaque colour, standing for the previous frame. An opaque window blends
// its backplane over that colour, exactly as before stage T5; a window
// granted a non-opaque backdrop replaces it, so that only the tint remains.
namespace gui_forms::compositor_presentation {

inline constexpr Size window_size{40.0, 30.0};
inline constexpr Rect window_rect{0.0, 0.0, 40.0, 30.0};
// Premultiplied, the tint is (20, 40, 60) at alpha 128.
inline constexpr Color window_tint = Color::rgba(40, 80, 120, 128);
inline constexpr Color stale_frame = Color::rgba(250, 0, 0, 255);

// The professional theme with every ordinary window recipe a plain tint.
inline std::shared_ptr<const Theme> tinted_theme() {
    const std::unique_ptr<ThemeDefinition> held(new ThemeDefinition(windows_professional_theme_definition()));
    ThemeDefinition& definition = *held;
    SurfaceMaterial tint{};
    tint.fills.clear();
    tint.fills.push_back(MaterialFillLayer::solid(window_tint));
    ControlRoleRecipes& window = definition.roles[static_cast<std::size_t>(ControlVisualRole::window)];
    for (ControlVisualRecipe& recipe : window.ordinary) recipe.material = tint;
    std::shared_ptr<const Theme> theme = Theme::create(definition);
    return theme;
}

// A compositor state in which every backdrop is supported.
inline CompositorState transparent_state() {
    CompositorState state{};
    state.generation = CompositorGeneration{1U};
    state.compositing = CompositorPresence::present;
    for (std::size_t index = 0U; index < window_backdrop_count; ++index) {
        state.support[index] = WindowBackdropSupport::supported;
    }
    return state;
}

// One pixel of a frame, as RGBA.
struct FramePixel final {
    std::array<std::uint8_t, 4> rgba{};
    bool painted{};
};

// Paints `window` into a new framebuffer of `factory`'s kind, at scale 1,
// after filling it with the stale frame, and reads the pixel at the
// window's centre.
inline FramePixel present_once(Painter& factory, Window& window) {
    FramePixel result{};
    const std::unique_ptr<PaintFramebuffer> framebuffer = factory.create_framebuffer(window_size, 1.0);
    if (framebuffer == nullptr) return result;
    const ImageRegistry images{};
    if (!(*framebuffer).begin(images, window_rect)) return result;
    Painter& painter = (*framebuffer).painter();
    painter.fill_rect(window_rect, stale_frame);
    const std::optional<PaintReceipt> receipt = window.paint(painter, window_rect);
    (*framebuffer).end();
    if (!receipt.has_value()) return result;
    const std::size_t column = static_cast<std::size_t>(window_size.width / 2.0);
    const std::size_t row = static_cast<std::size_t>(window_size.height / 2.0);
    const std::span<std::byte> bytes = (*framebuffer).pixels();
    const std::size_t offset = row * (*framebuffer).row_bytes() + column * 4U;
    if (offset + 4U > bytes.size()) return result;
    const std::array<std::uint8_t, 4> stored{
        static_cast<std::uint8_t>(bytes[offset]), static_cast<std::uint8_t>(bytes[offset + 1U]),
        static_cast<std::uint8_t>(bytes[offset + 2U]), static_cast<std::uint8_t>(bytes[offset + 3U])};
    // The framebuffer is premultiplied BGRA8.
    result.rgba = {stored[2], stored[1], stored[0], stored[3]};
    result.painted = true;
    return result;
}

// Whether a channel is within two steps of what it should be: the painters
// round a translucent fill differently by a step.
inline bool near_channel(std::uint8_t observed, unsigned wanted) {
    const unsigned value = observed;
    const bool within = value + 2U >= wanted && value <= wanted + 2U;
    return within;
}

// What the check found; `failure` names the first expectation not met.
struct PresentationCheck final {
    bool passed{};
    const char* failure{};
};

// The check. `keeps_alpha` says whether the framebuffer holds alpha (Skia)
// or is opaque (the Windows DIB until phase C2), where alpha is 255 and the
// colour is the premultiplied one.
inline PresentationCheck check_presentation(Painter& factory, bool keeps_alpha) {
    PresentationCheck check{};
    const std::shared_ptr<Control> root = make_control<Control>(StableId("root"));
    Window window(root, window_size);
    window.set_theme(tinted_theme());

    // Opaque: the tint over the stale frame, (20, 40, 60) plus the stale
    // red at 127/255.
    const FramePixel opaque = present_once(factory, window);
    if (!opaque.painted) {
        check.failure = "the opaque frame is painted";
        return check;
    }
    if (!near_channel(opaque.rgba[0], 144U) || !near_channel(opaque.rgba[1], 40U) ||
        !near_channel(opaque.rgba[2], 60U)) {
        check.failure = "an opaque window blends its backplane over the previous frame, as before";
        return check;
    }
    if (keeps_alpha && opaque.rgba[3] != 255U) {
        check.failure = "an opaque window presents alpha 255";
        return check;
    }

    // Granted transparent: the frame starts clear, so only the tint shows.
    if (!window.declare_non_opaque()) {
        check.failure = "a window without a host may be declared non-opaque";
        return check;
    }
    window.set_compositor_state(transparent_state(), false);
    static_cast<void>(window.request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::transparent)));
    if (window.backdrop_grant().granted != WindowBackdrop::transparent) {
        check.failure = "the window is granted transparent";
        return check;
    }
    const FramePixel cleared = present_once(factory, window);
    const unsigned tint_alpha = keeps_alpha ? 128U : 255U;
    if (!cleared.painted || !near_channel(cleared.rgba[0], 20U) || !near_channel(cleared.rgba[1], 40U) ||
        !near_channel(cleared.rgba[2], 60U) || !near_channel(cleared.rgba[3], tint_alpha)) {
        check.failure = "a window granted transparent starts its frame clear";
        return check;
    }

    // Withdrawn: opaque again, byte for byte the first frame.
    static_cast<void>(window.request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::opaque)));
    const FramePixel restored = present_once(factory, window);
    if (!restored.painted || restored.rgba != opaque.rgba) {
        check.failure = "an opaque grant again presents as before";
        return check;
    }
    check.passed = true;
    return check;
}

} // namespace gui_forms::compositor_presentation
