#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/engine/part_shape/part_shape.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/surface_material/procedural_fill/procedural_fill.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace gui_forms {

// Declared in theme_types.hpp and surface_material_types.hpp, which include
// the engine's headers; a target only borrows them.
enum class ControlVisualRole : std::uint8_t;
struct SurfaceMaterial;
class ImageRegistry;
class PartRaster;

// The engine's primitive registry: the extension seam of stage T2 (brief
// item 10). A primitive is one kind of effect, written in C++ under the house
// style. It declares, in the words of the atom vocabulary (design language
// §12.7), the part roles it paints and the states it reads, together with its
// parameters, its cost bound and how far it draws outside the part. Theme
// data selects a primitive by name and sets its parameters; it never adds
// code. Registration is explicit and checked.

enum class PrimitiveParameterKind : std::uint8_t {
    number,
    colour,
    // One of the declaration's named choices (E8), such as a shape. The
    // value is the choice's position, kept in `number`; theme data names it.
    choice,
};

// One declared parameter. A number must lie in [minimum, maximum]. A colour's
// alpha scales whatever the primitive uses the colour for. A choice is one
// of `choices` (snake_case, each once), by position; `default_number` is the
// default choice's position.
//
// An `animated` parameter is one a transition drives from frame to frame,
// such as the bulge's amplitude during the ribbon's rise. A primitive's
// preparation never reads it, so changing it never makes a preparation
// stale (see PrimitivePreparation).
struct GUI_FORMS_API PrimitiveParameterDeclaration final {
    std::string_view name;
    PrimitiveParameterKind kind{PrimitiveParameterKind::number};
    double minimum{};
    double maximum{};
    double default_number{};
    Color default_colour{};
    bool animated{};
    std::span<const std::string_view> choices{};
};

// A declared upper bound on a primitive's work. One rasterization costs at
// most `per_device_pixel` arithmetic operations for each destination pixel,
// plus `fixed` for the whole part. One preparation costs at most
// `preparation_per_device_pixel` for each device pixel of the part's box; it
// runs once per part geometry, not once per frame. Budgets are checked
// against these at load (E4).
struct GUI_FORMS_API PrimitiveCost final {
    std::uint32_t per_device_pixel{};
    std::uint32_t fixed{};
    std::uint32_t preparation_per_device_pixel{};
};

// What a primitive is. Every span refers to static storage owned by the
// primitive's translation unit.
struct GUI_FORMS_API PrimitiveDescriptor final {
    // Registered (snake_case) name, as theme documents write it.
    std::string_view name;
    // The engine version that introduced the primitive. A theme needing a
    // later version than the engine implements falls back (E4).
    EngineVersion introduced{};
    // The part roles the primitive paints: at least one.
    std::span<const PartRole> roles;
    // The published states the primitive's output depends on. A change to
    // any other state never repaints it.
    std::span<const AnatomyState> states_read;
    std::span<const PrimitiveParameterDeclaration> parameters;
    PrimitiveCost cost{};
    // How far the primitive draws outside the part's bounds, in logical
    // units; zero for a primitive that stays inside them.
    Insets outsets{};
    // How many values the primitive's preparation derives, at most
    // maximum_prepared_values; zero for a primitive that derives none.
    std::size_t prepared_values{};
    // The engine inputs the primitive's output depends on (E6), read from
    // PrimitiveTarget::inputs. A change to any other input never repaints
    // it. Like animated parameters, inputs are never read by the
    // preparation.
    std::span<const EngineInput> inputs_read{};
};

inline constexpr std::size_t maximum_primitive_parameters = 24;
inline constexpr std::size_t maximum_prepared_values = 4;

// One parameter's value. `number` is read for a number parameter and
// `colour` for a colour parameter.
struct GUI_FORMS_API PrimitiveParameterValue final {
    PrimitiveParameterKind kind{PrimitiveParameterKind::number};
    double number{};
    Color colour{};

    friend constexpr bool operator==(const PrimitiveParameterValue& left,
                                     const PrimitiveParameterValue& right) noexcept {
        return left.kind == right.kind && left.number == right.number && left.colour == right.colour;
    }
};

