#include "bundled_font_set.hpp"

#include "gui_forms/threading.hpp"
#include "stx_fontpack/loader.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <ios>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace gui_forms::detail {
namespace {

// {file name, size in bytes, SHA-256}, generated at configure time.
constexpr BundledFaceRecord compiled_manifest[] = {
#include "bundled_font_manifest.inc"
};

constexpr BundledFaceUse face_uses[] = {
    {"PortsmouthRapids.ttf", FontRole::control, 400, false, true},
    {"PortsmouthRapids-Bold.ttf", FontRole::control, 700, false, true},
    {"Carlito-Regular.ttf", FontRole::content, 400, false, true},
    {"Carlito-Bold.ttf", FontRole::content, 700, false, true},
    {"Carlito-Italic.ttf", FontRole::content, 400, true, true},
    {"Carlito-BoldItalic.ttf", FontRole::content, 700, true, true},
    {"Cousine-Regular.ttf", FontRole::monospace, 400, false, true},
    {"Cousine-Bold.ttf", FontRole::monospace, 700, false, false},
    {"Cousine-Italic.ttf", FontRole::monospace, 400, true, false},
    {"Cousine-BoldItalic.ttf", FontRole::monospace, 700, true, false},
    // The fallback faces, in the order fallback selection tries them (tools/fallback_font_subsets.py,
    // FALLBACK_ORDER): the Japanese and Korean faces, then the minimal Chinese face (only the
    // GB 2312 level 1 hanzi those two lack), Arabic, Hebrew and the minimal Indic faces, then
    // emoji. A character both CJK faces hold draws from the Japanese face: the text engine has
    // no text language to choose by yet.
    {"NotoSansCJKjp-Subset.otf", std::nullopt, 400, false, false},
    {"NotoSansCJKkr-Subset.otf", std::nullopt, 400, false, false},
    {"NotoSansCJKsc-Minimal.otf", std::nullopt, 400, false, false},
    {"NotoSansArabic-Minimal.ttf", std::nullopt, 400, false, false},
    {"NotoSansHebrew-Regular.ttf", std::nullopt, 400, false, false},
    {"NotoSansDevanagari-Minimal.ttf", std::nullopt, 400, false, false},
    {"NotoSansBengali-Minimal.ttf", std::nullopt, 400, false, false},
    {"NotoSansGurmukhi-Minimal.ttf", std::nullopt, 400, false, false},
    {"NotoEmoji-Regular.ttf", std::nullopt, 400, false, false},
};

#include "bundled_font_coverage.inc"

// The shared set and the source it reads: the embedded pack, or a directory; see
// shared_bundled_font_set.
struct SharedFontSet final {
    bool embedded_pack{};
    std::filesystem::path directory{};
    std::weak_ptr<BundledFontSet> set{};
};
SharedFontSet& shared_font_set() {
    // Never destroyed (rule 1: process termination releases none of our state).
    static SharedFontSet* const value = new SharedFontSet{};
    return *value;
}
// Guards shared_font_set: the hosts ask on the UI thread, GUI.Drawing's executors on theirs.
std::mutex& shared_font_set_mutex() {
    // Never destroyed (rule 1: process termination releases none of our state).
    static std::mutex* const value = new std::mutex();
    return *value;
}

// The process's font lookups (bundled_font_diagnostics). The refusal and notice texts are
// guarded by font_refusal_mutex, as a host may read them from another thread.
std::atomic<std::uint64_t> font_lookups{0U};
std::atomic<std::uint64_t> font_lookups_without_source{0U};
std::atomic<std::uint8_t> last_font_source{static_cast<std::uint8_t>(BundledFontSource::none)};
std::atomic<std::uint32_t> fallback_faces_found{0U};
std::atomic<std::uint32_t> fallback_faces_expected{0U};
std::atomic<std::uint64_t> font_pack_refusals{0U};
std::mutex& font_refusal_mutex() {
    // Never destroyed (rule 1: process termination releases none of our state).
    static std::mutex* const value = new std::mutex();
    return *value;
}
std::string& font_refusal() {
    // Never destroyed (rule 1: process termination releases none of our state).
    static std::string* const value = new std::string();
    return *value;
}
std::string& font_notice() {
    // Never destroyed (rule 1: process termination releases none of our state).
    static std::string* const value = new std::string();
    return *value;
}
std::string& font_pack_refusal() {
    // Never destroyed (rule 1: process termination releases none of our state).
    static std::string* const value = new std::string();
    return *value;
}

// Records a pack refusal (bundled_font_pack_refusal); the process's first is also written to
// stderr.
void record_pack_refusal(const std::string& text) {
    const std::uint64_t before = font_pack_refusals.fetch_add(1U, std::memory_order_relaxed);
    if (before == 0U) std::fprintf(stderr, "%s\n", text.c_str());
    const std::lock_guard<std::mutex> lock(font_refusal_mutex());
    font_pack_refusal() = text;
}

// Appends `directory` to `text` as UTF-8.
void append_directory(std::string& text, const std::filesystem::path& directory) {
    const std::u8string spelled = directory.u8string();
    text.append(reinterpret_cast<const char*>(spelled.data()), spelled.size());
}

// Appends the directories of `candidates` the lookup tried, each with its source, to `text`.
void append_tried_directories(std::string& text, std::span<const BundledFontCandidate> candidates) {
    std::size_t tried{};
    for (const BundledFontCandidate& candidate : candidates) {
        if (candidate.directory.empty()) continue;
        text.append(tried == 0U ? " " : ", ");
        text.append(bundled_font_source_name(candidate.source));
        text.append(" (");
        append_directory(text, candidate.directory);
        text.append(")");
        ++tried;
    }
    if (tried == 0U) text.append(" no directory (the host could not find its own location)");
}

// The refusal for a lookup that found nothing: the pack did not open, and when directories
// were honoured, none of `candidates` held the faces either.
std::string refusal_text(const BundledFontLookup& lookup) {
    std::string text =
        "GUI.Forms: the embedded font pack does not open (it is damaged or truncated), so windows draw no text";
    if (lookup.directories_honoured) {
        text.append("; no development font directory holds the required faces either, tried");
        append_tried_directories(text, lookup.candidates);
        text.append(". Set GUI_FORMS_FONT_DIR to assets/fonts");
    }
    text.append(".");
    return text;
}

// The notice for a development lookup whose directories held no faces, so the pack serves.
std::string development_notice_text(std::span<const BundledFontCandidate> candidates) {
    std::string text = "GUI.Forms: no development font directory holds the required faces, tried";
    append_tried_directories(text, candidates);
    text.append("; the embedded font pack is used.");
    return text;
}

// Whether `use` is a bundled fallback face: optional, and serving any role.
[[nodiscard]] bool is_fallback_face(const BundledFaceUse& use) noexcept {
    const bool fallback = !use.role && !use.required;
    return fallback;
}

// The fallback faces of face_uses.
[[nodiscard]] std::uint32_t fallback_face_total() noexcept {
    std::uint32_t total{};
    for (const BundledFaceUse& use : face_uses) {
        if (is_fallback_face(use)) ++total;
    }
    return total;
}

// Counts the fallback faces `directory` holds as regular files, and appends each absent one's
// name to `absent`, comma-separated.
[[nodiscard]] std::uint32_t count_fallback_faces(const std::filesystem::path& directory, std::string& absent) {
    std::uint32_t found{};
    for (const BundledFaceUse& use : face_uses) {
        if (!is_fallback_face(use)) continue;
        std::error_code error;
        const bool present = std::filesystem::is_regular_file(directory / use.file_name, error);
        if (present && !error) {
            ++found;
            continue;
        }
        if (!absent.empty()) absent.append(", ");
        absent.append(use.file_name);
    }
    return found;
}

// Counts the fallback faces the opened pack `index` holds, and appends each absent one's name
// to `absent`, comma-separated.
[[nodiscard]] std::uint32_t count_packed_fallback_faces(const stx_fontpack::PackIndex& index, std::string& absent) {
    std::uint32_t found{};
    for (const BundledFaceUse& use : face_uses) {
        if (!is_fallback_face(use)) continue;
        bool present = false;
        for (std::uint32_t font = 0U; font < index.font_count; ++font) {
            const stx_fontpack::PackFont& packed = index.fonts[font];
            const std::string_view name(packed.name, packed.name_length);
            if (name == use.file_name) {
                present = true;
                break;
            }
        }
        if (present) {
            ++found;
            continue;
        }
        if (!absent.empty()) absent.append(", ");
        absent.append(use.file_name);
    }
    return found;
}

// The notice for a pack built without the fallback faces named in `absent`.
std::string packed_fallback_notice_text(std::uint32_t found, std::uint32_t expected, const std::string& absent) {
    std::string text = "GUI.Forms: the embedded font pack leaves out bundled fallback faces (";
    text.append(std::to_string(found));
    text.append(" of ");
    text.append(std::to_string(expected));
    text.append("; absent: ");
    text.append(absent);
    text.append("), so text only they cover draws as missing glyphs. Configure with "
                "GUI_FORMS_INSTALL_FALLBACK_FONTS=ON to embed them.");
    return text;
}

// Joins two notices with a space; either may be empty.
std::string joined_notice(std::string first, const std::string& second) {
    if (first.empty()) return second;
    if (!second.empty()) {
        first.append(" ");
        first.append(second);
    }
    return first;
}

// The notice for a directory lacking the fallback faces named in `absent`.
std::string fallback_notice_text(const std::filesystem::path& directory, std::uint32_t found,
                                 std::uint32_t expected, const std::string& absent) {
    std::string text = "GUI.Forms: the bundled fallback faces are not all installed (";
    text.append(std::to_string(found));
    text.append(" of ");
    text.append(std::to_string(expected));
    text.append(" in ");
    append_directory(text, directory);
    text.append("; absent: ");
    text.append(absent);
    text.append("), so text only they cover draws as missing glyphs. Install the fallback_fonts component "
                "(GUI_FORMS_INSTALL_FALLBACK_FONTS) or package the program with fallback_fonts.");
    return text;
}

// Pages from `reserve` that have not been handed to a SealedFontFace yet; released on every
// exit that does not hand them over.
class PageReservation final {
public:
    PageReservation(const SealedPageFunctions& pages, std::size_t bytes) noexcept
        : release_(pages.release), storage_((*pages.reserve)(bytes)), size_bytes_(bytes) {}
    ~PageReservation() {
        if (storage_ != nullptr) (*release_)(storage_, size_bytes_);
    }
    PageReservation(const PageReservation&) = delete;
    PageReservation& operator=(const PageReservation&) = delete;

