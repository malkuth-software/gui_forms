#pragma once

// The hosted-content wire protocol (owner decisions 2026-11-13, "The two
// topologies" and "What this asks of GUI.Forms", item 3): a sub-surface
// program runs in its own process; its pixels arrive through shared memory
// and its input goes back through a channel. This header is shared by the
// two ends: GUI.Forms (make_hosted_content, the parent) and the program
// (HostedClient, the child). Everything the child sends is validated by the
// parent before use; nothing the child does reaches the parent's memory
// except through these checks. It is an integrity boundary, not a security
// boundary: it keeps a crashing, hanging or confused program from harming
// the application, and does not confine what the program may do on the
// system.
//
// The channel is a stream of messages, each a 16-byte header followed by its
// payload. Every integer is little-endian and written byte by byte, so the
// two ends need not share a compiler or structure layout:
//
//     header: magic (u32) kind (u32) payload_bytes (u32) reserved (u32, 0)
//
// The frames live in one shared region of `slot_count` slots of
// `slot_bytes` each, which the parent creates, maps read-only and hands to
// the child. The child writes a frame into a slot the parent has released,
// then sends `frame` naming it; the parent validates the record against the
// slot, copies the pixels out and sends `release` for the slot. A child that
// writes a slot it has not been given back only tears its own picture.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/content/types/content_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// "GFHC" read as a little-endian u32.
inline constexpr std::uint32_t hosted_protocol_magic = 0x43484647U;
inline constexpr std::uint32_t hosted_protocol_version = 1U;
inline constexpr std::size_t hosted_message_header_bytes = 16U;
// The largest payload either end accepts: semantic text up to its limit,
// three strings' length fields and slack.
inline constexpr std::size_t hosted_maximum_payload_bytes = maximum_content_semantic_text_bytes + 64U;
// The frame slots: two, so the child draws one while the parent copies the
// other.
inline constexpr std::uint32_t hosted_slot_count = 2U;
// The default and largest slot: a frame of up to 32 MiB (a 4096 by 2048
// frame) by default; at most the content frame limit.
inline constexpr std::uint64_t default_hosted_slot_bytes = 32ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t maximum_hosted_slot_bytes = maximum_content_frame_bytes;

// The descriptors and environment the parent gives the child. The channel
// and the frame region are inherited at these descriptor numbers on POSIX;
// the environment names them as well, with the slot shape.
inline constexpr int hosted_channel_descriptor = 3;
inline constexpr int hosted_frames_descriptor = 4;
inline constexpr std::string_view hosted_environment_channel = "GUI_FORMS_HOSTED_CHANNEL";
inline constexpr std::string_view hosted_environment_frames = "GUI_FORMS_HOSTED_FRAMES";
inline constexpr std::string_view hosted_environment_slot_bytes = "GUI_FORMS_HOSTED_SLOT_BYTES";
inline constexpr std::string_view hosted_environment_slot_count = "GUI_FORMS_HOSTED_SLOT_COUNT";

enum class HostedMessageKind : std::uint32_t {
    // Child to parent.
    // payload: version (u32). The first message; the parent waits for it
    // within the start timeout.
    hello = 1U,
    // payload: a HostedFrameRecord (hosted_frame_record_bytes).
    frame = 2U,
    // payload: none. The program is alive but has nothing new to show.
    heartbeat = 3U,
    // payload: three strings (u32 length, then UTF-8): name, text,
    // description (ContentSemantics).
    semantics = 4U,
    // payload: none. The program is about to exit on its own.
    goodbye = 5U,
    // Parent to child.
    // payload: width (u32) height (u32) scale (f64 bits, u64) generation
    // (u64): the region's device size (ContentRequest).
    configure = 16U,
    // payload: slot (u32). The parent has copied the slot out.
    release = 17U,
    // payload: a HostedKeyRecord.
    key = 18U,
    // payload: composing (u32, 0 or 1), then the UTF-8 text.
    text = 19U,
    // payload: a HostedPointerRecord.
    pointer = 20U,
    // payload: none. Stop, release everything and exit; the parent ends the
    // process after its grace.
    stop = 21U,
    // payload: focused (u32, 0 or 1). The surface gained or lost the
    // keyboard focus.
    focus = 22U,
};

