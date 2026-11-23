#include "gui_forms/audio/audio.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"
#define MINIAUDIO_IMPLEMENTATION
// miniaudio is vendored as published: its implementation defines helpers a
// platform's build does not call (ma_device__on_notification_rerouted on
// Linux), which is its code's warning, not ours.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "miniaudio.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "../platform/library_files.hpp"
#ifdef GUI_FORMS_AUDIO_LOOP_TRANSPORT
#include "loop_transport/loop_transport_state.hpp"
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <stxlib/files.hpp>
#include <stxlib/stop_token.hpp>
#include <stxlib/threads.hpp>

#ifdef GUI_FORMS_AUDIO_VORBIS_STX
#include "stx_vorbis/stb_compatible.h"
#else
namespace {
// stb_vorbis sorts codewords and floor points through qsort. Some C libraries
// allocate a temporary merge buffer there, outside the supplied codec arena.
// This private foreign-call adapter uses an in-place heap and constant scratch.
using VorbisCompare = int (*)(const void*, const void*);
void vorbis_swap(unsigned char* first, unsigned char* second, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) { std::swap(first[i], second[i]); }
}
void vorbis_sift(unsigned char* data, std::size_t root, std::size_t count,
                 std::size_t width, VorbisCompare compare) {
    while (root < count / 2) {
        std::size_t child = root * 2 + 1;
        if (child + 1 < count) {
            const int order = compare(data + child * width, data + (child + 1) * width);
            if (order < 0) { ++child; }
        }
        const int order = compare(data + root * width, data + child * width);
        if (order >= 0) { return; }
        vorbis_swap(data + root * width, data + child * width, width);
        root = child;
    }
}
void vorbis_sort(void* buffer, std::size_t count, std::size_t width, VorbisCompare compare) {
    if (count < 2) { return; }
    unsigned char* data = static_cast<unsigned char*>(buffer);
    for (std::size_t i = count / 2; i > 0; --i) { vorbis_sift(data, i - 1, count, width, compare); }
    for (std::size_t end = count; end > 1; --end) {
        vorbis_swap(data, data + (end - 1) * width, width);
        vorbis_sift(data, 0, end - 1, width, compare);
    }
}
}
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION
#define STB_VORBIS_MAX_CHANNELS 2
#define qsort vorbis_sort
#include "extras/stb_vorbis.c"
#undef qsort
#endif

