#include "bundled_font_set.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <ios>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
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
    {"NotoSansCJKjp-Regular.otf", std::nullopt, 400, false, false},
    {"NotoSansArabic-Regular.ttf", std::nullopt, 400, false, false},
    {"NotoSansHebrew-Regular.ttf", std::nullopt, 400, false, false},
    {"NotoSansDevanagari-Regular.ttf", std::nullopt, 400, false, false},
    {"NotoSansBengali-Regular.ttf", std::nullopt, 400, false, false},
    {"NotoSansGurmukhi-Regular.ttf", std::nullopt, 400, false, false},
    {"NotoEmoji-Regular.ttf", std::nullopt, 400, false, false},
};

#include "bundled_font_coverage.inc"

// The shared set and the directory it reads; see shared_bundled_font_set.
struct SharedFontSet final {
    std::filesystem::path directory{};
    std::weak_ptr<BundledFontSet> set{};
};
SharedFontSet shared_font_set{};

// The process's font lookups (bundled_font_diagnostics). The refusal text is guarded by
// font_refusal_mutex, as a host may read it from another thread.
std::atomic<std::uint64_t> font_lookups{0U};
std::atomic<std::uint64_t> font_lookups_without_source{0U};
std::atomic<std::uint8_t> last_font_source{static_cast<std::uint8_t>(BundledFontSource::none)};
std::mutex font_refusal_mutex;
std::string font_refusal{};

// Appends `directory` to `text` as UTF-8.
void append_directory(std::string& text, const std::filesystem::path& directory) {
    const std::u8string spelled = directory.u8string();
    text.append(reinterpret_cast<const char*>(spelled.data()), spelled.size());
}

// The refusal for a lookup over `candidates` that found nothing.
std::string refusal_text(std::span<const BundledFontCandidate> candidates) {
    std::string text =
        "GUI.Forms: no bundled font directory holds the required faces, so windows draw no text; tried";
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
    text.append(". Set GUI_FORMS_FONT_DIR, or place the fonts folder beside the GUI.Forms library.");
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

std::shared_ptr<BundledFontSet> shared_bundled_font_set(const std::filesystem::path& directory,
                                                        const SealedPageFunctions& pages) {
    std::shared_ptr<BundledFontSet> set = shared_font_set.set.lock();
    if (set && shared_font_set.directory == directory) return set;
    set = std::make_shared<BundledFontSet>(directory, pages);
    shared_font_set.directory = directory;
    shared_font_set.set = set;
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

BundledFontLocation locate_bundled_fonts(std::span<const BundledFontCandidate> candidates) {
    BundledFontLocation location{};
    for (const BundledFontCandidate& candidate : candidates) {
        if (!directory_holds_required_faces(candidate.directory)) continue;
        location.source = candidate.source;
        location.directory = candidate.directory;
        break;
    }
    font_lookups.fetch_add(1U, std::memory_order_relaxed);
    last_font_source.store(static_cast<std::uint8_t>(location.source), std::memory_order_relaxed);
    if (location.source != BundledFontSource::none) {
        const std::lock_guard<std::mutex> lock(font_refusal_mutex);
        font_refusal.clear();
        return location;
    }
    const std::string refusal = refusal_text(candidates);
    const std::uint64_t before = font_lookups_without_source.fetch_add(1U, std::memory_order_relaxed);
    if (before == 0U) std::fprintf(stderr, "%s\n", refusal.c_str());
    const std::lock_guard<std::mutex> lock(font_refusal_mutex);
    font_refusal = refusal;
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

FontFaceLoad BundledFontSet::face(std::string_view file_name) {
    for (std::size_t index = 0; index < manifest_.size(); ++index) {
        if (manifest_[index].file_name != file_name) continue;
        const std::lock_guard<std::mutex> lock(mutex_);
        std::optional<FontFaceLoad>& kept = results_[index];
        if (!kept) kept = load(index);
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
    case BundledFontSource::executable: return "executable";
    case BundledFontSource::library_module: return "library_module";
    case BundledFontSource::install_share: return "install_share";
    case BundledFontSource::application_bundle: return "application_bundle";
    }
    return "none";
}

BundledFontDiagnostics bundled_font_diagnostics() noexcept {
    BundledFontDiagnostics result{};
    result.lookups = detail::font_lookups.load(std::memory_order_relaxed);
    result.lookups_without_source = detail::font_lookups_without_source.load(std::memory_order_relaxed);
    result.last_source = static_cast<BundledFontSource>(detail::last_font_source.load(std::memory_order_relaxed));
    return result;
}

std::string bundled_font_refusal() {
    const std::lock_guard<std::mutex> lock(detail::font_refusal_mutex);
    std::string copy = detail::font_refusal;
    return copy;
}

} // namespace gui_forms
