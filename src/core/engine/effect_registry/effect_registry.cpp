#include "gui_forms/engine/effect_registry/effect_registry.hpp"

#include "../../text/localizable/gui_forms_notes.hpp"
#include "../primitives/kernel/box_blur.hpp"
#include "../../surface_root/access/surface_engine_access.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <optional>
#include <span>
#include <vector>

namespace gui_forms {

namespace {

bool snake_case(std::string_view name) noexcept {
    if (name.empty() || name.front() < 'a' || name.front() > 'z' || name.back() == '_') return false;
    char previous = '\0';
    for (const char character : name) {
        const bool lower = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        if (!lower && !digit && character != '_') return false;
        if (character == '_' && previous == '_') return false;
        previous = character;
    }
    return true;
}

bool outset_in_range(double outset) noexcept {
    return std::isfinite(outset) && outset >= 0.0 && outset <= maximum_effect_outset;
}

bool backdrop_margin_in_range(double margin) noexcept {
    return std::isfinite(margin) && margin >= 0.0 && margin <= maximum_effect_backdrop_margin;
}

// Device pixels covering `logical` units at `scale`, or none past the
// largest raster side.
std::optional<std::uint32_t> device_side(double logical, double scale) noexcept {
    const double device = std::ceil(std::max(0.0, logical) * scale);
    if (!(device <= static_cast<double>(maximum_part_raster_side))) return std::nullopt;
    const std::uint32_t side = static_cast<std::uint32_t>(device);
    return side;
}

// Reads little-endian integers and strings from one verification record.
class RecordReader final {
public:
    explicit RecordReader(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] std::optional<std::uint32_t> u32() noexcept {
        if (bytes_.size() - offset_ < 4U) return std::nullopt;
        std::uint32_t value = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            const std::uint32_t byte = std::to_integer<std::uint32_t>(bytes_[offset_ + index]);
            value = value | (byte << (8U * index));
        }
        offset_ = offset_ + 4U;
        return value;
    }

