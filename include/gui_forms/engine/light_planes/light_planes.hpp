#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/engine/types/engine_types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Lamps and lit keys as baked light planes (skeudoindustrial surfaces,
// phase 3; notes:planning/SKEUDOINDUSTRIAL_SURFACES.md). The
// industrial-surfaces study renders each lit key or lamp offline
// (notes:experiments/industrial-surfaces/cpp/lamp_keys.cpp, with BFFT's
// photonic transport) into linear light planes: the room's light, each
// lamp's light at unit white power, and the coverage. Light adds, so a
// frame at any lamp brightness and colour is a weighted sum of the planes,
// composited at run time (panel_lamps.html, composite):
//
//     v = room · R + Σ power_i · colour_i · L_i       (premultiplied, linear)
//     pixel = encode(tone(v / coverage)), alpha = coverage
//     tone(v) = 1 − e^(−1.3 v)
//
// and a key's press is the up and down bakes mixed. Nothing here decides
// where lamps live (a control, a part, or both: an open owner decision);
// this is the machinery any of them would composite with. The planes come
// from shipped bakes, read on first use (LightPlaneCache); whether the
// baker (BFFT, stx_conv) is needed at run time is also open, so nothing
// here bakes.
//
// Tinting after the bake is exact for light that reaches the eye after one
// interaction with the tinted material: the result is linear in the
// colour, which is what the lamp planes rely on (the study bakes each lamp
// at white and colours it here). It is not exact where light bounces
// inside a tinted body several times (the colour enters as a power); the
// study's dialog keys are white plastic and clear acrylic, tinted only by
// the lamp, so their planes are exact. A coloured body would need a plane
// per bounce order, or a bake in its colour.

namespace gui_forms::engine_light {

// A lamp's colour, linear RGB at unit power. The study's lamps
// (panel_lamps.html, C): amber, warm white, red (the owner's: [1, 0.12,
// 0.04]), green, blue and cyan.
struct GUI_FORMS_API LampColour final {
    double red{};
    double green{};
    double blue{};
};

inline constexpr LampColour lamp_amber{1.0, 0.50, 0.08};
inline constexpr LampColour lamp_white{1.0, 0.92, 0.78};
inline constexpr LampColour lamp_red{1.0, 0.12, 0.04};
inline constexpr LampColour lamp_green{0.16, 1.0, 0.25};
inline constexpr LampColour lamp_blue{0.10, 0.35, 1.0};
inline constexpr LampColour lamp_cyan{0.15, 0.85, 1.0};

// One bake's planes, `width` × `height` pixels:
// - `room`: the room's light, three floats a pixel (red, green, blue),
//   premultiplied by coverage;
// - `lamps`: `lamp_count` planes of the same shape, one after another, each
//   lamp alone at unit white power;
// - `coverage`: one float a pixel in [0, 1], the share of the pixel the
//   key covers.
// Rows run top to bottom, pixels left to right.
struct GUI_FORMS_API LightPlanes final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t lamp_count{};
    std::vector<float> room;
    std::vector<float> lamps;
    std::vector<float> coverage;
};

// The largest bake read: a side of 2048 pixels (the baker's own clamp) and
// eight lamps.
inline constexpr std::uint32_t maximum_light_plane_side = 2048U;
inline constexpr std::uint32_t maximum_light_plane_lamps = 8U;

enum class LightPlanesDefect : std::uint8_t {
    none,
    // No such file, or it could not be opened (LightPlaneCache).
    unreadable,
    // Fewer than the 12 header bytes.
    short_header,
    // A side of 0 or less.
    empty_size,
    // A side past maximum_light_plane_side.
    too_large,
    // A lamp count below 0 or past maximum_light_plane_lamps.
    too_many_lamps,
    // More or fewer bytes than the header's shape needs.
    length_mismatch,
    // A plane holds an infinity or a NaN.
    not_finite,
    // A coverage outside [0, 1].
    coverage_out_of_range,
};

// Reads the study's bake file (lamp_keys.cpp, write_buffers): three
// little-endian int32 (width, height, lamp count), then the room plane,
// each lamp's plane and the coverage plane as little-endian IEEE binary32.
// The whole buffer is validated before `planes` is replaced; on a defect
// `planes` is left as it was. Throws std::bad_alloc when the planes cannot
// be stored (`planes` left as it was).
[[nodiscard]] GUI_FORMS_API LightPlanesDefect read_light_planes(std::span<const std::byte> bytes, LightPlanes& planes);

// A lamp as composited: its colour and its power (0 off, 1 the baked
// unit; the strike envelope passes through, lamp_power).
struct GUI_FORMS_API LampDrive final {
    LampColour colour{};
    double power{};
};

// The composite's settings, defaulting to the owner's panel_lamps choices:
// the room at gain 1 and the tone's exposure 1.3. `press_mix` mixes the up
// bake (0) toward the down bake (1).
struct GUI_FORMS_API LightComposite final {
    double room{1.0};
    double exposure{1.3};
    double press_mix{};
};

// A rectangle of device pixels the planes are stretched over.
struct GUI_FORMS_API LightPlaneRect final {
    std::int64_t x{};
    std::int64_t y{};
    std::int64_t width{};
    std::int64_t height{};
};

