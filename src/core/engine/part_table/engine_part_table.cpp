#include "gui_forms/engine/part_table/engine_part_table.hpp"

#include "../primitives/material/image_variant.hpp"
#include "../primitives/material/material_primitive.hpp"
#include "slice_assembly.hpp"
#include "../primitives/procedural/procedural_pattern.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace gui_forms {

// A procedural pattern is at most half the shared budget (E10).
static_assert(engine_primitives::maximum_procedural_pattern_bytes <= shared_bake_budget_bytes / 2U,
              "one pattern never displaces the rest of the shared cache");

namespace {

// The material kernel, for the window's backplane material (E9-B).
const engine_primitives::MaterialPrimitive backplane_material{};

// One field of a lookup text: its byte length, a colon, its bytes.
void append_field(std::string& text, std::string_view field) {
    text += std::to_string(field.size());
    text += ':';
    text += field;
}

// The target's states that the primitive declares it reads: a change to
// any other state leaves its raster current.
AnatomyStateSet read_states_of(const EnginePrimitive& primitive, const PrimitiveTarget& target) noexcept {
    AnatomyStateSet read{};
    for (const AnatomyState state : primitive.descriptor().states_read) {
        if (target.states.contains(state)) read.insert(state);
    }
    return read;
}

// Whether two values of an input are the same, an input not published
// (NaN) being the same as another not published.
bool same_input(double left, double right) noexcept {
    if (left == right) return true;
    const bool both_unpublished = std::isnan(left) && std::isnan(right);
    return both_unpublished;
}

// Whether the inputs the primitive reads have the same values in both
// targets.
bool same_read_inputs(const EnginePrimitive& primitive, const PrimitiveTarget& left,
                      const PrimitiveTarget& right) noexcept {
    for (const EngineInput input : primitive.descriptor().inputs_read) {
        const std::size_t index = static_cast<std::size_t>(input);
        if (!same_input(left.inputs[index], right.inputs[index])) return false;
    }
    return true;
}

bool same_parameters(const PrimitiveParameterBlock& left, const PrimitiveParameterBlock& right) noexcept {
    if (left.count != right.count) return false;
    for (std::size_t index = 0U; index < left.count; ++index) {
        if (!(left.values[index] == right.values[index])) return false;
    }
    return true;
}

bool material_reads_images(const SurfaceMaterial& material) noexcept {
    for (const MaterialFillLayer& layer : material.fills) {
        if (layer.kind == MaterialFillKind::image) return true;
    }
    const bool image_mask = material.mask && (*material.mask).kind == MaterialFillKind::image;
    return image_mask;
}

// Whether a material primitive's lent material, and the images it reads,
// are those the cached raster was drawn with.
bool same_material(const CachedPartRaster& cached, const PrimitiveTarget& target) noexcept {
    if (target.material == nullptr) {
        const bool none = !cached.material;
        return none;
    }
    if (!cached.material || !(*cached.material == *target.material)) return false;
    if (cached.target.corner_radius != target.corner_radius) return false;
    if (!material_reads_images(*target.material)) return true;
    const bool same_images = target.images != nullptr && cached.image_revision == (*target.images).snapshot().revision;
    return same_images;
}

// Whether `cached` was drawn for what `request` asks: the same primitive,
// extent, size, scale, read states and inputs, parameters and material.
// Whether `cached` describes what `request` asks for (its raster aside).
bool describes(const CachedPartRaster& cached, const PartRasterRequest& request) noexcept {
    if (cached.primitive != request.primitive) return false;
    if (!(cached.extent == request.extent)) return false;
    if (!(cached.target.logical_size == request.target.logical_size)) return false;
    if (!(cached.target.scale == request.target.scale)) return false;
    const AnatomyStateSet read = read_states_of(*request.primitive, request.target);
    if (!(cached.target.states == read)) return false;
    if (!same_read_inputs(*request.primitive, cached.target, request.target)) return false;
    if (!same_parameters(cached.parameters, request.parameters)) return false;
    // A part that samples the backplane (E9-B) is current only over the
    // same backplane: its region and its damage generation.
    if (cached.target.backplane_generation != request.target.backplane_generation) return false;
    if (cached.target.backplane_margin != request.target.backplane_margin) return false;
    if (!(cached.target.backplane_origin == request.target.backplane_origin)) return false;
    const bool same = same_material(cached, request.target);
    return same;
}

// Whether `cached` holds a raster for `request`.
bool answers(const CachedPartRaster& cached, const PartRasterRequest& request) noexcept {
    if (!cached.raster) return false;
    const bool held = describes(cached, request);
    return held;
}

// Whether `held` is the variant `asked` names (the layer it serves aside).
bool same_variant(const ImageVariantRequest& held, const ImageVariantRequest& asked) noexcept {
    const bool same = held.image == asked.image && held.source_width == asked.source_width &&
                      held.source_height == asked.source_height && held.extent == asked.extent &&
                      held.source_slice == asked.source_slice && held.fixed == asked.fixed &&
                      held.scale == asked.scale && held.procedural == asked.procedural;
    return same;
}

// Records in `cached` what drew `raster` for `request`, with the lent
// material copied and the target's borrows cleared. The copy may allocate;
// `material` is made by the caller before anything is committed.
void keep_request(CachedPartRaster& cached, const PartRasterRequest& request, std::optional<SurfaceMaterial> material,
                  std::uint64_t image_revision) noexcept {
    cached.primitive = request.primitive;
    cached.parameters = request.parameters;
    cached.target = request.target;
    cached.target.states = read_states_of(*request.primitive, request.target);
    cached.target.material = nullptr;
    cached.target.images = nullptr;
    cached.target.variants = nullptr;
    cached.target.backplane = nullptr;
    cached.material = std::move(material);
    cached.image_revision = image_revision;
    cached.extent = request.extent;
}

// The bytes of a raster of `extent`. Each side is at most
// maximum_part_raster_side, so the product cannot overflow.
std::size_t raster_bytes(DeviceExtent extent) noexcept {
    const std::size_t bytes = static_cast<std::size_t>(extent.width) * static_cast<std::size_t>(extent.height) *
                              part_raster_bytes_per_pixel;
    return bytes;
}

} // namespace

