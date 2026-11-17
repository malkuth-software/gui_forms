#include "text_mask_native.hpp"
#include <stxlib/result.hpp>
#include <stxlib/text.hpp>
#include <stxlib/textlayout.hpp>
#include <stxlib/view.hpp>
#include <algorithm>

namespace gui_forms::detail {
namespace {
using namespace mask_native;

struct LineChoice final {
    std::size_t text_end{0};
    std::size_t consumed_end{0};
    bool overflow{false};
};

class NativeMaskJob final {
public:
    NativeMaskJob(const std::shared_ptr<MaskLedger>& ledger, const std::shared_ptr<const MaskKey>& key)
        : ledger_(ledger), key_(key), fonts_(*key), raster_work_(ledger),
          trial_(MaskAllocator<NativeGlyph>(ledger, MaskResource::shaping)),
          glyphs_(MaskAllocator<NativeGlyph>(ledger, MaskResource::shaping)),
          lines_(MaskAllocator<TextMaskLine>(ledger, MaskResource::workspace)),
          breaks_(MaskAllocator<stxlib::textlayout::LineBreak>(ledger, MaskResource::workspace)),
          cluster_edges_(MaskAllocator<std::uint8_t>(ledger, MaskResource::workspace)) {
        // Fixed bounded job storage is charged as workspace while the call is
        // live; runtime-sized arrays use measured allocator requests separately.
        fixed_workspace_.acquire(ledger, MaskResource::workspace, sizeof(NativeMaskJob));
        lines_.resize(256);
        const MaskKey& input = *key_;
        if (!input.text.empty()) text_ = std::string_view(input.text.data(), input.text.size());
        breaks_.resize(text_.size());
        cluster_edges_.resize(text_.size() + 1U);
        if (!text_.empty()) {
            // UAX #14 over the whole text as one piece (stxlib::textlayout;
            // libunibreak's "-strict" until 2026-11-16, whose CJ as NS is
            // the default here).
            const stxlib::OperationResult<stxlib::TextView> validated = stxlib::validate_utf8(text_.data(), text_.size());
            const stxlib::TextView* const text = validated.value();
            if (text == nullptr) throw NativeFailure(TextMaskStatus::invalid_input);
            const stxlib::MutableView<stxlib::textlayout::LineBreak> output(breaks_.data(), breaks_.size());
            const stxlib::OperationStatus found = stxlib::textlayout::find_line_breaks(output, *text);
            if (found.failed()) throw NativeFailure(TextMaskStatus::native_failure);
        }
    }

    [[nodiscard]] std::shared_ptr<const TextMaskStorage> execute() {
        std::size_t begin = 0;
        for (;;) {
            const std::size_t newline = text_.find('\n', begin);
            const bool hard_break = newline != std::string_view::npos;
            std::size_t end = text_.size();
            if (hard_break) end = newline;
            if (hard_break && end > begin && text_[end - 1U] == '\r') --end;
            const std::string_view content = text_.substr(begin, end - begin);
            prepare_paragraph(begin, content);
            if (hard_break) {
                lines_[line_count_ - 1U].consumed_end = static_cast<std::uint32_t>(newline + 1U);
                lines_[line_count_ - 1U].hard_break = true;
                begin = newline + 1U;
            } else break;
        }
        const MaskOptions& options = (*key_).options;
        const std::span<const NativeGlyph> glyphs(glyphs_.data(), glyph_count_);
        const NativeInk ink = measure_native_ink(fonts_, options, glyphs, raster_work_);
        TextMaskMetrics metrics{};
        metrics.size_64 = options.size_64;
        metrics.wrap_width_64 = options.width_64;
        metrics.additional_gap_64 = options.gap_64;
        metrics.device_scale = options.scale;
        metrics.logical_width = logical_width_;
        metrics.logical_height = logical_height_;
        metrics.horizontal_overflow = overflow_;
        if (ink.present) {
            metrics.ink_left_px = static_cast<std::int32_t>(ink.left);
            metrics.ink_top_px = static_cast<std::int32_t>(ink.top);
            metrics.width_px = static_cast<std::uint32_t>(ink.right - ink.left);
            metrics.height_px = static_cast<std::uint32_t>(ink.bottom - ink.top);
            metrics.stride_bytes = metrics.width_px;
        }
        std::shared_ptr<TextMaskStorage> candidate = allocate_mask_storage(ledger_, key_, metrics, line_count_);
        std::copy_n(lines_.begin(), line_count_, (*candidate).lines.begin());
        fill_native_mask(fonts_, options, glyphs, raster_work_, *candidate);
        return candidate;
    }
private:
    MaskCharge fixed_workspace_{};
    const std::shared_ptr<MaskLedger>& ledger_;
    const std::shared_ptr<const MaskKey>& key_;
    NativeFonts fonts_;
    NativeRasterWork raster_work_;
    NativeWork work_{};
    std::string_view text_{};
    NativeGlyphBuffer trial_;
    NativeGlyphBuffer glyphs_;
    std::vector<TextMaskLine, MaskAllocator<TextMaskLine>> lines_;
    std::vector<stxlib::textlayout::LineBreak, MaskAllocator<stxlib::textlayout::LineBreak>> breaks_;
    std::vector<std::uint8_t, MaskAllocator<std::uint8_t>> cluster_edges_;
    std::size_t glyph_count_{0};
    std::size_t line_count_{0};
    double logical_width_{0.0};
    double logical_height_{0.0};
    bool overflow_{false};

