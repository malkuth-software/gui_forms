#include "text_mask_state.hpp"
#include <cmath>
#include <cstring>
#include <stxlib/threads.hpp>

namespace gui_forms {
bool TextMaskLease::has_value() const noexcept { const bool valid = bool(storage_); return valid; }
std::span<const std::uint8_t> TextMaskLease::coverage() const noexcept {
    if (!storage_) return {};
    const std::span<const std::uint8_t> result((*storage_).pixels.data(), (*storage_).pixels.size());
    return result;
}
std::span<const TextMaskLine> TextMaskLease::lines() const noexcept {
    if (!storage_) return {};
    const std::span<const TextMaskLine> result((*storage_).lines.data(), (*storage_).lines.size());
    return result;
}
std::string_view TextMaskLease::source_utf8() const noexcept {
    if (!storage_) return {};
    const detail::MaskKey& key = *(*storage_).key;
    if (key.text.empty()) return {};
    const std::string_view result(key.text.data(), key.text.size());
    return result;
}
TextMaskMetrics TextMaskLease::metrics() const noexcept {
    if (!storage_) return {};
    return (*storage_).metrics;
}
void TextMaskLease::reset() noexcept { storage_.reset(); }
}

namespace gui_forms::detail {
namespace {
std::atomic<std::uint64_t> mask_identity{1};

bool valid_mask_utf8(const std::string_view text) noexcept {
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        std::size_t length = 1;
        std::uint32_t scalar = first;
        std::uint32_t minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) { length = 2; scalar = first & 0x1FU; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { length = 3; scalar = first & 0x0FU; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { length = 4; scalar = first & 0x07U; minimum = 0x10000; }
        else if (first >= 0x80) return false;
        if (length > text.size() - index) return false;
        for (std::size_t tail = 1; tail < length; ++tail) {
            const unsigned char continuation = static_cast<unsigned char>(text[index + tail]);
            if ((continuation & 0xC0U) != 0x80U) return false;
            scalar = (scalar << 6U) | (continuation & 0x3FU);
        }
        if (scalar < minimum || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) return false;
        index += length;
    }
    return true;
}
}

void require_mask_executor(const stxlib::ThreadIdentity executor) {
    if (executor != stxlib::current_thread()) throw std::logic_error("Text mask control on wrong executor");
}
std::uint64_t next_mask_identity() {
    std::uint64_t current = mask_identity.load(std::memory_order_relaxed);
    for (;;) {
        if (current == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Text mask identity exhausted");
        const bool acquired = mask_identity.compare_exchange_weak(current, current + 1U, std::memory_order_relaxed);
        if (acquired) return current;
    }
}
TextMaskResult normalize_mask_request(const TextMaskRequest& request, MaskOptions& output) noexcept {
    if (request.utf8.size() > 16384) return {TextMaskStatus::limit_exceeded, TextMaskLimit::input_bytes};
    if (!std::isfinite(request.size) || request.size < 4.0 || request.size > 128.0 ||
        !std::isfinite(request.wrap_width) || request.wrap_width < 0.0 || request.wrap_width > 8192.0 ||
        !std::isfinite(request.device_scale) || request.device_scale < 0.5 || request.device_scale > 4.0) {
        return {TextMaskStatus::invalid_input};
    }
    if (request.raster != TextMaskRaster::outline_gray && request.raster != TextMaskRaster::true_mono) {
        return {TextMaskStatus::unsupported_profile};
    }
    if (request.raster == TextMaskRaster::true_mono && std::floor(request.device_scale) != request.device_scale) {
        return {TextMaskStatus::unsupported_profile};
    }
    if (!valid_mask_utf8(request.utf8)) return {TextMaskStatus::invalid_input};
    std::size_t lines = 1;
    for (std::size_t index = 0; index < request.utf8.size(); ++index) {
        const unsigned char value = static_cast<unsigned char>(request.utf8[index]);
        if (value == '\n') ++lines;
        else if (value == '\r') {
            if (index + 1U == request.utf8.size() || request.utf8[index + 1U] != '\n') return {TextMaskStatus::unsupported_profile};
        } else if (value < 0x20 || value == 0x7F) return {TextMaskStatus::unsupported_profile};
        if (value == 0xC2 && index + 1U < request.utf8.size() && static_cast<unsigned char>(request.utf8[index + 1U]) == 0x85) {
            return {TextMaskStatus::unsupported_profile};
        }
        if (value == 0xE2 && index + 2U < request.utf8.size() &&
            static_cast<unsigned char>(request.utf8[index + 1U]) == 0x80 &&
            (static_cast<unsigned char>(request.utf8[index + 2U]) == 0xA8 || static_cast<unsigned char>(request.utf8[index + 2U]) == 0xA9)) {
            return {TextMaskStatus::unsupported_profile};
        }
    }
    if (lines > 256) return {TextMaskStatus::limit_exceeded, TextMaskLimit::line_count};
    MaskOptions normalized{};
    normalized.primary_face = request.primary_face;
    normalized.size_64 = static_cast<std::int32_t>(std::floor(request.size * 64.0 + 0.5));
    normalized.width_64 = static_cast<std::int32_t>(std::floor(request.wrap_width * 64.0 + 0.5));
    normalized.gap_64 = static_cast<std::int32_t>(std::floor(static_cast<double>(normalized.size_64) * 0.05 + 0.5));
    normalized.scale = request.device_scale;
    normalized.raster = request.raster;
    if (request.wrap_width > 0.0 && normalized.width_64 == 0) return {TextMaskStatus::invalid_input};
    output = normalized;
    return {TextMaskStatus::success};
}

std::shared_ptr<TextMaskStorage> allocate_mask_storage(const std::shared_ptr<MaskLedger>& ledger,
    const std::shared_ptr<const MaskKey>& key, const TextMaskMetrics& metrics, const std::size_t line_count) {
    if (line_count == 0 || line_count > 256) throw MaskLimitFailure(TextMaskLimit::line_count);
    if (metrics.width_px > 4096 || metrics.height_px > 4096 || metrics.stride_bytes < metrics.width_px) {
        throw MaskLimitFailure(TextMaskLimit::mask_dimension);
    }
    if (metrics.height_px != 0 && metrics.stride_bytes > (4U * 1024U * 1024U) / metrics.height_px) {
        throw MaskLimitFailure(TextMaskLimit::mask_bytes);
    }
    const std::size_t bytes = metrics.stride_bytes * metrics.height_px;
    MaskCharge count{};
    count.reserve(ledger, MaskResource::masks, 1);
    const MaskAllocator<TextMaskStorage> allocator(ledger);
    std::shared_ptr<TextMaskStorage> candidate = std::allocate_shared<TextMaskStorage>(allocator, ledger, std::move(count));
    (*candidate).key = key;
    (*candidate).metrics = metrics;
    (*candidate).lines.resize(line_count);
    (*candidate).pixels.resize(bytes);
    return candidate;
}
} // namespace gui_forms::detail
