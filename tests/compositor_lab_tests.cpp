// Stage T5, phase C8 (notes:planning/stages/T5-compositor/PLAN.md §5 C8 and
// §6): the compositor lab (demo/compositor_lab.hpp) in each compositor state
// a host may report, drawn on the Skia raster every host presents with.
//
// For each state and request the window is granted what the resolution
// gives, renders, and its frame's alpha follows the grant:
// - opaque: every pixel opaque;
// - transparent: the window's clear part clear, glass and reflections
//   partly clear over it, the scenery opaque;
// - a material: the clear part the material base's tint.
// Reduced transparency and high contrast make the grant opaque and drop
// the reflections. A run through a compositor stopping and starting, with
// the request standing, falls back and returns without error.
//
// Backdrop chains (owner, 2026-11-05, as revised): a window's blur is its
// granted material, saturate is dropped and reported, and the CPU paints
// only the tint. Overlays (menus, drop-downs, tooltips: composited into
// their owner, our own pixels) take no chain; they paint with the theme's
// materials over the window like any content (owner, 2026-11-04).
//
// Run with --print to print one JSON line per state and request (the
// headless part of the gate record): the state, the request, the grant,
// the reason and the frame's alpha (minimum, maximum, non-opaque count).
#include "compositor_lab.hpp"
#include "skia_raster.hpp"

#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;
namespace lab = gui_forms::compositor_lab;

bool print_mode = false;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_compositor_lab_tests: " << message << '\n';
    std::exit(1);
}

// One pixel, as straight RGBA order of the premultiplied BGRA8 frame.
struct Pixel final {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{};
};

// The alpha of a whole frame.
struct AlphaSummary final {
    std::uint8_t minimum{255U};
    std::uint8_t maximum{};
    std::size_t non_opaque{};
};

// One presented frame of the lab at scale 1, copied out of its framebuffer
// (`width` by `height` premultiplied BGRA8 pixels, rows packed).
struct Frame final {
    std::vector<std::uint8_t> bytes;
    std::size_t width{};
    std::size_t height{};

    [[nodiscard]] Pixel at(std::size_t column, std::size_t row) const {
        const std::size_t offset = (row * width + column) * 4U;
        const Pixel pixel{bytes[offset + 2U], bytes[offset + 1U], bytes[offset], bytes[offset + 3U]};
        return pixel;
    }

    [[nodiscard]] AlphaSummary alpha() const {
        AlphaSummary summary{};
        for (std::size_t offset = 3U; offset < bytes.size(); offset += 4U) {
            const std::uint8_t value = bytes[offset];
            if (value < summary.minimum) summary.minimum = value;
            if (value > summary.maximum) summary.maximum = value;
            if (value != 255U) summary.non_opaque = summary.non_opaque + 1U;
        }
        return summary;
    }
};

// Paints the whole window into the raster, as a host does (the raster is
// the window's painter, so the backplane is replayed in full: stage T5,
// C6), and copies its pixels out.
Frame present(render::SkiaRaster& raster, Window& window) {
    const Size size = lab::lab_size;
    const Rect whole{0.0, 0.0, size.width, size.height};
    static_cast<void>(raster.resize(size, 1.0));
    window.set_framebuffer_painter(&raster);
    window.perform_layout();
    DamageRegion damage;
    damage.add(whole);
    raster.begin_frame(damage);
    const std::optional<PaintReceipt> receipt = window.paint(raster, whole);
    raster.end_frame();
    require(receipt.has_value(), "the window renders");
    static_cast<void>(window.take_damage());

    Frame frame{};
    frame.width = static_cast<std::size_t>(size.width);
    frame.height = static_cast<std::size_t>(size.height);
    frame.bytes.resize(frame.width * frame.height * 4U);
    const std::uint8_t* const pixels = static_cast<const std::uint8_t*>(raster.pixels());
    const std::size_t row_bytes = raster.row_bytes();
    require(pixels != nullptr && row_bytes >= frame.width * 4U, "the raster holds the window at scale 1");
    for (std::size_t row = 0U; row < frame.height; ++row) {
        std::memcpy(frame.bytes.data() + row * frame.width * 4U, pixels + row * row_bytes, frame.width * 4U);
    }
    return frame;
}

// The compositor states a host may report.
enum class StateCase : std::uint8_t {
    every_backdrop,
    transparent_only,
    no_compositor,
    transparency_disabled,
    reduced_transparency,
    high_contrast,
};

