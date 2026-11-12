#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace gui_forms {

struct GUI_FORMS_API SourceByteOffset final {
    std::uint64_t value{};
    explicit constexpr SourceByteOffset(std::uint64_t offset = 0) noexcept : value(offset) {}
};
struct GUI_FORMS_API DisplayByteOffset final {
    std::uint32_t value{};
    explicit constexpr DisplayByteOffset(std::uint32_t offset = 0) noexcept : value(offset) {}
};
struct GUI_FORMS_API DocumentRevision final {
    std::uint64_t document{};
    std::uint64_t revision{};
};
struct GUI_FORMS_API SourceByteRange final {
    SourceByteOffset begin{};
    SourceByteOffset end{};
};
struct GUI_FORMS_API DocumentViewport final {
    SourceByteOffset anchor{};
    double horizontal_dip{};
};
struct GUI_FORMS_API DocumentPageRequest final {
    DocumentRevision revision{};
    std::uint64_t serial{};
    SourceByteRange permitted{};
    DocumentViewport viewport{};
};
enum class DocumentMapKind : std::uint8_t { identity_utf8, atomic_token };
struct GUI_FORMS_API DocumentMapSpan final {
    SourceByteRange source{};
    DisplayByteOffset begin{};
    DisplayByteOffset end{};
    DocumentMapKind kind{DocumentMapKind::identity_utf8};
};
enum class DocumentViewStatus : std::uint8_t {
    success, invalid_revision, invalid_range, invalid_page, budget_exceeded,
    stale, cancelled, busy, unavailable, context_required, serial_exhausted,
    invalid_boundary,
};
struct GUI_FORMS_API DocumentViewLimits final {
    static constexpr std::uint64_t source_bytes = 65'536;
    static constexpr std::size_t display_capacity = 1'048'576;
    static constexpr std::size_t mapping_capacity = 65'536;
    static constexpr std::size_t producer_slots = 2;
};

// Unique payload ownership; moves empty the former owner, including metadata.
// Producers check all capacity budgets before allocation; no source bytes are
// retained here. Consumer indexing/shaping workspaces have separate budgets.
struct GUI_FORMS_API DocumentPage final {
    DocumentPageRequest request{};
    SourceByteRange covered{};
    std::string display_utf8{};
    std::vector<DocumentMapSpan> mapping{};

    DocumentPage() = default;
    DocumentPage(const DocumentPage&) = delete;
    DocumentPage& operator=(const DocumentPage&) = delete;
    DocumentPage(DocumentPage&& other) noexcept;
    DocumentPage& operator=(DocumentPage&& other) noexcept;
    void swap(DocumentPage& other) noexcept;
};

// A page request's result: the request the document side fulfils, or why
// there is none. One alternative per state (house form for results).
class GUI_FORMS_API DocumentRequestResult final {
public:
    [[nodiscard]] static DocumentRequestResult requested(DocumentPageRequest request) noexcept {
        DocumentRequestResult result{Requested{std::move(request)}};
        return result;
    }
    // A refusal. `success` refuses nothing, so it is read as `unavailable`.
    [[nodiscard]] static DocumentRequestResult refused(DocumentViewStatus status) noexcept {
        const DocumentViewStatus reason = status == DocumentViewStatus::success ? DocumentViewStatus::unavailable
                                                                                : status;
        DocumentRequestResult result{Refused{reason}};
        return result;
    }

    [[nodiscard]] DocumentViewStatus status() const noexcept {
        const Refused* const refusal = std::get_if<Refused>(&state_);
        const DocumentViewStatus status = refusal != nullptr ? (*refusal).status : DocumentViewStatus::success;
        return status;
    }
    // The request, for `success`; null otherwise.
    [[nodiscard]] const DocumentPageRequest* request() const noexcept {
        const Requested* const requested = std::get_if<Requested>(&state_);
        const DocumentPageRequest* const found = requested != nullptr ? &(*requested).request : nullptr;
        return found;
    }

private:
    struct Requested final {
        DocumentPageRequest request{};
    };
    struct Refused final {
        DocumentViewStatus status{DocumentViewStatus::unavailable};
    };
    // A throwing move would leave the variant valueless, a state no query
    // describes.
    static_assert(std::is_nothrow_move_constructible_v<Requested>);