    [[nodiscard]] std::size_t trimmed_soft_end(const std::string_view text,
        const std::size_t begin, const std::size_t end) const noexcept {
        if (end == text.size()) return end;
        std::size_t trimmed = end;
        while (trimmed > begin && text[trimmed - 1U] == ' ') --trimmed;
        // A line made entirely of leading spaces retains its advance.
        if (trimmed == begin) return end;
        return trimmed;
    }
    [[nodiscard]] LineChoice choose_line(NativeParagraph& paragraph, const std::size_t origin,
        const std::string_view content, const std::size_t begin) {
        const double width = static_cast<double>((*key_).options.width_64) / 64.0;
        const std::span<const std::size_t> edges = paragraph.edges();
        LineChoice best{};
        std::size_t first_overflow = content.size();
        for (std::size_t index = 1; index < edges.size(); ++index) {
            const std::size_t end = edges[index];
            if (end <= begin || cluster_edges_[origin + end] == 0) continue;
            const stxlib::textlayout::LineBreak decision = breaks_[origin + end - 1U];
            const bool may_end = decision == stxlib::textlayout::LineBreak::allowed ||
                                 decision == stxlib::textlayout::LineBreak::mandatory;
            if (end != content.size() && !may_end) continue;
            const std::size_t visible_end = trimmed_soft_end(content, begin, end);
            const NativeShape shape = paragraph.shape(begin, visible_end, trial_);
            if (shape.advance > width) { first_overflow = end; break; }
            best = {visible_end, end, false};
        }
        if (best.consumed_end != 0) return best;
        // No legal word break fits: use only whole EGC + shaped cluster units.
        // The first indivisible overwide unit is kept intact and flagged.
        for (std::size_t index = 1; index < edges.size(); ++index) {
            const std::size_t end = edges[index];
            if (end <= begin || cluster_edges_[origin + end] == 0) continue;
            if (end > first_overflow) break;
            const NativeShape shape = paragraph.shape(begin, end, trial_);
            if (shape.advance > width) {
                if (best.consumed_end == 0) best = {end, end, true};
                break;
            }
            best = {end, end, false};
        }
        if (best.consumed_end == 0) throw NativeFailure(TextMaskStatus::native_failure);
        return best;
    }
    void append_line(NativeParagraph& paragraph, const std::size_t origin,
        const std::size_t begin, const LineChoice choice) {
        if (line_count_ == lines_.size()) throw MaskLimitFailure(TextMaskLimit::line_count);
        const NativeShape shape = paragraph.shape(begin, choice.text_end, trial_);
        if (shape.glyph_count > 65536U - glyph_count_) throw MaskLimitFailure(TextMaskLimit::shaping_payload);
        resize_native_glyphs(glyphs_, glyph_count_ + shape.glyph_count);
        if (line_count_ != 0) logical_height_ += static_cast<double>((*key_).options.gap_64) / 64.0;
        const double baseline = logical_height_ + shape.ascent;
        TextMaskLine& line = lines_[line_count_];
        line.source_begin = static_cast<std::uint32_t>(origin + begin);
        line.text_end = static_cast<std::uint32_t>(origin + choice.text_end);
        line.consumed_end = static_cast<std::uint32_t>(origin + choice.consumed_end);
        line.baseline = baseline;
        line.advance = shape.advance;
        line.ascent = shape.ascent;
        line.descent = shape.descent;
        line.horizontal_overflow = choice.overflow;
        for (std::size_t index = 0; index < shape.glyph_count; ++index) {
            NativeGlyph glyph = trial_[index];
            glyph.y += baseline;
            glyph.cluster += static_cast<std::uint32_t>(origin);
            glyphs_[glyph_count_ + index] = glyph;
        }
        glyph_count_ += shape.glyph_count;
        ++line_count_;
        logical_height_ += shape.ascent + shape.descent;
        logical_width_ = std::max(logical_width_, shape.advance);
        overflow_ = overflow_ || choice.overflow;
    }
    void prepare_paragraph(const std::size_t origin, const std::string_view content) {
        NativeParagraph paragraph(ledger_, fonts_, work_, content);
        if (content.empty()) { append_line(paragraph, origin, 0, {}); return; }
        if ((*key_).options.width_64 == 0) {
            append_line(paragraph, origin, 0, {content.size(), content.size(), false});
            return;
        }
        const NativeShape complete = paragraph.shape(0, content.size(), trial_);
        cluster_edges_[origin] = 1;
        cluster_edges_[origin + content.size()] = 1;
        for (std::size_t index = 0; index < complete.glyph_count; ++index) {
            cluster_edges_[origin + trial_[index].cluster] = 1;
        }
        std::size_t begin = 0;
        while (begin < content.size()) {
            const LineChoice choice = choose_line(paragraph, origin, content, begin);
            append_line(paragraph, origin, begin, choice);
            begin = choice.consumed_end;
        }
    }
};

class NativeMaskBackend final : public MaskBackend {
public:
    TextMaskResult execute(const std::shared_ptr<MaskLedger>& ledger,
        const std::shared_ptr<const MaskKey>& key, std::shared_ptr<const TextMaskStorage>& output) override {
        try {
            NativeMaskJob job(ledger, key);
            std::shared_ptr<const TextMaskStorage> candidate = job.execute();
            output = std::move(candidate);
            return {TextMaskStatus::success};
        } catch (const NativeFailure& failure) { return {failure.status}; }
    }
};
}
MaskBackend& native_mask_backend() noexcept {
    // Never destroyed (rule 1: process termination releases none of our state).
    static NativeMaskBackend* const backend = new NativeMaskBackend{};
    return *backend;
}
} // namespace gui_forms::detail