PartMotion::PartMotion(SurfaceRoot& window, PartRecordHandle record) noexcept : window_(&window), record_(record) {}

bool PartMotion::moving() const noexcept {
    for (const PartTransitionChannel& channel : channels) {
        if (channel.value && !(*channel.value).settled()) return true;
    }
    return false;
}

void PartMotion::on_step(const MotionStep& step) {
    (*window_).engine_part_moved(record_, step.settled);
}

EnginePartTable::EnginePartTable(const std::size_t item_record_cap) noexcept
    : item_record_cap_(item_record_cap == 0U ? 1U : item_record_cap) {}

void EnginePartTable::link_newest(const std::uint32_t slot) noexcept {
    RecordSlot& linked = slots_[slot];
    linked.older = newest_item_;
    linked.newer = no_slot;
    if (newest_item_ != no_slot) slots_[newest_item_].newer = slot;
    newest_item_ = slot;
    if (oldest_item_ == no_slot) oldest_item_ = slot;
}

void EnginePartTable::unlink(const std::uint32_t slot) noexcept {
    RecordSlot& unlinked = slots_[slot];
    if (unlinked.older != no_slot) {
        slots_[unlinked.older].newer = unlinked.newer;
    } else {
        oldest_item_ = unlinked.newer;
    }
    if (unlinked.newer != no_slot) {
        slots_[unlinked.newer].older = unlinked.older;
    } else {
        newest_item_ = unlinked.older;
    }
    unlinked.older = no_slot;
    unlinked.newer = no_slot;
}

void EnginePartTable::link_control(const std::uint32_t slot, const std::string_view control_id) noexcept {
    const std::unordered_map<std::string, std::uint32_t, ControlIdHash, ControlIdEqual>::iterator first =
        control_first_.find(control_id);
    // acquire() made the entry, with no_slot, before committing.
    std::uint32_t previous = no_slot;
    std::uint32_t next = (*first).second;
    while (next != no_slot && next < slot) {
        previous = next;
        next = slots_[next].control_next;
    }
    slots_[slot].control_previous = previous;
    slots_[slot].control_next = next;
    if (next != no_slot) slots_[next].control_previous = slot;
    if (previous != no_slot) {
        slots_[previous].control_next = slot;
    } else {
        (*first).second = slot;
    }
}

void EnginePartTable::unlink_control(const std::uint32_t slot) noexcept {
    RecordSlot& unlinked = slots_[slot];
    if (unlinked.control_next != no_slot) slots_[unlinked.control_next].control_previous = unlinked.control_previous;
    if (unlinked.control_previous != no_slot) {
        slots_[unlinked.control_previous].control_next = unlinked.control_next;
    } else {
        const std::unordered_map<std::string, std::uint32_t, ControlIdHash, ControlIdEqual>::iterator first =
            control_first_.find(std::string_view((*unlinked.record).control));
        if (first != control_first_.end()) {
            if (unlinked.control_next == no_slot) {
                control_first_.erase(first);
            } else {
                (*first).second = unlinked.control_next;
            }
        }
    }
    unlinked.control_previous = no_slot;
    unlinked.control_next = no_slot;
}

void EnginePartTable::list_raster_newest(CachedPartRaster& cached) noexcept {
    if (cached.listed) unlist_raster(cached);
    cached.older_raster = newest_raster_;
    cached.newer_raster = nullptr;
    if (newest_raster_ != nullptr) (*newest_raster_).newer_raster = &cached;
    newest_raster_ = &cached;
    if (oldest_raster_ == nullptr) oldest_raster_ = &cached;
    cached.listed = true;
}

void EnginePartTable::unlist_raster(CachedPartRaster& cached) noexcept {
    if (!cached.listed) return;
    if (cached.older_raster != nullptr) {
        (*cached.older_raster).newer_raster = cached.newer_raster;
    } else {
        oldest_raster_ = cached.newer_raster;
    }
    if (cached.newer_raster != nullptr) {
        (*cached.newer_raster).older_raster = cached.older_raster;
    } else {
        newest_raster_ = cached.older_raster;
    }
    cached.older_raster = nullptr;
    cached.newer_raster = nullptr;
    cached.listed = false;
}

void EnginePartTable::release_slot(const std::uint32_t slot) noexcept {
    RecordSlot& released = slots_[slot];
    drop_raster(*released.record);
    count_readers((*released.record).inputs_read, 0U);
    unlink_control(slot);
    if (released.item) {
        unlink(slot);
        released.item = false;
        item_records_ = item_records_ - 1U;
    }
    index_.erase(released.lookup);
    released.record.reset();
    released.lookup.clear();
    released.generation = released.generation + 1U;
    // A generation that wraps to 0 would match default handles; skip it.
    if (released.generation == 0U) released.generation = 1U;
    // free_slots_ holds fewer entries than slots_, and acquire() reserves its
    // capacity whenever slots_ grows, so this cannot allocate.
    free_slots_.push_back(slot);
    live_ = live_ - 1U;
}

std::size_t EnginePartTable::evict_items(const std::uint32_t keep) noexcept {
    std::size_t evicted = 0U;
    std::uint32_t candidate = oldest_item_;
    while (item_records_ > item_record_cap_ && candidate != no_slot) {
        const std::uint32_t next = slots_[candidate].newer;
        const bool unsettled = (*slots_[candidate].record).motion_unsettled;
        if (candidate != keep && !unsettled) {
            release_slot(candidate);
            evicted = evicted + 1U;
        }
        candidate = next;
    }
    return evicted;
}

void EnginePartTable::write_lookup_text(std::string& text, std::string_view control_id, const PartKey& key) {
    text.clear();
    append_field(text, control_id);
    append_field(text, key.part);
    append_field(text, key.item);
    // The index is identity only for the fallback; an item with a stable ID
    // keeps its record wherever it moves.
    if (key.index_fallback()) {
        text += '#';
        text += std::to_string(*key.item_index);
    }
}

