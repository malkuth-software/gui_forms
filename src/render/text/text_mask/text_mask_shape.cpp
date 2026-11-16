#include "text_mask_native.hpp"
#include <stxlib/result.hpp>
#include <stxlib/text.hpp>
#include <algorithm>
#include <cmath>
#include <new>

namespace gui_forms::detail::mask_native {
void resize_native_glyphs(NativeGlyphBuffer& glyphs, const std::size_t count) {
    constexpr std::size_t maximum_glyphs = 65536;
    if (count > maximum_glyphs) throw MaskLimitFailure(TextMaskLimit::shaping_payload);
    if (count > glyphs.capacity()) {
        std::size_t capacity = count;
        if (glyphs.capacity() != 0) {
            const std::size_t grown = glyphs.capacity() + std::min(glyphs.capacity(), maximum_glyphs - glyphs.capacity());
            capacity = std::max(count, grown);
        }
        glyphs.reserve(capacity);
        if (glyphs.capacity() > maximum_glyphs) throw MaskLimitFailure(TextMaskLimit::shaping_payload);
    }
    glyphs.resize(count);
}
namespace {
bool same_cluster_run(const NativeCluster& left, const NativeCluster& right) noexcept {
    const bool equal = left.face == right.face && left.script == right.script;
    return equal;
}
std::size_t cluster_at(const std::span<const NativeCluster> clusters, const std::size_t offset) noexcept {
    std::size_t first = 0;
    std::size_t last = clusters.size();
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2U;
        if (clusters[middle].end <= offset) first = middle + 1U;
        else last = middle;
    }
    return first;
}
}

NativeParagraph::NativeParagraph(const std::shared_ptr<MaskLedger>& ledger, NativeFonts& fonts,
    NativeWork& work, const std::string_view text)
    : fonts_(fonts), work_(work), text_(text),
      edges_(MaskAllocator<std::size_t>(ledger, MaskResource::workspace)),
      clusters_(MaskAllocator<NativeCluster>(ledger, MaskResource::workspace)), ledger_(ledger) {
    const GraphemeStorageRequirement requirement = grapheme_storage_requirement(text);
    if (requirement.status != GraphemeStorageStatus::success) throw NativeFailure(TextMaskStatus::invalid_input);
    {
        // Existing bounded grapheme code exposes its exact controlled peak.
        // Keep that peak reserved while its temporary arrays and returned owner
        // exist, including simultaneous copying into this counted workspace.
        MaskCharge grapheme_reservation{};
        grapheme_reservation.reserve(ledger, MaskResource::workspace, requirement.peak_bytes);
        GraphemeBoundaryBuffer boundaries{};
        const GraphemeStorageStatus status = bounded_grapheme_boundaries(text, requirement.peak_bytes, boundaries);
        if (status == GraphemeStorageStatus::resource_failure) throw std::bad_alloc{};
        if (status != GraphemeStorageStatus::success) throw MaskLimitFailure(TextMaskLimit::workspace);
        const std::span<const std::size_t> source = boundaries.boundaries();
        edges_.resize(source.size());
        std::copy(source.begin(), source.end(), edges_.begin());
    }
    if (text.empty()) return;
    clusters_.resize(edges_.size() - 1U);
    const stxlib::Result<stxlib::TextView> validated = stxlib::validate_utf8(text.data(), text.size());
    const stxlib::TextView* const view = validated.value();
    if (view == nullptr) throw NativeFailure(TextMaskStatus::invalid_input);
    try {
        // One analysis gives the levels and the script runs. The content
        // holds no line feed; another paragraph separator inside it ends
        // the paragraph early, which this path does not support.
        const stxlib::Status analysed = stxlib::textlayout::analyse_paragraph(
            *view, 0U, stxlib::textlayout::BaseDirection::automatic_left_to_right, paragraph_);
        charge_analysis();
        if (analysed.failed()) {
            if ((*analysed.error()).kind() == stxlib::ErrorKind::out_of_memory) throw std::bad_alloc{};
            throw NativeFailure(TextMaskStatus::native_failure);
        }
        if (static_cast<std::size_t>(paragraph_.length()) != text.size()) {
            throw NativeFailure(TextMaskStatus::unsupported_profile);
        }
        buffer_ = hb_buffer_create();
        if (buffer_ == nullptr || buffer_ == hb_buffer_get_empty() || !hb_buffer_allocation_successful(buffer_)) throw std::bad_alloc{};
        // A cluster takes the script of the run holding its first byte; the
        // runs cover the paragraph in order.
        const stxlib::View<stxlib::textlayout::ScriptRun> scripts = paragraph_.script_runs();
        const std::size_t script_count = scripts.count();
        std::size_t script_index = 0;
        for (std::size_t index = 0; index < clusters_.size(); ++index) {
            NativeCluster& cluster = clusters_[index];
            cluster.begin = edges_[index];
            cluster.end = edges_[index + 1U];
            while (script_index < script_count &&
                   cluster.begin >= static_cast<std::size_t>(scripts[script_index].offset) +
                                        static_cast<std::size_t>(scripts[script_index].length)) {
                ++script_index;
            }
            if (script_index == script_count || static_cast<std::size_t>(scripts[script_index].offset) > cluster.begin) {
                throw NativeFailure(TextMaskStatus::native_failure);
            }
            const std::uint32_t tag = stxlib::textlayout::script_tag(scripts[script_index].script);
            cluster.script = hb_script_from_iso15924_tag(tag);
            const std::string_view characters = text.substr(cluster.begin, cluster.end - cluster.begin);
            cluster.face = fonts.select_face(characters);
        }
    } catch (...) { release(); throw; }
}
void NativeParagraph::release() noexcept {
    if (buffer_ != nullptr) hb_buffer_destroy(buffer_);
    buffer_ = nullptr;
    paragraph_.release();
    line_.release();
    analysis_charge_.reset();
    charged_bytes_ = 0;
}
void NativeParagraph::charge_analysis() {
    const std::size_t held =
        static_cast<std::size_t>(paragraph_.bytes_held()) + static_cast<std::size_t>(line_.bytes_held());
    if (held == charged_bytes_) return;
    analysis_charge_.reset();
    charged_bytes_ = 0;
    analysis_charge_.acquire(ledger_, MaskResource::workspace, held);
    charged_bytes_ = held;
}
NativeParagraph::~NativeParagraph() { release(); }
std::span<const std::size_t> NativeParagraph::edges() const noexcept {
    const std::span<const std::size_t> result(edges_.data(), edges_.size());
    return result;
}

