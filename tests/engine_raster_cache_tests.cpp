// The theme engine's per-part framebuffers (T2 plan, phase E2c; GIVEN F2-1):
// a part's raster is kept while what drew it is unchanged, its storage is
// reused when the part is drawn again at the same size, the cache stays
// within 8 MB per window and evicts the least recently painted part first,
// and rasters are invalidated by generation.

#include "gui_forms/engine.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

const EnginePrimitive& builtin(const char* name) {
    const EnginePrimitive* const primitive = builtin_primitive_registry().find(name);
    require(primitive != nullptr, "the built-in primitive is registered");
    return *primitive;
}

PartRasterRequest request_for(const EnginePrimitive& primitive, Size logical, double scale, double amplitude) {
    PartRasterRequest request{};
    request.primitive = &primitive;
    request.parameters = default_parameters(primitive.descriptor());
    require(set_number_parameter(request.parameters, primitive.descriptor(), "amplitude", amplitude) ==
                ParameterAssignment::assigned,
            "the amplitude is assigned");
    request.target = PrimitiveTarget{logical, DeviceScale{scale}, {}};
    request.extent = DeviceExtent{static_cast<std::uint32_t>(logical.width * scale),
                                  static_cast<std::uint32_t>(logical.height * scale)};
    return request;
}

// Whether the record holds a cached raster.
bool holds_raster(const EnginePartTable& table, PartRecordHandle handle) {
    const EnginePartRecord* const record = table.record(handle);
    if (record == nullptr || !(*record).cached) return false;
    const bool held = (*(*record).cached).raster != nullptr;
    return held;
}

PartRecordHandle record_of(EnginePartTable& table, const char* control, const char* part) {
    const PartRecordAcquisition acquired = table.acquire(control, PartKey{part, {}, {}});
    return acquired.handle;
}

constexpr Size button{40.0, 28.0};

void test_reuse_across_unchanged_frames() {
    EnginePartTable table{};
    const EnginePrimitive& dithered = builtin("dithered_bulge");
    const PartRecordHandle face = record_of(table, "button", "face");

    PartRasterResult first = table.raster_for(face, request_for(dithered, button, 1.0, 1.0), 1U);
    require(first.status == PartRasterStatus::new_storage && first.raster != nullptr, "the first paint draws");
    const PartRaster* const drawn = first.raster.get();
    const std::vector<std::uint8_t> pixels((*first.raster).pixels().begin(), (*first.raster).pixels().end());
    for (std::uint64_t frame = 2U; frame <= 4U; ++frame) {
        const PartRasterResult again = table.raster_for(face, request_for(dithered, button, 1.0, 1.0), frame);
        require(again.status == PartRasterStatus::cached && again.raster.get() == drawn,
                "an unchanged part is answered from the cache");
    }
    RasterCacheStatistics statistics = table.cache_statistics();
    require(statistics.hits == 3U && statistics.storage_allocations == 1U && statistics.rasters == 1U &&
                statistics.bytes == 40U * 28U * 4U,
            "three hits, one allocation, one raster held");
    require((*table.record(face)).last_painted == 4U, "the last paint is recorded");

    // A state the primitive does not read keeps the raster current; one it
    // reads does not.
    PartRasterRequest hot = request_for(dithered, button, 1.0, 1.0);
    hot.target.states.insert(AnatomyState::hot);
    require(table.raster_for(face, hot, 5U).status == PartRasterStatus::cached, "an unread state keeps the raster");

    // A changed amplitude redraws into the same storage when nothing else
    // holds the raster.
    first.raster.reset();
    const PartRasterResult risen = table.raster_for(face, request_for(dithered, button, 1.0, 0.5), 6U);
    require(risen.status == PartRasterStatus::reused_storage && risen.raster.get() == drawn,
            "a changed part is redrawn into its own storage");
    const std::vector<std::uint8_t> half((*risen.raster).pixels().begin(), (*risen.raster).pixels().end());
    require(half != pixels, "the redrawn raster shows the new amplitude");

    // While a recorded command holds the raster, it is never rewritten:
    // the change goes to new storage and the held pixels stay.
    const std::shared_ptr<const PartRaster> held = risen.raster;
    PartRasterRequest pressed = request_for(dithered, button, 1.0, 0.5);
    pressed.target.states.insert(AnatomyState::pressed);
    const PartRasterResult dented = table.raster_for(face, pressed, 7U);
    require(dented.status == PartRasterStatus::new_storage && dented.raster.get() != held.get(),
            "a held raster is not rewritten");
    const std::vector<std::uint8_t> still((*held).pixels().begin(), (*held).pixels().end());
    require(still == half, "the held raster keeps its pixels");
    statistics = table.cache_statistics();
    require(statistics.rasters == 1U && statistics.bytes == 40U * 28U * 4U,
            "the cache holds only the part's current raster");

    // Passthrough transparency: the dither's gaps are transparent pixels
    // in the raster itself, which the composite leaves as the plane.
    bool transparent_gap = false;
    for (std::size_t offset = 3U; offset < (*dented.raster).pixels().size(); offset += 4U) {
        if ((*dented.raster).pixels()[offset] == 0U) transparent_gap = true;
    }
    require(transparent_gap, "the raster's own alpha carries its transparency");
}

