// The process's application font families (gui_forms/typography/app_fonts/app_fonts.hpp).
//
// The registry is a fixed, constant-initialized table of maximum_app_font_families entries in
// static storage, so it outlives every engine, theme and window that holds a family number, and
// every static that holds a handle. Entry n has number
// minimum_app_font_family + n; numbers are given in load order and never reused.
//
// Memory: an entry owns its faces' bytes through one shared AppFontBytes block, and with them
// the program's STXlib typeface of each face (program_typefaces.hpp), opened at load and
// registered for as long as the block lives, so every executor drawing the family draws the
// same typefaces. A text engine
// that opens the family copies a shared_ptr to that block (app_font_family_faces) and keeps it
// for as long as its faces live. Unloading moves the entry's reference out under the lock: if it
// was the last, the bytes go at once (released); otherwise they go with the last engine's
// reference (deferred), and the entry keeps only a weak reference to tell the two apart later.
// Either way unloading retires the family's typefaces first: an executor's face over one is
// rejected on its next draw and released, and no face opens over one again, so no executor
// draws an unloaded family while its bytes wait for the last engine.
//
// Threads: loads, unloads, name lookups and face copies take `registration`. A family's state is
// an atomic each text engine reads per shaped run without the lock; `given` publishes new entries
// with a release store, and readers look only below it.

#include "gui_forms/typography/app_fonts/app_fonts.hpp"

#include "gui_forms/typography/font_families/font_families.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include "program_typefaces.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string_view>
#include <stxlib/threads.hpp>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

// A family's faces as loaded: `count` of `faces` are live, each with the typeface opened over
// its bytes (null for a face STXlib refuses: its executors then fail to open it and quarantine
// the family, as before). Declared after `faces`, destroyed before them: a typeface reads them.
struct AppFontBytes final {
    std::array<AppFontFace, maximum_app_font_faces> faces{};
    std::size_t count{};
    std::array<std::unique_ptr<detail::ProgramTypeface>, maximum_app_font_faces> typefaces{};
};

// Retires every typeface of `block` (program_typefaces.hpp).
void retire_typefaces(const AppFontBytes& block) noexcept {
    for (std::size_t index = 0U; index < block.count; ++index) {
        const std::unique_ptr<detail::ProgramTypeface>& typeface = block.typefaces[index];
        if (typeface) (*typeface).retire();
    }
}

struct AppFontEntry final {
    // Written under the lock, read anywhere.
    std::atomic<AppFontState> state{AppFontState::absent};
    // Whether admit_font has logged a refusal of this family.
    std::atomic<bool> refusal_logged{false};
    // Guarded by the registry's lock.
    std::array<char, maximum_app_font_name_bytes> name{};
    std::size_t name_length{};
    std::shared_ptr<const AppFontBytes> bytes{};
    std::weak_ptr<const AppFontBytes> unloaded{};
};

struct AppFontTable final {
    stxlib::Mutex registration{};
    std::array<AppFontEntry, maximum_app_font_families> entries{};
    // How many entries have been given a number.
    std::atomic<std::size_t> given{0U};
};

struct AppFontCounters final {
    std::atomic<std::uint64_t> loads{0U};
    std::atomic<std::uint64_t> refused_loads{0U};
    std::atomic<std::uint64_t> refused_uses{0U};
    std::atomic<std::uint64_t> quarantines{0U};
    std::atomic<std::uint64_t> unloads_released{0U};
    std::atomic<std::uint64_t> unloads_deferred{0U};
};

// Constant-initialized, so both are in place before any dynamic initialization and are destroyed
// after every function-local static: a holder of AppFontFamily handles in static storage (the C
// ABI's table of loaded families) unloads them into a registry that still exists.
constinit AppFontTable app_font_table_storage{};
constinit AppFontCounters app_font_counters_storage{};

[[nodiscard]] AppFontTable& app_font_table() noexcept {
    return app_font_table_storage;
}

[[nodiscard]] AppFontCounters& app_font_counters() noexcept {
    return app_font_counters_storage;
}

// The entry index of `family` if a load gave its number, or maximum_app_font_families.
[[nodiscard]] std::size_t given_index(FontFamilyId family) noexcept {
    if (!app_font_family_number(family)) return maximum_app_font_families;
    const std::size_t index = static_cast<std::size_t>(family.value) - minimum_app_font_family;
    const std::size_t given = app_font_table().given.load(std::memory_order_acquire);
    if (index >= given) return maximum_app_font_families;
    return index;
}