[[nodiscard]] GUI_FORMS_API std::string_view hosted_message_kind_name(HostedMessageKind kind) noexcept;
// Whether `value` names a message kind the child may send (`from_child`) or
// the parent may send.
[[nodiscard]] GUI_FORMS_API bool hosted_message_kind_valid(std::uint32_t value, bool from_child) noexcept;

// One message read from the channel. `payload` is owned.
struct GUI_FORMS_API HostedMessage final {
    HostedMessageKind kind{HostedMessageKind::heartbeat};
    std::vector<std::byte> payload{};
};

// A frame in a slot: its shape (as ContentFrame) and damage, in the slot's
// pixels from the slot's first byte. 40 bytes on the wire: slot, width,
// height, opaque (u32 each), row_bytes (u64), damage x, y, width, height
// (u32 each).
struct GUI_FORMS_API HostedFrameRecord final {
    std::uint32_t slot{};
    std::uint32_t width{};
    std::uint32_t height{};
    bool opaque{};
    std::uint64_t row_bytes{};
    ContentPixelRect damage{};
};
inline constexpr std::size_t hosted_frame_record_bytes = 40U;

// A key, as KeyEvent's numeric fields: action (u32), physical key (u32),
// modifiers (u32), repeat (u32, 0 or 1). 16 bytes.
struct GUI_FORMS_API HostedKeyRecord final {
    std::uint32_t action{};
    std::uint32_t physical_key{};
    std::uint32_t modifiers{};
    bool repeat{};
};
inline constexpr std::size_t hosted_key_record_bytes = 16U;

// A pointer event in the region's device pixels from its top left:
// action, button, modifiers, click count (u32 each), then x, y, wheel x,
// wheel y (f64 bits, u64 each). 48 bytes.
struct GUI_FORMS_API HostedPointerRecord final {
    std::uint32_t action{};
    std::uint32_t button{};
    std::uint32_t modifiers{};
    std::uint32_t click_count{};
    double x{};
    double y{};
    double wheel_x{};
    double wheel_y{};
};
inline constexpr std::size_t hosted_pointer_record_bytes = 48U;

// The parent's request, as the child receives it.
struct GUI_FORMS_API HostedConfigureRecord final {
    std::uint32_t width{};
    std::uint32_t height{};
    double scale{1.0};
    std::uint64_t generation{};
};
inline constexpr std::size_t hosted_configure_record_bytes = 24U;

// Appends one whole message (header and payload) to `out`. Throws
// std::length_error for a payload over hosted_maximum_payload_bytes.
GUI_FORMS_API void append_hosted_message(std::vector<std::byte>& out, HostedMessageKind kind,
                                         std::span<const std::byte> payload);

// Payload encoders: each fills `payload`, replacing its contents.
GUI_FORMS_API void encode_hosted_hello(std::vector<std::byte>& payload);
GUI_FORMS_API void encode_hosted_frame(std::vector<std::byte>& payload, const HostedFrameRecord& record);
GUI_FORMS_API void encode_hosted_slot(std::vector<std::byte>& payload, std::uint32_t slot);
GUI_FORMS_API void encode_hosted_key(std::vector<std::byte>& payload, const HostedKeyRecord& record);
GUI_FORMS_API void encode_hosted_text(std::vector<std::byte>& payload, std::string_view text, bool composing);
GUI_FORMS_API void encode_hosted_pointer(std::vector<std::byte>& payload, const HostedPointerRecord& record);
GUI_FORMS_API void encode_hosted_configure(std::vector<std::byte>& payload, const HostedConfigureRecord& record);
GUI_FORMS_API void encode_hosted_focus(std::vector<std::byte>& payload, bool focused);
// Throws std::length_error when the three strings exceed the payload limit.
GUI_FORMS_API void encode_hosted_semantics(std::vector<std::byte>& payload, const ContentSemantics& semantics);

