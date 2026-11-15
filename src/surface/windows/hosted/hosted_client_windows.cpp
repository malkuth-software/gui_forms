// The hosted program's end on Windows (hosted_client.hpp; docs/user/
// content-surfaces.md, "Hosted content on Windows"). The channel and the
// frame region arrive as inherited handles whose values the environment
// names in decimal: an inherited handle keeps its value in the child
// ("Handle Inheritance", learn.microsoft.com, read 2026-11-15). The client
// checks them before use: the channel must be a pipe (GetFileType), and
// mapping the region fails when the section is smaller than the slots the
// environment names (MapViewOfFile: a view may not extend past the end of
// the section).
//
// The channel is the child end of a named pipe opened with
// FILE_FLAG_OVERLAPPED, so every read and write here passes an OVERLAPPED
// (ReadFile, WriteFile, GetOverlappedResult). A read stays pending across
// pump calls and is waited on with a timeout; a send waits for its write to
// complete: the program waits for its parent, never the other way round.

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0A00
#endif

#include "gui_forms/content/hosted/hosted_client.hpp"

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace gui_forms {

namespace detail {

// The channel's overlapped state (hosted_client.hpp): the events and
// OVERLAPPED records of the one read that may be pending and of the send in
// progress. A pending read writes the client's chunk_ until it completes;
// HostedClient::close_channel cancels it and waits before the handle
// closes. The events are owned here.
struct HostedClientChannelState final {
    HostedClientChannelState() noexcept = default;
    HostedClientChannelState(const HostedClientChannelState&) = delete;
    HostedClientChannelState& operator=(const HostedClientChannelState&) = delete;
    ~HostedClientChannelState() {
        if (read_event != nullptr) static_cast<void>(CloseHandle(read_event));
        if (write_event != nullptr) static_cast<void>(CloseHandle(write_event));
    }

    HANDLE read_event{};
    HANDLE write_event{};
    OVERLAPPED read_overlapped{};
    OVERLAPPED write_overlapped{};
    bool read_pending{};
};

} // namespace detail

namespace {

constexpr std::size_t hosted_client_read_chunk = 64U * 1024U;
// The most one write carries, so its count fits a DWORD.
constexpr std::size_t hosted_client_write_limit = 1024U * 1024U;
// Handle values are 32-bit quantities even in 64-bit processes.
constexpr std::uint64_t largest_handle_value = 0xFFFFFFFFULL;

// A decimal environment value, or none when unset or not a whole number.
[[nodiscard]] std::optional<std::uint64_t> environment_number(std::string_view name) {
    const std::string variable(name);
    const char* const value = std::getenv(variable.c_str());
    if (value == nullptr) return std::nullopt;
    const std::string_view text(value);
    std::uint64_t number = 0U;
    const char* const last = text.data() + text.size();
    const std::from_chars_result parsed = std::from_chars(text.data(), last, number);
    if (parsed.ec != std::errc{} || parsed.ptr != last) return std::nullopt;
    return number;
}

HANDLE handle_from_value(std::uint64_t value) noexcept {
    const std::uintptr_t bits = static_cast<std::uintptr_t>(value);
    const HANDLE handle = reinterpret_cast<HANDLE>(bits);
    return handle;
}

// A manual-reset event, initially nonsignaled; null on failure.
HANDLE make_event() noexcept {
    const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return event;
}

} // namespace

HostedClient::HostedClient(int channel, std::byte* region, std::uint64_t slot_bytes, std::uint32_t slot_count) noexcept
    : channel_(channel), region_(region), slot_bytes_(slot_bytes), slot_count_(slot_count) {}

std::unique_ptr<HostedClient> HostedClient::connect() {
    const std::optional<std::uint64_t> channel = environment_number(hosted_environment_channel);
    const std::optional<std::uint64_t> frames = environment_number(hosted_environment_frames);
    const std::optional<std::uint64_t> slot_bytes = environment_number(hosted_environment_slot_bytes);
    const std::optional<std::uint64_t> slot_count = environment_number(hosted_environment_slot_count);
    if (!channel || !frames || !slot_bytes || !slot_count) return nullptr;
    if (*channel == 0U || *frames == 0U || *channel > largest_handle_value || *frames > largest_handle_value) {
        return nullptr;
    }
    if (*slot_count != hosted_slot_count || *slot_bytes == 0U || *slot_bytes > maximum_hosted_slot_bytes) return nullptr;
    const HANDLE channel_handle = handle_from_value(*channel);
    const HANDLE frames_handle = handle_from_value(*frames);
    if (GetFileType(channel_handle) != FILE_TYPE_PIPE) return nullptr;
    std::unique_ptr<detail::HostedClientChannelState> state = std::make_unique<detail::HostedClientChannelState>();
    (*state).read_event = make_event();
    (*state).write_event = make_event();
    if ((*state).read_event == nullptr || (*state).write_event == nullptr) return nullptr;
    const std::uint64_t region_bytes = *slot_bytes * *slot_count;
    void* const mapped =
        MapViewOfFile(frames_handle, FILE_MAP_READ | FILE_MAP_WRITE, 0U, 0U, static_cast<SIZE_T>(region_bytes));
    if (mapped == nullptr) return nullptr;
    // The view keeps the section; the handle is not needed.
    static_cast<void>(CloseHandle(frames_handle));
    // The program's own children do not inherit the channel, so it breaks
    // when this program ends.
    static_cast<void>(SetHandleInformation(channel_handle, HANDLE_FLAG_INHERIT, 0U));
    std::unique_ptr<HostedClient> client(
        new HostedClient(-1, static_cast<std::byte*>(mapped), *slot_bytes, static_cast<std::uint32_t>(*slot_count)));
    (*client).channel_handle_ = reinterpret_cast<std::uintptr_t>(channel_handle);
    (*client).channel_state_ = std::move(state);
    (*client).chunk_.resize(hosted_client_read_chunk);
    encode_hosted_hello((*client).payload_);
    if (!(*client).send(HostedMessageKind::hello, (*client).payload_)) return nullptr;
    return client;
}

HostedClient::~HostedClient() {
    close_channel();
    if (region_ != nullptr) static_cast<void>(UnmapViewOfFile(region_));
}

void HostedClient::close_channel() noexcept {
    if (channel_handle_ == 0U) return;
    const HANDLE channel = reinterpret_cast<HANDLE>(channel_handle_);
    if (channel_state_ != nullptr && (*channel_state_).read_pending) {
        // The system stops writing chunk_ and the OVERLAPPED before the
        // handle closes.
        static_cast<void>(CancelIoEx(channel, &(*channel_state_).read_overlapped));
        DWORD transferred = 0U;
        static_cast<void>(GetOverlappedResult(channel, &(*channel_state_).read_overlapped, &transferred, TRUE));
        (*channel_state_).read_pending = false;
    }
    static_cast<void>(CloseHandle(channel));
    channel_handle_ = 0U;
}

bool HostedClient::send(HostedMessageKind kind, std::span<const std::byte> payload) {
    if (channel_handle_ == 0U) return false;
    const HANDLE channel = reinterpret_cast<HANDLE>(channel_handle_);
    detail::HostedClientChannelState& state = *channel_state_;
    outgoing_.clear();
    append_hosted_message(outgoing_, kind, payload);
    std::size_t sent = 0U;
    while (sent < outgoing_.size()) {
        const std::size_t piece = std::min(outgoing_.size() - sent, hosted_client_write_limit);
        state.write_overlapped = OVERLAPPED{};
        state.write_overlapped.hEvent = state.write_event;
        const BOOL written =
            WriteFile(channel, outgoing_.data() + sent, static_cast<DWORD>(piece), nullptr, &state.write_overlapped);
        if (written == FALSE && GetLastError() != ERROR_IO_PENDING) {
            close_channel();
            return false;
        }
        DWORD transferred = 0U;
        if (GetOverlappedResult(channel, &state.write_overlapped, &transferred, TRUE) == FALSE || transferred == 0U) {
            close_channel();
            return false;
        }
        sent += static_cast<std::size_t>(transferred);
    }
    return true;
}

void HostedClient::handle(const HostedMessage& message) {
    switch (message.kind) {
    case HostedMessageKind::configure: {
        HostedConfigureRecord record{};
        if (decode_hosted_configure(message.payload, record)) request_ = record;
        return;
    }
    case HostedMessageKind::release: {
        std::uint32_t slot = 0U;
        if (decode_hosted_slot(message.payload, slot) && slot < slot_count_) slot_free_[slot] = true;
        return;
    }
    case HostedMessageKind::key: {
        HostedClientInput input{};
        input.kind = HostedMessageKind::key;
        if (decode_hosted_key(message.payload, input.key)) input_.push_back(std::move(input));
        return;
    }
    case HostedMessageKind::text: {
        HostedClientInput input{};
        input.kind = HostedMessageKind::text;
        if (decode_hosted_text(message.payload, input.text, input.composing)) input_.push_back(std::move(input));
        return;
    }
    case HostedMessageKind::pointer: {
        HostedClientInput input{};
        input.kind = HostedMessageKind::pointer;
        if (decode_hosted_pointer(message.payload, input.pointer)) input_.push_back(std::move(input));
        return;
    }
    case HostedMessageKind::focus: {
        HostedClientInput input{};
        input.kind = HostedMessageKind::focus;
        if (decode_hosted_focus(message.payload, input.focused)) input_.push_back(std::move(input));
        return;
    }
    case HostedMessageKind::stop:
        stop_requested_ = true;
        return;
    case HostedMessageKind::hello:
    case HostedMessageKind::frame:
    case HostedMessageKind::heartbeat:
    case HostedMessageKind::semantics:
    case HostedMessageKind::goodbye:
        // The child's own kinds: a parent never sends them, so the channel
        // is closed below as for a broken peer.
        break;
    default:
        // A kind outside the protocol: the channel is closed below.
        break;
    }
    // The reader admits only the parent's kinds.
    close_channel();
}

bool HostedClient::pump(std::chrono::milliseconds timeout) {
    std::chrono::milliseconds wait = timeout;
    while (channel_handle_ != 0U && !stop_requested_) {
        const HANDLE channel = reinterpret_cast<HANDLE>(channel_handle_);
        detail::HostedClientChannelState& state = *channel_state_;
        if (!state.read_pending) {
            state.read_overlapped = OVERLAPPED{};
            state.read_overlapped.hEvent = state.read_event;
            const BOOL read =
                ReadFile(channel, chunk_.data(), static_cast<DWORD>(chunk_.size()), nullptr, &state.read_overlapped);
            if (read == FALSE && GetLastError() != ERROR_IO_PENDING) {
                close_channel();
                break;
            }
            state.read_pending = true;
        }
        const std::int64_t count = std::clamp<std::int64_t>(wait.count(), 0, static_cast<std::int64_t>(INFINITE) - 1);
        const DWORD waited = WaitForSingleObject(state.read_event, static_cast<DWORD>(count));
        if (waited == WAIT_TIMEOUT) break;
        if (waited != WAIT_OBJECT_0) {
            close_channel();
            break;
        }
        DWORD received = 0U;
        const BOOL completed = GetOverlappedResult(channel, &state.read_overlapped, &received, FALSE);
        state.read_pending = false;
        if (completed == FALSE) {
            close_channel();
            break;
        }
        reader_.feed(std::span<const std::byte>(chunk_.data(), static_cast<std::size_t>(received)));
        for (;;) {
            const HostedReadResult result = reader_.next(message_);
            if (result == HostedReadResult::need_more) break;
            if (result != HostedReadResult::message) {
                close_channel();
                break;
            }
            handle(message_);
        }
        // After the first read, take only what is already there.
        wait = std::chrono::milliseconds(0);
    }
    const bool running = channel_handle_ != 0U && !stop_requested_;
    return running;
}

std::optional<std::uint32_t> HostedClient::acquire_slot(std::chrono::milliseconds timeout) {
    // At most a day, so now() + timeout cannot overflow (A-gf-host-02-F13).
    const std::chrono::milliseconds bounded = std::min(timeout, std::chrono::milliseconds(std::chrono::hours(24)));
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + bounded;
    for (;;) {
        for (std::uint32_t slot = 0U; slot < slot_count_; ++slot) {
            if (slot_free_[slot]) return slot;
        }
        const std::chrono::steady_clock::duration left = deadline - std::chrono::steady_clock::now();
        const std::chrono::milliseconds wait = std::chrono::duration_cast<std::chrono::milliseconds>(left);
        if (wait.count() <= 0) return std::nullopt;
        if (!pump(wait)) return std::nullopt;
    }
}

std::span<std::byte> HostedClient::slot_pixels(std::uint32_t slot) noexcept {
    if (slot >= slot_count_ || region_ == nullptr) return std::span<std::byte>{};
    const std::uint64_t start = static_cast<std::uint64_t>(slot) * slot_bytes_;
    const std::span<std::byte> pixels(region_ + start, static_cast<std::size_t>(slot_bytes_));
    return pixels;
}

bool HostedClient::publish(const HostedFrameRecord& record) {
    if (record.slot >= slot_count_ || !slot_free_[record.slot]) return false;
    encode_hosted_frame(payload_, record);
    const bool sent = send(HostedMessageKind::frame, payload_);
    if (sent) slot_free_[record.slot] = false;
    return sent;
}

bool HostedClient::heartbeat() {
    const bool sent = send(HostedMessageKind::heartbeat, {});
    return sent;
}

bool HostedClient::publish_semantics(const ContentSemantics& semantics) {
    encode_hosted_semantics(payload_, semantics);
    const bool sent = send(HostedMessageKind::semantics, payload_);
    return sent;
}

bool HostedClient::goodbye() {
    const bool sent = send(HostedMessageKind::goodbye, {});
    return sent;
}

void HostedClient::take_input(std::vector<HostedClientInput>& out) {
    for (HostedClientInput& input : input_) out.push_back(std::move(input));
    input_.clear();
}

} // namespace gui_forms