    [[nodiscard]] std::byte* storage() const noexcept { return storage_; }
    // Called once a SealedFontFace owns the pages.
    void disown() noexcept { storage_ = nullptr; }

private:
    void (*release_)(std::byte* pages, std::size_t bytes) noexcept;
    std::byte* storage_;
    std::size_t size_bytes_;
};

// A decoded pack group, given back to its allocator on every exit (group_release leaves a
// refused, storage-less group as it is).
struct DecodedGroup final {
    stx_fontpack::LoadedGroup group{};
    DecodedGroup() = default;
    ~DecodedGroup() { stx_fontpack::group_release(group); }
    DecodedGroup(const DecodedGroup&) = delete;
    DecodedGroup& operator=(const DecodedGroup&) = delete;
};

// One worker call of a pack decode step on the pool: the step, the worker's index, and
// whether the pool started it (written by the call, read after the pool returns or refuses).
struct PoolWorkerCall final {
    const stx_fontpack::WorkerTask* task{};
    std::uint32_t worker{};
    std::atomic<bool> started{};
};

void run_pool_worker_call(void* context) noexcept {
    PoolWorkerCall& call = *static_cast<PoolWorkerCall*>(context);
    call.started.store(true, std::memory_order_release);
    const stx_fontpack::WorkerTask& task = *call.task;
    (*task.run)(task.context, call.worker);
}

// stx_fontpack's Executor::run_workers over the gui_forms::AtomicThreadPool `context`: the
// step's calls go to the pool as one batch, which returns once every call has (its join).
// The loader calls this from noexcept code, so nothing may escape: if the pool refuses the
// batch (it throws, as on an allocation failure), every call it did not start runs here in
// turn. That is correct for any step, whose calls may run in any order and never wait for one
// another (stx_fontpack/runtime.hpp); a call the pool started has returned when run does.
void run_pool_workers(void* context, const stx_fontpack::WorkerTask& task) {
    gui_forms::AtomicThreadPool& pool = *static_cast<gui_forms::AtomicThreadPool*>(context);
    const std::uint32_t count = std::min<std::uint32_t>(task.worker_count, stx_fontpack::kMaxWorkers);
    std::array<PoolWorkerCall, stx_fontpack::kMaxWorkers> calls{};
    std::array<gui_forms::AtomicTask, stx_fontpack::kMaxWorkers> entries{};
    for (std::uint32_t worker = 0U; worker < count; ++worker) {
        calls[worker].task = &task;
        calls[worker].worker = worker;
        entries[worker].entry = &run_pool_worker_call;
        entries[worker].context = &calls[worker];
    }
    bool refused = false;
    try {
        pool.run(std::span<const gui_forms::AtomicTask>(entries.data(), count));
    } catch (...) {
        refused = true;
    }
    if (!refused) return;
    for (std::uint32_t worker = 0U; worker < count; ++worker) {
        if (calls[worker].started.load(std::memory_order_acquire)) continue;
        run_pool_worker_call(&calls[worker]);
    }
}

// A pool for one pack decode, owned by the decoding thread (AtomicThreadPool's methods belong
// to one thread), with one thread per hardware thread up to the loader's limit; null where
// decoding runs serially instead: a single-threaded Emscripten build, one hardware thread, or
// a pool that fails to start.
std::unique_ptr<gui_forms::AtomicThreadPool> make_decode_pool() noexcept {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    return {};
#else
    const unsigned int hardware = std::thread::hardware_concurrency();
    const std::uint32_t threads = std::min<std::uint32_t>(hardware, stx_fontpack::kMaxWorkers);
    if (threads < 2U) return {};
    std::unique_ptr<gui_forms::AtomicThreadPool> pool{};
    try {
        pool = std::make_unique<gui_forms::AtomicThreadPool>(threads);
    } catch (...) {
        // The pool stays null: this decode runs serially.
    }
    return pool;
#endif
}

} // namespace