void test_invalidation_by_generation() {
    EnginePartTable table{};
    const EnginePrimitive& bulge = builtin("bulge");
    const PartRecordHandle face = record_of(table, "button", "face");
    const PartRecordHandle other = record_of(table, "button", "other");
    require(table.raster_for(face, request_for(bulge, button, 1.5, 1.0), 1U).status == PartRasterStatus::new_storage,
            "the face draws");
    require(table.raster_for(other, request_for(bulge, button, 1.5, 1.0), 1U).status == PartRasterStatus::new_storage,
            "the other part draws");

    table.invalidate_raster(face);
    require(table.raster_for(face, request_for(bulge, button, 1.5, 1.0), 2U).status ==
                PartRasterStatus::reused_storage,
            "an invalidated part is redrawn into its storage");
    require(table.raster_for(other, request_for(bulge, button, 1.5, 1.0), 2U).status == PartRasterStatus::cached,
            "invalidating one part leaves the others current");

    table.invalidate_rasters();
    require(table.raster_for(face, request_for(bulge, button, 1.5, 1.0), 3U).status ==
                PartRasterStatus::reused_storage &&
                table.raster_for(other, request_for(bulge, button, 1.5, 1.0), 3U).status ==
                    PartRasterStatus::reused_storage,
            "invalidating the table redraws every part");

    // A new scale is another raster; a stale handle draws nothing.
    require(table.raster_for(face, request_for(bulge, button, 2.0, 1.0), 4U).status == PartRasterStatus::new_storage,
            "a new scale draws new storage");
    const std::size_t released = table.release_control("button");
    require(released == 2U, "the control's records are released");
    const RasterCacheStatistics statistics = table.cache_statistics();
    require(statistics.rasters == 0U && statistics.bytes == 0U, "released records release their rasters");
    require(table.raster_for(face, request_for(bulge, button, 2.0, 1.0), 5U).status == PartRasterStatus::stale_record,
            "a stale handle draws nothing");
}

void test_refusal_keeps_the_cache() {
    EnginePartTable table{};
    const EnginePrimitive& dithered = builtin("dithered_bulge");
    const PartRecordHandle face = record_of(table, "button", "face");
    const PartRasterResult drawn = table.raster_for(face, request_for(dithered, button, 1.0, 1.0), 1U);
    require(drawn.status == PartRasterStatus::new_storage, "the face draws");
    PartRasterRequest forged = request_for(dithered, button, 1.0, 1.0);
    forged.parameters.values[0].number = 2.0;
    require(table.raster_for(face, forged, 2U).status == PartRasterStatus::refused, "invalid parameters are refused");
    PartRasterRequest nothing = request_for(dithered, button, 1.0, 1.0);
    nothing.primitive = nullptr;
    require(table.raster_for(face, nothing, 3U).status == PartRasterStatus::refused, "no primitive is refused");
    const PartRasterResult kept = table.raster_for(face, request_for(dithered, button, 1.0, 1.0), 4U);
    require(kept.status == PartRasterStatus::cached && kept.raster == drawn.raster,
            "a refusal leaves the cached raster current");
}

// Rasters of 1024 x 512 device px are 2 MiB each: exactly four fit the 8 MiB
// budget.
constexpr Size wide{1024.0, 512.0};