namespace gui_forms {
namespace {
constexpr std::uint64_t maximum_frames = 28800000;
constexpr std::uint64_t maximum_clip_bytes = maximum_frames * 2 * sizeof(float);
constexpr std::uint64_t clip_budget = 512ULL * 1024 * 1024;
constexpr std::uint64_t file_limit = 256ULL * 1024 * 1024;
constexpr std::uint64_t ogg_file_limit = 64ULL * 1024 * 1024;
constexpr std::size_t vorbis_arena_bytes = 16 * 1024 * 1024;
// Upstream initializes a process-global CRC table on each open. Our decoder
// uses sequential pull only (never seek/length/push, which read that table).
stxlib::Mutex& vorbis_open_mutex() {
    // Never destroyed (rule 1: process termination releases none of our state).
    static stxlib::Mutex* const value = new stxlib::Mutex();
    return *value;
}
std::atomic<std::uint64_t> clip_bytes{0};
#ifdef GUI_FORMS_AUDIO_TESTING
thread_local std::size_t test_arena_bytes = vorbis_arena_bytes;
thread_local stxlib::StopSource* test_cancel_after_chunk = nullptr;
thread_local CancellationFlag* test_flag_after_chunk = nullptr;
thread_local std::uint64_t test_clip_budget = clip_budget;
#endif
bool charge_clip(std::uint64_t bytes) {
    std::uint64_t budget = clip_budget;
#ifdef GUI_FORMS_AUDIO_TESTING
    budget = test_clip_budget;
#endif
    std::uint64_t current = clip_bytes.load();
    while (current <= budget && bytes <= budget - current) {
        const bool accepted = clip_bytes.compare_exchange_weak(current, current + bytes);
        if (accepted) {
            return true;
        }
    }
    return false;
}
std::uint32_t little32(const unsigned char* bytes) {
    const std::uint32_t value = std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
           (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
    return value;
}
std::uint16_t little16(const unsigned char* bytes) {
    const std::uint16_t value = std::uint16_t(std::uint16_t(bytes[0]) | (std::uint16_t(bytes[1]) << 8));
    return value;
}
bool tag(const unsigned char* bytes, const char* text) {
    const bool matches = bytes[0] == text[0] && bytes[1] == text[1] && bytes[2] == text[2] && bytes[3] == text[3];
    return matches;
}
bool decode_pcm16(const unsigned char* bytes, std::span<float> output) {
    for (std::size_t i = 0; i < output.size(); ++i) {
        const std::uint16_t encoded = little16(bytes + i * 2);
        const std::int16_t sample = std::bit_cast<std::int16_t>(encoded);
        output[i] = static_cast<float>(sample) / 32768.0f;
    }
    return true;
}
bool decode_float32(const unsigned char* bytes, std::span<float> output) {
    for (std::size_t i = 0; i < output.size(); ++i) {
        const std::uint32_t encoded = little32(bytes + i * 4);
        const float sample = std::bit_cast<float>(encoded);
        if (!std::isfinite(sample) || sample < -1 || sample > 1) { return false; }
        output[i] = sample;
    }
    return true;
}
constexpr std::array<std::uint32_t, 256> make_ogg_crc_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::size_t i = 0; i < table.size(); ++i) {
        std::uint32_t value = static_cast<std::uint32_t>(i) << 24;
        for (unsigned bit = 0; bit < 8; ++bit) {
            value = (value & 0x80000000U) != 0 ? (value << 1) ^ 0x04c11db7U : value << 1;
        }
        table[i] = value;
    }
    return table;
}
constexpr std::array<std::uint32_t, 256> ogg_crc_table = make_ogg_crc_table();
std::uint32_t ogg_crc(std::span<const unsigned char> page) {
    std::uint32_t crc = 0;
    // The checksum field is treated as four zero bytes by the Ogg checksum.
    for (std::size_t i = 0; i < page.size(); ++i) {
        const unsigned char value = i >= 22 && i < 26 ? 0 : page[i];
        crc = (crc << 8) ^ ogg_crc_table[(crc >> 24) ^ value];
    }
    return crc;
}
struct OggExtent final {
    std::uint64_t frames{};
    AudioStatus status{AudioStatus::invalid_format};
};
// Validate complete framing before passing packets to the codec. Reject chained
// streams, missing end pages, damaged checksums and inconsistent continuation.
template<class Cancellation>
OggExtent inspect_ogg(std::span<const unsigned char> bytes, const Cancellation& cancellation) {
    std::size_t offset = 0;
    std::uint32_t serial = 0, sequence = 0;
    std::uint64_t previous_granule = 0;
    bool continued = false;
    while (offset < bytes.size()) {
        if (cancellation.requested()) { return {0, AudioStatus::cancelled}; }
        if (bytes.size() - offset < 27) { return {}; }
        const unsigned char* header = bytes.data() + offset;
        const unsigned flags = header[5];
        const std::size_t segments = header[26];
        if (!tag(header, "OggS") || header[4] != 0 || flags > 7 ||
            segments == 0 || bytes.size() - offset < 27 + segments) { return {}; }
        if (offset == 0) {
            if (flags != 2) { return {}; }
            serial = little32(header + 14);
        } else if ((flags & 2) != 0) { return {}; }
        if (little32(header + 14) != serial || little32(header + 18) != sequence ||
            ((flags & 1) != 0) != continued) { return {}; }
        ++sequence;
        std::size_t page_bytes = 27 + segments;
        for (std::size_t i = 0; i < segments; ++i) { page_bytes += header[27 + i]; }
        if (page_bytes > bytes.size() - offset) { return {}; }
        const std::span<const unsigned char> page(header, page_bytes);
        const std::uint32_t checksum = ogg_crc(page);
        if (checksum != little32(header + 22)) { return {}; }
        continued = header[26 + segments] == 255;
        const std::uint64_t granule = std::uint64_t(little32(header + 6)) |
                                      (std::uint64_t(little32(header + 10)) << 32);
        if (granule != std::numeric_limits<std::uint64_t>::max()) {
            if (granule < previous_granule || granule > maximum_frames) { return {}; }
            previous_granule = granule;
        }
        offset += page_bytes;
        if ((flags & 4) != 0) {
            if (offset != bytes.size() || continued || granule == 0 || granule > maximum_frames) { return {}; }
            return {granule, AudioStatus::ok};
        }
    }
    return {};
}
struct VorbisOwner final {
    // Member order keeps input and arena alive until the decoder has closed.
    std::unique_ptr<unsigned char[]> input{};
    std::unique_ptr<std::max_align_t[]> arena{};
    stb_vorbis* decoder{};
    VorbisOwner() = default;
    VorbisOwner(const VorbisOwner&) = delete;
    VorbisOwner& operator=(const VorbisOwner&) = delete;
    ~VorbisOwner() { if (decoder != nullptr) { stb_vorbis_close(decoder); } }
};
}

AudioClip::~AudioClip() { clip_bytes.fetch_sub(charged_bytes_); }
std::span<const float> AudioClip::samples() const noexcept {
    const std::span<const float> view(samples_.get(), sample_count_);
    return view;
}
std::uint64_t AudioClip::frames() const noexcept {
    const std::uint64_t count = sample_count_ / 2;
    return count;
}
AudioClipResult AudioClip::copy(std::span<const float> stereo) {
    if (stereo.empty() || stereo.size() % 2 != 0 || stereo.size_bytes() > maximum_clip_bytes) {
        return {{}, AudioStatus::invalid_format};
    }
    for (float sample : stereo) {
        if (!std::isfinite(sample) || sample < -1 || sample > 1) {
            return {{}, AudioStatus::invalid_value};
        }
    }
    std::uint64_t bytes = stereo.size_bytes();
    const bool accepted = charge_clip(bytes);
    if (!accepted) {
        return {{}, AudioStatus::quota_exceeded};
    }
    bool owned_charge = false;
    try {
        std::shared_ptr<AudioClip> clip(new AudioClip);
        AudioClip& owned = *clip;
        owned.charged_bytes_ = bytes;
        owned_charge = true;
        owned.samples_ = std::make_unique<float[]>(stereo.size());
        owned.sample_count_ = stereo.size();
        std::copy(stereo.begin(), stereo.end(), owned.samples_.get());
        AudioClipResult result{std::move(clip), AudioStatus::ok};
        return result;
    } catch (const std::bad_alloc&) {
        if (!owned_charge) {
            clip_bytes.fetch_sub(bytes);
        }
        return {{}, AudioStatus::allocation_failed};
    }
}

