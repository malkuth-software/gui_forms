#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/engine/engine_expression/engine_expression.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gui_forms {

// One window's records of the parts the engine has painted with a key (T2
// plan, EnginePartTable). A record is the engine's memory of one part from
// frame to frame. E2 keeps its last states, its preparation, its cached
// raster and the paint on which it was last drawn; later phases add the
// time it entered each state (E3) and its transitions (E7).
//
// A part painted without a key has no record and is drawn statically: no
// cached raster either.
//
// Bounds. A part that is not an item's is one of its control's declared
// parts, so those records are bounded by the window's controls and released
// with them. Item parts are bounded by maximum_item_part_records: a
// virtualized control can paint any number of items over time, so beyond
// the cap the least recently painted item record is evicted, as if
// released. A record whose motion is unsettled (motion_unsettled) is never
// evicted; the table then holds more than the cap until its motion settles.
// No control publishes when it unrealizes an item yet, so records are
// released only by eviction and by their control leaving the window.
//
// The raster cache (GIVEN F2-1): a record keeps the raster its primitive
// last drew while what drew it is unchanged, and reuses the raster's
// storage when it must draw again at the same size. The table holds at most
// raster_cache_budget_bytes of cached rasters and evicts the least recently
// painted part's raster first to make room. A raster is invalidated by
// generation: invalidate_raster() for one part, invalidate_rasters() for the
// whole table (a theme change). A raster's transparency passes through as
// its own premultiplied alpha when it is composited. Releasing or evicting a
// record releases its raster.
//
// The shared bakes (E7r, owner 2026-10-22): rasters the table owns and
// every part may use, keyed by the request that drew them, held within
// shared_bake_budget_bytes with the least recently used evicted first. A
// theme change (invalidate_rasters) drops them all; invalidating one part
// bakes again the shared raster it uses. They are of two kinds.
// - Slice references. A primitive that slices (EnginePrimitive::slice_plan)
//   bakes one reference per distinct request less its sliced lengths: per
//   material, state and scale, say, for every button of a material,
//   whatever their widths. A part's raster is assembled from it by copying
//   (its corners and edges as they are, its centre repeated) and cached
//   like any other. Labels, icons and the focus ring are parts of their
//   own, drawn over the face, so they never split a face's reference.
// - Keyframes (PartRasterRequest::shared). A part whose animated parameters
//   stand at keyframe levels, or whose ambient effect is at one of its loop's
//   frames, uses the shared raster for its request as its own, with no copy;
//   parts of the same size and state share them.

// Which record a handle names: a slot of the table and the slot's
// generation when the record was made. Releasing a record advances its
// slot's generation, so a handle kept past the release is stale and finds
// nothing, even after the slot is reused.
struct GUI_FORMS_API PartRecordHandle final {
    std::uint32_t slot{};
    std::uint32_t generation{};

    friend constexpr bool operator==(PartRecordHandle left, PartRecordHandle right) noexcept {
        return left.slot == right.slot && left.generation == right.generation;
    }
};

// A part's cached raster and what drew it: the primitive, every parameter
// (animated ones included, since they change the pixels), the target with
// its states restricted to those the primitive reads, and the extent. The
// record owns it; `raster` is the cache's reference, and recorded commands
// that composite the raster hold others.
//
// A material the target lent is kept as a copy, compared by value: the
// theme's recipe that lent it may be replaced, and another recipe may later
// occupy its address. The kept target's borrowed pointers are cleared. When
// the material has an image fill, `image_revision` is the image registry's
// revision it was drawn at, so any change to the registry redraws it.
struct GUI_FORMS_API CachedPartRaster final {
    std::shared_ptr<PartRaster> raster{};
    const EnginePrimitive* primitive{};
    PrimitiveParameterBlock parameters{};
    PrimitiveTarget target{};
    DeviceExtent extent{};
    std::optional<SurfaceMaterial> material{};
    std::uint64_t image_revision{};
    // Set when `raster` drew a procedural fill's ground because its pattern
    // was not ready (E10): the table's pattern generation then. Such a
    // raster is current only until the table makes another pattern.
    std::optional<std::uint64_t> awaited_patterns{};
    // Whether `raster` is one of the table's shared bakes (a keyframe): it
    // then counts against shared_bake_budget_bytes, not the part cache's,
    // and is never drawn over.
    bool shared{};
    // The record's and the table's raster generations when `raster` was
    // drawn; invalidation advances the current ones.
    std::uint64_t raster_generation{};
    std::uint64_t raster_table_generation{};
    // N37: the table's recency list of part rasters it budgets (held, not
    // shared), least recently painted first. Observing links, kept by the
    // table; `listed` says whether this raster is on the list.
    bool listed{};
    CachedPartRaster* older_raster{};
    CachedPartRaster* newer_raster{};
};