std::span<const BundledFaceRecord> bundled_face_manifest() noexcept {
    const std::span<const BundledFaceRecord> manifest(compiled_manifest);
    return manifest;
}

std::span<const BundledFaceUse> bundled_face_uses() noexcept {
    const std::span<const BundledFaceUse> uses(face_uses);
    return uses;
}

std::span<const BundledFaceCoverage> bundled_face_coverages() noexcept {
    const std::span<const BundledFaceCoverage> coverages(generated_coverages);
    return coverages;
}

std::optional<std::span<const CodepointRange>> bundled_face_coverage(
    std::string_view file_name) noexcept {
    for (const BundledFaceCoverage& coverage : generated_coverages) {
        if (coverage.file_name != file_name) continue;
        for (const BundledFaceRecord& record : compiled_manifest) {
            if (record.file_name == file_name && record.sha256_hex == coverage.sha256_hex)
                return coverage.ranges;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

bool development_font_directories() noexcept {
#if defined(GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY)
    return true;
#else
    return false;
#endif
}

std::filesystem::path development_source_font_directory() {
#if defined(GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY)
    const std::filesystem::path directory(GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY);
#else
    const std::filesystem::path directory{};
#endif
    return directory;
}

std::shared_ptr<BundledFontSet> shared_bundled_font_set(const std::filesystem::path& directory,
                                                        const SealedPageFunctions& pages) {
    const std::lock_guard<std::mutex> lock(shared_font_set_mutex());
    std::shared_ptr<BundledFontSet> set = shared_font_set().set.lock();
    if (set && !shared_font_set().embedded_pack && shared_font_set().directory == directory) return set;
    set = std::make_shared<BundledFontSet>(directory, pages);
    shared_font_set().embedded_pack = false;
    shared_font_set().directory = directory;
    shared_font_set().set = set;
    return set;
}

std::shared_ptr<BundledFontSet> shared_bundled_font_set(const BundledFontLocation& location,
                                                        const SealedPageFunctions& pages) {
    std::shared_ptr<BundledFontSet> set{};
    if (location.source == BundledFontSource::none) return set;
    if (location.source != BundledFontSource::embedded_pack) {
        set = shared_bundled_font_set(location.directory, pages);
        return set;
    }
    const std::lock_guard<std::mutex> lock(shared_font_set_mutex());
    set = shared_font_set().set.lock();
    if (set && shared_font_set().embedded_pack) return set;
    set = std::make_shared<BundledFontSet>(bundled_font_pack(), pages);
    shared_font_set().embedded_pack = true;
    shared_font_set().directory.clear();
    shared_font_set().set = set;
    return set;
}

bool directory_holds_required_faces(const std::filesystem::path& directory) {
    if (directory.empty()) return false;
    for (const BundledFaceUse& use : face_uses) {
        if (!use.required) continue;
        std::error_code error;
        const bool present = std::filesystem::is_regular_file(directory / use.file_name, error);
        if (!present || error) return false;
    }
    return true;
}

BundledFontLocation locate_bundled_fonts(const BundledFontLookup& lookup) {
    BundledFontLocation location{};
    if (lookup.directories_honoured) {
        for (const BundledFontCandidate& candidate : lookup.candidates) {
            if (!directory_holds_required_faces(candidate.directory)) continue;
            location.source = candidate.source;
            location.directory = candidate.directory;
            break;
        }
    }
    // The pack's index is a few kilobytes of fixed arrays: on the heap, not the stack. It is
    // read only to see that the pack opens and which fallback faces it holds.
    const std::unique_ptr<stx_fontpack::PackIndex> index = std::make_unique<stx_fontpack::PackIndex>();
    if (location.source == BundledFontSource::none && stx_fontpack::pack_open(lookup.pack, *index))
        location.source = BundledFontSource::embedded_pack;
    font_lookups.fetch_add(1U, std::memory_order_relaxed);
    last_font_source.store(static_cast<std::uint8_t>(location.source), std::memory_order_relaxed);
    const std::uint32_t expected = fallback_face_total();
    fallback_faces_expected.store(expected, std::memory_order_relaxed);
    if (location.source == BundledFontSource::embedded_pack) {
        std::string absent{};
        const std::uint32_t found = count_packed_fallback_faces(*index, absent);
        fallback_faces_found.store(found, std::memory_order_relaxed);
        std::string notice{};
        if (lookup.directories_honoured) notice = development_notice_text(lookup.candidates);
        if (found != expected) notice = joined_notice(std::move(notice), packed_fallback_notice_text(found, expected, absent));
        const std::lock_guard<std::mutex> lock(font_refusal_mutex());
        font_refusal().clear();
        font_notice() = std::move(notice);
        return location;
    }
    if (location.source != BundledFontSource::none) {
        // A development directory: the fallback faces are optional there too, so count them
        // and keep a notice, but accept the directory either way.
        std::string absent{};
        const std::uint32_t found = count_fallback_faces(location.directory, absent);
        fallback_faces_found.store(found, std::memory_order_relaxed);
        std::string notice{};
        if (found != expected) notice = fallback_notice_text(location.directory, found, expected, absent);
        const std::lock_guard<std::mutex> lock(font_refusal_mutex());
        font_refusal().clear();
        font_notice() = std::move(notice);
        return location;
    }
    fallback_faces_found.store(0U, std::memory_order_relaxed);
    const std::string refusal = refusal_text(lookup);
    const std::uint64_t before = font_lookups_without_source.fetch_add(1U, std::memory_order_relaxed);
    if (before == 0U) std::fprintf(stderr, "%s\n", refusal.c_str());
    const std::lock_guard<std::mutex> lock(font_refusal_mutex());
    font_refusal() = refusal;
    font_notice().clear();
    return location;
}

BundledFontLocation locate_bundled_fonts(std::span<const BundledFontCandidate> candidates) {
    BundledFontLookup lookup{};
    lookup.candidates = candidates;
    lookup.directories_honoured = development_font_directories();
    lookup.pack = bundled_font_pack();
    const BundledFontLocation location = locate_bundled_fonts(lookup);
    return location;
}

SealedFontFace::SealedFontFace(const SealedPageFunctions& pages, std::byte* storage,
                               std::size_t size_bytes) noexcept
    : release_(pages.release), storage_(storage), size_bytes_(size_bytes) {}

SealedFontFace::~SealedFontFace() {
    (*release_)(storage_, size_bytes_);
}

std::span<const std::byte> SealedFontFace::bytes() const noexcept {
    const std::span<const std::byte> bytes(storage_, size_bytes_);
    return bytes;
}

FontFaceLoad::FontFaceLoad(State state) noexcept : state_(std::move(state)) {}

FontFaceLoad FontFaceLoad::loaded(std::shared_ptr<const SealedFontFace> face) {
    if (!face) throw std::invalid_argument("FontFaceLoad::loaded needs a face");
    FontFaceLoad result(State(std::in_place_index<0>, std::move(face)));
    return result;
}

FontFaceLoad FontFaceLoad::failed(FontSetError error) noexcept {
    FontFaceLoad result(State(std::in_place_index<1>, error));
    return result;
}

bool FontFaceLoad::accepted() const noexcept {
    const bool accepted = std::holds_alternative<std::shared_ptr<const SealedFontFace>>(state_);
    return accepted;
}

const std::shared_ptr<const SealedFontFace>* FontFaceLoad::face() const noexcept {
    const std::shared_ptr<const SealedFontFace>* face =
        std::get_if<std::shared_ptr<const SealedFontFace>>(&state_);
    return face;
}

std::optional<FontSetError> FontFaceLoad::error() const noexcept {
    const FontSetError* error = std::get_if<FontSetError>(&state_);
    if (error == nullptr) return std::nullopt;
    return *error;
}

BundledFontSet::BundledFontSet(std::filesystem::path directory, const SealedPageFunctions& pages)
    : BundledFontSet(std::move(directory), bundled_face_manifest(), pages) {}

BundledFontSet::BundledFontSet(std::filesystem::path directory,
                               std::span<const BundledFaceRecord> manifest,
                               const SealedPageFunctions& pages)
    : directory_(std::move(directory)), manifest_(manifest), pages_(pages) {
    if (pages_.reserve == nullptr || pages_.seal == nullptr || pages_.release == nullptr)
        throw std::invalid_argument("BundledFontSet needs reserve, seal and release");
    digests_.reserve(manifest_.size());
    for (const BundledFaceRecord& record : manifest_) {
        if (record.size_bytes == 0U || record.size_bytes > maximum_bundled_face_bytes)
            throw std::invalid_argument("bundled face size outside 1 byte to 64 MiB");
        Sha256Digest digest{};
        if (!parse_sha256_hex(std::span<const char>(record.sha256_hex.data(), record.sha256_hex.size()),
                              digest))
            throw std::invalid_argument("bundled face digest is not 64 hexadecimal digits");
        digests_.push_back(digest);
    }
    results_.resize(manifest_.size());
}

BundledFontSet::BundledFontSet(std::span<const std::byte> pack, const SealedPageFunctions& pages)
    : BundledFontSet(pack, bundled_face_manifest(), pages) {}

BundledFontSet::BundledFontSet(std::span<const std::byte> pack, std::span<const BundledFaceRecord> manifest,
                               const SealedPageFunctions& pages)
    : BundledFontSet(std::filesystem::path{}, manifest, pages) {
    pack_set_ = true;
    pack_ = pack;
    index_ = std::make_unique<stx_fontpack::PackIndex>();
    pack_fonts_.assign(manifest_.size(), no_pack_font);
    group_decoded_.assign(stx_fontpack::kMaxGroups, std::uint8_t{0});
    pack_opened_ = stx_fontpack::pack_open(pack_, *index_);
    if (!pack_opened_) {
        record_pack_refusal("GUI.Forms: the font pack does not open (it is damaged or truncated), so no bundled "
                            "face is drawn.");
        return;
    }
    // Each manifest face's font in the pack, by name; the first match, as the loader finds it.
    for (std::size_t index = 0; index < manifest_.size(); ++index) {
        for (std::uint32_t font = 0U; font < (*index_).font_count; ++font) {
            const stx_fontpack::PackFont& packed = (*index_).fonts[font];
            const std::string_view name(packed.name, packed.name_length);
            if (name != manifest_[index].file_name) continue;
            pack_fonts_[index] = font;
            break;
        }
    }
}

// Out of line: the pack index is complete only here.
BundledFontSet::~BundledFontSet() = default;

FontFaceLoad BundledFontSet::face(std::string_view file_name) {
    for (std::size_t index = 0; index < manifest_.size(); ++index) {
        if (manifest_[index].file_name != file_name) continue;
        const std::lock_guard<std::mutex> lock(mutex_);
        std::optional<FontFaceLoad>& kept = results_[index];
        // A pack set's first request of any face decodes the eager groups, which may answer
        // this one too.
        if (!kept && pack_set_ && !eager_decoded_) decode_eager_groups();
        if (!kept) kept = pack_set_ ? load_from_pack(index) : load(index);
        return *kept;
    }
    const FontFaceLoad unknown = FontFaceLoad::failed(FontSetError::unknown_face);
    return unknown;
}

std::size_t BundledFontSet::loaded_face_count() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (const std::optional<FontFaceLoad>& kept : results_) {
        if (kept && (*kept).accepted()) ++count;
    }
    return count;
}

std::uint64_t BundledFontSet::loaded_bytes() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t total = 0;
    for (const std::optional<FontFaceLoad>& kept : results_) {
        if (!kept) continue;
        const std::shared_ptr<const SealedFontFace>* face = (*kept).face();
        if (face != nullptr) total += (*(*face)).bytes().size();
    }
    return total;
}

std::size_t BundledFontSet::decoded_group_count() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t count = decoded_groups_;
    return count;
}

