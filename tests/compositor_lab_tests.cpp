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
// A popup's backdrop (owner, 2026-11-05, option 2) is applied to the
// window's finished pixels under it: computed once, drawn from its cache,
// recomputed only where the window's own damage reaches it, never computed
// under opaque content, its solid tint under reduced transparency and
// absent under high contrast. Run with --measure to print its frame cost
// by blur radius and popup size (one JSON line each).
//
// Run with --print to print one JSON line per state and request (the
// headless part of the gate record): the state, the request, the grant,
// the reason and the frame's alpha (minimum, maximum, non-opaque count).
#include "compositor_lab.hpp"
#include "skia_raster.hpp"

#include "gui_forms/gui_forms.hpp"

#include <algorithm>
#include <array>
#include <chrono>
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


// A popup's backdrop (owner, 2026-11-05, option 2) over the lab's scenery.
// The popup is a window: its chain is applied to the window's finished
// pixels under it, cached, recomputed only where damage of the window's own
// content reaches it, and computed only where the popup's content is clear.
constexpr Rect popup_bounds{8.0, 8.0, 128.0, 96.0};
constexpr std::size_t popup_column = 40U;
constexpr std::size_t popup_row = 40U;
constexpr std::size_t outside_column = 300U;
constexpr std::size_t outside_row = 300U;

BackdropChain frosted_chain() {
    BackdropChain chain{};
    static_cast<void>(chain.append(BackdropOperation::blur(8.0)));
    static_cast<void>(chain.append(BackdropOperation::saturate(1.5)));
    static_cast<void>(chain.append(BackdropOperation::tint(Color{255U, 255U, 255U, 64U})));
    return chain;
}

// The popup: clear unless `opaque_child`, then an opaque panel covers it.
std::shared_ptr<Panel> make_popup(bool opaque_child) {
    std::shared_ptr<Panel> popup = make_control<Panel>(StableId("lab.backdrop.popup"));
    (*popup).set_background(Color{0U, 0U, 0U, 0U});
    (*popup).set_requested_bounds(popup_bounds);
    if (opaque_child) {
        std::shared_ptr<Panel> cover = make_control<Panel>(StableId("lab.backdrop.cover"));
        (*cover).set_background(Color{40U, 80U, 120U, 255U});
        (*cover).set_requested_bounds(Rect{0.0, 0.0, popup_bounds.width, popup_bounds.height});
        (*popup).add_child(cover);
    }
    return popup;
}

bool same_pixel(const Pixel& left, const Pixel& right) noexcept {
    return left.red == right.red && left.green == right.green && left.blue == right.blue && left.alpha == right.alpha;
}