// A part's assembled material (T3 EC1): its recipe's material, the floor,
// with the winning material fields of the theme's rules in place, and what
// it was assembled from. It is assembled again only when that changes:
// - `rules_serial` and `fields`: the compiled rules' serial
//   (CompiledThemeRules::serial) and the winning declaration of each field,
//   by address within those rules (null for none);
// - `floor`: a copy of the recipe material, compared by value, since the
//   theme's recipe may be replaced and another may occupy its address;
// - `box`: the part's box size, for fields that read it
//   (material_fields_read_box), else empty.
struct GUI_FORMS_API AssembledPartMaterial final {
    std::uint64_t rules_serial{};
    // The serial of the part's inline style, or 0 (T3 P9).
    std::uint64_t inline_serial{};
    // The serial of the part's user theme, or 0 (T3 user themes).
    std::uint64_t user_serial{};
    std::array<const MaterialDeclaration*, material_field_count> fields{};
    Size box{};
    // The part's em and text colour, where a field reads them (T3 P8c);
    // else 0 and transparent black.
    double em{};
    Color current_colour{};
    SurfaceMaterial floor{};
    SurfaceMaterial material{};
};

// One of a window's assemblies for parts that keep none of their own (a
// part without a record, and every part's derived dark variant): the
// assembly, the role whose dark variant darkened it (none for a light
// assembly) and whether it holds one yet.
struct GUI_FORMS_API UnkeptPartAssembly final {
    AssembledPartMaterial assembled{};
    std::optional<ControlVisualRole> darkened{};
    bool filled{};
};

// How many assemblies a window keeps for such parts: a strip's tabs share
// a few identities (resting, selected, hot), so a few entries keep a
// repeated paint from assembling, or allocating, again.
inline constexpr std::size_t unkept_part_assembly_count = 8U;

class SurfaceRoot;

// One property's transition on a part (E7): an engine property or the
// parameter at `parameter`, the machine and the state of it the part was
// last painted in (state none when the part's states matched none), and
// the property's motion value, made the first time the part is painted
// with the transition. The machine is CompiledTransition::machine of the
// compiled rules whose serial is `rules_serial`; a machine of other rules
// (a theme replaced) is compared by state alone.
struct GUI_FORMS_API PartTransitionChannel final {
    PresentationProperty property{PresentationProperty::parameter};
    std::size_t parameter{};
    std::uint64_t rules_serial{};
    std::size_t machine{};
    std::optional<std::size_t> state{};
    std::optional<MotionValue> value{};
};

// A part's running transitions (E7; T2 plan, "How motion is driven"). The
// part's record owns it through a pointer, so that its motion values, which
// never move, keep their addresses however the table's storage grows. Each
// value is owned by the part's control (MotionValue's owner), runs on the
// window's MotionClock, and reports each step to on_step, which repaints
// the part's extent on the control that recorded it. The record is released
// with its control, which releases the values from the clock.
class GUI_FORMS_API PartMotion final {
public:
    PartMotion(SurfaceRoot& window, PartRecordHandle record) noexcept;
    PartMotion(const PartMotion&) = delete;
    PartMotion& operator=(const PartMotion&) = delete;

    // Whether any of the part's values has not settled.
    [[nodiscard]] bool moving() const noexcept;
    // A value's step, from the clock: the part's extent repaints.
    void on_step(const MotionStep& step);

    std::array<PartTransitionChannel, maximum_part_transitions> channels{};

private:
    // Observed: the window owns the table that owns this record.
    SurfaceRoot* window_;
    PartRecordHandle record_;
};

// One painted part's record. The identity strings are owned copies of the
// key the part was first painted with.
struct GUI_FORMS_API EnginePartRecord final {
    std::string control;
    std::string part;
    std::string item;
    // Set only for the index fallback (PartKey::index_fallback).
    std::optional<std::size_t> item_index{};
    // The states the part was last painted in.
    AnatomyStateSet states{};
    // What the part's primitive derived for its geometry; empty until a
    // primitive draws the part (never, before themes select one in T3).
    PrimitivePreparation preparation{};
    // When the part was last painted: the number of window paints completed
    // before the one that drew it. The cache evicts the smallest first.
    std::uint64_t last_painted{};
    // True while a transition or motion value of the part has not settled
    // (E3, E7): such a record is never evicted. Nothing sets it before E3.
    bool motion_unsettled{};

