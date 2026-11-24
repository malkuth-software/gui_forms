#include "light_map_primitive.hpp"

#include "gui_forms/engine/light_planes/light_planes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <string>
#include <string_view>

// Identical bytes on every host: no fused multiply-add.
// Only in the reference arithmetic (GUI_FORMS_REFERENCE_ARITHMETIC, owner
// 2026-11-16): deployment builds leave contraction on (CMakeLists.txt,
// "Floating-point contraction").
#if defined(__clang__) && defined(GUI_FORMS_REFERENCE_ARITHMETIC)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_primitives {
namespace {

const PartRole light_map_roles[] = {PartRole::region, PartRole::face, PartRole::well, PartRole::actuator};

constexpr std::size_t light_map_bake = 0U;
constexpr std::size_t light_map_lamp_colour = 1U;
constexpr std::size_t light_map_lamp_power = 2U;
constexpr std::size_t light_map_press = 3U;
constexpr std::size_t light_map_room = 4U;

// The shipped key bakes by stem, in the choice's order.
constexpr std::array<std::string_view, 2> light_map_bake_names{"dialog_ok", "dialog_cancel"};

const PrimitiveParameterDeclaration light_map_parameters[] = {
    {"bake", PrimitiveParameterKind::choice, 0.0, 0.0, 0.0, {}, false, light_map_bake_names},
    {"lamp_colour", PrimitiveParameterKind::colour, 0.0, 0.0, 0.0, {255, 255, 255, 255}},
    {"lamp_power", PrimitiveParameterKind::number, 0.0, 2.0, 0.0, {}},
    {"press", PrimitiveParameterKind::number, 0.0, 1.0, 0.0, {}},
    {"room", PrimitiveParameterKind::number, 0.0, 2.0, 1.0, {}},
};

// The cost: two bakes (up and down), each sampled bilinearly (four reads of
// the room and of each lamp, about 60 operations), mixed and toned (three
// exponentials). The fixed part is the drive's set-up; the stores are
// decoded once per process, not per bake.
const PrimitiveDescriptor light_map_descriptor{
    "light_map", EngineVersion{0, 3}, light_map_roles, {}, light_map_parameters, PrimitiveCost{320U, 256U, 0U},
    Insets{}, 0U};

// One shipped key, its up and down bakes decoded.
struct DecodedKey final {
    engine_light::LightPlanes up{};
    engine_light::LightPlanes down{};
    bool read{};
};

DecodedKey decode_key(std::string_view stem) noexcept {
    DecodedKey key{};
    try {
        const std::string up_name = std::string(stem) + "_up";
        const std::string down_name = std::string(stem) + "_down";
        const engine_light::LightPlanesDefect up =
            engine_light::read_light_plane_store(engine_light::shipped_light_plane_store(up_name), key.up);
        const engine_light::LightPlanesDefect down =
            engine_light::read_light_plane_store(engine_light::shipped_light_plane_store(down_name), key.down);
        key.read = up == engine_light::LightPlanesDefect::none && down == engine_light::LightPlanesDefect::none;
    } catch (const std::bad_alloc&) {
        key = DecodedKey{};
    }
    return key;
}

// Every shipped key, decoded on first use and kept for the process: the
// stores are static, so their planes never change.
const std::array<DecodedKey, light_map_bake_names.size()>& decoded_keys() noexcept {
    static const std::array<DecodedKey, light_map_bake_names.size()> keys{
        decode_key(light_map_bake_names[0]), decode_key(light_map_bake_names[1])};
    return keys;
}

} // namespace

const PrimitiveDescriptor& LightMapPrimitive::descriptor() const noexcept {
    return light_map_descriptor;
}

void LightMapPrimitive::derive(const PrimitiveParameterBlock&, const PrimitiveTarget&,
                               std::span<double>) const noexcept {}

void LightMapPrimitive::fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget&,
                             std::span<const double>, const PartRasterView& destination) const noexcept {
    const std::size_t chosen = static_cast<std::size_t>(parameters.values[light_map_bake].number);
    if (chosen >= light_map_bake_names.size()) return;
    const DecodedKey& key = decoded_keys()[chosen];
    if (!key.read) return;
    const Color colour = parameters.values[light_map_lamp_colour].colour;
    const double alpha = static_cast<double>(colour.alpha) / 255.0;
    const engine_light::LampDrive drive{engine_light::lamp_colour_from_srgb(colour),
                                        parameters.values[light_map_lamp_power].number * alpha};
    // Every lamp of the bake at the one drive (a key's lamps light together).
    std::array<engine_light::LampDrive, engine_light::maximum_light_plane_lamps> drives{};
    drives.fill(drive);
    const std::size_t lamps = key.up.lamp_count;
    engine_light::LightComposite settings{};
    settings.room = parameters.values[light_map_room].number;
    settings.press_mix = parameters.values[light_map_press].number;
    const engine_light::LightPlaneRect whole{0, 0, static_cast<std::int64_t>(destination.extent.width),
                                             static_cast<std::int64_t>(destination.extent.height)};
    static_cast<void>(engine_light::composite_light_planes(key.up, key.down, std::span(drives).first(lamps), settings,
                                                           whole, destination));
}

} // namespace gui_forms::engine_primitives