AudioClipResult AudioClip::load_wav(const std::filesystem::path& path, std::uint64_t trim_frames) {
    try {
        const std::optional<std::uint64_t> regular_bytes = gui_forms::platform::regular_file_bytes(path);
        if (!regular_bytes.has_value()) { return {{}, AudioStatus::file_error}; }
        const std::uint64_t file_bytes = *regular_bytes;
        if (file_bytes < 44 || file_bytes > file_limit) { return {{}, AudioStatus::invalid_format}; }
        // A file that cannot be opened reads nothing, which the RIFF check
        // refuses as before.
        stxlib::OperationResult<stxlib::File> opened = gui_forms::platform::open_file(path, stxlib::FileMode::read);
        if (opened.failed()) { return {{}, AudioStatus::invalid_format}; }
        stxlib::File& file = *opened.value();
        std::array<unsigned char, 64> header{};
        const bool riff_read = gui_forms::platform::read_exactly_at(file, 0, header.data(), 12);
        if (!riff_read || !tag(header.data(), "RIFF") || !tag(header.data() + 8, "WAVE") ||
            std::uint64_t(little32(header.data() + 4)) + 8 != file_bytes) {
            return {{}, AudioStatus::invalid_format};
        }
        std::uint64_t position = 12, data_offset = 0, data_bytes = 0;
        unsigned sample_bytes = 0;
        bool format_seen = false;
        while (position + 8 <= file_bytes) {
            const bool chunk_read = gui_forms::platform::read_exactly_at(file, position, header.data(), 8);
            std::uint64_t chunk_bytes = little32(header.data() + 4);
            if (!chunk_read || chunk_bytes > file_bytes - position - 8) {
                return {{}, AudioStatus::invalid_format};
            }
            if (tag(header.data(), "fmt ")) {
                if (format_seen || chunk_bytes < 16 || chunk_bytes > header.size()) {
                    return {{}, AudioStatus::invalid_format};
                }
                const bool format_read = gui_forms::platform::read_exactly_at(
                    file, position + 8, header.data(), static_cast<std::size_t>(chunk_bytes));
                unsigned encoding = little16(header.data());
                unsigned bits = little16(header.data() + 14);
                sample_bytes = bits / 8;
                if (!format_read || little16(header.data() + 2) != 2 || little32(header.data() + 4) != 48000 ||
                    !((encoding == 1 && bits == 16) || (encoding == 3 && bits == 32)) ||
                    little16(header.data() + 12) != sample_bytes * 2 ||
                    little32(header.data() + 8) != sample_bytes * 2 * 48000) {
                    return {{}, AudioStatus::invalid_format};
                }
                format_seen = true;
            } else if (tag(header.data(), "data")) {
                if (data_offset != 0) { return {{}, AudioStatus::invalid_format}; }
                data_offset = position + 8;
                data_bytes = chunk_bytes;
            }
            position += 8 + chunk_bytes + (chunk_bytes & 1);
        }
        if (!format_seen || data_offset == 0 || data_bytes == 0 || position != file_bytes ||
            data_bytes % (sample_bytes * 2) != 0) { return {{}, AudioStatus::invalid_format}; }
        std::uint64_t frames = data_bytes / (sample_bytes * 2);
        if (frames > maximum_frames || trim_frames > frames) { return {{}, AudioStatus::invalid_format}; }
        // One bounded conversion workspace, independent of the live-clip budget.
        // No whole encoded file allocation; extra workspace <=230,400,000 bytes.
        std::vector<float> decoded(static_cast<std::size_t>(frames * 2));
        std::uint64_t read_offset = data_offset;
        std::array<unsigned char, 8192> block{};
        using DecodeKernel = bool (*)(const unsigned char*, std::span<float>);
        const DecodeKernel decode = sample_bytes == 2 ? &decode_pcm16 : &decode_float32;
        std::size_t sample_index = 0;
        while (sample_index < decoded.size()) {
            std::size_t count = std::min(decoded.size() - sample_index, block.size() / sample_bytes);
            const std::size_t block_bytes = count * sample_bytes;
            if (!gui_forms::platform::read_exactly_at(file, read_offset, block.data(), block_bytes)) {
                return {{}, AudioStatus::file_error};
            }
            read_offset += block_bytes;
            std::span<float> destination(decoded.data() + sample_index, count);
            const bool valid = decode(block.data(), destination);
            if (!valid) { return {{}, AudioStatus::invalid_value}; }
            sample_index += count;
        }
        std::size_t count = static_cast<std::size_t>((trim_frames == 0 ? frames : trim_frames) * 2);
        const std::span<const float> trimmed(decoded.data(), count);
        AudioClipResult result = copy(trimmed);
        return result;
    } catch (const std::bad_alloc&) {
        return {{}, AudioStatus::allocation_failed};
    }
}