    // What the part's last paint read, and where it was drawn (E6). An
    // input change damages exactly this extent of this control.
    // - `inputs_read`: the inputs its primitive and its expressions read,
    //   as expression input bits (expression_input_bit); zero for a part
    //   the built-in recipe draws. Set only through
    //   EnginePartTable::note_paint, which keeps the table's reader counts.
    // - `painted_by`: the stable ID of the control whose display chunk
    //   recorded the part. It is the control painting the part, or one
    //   that paints on its behalf, such as a docking site's drag layer.
    // - `extent`: the part's bounds and its primitive's outsets, in that
    //   control's local coordinates.
    std::uint64_t inputs_read{};
    std::string painted_by{};
    Rect extent{};
    // Whether the part's last paint sampled the window's backplane (E9-B):
    // backplane damage within its extent repaints it.
    bool reads_backplane{};
    // When the part entered the states it was last painted in, on the
    // window's motion clock; time_in_state counts from it.
    FrameTime state_entered{};

    // The part's transitions (E7), made the first time it is painted with
    // one; null for a part without any.
    std::unique_ptr<PartMotion> motion{};
    // For a part with a face cross-fade (E7r form a): the face it showed
    // last and the states it showed it in, and while a fade runs the face
    // it fades from. A fade starts only when the states change (a discrete
    // change), never when a keyframe step or a scale replaces the face.
    // The faces are shared with the raster cache and the display; they keep
    // a replaced face alive only until its fade settles, and are released
    // on the first paint without a face transition.
    std::shared_ptr<const PartRaster> shown_face{};
    std::shared_ptr<const PartRaster> fading_face{};
    AnatomyStateSet face_states{};
    // The states whose keyframes were last queued for idle frames (E7r):
    // queued again when the part's states change.
    std::optional<AnatomyStateSet> keyframe_states{};
    // Whether the part's last paint had an ambient effect, and whether that
    // effect was refused for its area (E7). An ambient part repaints when
    // ambient motion resumes; a refusal is counted once, when it begins.
    bool ambient{};
    bool ambient_refused{};
    // Whether the part's primitive refused its last paint (E8c,
    // EnginePrimitive::accepts), so that it was drawn by the built-in
    // recipe; a refusal is counted once, when it begins.
    bool primitive_refused{};

    // The cached raster and what drew it, made the first time a primitive
    // draws the part, so that a record no primitive draws stays small.
    std::unique_ptr<CachedPartRaster> cached{};
    // The part's assembled material, made the first time a material
    // primitive draws the part with a material field a rule sets (EC1).
    std::unique_ptr<AssembledPartMaterial> assembled_material{};
    // The record's current raster generation; invalidate_raster advances it.
    std::uint64_t generation{1U};
};

// The most item-part records one window keeps before it evicts the least
// recently painted (see EnginePartTable). A record is about 800 bytes on
// 64-bit hosts (its preparation is about half of that); with its slot, its
// lookup text and its index entry it takes about 1 KB, so the cap costs at
// most about 4 MB per window. A part a primitive draws also has its
// CachedPartRaster (about 600 bytes, and a copy of its material when a
// material primitive draws it), two more copies of the material when the
// theme's rules set its fields (AssembledPartMaterial), and its raster,
// which the raster
// cache's budget bounds. The cap covers far more item parts than a window
// shows at once: a record evicted while its part is still on screen is only
// made again on the next paint. Each eviction is O(1).
inline constexpr std::size_t maximum_item_part_records = 4096;

// What a part needs drawn: the primitive a rule selected, its parameters,
// the part's target, and the destination extent in device pixels (normally
// the logical size grown by the primitive's outsets, times the scale, rounded
// up).
//
// `shared` asks for the table's shared raster for the request (a keyframe,
// E7r) in place of one of the part's own: set for a part whose animated
// parameters stand at keyframe levels or whose ambient effect is at a frame
// of its loop, so that its rasters are baked once and kept for every
// transition after.
struct GUI_FORMS_API PartRasterRequest final {
    const EnginePrimitive* primitive{};
    PrimitiveParameterBlock parameters{};
    PrimitiveTarget target{};
    DeviceExtent extent{};
    bool shared{};
};

enum class PartRasterStatus : std::uint8_t {
    // The cached raster is still current; nothing was drawn.
    cached,
    // The primitive drew the part into the cached raster's own storage.
    reused_storage,
    // The primitive drew the part into new storage, now cached.
    new_storage,
    // The primitive drew the part, but the raster alone exceeds the cache
    // budget, so it is returned and not kept.
    uncached,
    // The raster is the table's shared bake for the request (a keyframe),
    // baked now or before.
    shared,
    // Nothing was drawn: the handle is stale.
    stale_record,
    // Nothing was drawn: no primitive, or the primitive refused the target,
    // the parameters or the extent. The cache is unchanged.
    refused,
};