constexpr std::array<StateCase, 6> state_cases{StateCase::every_backdrop,        StateCase::transparent_only,
                                                StateCase::no_compositor,         StateCase::transparency_disabled,
                                                StateCase::reduced_transparency, StateCase::high_contrast};

const char* state_case_name(StateCase state) noexcept {
    switch (state) {
    case StateCase::every_backdrop: return "every_backdrop";
    case StateCase::transparent_only: return "transparent_only";
    case StateCase::no_compositor: return "no_compositor";
    case StateCase::transparency_disabled: return "transparency_disabled";
    case StateCase::reduced_transparency: return "reduced_transparency";
    case StateCase::high_contrast: return "high_contrast";
    }
    return "unknown";
}

CompositorState make_state(StateCase state_case, std::uint64_t generation) {
    CompositorState state{};
    state.generation = CompositorGeneration{generation};
    state.compositing = CompositorPresence::present;
    for (std::size_t index = 0U; index < window_backdrop_count; ++index) {
        state.support[index] = WindowBackdropSupport::supported;
    }
    switch (state_case) {
    case StateCase::every_backdrop:
        // Every backdrop supported, as set above.
        break;
    case StateCase::transparent_only:
        // Windows 10 and Server 2022: no system materials.
        state.support[static_cast<std::size_t>(WindowBackdrop::window_material)] = WindowBackdropSupport::os_version;
        state.support[static_cast<std::size_t>(WindowBackdrop::transient_material)] =
            WindowBackdropSupport::os_version;
        break;
    case StateCase::no_compositor:
        state.compositing = CompositorPresence::absent;
        for (std::size_t index = 1U; index < window_backdrop_count; ++index) {
            state.support[index] = WindowBackdropSupport::no_compositor;
        }
        break;
    case StateCase::transparency_disabled: state.transparency_enabled = false; break;
    case StateCase::reduced_transparency: state.reduced_transparency = true; break;
    case StateCase::high_contrast: state.high_contrast = true; break;
    default:
        // A case outside the enumeration names no compositor state.
        require(false, "make_state is given a known state case");
        break;
    }
    return state;
}

// What a request should be granted in a state, with the reason.
struct Expected final {
    WindowBackdrop granted{WindowBackdrop::opaque};
    WindowBackdropReason reason{WindowBackdropReason::granted};
};

Expected expected_grant(StateCase state, const WindowBackdropRequest& request) {
    const WindowBackdrop preferred = request.preferred();
    Expected expected{};
    if (preferred == WindowBackdrop::opaque) return expected;
    switch (state) {
    case StateCase::every_backdrop: expected.granted = preferred; break;
    case StateCase::transparent_only: {
        const bool material = preferred == WindowBackdrop::window_material ||
                              preferred == WindowBackdrop::transient_material;
        if (!material) {
            expected.granted = preferred;
            break;
        }
        expected.reason = WindowBackdropReason::os_version;
        for (std::size_t index = 1U; index < request.length; ++index) {
            if (request.entries[index] == WindowBackdrop::transparent) expected.granted = WindowBackdrop::transparent;
        }
        break;
    }
    case StateCase::no_compositor: expected.reason = WindowBackdropReason::no_compositor; break;
    case StateCase::transparency_disabled: expected.reason = WindowBackdropReason::transparency_disabled; break;
    case StateCase::reduced_transparency: expected.reason = WindowBackdropReason::reduced_transparency; break;
    case StateCase::high_contrast: expected.reason = WindowBackdropReason::high_contrast; break;
    default:
        // A case outside the enumeration has no expected grant.
        require(false, "expected_grant is given a known state case");
        break;
    }
    return expected;
}

bool near(std::uint8_t observed, unsigned wanted) noexcept {
    const unsigned value = observed;
    const bool within = value + 2U >= wanted && value <= wanted + 2U;
    return within;
}

// Pixels the checks read, in window coordinates (demo/compositor_lab.cpp's
// layout): the clear part away from every part; the scenery; inside the
// glass over the clear part; just below the face mirror (its reflection)
// and beyond the reflection's reach; just below the backplane mirror, over
// the scenery's white cross band, and the band beside the mirror.
constexpr std::size_t clear_column = 540U;
constexpr std::size_t clear_row = 20U;
constexpr std::size_t scenery_column = 20U;
constexpr std::size_t scenery_row = 20U;
constexpr std::size_t glass_column = 420U;
constexpr std::size_t glass_row = 150U;
constexpr std::size_t face_reflection_column = 420U;
constexpr std::size_t face_reflection_row = 239U;
constexpr std::size_t past_reach_row = 252U;
constexpr std::size_t band_reflection_column = 140U;
constexpr std::size_t band_reflection_row = 240U;
constexpr std::size_t band_beside_column = 20U;