// The values of one part's parameters: `count` live values, one per
// declared parameter, in declaration order.
struct GUI_FORMS_API PrimitiveParameterBlock final {
    std::array<PrimitiveParameterValue, maximum_primitive_parameters> values{};
    std::size_t count{};
};

enum class ParameterDefect : std::uint8_t {
    none,
    wrong_count,
    wrong_kind,
    not_finite,
    out_of_range,
};

// The first defect found and the index of the parameter it concerns.
struct GUI_FORMS_API ParameterValidation final {
    ParameterDefect defect{ParameterDefect::none};
    std::size_t parameter{};
};

// The block of a descriptor's declared defaults.
[[nodiscard]] GUI_FORMS_API PrimitiveParameterBlock default_parameters(const PrimitiveDescriptor& descriptor) noexcept;

// Checks every value against its declaration.
[[nodiscard]] GUI_FORMS_API ParameterValidation validate_parameters(const PrimitiveDescriptor& descriptor,
                                                      const PrimitiveParameterBlock& block) noexcept;

[[nodiscard]] GUI_FORMS_API std::optional<std::size_t> find_parameter(const PrimitiveDescriptor& descriptor,
                                                        std::string_view name) noexcept;

enum class ParameterAssignment : std::uint8_t {
    assigned,
    unknown_name,
    wrong_kind,
    not_finite,
    out_of_range,
};

// Set one parameter of `block` by name. Anything but `assigned` leaves the
// block unchanged.
[[nodiscard]] GUI_FORMS_API ParameterAssignment set_number_parameter(PrimitiveParameterBlock& block,
                                                       const PrimitiveDescriptor& descriptor,
                                                       std::string_view name, double value) noexcept;
[[nodiscard]] GUI_FORMS_API ParameterAssignment set_colour_parameter(PrimitiveParameterBlock& block,
                                                       const PrimitiveDescriptor& descriptor,
                                                       std::string_view name, Color value) noexcept;
// A choice by its name; an unknown choice is out_of_range.
[[nodiscard]] GUI_FORMS_API ParameterAssignment set_choice_parameter(PrimitiveParameterBlock& block,
                                                       const PrimitiveDescriptor& descriptor,
                                                       std::string_view name, std::string_view choice) noexcept;
// The position of `choice` among a choice parameter's choices, or none.
[[nodiscard]] GUI_FORMS_API std::optional<std::size_t> find_choice(const PrimitiveParameterDeclaration& declaration,
                                                     std::string_view choice) noexcept;

// The part a primitive draws: its size in logical units (the box the
// primitive's geometry is measured in), the display scale, its published
// states, and the engine inputs' values for it, by EngineInput (NaN for an
// input that is not published, such as the pointer of a part that is not
// hot). The destination's extent is the caller's choice, normally the
// logical size grown by the descriptor's outsets, times the scale, rounded
// up. A primitive with outsets draws its box starting at the left and top
// outsets times the scale, in device pixels, and clips what it draws to the
// destination.
//
// A primitive that draws a theme material (theme_material) also gets the
// part's material for its state, its own corner radius when the part sets
// one (PartPaint::corner_radius, which replaces the material's), and the
// window's image resources for the material's image fills. `material` and
// `images` are borrowed for the call that receives the target; a caller that
// keeps a target clears them.
//
// Image faces (owner, 2026-10-22: stx_conv prepares face sizes; nothing
// resamples inside a bake). A primitive with image fills names the sized
// variants it needs (EnginePrimitive::image_variants); the engine prepares
// each once, through the internal interpolation seam, keeps it in its
// shared cache, and lends it in `variants` for the bake, which only
// composites it. A fill whose variant is not lent draws nothing.

// The most image variants one bake uses: one per fill layer
// (SurfaceMaterial::maximum_fill_layers), then one for an image mask
// (E9.6), whose `layer` is maximum_image_variants - 1.
inline constexpr std::size_t maximum_image_variants = 9U;
inline constexpr std::size_t image_mask_variant_layer = maximum_image_variants - 1U;