    [[nodiscard]] std::optional<std::string_view> text() noexcept {
        const std::optional<std::uint32_t> size = u32();
        if (!size || bytes_.size() - offset_ < *size) return std::nullopt;
        const std::string_view value(reinterpret_cast<const char*>(bytes_.data() + offset_), *size);
        offset_ = offset_ + *size;
        return value;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

// Whether one record table names the effect `name` with state `state_size`.
bool table_names(std::span<const std::byte> table, std::string_view name, std::size_t state_size) noexcept {
    RecordReader reader(table);
    const std::optional<std::uint32_t> count = reader.u32();
    if (!count) return false;
    for (std::uint32_t index = 0U; index < *count; ++index) {
        const std::optional<std::string_view> recorded = reader.text();
        const std::optional<std::uint32_t> bound = reader.u32();
        const std::optional<std::uint32_t> setup_bound = reader.u32();
        const std::optional<std::uint32_t> state_size = reader.u32();
        if (!recorded || !bound || !setup_bound || !state_size) return false;
        if (*recorded == name && *state_size == state_size) return true;
    }
    return false;
}

// FNV-1a over `bytes`.
std::uint64_t fnv1a(std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const std::byte byte : bytes) {
        hash = (hash ^ std::to_integer<std::uint64_t>(byte)) * 0x100000001b3ULL;
    }
    return hash;
}

// The process's registration counts (effect_load_diagnostics).
std::atomic<std::uint64_t> registered_effects{0U};
std::atomic<std::uint64_t> unverified_effects{0U};
std::atomic<std::uint64_t> refused_effects{0U};

// The most bytes of an effect's name the load diagnostic prints; a longer
// name is cut there, so the printed length always fits printf's int.
constexpr std::size_t maximum_reported_name_bytes = 256U;

// Counts one registration and, for the process's first unverified one,
// writes the load diagnostic.
void note_registration(std::string_view name, bool verified) noexcept {
    registered_effects.fetch_add(1U, std::memory_order_relaxed);
    if (verified) return;
    const std::uint64_t before = unverified_effects.fetch_add(1U, std::memory_order_relaxed);
    if (before != 0U) return;
    const std::size_t reported_bytes =
        name.size() < maximum_reported_name_bytes ? name.size() : maximum_reported_name_bytes;
    std::fprintf(stderr,
                 "GUI.Forms: effect program %.*s is registered unverified: no record of the gui_forms Clang "
                 "plugin's effect pass is in its binary, so its bounds were not checked (further unverified "
                 "effects are counted, not reported)\n",
                 static_cast<int>(reported_bytes), name.data());
}

} // namespace

EffectLoadDiagnostics effect_load_diagnostics() noexcept {
    EffectLoadDiagnostics result{};
    result.registered = registered_effects.load(std::memory_order_relaxed);
    result.unverified = unverified_effects.load(std::memory_order_relaxed);
    result.refused = refused_effects.load(std::memory_order_relaxed);
    return result;
}

bool effect_name_valid(std::string_view name) noexcept {
    if (name.size() > maximum_effect_name_length || name == effect_none_name) return false;
    const bool valid = snake_case(name);
    return valid;
}

static_assert(maximum_effect_operations_per_frame == maximum_animated_operations_per_frame,
              "an effect's frame is bounded as an animated primitive's part is");

std::string_view effect_selection_source_name(EffectSelectionSource source) noexcept {
    switch (source) {
    case EffectSelectionSource::none: return "none";
    case EffectSelectionSource::application: return "application";
    case EffectSelectionSource::style: return "style";
    case EffectSelectionSource::theme: return "theme";
    }
    return "none";
}

std::string_view effect_selection_outcome_name(EffectSelectionOutcome outcome) noexcept {
    switch (outcome) {
    case EffectSelectionOutcome::selected: return "selected";
    case EffectSelectionOutcome::none: return "none";
    case EffectSelectionOutcome::unknown_effect: return "unknown_effect";
    case EffectSelectionOutcome::parent_mismatch: return "parent_mismatch";
    case EffectSelectionOutcome::over_frame_budget: return "over_frame_budget";
    case EffectSelectionOutcome::unverified: return "unverified";
    }
    return "none";
}

EffectSelectionOutcome check_effect_selection(const EffectRegistry& registry, std::string_view name,
                                              EffectSelectionSource source, std::string_view parent, Size part,
                                              double scale, const EffectEntry*& entry) noexcept {
    entry = nullptr;
    if (name.empty() || source == EffectSelectionSource::none) return EffectSelectionOutcome::none;
    const EffectEntry* const found = registry.find(name);
    if (found == nullptr) return EffectSelectionOutcome::unknown_effect;
    if ((*found).parent != parent) return EffectSelectionOutcome::parent_mismatch;
    const EffectDeclaration& declaration = *(*found).declaration;
    // The frame's cost over the part and its outsets in device pixels, in
    // double: a part's logical size and the scale are finite and bounded,
    // and the comparison only needs to know whether the bound is passed.
    const Insets outsets = declaration.outsets;
    const double width = std::max(0.0, part.width + outsets.left + outsets.right) * scale;
    const double height = std::max(0.0, part.height + outsets.top + outsets.bottom) * scale;
    const double pixels = std::ceil(width) * std::ceil(height);
    double cost = static_cast<double>(declaration.cost.fixed) +
                  static_cast<double>(declaration.cost.per_device_pixel) * pixels;
    // Sampling the backdrop: the grown image's pixels, at the sampling cost.
    if (declaration.backdrop) {
        const double grown = 2.0 * static_cast<double>(effect_backdrop_half(declaration.backdrop_margin, scale) * 3U);
        const double sampled = (std::ceil(width) + grown) * (std::ceil(height) + grown);
        cost = cost + static_cast<double>(effect_backdrop_operations_per_pixel) * sampled;
    }
    if (!(cost <= static_cast<double>(maximum_effect_operations_per_frame))) {
        return EffectSelectionOutcome::over_frame_budget;
    }
    if (source == EffectSelectionSource::theme && !(*found).verified) return EffectSelectionOutcome::unverified;
    entry = found;
    return EffectSelectionOutcome::selected;
}

void note_effect_refused(const EffectSelection& selection) noexcept {
    const std::uint64_t before = refused_effects.fetch_add(1U, std::memory_order_relaxed);
    if (before != 0U) return;
    const std::size_t reported_bytes = selection.name.size() < maximum_reported_name_bytes
                                           ? selection.name.size()
                                           : maximum_reported_name_bytes;
    const std::string_view source = effect_selection_source_name(selection.source);
    const std::string_view outcome = effect_selection_outcome_name(selection.outcome);
    // The source and outcome names are short literals; the program name's
    // bound keeps every %.*s precision representable as an int.
    const std::size_t source_bytes =
        source.size() < maximum_reported_name_bytes ? source.size() : maximum_reported_name_bytes;
    const std::size_t outcome_bytes =
        outcome.size() < maximum_reported_name_bytes ? outcome.size() : maximum_reported_name_bytes;
    std::fprintf(stderr,
                 "GUI.Forms: effect program %.*s, selected by the %.*s, is refused (%.*s): the part draws its "
                 "primitive alone (further refusals are counted, not reported)\n",
                 static_cast<int>(reported_bytes), selection.name.data(), static_cast<int>(source_bytes),
                 source.data(), static_cast<int>(outcome_bytes), outcome.data());
}

bool effect_verification_recorded(std::string_view name, std::size_t state_size,
                                  const void* run_thunk) noexcept {
    try {
        const std::vector<std::vector<std::byte>> tables =
            gui_forms_note_descriptions(effect_verification_note_type, run_thunk);
        for (const std::vector<std::byte>& table : tables) {
            if (table_names(table, name, state_size)) return true;
        }
        return false;
    } catch (...) {
        // Reading the tables could not allocate: the effect is reported
        // unverified, which is the safe answer.
        return false;
    }
}

EffectRegistrationOutcome EffectRegistry::add(const EffectEntry& entry) noexcept {
    const EffectDeclaration* const declaration = entry.declaration;
    if (declaration == nullptr || !effect_name_valid((*declaration).name)) {
        return EffectRegistrationOutcome::name_not_snake_case;
    }
    if (entry.parent.empty() || entry.run == nullptr || entry.construct == nullptr) {
        return EffectRegistrationOutcome::no_parent;
    }
    if (entry.setup != nullptr && (*declaration).setup_cost == 0U) {
        return EffectRegistrationOutcome::setup_without_budget;
    }
    const double rate = (*declaration).frames_per_second;
    if (!(rate > 0.0) || rate > maximum_ambient_frames_per_second) {
        return EffectRegistrationOutcome::frame_rate_out_of_range;
    }
    const Insets outsets = (*declaration).outsets;
    if (!outset_in_range(outsets.left) || !outset_in_range(outsets.top) || !outset_in_range(outsets.right) ||
        !outset_in_range(outsets.bottom)) {
        return EffectRegistrationOutcome::outsets_out_of_range;
    }
    if (!backdrop_margin_in_range((*declaration).backdrop_margin)) {
        return EffectRegistrationOutcome::backdrop_out_of_range;
    }
    const std::uint32_t version = (*declaration).engine_version;
    if (version == 0U || version > current_effect_vocabulary_version) {
        return EffectRegistrationOutcome::vocabulary_version_unknown;
    }
    if (find((*declaration).name) != nullptr) return EffectRegistrationOutcome::duplicate_name;
    if (count_ == entries_.size()) return EffectRegistrationOutcome::registry_full;
    EffectEntry registered = entry;
    const void* const thunk = reinterpret_cast<const void*>(entry.run);
    registered.verified = effect_verification_recorded((*declaration).name, entry.size, thunk);
    entries_[count_] = registered;
    count_ = count_ + 1U;
    note_registration((*declaration).name, registered.verified);
    return EffectRegistrationOutcome::registered;
}

const EffectEntry* EffectRegistry::find(std::string_view name) const noexcept {
    for (std::size_t index = 0U; index < count_; ++index) {
        if ((*entries_[index].declaration).name == name) return &entries_[index];
    }
    return nullptr;
}

std::size_t EffectRegistry::unverified_count() const noexcept {
    std::size_t unverified = 0U;
    for (std::size_t index = 0U; index < count_; ++index) {
        if (!entries_[index].verified) unverified = unverified + 1U;
    }
    return unverified;
}

void effect_detail::StorageRelease::operator()(std::byte* storage) const noexcept {
    ::operator delete(storage, std::align_val_t(alignment));
}

void effect_detail::BlurWorkRelease::operator()(engine_primitives::BoxBlurWork* work) const noexcept {
    delete work;
}

std::uint32_t effect_backdrop_half(double margin, double scale) noexcept {
    if (!backdrop_margin_in_range(margin) || !(scale > 0.0) || !std::isfinite(scale)) return 0U;
    const double device = std::floor(margin * scale);
    const std::uint32_t half = static_cast<std::uint32_t>(device) / 3U;
    return half;
}

EffectInstance::~EffectInstance() {
    detach();
}

void EffectInstance::attach(const EffectEntry& entry, std::uint64_t seed, FrameTime now) {
    detach();
    // Effects are trivially destructible (EffectProgram), so releasing the
    // storage ends the state's lifetime.
    std::byte* const storage =
        static_cast<std::byte*>(::operator new(entry.size, std::align_val_t(entry.alignment)));
    storage_ = std::unique_ptr<std::byte, effect_detail::StorageRelease>(
        storage, effect_detail::StorageRelease{entry.alignment});
    state_ = (*entry.construct)(storage);
    entry_ = &entry;
    seed_ = seed;
    origin_ = now;
    counted_ = 0U;
    next_tick_ = 0U;
    pending_ = 0U;
    input_count_ = 0U;
    pointer_ = EffectPointer{};
    backdrop_.reset();
    backdrop_key_.reset();
    set_up_ = false;
    setup_part_ = Size{};
    setup_scale_ = 0.0;
    counters_ = EffectInstanceCounters{};
    last_meter_ = EffectCanvasMeter{};
}

void EffectInstance::detach() noexcept {
    state_ = nullptr;
    storage_.reset();
    entry_ = nullptr;
}

EffectAdvance EffectInstance::advance(FrameTime now) noexcept {
    EffectAdvance result{};
    if (entry_ == nullptr || now < origin_) {
        result.pending = pending_;
        return result;
    }
    // Exact in the clock's own ticks: floor(elapsed × 60 / 1 s).
    const std::chrono::nanoseconds elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - origin_);
    const std::uint64_t due = static_cast<std::uint64_t>(elapsed.count()) / 1000000000ULL * effect_ticks_per_second +
                              static_cast<std::uint64_t>(elapsed.count()) % 1000000000ULL * effect_ticks_per_second /
                                  1000000000ULL;
    if (due <= counted_) {
        result.pending = pending_;
        return result;
    }
    const std::uint64_t fresh = due - counted_;
    const std::uint64_t room = maximum_effect_ticks_per_frame - pending_;
    const std::uint64_t taken = fresh < room ? fresh : room;
    pending_ = pending_ + static_cast<std::uint32_t>(taken);
    const std::uint64_t skipped = fresh - taken;
    // Skipped ticks are never run: the effect's tick numbers stay
    // consecutive, and time moves on without them.
    counted_ = due;
    counters_.skipped_ticks = counters_.skipped_ticks + skipped;
    result.pending = pending_;
    result.skipped = static_cast<std::uint32_t>(skipped < 0xffffffffULL ? skipped : 0xffffffffULL);
    return result;
}