// Whether an entry still holds its name: loaded and not unloaded.
[[nodiscard]] bool holds_name(AppFontState state) noexcept {
    const bool holds = state == AppFontState::live || state == AppFontState::quarantined;
    return holds;
}

[[nodiscard]] std::string_view entry_name(const AppFontEntry& entry) noexcept {
    const std::string_view name(entry.name.data(), entry.name_length);
    return name;
}

// Whether `name` may name a family: 1 to maximum_app_font_name_bytes bytes, none a control
// character, quote, comma or semicolon.
[[nodiscard]] bool admissible_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > maximum_app_font_name_bytes) return false;
    for (const char character : name) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7FU) return false;
        if (character == '"' || character == '\'' || character == ',' || character == ';') return false;
    }
    return true;
}

[[nodiscard]] AppFontLoad refused_load(AppFontStatus status, std::size_t face_index = 0U,
                                       std::uint32_t table = 0U) {
    app_font_counters().refused_loads.fetch_add(1U, std::memory_order_relaxed);
    AppFontLoad result{};
    result.validation.status = status;
    result.validation.table = table;
    result.face_index = face_index;
    return result;
}

[[nodiscard]] const char* quarantine_name(AppFontQuarantine reason) noexcept {
    switch (reason) {
    case AppFontQuarantine::open_failed: return "open_failed";
    case AppFontQuarantine::shaping_failed: return "shaping_failed";
    case AppFontQuarantine::rasterising_failed: return "rasterising_failed";
    default: break;
    }
    return "unknown";
}

// Unloads the family numbered `family`; what AppFontFamily::unload and its destructor do.
AppFontUnload unload_app_font_family(FontFamilyId family) noexcept {
    const std::size_t index = given_index(family);
    if (index == maximum_app_font_families) return AppFontUnload::not_loaded;
    AppFontTable& table = app_font_table();
    // Declared before the lock so the bytes, when this was their last reference, are freed after
    // it is released.
    std::shared_ptr<const AppFontBytes> held{};
    AppFontUnload result = AppFontUnload::not_loaded;
    {
        const stxlib::ScopedLock lock(table.registration);
        AppFontEntry& entry = table.entries[index];
        if (!holds_name(entry.state.load(std::memory_order_relaxed))) return result;
        held = std::move(entry.bytes);
        if (held) retire_typefaces(*held);
        entry.unloaded = held;
        const bool shared = held.use_count() > 1;
        result = shared ? AppFontUnload::deferred : AppFontUnload::released;
        const AppFontState state = shared ? AppFontState::unloading : AppFontState::released;
        entry.state.store(state, std::memory_order_release);
    }
    AppFontCounters& counters = app_font_counters();
    if (result == AppFontUnload::deferred) counters.unloads_deferred.fetch_add(1U, std::memory_order_relaxed);
    if (result == AppFontUnload::released) counters.unloads_released.fetch_add(1U, std::memory_order_relaxed);
    return result;
}

// Logs the first refusal of `family` in the interface context.
void log_refused_use(FontFamilyId family) noexcept {
    const std::size_t index = given_index(family);
    if (index == maximum_app_font_families) return;
    AppFontEntry& entry = app_font_table().entries[index];
    if (entry.refusal_logged.exchange(true, std::memory_order_relaxed)) return;
    std::fprintf(stderr,
                 "gui-forms-app-font=refused_use|family:%u|an application font family draws only inside "
                 "composites and sub-surfaces; the bundled faces drew instead\n",
                 static_cast<unsigned>(family.value));
    std::fflush(stderr);
}

} // namespace

