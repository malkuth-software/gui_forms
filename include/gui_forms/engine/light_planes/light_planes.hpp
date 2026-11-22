#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/platform_types/platform_types.hpp"

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
// where lamps live; this is the machinery any of them composites with.
//
// The bakes ship with the library (owner, 2026-11-22: "baking must happen
// on the client at launch time, or ship in a compact format, and the baker
// must be re-runnable"). The baker (tools/industrial_surfaces/lamp_keys.cpp)
// needs GUI.Drawing's raster for its legends, which Core does not link, and
// costs about 0.8 CPU seconds a dialog key at 240 pixels (the study's bake
// study, 3.21 s for the four), so a first-use bake is not affordable at
// launch. Instead each bake is stored grey, its lamps summed and its planes
// in binary16 (read_light_plane_store; tools/industrial_surfaces/
// pack_light_planes.py, which measured CIEDE2000 0.84 at worst on the keys
// and 1.24 on 0.17% of the jewels' pixels against the float bakes), 755 KB
// for the six shipped bakes against 4.1 MB, and compiled into Core
// (shipped_light_plane_store). The CMake target gui_forms_light_planes
// runs the baker and the packer again into assets/light_planes.
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

// A colour given as sRGB, such as a control's lamp colour property, as a
// lamp's linear colour at unit power: each channel decoded by the sRGB
// curve (through engine_power, so every host decodes alike). Alpha is
// ignored.
[[nodiscard]] GUI_FORMS_API LampColour lamp_colour_from_srgb(Color colour) noexcept;

// One bake's planes, `width` × `height` pixels:
// - `room`: the room's light, three floats a pixel (red, green, blue),
//   premultiplied by coverage;
// - `lamps`: `lamp_count` planes of the same shape, one after another, each
//   lamp alone at unit white power;
// - `coverage`: one float a pixel in [0, 1], the share of the pixel the
//   key covers.
// Rows run top to bottom, pixels left to right.
// - `aperture`: for a bake seen through a round hole in its panel (the
//   study's indicator jewel, whose crown shows through a dot), the hole's
//   radius as a share of the bake's half-width, centred; 0 for a bake shown
//   whole (a key). LightShape::jewel and LightShape::pinpoint read it.
struct GUI_FORMS_API LightPlanes final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t lamp_count{};
    std::vector<float> room;
    std::vector<float> lamps;
    std::vector<float> coverage;
    double aperture{};
};

// The study's indicator jewel's hole (lamp_keys.cpp, make_indicator: a
// 0.19-unit hole in a view of ±2.6 · tan(9.5°) units), as a share of the
// bake's half-width: 0.19 / (2.6 · tan 9.5°).
inline constexpr double indicator_jewel_aperture = 0.43669047278549333;

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
    // A store (read_light_plane_store) that does not begin "GFLP".
    not_a_store,
    // A store of a version this library does not read.
    unsupported_version,
    // A store's aperture outside [0, 1] or not finite.
    aperture_out_of_range,
};

// Reads the study's bake file (lamp_keys.cpp, write_buffers): three
// little-endian int32 (width, height, lamp count), then the room plane,
// each lamp's plane and the coverage plane as little-endian IEEE binary32.
// The whole buffer is validated before `planes` is replaced; on a defect
// `planes` is left as it was. Throws std::bad_alloc when the planes cannot
// be stored (`planes` left as it was).
[[nodiscard]] GUI_FORMS_API LightPlanesDefect read_light_planes(std::span<const std::byte> bytes, LightPlanes& planes);

// The shipped store's version (pack_light_planes.py writes it).
inline constexpr std::uint32_t light_plane_store_version = 1U;

// Reads a shipped bake's store (tools/industrial_surfaces/
// pack_light_planes.py): "GFLP", then little-endian uint32 version,
// width, height and lamp count, float32 aperture, and then the room plane,
// each lamp's plane and the coverage plane as IEEE binary16, one grey
// value a pixel, premultiplied by coverage as baked. Each grey value is
// read into all three channels of `planes`. The whole buffer is validated
// before `planes` is replaced (as read_light_planes, and the store's own
// defects: not_a_store, unsupported_version, aperture_out_of_range); on a
// defect `planes` is left as it was. Throws std::bad_alloc when the planes
// cannot be stored (`planes` left as it was).
[[nodiscard]] GUI_FORMS_API LightPlanesDefect read_light_plane_store(std::span<const std::byte> bytes,
                                                                     LightPlanes& planes);