// One sized variant of an image resource: the image resampled to `extent`
// device pixels, for display `scale`. A nine-patch's fixed segments are
// resampled apart from its middle: `source_slice` are their source pixels
// (left, top, right, bottom) and `fixed` their device lengths in the
// variant, both zero for a stretched or tiled image, which is resampled
// whole. `layer` is the fill layer the variant serves, by index among the
// material's fills (an image mask's is maximum_image_variants - 1); it is
// not part of what the variant is.
//
// A procedural fill's pattern (E10) is asked for the same way, with
// `procedural` set and no image: the pattern generated at `scale` and
// `extent`, never resampled. The engine never generates one inside a
// paint: a bake whose pattern is not ready is lent none, and the fill draws
// its ground; the pattern is generated in an idle frame and the parts that
// drew the ground are repainted.
struct GUI_FORMS_API ImageVariantRequest final {
    std::size_t layer{};
    ImageId image{};
    std::uint32_t source_width{};
    std::uint32_t source_height{};
    DeviceExtent extent{};
    Insets source_slice{};
    std::array<std::uint32_t, 4> fixed{};
    DeviceScale scale{};
    std::optional<ProceduralFill> procedural{};
};

// The variants lent for one bake, by fill layer; null where none is lent.
struct GUI_FORMS_API LentImageVariants final {
    std::array<std::shared_ptr<const PartRaster>, maximum_image_variants> layers{};
};

// The backplane (E9-B, owner 2026-10-22). A primitive that samples the
// window's style backplane (glass) says how far past the part it reads
// (EnginePrimitive::backplane_margin). The engine lends the backplane
// plane alone, never the control plane, in `backplane`: an image at the
// target's scale covering the destination grown by `backplane_margin`
// device pixels on every side, so destination pixel (x, y) lies over its
// pixel (x + margin, y + margin). `backplane_generation` is the backplane's
// damage generation the image was made at; a raster drawn from it is
// current only while the generation is. `backplane` is borrowed, as
// `material` is, and null when the window lends none: the primitive then
// draws without it. `reduced_transparency` is the window's presentation
// setting (stage T5, PLAN.md §4.5), set for a primitive that samples the
// backplane only: such a raster is never shared, and a change of the
// setting damages every part that sampled it, so the setting needs no
// place in a cache key.

struct GUI_FORMS_API PrimitiveTarget final {
    Size logical_size{};
    DeviceScale scale{};
    AnatomyStateSet states{};
    std::array<double, engine_input_count> inputs{};
    const SurfaceMaterial* material{};
    std::optional<double> corner_radius{};
    const ImageRegistry* images{};
    // Borrowed, as `material` is.
    const LentImageVariants* variants{};
    const PartRaster* backplane{};
    std::uint32_t backplane_margin{};
    std::uint64_t backplane_generation{};
    // The backplane image's top-left pixel in the window's device pixels.
    Point backplane_origin{};
    bool reduced_transparency{};
};

// Slicing (E7r, owner 2026-10-22). A primitive whose raster repeats along
// an axis away from the part's edges may be baked once, short along that
// axis (the reference), and any longer part assembled from it by copying:
// the reference's pixels at the start and the end of the axis (the
// corners and edge strips) are copied as they are, and one interior pixel
// is repeated between them (the centre), or one interior run of a pattern's
// period (SliceAxis::period, a dithered material). Every part of one material,
// state and scale that differs only in its sliced lengths shares one bake.
//
// How one axis of a part is drawn.
enum class SliceAxisOutcome : std::uint8_t {
    // Assembled from the reference, which is shorter along the axis.
    sliced,
    // Baked at the part's own length along the axis, because a layer
    // varies along it (such as a gradient along the axis).
    varies,
    // Baked at its own length: the part's box does not start and end on
    // device-pixel boundaries along the axis, so a shorter reference would
    // not have the same pixels at its far end.
    fractional,
    // Baked at its own length: the part is no longer than the reference
    // would be, as when its blur and radius exceed half its length.
    too_small,
};