void clear_popup_backdrop(render::SkiaRaster& raster) {
    const std::unique_ptr<Window> window =
        lab::make_compositor_lab(WindowBackdropRequest::preferring(WindowBackdrop::opaque), false);
    Window& shown = *window;
    shown.set_compositor_state(make_state(StateCase::every_backdrop, 1U), true);
    const Frame before = present(raster, shown);
    const std::shared_ptr<Panel> popup = make_popup(false);
    PopupOptions options{};
    options.backdrop = frosted_chain();
    PopupToken token = shown.open_popup(shown.root(), popup, options);
    const Frame first = present(raster, shown);
    const BackdropStatistics computed = shown.backdrop_statistics();
    require(computed.computations > 0U && computed.draws > 0U, "a clear popup's backdrop is computed and drawn");
    require(computed.computed_pixels == 128U * 96U, "the backdrop is computed once over the popup's pixels");
    require(computed.occluded == 0U && computed.solid_fallbacks == 0U, "a clear popup is neither occluded nor solid");
    require(!same_pixel(first.at(popup_column, popup_row), before.at(popup_column, popup_row)),
            "the backdrop changes the pixels under the popup");
    require(first.at(popup_column, popup_row).alpha == 255U, "over opaque scenery the backdrop is opaque");
    require(same_pixel(first.at(outside_column, outside_row), before.at(outside_column, outside_row)),
            "outside the popup nothing changes");
    // Unchanged: drawn from the cache.
    const Frame again = present(raster, shown);
    const BackdropStatistics cached = shown.backdrop_statistics();
    require(cached.computations == computed.computations && cached.draws > computed.draws,
            "with nothing under it changed the backdrop is drawn from its cache");
    require(same_pixel(again.at(popup_column, popup_row), first.at(popup_column, popup_row)),
            "the cached backdrop is the computed one");
    // Damage of the popup's own content leaves the cache valid.
    (*popup).invalidate(Dirty::paint);
    static_cast<void>(present(raster, shown));
    const BackdropStatistics own = shown.backdrop_statistics();
    require(own.invalidated_tiles == cached.invalidated_tiles && own.computations == cached.computations,
            "the popup's own damage never invalidates its backdrop");
    // Damage of the window's content under it recomputes what it reaches.
    (*shown.root()).invalidate(Rect{popup_bounds.x, popup_bounds.y, 4.0, 4.0});
    static_cast<void>(present(raster, shown));
    const BackdropStatistics damaged = shown.backdrop_statistics();
    require(damaged.invalidated_tiles > own.invalidated_tiles && damaged.computations > own.computations,
            "damage under the popup recomputes the tiles it reaches");
    require(damaged.computed_pixels - own.computed_pixels < 128U * 96U,
            "damage in one corner recomputes only the tiles it reaches");
    // Reduced transparency: the chain's tint as a solid colour.
    shown.set_compositor_state(make_state(StateCase::reduced_transparency, 2U), true);
    static_cast<void>(present(raster, shown));
    const BackdropStatistics reduced = shown.backdrop_statistics();
    require(reduced.solid_fallbacks > damaged.solid_fallbacks && reduced.computations == damaged.computations,
            "under reduced transparency the backdrop is its solid tint");
    // High contrast: no backdrop at all.
    shown.set_compositor_state(make_state(StateCase::high_contrast, 3U), true);
    static_cast<void>(present(raster, shown));
    const BackdropStatistics none = shown.backdrop_statistics();
    require(none.solid_fallbacks == reduced.solid_fallbacks && none.draws == reduced.draws &&
                none.computations == reduced.computations,
            "under high contrast there is no backdrop");
    token.disconnect();
}

void opaque_popup_occludes(render::SkiaRaster& raster) {
    const std::unique_ptr<Window> window =
        lab::make_compositor_lab(WindowBackdropRequest::preferring(WindowBackdrop::opaque), false);
    Window& shown = *window;
    shown.set_compositor_state(make_state(StateCase::every_backdrop, 1U), true);
    const std::shared_ptr<Panel> popup = make_popup(true);
    PopupOptions options{};
    options.backdrop = frosted_chain();
    PopupToken token = shown.open_popup(shown.root(), popup, options);
    // The first paint records the cover; until then it may not count.
    static_cast<void>(present(raster, shown));
    const BackdropStatistics first = shown.backdrop_statistics();
    (*shown.root()).invalidate(Dirty::paint);
    const Frame covered = present(raster, shown);
    const BackdropStatistics steady = shown.backdrop_statistics();
    require(steady.occluded > first.occluded, "a popup wholly covered by opaque content occludes its backdrop");
    require(steady.computations == first.computations && steady.draws == first.draws,
            "an occluded backdrop is neither computed nor drawn");
    const Pixel face = covered.at(popup_column, popup_row);
    require(face.red == 40U && face.green == 80U && face.blue == 120U && face.alpha == 255U,
            "the opaque content is all that shows");
    token.disconnect();
}

void chain_rules() {
    const BackdropChain chain = frosted_chain();
    require(backdrop_chain_valid(chain) && backdrop_chain_blur(chain) == 8.0, "the frosted chain is valid");
    const Color solid = backdrop_chain_solid(chain);
    require(solid.red == 255U && solid.green == 255U && solid.blue == 255U && solid.alpha == 64U,
            "the solid fallback is the tint");
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
        PopupOptions options{};
        options.backdrop = two_blurs;
        static_cast<void>(shown.open_popup(shown.root(), make_popup(false), options));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a popup with an invalid chain is refused");
    refused = false;
    try {
        shown.set_backdrop_chain(two_blurs);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && !shown.backdrop_chain().has_value(), "a window refuses an invalid chain, changing nothing");
    shown.set_backdrop_chain(chain);
    require(shown.backdrop_chain().has_value() && *shown.backdrop_chain() == chain, "a window holds its chain");
}


// --measure: the backdrop's frame cost (owner, 2026-11-05: efficiency over
// looks), one JSON line per blur radius and popup size. Each figure is the
// median of `rounds` whole-window presents at scale 1, in microseconds:
// none, the window without the popup; cached, with the backdrop drawn from
// its cache; recomputed, after damage of the window under the whole popup;
// occluded, with an opaque panel over the whole popup and the window
// damaged under it.
constexpr std::size_t rounds = 15U;

