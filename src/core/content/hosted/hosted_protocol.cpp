#include "gui_forms/content/hosted/hosted_protocol.hpp"

#include "gui_forms/text/types/text_types.hpp"

#include <bit>
#include <cmath>
#include <stdexcept>

namespace gui_forms {

namespace {

void put_u32(std::vector<std::byte>& out, std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void put_u64(std::vector<std::byte>& out, std::uint64_t value) {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void put_f64(std::vector<std::byte>& out, double value) {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    put_u64(out, bits);
}

void put_text(std::vector<std::byte>& out, std::string_view text) {
    const std::uint32_t length = static_cast<std::uint32_t>(text.size());
    put_u32(out, length);
    const std::byte* const first = reinterpret_cast<const std::byte*>(text.data());
    out.insert(out.end(), first, first + text.size());
}

// Reads little-endian fields from a payload in order; any read past the end
// sets `failed` and yields zero, so a decoder checks once at the end.
class PayloadCursor final {
public:
    explicit PayloadCursor(std::span<const std::byte> payload) noexcept : payload_(payload) {}

    [[nodiscard]] std::uint32_t u32() noexcept {
        if (offset_ > payload_.size() || payload_.size() - offset_ < 4U) {
            failed_ = true;
            return 0U;
        }
        std::uint32_t value = 0U;
        for (std::uint32_t index = 0U; index < 4U; ++index) {
            const std::uint32_t part = std::to_integer<std::uint32_t>(payload_[offset_ + index]);
            value |= part << (index * 8U);
        }
        offset_ += 4U;
        return value;
    }

    [[nodiscard]] std::uint64_t u64() noexcept {
        const std::uint64_t low = u32();
        const std::uint64_t high = u32();
        const std::uint64_t value = low | (high << 32U);
        return value;
    }

    [[nodiscard]] double f64() noexcept {
        const std::uint64_t bits = u64();
        const double value = std::bit_cast<double>(bits);
        return value;
    }

    // A u32 length and that many bytes; false past the end.
    [[nodiscard]] bool text(std::string& out) {
        const std::uint32_t length = u32();
        if (failed_ || payload_.size() - offset_ < length) {
            failed_ = true;
            return false;
        }
        const char* const first = reinterpret_cast<const char*>(payload_.data() + offset_);
        out.assign(first, length);
        offset_ += length;
        return true;
    }

    [[nodiscard]] std::span<const std::byte> rest() const& noexcept {
        const std::span<const std::byte> remaining = payload_.subspan(offset_);
        return remaining;
    }
    std::span<const std::byte> rest() const&& = delete;

    // Whether every read stayed inside and the payload was used exactly.
    [[nodiscard]] bool complete() const noexcept {
        const bool result = !failed_ && offset_ == payload_.size();
        return result;
    }

    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    std::span<const std::byte> payload_;
    std::size_t offset_{};
    bool failed_{};
};

[[nodiscard]] bool flag_of(std::uint32_t value, bool& out) noexcept {
    if (value > 1U) return false;
    out = value == 1U;
    return true;
}

[[nodiscard]] bool finite_scale(double scale) noexcept {
    const bool result = std::isfinite(scale) && scale > 0.0;
    return result;
}

} // namespace

std::string_view hosted_message_kind_name(HostedMessageKind kind) noexcept {
    switch (kind) {
    case HostedMessageKind::hello: return "hello";
    case HostedMessageKind::frame: return "frame";
    case HostedMessageKind::heartbeat: return "heartbeat";
    case HostedMessageKind::semantics: return "semantics";
    case HostedMessageKind::goodbye: return "goodbye";
    case HostedMessageKind::configure: return "configure";
    case HostedMessageKind::release: return "release";
    case HostedMessageKind::key: return "key";
    case HostedMessageKind::text: return "text";
    case HostedMessageKind::pointer: return "pointer";
    case HostedMessageKind::stop: return "stop";
    case HostedMessageKind::focus: return "focus";
    }
    return "unknown";
}

bool hosted_message_kind_valid(std::uint32_t value, bool from_child) noexcept {
    const bool child_kind = value >= static_cast<std::uint32_t>(HostedMessageKind::hello) &&
                            value <= static_cast<std::uint32_t>(HostedMessageKind::goodbye);
    const bool parent_kind = value >= static_cast<std::uint32_t>(HostedMessageKind::configure) &&
                             value <= static_cast<std::uint32_t>(HostedMessageKind::focus);
    const bool result = from_child ? child_kind : parent_kind;
    return result;
}

void append_hosted_message(std::vector<std::byte>& out, HostedMessageKind kind, std::span<const std::byte> payload) {
    if (payload.size() > hosted_maximum_payload_bytes) {
        throw std::length_error("hosted message payload over the limit");
    }
    out.reserve(out.size() + hosted_message_header_bytes + payload.size());
    put_u32(out, hosted_protocol_magic);
    put_u32(out, static_cast<std::uint32_t>(kind));
    put_u32(out, static_cast<std::uint32_t>(payload.size()));
    put_u32(out, 0U);
    out.insert(out.end(), payload.begin(), payload.end());
}

void encode_hosted_hello(std::vector<std::byte>& payload) {
    payload.clear();
    put_u32(payload, hosted_protocol_version);
}

void encode_hosted_frame(std::vector<std::byte>& payload, const HostedFrameRecord& record) {
    payload.clear();
    put_u32(payload, record.slot);
    put_u32(payload, record.width);
    put_u32(payload, record.height);
    put_u32(payload, record.opaque ? 1U : 0U);
    put_u64(payload, record.row_bytes);
    put_u32(payload, record.damage.x);
    put_u32(payload, record.damage.y);
    put_u32(payload, record.damage.width);
    put_u32(payload, record.damage.height);
}

void encode_hosted_slot(std::vector<std::byte>& payload, std::uint32_t slot) {
    payload.clear();
    put_u32(payload, slot);
}

void encode_hosted_key(std::vector<std::byte>& payload, const HostedKeyRecord& record) {
    payload.clear();
    put_u32(payload, record.action);
    put_u32(payload, record.physical_key);
    put_u32(payload, record.modifiers);
    put_u32(payload, record.repeat ? 1U : 0U);
}

void encode_hosted_text(std::vector<std::byte>& payload, std::string_view text, bool composing) {
    if (text.size() > hosted_maximum_payload_bytes - 4U) throw std::length_error("hosted text over the limit");
    payload.clear();
    put_u32(payload, composing ? 1U : 0U);
    const std::byte* const first = reinterpret_cast<const std::byte*>(text.data());
    payload.insert(payload.end(), first, first + text.size());
}

void encode_hosted_focus(std::vector<std::byte>& payload, bool focused) {
    payload.clear();
    put_u32(payload, focused ? 1U : 0U);
}

void encode_hosted_pointer(std::vector<std::byte>& payload, const HostedPointerRecord& record) {
    payload.clear();
    put_u32(payload, record.action);
    put_u32(payload, record.button);
    put_u32(payload, record.modifiers);
    put_u32(payload, record.click_count);
    put_f64(payload, record.x);
    put_f64(payload, record.y);
    put_f64(payload, record.wheel_x);
    put_f64(payload, record.wheel_y);
}

void encode_hosted_configure(std::vector<std::byte>& payload, const HostedConfigureRecord& record) {
    payload.clear();
    put_u32(payload, record.width);
    put_u32(payload, record.height);
    put_f64(payload, record.scale);
    put_u64(payload, record.generation);
}

void encode_hosted_semantics(std::vector<std::byte>& payload, const ContentSemantics& semantics) {
    const std::size_t total = semantics.name.size() + semantics.text.size() + semantics.description.size();
    if (total > hosted_maximum_payload_bytes - 12U) throw std::length_error("hosted semantic text over the limit");
    payload.clear();
    put_text(payload, semantics.name);
    put_text(payload, semantics.text);
    put_text(payload, semantics.description);
}

bool decode_hosted_hello(std::span<const std::byte> payload, std::uint32_t& version) {
    PayloadCursor cursor(payload);
    version = cursor.u32();
    const bool result = cursor.complete() && version == hosted_protocol_version;
    return result;
}

bool decode_hosted_frame(std::span<const std::byte> payload, HostedFrameRecord& out) {
    PayloadCursor cursor(payload);
    out.slot = cursor.u32();
    out.width = cursor.u32();
    out.height = cursor.u32();
    const std::uint32_t opaque = cursor.u32();
    out.row_bytes = cursor.u64();
    out.damage.x = cursor.u32();
    out.damage.y = cursor.u32();
    out.damage.width = cursor.u32();
    out.damage.height = cursor.u32();
    if (!cursor.complete()) return false;
    const bool result = flag_of(opaque, out.opaque);
    return result;
}

bool decode_hosted_slot(std::span<const std::byte> payload, std::uint32_t& slot) {
    PayloadCursor cursor(payload);
    slot = cursor.u32();
    const bool result = cursor.complete() && slot < hosted_slot_count;
    return result;
}

bool decode_hosted_focus(std::span<const std::byte> payload, bool& focused) {
    PayloadCursor cursor(payload);
    const std::uint32_t flag = cursor.u32();
    if (!cursor.complete()) return false;
    const bool result = flag_of(flag, focused);
    return result;
}

bool decode_hosted_key(std::span<const std::byte> payload, HostedKeyRecord& out) {
    PayloadCursor cursor(payload);
    out.action = cursor.u32();
    out.physical_key = cursor.u32();
    out.modifiers = cursor.u32();
    const std::uint32_t repeat = cursor.u32();
    if (!cursor.complete()) return false;
    const bool result = flag_of(repeat, out.repeat);
    return result;
}

bool decode_hosted_text(std::span<const std::byte> payload, std::string& text, bool& composing) {
    PayloadCursor cursor(payload);
    const std::uint32_t flag = cursor.u32();
    if (cursor.failed() || !flag_of(flag, composing)) return false;
    const std::span<const std::byte> rest = cursor.rest();
    const char* const first = reinterpret_cast<const char*>(rest.data());
    text.assign(first, rest.size());
    const Utf8ValidationResult validation = validate_utf8(text);
    return validation.valid();
}

bool decode_hosted_pointer(std::span<const std::byte> payload, HostedPointerRecord& out) {
    PayloadCursor cursor(payload);
    out.action = cursor.u32();
    out.button = cursor.u32();
    out.modifiers = cursor.u32();
    out.click_count = cursor.u32();
    out.x = cursor.f64();
    out.y = cursor.f64();
    out.wheel_x = cursor.f64();
    out.wheel_y = cursor.f64();
    if (!cursor.complete()) return false;
    const bool result = std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.wheel_x) &&
                        std::isfinite(out.wheel_y);
    return result;
}

bool decode_hosted_configure(std::span<const std::byte> payload, HostedConfigureRecord& out) {
    PayloadCursor cursor(payload);
    out.width = cursor.u32();
    out.height = cursor.u32();
    out.scale = cursor.f64();
    out.generation = cursor.u64();
    const bool result = cursor.complete() && finite_scale(out.scale) && out.width <= maximum_content_frame_extent &&
                        out.height <= maximum_content_frame_extent;
    return result;
}

bool decode_hosted_semantics(std::span<const std::byte> payload, ContentSemantics& out) {
    PayloadCursor cursor(payload);
    if (!cursor.text(out.name) || !cursor.text(out.text) || !cursor.text(out.description)) return false;
    if (!cursor.complete()) return false;
    const ContentSemanticsResult validity = validate_content_semantics(out);
    return validity == ContentSemanticsResult::published;
}

HostedFrameView view_hosted_frame(const HostedFrameRecord& record, std::span<const std::byte> region,
                                  std::uint32_t slot_count, std::uint64_t slot_bytes) noexcept {
    HostedFrameView view{};
    // The region must hold every slot; a caller passing less is refused
    // rather than trusted.
    if (record.slot >= slot_count || slot_bytes == 0U || slot_bytes > maximum_hosted_slot_bytes) {
        view.check = HostedFrameCheck::refused_slot;
        return view;
    }
    const std::uint64_t slot_start = static_cast<std::uint64_t>(record.slot) * slot_bytes;
    if (slot_start > region.size() || region.size() - slot_start < slot_bytes) {
        view.check = HostedFrameCheck::refused_slot;
        return view;
    }
    ContentFrame frame{};
    frame.width = record.width;
    frame.height = record.height;
    frame.row_bytes = record.row_bytes;
    frame.opaque = record.opaque;
    frame.damage = record.damage;
    frame.pixels = region.subspan(static_cast<std::size_t>(slot_start), static_cast<std::size_t>(slot_bytes));
    const ContentPublishResult validity = validate_content_frame(frame);
    view.frame_result = validity;
    if (validity != ContentPublishResult::published) {
        // The refused frame still views the slot, so a sink that publishes
        // it refuses it again and counts the reason.
        view.check = HostedFrameCheck::refused_frame;
        view.frame = frame;
        return view;
    }
    // Only the bytes the frame occupies are handed on.
    const std::uint64_t extent = content_frame_extent_bytes(record.width, record.height, record.row_bytes);
    frame.pixels = frame.pixels.first(static_cast<std::size_t>(extent));
    view.check = HostedFrameCheck::accepted;
    view.frame = frame;
    return view;
}

std::string_view hosted_read_result_name(HostedReadResult result) noexcept {
    switch (result) {
    case HostedReadResult::message: return "message";
    case HostedReadResult::need_more: return "need_more";
    case HostedReadResult::refused_magic: return "refused_magic";
    case HostedReadResult::refused_kind: return "refused_kind";
    case HostedReadResult::refused_size: return "refused_size";
    }
    return "unknown";
}

void HostedMessageReader::feed(std::span<const std::byte> bytes) {
    // Taken bytes are dropped before the buffer grows, so it holds at most
    // what is pending.
    if (consumed_ != 0U && consumed_ == buffer_.size()) {
        buffer_.clear();
        consumed_ = 0U;
    } else if (consumed_ > buffer_.size() / 2U) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed_));
        consumed_ = 0U;
    }
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