// The bake `name` as shipped with the library (assets/light_planes/
// <name>.gflp, compiled into Core): its store's bytes, static storage, or
// empty for a name not shipped. The shipped bakes are the study's dialog
// keys (dialog_ok_up, dialog_ok_down, dialog_cancel_up, dialog_cancel_down)
// and its indicator jewels (indicator_backed_up, indicator_clear_up).
[[nodiscard]] GUI_FORMS_API std::span<const std::byte> shipped_light_plane_store(std::string_view name) noexcept;

// The jewel lamp's bake: the study's backed jewel (a mirror behind a clear
// faceted crown), the owner's status lamps (panel_lamps.html, demo.html).
inline constexpr std::string_view jewel_lamp_bake = "indicator_backed_up";

// A lamp as composited: its colour and its power (0 off, 1 the baked
// unit; the strike envelope passes through, lamp_power).
struct GUI_FORMS_API LampDrive final {
    LampColour colour{};
    double power{};
};

// How a bake is laid over its rectangle:
// - whole: stretched over it (a key, a caption);
// - jewel: the study's status lamp (demo.html and dialog_demo.cpp,
//   place_indicator): the jewel's crown seen through a round dot in the
//   panel, the bake's central disc of `aperture` (else its whole width)
//   drawn in the rectangle's inscribed circle less a dark rim, the rim the
//   panel's shadowed edge round the hole (jewel_rim_share of the radius,
//   darkened 85% toward 0.004 in linear light); transparent outside it;
// - pinpoint: a status accent (owner, 2026-11-21): the same crown through a
//   dot pinpoint_core_share of the rectangle's half side, no rim and no
//   bezel, with the study's halo (place_indicator's: 0.22 · power ·
//   colour · e^(−b² / (1.2 r²)), b the distance past 0.6 r) lighting
//   round it in the lamp's colour, all inside the rectangle.
enum class LightShape : std::uint8_t { whole, jewel, pinpoint };

inline constexpr double jewel_rim_share = 2.0 / 14.0;
inline constexpr double pinpoint_core_share = 1.0 / 3.0;

// The composite's settings, defaulting to the owner's panel_lamps choices:
// the room at gain 1 and the tone's exposure 1.3. `press_mix` mixes the up
// bake (0) toward the down bake (1). `filter` is a coloured face over the
// bake (dialog_demo.cpp, lamp_light): the room's light crosses it twice,
// in and out, and a lamp's once, so an unlit coloured lamp shows its tint;
// white, the default, leaves the bake as baked. `shape` lays it out.
struct GUI_FORMS_API LightComposite final {
    double room{1.0};
    double exposure{1.3};
    double press_mix{};
    LampColour filter{1.0, 1.0, 1.0};
    LightShape shape{LightShape::whole};
};

// A jewel lamp's face filter for a lamp of `colour` (IndicatorLamp's and
// RadioButton's lamp look): the colour scaled to a largest channel of 1,
// mixed 60% from white, so an unlit jewel is tinted glass and a lit one
// glows in its colour. Only a neutral colour gives clear glass: the
// study's colours are filters over a white-hot lamp, and its white
// (lamp_white) is warm, so a white jewel is faintly warm.
[[nodiscard]] GUI_FORMS_API LampColour jewel_filter(LampColour colour) noexcept;