std::optional<std::size_t> BundledFontSet::manifest_position(std::string_view file_name) const noexcept {
    for (std::size_t index = 0; index < manifest_.size(); ++index) {
        if (manifest_[index].file_name == file_name) return index;
    }
    return std::nullopt;
}

// The face at `index` from its pack group, decoding the group if it has not been. A face the
// pack does not hold is unreadable; a face of a refused group, or of a pack that did not open,
// is pack_refused (refuse_group kept that result).
FontFaceLoad BundledFontSet::load_from_pack(std::size_t index) {
    if (!pack_opened_) return FontFaceLoad::failed(FontSetError::pack_refused);
    const std::uint32_t font = pack_fonts_[index];
    if (font == no_pack_font) return FontFaceLoad::failed(FontSetError::unreadable);
    const std::uint32_t group = (*index_).fonts[font].group;
    if (group_decoded_[group] == 0U) decode_group(group);
    const std::optional<FontFaceLoad>& decoded = results_[index];
    if (decoded) return *decoded;
    // Not reached for a pack that opened: every font of a decoded group is kept.
    return FontFaceLoad::failed(FontSetError::unreadable);
}

void BundledFontSet::decode_eager_groups() {
    eager_decoded_ = true;
    if (!pack_opened_) return;
    for (std::uint32_t group = 0U; group < (*index_).group_count; ++group) {
        if (!(*index_).groups[group].eager || group_decoded_[group] != 0U) continue;
        decode_group(group);
    }
}

