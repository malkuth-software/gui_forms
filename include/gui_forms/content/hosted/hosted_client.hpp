#pragma once

// The hosted program's end (hosted_content.hpp, hosted_protocol.hpp): a
// program GUI.Forms started with make_hosted_content connects with
// HostedClient::connect, draws into the frame slots the parent releases and
// publishes them, and reads its input. One thread uses a client. The client
// library (gui_forms_hosted_client: macOS, Linux and Windows) links the
// protocol and nothing of the interface.
//
//     std::unique_ptr<HostedClient> client = HostedClient::connect();
//     if (!client) return 1;                         // not started by GUI.Forms
//     while ((*client).pump(std::chrono::milliseconds(0))) {
//         std::optional<std::uint32_t> slot = (*client).acquire_slot(std::chrono::milliseconds(100));
//         if (!slot) continue;
//         std::span<std::byte> pixels = (*client).slot_pixels(*slot);
//         ... draw width by height BGRA rows into pixels ...
//         (*client).publish(HostedFrameRecord{*slot, width, height, true, row_bytes, {}});
//     }
//     // pump returned false: the parent asked to stop or went away; exit.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/content/hosted/hosted_protocol.hpp"

#include <stxlib/optional.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

namespace detail {
// The channel's platform state beyond its descriptor or handle: on Windows
// the overlapped read and write; empty elsewhere.
struct HostedClientChannelState;
} // namespace detail

// One input event the parent forwarded. `kind` is key, text, pointer or
// focus and says which fields are set. They arrive in the order the
// surface received them (ContentSurface::start_hosted_content).
struct GUI_FORMS_API HostedClientInput final {
    HostedMessageKind kind{HostedMessageKind::key};
    HostedKeyRecord key{};
    HostedPointerRecord pointer{};
    std::string text{};
    bool composing{};
    bool focused{};
};

class GUI_FORMS_API HostedClient final {
public:
    // Connects to the parent the environment names, maps the frame region
    // and sends hello. None when the process was not started as hosted
    // content (the variables are missing or name no usable descriptors).
    [[nodiscard]] static std::unique_ptr<HostedClient> connect();
    ~HostedClient();
    HostedClient(const HostedClient&) = delete;
    HostedClient& operator=(const HostedClient&) = delete;

    [[nodiscard]] std::uint32_t slot_count() const noexcept { return slot_count_; }
    [[nodiscard]] std::uint64_t slot_bytes() const noexcept { return slot_bytes_; }

    // Waits up to `timeout` for parent messages and handles every one that
    // arrived: configure updates request(), release frees a slot, input
    // (key, text, pointer, focus) is queued for take_input, stop sets
    // stop_requested(). Returns false once
    // a stop was asked for or the channel closed or broke.
    bool pump(std::chrono::milliseconds timeout);
    // The region's device size and scale as last configured (zero before
    // the first configure).
    [[nodiscard]] HostedConfigureRecord request() const noexcept { return request_; }
    [[nodiscard]] bool stop_requested() const noexcept { return stop_requested_; }
    [[nodiscard]] bool connected() const noexcept { return channel_ >= 0 || channel_handle_ != 0U; }

    // A slot the parent has released, pumping up to `timeout` for one; none
    // on timeout, stop or a closed channel.
    [[nodiscard]] stxlib::Optional<std::uint32_t> acquire_slot(std::chrono::milliseconds timeout);
    // The slot's writable bytes, slot_bytes() long, valid while the client
    // lives. An empty span for a slot that does not exist.
    [[nodiscard]] std::span<std::byte> slot_pixels(std::uint32_t slot) noexcept;
    // Sends the frame in `record.slot`; the slot is in use until the parent
    // releases it. False when the slot is not one acquire_slot gave, or the
    // channel is closed.
    bool publish(const HostedFrameRecord& record);
    bool heartbeat();
    bool publish_semantics(const ContentSemantics& semantics);
    // Says the program is about to exit on its own.
    bool goodbye();
    // Moves the input received so far to the end of `out`.
    void take_input(std::vector<HostedClientInput>& out);
    // The channel's descriptor, for a program that waits on it with its own
    // loop (and for tests that write malformed bytes); -1 once closed, and
    // always on Windows, which has channel_handle() instead.
    [[nodiscard]] int channel_descriptor() const noexcept { return channel_; }
    // Windows: the channel's HANDLE value, for the same uses; 0 once closed,
    // and always elsewhere. It is the child end of a named pipe opened with
    // FILE_FLAG_OVERLAPPED, so every ReadFile and WriteFile on it passes an
    // OVERLAPPED; the client may have a read of its own pending on it.
    [[nodiscard]] std::uintptr_t channel_handle() const noexcept { return channel_handle_; }

private:
    HostedClient(int channel, std::byte* region, std::uint64_t slot_bytes, std::uint32_t slot_count) noexcept;
    bool send(HostedMessageKind kind, std::span<const std::byte> payload);
    void handle(const HostedMessage& message);
    void close_channel() noexcept;

    int channel_{-1};
    std::uintptr_t channel_handle_{};
    std::unique_ptr<detail::HostedClientChannelState> channel_state_{};
    std::byte* region_{};
    std::uint64_t slot_bytes_{};
    std::uint32_t slot_count_{};
    // Whether each slot is free to draw (released or never published).
    std::array<bool, hosted_slot_count> slot_free_{true, true};
    HostedConfigureRecord request_{0U, 0U, 1.0, 0U};
    bool stop_requested_{};
    HostedMessageReader reader_{false};
    HostedMessage message_{};
    std::vector<std::byte> outgoing_{};
    std::vector<std::byte> payload_{};
    std::vector<std::byte> chunk_{};
    std::vector<HostedClientInput> input_{};
};

} // namespace gui_forms