// The power the study's dialog drives its jewels at (demo.html,
// INDICATORS: 1.6): a lamp's power from 0 to 1 is scaled by it for a jewel
// or a pinpoint.
inline constexpr double jewel_lamp_power = 1.6;

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
// nothing. A jewel or pinpoint shape (LightComposite::shape) reads `up`
// alone, its 3 × 3 samples a pixel each toned and then averaged, as the
// study's place_indicator does; its halo takes the first drive's colour
// and power. The planes are sampled bilinearly in linear, premultiplied
// light, so a 240-pixel bake shown 180 units wide (the owner's choice)
// is filtered before it is toned; a rectangle the bake's own size reads
// each plane pixel exactly. Returns the pixels written; none when the two
// bakes' shapes or lamp counts differ, `drives` holds fewer than the lamp
// count, `rect` is empty or its far edge overflows, or `destination`'s
// shape does not fit its buffer (validate_raster_view).
GUI_FORMS_API std::size_t composite_light_planes(const LightPlanes& up, const LightPlanes& down,
                                                 std::span<const LampDrive> drives, const LightComposite& settings,
                                                 LightPlaneRect rect, const PartRasterView& destination) noexcept;

// -- lamp and key motion ---------------------------------------------------------------
//
// The study's envelopes (panel_lamps.html, powerAt and the press), the
// owner's: a lamp strikes from 40% to full power over 60 ms, as a filament
// warms. Switched off it cools (principle 7, owner 2026-11-12: the
// filament's inertia): from the power it had to 0 over 120 ms, falling
// fastest at first (p · (1 − u)²), and is fully off from then, so the
// frames end. A key's press eases out over 70 ms (1 − (1 − u)²) from
// wherever it was. Under reduced motion each arrives at once. Durations
// are perceptual, so they are given in seconds.

inline constexpr double lamp_strike_seconds = 0.06;
inline constexpr double lamp_strike_start = 0.4;
inline constexpr double key_press_seconds = 0.07;
inline constexpr double lamp_cool_seconds = 0.12;

// A lamp's power `seconds` after it was switched: 0 when off, the strike
// envelope when on (1 from lamp_strike_seconds, or at once under reduced
// motion). A lamp switched off while lit cools instead (lamp_cooling_power).
[[nodiscard]] GUI_FORMS_API double lamp_power(bool on, double seconds, bool reduced_motion) noexcept;
// A lamp's power `seconds` after it was switched off at power `from`:
// from · (1 − u)², u = seconds / lamp_cool_seconds, and 0 from
// lamp_cool_seconds on, or at once under reduced motion.
[[nodiscard]] GUI_FORMS_API double lamp_cooling_power(double from, double seconds, bool reduced_motion) noexcept;

// A key's press mix `seconds` after its press or release began from mix
// `from` toward `to` (0 up, 1 down): eased, reaching `to` at
// key_press_seconds, or at once under reduced motion.
[[nodiscard]] GUI_FORMS_API double key_press_mix(double from, double to, double seconds, bool reduced_motion) noexcept;

// Whether a lamp switched on `seconds` ago and a press begun `seconds` ago
// have settled: the frames after it change nothing.
[[nodiscard]] GUI_FORMS_API bool lamp_settled(double seconds) noexcept;

// -- the failing lamp (owner, 2026-11-22) -------------------------------------------
//
// A lamp in a fault flickers and stutters as a failing lamp does: irregular
// dropouts to near dark, short dim stutters, and uneven recoveries (the
// filament catching part way, then creeping back). It is deterministic: the
// same seed and the same time since the fault began always give the same
// power, so captures and tests repeat; nothing is drawn at random per
// frame.
//
// The timeline is cut into slots of lamp_fault_slot_seconds. Each slot holds
// at most one event, chosen by a hash of the seed and the slot's index:
// - none (40%): the lamp stays lit;
// - a dropout (35%): near dark (3% to 10%) for 50 to 180 ms, then back to a
//   partial level (40% to 80%) at once and on to full with a time constant
//   of 30 to 150 ms;
// - a dim stutter (25%): down to 35% to 60% for 30 to 80 ms, then back with
//   a time constant of 20 to 60 ms.
// An event starts at most lamp_fault_latest_start into its slot, so two
// events start at least 0.6 − 0.08 = 0.52 s apart: any one second holds at
// most two, under the three flashes a second of the photosensitive seizure
// guidance (WCAG 2.3.1), small as the lamp is. A recovery may run into the
// next slot; the power is the lower of the two slots' events.
inline constexpr double lamp_fault_slot_seconds = 0.6;
inline constexpr double lamp_fault_latest_start = 0.08;
// The fault's power factor, in (0, 1], `seconds` after the fault began, for
// `seed` (a lamp's own, so that two failing lamps fail differently). 1 at
// once and always under reduced motion: a steady lit lamp.
[[nodiscard]] GUI_FORMS_API double lamp_fault_flicker(double seconds, std::uint32_t seed,
                                                      bool reduced_motion) noexcept;