PartRecordAcquisition EnginePartTable::acquire(std::string_view control_id, const PartKey& key) {
    write_lookup_text(lookup_scratch_, control_id, key);
    const std::string& lookup = lookup_scratch_;
    const std::unordered_map<std::string, std::uint32_t>::const_iterator found = index_.find(lookup);
    if (found != index_.end()) {
        const std::uint32_t slot = (*found).second;
        if (slots_[slot].item && newest_item_ != slot) {
            unlink(slot);
            link_newest(slot);
        }
        const PartRecordAcquisition existing{PartRecordHandle{slot, slots_[slot].generation}, false};
        return existing;
    }

    // Prepare everything that can fail before the table changes.
    EnginePartRecord made{};
    made.control = std::string(control_id);
    made.part = std::string(key.part);
    made.item = std::string(key.item);
    if (key.index_fallback()) made.item_index = key.item_index;
    std::string owned_lookup = lookup;
    const bool reuse = !free_slots_.empty();
    if (!reuse && slots_.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::bad_alloc();
    }
    if (!reuse) {
        // Room for the new slot, and for its entry in free_slots_ when it is
        // released, so that release_control never allocates.
        slots_.reserve(slots_.size() + 1U);
        free_slots_.reserve(slots_.size() + 1U);
    }
    const std::uint32_t slot = reuse ? free_slots_.back() : static_cast<std::uint32_t>(slots_.size());
    // The control's entry (N36) first: a control's first record makes it
    // with no record; release_control and link_control find it.
    const bool new_control = control_first_.find(control_id) == control_first_.end();
    if (new_control) control_first_.emplace(std::string(control_id), no_slot);
    try {
        index_.emplace(owned_lookup, slot);
    } catch (...) {
        if (new_control) control_first_.erase(control_first_.find(control_id));
        throw;
    }

    // Commit: nothing below allocates.
    if (reuse) {
        free_slots_.pop_back();
    } else {
        slots_.emplace_back();
    }
    RecordSlot& target = slots_[slot];
    target.record = std::move(made);
    target.lookup = std::move(owned_lookup);
    link_control(slot, control_id);
    live_ = live_ + 1U;
    const bool item = !key.item.empty() || key.item_index.has_value();
    std::size_t evicted = 0U;
    if (item) {
        target.item = true;
        item_records_ = item_records_ + 1U;
        link_newest(slot);
        evicted = evict_items(slot);
    }
    const PartRecordAcquisition acquired{PartRecordHandle{slot, slots_[slot].generation}, true, evicted};
    return acquired;
}

std::optional<PartRecordHandle> EnginePartTable::find(std::string_view control_id, const PartKey& key) const {
    std::string lookup;
    write_lookup_text(lookup, control_id, key);
    const std::unordered_map<std::string, std::uint32_t>::const_iterator found = index_.find(lookup);
    if (found == index_.end()) return std::nullopt;
    const std::uint32_t slot = (*found).second;
    const PartRecordHandle handle{slot, slots_[slot].generation};
    return handle;
}

EnginePartRecord* EnginePartTable::record(PartRecordHandle handle) noexcept {
    if (handle.slot >= slots_.size()) return nullptr;
    RecordSlot& slot = slots_[handle.slot];
    if (slot.generation != handle.generation || !slot.record) return nullptr;
    EnginePartRecord* const live = &*slot.record;
    return live;
}

const EnginePartRecord* EnginePartTable::record(PartRecordHandle handle) const noexcept {
    if (handle.slot >= slots_.size()) return nullptr;
    const RecordSlot& slot = slots_[handle.slot];
    if (slot.generation != handle.generation || !slot.record) return nullptr;
    const EnginePartRecord* const live = &*slot.record;
    return live;
}

std::size_t EnginePartTable::release_control(std::string_view control_id) noexcept {
    // N36: the control's own records, in ascending slot order as a scan of
    // every slot would release them; the last release erases the entry.
    const std::unordered_map<std::string, std::uint32_t, ControlIdHash, ControlIdEqual>::const_iterator first =
        control_first_.find(control_id);
    if (first == control_first_.end()) return 0U;
    std::size_t released = 0U;
    std::uint32_t slot = (*first).second;
    while (slot != no_slot) {
        const std::uint32_t next = slots_[slot].control_next;
        release_slot(slot);
        released = released + 1U;
        slot = next;
    }
    return released;
}

void EnginePartTable::drop_raster(EnginePartRecord& record) noexcept {
    if (!record.cached) return;
    drop_cached_raster(*record.cached);
}

void EnginePartTable::drop_cached_raster(CachedPartRaster& cached) noexcept {
    if (!cached.raster) return;
    // A shared raster counts against the shared bakes' budget, not this one.
    if (!cached.shared) {
        statistics_.bytes = statistics_.bytes - (*cached.raster).pixels().size();
        statistics_.rasters = statistics_.rasters - 1U;
    }
    unlist_raster(cached);
    cached.raster.reset();
    cached.primitive = nullptr;
    cached.shared = false;
}

void EnginePartTable::make_room(const std::size_t incoming, const EnginePartRecord* const keep) noexcept {
    // N37: the least recently painted held raster, other than `keep`'s, is
    // the oldest on the recency list; of rasters painted in the same frame,
    // the one painted first goes first, so eviction order is fixed.
    const CachedPartRaster* const kept = keep != nullptr ? (*keep).cached.get() : nullptr;
    while (statistics_.bytes + incoming > raster_cache_budget_bytes) {
        CachedPartRaster* oldest = oldest_raster_;
        if (oldest != nullptr && oldest == kept) oldest = (*oldest).newer_raster;
        if (oldest == nullptr) return;
        drop_cached_raster(*oldest);
        statistics_.evictions = statistics_.evictions + 1U;
    }
}

bool EnginePartTable::raster_current(const EnginePartRecord& record, const PartRasterRequest& request) const noexcept {
    if (!record.cached) return false;
    const CachedPartRaster& cached = *record.cached;
    if (cached.raster_generation != record.generation) return false;
    if (cached.raster_table_generation != raster_table_generation_) return false;
    if (awaits_pattern(cached)) return false;
    const bool current = answers(cached, request);
    return current;
}