template<class Cancellation>
AudioClipResult AudioClip::load_ogg_impl(const std::filesystem::path& path, const Cancellation& cancellation) {
    std::uint64_t reserved_bytes = 0;
    bool owned_charge = false;
    try {
        if (cancellation.requested()) { return {{}, AudioStatus::cancelled}; }
        const std::optional<std::uint64_t> regular_bytes = gui_forms::platform::regular_file_bytes(path);
        if (!regular_bytes.has_value()) { return {{}, AudioStatus::file_error}; }
        const std::uint64_t file_bytes = *regular_bytes;
        if (file_bytes < 58 || file_bytes > ogg_file_limit) { return {{}, AudioStatus::invalid_format}; }
        VorbisOwner owner{};
        owner.input = std::make_unique<unsigned char[]>(static_cast<std::size_t>(file_bytes));
        stxlib::OperationResult<stxlib::File> opened = gui_forms::platform::open_file(path, stxlib::FileMode::read);
        if (opened.failed()) { return {{}, AudioStatus::file_error}; }
        stxlib::File& file = *opened.value();
        std::size_t offset = 0;
        while (offset < file_bytes) {
            if (cancellation.requested()) { return {{}, AudioStatus::cancelled}; }
            const std::size_t count = std::min<std::size_t>(65536, static_cast<std::size_t>(file_bytes) - offset);
            if (!gui_forms::platform::read_exactly_at(file, offset, owner.input.get() + offset, count)) {
                return {{}, AudioStatus::file_error};
            }
            offset += count;
        }
        // A file that grew while it was read is refused, as a byte past the
        // size taken first was.
        if (!gui_forms::platform::ends_at(file, offset)) { return {{}, AudioStatus::invalid_format}; }
        const std::span<const unsigned char> bytes(owner.input.get(), static_cast<std::size_t>(file_bytes));
        const OggExtent extent = inspect_ogg(bytes, cancellation);
        if (extent.status != AudioStatus::ok) { return {{}, extent.status}; }
        std::size_t arena_bytes = vorbis_arena_bytes;
#ifdef GUI_FORMS_AUDIO_TESTING
        arena_bytes = test_arena_bytes;
#endif
        owner.arena = std::make_unique<std::max_align_t[]>(arena_bytes / sizeof(std::max_align_t));
        const stb_vorbis_alloc allocation{reinterpret_cast<char*>(owner.arena.get()), static_cast<int>(arena_bytes)};
        int decoder_error = 0;
        {
            const stxlib::ScopedLock lock(vorbis_open_mutex());
            if (cancellation.requested()) { return {{}, AudioStatus::cancelled}; }
            owner.decoder = stb_vorbis_open_memory(owner.input.get(), static_cast<int>(file_bytes), &decoder_error, &allocation);
        }
        if (owner.decoder == nullptr) {
            const AudioStatus status = decoder_error == VORBIS_outofmem ? AudioStatus::allocation_failed : AudioStatus::invalid_format;
            return {{}, status};
        }
        const stb_vorbis_info info = stb_vorbis_get_info(owner.decoder);
        if (info.channels != 2 || info.sample_rate != 48000) { return {{}, AudioStatus::invalid_format}; }
        if (cancellation.requested()) { return {{}, AudioStatus::cancelled}; }
        reserved_bytes = extent.frames * 2 * sizeof(float);
        const bool accepted = charge_clip(reserved_bytes);
        if (!accepted) { reserved_bytes = 0; return {{}, AudioStatus::quota_exceeded}; }
        std::shared_ptr<AudioClip> clip(new AudioClip);
        AudioClip& owned = *clip;
        owned.charged_bytes_ = reserved_bytes;
        owned_charge = true;
        owned.sample_count_ = static_cast<std::size_t>(extent.frames * 2);
        owned.samples_ = std::make_unique<float[]>(owned.sample_count_);
        std::array<float, 8192> block{};
        std::uint64_t decoded_frames = 0;
        for (;;) {
            if (cancellation.requested()) { return {{}, AudioStatus::cancelled}; }
            const int frames = stb_vorbis_get_samples_float_interleaved(owner.decoder, 2, block.data(), static_cast<int>(block.size()));
            decoder_error = stb_vorbis_get_error(owner.decoder);
            if (decoder_error != VORBIS__no_error || frames < 0 || frames > 4096) { return {{}, AudioStatus::invalid_format}; }
            if (frames == 0) { break; }
            const std::uint64_t count = static_cast<std::uint64_t>(frames);
            if (count > maximum_frames - decoded_frames || count > extent.frames - decoded_frames) {
                return {{}, AudioStatus::invalid_format};
            }
            const std::size_t destination = static_cast<std::size_t>(decoded_frames * 2);
            const std::size_t sample_count = static_cast<std::size_t>(frames) * 2;
            for (std::size_t i = 0; i < sample_count; ++i) {
                if (!std::isfinite(block[i])) { return {{}, AudioStatus::invalid_value}; }
                owned.samples_[destination + i] = std::clamp(block[i], -1.0f, 1.0f);
            }
            decoded_frames += count;
#ifdef GUI_FORMS_AUDIO_TESTING
            if (test_cancel_after_chunk != nullptr) { static_cast<void>((*test_cancel_after_chunk).request_stop()); }
            if (test_flag_after_chunk != nullptr) { (*test_flag_after_chunk).request(); }
#endif
        }
        if (decoded_frames != extent.frames) { return {{}, AudioStatus::invalid_format}; }
        if (cancellation.requested()) { return {{}, AudioStatus::cancelled}; }
        AudioClipResult result{std::move(clip), AudioStatus::ok};
        return result;
    } catch (const std::bad_alloc&) {
        if (!owned_charge) { clip_bytes.fetch_sub(reserved_bytes); }
        return {{}, AudioStatus::allocation_failed};
    }
}

