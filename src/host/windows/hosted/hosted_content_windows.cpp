// Hosted content on Windows: not written yet (the Windows hand-back). The
// interface is the portable one (hosted_content.hpp); the Windows host does
// not report HostCapability::hosted_content, hosted_content_support()
// answers not_reported, and make_hosted_content refuses.
//
// The intended implementation, for whoever writes it: CreateProcessW with
// an explicit handle list (PROC_THREAD_ATTRIBUTE_HANDLE_LIST) passing an
// anonymous pipe pair (or a named pipe opened with FILE_FLAG_OVERLAPPED)
// as the channel and a pagefile-backed section (CreateFileMappingW, mapped
// FILE_MAP_READ in the parent) as the frame region, with their handle values
// in the same environment variables; a job object with
// JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE so the process ends with the
// application; WaitForMultipleObjects over the channel's event, the process
// handle and the stop event in the worker; TerminateProcess after the stop
// grace. Exit codes of 0xC0000000 and above report a crash.

#include "gui_forms/content/hosted/hosted_content.hpp"

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {

namespace detail {

// The Windows input state: never connected, so every send is dropped.
struct HostedInputState final {
    std::atomic<std::uint64_t> dropped{0U};
};

} // namespace detail

HostedContentSupport hosted_content_support() noexcept {
    return HostedContentSupport::not_reported;
}

HostedInput::HostedInput(std::shared_ptr<detail::HostedInputState> state) noexcept : state_(std::move(state)) {}

bool HostedInput::send(HostedMessageKind kind, std::span<const std::byte> payload) {
    static_cast<void>(kind);
    static_cast<void>(payload);
    if (state_) (*state_).dropped.fetch_add(1U, std::memory_order_relaxed);
    return false;
}

bool HostedInput::send_key(const KeyEvent& event) {
    static_cast<void>(event);
    const bool sent = send(HostedMessageKind::key, {});
    return sent;
}

bool HostedInput::send_text(std::string_view text, bool composing) {
    static_cast<void>(text);
    static_cast<void>(composing);
    const bool sent = send(HostedMessageKind::text, {});
    return sent;
}

bool HostedInput::send_pointer(const PointerEvent& event, Point device_position) {
    static_cast<void>(event);
    static_cast<void>(device_position);
    const bool sent = send(HostedMessageKind::pointer, {});
    return sent;
}

std::uint64_t HostedInput::dropped() const noexcept {
    if (!state_) return 0U;
    const std::uint64_t count = (*state_).dropped.load(std::memory_order_relaxed);
    return count;
}

bool HostedInput::connected() const noexcept {
    return false;
}

HostedContent make_hosted_content(HostedContentOptions options) {
    static_cast<void>(options);
    throw std::runtime_error("hosted content is not implemented on Windows yet (HostCapability::hosted_content "
                             "is not reported)");
}

} // namespace gui_forms