void EffectInstance::rebase(FrameTime now) noexcept {
    origin_ = now;
    counted_ = 0U;
    pending_ = 0U;
}

namespace {

// How much input `kind` admits: 0 none, 1 pointer, 2 pointer and keys.
std::uint32_t input_level(EffectInputKind kind) noexcept {
    if (kind == EffectInputKind::pointer) return 1U;
    if (kind == EffectInputKind::pointer_and_keys) return 2U;
    return 0U;
}

} // namespace

bool EffectInstance::queue_input(const EffectInputEvent& event, EffectInputKind granted) noexcept {
    if (entry_ == nullptr) return false;
    const std::uint32_t declared = input_level((*(*entry_).declaration).input);
    const std::uint32_t admitted = declared < input_level(granted) ? declared : input_level(granted);
    const std::uint32_t needed = event.kind == EffectInputEventKind::key_pressed ? 2U : 1U;
    if (admitted < needed) return false;
    if (input_count_ == input_.size()) {
        counters_.dropped_input = counters_.dropped_input + 1U;
        return false;
    }
    input_[input_count_] = event;
    input_count_ = input_count_ + 1U;
    return true;
}

void EffectInstance::set_input_grant(EffectInputKind grant) noexcept {
    input_grant_ = grant;
    // A withdrawn grant forgets where the pointer was.
    if (grant == EffectInputKind::none) pointer_ = EffectPointer{};
}