double median_of(std::vector<double>& values) {
    std::sort(values.begin(), values.end());
    const double middle = values[values.size() / 2U];
    return middle;
}

double time_present(render::SkiaRaster& raster, Window& window, const Control::Ptr& damaged, Rect damage) {
    std::vector<double> samples;
    samples.reserve(rounds);
    for (std::size_t round = 0U; round < rounds; ++round) {
        if (damaged) (*damaged).invalidate(damage);
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        static_cast<void>(present(raster, window));
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        const std::chrono::duration<double, std::micro> spent = end - start;
        samples.push_back(spent.count());
    }
    const double middle = median_of(samples);
    return middle;
}

void measure_backdrops(render::SkiaRaster& raster) {
    const std::array<double, 5> radii{0.0, 4.0, 8.0, 16.0, 32.0};
    const std::array<Size, 3> sizes{Size{64.0, 64.0}, Size{128.0, 96.0}, Size{256.0, 192.0}};
    for (const double radius : radii) {
        for (const Size size : sizes) {
            BackdropChain chain{};
            if (radius > 0.0) static_cast<void>(chain.append(BackdropOperation::blur(radius)));
            static_cast<void>(chain.append(BackdropOperation::saturate(1.5)));
            static_cast<void>(chain.append(BackdropOperation::tint(Color{255U, 255U, 255U, 64U})));
            const Rect bounds{8.0, 8.0, size.width, size.height};
            std::array<double, 4> timings{};
            std::array<std::uint64_t, 4> computed{};
            for (std::size_t mode = 0U; mode < 4U; ++mode) {
                const std::unique_ptr<Window> window =
                    lab::make_compositor_lab(WindowBackdropRequest::preferring(WindowBackdrop::opaque), false);
                Window& shown = *window;
                shown.set_compositor_state(make_state(StateCase::every_backdrop, 1U), true);
                std::shared_ptr<Panel> popup = make_control<Panel>(StableId("lab.measure.popup"));
                (*popup).set_background(Color{0U, 0U, 0U, 0U});
                (*popup).set_requested_bounds(bounds);
                if (mode == 3U) {
                    std::shared_ptr<Panel> cover = make_control<Panel>(StableId("lab.measure.cover"));
                    (*cover).set_background(Color{40U, 80U, 120U, 255U});
                    (*cover).set_requested_bounds(Rect{0.0, 0.0, size.width, size.height});
                    (*popup).add_child(cover);
                }
                PopupOptions options{};
                options.backdrop = chain;
                PopupToken token{};
                if (mode != 0U) token = shown.open_popup(shown.root(), popup, options);
                // Two warm presents: the first computes, the second records covers.
                static_cast<void>(present(raster, shown));
                static_cast<void>(present(raster, shown));
                const std::uint64_t before = shown.backdrop_statistics().computed_pixels;
                const Control::Ptr damaged = mode >= 2U ? shown.root() : Control::Ptr{};
                timings[mode] = time_present(raster, shown, damaged, bounds);
                computed[mode] = (shown.backdrop_statistics().computed_pixels - before) / rounds;
                token.disconnect();
            }
            std::printf("{\"blur\":%.0f,\"width\":%.0f,\"height\":%.0f,\"none_us\":%.1f,\"cached_us\":%.1f,"
                        "\"recomputed_us\":%.1f,\"occluded_us\":%.1f,\"recomputed_pixels\":%llu,"
                        "\"occluded_pixels\":%llu}\n",
                        radius, size.width, size.height, timings[0], timings[1], timings[2], timings[3],
                        static_cast<unsigned long long>(computed[2]), static_cast<unsigned long long>(computed[3]));
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    print_mode = argc > 1 && std::strcmp(argv[1], "--print") == 0;
    if (argc > 1 && std::strcmp(argv[1], "--measure") == 0) {
        render::SkiaRaster raster;
        measure_backdrops(raster);
        return EXIT_SUCCESS;
    }
    parsing();
    chain_rules();
    {
        render::SkiaRaster raster;
        each_state_and_request(raster);
        compositor_stops_and_starts(raster);
        clear_popup_backdrop(raster);
        opaque_popup_occludes(raster);
    }
    std::cout << "gui_forms_compositor_lab_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