struct GUI_FORMS_API PartRasterResult final {
    // The part's raster, or null when status is stale_record or refused.
    std::shared_ptr<const PartRaster> raster{};
    PartRasterStatus status{PartRasterStatus::refused};
};

// The raster cache's counters, for instrumentation.
struct GUI_FORMS_API RasterCacheStatistics final {
    // Rasters held and their bytes now.
    std::size_t rasters{};
    std::size_t bytes{};
    // Requests answered from the cache, drawn into reused storage, drawn
    // into new storage, and drawn without caching.
    std::uint64_t hits{};
    std::uint64_t storage_reuses{};
    std::uint64_t storage_allocations{};
    std::uint64_t uncached{};
    // Rasters evicted to keep within the budget.
    std::uint64_t evictions{};
    // Every time a primitive drew pixels: a whole part or a slice
    // reference. Compositing a cached raster, or assembling one from a
    // cached reference, is not a bake.
    std::uint64_t bakes{};
    // Slicing (E7r): references baked; parts assembled from a reference
    // already held; parts of a slicing primitive baked whole because
    // neither axis could be sliced, and axes kept whole, each counted per
    // draw.
    std::uint64_t slice_bakes{};
    std::uint64_t slice_hits{};
    std::uint64_t slice_whole_fallbacks{};
    std::uint64_t slice_axis_fallbacks{};
    // Keyframes (E7r; owner A, 2026-10-22): baked in idle frames ahead of
    // use, baked on demand the first time a paint reached one not yet held,
    // and taken from the shared bakes already held; and those queued for
    // idle frames now.
    std::uint64_t keyframe_idle_bakes{};
    std::uint64_t keyframe_demand_bakes{};
    std::uint64_t keyframe_hits{};
    std::size_t idle_keyframes_queued{};
    // The shared bakes: evicted to keep within their budget, held now, and
    // their bytes. The bytes, and the evictions, include image variants'.
    std::uint64_t shared_evictions{};
    std::size_t shared_rasters{};
    std::size_t shared_bytes{};
    // Image faces' sized variants (owner, 2026-10-22): made from their
    // images, lent from those held, and held now. Each is made once while
    // it is held; a theme change or a change to the window's images drops
    // them all.
    std::uint64_t image_variant_bakes{};
    std::uint64_t image_variant_hits{};
    std::size_t image_variants{};
    // Procedural fills' patterns (E10): generated in idle frames, lent from
    // those held, and asked for by a bake before they were ready (the fill
    // then drew its ground); held now, and queued for idle frames now.
    // Their bytes count with the shared bakes'. A theme change keeps them.
    std::uint64_t procedural_generations{};
    std::uint64_t procedural_hits{};
    std::uint64_t procedural_placeholders{};
    std::size_t procedural_patterns{};
    std::size_t procedural_queued{};
    // The window's backplane images (E9-B, owner 2026-10-22): made, lent
    // from those held, and held now; those of them the window's renderer
    // replayed whole (stage T5, C6); and the backplane commands their
    // making skipped. Without a renderer that replays, the core bakes a
    // backplane image from the window's backplane material and the
    // backplane plane's engine part rasters alone (option 1), so text and
    // other commands drawn in the backplane plane are absent from glass;
    // each one is counted here every time an image is made over it. A
    // replayed image skips nothing.
    std::uint64_t backplane_bakes{};
    std::uint64_t backplane_hits{};
    std::size_t backplane_images{};
    std::uint64_t backplane_replays{};
    std::uint64_t backplane_skipped_commands{};
    // Bakes whose image mask's variant was asked for and not lent (E9.6):
    // the part, or the backplane region, was drawn unmasked, as the
    // built-in path draws it.
    std::uint64_t image_mask_fallbacks{};
};

// One backplane image's region (E9-B): `extent` device pixels at `scale`,
// its top-left pixel at `origin` in the window's device pixels (the
// window's logical origin times the scale; possibly fractional).
struct GUI_FORMS_API BackplaneRegion final {
    Point origin{};
    DeviceExtent extent{};
    DeviceScale scale{};

    friend bool operator==(const BackplaneRegion& left, const BackplaneRegion& right) noexcept {
        return left.origin == right.origin && left.extent == right.extent && left.scale == right.scale;
    }
};

// The most bytes of cached rasters one window keeps (GIVEN F2-1: 8 MB).
inline constexpr std::size_t raster_cache_budget_bytes = 8U * 1024U * 1024U;

// The most bytes of shared bakes one window keeps (E7r), apart from
// raster_cache_budget_bytes: slice references, a few kilobytes each for a
// button at scale 2, and keyframes, ten whole rasters for each size and
// state of a part with an animated parameter (some 200 KB for a large
// ribbon button at scale 1.5). A proposed default.
inline constexpr std::size_t shared_bake_budget_bytes = 4U * 1024U * 1024U;