void NativeParagraph::append_run(const std::size_t line_begin, const std::size_t line_end,
    const std::size_t run_begin, const std::size_t run_end, const NativeCluster& cluster,
    const bool rtl, NativeGlyphBuffer& output, NativeShape& shape) {
    const std::string_view line = text_.substr(line_begin, line_end - line_begin);
    work_.admit(line.size());
    hb_buffer_reset(buffer_);
    hb_buffer_set_cluster_level(buffer_, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
    unsigned flags = HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES;
    if (run_begin == line_begin) flags |= HB_BUFFER_FLAG_BOT;
    if (run_end == line_end) flags |= HB_BUFFER_FLAG_EOT;
    hb_buffer_set_flags(buffer_, static_cast<hb_buffer_flags_t>(flags));
    hb_buffer_add_utf8(buffer_, line.data(), static_cast<int>(line.size()),
        static_cast<unsigned>(run_begin - line_begin), static_cast<int>(run_end - run_begin));
    hb_buffer_set_direction(buffer_, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer_, cluster.script);
    hb_buffer_set_language(buffer_, hb_language_from_string("und", -1));
    hb_buffer_guess_segment_properties(buffer_);
    if (!hb_buffer_allocation_successful(buffer_)) throw std::bad_alloc{};
    NativeFace& face = fonts_.faces[cluster.face];
    const hb_bool_t shaped = hb_shape_full(face.shaping, buffer_, nullptr, 0, nullptr);
    if (!hb_buffer_allocation_successful(buffer_)) throw std::bad_alloc{};
    if (!shaped) throw NativeFailure(TextMaskStatus::native_failure);
    unsigned count = 0;
    unsigned position_count = 0;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer_, &count);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer_, &position_count);
    if (count != position_count || (count != 0 && (infos == nullptr || positions == nullptr))) {
        throw NativeFailure(TextMaskStatus::native_failure);
    }
    if (count > 65536U - shape.glyph_count) throw MaskLimitFailure(TextMaskLimit::shaping_payload);
    resize_native_glyphs(output, shape.glyph_count + count);
    double pen = shape.advance;
    for (unsigned index = 0; index < count; ++index) {
        const hb_glyph_info_t& info = infos[index];
        const hb_glyph_position_t& position = positions[index];
        if (info.codepoint == 0) throw NativeFailure(TextMaskStatus::missing_font_coverage);
        if (info.codepoint >= static_cast<std::uint64_t>((*face.value).num_glyphs) ||
            info.cluster < run_begin - line_begin || info.cluster >= run_end - line_begin) {
            throw NativeFailure(TextMaskStatus::native_failure);
        }
        NativeGlyph& glyph = output[shape.glyph_count];
        glyph.face = cluster.face;
        glyph.glyph = info.codepoint;
        glyph.cluster = static_cast<std::uint32_t>(line_begin + info.cluster);
        glyph.x = pen + static_cast<double>(position.x_offset) / 64.0;
        glyph.y = -static_cast<double>(position.y_offset) / 64.0;
        glyph.advance = static_cast<double>(position.x_advance) / 64.0;
        if (position.y_advance != 0 || glyph.advance < 0.0) throw NativeFailure(TextMaskStatus::unsupported_profile);
        pen += glyph.advance;
        ++shape.glyph_count;
    }
    shape.advance = pen;
    shape.ascent = std::max(shape.ascent, face.ascent);
    shape.descent = std::max(shape.descent, face.descent);
}

