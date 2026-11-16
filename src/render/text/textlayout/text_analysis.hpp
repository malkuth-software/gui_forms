#pragma once

// One paragraph's bidi analysis for the bundled text engine, over
// stxlib::textlayout (UAX #9 at Unicode 18.0.0; SheenBidi 2.9.0 until
// 2026-11-16). The engine reorders each paragraph of a text as one line: it
// analyses the paragraph, then the line that is the whole paragraph, and
// reads that line's runs in visual order.
//
// Memory. A TextAnalysis owns one Paragraph and one Line, which keep their
// storage grown to the largest paragraph and line they have held and reuse
// it; nothing is allocated per character. They allocate on STXlib's own
// allocator, so their bytes are counted here, in bytes, under
// AllocationSubsystem::text_layout: account() counts the change of their
// bytes_held since the last count (a growth as one allocation of the
// difference, a shrink as one release), and release() gives the storage
// back and counts its release. Single-threaded: one analysis at a time, on
// one thread.

#include "gui_forms/allocation/statistics/allocation_statistics.hpp"

#include <stxlib/result.hpp>
#include <stxlib/text_view.hpp>
#include <stxlib/textlayout.hpp>

#include <cstddef>
#include <new>
#include <stdexcept>

namespace gui_forms::render::text {

class TextAnalysis final {
public:
    TextAnalysis() noexcept = default;
    ~TextAnalysis() { release(); }
    TextAnalysis(const TextAnalysis&) = delete;
    TextAnalysis& operator=(const TextAnalysis&) = delete;
    TextAnalysis(TextAnalysis&&) = delete;
    TextAnalysis& operator=(TextAnalysis&&) = delete;

    // The last paragraph analysed and its line. Read them between
    // analyse_paragraph_line and the next analysis.
    stxlib::textlayout::Paragraph paragraph{};
    stxlib::textlayout::Line line{};

    // What the two hold now, in bytes.
    [[nodiscard]] std::size_t bytes_held() const noexcept {
        const std::size_t held = static_cast<std::size_t>(paragraph.bytes_held()) +
                                 static_cast<std::size_t>(line.bytes_held());
        return held;
    }

    // Counts the change of bytes_held since the last count.
    void account() noexcept {
        const std::size_t held = bytes_held();
        if (held > counted_) detail::count_allocation(AllocationSubsystem::text_layout, held - counted_);
        if (held < counted_) detail::count_release(AllocationSubsystem::text_layout, counted_ - held);
        counted_ = held;
    }

    // Frees both, as new ones, and counts the release.
    void release() noexcept {
        paragraph.release();
        line.release();
        account();
    }

private:
    std::size_t counted_{};
};

// Analyses the paragraph of `text` that starts at byte `offset` (the
// automatic base level, left to right when the paragraph has no strong
// character), then its whole length as one
// line, and counts the storage. The visual runs are analysis.line.runs(),
// their offsets bytes of the whole text. Returns the paragraph's length,
// which is never zero; the next paragraph starts at offset plus it.
// Requirement: offset < text.count() and at a code point's first byte.
// Throws std::bad_alloc when the storage cannot grow (the analysis is then
// empty) and std::runtime_error for any other refusal.
[[nodiscard]] inline std::size_t analyse_paragraph_line(const stxlib::TextView text, const std::size_t offset,
                                                        TextAnalysis& analysis) {
    const stxlib::OperationStatus analysed = stxlib::textlayout::analyse_paragraph(
        text, offset, stxlib::textlayout::BaseDirection::automatic_left_to_right, analysis.paragraph);
    if (analysed.failed()) {
        analysis.account();
        if ((*analysed.error()).kind() == stxlib::ErrorKind::out_of_memory) throw std::bad_alloc();
        throw std::runtime_error("Bidi paragraph analysis refused");
    }
    const std::size_t length = static_cast<std::size_t>(analysis.paragraph.length());
    const stxlib::OperationStatus lined = stxlib::textlayout::analyse_line(analysis.paragraph, offset, length, analysis.line);
    analysis.account();
    if (lined.failed()) {
        if ((*lined.error()).kind() == stxlib::ErrorKind::out_of_memory) throw std::bad_alloc();
        throw std::runtime_error("Bidi line analysis refused");
    }
    return length;
}

} // namespace gui_forms::render::text