// Composites `up` and `down` (the same bake for a key that is not pressed)
// with one drive per lamp over `rect` in `destination` (premultiplied
// BGRA8), writing every pixel of `rect` inside the destination: the key's
// pixels with their coverage as alpha, transparent where it covers
// nothing. The planes are sampled bilinearly in linear, premultiplied
// light, so a 240-pixel bake shown 180 units wide (the owner's choice)
// is filtered before it is toned; a rectangle the bake's own size reads
// each plane pixel exactly. Returns the pixels written; none when the two
// bakes' shapes or lamp counts differ, `drives` holds fewer than the lamp
// count, or `rect` is empty.
GUI_FORMS_API std::size_t composite_light_planes(const LightPlanes& up, const LightPlanes& down,
                                                 std::span<const LampDrive> drives, const LightComposite& settings,
                                                 LightPlaneRect rect, const PartRasterView& destination) noexcept;

// -- lamp and key motion ---------------------------------------------------------------
//
// The study's envelopes (panel_lamps.html, powerAt and the press), the
// owner's: a lamp strikes from 40% to full power over 60 ms, as a filament
// warms; it goes out at once. A key's press eases out over 70 ms
// (1 − (1 − u)²) from wherever it was. Under reduced motion both arrive at
// once. Durations are perceptual, so they are given in seconds.

inline constexpr double lamp_strike_seconds = 0.06;
inline constexpr double lamp_strike_start = 0.4;
inline constexpr double key_press_seconds = 0.07;

// A lamp's power `seconds` after it was switched: 0 when off, the strike
// envelope when on (1 from lamp_strike_seconds, or at once under reduced
// motion).
[[nodiscard]] GUI_FORMS_API double lamp_power(bool on, double seconds, bool reduced_motion) noexcept;

// A key's press mix `seconds` after its press or release began from mix
// `from` toward `to` (0 up, 1 down): eased, reaching `to` at
// key_press_seconds, or at once under reduced motion.
[[nodiscard]] GUI_FORMS_API double key_press_mix(double from, double to, double seconds, bool reduced_motion) noexcept;

// Whether a lamp switched on `seconds` ago and a press begun `seconds` ago
// have settled: the frames after it change nothing.
[[nodiscard]] GUI_FORMS_API bool lamp_settled(double seconds) noexcept;
[[nodiscard]] GUI_FORMS_API bool key_press_settled(double seconds) noexcept;

// -- the lit keys' bake ------------------------------------------------------------------
//
// One lit key's bake parameters, and the values the study baked its dialog
// keys with (lamp_keys.cpp, the dialog family):
// - height above the panel as a share of the key's short side: 0.57 there,
//   an early test value. Relief is contextual (owner, 2026-11-12): each
//   role takes its own height, a theme parameter, so the baker is given the
//   role's share and a bake is made per role; nothing here fixes one;
// - the sides lean in by 3° + 17° × clamp((share − 0.25) / 0.75, 0, 1);
// - the sides are the key's own plastic;
// - the control camera tilted 16° toward the viewer;
// - OK lit as the ANTI-ICE caption: clear acrylic, its one coil just above a
//   frosted back, so the coil is neither invisible nor plainly visible;
//   CANCEL in white opaque plastic with its lamps deep in the shell;
// - renders 240 pixels wide, shown 180 logical units wide;
// - composited with the room at 1 (panel_lamps' colouring).
// Light is never local (owner, 2026-11-12): a lamp lights its own lens and
// legend only. The composite keeps it so, writing nothing where the key's
// coverage is 0, so a bake's spill onto the panel never shows.
// The baker is run by the integrator (`lamps <dir> 240 dialog`); nothing
// at run time reads these but the display width.
struct GUI_FORMS_API LitKeyBake final {
    double height_share{};
    double camera_tilt_degrees{};
    std::uint32_t bake_width{};
    double display_width{};
    double room{};
};

inline constexpr LitKeyBake dialog_key_study_bake{
    .height_share = 0.57,
    .camera_tilt_degrees = 16.0,
    .bake_width = 240U,
    .display_width = 180.0,
    .room = 1.0,
};

// The sides' lean in degrees for a key `height_share` of its short side
// tall (lamp_keys.cpp, shape_key).
[[nodiscard]] GUI_FORMS_API double lit_key_lean_degrees(double height_share) noexcept;

// -- shipped bakes ---------------------------------------------------------------------------

// The bakes in one directory, each read the first time it is asked for
// (`<directory>/<name>.f32`) and kept: a missing or defective file is
// remembered and not read again. Names are lower-case letters, digits and
// '_' (the baker's stems, such as dialog_ok_up), so a name never leaves the
// directory. One thread: the UI thread that asks.
class GUI_FORMS_API LightPlaneCache final {
public:
    explicit LightPlaneCache(std::filesystem::path directory);

    // The bake named `name`, or null when it is missing, defective, or the
    // name is not one. Throws std::bad_alloc when a read's storage cannot
    // be had (nothing is remembered then).
    [[nodiscard]] std::shared_ptr<const LightPlanes> lend(std::string_view name);
    // Files read so far (each at most once).
    [[nodiscard]] std::size_t reads() const noexcept { return reads_; }
    // The defect of the last read of `name`; none for a name not read.
    [[nodiscard]] LightPlanesDefect defect(std::string_view name) const noexcept;

private:
    struct CachedBake final {
        std::string name{};
        std::shared_ptr<const LightPlanes> planes{};
        LightPlanesDefect defect{LightPlanesDefect::none};
    };
    std::filesystem::path directory_;
    std::vector<CachedBake> bakes_{};
    std::size_t reads_{};
};

} // namespace gui_forms::engine_light