namespace {
struct LegacyAudioCancellation final {
    const stxlib::StopToken& token;
    [[nodiscard]] bool requested() const noexcept { return token.stop_requested(); }
};
} // namespace

AudioClipResult AudioClip::load_ogg(const std::filesystem::path& path,
                                  const stxlib::StopToken cancellation) {
    const LegacyAudioCancellation view{cancellation};
    return load_ogg_impl(path, view);
}

AudioClipResult AudioClip::load_ogg(const std::filesystem::path& path,
                                  const CancellationFlag& cancellation) {
    return load_ogg_impl(path, cancellation);
}

struct AudioEngineState final {
    ma_engine engine{};
    ma_device device{};
    std::array<AudioVoiceState*, 64> voices{}; // Non-owning, control-thread only.
#ifdef GUI_FORMS_AUDIO_LOOP_TRANSPORT
    std::array<AudioLoopTransportState*, 64> transports{};
    const stxlib::ThreadIdentity control_thread{stxlib::current_thread()};
#endif
    std::atomic<AudioStatus> status{AudioStatus::closed};
    bool initialized{false}, device_initialized{false}, offline{false};
    void callback_failure(AudioStatus failure) {
        AudioStatus expected = AudioStatus::ok;
        status.compare_exchange_strong(expected, failure);
    }
    void mix(std::span<float> stereo) {
        std::size_t offset = 0;
        while (offset < stereo.size()) {
            std::size_t count = std::min<std::size_t>(8192, stereo.size() - offset);
            ma_result result = ma_engine_read_pcm_frames(&engine, stereo.data() + offset, count / 2, nullptr);
            if (result != MA_SUCCESS) {
                callback_failure(AudioStatus::backend_error);
                std::fill(stereo.begin() + static_cast<std::ptrdiff_t>(offset), stereo.end(), 0);
                return;
            }
            for (std::size_t i = offset; i < offset + count; ++i) {
                stereo[i] = std::clamp(stereo[i], -1.0f, 1.0f);
            }
            offset += count;
        }
    }
    static void output(ma_device* device, void* output, const void*, ma_uint32 frames) {
        AudioEngineState& state = *static_cast<AudioEngineState*>((*device).pUserData);
        std::span<float> samples(static_cast<float*>(output), static_cast<std::size_t>(frames) * 2);
        // The backend fixed-size intermediary is configured to 256 frames.
        // Graph work is capped at 4096 frames. An unexpected larger native buffer
        // still requires a complete silence fill proportional to its supplied size.
        if (frames > 4096 || state.status.load() != AudioStatus::ok) {
            std::fill(samples.begin(), samples.end(), 0);
            if (frames > 4096) { state.callback_failure(AudioStatus::backend_error); }
            return;
        }
        state.mix(samples);
    }
    static void notification(const ma_device_notification* event) {
        AudioEngineState& state = *static_cast<AudioEngineState*>((*(*event).pDevice).pUserData);
        if ((*event).type == ma_device_notification_type_stopped ||
            (*event).type == ma_device_notification_type_interruption_began) {
            state.callback_failure(AudioStatus::device_unavailable);
        }
    }
};
#ifdef GUI_FORMS_AUDIO_LOOP_TRANSPORT
AudioLoopStatus loop_engine_status(const AudioEngineState& engine) noexcept {
    const AudioStatus observed = engine.status.load();
    if (observed == AudioStatus::ok) { return AudioLoopStatus::ok; }
    if (observed == AudioStatus::closed) { return AudioLoopStatus::closed; }
    // This profile has no device-specific result: interruption/unavailability
    // and mixer failure both mean the backend cannot advance transport time.
    return AudioLoopStatus::backend_error;
}
#ifdef GUI_FORMS_AUDIO_TESTING
void audio_loop_test_engine_failure(AudioEngineState& engine, AudioStatus failure) {
    engine.callback_failure(failure);
}
#endif
void release_loop_slot(AudioEngineState& engine, AudioLoopTransportState& transport, std::size_t slot) noexcept {
    if (engine.transports[slot] == &transport) { engine.transports[slot] = nullptr; }
}
#endif
// miniaudio reads a generator through this: the base must come first.
struct AudioGeneratorSource final {
    ma_data_source_base base{};
    AudioGenerator* generator{};
    static ma_result read(ma_data_source* source, void* output, ma_uint64 frames, ma_uint64* frames_read) {
        AudioGeneratorSource& self = *static_cast<AudioGeneratorSource*>(source);
        if (output != nullptr) {
            std::span<float> stereo(static_cast<float*>(output), static_cast<std::size_t>(frames) * 2);
            (*self.generator).render(stereo);
        }
        if (frames_read != nullptr) { *frames_read = frames; }
        return MA_SUCCESS;
    }
    static ma_result seek(ma_data_source*, ma_uint64) { return MA_SUCCESS; }
    static ma_result format(ma_data_source*, ma_format* sample_format, ma_uint32* channels, ma_uint32* rate,
                            ma_channel* channel_map, size_t channel_map_capacity) {
        if (sample_format != nullptr) { *sample_format = ma_format_f32; }
        if (channels != nullptr) { *channels = 2; }
        if (rate != nullptr) { *rate = 48000; }
        if (channel_map != nullptr) {
            ma_channel_map_init_standard(ma_standard_channel_map_default, channel_map, channel_map_capacity, 2);
        }
        return MA_SUCCESS;
    }
};
static ma_data_source_vtable audio_generator_vtable{
    &AudioGeneratorSource::read, &AudioGeneratorSource::seek, &AudioGeneratorSource::format,
    nullptr, nullptr, nullptr, 0};