AppFontLoad load_app_font_family(std::string_view name, std::span<const AppFontFace> faces) {
    if (faces.empty()) return refused_load(AppFontStatus::no_faces);
    if (faces.size() > maximum_app_font_faces) return refused_load(AppFontStatus::too_many_faces);
    if (!admissible_name(name)) return refused_load(AppFontStatus::name_invalid);
    if (font_family_named(name) != nullptr || generic_font_family(name) != nullptr) {
        return refused_load(AppFontStatus::name_taken);
    }
    for (std::size_t index = 0U; index < faces.size(); ++index) {
        const AppFontFace& face = faces[index];
        if (face.weight < 1U || face.weight > 1000U) return refused_load(AppFontStatus::weight_invalid, index);
        if (face.bytes == nullptr) return refused_load(AppFontStatus::empty, index);
        const std::span<const std::byte> bytes((*face.bytes).data(), (*face.bytes).size());
        const AppFontValidation validation = validate_app_font(bytes);
        if (validation.status != AppFontStatus::loaded) {
            return refused_load(validation.status, index, validation.table);
        }
    }
    // Built before the lock: the faces share the application's bytes. Each face's typeface is
    // opened over them and registered while the block lives; a block whose load is then refused
    // takes its registrations with it.
    std::shared_ptr<AppFontBytes> block = gui_forms::detail::make_library_shared<AppFontBytes>();
    for (std::size_t index = 0U; index < faces.size(); ++index) (*block).faces[index] = faces[index];
    (*block).count = faces.size();
    const std::shared_ptr<const void> owner = block;
    for (std::size_t index = 0U; index < faces.size(); ++index) {
        const std::span<const std::byte> bytes((*faces[index].bytes).data(), (*faces[index].bytes).size());
        std::unique_ptr<detail::ProgramTypeface> typeface = detail::open_program_typeface(bytes, 0U);
        if (!typeface) continue;
        const detail::ProgramTypeface& registered = *typeface;
        (*block).typefaces[index] = std::move(typeface);
        static_cast<void>(detail::register_program_typeface(bytes, 0U, owner, registered));
    }

    AppFontTable& table = app_font_table();
    FontFamilyId family{};
    {
        const stxlib::ScopedLock lock(table.registration);
        const std::size_t given = table.given.load(std::memory_order_relaxed);
        for (std::size_t index = 0U; index < given; ++index) {
            const AppFontEntry& entry = table.entries[index];
            if (!holds_name(entry.state.load(std::memory_order_relaxed))) continue;
            if (same_font_family_name(entry_name(entry), name)) return refused_load(AppFontStatus::name_taken);
        }
        if (given == maximum_app_font_families) return refused_load(AppFontStatus::capacity_exhausted);
        AppFontEntry& entry = table.entries[given];
        for (std::size_t index = 0U; index < name.size(); ++index) entry.name[index] = name[index];
        entry.name_length = name.size();
        entry.bytes = std::move(block);
        entry.state.store(AppFontState::live, std::memory_order_release);
        table.given.store(given + 1U, std::memory_order_release);
        family.value = static_cast<std::uint16_t>(minimum_app_font_family + given);
    }
    app_font_counters().loads.fetch_add(1U, std::memory_order_relaxed);
    AppFontLoad result{};
    result.family = AppFontFamily(family);
    return result;
}

AppFontFamily::~AppFontFamily() {
    if (id_.value != 0U) static_cast<void>(unload_app_font_family(id_));
}

AppFontFamily::AppFontFamily(AppFontFamily&& other) noexcept : id_(other.id_) {
    other.id_ = FontFamilyId{};
}

AppFontFamily& AppFontFamily::operator=(AppFontFamily&& other) noexcept {
    if (this == &other) return *this;
    if (id_.value != 0U) static_cast<void>(unload_app_font_family(id_));
    id_ = other.id_;
    other.id_ = FontFamilyId{};
    return *this;
}

AppFontUnload AppFontFamily::unload() noexcept {
    const FontFamilyId family = id_;
    id_ = FontFamilyId{};
    if (family.value == 0U) return AppFontUnload::not_loaded;
    const AppFontUnload result = unload_app_font_family(family);
    return result;
}

AppFontState app_font_family_state(FontFamilyId family) noexcept {
    const std::size_t index = given_index(family);
    if (index == maximum_app_font_families) return AppFontState::absent;
    AppFontTable& table = app_font_table();
    AppFontEntry& entry = table.entries[index];
    const AppFontState state = entry.state.load(std::memory_order_acquire);
    if (state != AppFontState::unloading) return state;
    // An unloading family becomes released once the last engine's reference is gone.
    const stxlib::ScopedLock lock(table.registration);
    if (!entry.unloaded.expired()) return AppFontState::unloading;
    entry.state.store(AppFontState::released, std::memory_order_release);
    return AppFontState::released;
}

bool font_family_drawable(FontFamilyId family) noexcept {
    if (family.value == 0U) return false;
    if (!app_font_family_number(family)) return font_family_record(family) != nullptr;
    const std::size_t index = given_index(family);
    if (index == maximum_app_font_families) return false;
    const AppFontState state = app_font_table().entries[index].state.load(std::memory_order_acquire);
    const bool drawable = state == AppFontState::live;
    return drawable;
}

