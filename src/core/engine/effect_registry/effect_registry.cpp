#include "gui_forms/engine/effect_registry/effect_registry.hpp"

#include "../../text/localizable/gui_forms_notes.hpp"

#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
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
    return result;
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
    if (declaration == nullptr || !snake_case((*declaration).name)) {
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

void EffectInstance::StorageRelease::operator()(std::byte* storage) const noexcept {
    ::operator delete(storage, std::align_val_t(alignment));
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
    storage_ = std::unique_ptr<std::byte, StorageRelease>(storage, StorageRelease{entry.alignment});
    state_ = (*entry.construct)(storage);
    entry_ = &entry;
    seed_ = seed;
    origin_ = now;
    counted_ = 0U;
    next_tick_ = 0U;
    pending_ = 0U;
    input_count_ = 0U;
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

void EffectInstance::paint_view(Painter& painter, Rect part, double scale, bool still, const void* view) {
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
    input_count_ = 0U;
    const Insets outsets = declaration.outsets;
    const Rect layer{part.x - outsets.left, part.y - outsets.top, part.width + outsets.left + outsets.right,
                     part.height + outsets.top + outsets.bottom};
    painter.save();
    painter.clip_rect(layer);
    painter.translate(Point{part.x, part.y});
    {
        EffectCanvas canvas(painter, size, outsets, scale, declaration.cost.per_device_pixel);
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

} // namespace gui_forms