    explicit DocumentRequestResult(Requested requested) noexcept : state_(std::move(requested)) {}
    explicit DocumentRequestResult(Refused refused) noexcept : state_(refused) {}

    std::variant<Requested, Refused> state_;
};
// A display position mapped to the source.
// One alternative per state (house form for results).
class GUI_FORMS_API SourceMappingResult final {
public:
    [[nodiscard]] static SourceMappingResult mapped(SourceByteOffset position) noexcept {
        SourceMappingResult result{Mapped{position}};
        return result;
    }
    // A refusal. `success` refuses nothing, so it is read as `unavailable`.
    [[nodiscard]] static SourceMappingResult refused(DocumentViewStatus status) noexcept {
        const DocumentViewStatus reason = status == DocumentViewStatus::success ? DocumentViewStatus::unavailable
                                                                                : status;
        SourceMappingResult result{Refused{reason}};
        return result;
    }

    [[nodiscard]] DocumentViewStatus status() const noexcept {
        const Refused* const refusal = std::get_if<Refused>(&state_);
        const DocumentViewStatus status = refusal != nullptr ? (*refusal).status : DocumentViewStatus::success;
        return status;
    }
    // The mapped position, for `success`; null otherwise.
    [[nodiscard]] const SourceByteOffset* position() const noexcept {
        const Mapped* const mapped = std::get_if<Mapped>(&state_);
        const SourceByteOffset* const found = mapped != nullptr ? &(*mapped).position : nullptr;
        return found;
    }

private:
    struct Mapped final {
        SourceByteOffset position{0};
    };
    struct Refused final {
        DocumentViewStatus status{DocumentViewStatus::unavailable};
    };

    explicit SourceMappingResult(Mapped mapped) noexcept : state_(mapped) {}
    explicit SourceMappingResult(Refused refused) noexcept : state_(refused) {}

    std::variant<Mapped, Refused> state_;
};
// A source position mapped to the display.
// One alternative per state (house form for results).
class GUI_FORMS_API DisplayMappingResult final {
public:
    [[nodiscard]] static DisplayMappingResult mapped(DisplayByteOffset position) noexcept {
        DisplayMappingResult result{Mapped{position}};
        return result;
    }
    // A refusal. `success` refuses nothing, so it is read as `unavailable`.
    [[nodiscard]] static DisplayMappingResult refused(DocumentViewStatus status) noexcept {
        const DocumentViewStatus reason = status == DocumentViewStatus::success ? DocumentViewStatus::unavailable
                                                                                : status;
        DisplayMappingResult result{Refused{reason}};
        return result;
    }

    [[nodiscard]] DocumentViewStatus status() const noexcept {
        const Refused* const refusal = std::get_if<Refused>(&state_);
        const DocumentViewStatus status = refusal != nullptr ? (*refusal).status : DocumentViewStatus::success;
        return status;
    }
    // The mapped position, for `success`; null otherwise.
    [[nodiscard]] const DisplayByteOffset* position() const noexcept {
        const Mapped* const mapped = std::get_if<Mapped>(&state_);
        const DisplayByteOffset* const found = mapped != nullptr ? &(*mapped).position : nullptr;
        return found;
    }

private:
    struct Mapped final {
        DisplayByteOffset position{0};
    };
    struct Refused final {
        DocumentViewStatus status{DocumentViewStatus::unavailable};
    };

    explicit DisplayMappingResult(Mapped mapped) noexcept : state_(mapped) {}
    explicit DisplayMappingResult(Refused refused) noexcept : state_(refused) {}

    std::variant<Mapped, Refused> state_;
};

} // namespace gui_forms