// The most keyframes queued for idle frames at once (E7r): ten levels for
// each of about six parts. A keyframe that does not fit is baked on demand
// when a paint first reaches it.
inline constexpr std::size_t maximum_idle_keyframes = 64U;

// The most procedural patterns queued for idle frames at once (E10). A
// pattern that does not fit is queued again when a paint next asks for it.
inline constexpr std::size_t maximum_queued_patterns = 32U;

// What EnginePartTable::generate_pattern did (E10): nothing was queued
// (none); a pattern was made, and the parts whose rasters drew a ground
// are to be repainted (made); or a pattern was made and a raster without a
// record had drawn a ground, so that every part is to be repainted
// (made_unrecorded).
enum class PatternGeneration : std::uint8_t { none, made, made_unrecorded };

// What acquire() did.
struct GUI_FORMS_API PartRecordAcquisition final {
    PartRecordHandle handle{};
    // A new record was made for the key.
    bool created{};
    // Item records evicted to keep within the cap; their handles are stale.
    std::size_t evicted{};
};

// The table. It owns its records. Every member runs on the window's UI
// thread; the table is not shared between windows.
class GUI_FORMS_API EnginePartTable final {
public:
    // The table keeps at most `item_record_cap` item-part records (at least
    // 1); the window's table uses maximum_item_part_records.
    explicit EnginePartTable(std::size_t item_record_cap = maximum_item_part_records) noexcept;

    // The record for `control_id` and `key`, made if there is none, and
    // marked the most recently painted item record when it is an item's. A
    // new record reuses a released slot before the table grows; a new item
    // record beyond the cap evicts the least recently painted settled item
    // record (O(1) unless records with unsettled motion are passed over).
    // If an allocation fails, std::bad_alloc propagates and the table is
    // unchanged.
    [[nodiscard]] PartRecordAcquisition acquire(std::string_view control_id, const PartKey& key);

    // The live record for `control_id` and `key`, if there is one. For
    // inspection: it builds its lookup text in a string of its own.
    [[nodiscard]] std::optional<PartRecordHandle> find(std::string_view control_id, const PartKey& key) const;

    // The record a handle names, or null when the handle is stale (its
    // record was released) or names no slot. The pointer is valid until the
    // next acquire() or release on this table.
    [[nodiscard]] EnginePartRecord* record(PartRecordHandle handle) noexcept;
    [[nodiscard]] const EnginePartRecord* record(PartRecordHandle handle) const noexcept;

    // Releases every record of the control with this stable ID, as when it
    // leaves the window, and returns how many. Their handles become stale
    // and their cached rasters are released. The cost is one pass over the
    // table's slots.
    std::size_t release_control(std::string_view control_id) noexcept;

    // Live records, and those of item parts among them.
    [[nodiscard]] std::size_t record_count() const noexcept { return live_; }
    [[nodiscard]] std::size_t item_record_count() const noexcept { return item_records_; }
    // Record storage allocated, live or free: released and evicted slots are
    // reused before it grows, so it stays near the most records live at once.
    [[nodiscard]] std::size_t slot_count() const noexcept { return slots_.size(); }

    // The part's raster for `request`, painted on window paint
    // `paint_sequence` (the record's new last_painted).
    // - The cached raster is returned unchanged while its primitive,
    //   parameters, target, read states and extent are the request's and
    //   neither generation has advanced.
    // - Otherwise the primitive draws the part. It draws into the cached
    //   raster's storage when the extent and scale are unchanged and no
    //   recorded command still holds that raster (a held raster is never
    //   rewritten, since a recording shows it), and into new storage
    //   otherwise.
    // - New storage is made room for by evicting other parts' rasters,
    //   least recently painted first, until the cache fits its budget. A
    //   raster larger than the whole budget is drawn and returned but not
    //   kept.
    // - The record's preparation is remade when the primitive refuses it as
    //   stale.
    // If the primitive refuses, nothing is evicted and the record keeps its
    // cached raster. Allocation failure propagates as std::bad_alloc, with
    // the record unchanged.
    [[nodiscard]] PartRasterResult raster_for(PartRecordHandle handle, const PartRasterRequest& request,
                                              std::uint64_t paint_sequence);