HostedReadResult HostedMessageReader::next(HostedMessage& out) {
    if (broken_ != HostedReadResult::need_more) return broken_;
    const std::size_t pending = buffer_.size() - consumed_;
    if (pending < hosted_message_header_bytes) return HostedReadResult::need_more;
    const std::span<const std::byte> header(buffer_.data() + consumed_, hosted_message_header_bytes);
    PayloadCursor cursor(header);
    const std::uint32_t magic = cursor.u32();
    const std::uint32_t kind = cursor.u32();
    const std::uint32_t payload_bytes = cursor.u32();
    const std::uint32_t reserved = cursor.u32();
    // The header is checked before its payload is waited for or buffered.
    if (magic != hosted_protocol_magic || reserved != 0U) {
        broken_ = HostedReadResult::refused_magic;
        return broken_;
    }
    if (!hosted_message_kind_valid(kind, from_child_)) {
        broken_ = HostedReadResult::refused_kind;
        return broken_;
    }
    if (payload_bytes > hosted_maximum_payload_bytes) {
        broken_ = HostedReadResult::refused_size;
        return broken_;
    }
    if (pending - hosted_message_header_bytes < payload_bytes) return HostedReadResult::need_more;
    const std::byte* const first = buffer_.data() + consumed_ + hosted_message_header_bytes;
    out.kind = static_cast<HostedMessageKind>(kind);
    out.payload.assign(first, first + payload_bytes);
    consumed_ += hosted_message_header_bytes + payload_bytes;
    return HostedReadResult::message;
}

} // namespace gui_forms
