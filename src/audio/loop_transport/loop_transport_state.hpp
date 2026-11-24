#pragma once
#include "gui_forms/audio/audio.hpp"
#include "scheduler.hpp"
#include "miniaudio.h"
#include <array>
#include <atomic>
#include <stxlib/threads.hpp>

namespace gui_forms {
struct AudioEngineState;
struct AudioLoopTransportState;
// Concrete engine bridge remains in audio.cpp where its owner is complete.
AudioLoopStatus loop_engine_status(const AudioEngineState& engine) noexcept;
void release_loop_slot(AudioEngineState& engine, AudioLoopTransportState& transport, std::size_t slot) noexcept;
struct LoopDataSource final {
    ma_data_source_base base{}; // Foreign API requires this first member.
    AudioLoopTransportState* owner{};
};
enum class LoopSlotOwner { free, published, retired };
enum class LoopReceiptOwner { free, queued, callback, terminal };
struct LoopPayload final {
    std::atomic<LoopSlotOwner> owner{LoopSlotOwner::free};
    // Assigned/released only by control; descriptor immutable until retirement.
    std::shared_ptr<const AudioClip> clip{};
    const float* samples{};
    audio_experiment::ClipShape shape{};
};
struct LoopReceiptCell final {
    std::atomic<LoopReceiptOwner> owner{LoopReceiptOwner::free};
    std::atomic<std::uint64_t> sequence{}, id{}, epoch{}, admission{}, application{};
    std::atomic<AudioLoopPhase> phase{AudioLoopPhase::queued};
    std::atomic<AudioLoopStatus> reason{AudioLoopStatus::ok};
};
struct LoopCommandCell final {
    audio_experiment::CommandKind kind{};
    std::uint64_t id{}, target{};
    std::size_t payload{32}, receipt{};
    audio_experiment::ChangeTiming timing{};
    double gain{1};
};
struct AudioLoopTransportState final {
    explicit AudioLoopTransportState(std::uint64_t epoch);
    ~AudioLoopTransportState();
    std::shared_ptr<AudioEngineState> engine{};
    std::size_t engine_slot{};
    LoopDataSource source{};
    ma_sound sound{};
    ma_engine* native_engine{}; // Borrowed while engine owns this graph.
    bool source_initialized{}, sound_initialized{}; // Control thread only.
    const stxlib::ThreadIdentity control_thread;
    const std::uint64_t epoch;
    std::atomic<bool> closing{};
    std::atomic<AudioLoopStatus> failure{AudioLoopStatus::ok};
    std::atomic<std::uint64_t> rendered_frame{};
    std::array<LoopPayload, 32> payloads{};
    std::array<LoopReceiptCell, 64> receipts{};
    std::array<LoopCommandCell, 16> commands{};
    std::atomic<std::uint64_t> written{}, read{};
    std::uint64_t next_id{1}; // Control thread only; zero means exhausted.
    std::size_t receipt_cursor{};
    audio_experiment::LoopScheduler scheduler; // Callback exclusive until detach.
    std::array<std::size_t, 32> payload_map{}; // Model index to actual slot.
    std::array<AudioLoopPhase, 64> last_phase{}; // Callback-only publication cache.
    [[nodiscard]] AudioLoopStatus initialize(ma_engine& native_engine);
    [[nodiscard]] AudioLoopStatus status() const noexcept;
    [[nodiscard]] AudioLoopCommand enqueue(LoopCommandCell command,
                                          std::shared_ptr<const AudioClip> clip = {});
    [[nodiscard]] AudioLoopReceipt poll(std::uint64_t id);
    void collect() noexcept;
    void render(float* samples, std::size_t frames) noexcept;
    void ingest() noexcept;
    void publish_receipts() noexcept;
    void retire_payloads() noexcept;
    void close() noexcept;
};
#ifdef GUI_FORMS_AUDIO_TESTING
void audio_loop_test_engine_failure(AudioEngineState& engine, AudioStatus failure);
// Simulates the engine's private render thread; caller keeps engine alive until
// joined and never overlaps any public offline render or native device use.
AudioLoopStatus audio_loop_test_native_render(AudioLoopTransport& transport, std::span<float> samples);
#endif
}