// Decodes `group` once (on the pool, or in turn here), keeps a sealed copy of every manifest
// face in it and gives the decoded storage back, so the set holds each face once. A refusal
// refuses every manifest face of the group and is recorded.
void BundledFontSet::decode_group(std::uint32_t group) {
    group_decoded_[group] = 1U;
    ++decoded_groups_;
    const std::unique_ptr<gui_forms::AtomicThreadPool> pool = make_decode_pool();
    stx_fontpack::Executor executor = stx_fontpack::serial_executor();
    if (pool) {
        const std::uint32_t threads = (*pool).thread_count();
        executor.run_workers = &run_pool_workers;
        executor.context = pool.get();
        executor.worker_count = std::clamp<std::uint32_t>(threads, 1U, stx_fontpack::kMaxWorkers);
    }
    DecodedGroup decoded;
    const stx_fontpack::Allocator allocator = stx_fontpack::standard_allocator();
    if (!stx_fontpack::pack_load_group(*index_, group, allocator, executor, decoded.group)) {
        refuse_group(group, stx_fontpack::refusal_name(decoded.group.refusal));
        return;
    }
    for (std::uint32_t font = 0U; font < decoded.group.font_count; ++font) {
        const stx_fontpack::LoadedFont& loaded = decoded.group.fonts[font];
        const std::string_view name(loaded.name, loaded.name_length);
        const std::optional<std::size_t> position = manifest_position(name);
        // A packed font the manifest does not list is never asked for.
        if (!position) continue;
        std::optional<FontFaceLoad>& kept = results_[*position];
        if (kept) continue;
        kept = seal_copy(*position, loaded.data, loaded.size);
    }
}