// One axis of a slice plan. For a sliced axis, the destination's first
// `start` and last `end` device pixels are the reference's own; every
// pixel i between them is the reference's pixel start + (i − start) mod
// `period`. The period is 1, one repeated pixel, unless the raster carries
// a pattern anchored to the part that repeats every `period` pixels (a
// dithered material, MaterialDither): then the reference holds a whole
// period past `start` and differs from the part's length by whole periods,
// so the copies keep the pattern.
struct GUI_FORMS_API SliceAxis final {
    SliceAxisOutcome outcome{SliceAxisOutcome::varies};
    std::uint32_t start{};
    std::uint32_t end{};
    std::uint32_t period{1U};
};

// How a part's raster may be assembled (EnginePrimitive::slice_plan).
// `slices` is false for a primitive that never slices: its parts are baked
// whole and nothing is counted. Otherwise, when either axis is sliced, the
// reference is the target at `reference_size` with `reference_extent`;
// when neither is, the part is baked whole and counted as a fallback.
struct GUI_FORMS_API PrimitiveSlicePlan final {
    bool slices{};
    SliceAxis across{};
    SliceAxis down{};
    Size reference_size{};
    DeviceExtent reference_extent{};
};

class EnginePrimitive;

// What a primitive derives once from a part's geometry and fixed
// parameters, and reuses for every rasterization in which only animated
// parameters change. The dithered bulge, for example, derives its tone's
// full-amplitude extremes here, so that the rise does not recompute them
// each frame.
//
// The caller owns a preparation and keeps it beside the part's raster (in
// E2, the part's record in the window's EnginePartTable). It records what it
// was derived from: the primitive, the part's logical size and scale, the
// read states it saw, and every parameter value. A rasterization whose
// primitive, size, scale, read states or non-animated parameters differ is
// refused as stale; the caller prepares again. A preparation borrows
// nothing: `primitive` is compared, never called through.
struct GUI_FORMS_API PrimitivePreparation final {
    // The primitive that derived the values; null until the first
    // successful prepare().
    const EnginePrimitive* primitive{};
    Size logical_size{};
    DeviceScale scale{};
    // The target's states restricted to the descriptor's states_read.
    AnatomyStateSet states_read{};
    // The parameters as they were at preparation. Only non-animated values
    // are compared.
    PrimitiveParameterBlock parameters{};
    // `count` live values, as many as the descriptor's prepared_values; what
    // each means is the primitive's own business.
    std::array<double, maximum_prepared_values> values{};
    std::size_t count{};
};

enum class PreparationOutcome : std::uint8_t {
    // The preparation now holds values for this primitive, target and
    // parameters.
    prepared,
    // Nothing was changed: the size or scale is out of range.
    invalid_target,
    // Nothing was changed: the parameters do not match the declarations.
    invalid_parameters,
};

enum class RasterOutcome : std::uint8_t {
    // Every pixel of the destination was written.
    painted,
    // The destination has no pixels; nothing was written.
    empty,
    // Nothing was written: the destination's shape does not fit its buffer.
    invalid_view,
    // Nothing was written: the size or scale is out of range, or the
    // primitive does not accept the target (EnginePrimitive::accepts).
    invalid_target,
    // Nothing was written: the parameters do not match the declarations.
    invalid_parameters,
    // Nothing was written: the preparation was made by another primitive,
    // or for another size, scale, read state or non-animated parameter.
    stale_preparation,
};

// How a primitive behind the overlay seam is composited (E9-A, owner
// 2026-10-22: the seam serves the sheen only). Its raster is `size`, in
// logical units, baked once per (size, state, scale) and never per
// pointer position; the engine composites it with its centre across at the
// part-local pointer, its top at the part's top, clipped to the part's
// shape, and damages only the part's extent when the pointer moves within
// it. While the pointer is not over the part, and always under reduced
// motion, its centre rests at `rest` times the part's width. It follows the
// part's presentational scale as a face does: baked at the scaled size at
// rest, and stretched with the face while the scale moves.
struct GUI_FORMS_API PointerOverlay final {
    Size size{};
    double rest{};
};