    // Idle keyframes (E7r; owner A, 2026-10-22). queue_idle_keyframe keeps
    // a copy of `request` (its lent material copied, its borrows cleared) to
    // bake in an idle frame, unless the table holds or queues it already or
    // the queue is full. bake_idle_keyframe bakes the oldest queued keyframe
    // the table does not hold, lending it `images` (a queued material whose
    // images changed is dropped), and returns whether any remain queued. A
    // theme change (invalidate_rasters) empties the queue. Allocation
    // failure propagates as std::bad_alloc, the queue then holding what it
    // held less the keyframe taken.
    void queue_idle_keyframe(const PartRasterRequest& request);
    bool bake_idle_keyframe(const ImageRegistry* images, std::uint64_t paint_sequence);
    [[nodiscard]] bool idle_keyframes_waiting() const noexcept;

    // Procedural patterns (E10). A bake that asks for a pattern not held
    // queues it (at most maximum_queued_patterns) and draws the fill's
    // ground; nothing is generated inside a paint. generate_pattern
    // generates the oldest queued pattern not held and holds it. Making
    // one ends the currency of every raster that drew a ground
    // (CachedPartRaster::awaited_patterns): the shared bakes among them and
    // the backplane images are dropped, and the window repaints the parts
    // (awaits_pattern), or every part when a raster without a record drew
    // one, since it records no place. A pattern that cannot be made is
    // dropped from the queue. Allocation failure propagates as
    // std::bad_alloc.
    PatternGeneration generate_pattern(std::uint64_t paint_sequence);
    [[nodiscard]] bool patterns_waiting() const noexcept;
    // Whether `cached` drew a ground for a pattern made since.
    [[nodiscard]] bool awaits_pattern(const CachedPartRaster& cached) const noexcept;

    // A raster for a part painted without a key, which has no record: drawn
    // for this paint and never kept (status uncached), assembled from a
    // slice reference when the primitive slices it, which is kept. Refused
    // as raster_for is; allocation failure propagates as std::bad_alloc.
    [[nodiscard]] PartRasterResult raster_without_record(const PartRasterRequest& request,
                                                         std::uint64_t paint_sequence);

    // Makes one part's cached raster stale; the next raster_for draws it
    // again, and bakes again the slice reference it is assembled from. A
    // stale handle does nothing.
    void invalidate_raster(PartRecordHandle handle) noexcept;
    // Makes every cached raster stale and drops every slice reference, as
    // when the theme changes.
    void invalidate_rasters() noexcept;

    [[nodiscard]] RasterCacheStatistics cache_statistics() const noexcept { return statistics_; }

    // Records what a part's paint read and where it was drawn (E6; see
    // EnginePartRecord::inputs_read). A stale handle does nothing. If the
    // control's ID cannot be copied, std::bad_alloc propagates and the
    // record is unchanged.
    void note_paint(PartRecordHandle handle, std::uint64_t inputs_read, std::string_view painted_by, Rect extent);
    // Records whether a part's last paint sampled the window's backplane
    // (E9-B), so that backplane damage under it repaints it. A stale handle
    // does nothing.
    void note_backplane_reader(PartRecordHandle handle, bool reads) noexcept;

    // The backplane image held for `region` at the backplane's damage
    // `generation`, or null; one that is held is marked used by
    // `paint_sequence` and counted a hit. Images of an earlier generation
    // are dropped first.
    [[nodiscard]] std::shared_ptr<const PartRaster> held_backplane(const BackplaneRegion& region,
                                                                   std::uint64_t generation,
                                                                   std::uint64_t paint_sequence);
    // Holds `image`, just made for `region` at `generation`, replayed
    // whole (`replayed`) or with `skipped` backplane commands left out,
    // within the shared budget
    // (least recently used evicted first; an image larger than the budget
    // is counted but not held). Allocation failure propagates as
    // std::bad_alloc and holds nothing.
    void hold_backplane(const BackplaneRegion& region, std::uint64_t generation,
                        const std::shared_ptr<const PartRaster>& image, std::uint64_t skipped, bool replayed,
                        std::uint64_t paint_sequence);
    // Draws the window's backplane `material` over `region` into `image`
    // (E9-B), for a window of `window_size` logical units whose images are
    // `images` (null for none): the material kernel over that region of
    // the window's box, with the material's image variants lent as for a
    // part. Nothing is drawn when the material is invalid.
    void bake_backplane_material(const SurfaceMaterial& material, Size window_size, const BackplaneRegion& region,
                                 const ImageRegistry* images, const PartRasterView& image,
                                 std::uint64_t paint_sequence);
    // Whether any live record reads any input of `inputs`, from the
    // table's reader counts, without a scan.
    [[nodiscard]] bool reads_any(std::uint64_t inputs) const noexcept;
    // The live record in `slot` (below slot_count()), or null for a free
    // slot: for scans over the table. Valid until the next acquire() or
    // release.
    [[nodiscard]] const EnginePartRecord* record_in_slot(std::size_t slot) const noexcept;

private:
    static constexpr std::uint32_t no_slot = 0xFFFFFFFFU;

