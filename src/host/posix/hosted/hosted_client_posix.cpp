// The hosted program's end on macOS and Linux (hosted_client.hpp). The
// channel and the frame region arrive as inherited descriptors whose numbers
// the environment names; the client checks them before use (a socket, a
// region at least as large as the slots it is told of). Sends block: the
// program waits for its parent, never the other way round.

#include "gui_forms/content/hosted/hosted_client.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace gui_forms {

namespace {

constexpr std::size_t hosted_client_read_chunk = 64U * 1024U;

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

int client_send_flags() noexcept {
#if defined(MSG_NOSIGNAL)
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
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
    if (*channel > 1024U || *frames > 1024U) return nullptr;
    if (*slot_count != hosted_slot_count || *slot_bytes == 0U || *slot_bytes > maximum_hosted_slot_bytes) return nullptr;
    const int channel_descriptor = static_cast<int>(*channel);
    const int frames_descriptor = static_cast<int>(*frames);
    int socket_type = 0;
    socklen_t type_size = sizeof(socket_type);
    if (getsockopt(channel_descriptor, SOL_SOCKET, SO_TYPE, &socket_type, &type_size) != 0 || socket_type != SOCK_STREAM) {
        return nullptr;
    }
    struct stat region_status {};
    const std::uint64_t region_bytes = *slot_bytes * *slot_count;
    if (fstat(frames_descriptor, &region_status) != 0 || region_status.st_size < 0 ||
        static_cast<std::uint64_t>(region_status.st_size) < region_bytes) {
        return nullptr;
    }
    void* const mapped = mmap(nullptr, static_cast<std::size_t>(region_bytes), PROT_READ | PROT_WRITE, MAP_SHARED,
                              frames_descriptor, 0);
    if (mapped == MAP_FAILED) return nullptr;
    // The mapping keeps the region; the descriptor is not needed.
    static_cast<void>(close(frames_descriptor));
#if defined(SO_NOSIGPIPE)
    const int one = 1;
    static_cast<void>(setsockopt(channel_descriptor, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)));
#endif
    std::unique_ptr<HostedClient> client(new HostedClient(channel_descriptor, static_cast<std::byte*>(mapped),
                                                          *slot_bytes, static_cast<std::uint32_t>(*slot_count)));
    (*client).chunk_.resize(hosted_client_read_chunk);
    encode_hosted_hello((*client).payload_);
    if (!(*client).send(HostedMessageKind::hello, (*client).payload_)) return nullptr;
    return client;
}

HostedClient::~HostedClient() {
    close_channel();
    if (region_ != nullptr) {
        static_cast<void>(munmap(region_, static_cast<std::size_t>(slot_bytes_ * slot_count_)));
    }
}

void HostedClient::close_channel() noexcept {
    if (channel_ >= 0) static_cast<void>(close(channel_));
    channel_ = -1;
}

bool HostedClient::send(HostedMessageKind kind, std::span<const std::byte> payload) {
    if (channel_ < 0) return false;
    outgoing_.clear();
    append_hosted_message(outgoing_, kind, payload);
    std::size_t sent = 0U;
    while (sent < outgoing_.size()) {
        const ssize_t written = ::send(channel_, outgoing_.data() + sent, outgoing_.size() - sent, client_send_flags());
        if (written < 0) {
            if (errno == EINTR) continue;
            close_channel();
            return false;
        }
        sent += static_cast<std::size_t>(written);
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
    while (channel_ >= 0 && !stop_requested_) {
        pollfd watched{};
        watched.fd = channel_;
        watched.events = POLLIN;
        const int milliseconds = static_cast<int>(wait.count() < 0 ? 0 : wait.count());
        const int ready = ::poll(&watched, 1U, milliseconds);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) break;
        const ssize_t received = recv(channel_, chunk_.data(), chunk_.size(), 0);
        if (received == 0 || (received < 0 && errno != EINTR && errno != EAGAIN)) {
            close_channel();
            break;
        }
        if (received > 0) {
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
        }
        // After the first read, take only what is already there.
        wait = std::chrono::milliseconds(0);
    }
    const bool running = channel_ >= 0 && !stop_requested_;
    return running;
}

std::optional<std::uint32_t> HostedClient::acquire_slot(std::chrono::milliseconds timeout) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + timeout;
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
