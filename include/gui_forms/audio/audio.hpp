#pragma once
#include "gui_forms/threading.hpp"
#include "gui_forms/types/platform_types/platform_types.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>
#ifdef GUI_FORMS_AUDIO_LOOP_TRANSPORT
#include "gui_forms/audio/loop_transport/loop_transport.hpp"
#endif

namespace gui_forms {
enum class AudioStatus {
    ok, invalid_format, invalid_value, file_error, allocation_failed,
    quota_exceeded, device_unavailable, backend_error, closed, cancelled
};
class AudioClip;
struct AudioClipResult final {
    std::shared_ptr<const AudioClip> clip{};
    AudioStatus status{AudioStatus::ok};
};
// Immutable interleaved L,R samples, 48 kHz. Up to 600 seconds per clip;
// aggregate live clip payload is limited to 512 MiB, including external owners.
class AudioClip final {
public:
    ~AudioClip();
    AudioClip(const AudioClip&) = delete;
    AudioClip& operator=(const AudioClip&) = delete;
    [[nodiscard]] static AudioClipResult copy(std::span<const float> stereo);
    // Synchronous bounded file I/O: never call on the device callback or a UI
    // latency-sensitive path. Little-endian RIFF PCM16/float32 stereo 48 kHz only.
    [[nodiscard]] static AudioClipResult load_wav(const FilePath& path,
                                  std::uint64_t trim_frames = 0);
    // Single-stream Ogg Vorbis, stereo 48 kHz, <=64 MiB encoded input and
    // <=600 seconds decoded. Owns input plus a 16 MiB codec arena until close.
    // Output is charged to the live-clip budget before allocation. Vorbis
    // overshoot is saturated to [-1,1]. Failure publishes no partial clip.
    // Synchronous: use a background executor. Cancellation is observed between
    // reads, pages and <=4096-frame decode calls and before publication; foreign
    // codec open/decode calls and the serialized open wait cannot be interrupted.
    [[nodiscard]] static AudioClipResult load_ogg(const FilePath& path,
                                  std::stop_token cancellation = {});
    // Preferred cancellation form; borrows the flag until this call returns.
    [[nodiscard]] static AudioClipResult load_ogg(const FilePath& path,
                                  const CancellationFlag& cancellation);
    std::span<const float> samples() const noexcept;
    std::uint64_t frames() const noexcept;
private:
    template<class Cancellation>
    static AudioClipResult load_ogg_impl(const FilePath& path,
                                       const Cancellation& cancellation);
    AudioClip() = default;
    std::unique_ptr<float[]> samples_{};
    std::size_t sample_count_{};
    std::uint64_t charged_bytes_{};
};

// A live source: the mixer pulls interleaved L,R 48 kHz frames from it instead of
// reading a clip. render() runs on the device callback (or inside offline
// AudioEngine::render), never on the control thread: it must fill the whole span
// with finite samples and must not block, allocate, lock or throw. Hand it its
// controls through atomics. It is mixed like a clip and the mix is clamped to
// [-1,1]. It is only pulled while its voice plays; a paused voice freezes it.
#define GUI_FORMS_AUDIO_GENERATOR 1
class AudioGenerator {
public:
    virtual void render(std::span<float> stereo) noexcept = 0;
protected:
    AudioGenerator() = default;
    ~AudioGenerator() = default;
};

struct AudioEngineState;
struct AudioVoiceState;
class AudioVoice final {
public:
    AudioVoice();
    ~AudioVoice();
    AudioVoice(AudioVoice&&) noexcept;
    AudioVoice& operator=(AudioVoice&&) noexcept;
    AudioVoice(const AudioVoice&) = delete;
    AudioVoice& operator=(const AudioVoice&) = delete;
    AudioStatus play(); // Resume; at EOF seek to zero before replaying.
    AudioStatus pause(); // Retain cursor.
    AudioStatus stop(); // Pause and rewind to frame zero. A generator only pauses.
    AudioStatus set_gain(double gain); // Finite [0,1]. Mix clips to [-1,1].
    AudioStatus set_rate(double rate); // Finite [.25,4], linear interpolation.
    bool playing() const;
private:
    friend class AudioEngine;
    std::unique_ptr<AudioVoiceState> state_{};
};

// All public engine/voice operations belong to one control thread. The device
// callback is private. Offline render is exclusive with controls and device use.
// At most 64 live voice objects, including paused/stopped voices. Files are not
// streamed; a generator voice synthesizes as it plays.
class AudioEngine final {
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioStatus open(bool offline = false);
    // Pitch-enabled voices use a linear interpolator with one input-frame latency.
    // Fixed-rate voices bypass it, preserving exact PCM sample positions.
    AudioStatus voice(std::shared_ptr<const AudioClip> clip, bool loop, AudioVoice& output,
                      bool pitch_enabled = false);
    // Fixed rate, no seek, no loop point. The voice shares ownership of the source
    // and releases it, after the mixer has stopped reading, when closed.
    AudioStatus generator(std::shared_ptr<AudioGenerator> source, AudioVoice& output);
#ifdef GUI_FORMS_AUDIO_LOOP_TRANSPORT
    [[nodiscard]] AudioLoopStatus loop_transport(AudioLoopTransport& output);
#endif
    AudioStatus render(std::span<float> stereo);
    AudioStatus status() const noexcept;
    // Immediately stop output, quiesce device callback, then release backend
    // voices. Held voice handles become closed. No audible drain or auto-reopen.
    void shutdown();
private:
    std::shared_ptr<AudioEngineState> state_{};
};
}