// One kind of effect. A primitive is immutable and holds no per-part state:
// one instance serves every part, on the UI thread. Per-part derived values
// live in the caller's PrimitivePreparation.
class GUI_FORMS_API EnginePrimitive {
public:
    virtual ~EnginePrimitive() = default;

    [[nodiscard]] virtual const PrimitiveDescriptor& descriptor() const noexcept = 0;

    // Whether the primitive draws nothing at all with `parameters`, such as
    // a bulge at amplitude 0 (the flat plane): the engine then composites
    // nothing and keeps no raster for the part (E7). False unless the
    // primitive says otherwise. `parameters` match the declarations.
    [[nodiscard]] virtual bool blank(const PrimitiveParameterBlock& parameters) const noexcept;

    // The shape the primitive draws with `parameters`, its radius in
    // logical units (E8): what the part's hit region derives from (design
    // language §7.4 rule 1). A rectangle filling the box unless the
    // primitive says otherwise. `parameters` match the declarations.
    [[nodiscard]] virtual PartShape shape(const PrimitiveParameterBlock& parameters) const noexcept;

    // How far inside a part of `logical` size, at display `scale` and the
    // window's `text_scale`, its text may begin (E8, T1's hand-off: text is
    // published as a layout slot, ThemeEngine::text_slot). In logical
    // units; the slot inside them lies within the shape the primitive
    // draws. None unless the primitive says otherwise. `parameters` match
    // the declarations; `logical` and `scale` are positive and finite.
    [[nodiscard]] virtual Insets content_insets(const PrimitiveParameterBlock& parameters, Size logical,
                                                DeviceScale scale, double text_scale) const noexcept;

    // The recipe role (ControlVisualRole) whose theme material the
    // primitive draws with `parameters` (E8-1) on a part of recipe family
    // `family`: the engine then resolves the role's recipe for the part's
    // state and lends its material in the target, with the material fields
    // the theme's rules set in place of the recipe's (EC1). None unless the
    // primitive says otherwise. `parameters` match the declarations.
    [[nodiscard]] virtual std::optional<ControlVisualRole> theme_material(const PrimitiveParameterBlock& parameters,
                                                                          ControlVisualRole family) const noexcept;

    // Whether the primitive draws the theme's overlay shadow (E9, GIVEN
    // E1-1): the engine then lends the theme's overlay shadow layers for the
    // part's presentation as a material of one transparent fill and those
    // shadows, at the part's corner radius, and draws nothing when the theme
    // casts none. False unless the primitive says otherwise.
    [[nodiscard]] virtual bool lends_overlay_shadow() const noexcept;

    // How the primitive is composited over a part of `part` size when it
    // stands behind the overlay seam (PointerOverlay); none for a primitive
    // drawn over the part's own box, unless the primitive says otherwise.
    // `parameters` match the declarations.
    [[nodiscard]] virtual std::optional<PointerOverlay> pointer_overlay(const PrimitiveParameterBlock& parameters,
                                                                        Size part) const noexcept;

    // How many device pixels past the destination, on every side, the
    // primitive reads the backplane at display `scale` (PrimitiveTarget::
    // backplane); none for a primitive that does not sample it, unless the
    // primitive says otherwise. `parameters` match the declarations.
    [[nodiscard]] virtual std::optional<std::uint32_t> backplane_margin(const PrimitiveParameterBlock& parameters,
                                                                        DeviceScale scale) const noexcept;

    // Fills the front of `requests` with the image variants a bake of this
    // target needs, at most one per fill layer, and returns how many; the
    // engine lends them in PrimitiveTarget::variants. None unless the
    // primitive says otherwise. Called as accepts() is, with a target the
    // primitive accepts; `requests` holds maximum_image_variants entries.
    [[nodiscard]] virtual std::size_t image_variants(const PrimitiveParameterBlock& parameters,
                                                     const PrimitiveTarget& target,
                                                     std::span<ImageVariantRequest> requests) const noexcept;