void BundledFontSet::refuse_group(std::uint32_t group, std::string_view reason) {
    const stx_fontpack::PackGroup& refused = (*index_).groups[group];
    std::string text = "GUI.Forms: the font pack's group ";
    text.append(refused.name, refused.name_length);
    text.append(" is refused (");
    text.append(reason);
    text.append("), so its faces draw as missing:");
    std::size_t listed{};
    for (std::size_t index = 0; index < manifest_.size(); ++index) {
        const std::uint32_t font = pack_fonts_[index];
        if (font == no_pack_font || (*index_).fonts[font].group != group) continue;
        std::optional<FontFaceLoad>& kept = results_[index];
        if (!kept) kept = FontFaceLoad::failed(FontSetError::pack_refused);
        text.append(listed == 0U ? " " : ", ");
        text.append(manifest_[index].file_name);
        ++listed;
    }
    text.append(".");
    record_pack_refusal(text);
}

// Copies one decoded face into sealed pages, once its size matches the manifest's (its bytes
// already matched the pack's CRC-32). The pages are released on every failure.
FontFaceLoad BundledFontSet::seal_copy(std::size_t index, const std::uint8_t* data, std::size_t size_bytes) const {
    const BundledFaceRecord& record = manifest_[index];
    if (size_bytes != record.size_bytes) return FontFaceLoad::failed(FontSetError::size_mismatch);
    PageReservation reservation(pages_, size_bytes);
    if (reservation.storage() == nullptr) return FontFaceLoad::failed(FontSetError::page_failure);
    std::memcpy(reservation.storage(), data, size_bytes);
    if (!(*pages_.seal)(reservation.storage(), size_bytes))
        return FontFaceLoad::failed(FontSetError::page_failure);
    std::shared_ptr<const SealedFontFace> face =
        std::make_shared<const SealedFontFace>(pages_, reservation.storage(), size_bytes);
    reservation.disown();
    FontFaceLoad result = FontFaceLoad::loaded(std::move(face));
    return result;
}