[[nodiscard]] GUI_FORMS_API bool key_press_settled(double seconds) noexcept;
// Whether a lamp switched off `seconds` ago has cooled: it is fully off.
[[nodiscard]] GUI_FORMS_API bool lamp_cooled(double seconds) noexcept;

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

// -- a part's kept composite -----------------------------------------------------------------
//
// A part composited from a bake (PartPaint::light_planes, such as
// IndicatorLamp's lens) keeps its last composite in one of these, owned by
// the part's control, so that a repaint with nothing changed (a lamp
// repainted for a neighbour's damage) draws the kept raster again instead
// of compositing. Every lamp of the bake takes the one drive. It holds the
// bake it composited, so a bake released elsewhere is never mistaken for a
// new one at the same address. One thread: the painting one.
class GUI_FORMS_API LightPlaneRaster final {
public:
    // The composite of `planes` over `extent` device pixels at `scale`,
    // every lamp driven by `drive`: the kept raster when the bake, drive,
    // settings, extent and scale are those it was made with, else a new
    // one, then kept. Null, and nothing kept, when `planes` is null, the
    // extent is empty, or no raster of that extent and scale can be made
    // (PartRaster::create). Throws std::bad_alloc when a new raster cannot
    // be stored (what was kept stays).
    [[nodiscard]] std::shared_ptr<const PartRaster> composite(const std::shared_ptr<const LightPlanes>& planes,
                                                              LampDrive drive, const LightComposite& settings,
                                                              DeviceExtent extent, DeviceScale scale);
    // Releases the kept raster and bake (a control leaving its window).
    void release() noexcept;
    // Composites made so far: one for each change of the inputs.
    [[nodiscard]] std::size_t composites() const noexcept { return composites_; }

private:
    std::shared_ptr<const LightPlanes> planes_{};
    std::shared_ptr<const PartRaster> raster_{};
    LampDrive drive_{};
    LightComposite settings_{};
    std::size_t composites_{};
};

// -- shipped bakes ---------------------------------------------------------------------------

// Whether `name` names a bake: 1 to 64 lower-case ASCII letters, digits and
// '_' (the baker's stems, such as dialog_ok_up), so that a name never
// leaves its directory.
[[nodiscard]] GUI_FORMS_API bool light_plane_name_valid(std::string_view name) noexcept;

// The bakes in one directory, then the shipped ones, each read the first
// time it is asked for and kept: `<directory>/<name>.f32` (the baker's
// float planes), else `<directory>/<name>.gflp` (a store), else the
// shipped store of that name (shipped_light_plane_store). A name found
// nowhere, or a defective file, is remembered and not read again. Names
// are lower-case letters, digits and '_' (the baker's stems, such as
// dialog_ok_up), so a name never leaves the directory. An empty directory
// reads the shipped bakes alone. One thread: the UI thread that asks.
class GUI_FORMS_API LightPlaneCache final {
public:
    explicit LightPlaneCache(FilePath directory);

    // The bake named `name`, or null when it is missing, defective, or the
    // name is not one. Throws std::bad_alloc when a read's storage cannot
    // be had (nothing is remembered then).
    [[nodiscard]] std::shared_ptr<const LightPlanes> lend(std::string_view name);
    // Bakes read so far, from a file or the shipped stores (each at most
    // once).
    [[nodiscard]] std::size_t reads() const noexcept { return reads_; }
    // The defect of the last read of `name`; none for a name not read.
    [[nodiscard]] LightPlanesDefect defect(std::string_view name) const noexcept;

private:
    struct CachedBake final {
        std::string name{};
        std::shared_ptr<const LightPlanes> planes{};
        LightPlanesDefect defect{LightPlanesDefect::none};
    };
    FilePath directory_;
    std::vector<CachedBake> bakes_{};
    std::size_t reads_{};
};

} // namespace gui_forms::engine_light