bool EffectInstance::note_pointer(const PointerEvent& event, Point part_origin) noexcept {
    if (entry_ == nullptr || input_grant_ == EffectInputKind::none ||
        (*(*entry_).declaration).input == EffectInputKind::none) {
        return false;
    }
    const EffectPointer before = pointer_;
    const Point local{event.position.x - part_origin.x, event.position.y - part_origin.y};
    bool queued = false;
    EffectInputEvent input{};
    input.position = local;
    switch (event.action) {
    case PointerAction::move:
        pointer_.inside = true;
        pointer_.position = local;
        input.kind = EffectInputEventKind::pointer_moved;
        queued = queue_input(input, input_grant_);
        break;
    case PointerAction::down:
        pointer_.inside = true;
        pointer_.pressed = true;
        pointer_.position = local;
        input.kind = EffectInputEventKind::pointer_pressed;
        queued = queue_input(input, input_grant_);
        break;
    case PointerAction::up:
        pointer_.pressed = false;
        pointer_.position = local;
        input.kind = EffectInputEventKind::pointer_released;
        queued = queue_input(input, input_grant_);
        break;
    case PointerAction::enter:
        pointer_.inside = true;
        pointer_.position = local;
        break;
    case PointerAction::leave:
        // The last position seen inside stays; a held button is let go.
        pointer_.inside = false;
        pointer_.pressed = false;
        break;
    case PointerAction::wheel:
        break;
    default:
        // An action a later host adds (and the wheel above) leaves the
        // standing pointer as it was: the effect is not given it.
        break;
    }
    const bool changed = queued || !(pointer_ == before);
    return changed;
}