// Reads, checks and seals one face. Nothing is handed out unless every step succeeds; the
// pages are released on every failure.
FontFaceLoad BundledFontSet::load(std::size_t index) const {
    const BundledFaceRecord& record = manifest_[index];
    const std::filesystem::path path = directory_ / std::filesystem::path(record.file_name);
    std::error_code status;
    if (!std::filesystem::is_regular_file(path, status) || status)
        return FontFaceLoad::failed(FontSetError::unreadable);
    // The file must have the manifest's size, which the constructor bounded, before anything
    // is reserved for it.
    const std::uintmax_t file_bytes = std::filesystem::file_size(path, status);
    if (status) return FontFaceLoad::failed(FontSetError::unreadable);
    if (file_bytes != record.size_bytes) return FontFaceLoad::failed(FontSetError::size_mismatch);
    const std::size_t size_bytes = static_cast<std::size_t>(record.size_bytes);

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) return FontFaceLoad::failed(FontSetError::unreadable);
    PageReservation reservation(pages_, size_bytes);
    if (reservation.storage() == nullptr) return FontFaceLoad::failed(FontSetError::page_failure);
    // size_bytes is at most 64 MiB, so it fits std::streamsize.
    const std::streamsize wanted = static_cast<std::streamsize>(size_bytes);
    stream.read(reinterpret_cast<char*>(reservation.storage()), wanted);
    if (stream.gcount() != wanted) return FontFaceLoad::failed(FontSetError::unreadable);

    const std::span<const std::uint8_t> read(
        reinterpret_cast<const std::uint8_t*>(reservation.storage()), size_bytes);
    if (sha256(read) != digests_[index]) return FontFaceLoad::failed(FontSetError::digest_mismatch);
    if (!(*pages_.seal)(reservation.storage(), size_bytes))
        return FontFaceLoad::failed(FontSetError::page_failure);
    // The face adopts the pages only once it exists; if make_shared throws, the reservation
    // still releases them.
    std::shared_ptr<const SealedFontFace> face =
        std::make_shared<const SealedFontFace>(pages_, reservation.storage(), size_bytes);
    reservation.disown();
    FontFaceLoad result = FontFaceLoad::loaded(std::move(face));
    return result;
}

} // namespace gui_forms::detail