void EnginePartTable::clear_shared_bakes() noexcept {
    for (const SharedBake& held : shared_bakes_) {
        statistics_.shared_bytes = statistics_.shared_bytes - (*held.cached.raster).pixels().size();
    }
    shared_bakes_.clear();
    statistics_.shared_rasters = 0U;
    // The procedural patterns are kept: they depend on their parameters
    // alone (E10).
    clear_image_variants();
    clear_backplanes();
}

void EnginePartTable::clear_backplanes() noexcept {
    for (const HeldBackplane& held : backplanes_) {
        statistics_.shared_bytes = statistics_.shared_bytes - (*held.raster).pixels().size();
    }
    backplanes_.clear();
    statistics_.backplane_images = 0U;
}

void EnginePartTable::clear_image_variants() noexcept {
    // The procedural patterns move to the front, in order; nothing here
    // allocates.
    std::size_t kept = 0U;
    for (std::size_t index = 0U; index < image_variants_.size(); ++index) {
        if (!image_variants_[index].request.procedural) {
            forget_variant(image_variants_[index]);
            continue;
        }
        if (kept != index) image_variants_[kept] = std::move(image_variants_[index]);
        kept = kept + 1U;
    }
    image_variants_.erase(image_variants_.begin() + static_cast<std::ptrdiff_t>(kept), image_variants_.end());
}

void EnginePartTable::forget_variant(const HeldImageVariant& held) noexcept {
    statistics_.shared_bytes = statistics_.shared_bytes - (*held.raster).pixels().size();
    if (held.request.procedural) {
        statistics_.procedural_patterns = statistics_.procedural_patterns - 1U;
    } else {
        statistics_.image_variants = statistics_.image_variants - 1U;
    }
}

void EnginePartTable::make_shared_room(const std::size_t incoming) noexcept {
    while (statistics_.shared_bytes + incoming > shared_bake_budget_bytes) {
        // The least recently used of each list; ties go to the earlier
        // entry, and between the lists to the shared bake, then the image
        // variant.
        std::size_t oldest_bake = 0U;
        for (std::size_t index = 1U; index < shared_bakes_.size(); ++index) {
            if (shared_bakes_[index].last_used < shared_bakes_[oldest_bake].last_used) oldest_bake = index;
        }
        std::size_t oldest_variant = 0U;
        for (std::size_t index = 1U; index < image_variants_.size(); ++index) {
            if (image_variants_[index].last_used < image_variants_[oldest_variant].last_used) oldest_variant = index;
        }
        std::size_t oldest_backplane = 0U;
        for (std::size_t index = 1U; index < backplanes_.size(); ++index) {
            if (backplanes_[index].last_used < backplanes_[oldest_backplane].last_used) oldest_backplane = index;
        }
        const std::uint64_t never = std::numeric_limits<std::uint64_t>::max();
        const std::uint64_t bake_used = shared_bakes_.empty() ? never : shared_bakes_[oldest_bake].last_used;
        const std::uint64_t variant_used = image_variants_.empty() ? never : image_variants_[oldest_variant].last_used;
        const std::uint64_t backplane_used = backplanes_.empty() ? never : backplanes_[oldest_backplane].last_used;
        if (shared_bakes_.empty() && image_variants_.empty() && backplanes_.empty()) return;
        statistics_.shared_evictions = statistics_.shared_evictions + 1U;
        // Order does not matter: move the last entry into the hole. A part
        // using the evicted raster keeps it alive until it draws again.
        if (!shared_bakes_.empty() && bake_used <= variant_used && bake_used <= backplane_used) {
            statistics_.shared_bytes = statistics_.shared_bytes - (*shared_bakes_[oldest_bake].cached.raster).pixels().size();
            statistics_.shared_rasters = statistics_.shared_rasters - 1U;
            if (oldest_bake + 1U != shared_bakes_.size()) shared_bakes_[oldest_bake] = std::move(shared_bakes_.back());
            shared_bakes_.pop_back();
            continue;
        }
        if (!image_variants_.empty() && variant_used <= backplane_used) {
            forget_variant(image_variants_[oldest_variant]);
            if (oldest_variant + 1U != image_variants_.size()) {
                image_variants_[oldest_variant] = std::move(image_variants_.back());
            }
            image_variants_.pop_back();
            continue;
        }
        statistics_.shared_bytes = statistics_.shared_bytes - (*backplanes_[oldest_backplane].raster).pixels().size();
        statistics_.backplane_images = statistics_.backplane_images - 1U;
        if (oldest_backplane + 1U != backplanes_.size()) backplanes_[oldest_backplane] = std::move(backplanes_.back());
        backplanes_.pop_back();
    }
}

std::shared_ptr<const PartRaster> EnginePartTable::held_backplane(const BackplaneRegion& region,
                                                                  const std::uint64_t generation,
                                                                  const std::uint64_t paint_sequence) {
    if (generation != backplane_generation_) {
        clear_backplanes();
        backplane_generation_ = generation;
        return nullptr;
    }
    for (HeldBackplane& held : backplanes_) {
        if (!(held.region == region)) continue;
        held.last_used = paint_sequence;
        statistics_.backplane_hits = statistics_.backplane_hits + 1U;
        return held.raster;
    }
    return nullptr;
}

void EnginePartTable::hold_backplane(const BackplaneRegion& region, const std::uint64_t generation,
                                     const std::shared_ptr<const PartRaster>& image, const std::uint64_t skipped,
                                     const bool replayed, const std::uint64_t paint_sequence) {
    statistics_.backplane_bakes = statistics_.backplane_bakes + 1U;
    if (replayed) statistics_.backplane_replays = statistics_.backplane_replays + 1U;
    statistics_.backplane_skipped_commands = statistics_.backplane_skipped_commands + skipped;
    if (generation != backplane_generation_) {
        clear_backplanes();
        backplane_generation_ = generation;
    }
    const std::size_t bytes = (*image).pixels().size();
    if (bytes > shared_bake_budget_bytes) return;
    backplanes_.reserve(backplanes_.size() + 1U);
    // Commit: nothing below allocates.
    make_shared_room(bytes);
    backplanes_.push_back(HeldBackplane{region, image, paint_sequence});
    statistics_.backplane_images = statistics_.backplane_images + 1U;
    statistics_.shared_bytes = statistics_.shared_bytes + bytes;
}