// The material bases' alpha (demo/compositor_lab.cpp).
constexpr unsigned window_material_alpha = 72U;
constexpr unsigned transient_material_alpha = 104U;

void check_frame(const Frame& frame, const WindowBackdropGrant& grant, bool reflections) {
    const AlphaSummary alpha = frame.alpha();
    const Pixel scenery = frame.at(scenery_column, scenery_row);
    require(scenery.alpha == 255U, "the scenery is opaque under every grant");
    const Pixel clear = frame.at(clear_column, clear_row);
    const Pixel glass = frame.at(glass_column, glass_row);
    const Pixel below_face = frame.at(face_reflection_column, face_reflection_row);
    const Pixel past_reach = frame.at(face_reflection_column, past_reach_row);
    switch (grant.granted) {
    case WindowBackdrop::opaque:
        require(alpha.minimum == 255U && alpha.non_opaque == 0U, "an opaque grant presents every pixel opaque");
        break;
    case WindowBackdrop::transparent:
        require(clear.alpha == 0U, "over transparent the clear part is clear");
        require(glass.alpha > 0U && glass.alpha < 255U, "glass over the clear part is partly clear");
        require(past_reach.alpha == 0U, "past the reflection's reach the clear part is clear");
        require(below_face.alpha > 0U, "the face's reflection adds coverage over the clear part");
        break;
    case WindowBackdrop::window_material:
        require(near(clear.alpha, window_material_alpha), "over the window material the clear part is its base's tint");
        break;
    case WindowBackdrop::transient_material:
        require(near(clear.alpha, transient_material_alpha),
                "over the transient material the clear part is its base's tint");
        break;
    default:
        // A grant names one of the four backdrops; anything else is a defect.
        require(false, "the grant names a known backdrop");
        break;
    }
    const Pixel band_reflected = frame.at(band_reflection_column, band_reflection_row);
    const Pixel band_beside = frame.at(band_beside_column, band_reflection_row);
    const bool band_differs = band_reflected.red != band_beside.red || band_reflected.green != band_beside.green ||
                              band_reflected.blue != band_beside.blue;
    if (reflections) {
        require(band_differs, "the backplane mirror reflects the scenery over the white band");
    } else {
        require(!band_differs, "reflections are dropped under reduced transparency and high contrast");
    }
}

void print_record(StateCase state_case, const Window& window, const Frame& frame) {
    const AlphaSummary alpha = frame.alpha();
    const std::string record = lab::grant_record(window, window.backdrop_grant());
    // grant_record ends with '}': the case and the frame's alpha join it.
    std::printf("{\"case\":\"%s\",\"record\":%s,\"alpha\":{\"minimum\":%u,\"maximum\":%u,\"non_opaque\":%zu}}\n",
                state_case_name(state_case), record.c_str(), static_cast<unsigned>(alpha.minimum),
                static_cast<unsigned>(alpha.maximum), alpha.non_opaque);
}

// Every request the lab is run with by tools/t5_gate_record.py.
std::array<WindowBackdropRequest, 4> lab_requests() {
    const std::array<WindowBackdropRequest, 4> requests{
        lab::default_request(),
        WindowBackdropRequest::with_fallback(WindowBackdrop::transient_material, WindowBackdrop::transparent),
        WindowBackdropRequest::preferring(WindowBackdrop::transparent),
        WindowBackdropRequest::preferring(WindowBackdrop::opaque)};
    return requests;
}

void each_state_and_request(render::SkiaRaster& raster) {
    const std::array<WindowBackdropRequest, 4> requests = lab_requests();
    for (const StateCase state_case : state_cases) {
        for (const WindowBackdropRequest& request : requests) {
            const std::unique_ptr<Window> window = lab::make_compositor_lab(request, false);
            require((*window).non_opaque_declared(), "the lab declares its window non-opaque");
            // The state's preferences reach the window's presentation
            // settings as a host with presentation_preferences gives them.
            (*window).set_compositor_state(make_state(state_case, 1U), true);
            const WindowBackdropGrant& grant = (*window).backdrop_grant();
            const Expected expected = expected_grant(state_case, request);
            require(grant.granted == expected.granted, "the lab is granted what the state allows");
            require(grant.reason == expected.reason, "the grant gives the reason the state implies");
            const Frame frame = present(raster, *window);
            const bool reflections =
                state_case != StateCase::reduced_transparency && state_case != StateCase::high_contrast;
            check_frame(frame, grant, reflections);
            if (print_mode) print_record(state_case, *window, frame);
        }
    }
}