NativeShape NativeParagraph::shape(const std::size_t begin, const std::size_t end,
    NativeGlyphBuffer& output) {
    if (begin > end || end > text_.size()) throw NativeFailure(TextMaskStatus::invalid_input);
    NativeShape result{};
    output.clear();
    result.ascent = fonts_.faces[fonts_.primary].ascent;
    result.descent = fonts_.faces[fonts_.primary].descent;
    if (begin == end) return result;
    // The line's levels after L1 and its runs in visual order; the line
    // analysis is reused from trial to trial.
    const stxlib::Status lined = stxlib::textlayout::analyse_line(paragraph_, begin, end - begin, line_);
    charge_analysis();
    if (lined.failed()) {
        if ((*lined.error()).kind() == stxlib::ErrorKind::out_of_memory) throw std::bad_alloc{};
        throw NativeFailure(TextMaskStatus::native_failure);
    }
    const stxlib::View<stxlib::textlayout::BidiRun> runs = line_.runs();
    const std::size_t count = runs.count();
    const std::span<const NativeCluster> clusters(clusters_.data(), clusters_.size());
    for (std::size_t index = 0; index < count; ++index) {
        const stxlib::textlayout::BidiRun& run = runs[index];
        const std::size_t run_offset = run.offset;
        const std::size_t run_length = run.length;
        if (run_offset < begin || run_offset >= end || run_length == 0 || run_length > end - run_offset) {
            throw NativeFailure(TextMaskStatus::native_failure);
        }
        const std::size_t run_end = run_offset + run_length;
        const bool rtl = (run.level & 1U) != 0;
        if (!rtl) {
            std::size_t first = cluster_at(clusters, run_offset);
            while (first < clusters.size() && clusters[first].begin < run_end) {
                std::size_t last = first + 1U;
                while (last < clusters.size() && clusters[last].begin < run_end && same_cluster_run(clusters[first], clusters[last])) ++last;
                const std::size_t segment_begin = std::max(run_offset, clusters[first].begin);
                const std::size_t segment_end = std::min(run_end, clusters[last - 1U].end);
                append_run(begin, end, segment_begin, segment_end, clusters[first], false, output, result);
                first = last;
            }
        } else {
            std::size_t last = cluster_at(clusters, run_end - 1U) + 1U;
            while (last != 0 && clusters[last - 1U].end > run_offset) {
                std::size_t first = last - 1U;
                while (first != 0 && clusters[first - 1U].end > run_offset && same_cluster_run(clusters[last - 1U], clusters[first - 1U])) --first;
                const std::size_t segment_begin = std::max(run_offset, clusters[first].begin);
                const std::size_t segment_end = std::min(run_end, clusters[last - 1U].end);
                append_run(begin, end, segment_begin, segment_end, clusters[first], true, output, result);
                last = first;
            }
        }
    }
    if (!std::isfinite(result.advance)) throw NativeFailure(TextMaskStatus::native_failure);
    return result;
}
} // namespace gui_forms::detail::mask_native