void EnginePartTable::bake_backplane_material(const SurfaceMaterial& material, const Size window_size,
                                              const BackplaneRegion& region, const ImageRegistry* images,
                                              const PartRasterView& image, const std::uint64_t paint_sequence) {
    if (!valid_surface_material(material)) return;
    // The window's box as a material part's target; the material kernel
    // draws the region of it the image covers.
    PartRasterRequest request{};
    request.primitive = &backplane_material;
    request.target.logical_size = window_size;
    request.target.scale = region.scale;
    request.target.material = &material;
    request.target.images = images;
    LentImageVariants lent{};
    const std::optional<std::uint64_t> awaited = lend_image_variants(request, paint_sequence, lent);
    if (awaited) unrecorded_awaited_ = true;
    request.target.variants = &lent;
    engine_primitives::bake_material_region(material, request.target, region.origin, image);
    count_image_mask_fallback(request, lent);
}

std::optional<std::uint64_t> EnginePartTable::lend_image_variants(const PartRasterRequest& request,
                                                                  const std::uint64_t paint_sequence,
                                                                  LentImageVariants& lent) {
    std::optional<std::uint64_t> awaited{};
    std::array<ImageVariantRequest, maximum_image_variants> wanted{};
    const std::size_t asked_count = (*request.primitive).image_variants(request.parameters, request.target, wanted);
    const std::size_t count = std::min(asked_count, wanted.size());
    if (count == 0U) return awaited;
    const ImageRegistry* const images = request.target.images;
    if (images != nullptr) {
        const std::uint64_t revision = (*images).snapshot().revision;
        if (!image_variant_revision_ || *image_variant_revision_ != revision) {
            clear_image_variants();
            image_variant_revision_ = revision;
        }
    }
    for (std::size_t index = 0U; index < count; ++index) {
        const ImageVariantRequest& asked = wanted[index];
        if (asked.layer >= lent.layers.size()) continue;
        const bool pattern = asked.procedural.has_value();
        std::size_t found = image_variants_.size();
        for (std::size_t held = 0U; held < image_variants_.size(); ++held) {
            if (same_variant(image_variants_[held].request, asked)) found = held;
        }
        if (found != image_variants_.size()) {
            image_variants_[found].last_used = paint_sequence;
            if (pattern) {
                statistics_.procedural_hits = statistics_.procedural_hits + 1U;
            } else {
                statistics_.image_variant_hits = statistics_.image_variant_hits + 1U;
            }
            lent.layers[asked.layer] = image_variants_[found].raster;
            continue;
        }
        if (pattern) {
            // Never generated inside a paint (E10): queued for an idle
            // frame, and the fill draws its ground meanwhile.
            queue_pattern(asked);
            statistics_.procedural_placeholders = statistics_.procedural_placeholders + 1U;
            awaited = pattern_generation_;
            continue;
        }
        if (images == nullptr) continue;
        const std::shared_ptr<PartRaster> made = engine_primitives::make_image_variant(*images, asked);
        if (!made) continue;
        statistics_.image_variant_bakes = statistics_.image_variant_bakes + 1U;
        lent.layers[asked.layer] = made;
        hold_variant(asked, made, paint_sequence);
    }
    return awaited;
}

void EnginePartTable::count_image_mask_fallback(const PartRasterRequest& request,
                                                const LentImageVariants& lent) noexcept {
    if (request.primitive == nullptr || lent.layers[image_mask_variant_layer]) return;
    std::array<ImageVariantRequest, maximum_image_variants> wanted{};
    const std::size_t asked_count = (*request.primitive).image_variants(request.parameters, request.target, wanted);
    const std::size_t count = std::min(asked_count, wanted.size());
    for (std::size_t index = 0U; index < count; ++index) {
        if (wanted[index].layer != image_mask_variant_layer) continue;
        statistics_.image_mask_fallbacks = statistics_.image_mask_fallbacks + 1U;
        return;
    }
}

void EnginePartTable::hold_variant(const ImageVariantRequest& asked, const std::shared_ptr<PartRaster>& made,
                                   const std::uint64_t paint_sequence) {
    // A variant larger than the whole budget is not held.
    const std::size_t bytes = (*made).pixels().size();
    if (bytes > shared_bake_budget_bytes) return;
    image_variants_.reserve(image_variants_.size() + 1U);
    // Commit: nothing below allocates.
    make_shared_room(bytes);
    image_variants_.push_back(HeldImageVariant{asked, made, paint_sequence});
    if (asked.procedural) {
        statistics_.procedural_patterns = statistics_.procedural_patterns + 1U;
    } else {
        statistics_.image_variants = statistics_.image_variants + 1U;
    }
    statistics_.shared_bytes = statistics_.shared_bytes + bytes;
}

void EnginePartTable::queue_pattern(const ImageVariantRequest& asked) {
    if (pattern_queue_.size() >= maximum_queued_patterns) return;
    for (const ImageVariantRequest& queued : pattern_queue_) {
        if (same_variant(queued, asked)) return;
    }
    pattern_queue_.push_back(asked);
    statistics_.procedural_queued = pattern_queue_.size();
}