// A compositor stopping and starting with the request standing: transparent
// is granted, falls back to opaque and returns, each frame rendered.
void compositor_stops_and_starts(render::SkiaRaster& raster) {
    const std::unique_ptr<Window> window =
        lab::make_compositor_lab(WindowBackdropRequest::preferring(WindowBackdrop::transparent), false);
    Window& shown = *window;
    shown.set_compositor_state(make_state(StateCase::every_backdrop, 1U), true);
    require(shown.backdrop_grant().granted == WindowBackdrop::transparent, "transparent is granted");
    check_frame(present(raster, shown), shown.backdrop_grant(), true);
    shown.set_compositor_state(make_state(StateCase::no_compositor, 2U), true);
    require(shown.backdrop_grant().granted == WindowBackdrop::opaque, "without a compositor the window is opaque");
    check_frame(present(raster, shown), shown.backdrop_grant(), true);
    shown.set_compositor_state(make_state(StateCase::every_backdrop, 3U), true);
    require(shown.backdrop_grant().granted == WindowBackdrop::transparent, "transparent returns with the compositor");
    check_frame(present(raster, shown), shown.backdrop_grant(), true);
}

void parsing() {
    require(lab::parse_backdrop_request("") == lab::default_request(), "no --backdrop is the default request");
    const std::optional<WindowBackdropRequest> two = lab::parse_backdrop_request("transient_material,transparent");
    require(two.has_value() && *two == WindowBackdropRequest::with_fallback(WindowBackdrop::transient_material,
                                                                            WindowBackdrop::transparent),
            "a preferred backdrop and a fallback parse");
    require(!lab::parse_backdrop_request("mica").has_value(), "an unknown name is refused");
    require(!lab::parse_backdrop_request("opaque,transparent").has_value(), "opaque only last");
    require(!lab::parse_backdrop_request("transparent,transparent").has_value(), "no entry twice");
    require(!lab::parse_backdrop_request("transparent,").has_value(), "an empty entry is refused");
    require(!lab::parse_backdrop_request("transparent,window_material,transient_material,opaque").has_value(),
            "at most three entries");
}


// Backdrop chains (owner, 2026-11-05, as revised). GUI.Forms never blurs
// what lies outside a window's own surface: a window's blur is its granted
// system material. An overlay is the window's own pixels and takes no chain.
constexpr Rect overlay_bounds{8.0, 8.0, 128.0, 96.0};
constexpr std::size_t overlay_column = 40U;
constexpr std::size_t overlay_row = 40U;
constexpr Color frosted_tint = Color::rgba(255, 255, 255, 64);

BackdropChain frosted_chain() {
    BackdropChain chain{};
    static_cast<void>(chain.append(BackdropOperation::blur(8.0)));
    static_cast<void>(chain.append(BackdropOperation::saturate(1.5)));
    static_cast<void>(chain.append(BackdropOperation::tint(frosted_tint)));
    return chain;
}

// A clear overlay.
std::shared_ptr<Panel> make_overlay() {
    std::shared_ptr<Panel> overlay = make_control<Panel>(StableId("lab.backdrop.overlay"));
    (*overlay).set_background(Color::rgba(0, 0, 0, 0));
    (*overlay).set_requested_bounds(overlay_bounds);
    return overlay;
}

bool same_pixel(const Pixel& left, const Pixel& right) noexcept {
    return left.red == right.red && left.green == right.green && left.blue == right.blue && left.alpha == right.alpha;
}

// A window's chain is the window's base only: a clear overlay over it adds
// no second tint (owner, 2026-11-04: overlays are the window's own pixels).
void overlay_takes_no_chain(render::SkiaRaster& raster) {
    const std::unique_ptr<Window> window = lab::make_compositor_lab(lab::default_request(), false);
    Window& shown = *window;
    shown.set_compositor_state(make_state(StateCase::every_backdrop, 1U), true);
    shown.set_backdrop_chain(frosted_chain());
    const Frame before = present(raster, shown);
    // The window observes an overlay root; the caller owns it while it shows.
    const std::shared_ptr<Panel> overlay = make_overlay();
    OverlayToken token = shown.open_overlay(shown.root(), overlay, OverlayOptions{});
    const Frame after = present(raster, shown);
    require(same_pixel(after.at(overlay_column, overlay_row), before.at(overlay_column, overlay_row)),
            "a clear overlay shows the window beneath it, with no tint of its own");
    token.disconnect();
}