std::shared_ptr<const PartRaster> EffectInstance::sample_backdrop(Painter& painter, const Control& host,
                                                                 Rect layer, double scale) {
    // The host's own surface root: a window, or a sub-surface, which samples
    // within its own surface (design note section 14).
    SurfaceRoot* const root = host.attached_surface_root();
    if (root == nullptr) return {};
    const detail::SurfaceEngineAccess::PaintPlace place = detail::SurfaceEngineAccess::paint_place(*root, host, painter);
    // A part recorded in the backplane plane would sample itself.
    if (place.painted_by == nullptr || (*place.painted_by).paint_plane() == PaintPlane::backplane) return {};
    const EffectDeclaration& declaration = *(*entry_).declaration;
    // An effect that is not ticked repaints only when told: the backplane's
    // damage under it tells it (EP-7), through a record of its own.
    if (declaration.time_model != EffectTimeModel::ambient) {
        PartKey key{};
        key.part = declaration.name;
        const std::optional<PartRecordHandle> record =
            detail::SurfaceEngineAccess::note_part(*root, host, key, AnatomyStateSet{});
        if (record) {
            const Rect extent{layer.x + place.origin.x, layer.y + place.origin.y, layer.width, layer.height};
            detail::SurfaceEngineAccess::note_paint(*root, *record, 0U, *place.painted_by, extent);
            detail::SurfaceEngineAccess::note_backplane_reader(*root, *record, true);
        }
    }
    const std::optional<std::uint32_t> width = device_side(layer.width, scale);
    const std::optional<std::uint32_t> height = device_side(layer.height, scale);
    if (!width || !height || *width == 0U || *height == 0U) return {};
    const DeviceExtent extent{*width, *height};
    // Under reduced transparency the backdrop is drawn unblurred.
    const bool flattened = (*root).presentation_settings().reduced_transparency;
    const std::uint32_t half = flattened ? 0U : effect_backdrop_half(declaration.backdrop_margin, scale);
    const std::uint32_t margin = half * 3U;
    const Point raster_origin{place.window_origin.x + layer.x, place.window_origin.y + layer.y};
    const detail::SurfaceEngineAccess::Backplane lent =
        detail::SurfaceEngineAccess::backplane(*root, raster_origin, extent, DeviceScale{scale}, margin);
    if (!lent.image) return {};
    BackdropKey key{};
    key.generation = lent.generation;
    key.origin = raster_origin;
    key.extent = extent;
    key.scale = scale;
    key.half = half;
    if (backdrop_ && backdrop_key_ && *backdrop_key_ == key) return backdrop_;
    const PartRaster& image = *lent.image;
    const DeviceExtent grown{extent.width + margin * 2U, extent.height + margin * 2U};
    if (!(image.extent() == grown)) return {};
    backdrop_key_.reset();
    // Refilled in place only while no recorded command holds it.
    const bool reusable = backdrop_ && backdrop_.use_count() == 1 && (*backdrop_).extent() == extent &&
                          (*backdrop_).scale() == DeviceScale{scale};
    if (!reusable) backdrop_ = PartRaster::create(extent, DeviceScale{scale});
    if (!backdrop_) return {};
    const std::size_t lanes = engine_primitives::box_blur_lanes;
    const std::size_t columns = grown.width;
    const std::size_t rows = grown.height;
    const std::size_t stride = columns * lanes;
    try {
        if (!blur_work_) {
            blur_work_ = std::unique_ptr<engine_primitives::BoxBlurWork, effect_detail::BlurWorkRelease>(
                new engine_primitives::BoxBlurWork{});
        }
        engine_primitives::prepare_box_blur(*blur_work_, columns, rows, lanes);
    } catch (const std::exception&) {
        // std::bad_alloc or std::length_error: no backdrop this paint.
        backdrop_.reset();
        return {};
    }
    // Into the blur's first image, blurred there (the result, extent-sized,
    // at its start, rows still `stride` apart), then out to the raster.
    engine_primitives::BoxBlurWork& work = *blur_work_;
    const std::span<const std::uint8_t> source = image.pixels();
    for (std::size_t row = 0U; row < rows; ++row) {
        const std::size_t read = row * image.row_bytes();
        const std::size_t write = row * stride;
        for (std::size_t element = 0U; element < stride; ++element) {
            work.first[write + element] = source[read + element];
        }
    }
    if (half > 0U) engine_primitives::blur_three_boxes(work, columns, rows, lanes, half);
    const PartRasterView destination = (*backdrop_).view();
    const std::size_t shown = static_cast<std::size_t>(extent.width) * lanes;
    // Unblurred, the image keeps its grown edge: margin is 0 then, so the
    // rows line up either way.
    for (std::size_t row = 0U; row < extent.height; ++row) {
        const std::size_t read = row * stride;
        const std::size_t write = row * destination.row_bytes;
        for (std::size_t element = 0U; element < shown; ++element) {
            destination.pixels[write + element] = static_cast<std::uint8_t>(work.first[read + element]);
        }
    }
    backdrop_key_ = key;
    return backdrop_;
}