struct AudioVoiceState final {
    std::shared_ptr<AudioEngineState> engine{};
    std::shared_ptr<const AudioClip> clip{};
    std::shared_ptr<AudioGenerator> generator{};
    AudioGeneratorSource source{};
    ma_audio_buffer buffer{};
    ma_sound sound{};
    std::size_t slot{};
    bool initialized{false};
    bool pitch_enabled{false};
    void close() {
        if (initialized) {
            // miniaudio detaches on the control thread and waits for graph readers.
            ma_sound_uninit(&sound);
            if (generator) {
                ma_data_source_uninit(&source.base);
                generator.reset();
            } else {
                ma_audio_buffer_uninit(&buffer);
            }
            initialized = false;
        }
        if (engine) { (*engine).voices[slot] = nullptr; }
    }
    ~AudioVoiceState() { close(); }
    bool usable() const {
        const bool available = initialized && (*engine).status.load() == AudioStatus::ok;
        return available;
    }
};

#ifdef GUI_FORMS_AUDIO_TESTING
void audio_test_decode_limits(std::size_t arena_bytes, std::uint64_t budget,
                              stxlib::StopSource* cancel_after_chunk) {
    test_arena_bytes = std::min(arena_bytes, vorbis_arena_bytes);
    test_clip_budget = std::min(budget, clip_budget);
    test_cancel_after_chunk = cancel_after_chunk;
}
void audio_test_cancel_flag_after_chunk(CancellationFlag* const flag) noexcept {
    test_flag_after_chunk = flag;
}
#ifndef GUI_FORMS_AUDIO_VORBIS_STX
bool audio_test_vorbis_seek_bounds() noexcept {
    std::array<unsigned char, 4> bytes{};
    stb_vorbis decoder{};
    decoder.stream_start = bytes.data();
    decoder.stream_end = bytes.data() + bytes.size();
    decoder.stream = bytes.data();
    if (set_file_offset(&decoder, 0U) != 1 || decoder.stream != bytes.data()) return false;
    if (set_file_offset(&decoder, 3U) != 1 || decoder.stream != bytes.data() + 3U) return false;
    if (set_file_offset(&decoder, 4U) != 0 || decoder.stream != decoder.stream_end || !decoder.eof) return false;
    if (set_file_offset(&decoder, std::numeric_limits<unsigned int>::max()) != 0 ||
        decoder.stream != decoder.stream_end || !decoder.eof) return false;
    decoder.stream_end = decoder.stream_start;
    const bool empty_rejected = set_file_offset(&decoder, 0U) == 0 && decoder.eof;
    return empty_rejected;
}
#endif
std::uint64_t audio_test_clip_bytes() {
    const std::uint64_t result = clip_bytes.load();
    return result;
}
// Private regression seam: model callback errors arriving after revocation.
AudioStatus audio_test_revoked_callback_status() {
    AudioEngineState state{};
    state.status.store(AudioStatus::closed);
    state.callback_failure(AudioStatus::backend_error);
    state.callback_failure(AudioStatus::device_unavailable);
    const AudioStatus result = state.status.load();
    return result;
}


#endif