void test_eviction_order_and_the_cap() {
    EnginePartTable table{};
    const EnginePrimitive& dithered = builtin("dithered_bulge");
    const char* const names[] = {"a", "b", "c", "d", "e"};
    std::vector<PartRecordHandle> handles{};
    for (const char* const name : names) handles.push_back(record_of(table, "panel", name));

    for (std::size_t index = 0U; index < 4U; ++index) {
        const PartRasterResult drawn =
            table.raster_for(handles[index], request_for(dithered, wide, 1.0, 1.0), index + 1U);
        require(drawn.status == PartRasterStatus::new_storage, "each part draws");
    }
    RasterCacheStatistics statistics = table.cache_statistics();
    require(statistics.rasters == 4U && statistics.bytes == raster_cache_budget_bytes && statistics.evictions == 0U,
            "four 2 MiB rasters fill the budget exactly");

    // Painting "a" again makes "b" the least recently painted.
    require(table.raster_for(handles[0], request_for(dithered, wide, 1.0, 1.0), 5U).status == PartRasterStatus::cached,
            "a is answered from the cache");
    require(table.raster_for(handles[4], request_for(dithered, wide, 1.0, 1.0), 6U).status ==
                PartRasterStatus::new_storage,
            "e draws");
    statistics = table.cache_statistics();
    require(statistics.evictions == 1U && statistics.rasters == 4U && statistics.bytes <= raster_cache_budget_bytes,
            "one raster is evicted to stay within the budget");
    require(!holds_raster(table, handles[1]), "the least recently painted part (b) is evicted");
    require(holds_raster(table, handles[0]) && holds_raster(table, handles[2]) && holds_raster(table, handles[3]) &&
                holds_raster(table, handles[4]),
            "the more recently painted parts keep their rasters");

    // An evicted part draws again when it is next painted, evicting the
    // next least recently painted (c).
    require(table.raster_for(handles[1], request_for(dithered, wide, 1.0, 1.0), 7U).status ==
                PartRasterStatus::new_storage,
            "an evicted part draws again");
    require(!holds_raster(table, handles[2]), "the next least recently painted part (c) is evicted");

    // A raster larger than the whole budget is drawn and returned, never
    // kept, and evicts nothing.
    const PartRecordHandle huge = record_of(table, "panel", "huge");
    const std::uint64_t evictions_before = table.cache_statistics().evictions;
    const PartRasterResult oversized = table.raster_for(huge, request_for(dithered, Size{2048.0, 1100.0}, 1.0, 1.0), 8U);
    require(oversized.status == PartRasterStatus::uncached && oversized.raster != nullptr,
            "a raster over the budget is drawn but not kept");
    statistics = table.cache_statistics();
    require(statistics.uncached == 1U && statistics.evictions == evictions_before &&
                statistics.bytes <= raster_cache_budget_bytes && !holds_raster(table, huge),
            "the cap holds: nothing is evicted for a raster that cannot fit");
}

// Evicting an item record (E2a's cap) releases its raster from the cache
// too, so the raster budget and the record cap hold together.
void test_evicted_item_records_release_their_rasters() {
    EnginePartTable table(2U);
    const EnginePrimitive& dithered = builtin("dithered_bulge");
    const PartRecordHandle first = table.acquire("list", PartKey{"row", {}, stxlib::Optional<std::size_t>::of(0U)}).handle;
    const PartRecordHandle second = table.acquire("list", PartKey{"row", {}, stxlib::Optional<std::size_t>::of(1U)}).handle;
    require(table.raster_for(first, request_for(dithered, button, 1.0, 1.0), 1U).status ==
                    PartRasterStatus::new_storage &&
                table.raster_for(second, request_for(dithered, button, 1.0, 1.0), 2U).status ==
                    PartRasterStatus::new_storage,
            "both rows draw");
    require(table.cache_statistics().rasters == 2U, "two rasters are cached");
    const PartRecordAcquisition third = table.acquire("list", PartKey{"row", {}, stxlib::Optional<std::size_t>::of(2U)});
    require(third.evicted == 1U && table.record(first) == nullptr, "the oldest row's record is evicted");
    const RasterCacheStatistics statistics = table.cache_statistics();
    require(statistics.rasters == 1U && statistics.bytes == 40U * 28U * 4U,
            "the evicted record's raster leaves the cache");
}

} // namespace

int main() {
    test_reuse_across_unchanged_frames();
    test_invalidation_by_generation();
    test_refusal_keeps_the_cache();
    test_eviction_order_and_the_cap();
    test_evicted_item_records_release_their_rasters();
    std::cout << "engine raster cache tests passed\n";
    return 0;
}