    struct RecordSlot final {
        std::optional<EnginePartRecord> record{};
        // The record's lookup text, the key of its index_ entry; empty while
        // the slot is free.
        std::string lookup;
        // Starts at 1, so that a default handle (generation 0) is never live.
        std::uint32_t generation{1U};
        // An item record's neighbours in recency order, older and newer, or
        // no_slot at either end; unused for other records.
        bool item{};
        std::uint32_t older{no_slot};
        std::uint32_t newer{no_slot};
        // N36: the record's neighbours among its control's records, in
        // ascending slot order, or no_slot at either end.
        std::uint32_t control_previous{no_slot};
        std::uint32_t control_next{no_slot};
    };

    // Heterogeneous lookup by control id (N36): a string_view finds a
    // std::string key without making one.
    struct ControlIdHash final {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view id) const noexcept {
            return std::hash<std::string_view>{}(id);
        }
    };
    struct ControlIdEqual final {
        using is_transparent = void;
        [[nodiscard]] bool operator()(std::string_view left, std::string_view right) const noexcept {
            return left == right;
        }
    };

    // N36: a control's records, linked in ascending slot order from
    // control_first_; link before the first record, unlink anywhere.
    void link_control(std::uint32_t slot, std::string_view control_id) noexcept;
    void unlink_control(std::uint32_t slot) noexcept;
    // N37: the raster recency list.
    void list_raster_newest(CachedPartRaster& cached) noexcept;
    void unlist_raster(CachedPartRaster& cached) noexcept;

    // Recency list of item records: link at the newest end, unlink anywhere.
    void link_newest(std::uint32_t slot) noexcept;
    void unlink(std::uint32_t slot) noexcept;
    // Ends a live record's lifetime: its handles become stale and its slot
    // is free for reuse. Never allocates.
    void release_slot(std::uint32_t slot) noexcept;
    // Evicts least recently painted settled item records, never `keep`,
    // until the cap holds; returns how many.
    std::size_t evict_items(std::uint32_t keep) noexcept;

    // Writes the lookup text for `control_id` and `key` into `text`,
    // replacing its contents. Each field is written as its byte length, a
    // colon and its bytes, so that no two keys share a text whatever bytes
    // their fields hold.
    static void write_lookup_text(std::string& text, std::string_view control_id, const PartKey& key);

    // Drops a record's cached raster from the cache and its counts.
    void drop_raster(EnginePartRecord& record) noexcept;
    // Moves the reader counts from a record's old inputs to `inputs`.
    void count_readers(std::uint64_t old_inputs, std::uint64_t inputs) noexcept;
    // Evicts the least recently painted cached rasters, never `keep`'s,
    // until `incoming` more bytes fit the budget.
    // drop_raster for a record's cache.
    void drop_cached_raster(CachedPartRaster& cached) noexcept;
    void make_room(std::size_t incoming, const EnginePartRecord* keep) noexcept;
    // Whether the record's cached raster answers `request` now.
    [[nodiscard]] bool raster_current(const EnginePartRecord& record, const PartRasterRequest& request) const noexcept;

    // A shared bake: the raster and what drew it (generations unused), and
    // the paint that last used it.
    struct SharedBake final {
        CachedPartRaster cached{};
        std::uint64_t last_used{};
    };
    enum class SharedUse : std::uint8_t {
        slice_reference,
        keyframe,
        idle_keyframe,
    };
    // Draws `request` into `destination`: assembled from its slice
    // reference when the primitive slices it (baking the reference when
    // none is held, or again when `refresh`), else baked whole with
    // `preparation`, prepared again when stale. Anything but painted or
    // empty leaves `destination` unchanged. `awaited` is set to the
    // pattern generation when the drawing drew a procedural fill's ground
    // (E10), and reset otherwise.
    [[nodiscard]] RasterOutcome draw(const PartRasterRequest& request, PrimitivePreparation& preparation,
                                     const PartRasterView& destination, std::uint64_t paint_sequence, bool refresh,
                                     std::optional<std::uint64_t>& awaited);
    // The held shared bake answering `request`, or one baked and held now
    // (again in place of the held one when `refresh`); null when the
    // primitive refuses it. `use` chooses the counters. `awaited` is set
    // as draw sets it, from the held bake's record when one answers.
    [[nodiscard]] std::shared_ptr<PartRaster> shared_bake(const PartRasterRequest& request,
                                                          std::uint64_t paint_sequence, bool refresh, SharedUse use,
                                                          std::optional<std::uint64_t>& awaited);
    // Evicts the least recently used shared bakes and image variants until
    // `incoming` more bytes fit shared_bake_budget_bytes.
    void make_shared_room(std::size_t incoming) noexcept;
    // Drops every shared bake and image variant.
    void clear_shared_bakes() noexcept;
    // Drops every image variant.
    void clear_image_variants() noexcept;
    // Drops every backplane image.
    void clear_backplanes() noexcept;

    // An image variant held: what it is (its layer unused), and the paint
    // that last used it.
    struct HeldImageVariant final {
        ImageVariantRequest request{};
        std::shared_ptr<const PartRaster> raster{};
        std::uint64_t last_used{};
    };
    // Fills `lent` with the image variants `request`'s primitive asks for:
    // those held, and those not held made now and held (the first need;
    // owner, 2026-10-22). A variant that cannot be made is not lent, and
    // its fill draws nothing. Held variants made from an earlier revision
    // of the window's images are dropped first. A procedural pattern not
    // held is queued instead and not lent (E10). Returns the pattern
    // generation when a pattern was not lent, else none.
    std::optional<std::uint64_t> lend_image_variants(const PartRasterRequest& request,
                                                     std::uint64_t paint_sequence, LentImageVariants& lent);
    // Counts a bake of `request` whose primitive asked for an image mask's
    // variant (image_mask_variant_layer) that `lent` does not hold: it was
    // drawn unmasked (E9.6).
    void count_image_mask_fallback(const PartRasterRequest& request, const LentImageVariants& lent) noexcept;
    // Queues `asked`, a procedural pattern, unless it is queued already or
    // the queue is full.
    void queue_pattern(const ImageVariantRequest& asked);
    // Holds `made` for `asked` within the shared budget, as an image
    // variant is held; counts it.
    void hold_variant(const ImageVariantRequest& asked, const std::shared_ptr<PartRaster>& made,
                      std::uint64_t paint_sequence);
    // Subtracts `held`'s bytes and its count from the statistics.
    void forget_variant(const HeldImageVariant& held) noexcept;

    std::vector<RecordSlot> slots_;
    std::vector<std::uint32_t> free_slots_;
    // Lookup text to slot, one entry per live record.
    std::unordered_map<std::string, std::uint32_t> index_;
    // Each control's lowest record slot (N36).
    std::unordered_map<std::string, std::uint32_t, ControlIdHash, ControlIdEqual> control_first_;
    // The raster recency list's ends (N37).
    CachedPartRaster* oldest_raster_{};
    CachedPartRaster* newest_raster_{};
    // Reused by every lookup, so that a paint of a known part allocates
    // nothing once the string has grown to the longest key.
    std::string lookup_scratch_;
    std::size_t live_{};
    std::size_t item_records_{};
    std::size_t item_record_cap_{};
    std::uint32_t oldest_item_{no_slot};
    std::uint32_t newest_item_{no_slot};
    // Advanced by invalidate_rasters; starts at 1 so a record that never
    // cached a raster (raster_table_generation 0) is never current.
    std::uint64_t raster_table_generation_{1U};
    RasterCacheStatistics statistics_{};
    // The shared bakes, in no order; a few hundred at most, and looked up
    // only when a part's own raster is not current, so lookups scan them.
    std::vector<SharedBake> shared_bakes_;
    // The image variants, in no order; one for each image fill size in use,
    // so lookups scan them. Made from the window's images at
    // image_variant_revision_ (none before the first).
    std::vector<HeldImageVariant> image_variants_;
    std::optional<std::uint64_t> image_variant_revision_{};
    // Procedural patterns (E10) are held among image_variants_ (their
    // requests' `procedural` set) and are kept when the images or the
    // theme change. Patterns waiting for idle frames, oldest first; the
    // number of patterns made so far; and whether a raster without a
    // record (raster_without_record, or a backplane image) drew a ground
    // since the last pattern was made.
    std::deque<ImageVariantRequest> pattern_queue_;
    std::uint64_t pattern_generation_{};
    bool unrecorded_awaited_{};
    // A backplane image held: its region, and the paint that last used it.
    struct HeldBackplane final {
        BackplaneRegion region{};
        std::shared_ptr<const PartRaster> raster{};
        std::uint64_t last_used{};
    };
    // The backplane images, in no order; one for each glass part's region
    // in use, so lookups scan them. All made at backplane_generation_.
    std::vector<HeldBackplane> backplanes_;
    std::uint64_t backplane_generation_{};
    // Keyframes waiting for idle frames, oldest first; each a request kept
    // as a cached raster's description, its raster null.
    std::deque<CachedPartRaster> idle_keyframes_;
    // Live records reading each expression input, by position.
    std::array<std::uint32_t, maximum_expression_inputs> readers_{};
};

} // namespace gui_forms