bool known_font_family(FontFamilyId family) noexcept {
    if (family.value == 0U) return true;
    if (!app_font_family_number(family)) return font_family_record(family) != nullptr;
    const bool known = given_index(family) != maximum_app_font_families;
    return known;
}

FontFamilyId app_font_family_named(std::string_view name) {
    FontFamilyId result{};
    AppFontTable& table = app_font_table();
    const stxlib::ScopedLock lock(table.registration);
    const std::size_t given = table.given.load(std::memory_order_relaxed);
    for (std::size_t index = 0U; index < given; ++index) {
        const AppFontEntry& entry = table.entries[index];
        if (entry.state.load(std::memory_order_relaxed) != AppFontState::live) continue;
        if (!same_font_family_name(entry_name(entry), name)) continue;
        result.value = static_cast<std::uint16_t>(minimum_app_font_family + index);
        break;
    }
    return result;
}

void quarantine_app_font_family(FontFamilyId family, AppFontQuarantine reason) noexcept {
    const std::size_t index = given_index(family);
    if (index == maximum_app_font_families) return;
    AppFontEntry& entry = app_font_table().entries[index];
    AppFontState expected = AppFontState::live;
    const bool quarantined = entry.state.compare_exchange_strong(expected, AppFontState::quarantined,
                                                                 std::memory_order_acq_rel);
    if (!quarantined) return;
    app_font_counters().quarantines.fetch_add(1U, std::memory_order_relaxed);
    std::fprintf(stderr,
                 "gui-forms-app-font=quarantined|family:%u|reason:%s|the family draws no more text in "
                 "this process; its font's bundled faces draw instead\n",
                 static_cast<unsigned>(family.value), quarantine_name(reason));
    std::fflush(stderr);
}

std::size_t app_font_family_faces(FontFamilyId family, std::span<AppFontFaceSource> output) {
    const std::size_t index = given_index(family);
    if (index == maximum_app_font_families) return 0U;
    AppFontTable& table = app_font_table();
    const stxlib::ScopedLock lock(table.registration);
    const AppFontEntry& entry = table.entries[index];
    if (entry.state.load(std::memory_order_relaxed) != AppFontState::live) return 0U;
    if (entry.bytes == nullptr) return 0U;
    const AppFontBytes& block = *entry.bytes;
    if (block.count > output.size()) return 0U;
    const std::shared_ptr<const void> owner = entry.bytes;
    for (std::size_t face_index = 0U; face_index < block.count; ++face_index) {
        const AppFontFace& face = block.faces[face_index];
        AppFontFaceSource& source = output[face_index];
        source.bytes = std::span<const std::byte>((*face.bytes).data(), (*face.bytes).size());
        source.owner = owner;
        source.weight = face.weight;
        source.italic = face.italic;
    }
    return block.count;
}

FontSpec admit_font(FontSpec font, FontContext context) noexcept {
    if (context != FontContext::factory_bundle) return font;
    bool changed = false;
    if (app_font_family_number(font.family)) {
        log_refused_use(font.family);
        font.family = FontFamilyId{};
        changed = true;
    }
    const FontFamilyStack stack = font_family_stack(font.stack);
    bool stack_refused = false;
    for (std::size_t index = 0U; index < stack.count; ++index) {
        const FontFamilyId family = stack.families[index];
        if (!app_font_family_number(family)) continue;
        log_refused_use(family);
        stack_refused = true;
    }
    if (stack_refused) {
        font.stack = FontFamilyStackId{};
        changed = true;
    }
    if (changed) app_font_counters().refused_uses.fetch_add(1U, std::memory_order_relaxed);
    return font;
}

AppFontStatistics app_font_statistics() noexcept {
    const AppFontCounters& counters = app_font_counters();
    AppFontStatistics result{};
    result.loads = counters.loads.load(std::memory_order_relaxed);
    result.refused_loads = counters.refused_loads.load(std::memory_order_relaxed);
    result.refused_uses = counters.refused_uses.load(std::memory_order_relaxed);
    result.quarantines = counters.quarantines.load(std::memory_order_relaxed);
    result.unloads_released = counters.unloads_released.load(std::memory_order_relaxed);
    result.unloads_deferred = counters.unloads_deferred.load(std::memory_order_relaxed);
    return result;
}

} // namespace gui_forms