    // Whether the primitive will draw this target with `parameters`, such as
    // a material whose bake would exceed its bound. A primitive that refuses
    // is drawn by the built-in recipe instead, and the engine counts the
    // refusal. True unless the primitive says otherwise. Called only with a
    // target in range and parameters that match the declarations.
    [[nodiscard]] virtual bool accepts(const PrimitiveParameterBlock& parameters,
                                       const PrimitiveTarget& target) const noexcept;

    // How a raster of `extent` for this target may be assembled from a
    // shorter reference bake (E7r slicing). A plan that slices an axis
    // promises that assembling the reference gives the bytes a whole bake
    // would. Never slices unless the primitive says otherwise. Called as
    // accepts() is, with a target the primitive accepts.
    [[nodiscard]] virtual PrimitiveSlicePlan slice_plan(const PrimitiveParameterBlock& parameters,
                                                        const PrimitiveTarget& target,
                                                        DeviceExtent extent) const noexcept;

    // Validates the target and the parameters, then derives the
    // preparation's values. Anything but `prepared` leaves `preparation`
    // unchanged. Its cost is the descriptor's preparation_per_device_pixel
    // for each device pixel of the part's box.
    [[nodiscard]] PreparationOutcome prepare(const PrimitiveParameterBlock& parameters,
                                             const PrimitiveTarget& target,
                                             PrimitivePreparation& preparation) const noexcept;

    // Validates the destination, the target, the parameters and the
    // preparation once, then fills every pixel of the destination with
    // premultiplied BGRA8. Anything but `painted` leaves the destination's
    // bytes unchanged.
    [[nodiscard]] RasterOutcome rasterize(const PrimitiveParameterBlock& parameters,
                                          const PrimitiveTarget& target,
                                          const PrimitivePreparation& preparation,
                                          const PartRasterView& destination) const noexcept;

private:
    // Called only with a target whose size is positive and finite and whose
    // scale is in range, and parameters that match the descriptor. Writes
    // exactly the descriptor's prepared_values entries of `values`.
    virtual void derive(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                        std::span<double> values) const noexcept = 0;

    // Called only with a validated, non-empty destination, a target and
    // parameters as for derive(), and the values derive() gave for them.
    virtual void fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                      std::span<const double> prepared, const PartRasterView& destination) const noexcept = 0;
};

enum class RegistrationOutcome : std::uint8_t {
    registered,
    registry_full,
    name_not_snake_case,
    duplicate_name,
    no_roles,
    role_out_of_range,
    state_out_of_range,
    too_many_parameters,
    parameter_name_not_snake_case,
    duplicate_parameter_name,
    parameter_range_invalid,
    default_out_of_range,
    introduced_after_engine,
    too_many_prepared_values,
    input_out_of_range,
    choices_invalid,
};

[[nodiscard]] GUI_FORMS_API const char* registration_outcome_name(RegistrationOutcome outcome) noexcept;

inline constexpr std::size_t maximum_registered_primitives = 64;

// A fixed table of primitives, in registration order. It borrows each
// primitive, which must outlive the registry; the first-party primitives
// have static storage.
class GUI_FORMS_API PrimitiveRegistry final {
public:
    // Checks the primitive's descriptor and adds it. Anything but
    // `registered` leaves the registry unchanged.
    [[nodiscard]] RegistrationOutcome register_primitive(const EnginePrimitive& primitive) noexcept;

    // The primitive registered under `name`, or null.
    [[nodiscard]] const EnginePrimitive* find(std::string_view name) const noexcept;

    [[nodiscard]] std::span<const EnginePrimitive* const> primitives() const noexcept;

private:
    std::array<const EnginePrimitive*, maximum_registered_primitives> entries_{};
    std::size_t count_{};
};

// The first-party primitives, registered once in a fixed order on first use.
// The registry and its primitives live for the rest of the program.
[[nodiscard]] GUI_FORMS_API const PrimitiveRegistry& builtin_primitive_registry();

} // namespace gui_forms