PatternGeneration EnginePartTable::generate_pattern(const std::uint64_t paint_sequence) {
    while (!pattern_queue_.empty()) {
        const ImageVariantRequest asked = pattern_queue_.front();
        pattern_queue_.pop_front();
        statistics_.procedural_queued = pattern_queue_.size();
        bool held = false;
        for (const HeldImageVariant& variant : image_variants_) {
            if (same_variant(variant.request, asked)) held = true;
        }
        if (held) continue;
        const std::shared_ptr<PartRaster> made =
            engine_primitives::make_procedural_pattern(*asked.procedural, asked.scale.value, asked.extent);
        if (!made) continue;
        hold_variant(asked, made, paint_sequence);
        statistics_.procedural_generations = statistics_.procedural_generations + 1U;
        pattern_generation_ = pattern_generation_ + 1U;
        // The shared bakes that drew a ground are no longer current.
        std::size_t index = 0U;
        while (index < shared_bakes_.size()) {
            if (!shared_bakes_[index].cached.awaited_patterns) {
                index = index + 1U;
                continue;
            }
            statistics_.shared_bytes = statistics_.shared_bytes - (*shared_bakes_[index].cached.raster).pixels().size();
            statistics_.shared_rasters = statistics_.shared_rasters - 1U;
            if (index + 1U != shared_bakes_.size()) shared_bakes_[index] = std::move(shared_bakes_.back());
            shared_bakes_.pop_back();
        }
        if (!unrecorded_awaited_) return PatternGeneration::made;
        // A backplane image may have drawn the ground: each is made again.
        clear_backplanes();
        unrecorded_awaited_ = false;
        return PatternGeneration::made_unrecorded;
    }
    return PatternGeneration::none;
}

bool EnginePartTable::patterns_waiting() const noexcept {
    const bool waiting = !pattern_queue_.empty();
    return waiting;
}

bool EnginePartTable::awaits_pattern(const CachedPartRaster& cached) const noexcept {
    if (!cached.awaited_patterns) return false;
    const bool stale = *cached.awaited_patterns != pattern_generation_;
    return stale;
}

std::shared_ptr<PartRaster> EnginePartTable::shared_bake(const PartRasterRequest& request,
                                                         const std::uint64_t paint_sequence, const bool refresh,
                                                         const SharedUse use, std::optional<std::uint64_t>& awaited) {
    awaited.reset();
    std::size_t found = shared_bakes_.size();
    for (std::size_t index = 0U; index < shared_bakes_.size(); ++index) {
        if (answers(shared_bakes_[index].cached, request)) found = index;
    }
    const bool keyframe = use == SharedUse::keyframe;
    if (found != shared_bakes_.size() && !refresh) {
        shared_bakes_[found].last_used = paint_sequence;
        if (keyframe) {
            statistics_.keyframe_hits = statistics_.keyframe_hits + 1U;
        } else {
            statistics_.slice_hits = statistics_.slice_hits + 1U;
        }
        const std::shared_ptr<PartRaster> held = shared_bakes_[found].cached.raster;
        awaited = shared_bakes_[found].cached.awaited_patterns;
        return held;
    }
    // Bake. Everything that can fail comes first.
    const EnginePrimitive& primitive = *request.primitive;
    std::shared_ptr<PartRaster> baked = PartRaster::create(request.extent, request.target.scale);
    if (!baked) return nullptr;
    // The image variants, lent for the bake.
    LentImageVariants lent{};
    const std::optional<std::uint64_t> drew_ground = lend_image_variants(request, paint_sequence, lent);
    PrimitiveTarget target = request.target;
    target.variants = &lent;
    PrimitivePreparation preparation{};
    if (primitive.prepare(request.parameters, target, preparation) != PreparationOutcome::prepared) {
        return nullptr;
    }
    const RasterOutcome outcome = primitive.rasterize(request.parameters, target, preparation, (*baked).view());
    if (outcome != RasterOutcome::painted && outcome != RasterOutcome::empty) return nullptr;
    (*baked).record_opacity();
    statistics_.bakes = statistics_.bakes + 1U;
    count_image_mask_fallback(request, lent);
    if (use == SharedUse::idle_keyframe) {
        statistics_.keyframe_idle_bakes = statistics_.keyframe_idle_bakes + 1U;
    } else if (keyframe) {
        statistics_.keyframe_demand_bakes = statistics_.keyframe_demand_bakes + 1U;
    } else {
        statistics_.slice_bakes = statistics_.slice_bakes + 1U;
    }
    std::optional<SurfaceMaterial> material{};
    std::uint64_t image_revision = 0U;
    if (request.target.material != nullptr) {
        material = *request.target.material;
        if (request.target.images != nullptr) image_revision = (*request.target.images).snapshot().revision;
    }
    const std::size_t bytes = (*baked).pixels().size();
    if (found != shared_bakes_.size()) {
        // Baked again in place of the stale one.
        SharedBake& replaced = shared_bakes_[found];
        statistics_.shared_bytes = statistics_.shared_bytes - (*replaced.cached.raster).pixels().size() + bytes;
        replaced.cached.raster = baked;
        keep_request(replaced.cached, request, std::move(material), image_revision);
        replaced.cached.awaited_patterns = drew_ground;
        replaced.last_used = paint_sequence;
        awaited = drew_ground;
        return baked;
    }
    awaited = drew_ground;
    if (bytes > shared_bake_budget_bytes) return baked;
    SharedBake made{};
    made.cached.raster = baked;
    keep_request(made.cached, request, std::move(material), image_revision);
    made.cached.awaited_patterns = drew_ground;
    made.last_used = paint_sequence;
    shared_bakes_.reserve(shared_bakes_.size() + 1U);
    // Commit: nothing below allocates.
    make_shared_room(bytes);
    shared_bakes_.push_back(std::move(made));
    statistics_.shared_rasters = statistics_.shared_rasters + 1U;
    statistics_.shared_bytes = statistics_.shared_bytes + bytes;
    return baked;
}

void EnginePartTable::queue_idle_keyframe(const PartRasterRequest& request) {
    if (request.primitive == nullptr || idle_keyframes_.size() >= maximum_idle_keyframes) return;
    for (const SharedBake& held : shared_bakes_) {
        if (answers(held.cached, request)) return;
    }
    for (const CachedPartRaster& queued : idle_keyframes_) {
        if (describes(queued, request)) return;
    }
    std::optional<SurfaceMaterial> material{};
    std::uint64_t image_revision = 0U;
    if (request.target.material != nullptr) {
        material = *request.target.material;
        if (request.target.images != nullptr) image_revision = (*request.target.images).snapshot().revision;
    }
    CachedPartRaster queued{};
    queued.shared = true;
    keep_request(queued, request, std::move(material), image_revision);
    idle_keyframes_.push_back(std::move(queued));
    statistics_.idle_keyframes_queued = idle_keyframes_.size();
}