// Payload decoders: each returns false, leaving `out` unspecified, for a
// payload of the wrong size or contents (an unknown version, a slot index
// or flag out of range, a string running past the payload, text that is
// not UTF-8).
[[nodiscard]] GUI_FORMS_API bool decode_hosted_hello(std::span<const std::byte> payload, std::uint32_t& version);
[[nodiscard]] GUI_FORMS_API bool decode_hosted_frame(std::span<const std::byte> payload, HostedFrameRecord& out);
[[nodiscard]] GUI_FORMS_API bool decode_hosted_slot(std::span<const std::byte> payload, std::uint32_t& slot);
[[nodiscard]] GUI_FORMS_API bool decode_hosted_key(std::span<const std::byte> payload, HostedKeyRecord& out);
[[nodiscard]] GUI_FORMS_API bool decode_hosted_text(std::span<const std::byte> payload, std::string& text,
                                                    bool& composing);
[[nodiscard]] GUI_FORMS_API bool decode_hosted_pointer(std::span<const std::byte> payload, HostedPointerRecord& out);
[[nodiscard]] GUI_FORMS_API bool decode_hosted_configure(std::span<const std::byte> payload,
                                                         HostedConfigureRecord& out);
[[nodiscard]] GUI_FORMS_API bool decode_hosted_semantics(std::span<const std::byte> payload, ContentSemantics& out);
[[nodiscard]] GUI_FORMS_API bool decode_hosted_focus(std::span<const std::byte> payload, bool& focused);

// What checking a frame record against the frame region found.
enum class HostedFrameCheck : std::uint8_t {
    accepted,
    // The slot index is not below the slot count.
    refused_slot,
    // The frame does not fit its slot, or validate_content_frame refused it
    // (`frame_result` says why).
    refused_frame,
};

// The frame a record describes, over `region` (the whole mapped region of
// slot_count slots of slot_bytes each), validated before any pixel is read:
// the slot index, then validate_content_frame over exactly the slot's bytes.
// The view borrows `region`, which must outlive it. On `accepted`, `frame`
// views the frame's bytes in the slot; on
// `refused_frame`, the whole slot (publishing it refuses it again, counted).
struct GUI_FORMS_API HostedFrameView final {
    HostedFrameCheck check{HostedFrameCheck::refused_slot};
    ContentPublishResult frame_result{ContentPublishResult::refused_empty};
    ContentFrame frame{};
};
[[nodiscard]] GUI_FORMS_API HostedFrameView view_hosted_frame(const HostedFrameRecord& record,
                                                              std::span<const std::byte> region,
                                                              std::uint32_t slot_count, std::uint64_t slot_bytes) noexcept;

// What the reader found in the bytes fed so far.
enum class HostedReadResult : std::uint8_t {
    // `next` filled one message.
    message,
    // No whole message yet.
    need_more,
    // The stream is broken and nothing more can be read from it: a wrong
    // magic, a nonzero reserved field, a kind this end does not accept, or a
    // payload over the limit.
    refused_magic,
    refused_kind,
    refused_size,
};

[[nodiscard]] GUI_FORMS_API std::string_view hosted_read_result_name(HostedReadResult result) noexcept;

// Splits a byte stream into messages, checking each header before its
// payload is buffered. One reader per direction and thread. Its buffer
// grows to at most one header and one largest payload beyond what was fed
// and not yet read.
class GUI_FORMS_API HostedMessageReader final {
public:
    // `from_child`: the reader accepts the kinds a child sends (the parent's
    // reader) or the kinds a parent sends (the child's).
    explicit HostedMessageReader(bool from_child) noexcept : from_child_(from_child) {}

    // Appends received bytes.
    void feed(std::span<const std::byte> bytes);
    // Takes the next whole message into `out`. After a refusal every later
    // call repeats it.
    [[nodiscard]] HostedReadResult next(HostedMessage& out);
    // Bytes fed and not yet taken.
    [[nodiscard]] std::size_t pending_bytes() const noexcept { return buffer_.size() - consumed_; }

private:
    std::vector<std::byte> buffer_{};
    std::size_t consumed_{};
    HostedReadResult broken_{HostedReadResult::need_more};
    bool from_child_{};
};

} // namespace gui_forms