void EffectInstance::paint_view(Painter& painter, Rect part, double scale, bool still, const void* view,
                                PartShape shape, const Control* host) {
    const EffectDeclaration& declaration = *(*entry_).declaration;
    if (!part.finite() || part.empty() || !(scale > 0.0) || !std::isfinite(scale)) return;
    const Size size{part.width, part.height};
    const bool geometry_changed = setup_part_ != size || setup_scale_ != scale;
    if ((*entry_).setup != nullptr && (!set_up_ || (geometry_changed && declaration.setup_on_geometry))) {
        EffectSetup setup{};
        setup.seed = seed_;
        setup.part = size;
        setup.scale = scale;
        (*(*entry_).setup)(state_, view, setup);
        counters_.setups = counters_.setups + 1U;
    }
    set_up_ = true;
    setup_part_ = size;
    setup_scale_ = scale;

    EffectFrame frame{};
    frame.still = still;
    frame.ticks = still ? 0U : pending_;
    frame.first_tick = next_tick_;
    frame.seed = seed_;
    frame.part = size;
    frame.scale = scale;
    frame.input = input_;
    frame.input_count = input_count_;
    frame.colours = colours_;
    // The standing pointer, only where the effect declares input and the
    // host grants it (section 8a(1)); otherwise zero.
    const bool pointer_granted =
        input_grant_ != EffectInputKind::none && declaration.input != EffectInputKind::none;
    if (pointer_granted) frame.pointer = pointer_;
    input_count_ = 0U;
    const Insets outsets = declaration.outsets;
    const Rect layer{part.x - outsets.left, part.y - outsets.top, part.width + outsets.left + outsets.right,
                     part.height + outsets.top + outsets.bottom};
    // The backdrop, sampled before the painter moves (EP-7).
    std::shared_ptr<const PartRaster> backdrop{};
    if (declaration.backdrop && host != nullptr) backdrop = sample_backdrop(painter, *host, layer, scale);
    painter.save();
    painter.clip_rect(layer);
    painter.translate(Point{part.x, part.y});
    {
        const PartCornerRadii corners = part_corner_radii(shape, Rect{0.0, 0.0, size.width, size.height});
        EffectCanvas canvas(painter, size, outsets, scale, declaration.cost.per_device_pixel, corners,
                            &backdrop);
        (*(*entry_).run)(state_, view, frame, canvas);
        last_meter_ = canvas.meter();
    }
    painter.restore();
    next_tick_ = next_tick_ + frame.ticks;
    counters_.ticks = counters_.ticks + frame.ticks;
    counters_.frames = counters_.frames + 1U;
    counters_.refused_calls = counters_.refused_calls + last_meter_.refused;
    if (!still) pending_ = 0U;
}

std::uint64_t EffectInstance::state_checksum() const noexcept {
    if (entry_ == nullptr) return 0U;
    const std::span<const std::byte> bytes(static_cast<const std::byte*>(state_), (*entry_).size);
    const std::uint64_t hash = fnv1a(bytes);
    return hash;
}

namespace {

// The process's installed registry (install_effect_registry), owned by its
// installer; null while none is installed.
std::atomic<const EffectRegistry*> installed_registry{nullptr};

} // namespace

void install_effect_registry(const EffectRegistry* registry) noexcept {
    installed_registry.store(registry, std::memory_order_release);
}

const EffectRegistry* installed_effect_registry() noexcept {
    const EffectRegistry* const registry = installed_registry.load(std::memory_order_acquire);
    return registry;
}

} // namespace gui_forms