bool EnginePartTable::bake_idle_keyframe(const ImageRegistry* images, const std::uint64_t paint_sequence) {
    while (!idle_keyframes_.empty()) {
        const CachedPartRaster queued = std::move(idle_keyframes_.front());
        idle_keyframes_.pop_front();
        statistics_.idle_keyframes_queued = idle_keyframes_.size();
        // The request again, its material and the window's images lent.
        PartRasterRequest request{};
        request.primitive = queued.primitive;
        request.parameters = queued.parameters;
        request.target = queued.target;
        request.extent = queued.extent;
        request.shared = true;
        if (queued.material) {
            if (images == nullptr || (*images).snapshot().revision != queued.image_revision) continue;
            request.target.material = &*queued.material;
            request.target.images = images;
        }
        bool held = false;
        for (const SharedBake& bake : shared_bakes_) {
            if (answers(bake.cached, request)) held = true;
        }
        if (held) continue;
        std::optional<std::uint64_t> awaited{};
        static_cast<void>(shared_bake(request, paint_sequence, false, SharedUse::idle_keyframe, awaited));
        break;
    }
    const bool waiting = !idle_keyframes_.empty();
    return waiting;
}

bool EnginePartTable::idle_keyframes_waiting() const noexcept {
    const bool waiting = !idle_keyframes_.empty();
    return waiting;
}

RasterOutcome EnginePartTable::draw(const PartRasterRequest& request, PrimitivePreparation& preparation,
                                    const PartRasterView& destination, const std::uint64_t paint_sequence,
                                    const bool refresh, std::optional<std::uint64_t>& awaited) {
    awaited.reset();
    const EnginePrimitive& primitive = *request.primitive;
    const PrimitiveSlicePlan plan = primitive.slice_plan(request.parameters, request.target, request.extent);
    const bool across = plan.across.outcome == SliceAxisOutcome::sliced;
    const bool down = plan.down.outcome == SliceAxisOutcome::sliced;
    if (plan.slices) {
        const std::uint64_t kept_whole = static_cast<std::uint64_t>(!across) + static_cast<std::uint64_t>(!down);
        statistics_.slice_axis_fallbacks = statistics_.slice_axis_fallbacks + kept_whole;
        if (!across && !down) statistics_.slice_whole_fallbacks = statistics_.slice_whole_fallbacks + 1U;
    }
    if (plan.slices && (across || down)) {
        PartRasterRequest reference = request;
        reference.target.logical_size = plan.reference_size;
        reference.extent = plan.reference_extent;
        const std::shared_ptr<const PartRaster> held =
            shared_bake(reference, paint_sequence, refresh, SharedUse::slice_reference, awaited);
        if (!held) return RasterOutcome::invalid_target;
        if (destination.extent.width == 0U || destination.extent.height == 0U) return RasterOutcome::empty;
        detail::assemble_slices(*held, plan, destination);
        return RasterOutcome::painted;
    }
    // The image variants, lent for the bake.
    LentImageVariants lent{};
    awaited = lend_image_variants(request, paint_sequence, lent);
    PrimitiveTarget target = request.target;
    target.variants = &lent;
    RasterOutcome outcome = primitive.rasterize(request.parameters, target, preparation, destination);
    if (outcome == RasterOutcome::stale_preparation) {
        PrimitivePreparation prepared{};
        if (primitive.prepare(request.parameters, target, prepared) != PreparationOutcome::prepared) {
            return RasterOutcome::invalid_target;
        }
        preparation = prepared;
        outcome = primitive.rasterize(request.parameters, target, preparation, destination);
    }
    if (outcome == RasterOutcome::painted || outcome == RasterOutcome::empty) {
        statistics_.bakes = statistics_.bakes + 1U;
        count_image_mask_fallback(request, lent);
    }
    return outcome;
}

PartRasterResult EnginePartTable::raster_without_record(const PartRasterRequest& request,
                                                        const std::uint64_t paint_sequence) {
    if (request.primitive == nullptr) return PartRasterResult{nullptr, PartRasterStatus::refused};
    std::shared_ptr<PartRaster> storage = PartRaster::create(request.extent, request.target.scale);
    if (!storage) return PartRasterResult{nullptr, PartRasterStatus::refused};
    PrimitivePreparation preparation{};
    std::optional<std::uint64_t> awaited{};
    const RasterOutcome outcome = draw(request, preparation, (*storage).view(), paint_sequence, false, awaited);
    if (outcome != RasterOutcome::painted && outcome != RasterOutcome::empty) {
        return PartRasterResult{nullptr, PartRasterStatus::refused};
    }
    (*storage).record_opacity();
    if (awaited) unrecorded_awaited_ = true;
    statistics_.uncached = statistics_.uncached + 1U;
    return PartRasterResult{storage, PartRasterStatus::uncached};
}

