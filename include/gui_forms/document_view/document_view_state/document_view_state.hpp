#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/document_view/types/document_view_types.hpp"

#include <array>

namespace gui_forms {

// Single UI-owner thread. No locks, workers, callbacks, I/O, edits or layout.
// Tokens are scoped to this instance: never route them to another model.
// Before destruction, cancel/drain dispatch, complete workers and release their
// payloads. Neither a producer nor deferred work may retain a model borrow.
class GUI_FORMS_API DocumentViewState final {
public:
    DocumentViewState() = default;
    DocumentViewState(const DocumentViewState&) = delete;
    DocumentViewState& operator=(const DocumentViewState&) = delete;
    DocumentViewState(DocumentViewState&&) = delete;
    DocumentViewState& operator=(DocumentViewState&&) = delete;

    [[nodiscard]] DocumentViewStatus bind(DocumentRevision revision,
                                          SourceByteOffset document_size) noexcept;
    [[nodiscard]] DocumentRequestResult request_page(DocumentViewport viewport,
                                                     SourceByteRange permitted) noexcept;
    void cancel() noexcept;
    // Acknowledge only after producer completion and release of all per-slot
    // payload/source/scratch storage. Cancellation alone does not release slots.
    [[nodiscard]] DocumentViewStatus finish(const DocumentPageRequest& request) noexcept;
    // Success consumes payload and slot. Failure preserves both payload and
    // slot: caller releases buffers, then finish acknowledges their release.
    // Release producer source/scratch before successful publication.
    [[nodiscard]] DocumentViewStatus publish(DocumentPage&& incoming) noexcept;
    // Borrow ends on publication, changed bind or destruction, never dispatch.
    [[nodiscard]] const std::optional<DocumentPage>& page() const noexcept { return page_; }
    [[nodiscard]] DocumentViewport viewport() const noexcept { return viewport_; }
    [[nodiscard]] std::size_t pending_count() const noexcept;
    // Exact scalar/token mapping only. Retain the supplied page token alongside
    // any deferred position. Grapheme legality is a later layout/edit contract.
    [[nodiscard]] SourceMappingResult source_position(
        const DocumentPageRequest& page_token, DisplayByteOffset position) const noexcept;
    [[nodiscard]] DisplayMappingResult display_position(
        const DocumentPageRequest& page_token, SourceByteOffset position) const noexcept;

private:
    [[nodiscard]] std::size_t find_slot(const DocumentPageRequest& request) const noexcept;
    DocumentRevision revision_{};
    SourceByteOffset document_size_{};
    DocumentViewport viewport_{};
    std::uint64_t serial_{};
    std::uint64_t authorized_serial_{};
    std::array<std::optional<DocumentPageRequest>, DocumentViewLimits::producer_slots> slots_{};
    std::optional<DocumentPage> page_{};
};

} // namespace gui_forms