// A top-level window: the blur is the granted material, the CPU paints
// only the tint, saturate is dropped and reported.
void window_chain_resolution(render::SkiaRaster& raster) {
    const std::unique_ptr<Window> window = lab::make_compositor_lab(lab::default_request(), false);
    Window& shown = *window;
    shown.set_compositor_state(make_state(StateCase::every_backdrop, 1U), true);
    shown.set_backdrop_chain(frosted_chain());
    require(shown.backdrop_grant().granted == WindowBackdrop::window_material, "the lab is granted its material");
    const std::optional<BackdropChainResolution> material = shown.backdrop_chain_resolution();
    require(material.has_value() && (*material).blur_by_material && !(*material).blur_dropped &&
                (*material).saturate_dropped && !(*material).solid && !(*material).withdrawn &&
                (*material).tint == frosted_tint,
            "under a material the blur is the system's, saturate is dropped, the tint is ours");
    const Frame granted = present(raster, shown);
    require(near(granted.at(clear_column, clear_row).alpha, 64U), "the clear part is the chain's tint alone");
    // Without a compositor: opaque, the blur dropped, the tint opaque.
    shown.set_compositor_state(make_state(StateCase::no_compositor, 2U), true);
    const std::optional<BackdropChainResolution> refused = shown.backdrop_chain_resolution();
    require(refused.has_value() && (*refused).solid && (*refused).blur_dropped && !(*refused).blur_by_material,
            "without a compositor the chain is its tint, opaque");
    const Pixel flat = present(raster, shown).at(clear_column, clear_row);
    require(flat.red == 255U && flat.green == 255U && flat.blue == 255U && flat.alpha == 255U,
            "the opaque fallback paints the tint opaque");
    shown.set_compositor_state(make_state(StateCase::high_contrast, 3U), true);
    const std::optional<BackdropChainResolution> contrast = shown.backdrop_chain_resolution();
    require(contrast.has_value() && (*contrast).withdrawn, "under high contrast the chain is withdrawn");
}

void chain_rules() {
    const BackdropChain chain = frosted_chain();
    require(backdrop_chain_valid(chain) && backdrop_chain_blur(chain) == 8.0, "the frosted chain is valid");
    require(backdrop_chain_solid(chain) == frosted_tint, "the solid fallback is the tint");
    BackdropChain two_blurs = chain;
    static_cast<void>(two_blurs.append(BackdropOperation::blur(2.0)));
    require(!backdrop_chain_valid(two_blurs), "at most one blur");
    BackdropChain too_wide{};
    static_cast<void>(too_wide.append(BackdropOperation::blur(backdrop_maximum_blur + 1.0)));
    require(!backdrop_chain_valid(too_wide), "a blur past the largest radius is refused");
    const std::unique_ptr<Window> window =
        lab::make_compositor_lab(WindowBackdropRequest::preferring(WindowBackdrop::opaque), false);
    Window& shown = *window;
    bool refused = false;
    try {
        shown.set_backdrop_chain(two_blurs);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && !shown.backdrop_chain().has_value(), "a window refuses an invalid chain, changing nothing");
    // A window following its theme asks for the material once its chain
    // has a blur.
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("lab.backdrop.plain"));
    Window plain(root, Size{200.0, 100.0});
    static_cast<void>(plain.declare_non_opaque());
    plain.set_backdrop_chain(chain);
    require(plain.backdrop_request() ==
                WindowBackdropRequest::with_fallback(WindowBackdrop::window_material, WindowBackdrop::transparent),
            "a chain with a blur asks for the window material");
}

} // namespace

int main(int argc, char** argv) {
    print_mode = argc > 1 && std::strcmp(argv[1], "--print") == 0;
    parsing();
    chain_rules();
    {
        render::SkiaRaster raster;
        each_state_and_request(raster);
        compositor_stops_and_starts(raster);
        overlay_takes_no_chain(raster);
        window_chain_resolution(raster);
    }
    std::cout << "gui_forms_compositor_lab_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