PartRasterResult EnginePartTable::raster_for(const PartRecordHandle handle, const PartRasterRequest& request,
                                             const std::uint64_t paint_sequence) {
    EnginePartRecord* const found = record(handle);
    if (found == nullptr) return PartRasterResult{nullptr, PartRasterStatus::stale_record};
    if (request.primitive == nullptr) return PartRasterResult{nullptr, PartRasterStatus::refused};
    EnginePartRecord& part = *found;
    part.last_painted = paint_sequence;
    if (part.cached && (*part.cached).listed) list_raster_newest(*part.cached);
    if (raster_current(part, request)) {
        statistics_.hits = statistics_.hits + 1U;
        return PartRasterResult{(*part.cached).raster, PartRasterStatus::cached};
    }
    // The cache record is made once per part, before anything is drawn, so
    // that an allocation failure leaves the record as it was.
    if (!part.cached) part.cached = std::make_unique<CachedPartRaster>();
    CachedPartRaster& cached = *part.cached;
    // A part invalidated on its own since its raster was drawn bakes again
    // the shared raster it uses.
    const bool refresh = cached.raster && cached.raster_generation != part.generation;

    if (request.shared) {
        // A keyframe: the shared raster becomes the part's own, uncopied.
        std::optional<SurfaceMaterial> shared_material{};
        std::uint64_t shared_revision = 0U;
        if (request.target.material != nullptr) {
            shared_material = *request.target.material;
            if (request.target.images != nullptr) shared_revision = (*request.target.images).snapshot().revision;
        }
        std::optional<std::uint64_t> awaited{};
        const std::shared_ptr<PartRaster> held =
            shared_bake(request, paint_sequence, refresh, SharedUse::keyframe, awaited);
        if (!held) return PartRasterResult{nullptr, PartRasterStatus::refused};
        drop_raster(part);
        cached.raster = held;
        cached.shared = true;
        keep_request(cached, request, std::move(shared_material), shared_revision);
        cached.awaited_patterns = awaited;
        cached.raster_generation = part.generation;
        cached.raster_table_generation = raster_table_generation_;
        return PartRasterResult{held, PartRasterStatus::shared};
    }

    // Choose the storage. The cached raster is rewritten only when the cache
    // holds its one reference: a recorded command still showing it keeps
    // its pixels.
    const std::size_t bytes = raster_bytes(request.extent);
    const bool reusable = cached.raster && !cached.shared && cached.extent == request.extent &&
                          (*cached.raster).scale() == request.target.scale && cached.raster.use_count() == 1;
    std::shared_ptr<PartRaster> storage{};
    PartRasterStatus status = PartRasterStatus::reused_storage;
    if (reusable) {
        storage = cached.raster;
    } else {
        storage = PartRaster::create(request.extent, request.target.scale);
        if (!storage) return PartRasterResult{nullptr, PartRasterStatus::refused};
        status = bytes > raster_cache_budget_bytes ? PartRasterStatus::uncached : PartRasterStatus::new_storage;
    }

    // The lent material's copy, made before anything is drawn or committed.
    std::optional<SurfaceMaterial> kept_material{};
    std::uint64_t image_revision = 0U;
    if (request.target.material != nullptr) {
        kept_material = *request.target.material;
        if (request.target.images != nullptr) image_revision = (*request.target.images).snapshot().revision;
    }

    // Draw, preparing again when the record's preparation is stale.
    const PartRasterView destination = (*storage).view();
    std::optional<std::uint64_t> awaited{};
    const RasterOutcome outcome = draw(request, part.preparation, destination, paint_sequence, refresh, awaited);
    if (outcome != RasterOutcome::painted && outcome != RasterOutcome::empty) {
        return PartRasterResult{nullptr, PartRasterStatus::refused};
    }
    (*storage).record_opacity();

    // Commit. The part's former raster no longer shows it either way.
    if (status == PartRasterStatus::uncached) {
        drop_raster(part);
        statistics_.uncached = statistics_.uncached + 1U;
        return PartRasterResult{storage, status};
    }
    if (status == PartRasterStatus::new_storage) {
        drop_raster(part);
        make_room(bytes, &part);
        cached.raster = storage;
        cached.shared = false;
        statistics_.bytes = statistics_.bytes + bytes;
        statistics_.rasters = statistics_.rasters + 1U;
        statistics_.storage_allocations = statistics_.storage_allocations + 1U;
        list_raster_newest(cached);
    } else {
        statistics_.storage_reuses = statistics_.storage_reuses + 1U;
    }
    keep_request(cached, request, std::move(kept_material), image_revision);
    cached.awaited_patterns = awaited;
    cached.raster_generation = part.generation;
    cached.raster_table_generation = raster_table_generation_;
    return PartRasterResult{storage, status};
}

void EnginePartTable::count_readers(const std::uint64_t old_inputs, const std::uint64_t inputs) noexcept {
    if (old_inputs == inputs) return;
    for (std::size_t position = 0; position < maximum_expression_inputs; ++position) {
        const std::uint64_t bit = std::uint64_t{1} << position;
        if ((old_inputs & bit) != 0U) readers_[position] = readers_[position] - 1U;
        if ((inputs & bit) != 0U) readers_[position] = readers_[position] + 1U;
    }
}

void EnginePartTable::note_backplane_reader(const PartRecordHandle handle, const bool reads) noexcept {
    EnginePartRecord* const found = record(handle);
    if (found == nullptr) return;
    (*found).reads_backplane = reads;
}

void EnginePartTable::note_paint(const PartRecordHandle handle, const std::uint64_t inputs_read,
                                 std::string_view painted_by, const Rect extent) {
    EnginePartRecord* const found = record(handle);
    if (found == nullptr) return;
    EnginePartRecord& part = *found;
    // The copy first: it may allocate, and nothing has changed yet.
    if (part.painted_by != painted_by) part.painted_by.assign(painted_by.data(), painted_by.size());
    part.extent = extent;
    count_readers(part.inputs_read, inputs_read);
    part.inputs_read = inputs_read;
}

bool EnginePartTable::reads_any(const std::uint64_t inputs) const noexcept {
    for (std::size_t position = 0; position < maximum_expression_inputs; ++position) {
        const std::uint64_t bit = std::uint64_t{1} << position;
        if ((inputs & bit) != 0U && readers_[position] != 0U) return true;
    }
    return false;
}

const EnginePartRecord* EnginePartTable::record_in_slot(const std::size_t slot) const noexcept {
    if (slot >= slots_.size() || !slots_[slot].record) return nullptr;
    const EnginePartRecord* const live = &*slots_[slot].record;
    return live;
}

void EnginePartTable::invalidate_raster(const PartRecordHandle handle) noexcept {
    EnginePartRecord* const found = record(handle);
    if (found == nullptr) return;
    (*found).generation = (*found).generation + 1U;
}

void EnginePartTable::invalidate_rasters() noexcept {
    raster_table_generation_ = raster_table_generation_ + 1U;
    clear_shared_bakes();
    idle_keyframes_.clear();
    statistics_.idle_keyframes_queued = 0U;
    // The new theme's paints queue the patterns they need.
    pattern_queue_.clear();
    statistics_.procedural_queued = 0U;
}

} // namespace gui_forms