AudioVoice::AudioVoice() = default;
AudioVoice::~AudioVoice() = default;
AudioVoice::AudioVoice(AudioVoice&&) noexcept = default;
AudioVoice& AudioVoice::operator=(AudioVoice&&) noexcept = default;
AudioStatus AudioVoice::play() {
    if (!state_ || !(*state_).usable()) { return AudioStatus::closed; }
    ma_result result = ma_sound_start(&(*state_).sound);
    const AudioStatus status = result == MA_SUCCESS ? AudioStatus::ok : AudioStatus::backend_error;
    return status;
}
AudioStatus AudioVoice::pause() {
    if (!state_ || !(*state_).usable()) { return AudioStatus::closed; }
    ma_result result = ma_sound_stop(&(*state_).sound);
    const AudioStatus status = result == MA_SUCCESS ? AudioStatus::ok : AudioStatus::backend_error;
    return status;
}
AudioStatus AudioVoice::stop() {
    AudioStatus result = pause();
    if (result != AudioStatus::ok) { return result; }
    ma_result seek = ma_sound_seek_to_pcm_frame(&(*state_).sound, 0);
    const AudioStatus status = seek == MA_SUCCESS ? AudioStatus::ok : AudioStatus::backend_error;
    return status;
}
AudioStatus AudioVoice::set_gain(double gain) {
    if (!std::isfinite(gain) || gain < 0 || gain > 1) { return AudioStatus::invalid_value; }
    if (!state_ || !(*state_).usable()) { return AudioStatus::closed; }
    ma_sound_set_volume(&(*state_).sound, static_cast<float>(gain));
    return AudioStatus::ok;
}
AudioStatus AudioVoice::set_rate(double rate) {
    if (!std::isfinite(rate) || rate < .25 || rate > 4) { return AudioStatus::invalid_value; }
    if (!state_ || !(*state_).usable()) { return AudioStatus::closed; }
    if (!(*state_).pitch_enabled && rate != 1) { return AudioStatus::invalid_value; }
    ma_sound_set_pitch(&(*state_).sound, static_cast<float>(rate));
    return AudioStatus::ok;
}
bool AudioVoice::playing() const {
    if (!state_) { return false; }
    const AudioVoiceState& state = *state_;
    const bool active = state.usable() && ma_sound_is_playing(&state.sound);
    return active;
}
AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { shutdown(); }
AudioStatus AudioEngine::open(bool offline) {
    if (state_ && (*state_).initialized) { return AudioStatus::invalid_value; }
    try { state_ = gui_forms::detail::make_library_shared<AudioEngineState>(); }
    catch (const std::bad_alloc&) { return AudioStatus::allocation_failed; }
    AudioEngineState& state = *state_;
    state.offline = offline;
    ma_engine_config config = ma_engine_config_init();
    config.noDevice = MA_TRUE;
    config.channels = 2;
    config.sampleRate = 48000;
    // No graph look-ahead cache: offline controls apply at the next render call.
    // The physical device separately supplies fixed 256-frame callback blocks.
    config.periodSizeInFrames = 0;
    const ma_result engine_result = ma_engine_init(&config, &state.engine);
    if (engine_result != MA_SUCCESS) {
        state.status.store(AudioStatus::backend_error);
        return AudioStatus::backend_error;
    }
    state.initialized = true;
    state.status.store(AudioStatus::ok);
    if (!offline) {
        ma_device_config device = ma_device_config_init(ma_device_type_playback);
        device.playback.format = ma_format_f32;
        device.playback.channels = 2;
        device.sampleRate = 48000;
        device.periodSizeInFrames = 256;
        device.noFixedSizedCallback = MA_FALSE;
        device.dataCallback = &AudioEngineState::output;
        device.notificationCallback = &AudioEngineState::notification;
        device.pUserData = &state;
        const ma_result device_result = ma_device_init(nullptr, &device, &state.device);
        if (device_result != MA_SUCCESS) {
            shutdown();
            state.status.store(AudioStatus::device_unavailable);
            return AudioStatus::device_unavailable;
        }
        state.device_initialized = true;
        const ma_result start_result = ma_device_start(&state.device);
        if (start_result != MA_SUCCESS) {
            shutdown();
            state.status.store(AudioStatus::device_unavailable);
        }
    }
    const AudioStatus result = state.status.load();
    return result;
}
AudioStatus AudioEngine::voice(std::shared_ptr<const AudioClip> clip, bool loop, AudioVoice& output,
                               bool pitch_enabled) {
    if (!state_ || status() != AudioStatus::ok) { return AudioStatus::closed; }
    if (!clip || (*clip).frames() == 0) { return AudioStatus::invalid_value; }
    AudioEngineState& engine = *state_;
    std::size_t slot = 0;
#ifdef GUI_FORMS_AUDIO_LOOP_TRANSPORT
    while (slot < engine.voices.size() && (engine.voices[slot] != nullptr || engine.transports[slot] != nullptr)) { ++slot; }
#else
    while (slot < engine.voices.size() && engine.voices[slot] != nullptr) { ++slot; }
#endif
    if (slot == engine.voices.size()) { return AudioStatus::quota_exceeded; }
    try {
        std::unique_ptr<AudioVoiceState> candidate = std::make_unique<AudioVoiceState>();
        AudioVoiceState& state = *candidate;
        state.engine = state_;
        state.clip = std::move(clip);
        state.slot = slot;
        state.pitch_enabled = pitch_enabled;
        ma_audio_buffer_config config = ma_audio_buffer_config_init(ma_format_f32, 2, (*state.clip).frames(),
                                                                    (*state.clip).samples().data(), nullptr);
        const ma_result buffer_result = ma_audio_buffer_init(&config, &state.buffer);
        if (buffer_result != MA_SUCCESS) { return AudioStatus::backend_error; }
        ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
        if (!pitch_enabled) { flags |= MA_SOUND_FLAG_NO_PITCH; }
        const ma_result sound_result = ma_sound_init_from_data_source(&engine.engine, &state.buffer, flags, nullptr, &state.sound);
        if (sound_result != MA_SUCCESS) {
            ma_audio_buffer_uninit(&state.buffer);
            return AudioStatus::backend_error;
        }
        state.initialized = true;
        ma_sound_set_looping(&state.sound, loop ? MA_TRUE : MA_FALSE);
        output.state_ = std::move(candidate);
        engine.voices[slot] = &(*output.state_);
        return AudioStatus::ok;
    } catch (const std::bad_alloc&) { return AudioStatus::allocation_failed; }
}
AudioStatus AudioEngine::generator(std::shared_ptr<AudioGenerator> source, AudioVoice& output) {
    if (!state_ || status() != AudioStatus::ok) { return AudioStatus::closed; }
    if (!source) { return AudioStatus::invalid_value; }
    AudioEngineState& engine = *state_;
    std::size_t slot = 0;
#ifdef GUI_FORMS_AUDIO_LOOP_TRANSPORT
    while (slot < engine.voices.size() && (engine.voices[slot] != nullptr || engine.transports[slot] != nullptr)) { ++slot; }
#else
    while (slot < engine.voices.size() && engine.voices[slot] != nullptr) { ++slot; }
#endif
    if (slot == engine.voices.size()) { return AudioStatus::quota_exceeded; }
    try {
        std::unique_ptr<AudioVoiceState> candidate = std::make_unique<AudioVoiceState>();
        AudioVoiceState& state = *candidate;
        state.engine = state_;
        state.slot = slot;
        state.source.generator = &(*source);
        ma_data_source_config config = ma_data_source_config_init();
        config.vtable = &audio_generator_vtable;
        const ma_result source_result = ma_data_source_init(&config, &state.source.base);
        if (source_result != MA_SUCCESS) { return AudioStatus::backend_error; }
        const ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
        const ma_result sound_result = ma_sound_init_from_data_source(&engine.engine, &state.source, flags, nullptr, &state.sound);
        if (sound_result != MA_SUCCESS) {
            ma_data_source_uninit(&state.source.base);
            return AudioStatus::backend_error;
        }
        state.generator = std::move(source);
        state.initialized = true;
        output.state_ = std::move(candidate);
        engine.voices[slot] = &(*output.state_);
        return AudioStatus::ok;
    } catch (const std::bad_alloc&) { return AudioStatus::allocation_failed; }
}
AudioStatus AudioEngine::render(std::span<float> stereo) {
    if (!state_ || status() != AudioStatus::ok) { return AudioStatus::closed; }
    if (!(*state_).offline || stereo.size() % 2 != 0) { return AudioStatus::invalid_value; }
    (*state_).mix(stereo);
    const AudioStatus result = status();
    return result;
}
#ifdef GUI_FORMS_AUDIO_LOOP_TRANSPORT
AudioLoopStatus AudioEngine::loop_transport(AudioLoopTransport& output) {
    if (!state_ || status() != AudioStatus::ok) { return AudioLoopStatus::closed; }
    AudioEngineState& engine = *state_;
    if (stxlib::current_thread() != engine.control_thread) { return AudioLoopStatus::wrong_thread; }
    std::size_t slot{};
    while (slot < engine.voices.size() && (engine.voices[slot] != nullptr || engine.transports[slot] != nullptr)) { ++slot; }
    if (slot == engine.voices.size()) { return AudioLoopStatus::quota_exceeded; }
    static std::atomic<std::uint64_t> next_epoch{1};
    std::uint64_t epoch = next_epoch.load();
    while (epoch != 0) {
        const std::uint64_t next = epoch == std::numeric_limits<std::uint64_t>::max() ? 0 : epoch + 1;
        const bool assigned = next_epoch.compare_exchange_weak(epoch, next);
        if (assigned) { break; }
    }
    if (epoch == 0) { return AudioLoopStatus::overflow; }
    try {
        std::unique_ptr<AudioLoopTransportState> candidate = std::make_unique<AudioLoopTransportState>(epoch);
        (*candidate).engine = state_;
        (*candidate).engine_slot = slot;
        const AudioLoopStatus initialized = (*candidate).initialize(engine.engine);
        if (initialized != AudioLoopStatus::ok) { return initialized; }
        engine.transports[slot] = &(*candidate);
        output.state_ = std::move(candidate);
        return AudioLoopStatus::ok;
    } catch (const std::bad_alloc&) { return AudioLoopStatus::quota_exceeded; }
}
#endif
AudioStatus AudioEngine::status() const noexcept {
    const AudioStatus result = state_ ? (*state_).status.load() : AudioStatus::closed;
    return result;
}
void AudioEngine::shutdown() {
    if (!state_) { return; }
    AudioEngineState& state = *state_;
    state.status.store(AudioStatus::closed);
    if (state.device_initialized) {
        ma_device_uninit(&state.device);
        state.device_initialized = false;
    }
    for (AudioVoiceState* voice : state.voices) {
        if (voice != nullptr) { (*voice).close(); }
    }
#ifdef GUI_FORMS_AUDIO_LOOP_TRANSPORT
    for (AudioLoopTransportState* transport : state.transports) {
        if (transport != nullptr) { (*transport).close(); }
    }
#endif
    if (state.initialized) {
        ma_engine_uninit(&state.engine);
        state.initialized = false;
    }
    state.status.store(AudioStatus::closed);
}
}