namespace gui_forms {

std::string_view bundled_font_source_name(BundledFontSource source) noexcept {
    switch (source) {
    case BundledFontSource::none: return "none";
    case BundledFontSource::environment: return "environment";
    case BundledFontSource::source_tree: return "source_tree";
    case BundledFontSource::embedded_pack: return "embedded_pack";
    }
    return "none";
}

BundledFontDiagnostics bundled_font_diagnostics() noexcept {
    BundledFontDiagnostics result{};
    result.lookups = detail::font_lookups.load(std::memory_order_relaxed);
    result.lookups_without_source = detail::font_lookups_without_source.load(std::memory_order_relaxed);
    result.last_source = static_cast<BundledFontSource>(detail::last_font_source.load(std::memory_order_relaxed));
    result.fallback_faces_found = detail::fallback_faces_found.load(std::memory_order_relaxed);
    result.fallback_faces_expected = detail::fallback_faces_expected.load(std::memory_order_relaxed);
    result.pack_refusals = detail::font_pack_refusals.load(std::memory_order_relaxed);
    return result;
}

std::string bundled_font_refusal() {
    const std::lock_guard<std::mutex> lock(detail::font_refusal_mutex());
    std::string copy = detail::font_refusal();
    return copy;
}

std::string bundled_font_notice() {
    const std::lock_guard<std::mutex> lock(detail::font_refusal_mutex());
    std::string copy = detail::font_notice();
    return copy;
}

std::string bundled_font_pack_refusal() {
    const std::lock_guard<std::mutex> lock(detail::font_refusal_mutex());
    std::string copy = detail::font_pack_refusal();
    return copy;
}

} // namespace gui_forms
