#include "loop_transport_state.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stxlib/threads.hpp>
#include <utility>

namespace gui_forms {
namespace {
constexpr std::uint64_t maximum_counter = std::numeric_limits<std::uint64_t>::max();
AudioLoopPhase public_phase(audio_experiment::RequestPhase phase) noexcept {
    using P = audio_experiment::RequestPhase;
    switch (phase) {
    case P::queued: return AudioLoopPhase::queued;
    case P::admitted: return AudioLoopPhase::admitted;
    case P::applied: return AudioLoopPhase::applied;
    case P::cancelled: return AudioLoopPhase::cancelled;
    case P::replaced: return AudioLoopPhase::replaced;
    case P::too_late: return AudioLoopPhase::too_late;
    case P::expired_target: return AudioLoopPhase::expired_target;
    case P::rejected: return AudioLoopPhase::rejected;
    default: return AudioLoopPhase::closed;
    }
}
void write_receipt(LoopReceiptCell& cell, const audio_experiment::Receipt& receipt) noexcept {
    const std::uint64_t sequence = cell.sequence.load(std::memory_order_seq_cst);
    cell.sequence.store(sequence + 1, std::memory_order_seq_cst);
    cell.admission.store(receipt.admission_frame, std::memory_order_seq_cst);
    cell.application.store(receipt.application_frame, std::memory_order_seq_cst);
    const AudioLoopPhase phase = public_phase(receipt.phase);
    cell.phase.store(phase, std::memory_order_seq_cst);
    const AudioLoopStatus reason = receipt.reason == audio_experiment::ScheduleStatus::overflow ?
        AudioLoopStatus::overflow : AudioLoopStatus::ok;
    cell.reason.store(reason, std::memory_order_seq_cst);
    cell.sequence.store(sequence + 2, std::memory_order_seq_cst);
    if (phase != AudioLoopPhase::admitted && phase != AudioLoopPhase::queued) {
        // Last callback write: producer may now reclaim this receipt.
        cell.owner.store(LoopReceiptOwner::terminal, std::memory_order_release);
    }
}
ma_result loop_read(ma_data_source* raw, void* output, ma_uint64 count, ma_uint64* read) {
    LoopDataSource& source = *static_cast<LoopDataSource*>(raw);
    if (output == nullptr || count > 4096) { return MA_INVALID_ARGS; }
    (*source.owner).render(static_cast<float*>(output), static_cast<std::size_t>(count));
    if (read != nullptr) { *read = count; }
    return MA_SUCCESS;
}
ma_result loop_format(ma_data_source*, ma_format* format, ma_uint32* channels,
                     ma_uint32* rate, ma_channel* map, size_t capacity) {
    if (format != nullptr) { *format = ma_format_f32; }
    if (channels != nullptr) { *channels = 2; }
    if (rate != nullptr) { *rate = 48000; }
    if (map != nullptr && capacity >= 2) { map[0] = MA_CHANNEL_FRONT_LEFT; map[1] = MA_CHANNEL_FRONT_RIGHT; }
    return MA_SUCCESS;
}
ma_result loop_cursor(ma_data_source* raw, ma_uint64* position) {
    if (position == nullptr) { return MA_INVALID_ARGS; }
    LoopDataSource& source = *static_cast<LoopDataSource*>(raw);
    *position = (*source.owner).rendered_frame.load(std::memory_order_acquire);
    return MA_SUCCESS;
}
const ma_data_source_vtable loop_vtable{&loop_read, nullptr, &loop_format, &loop_cursor, nullptr, nullptr, 0};
}
AudioLoopTransportState::AudioLoopTransportState(std::uint64_t identity)
    : control_thread(stxlib::current_thread()), epoch(identity), scheduler(identity) {
    payload_map.fill(32);
}
AudioLoopTransportState::~AudioLoopTransportState() { close(); }
AudioLoopStatus AudioLoopTransportState::initialize(ma_engine& engine_node) {
    const bool lock_free = std::atomic<std::uint64_t>::is_always_lock_free &&
        std::atomic<LoopSlotOwner>::is_always_lock_free && std::atomic<LoopReceiptOwner>::is_always_lock_free &&
        std::atomic<AudioLoopPhase>::is_always_lock_free && std::atomic<AudioLoopStatus>::is_always_lock_free &&
        std::atomic<bool>::is_always_lock_free;
    if (!lock_free) { return AudioLoopStatus::unsupported; }
    native_engine = &engine_node;
    ma_data_source_config config = ma_data_source_config_init();
    config.vtable = &loop_vtable;
    source.owner = this;
    const ma_result source_result = ma_data_source_init(&config, &source.base);
    if (source_result != MA_SUCCESS) { return AudioLoopStatus::backend_error; }
    source_initialized = true;
    const ma_result sound_result = ma_sound_init_from_data_source(&engine_node, &source.base,
        MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH, nullptr, &sound);
    if (sound_result != MA_SUCCESS) { return AudioLoopStatus::backend_error; }
    sound_initialized = true;
    const ma_result started = ma_sound_start(&sound);
    const AudioLoopStatus result = started == MA_SUCCESS ? AudioLoopStatus::ok : AudioLoopStatus::backend_error;
    return result;
}
AudioLoopStatus AudioLoopTransportState::status() const noexcept {
    if (stxlib::current_thread() != control_thread) { return AudioLoopStatus::wrong_thread; }
    if (closing.load(std::memory_order_acquire) || !engine) { return AudioLoopStatus::closed; }
    const AudioLoopStatus failed = failure.load(std::memory_order_acquire);
    if (failed != AudioLoopStatus::ok) { return failed; }
    const AudioLoopStatus result = loop_engine_status(*engine);
    return result;
}
void AudioLoopTransportState::collect() noexcept {
    for (LoopPayload& payload : payloads) {
        const LoopSlotOwner owner = payload.owner.load(std::memory_order_acquire);
        if (owner == LoopSlotOwner::retired) {
            std::shared_ptr<const AudioClip> retired = std::move(payload.clip);
            payload.samples = nullptr;
            payload.shape = {};
            payload.owner.store(LoopSlotOwner::free, std::memory_order_release);
            retired.reset();
        }
    }
}
AudioLoopCommand AudioLoopTransportState::enqueue(LoopCommandCell item, std::shared_ptr<const AudioClip> clip) {
    const AudioLoopStatus ready = status();
    if (ready != AudioLoopStatus::ok) { return {ready, 0}; }
    collect();
    const std::uint64_t write = written.load(std::memory_order_relaxed);
    const std::uint64_t consumed = read.load(std::memory_order_acquire);
    if (next_id == 0 || write == maximum_counter) { return {AudioLoopStatus::overflow, 0}; }
    if (write - consumed >= commands.size()) { return {AudioLoopStatus::quota_exceeded, 0}; }
    std::size_t receipt = receipts.size();
    bool sequence_available{};
    for (std::size_t i = 0; i < receipts.size(); ++i) {
        const std::size_t candidate = (receipt_cursor + i) % receipts.size();
        const LoopReceiptOwner owner = receipts[candidate].owner.load(std::memory_order_acquire);
        const std::uint64_t sequence = receipts[candidate].sequence.load(std::memory_order_relaxed);
        if (sequence <= maximum_counter - 6) { sequence_available = true; }
        // Reserve queued, admitted and terminal publications without wrap/ABA.
        if ((owner == LoopReceiptOwner::free || owner == LoopReceiptOwner::terminal) && sequence <= maximum_counter - 6) {
            receipt = candidate; break;
        }
    }
    if (receipt == receipts.size()) {
        const AudioLoopStatus exhausted = sequence_available ? AudioLoopStatus::quota_exceeded : AudioLoopStatus::overflow;
        return {exhausted, 0};
    }
    if (item.kind == audio_experiment::CommandKind::change) {
        std::size_t slot{};
        while (slot < payloads.size() && payloads[slot].owner.load(std::memory_order_acquire) != LoopSlotOwner::free) { ++slot; }
        if (slot == payloads.size()) { return {AudioLoopStatus::quota_exceeded, 0}; }
        LoopPayload& payload = payloads[slot];
        payload.clip = std::move(clip);
        payload.samples = (*payload.clip).samples().data();
        payload.shape.frames = (*payload.clip).frames();
        payload.shape.bar_frames = item.target;
        payload.owner.store(LoopSlotOwner::published, std::memory_order_release);
        item.payload = slot;
    }
    item.id = next_id;
    next_id = next_id == maximum_counter ? 0 : next_id + 1;
    item.receipt = receipt;
    LoopReceiptCell& cell = receipts[receipt];
    const std::uint64_t sequence = cell.sequence.load(std::memory_order_seq_cst);
    cell.sequence.store(sequence + 1, std::memory_order_seq_cst);
    cell.id.store(item.id, std::memory_order_seq_cst);
    cell.epoch.store(epoch, std::memory_order_seq_cst);
    cell.admission.store(0, std::memory_order_seq_cst);
    cell.application.store(0, std::memory_order_seq_cst);
    cell.phase.store(AudioLoopPhase::queued, std::memory_order_seq_cst);
    cell.reason.store(AudioLoopStatus::ok, std::memory_order_seq_cst);
    cell.sequence.store(sequence + 2, std::memory_order_seq_cst);
    cell.owner.store(LoopReceiptOwner::queued, std::memory_order_release);
    receipt_cursor = (receipt + 1) % receipts.size();
    commands[static_cast<std::size_t>(write % commands.size())] = item;
    written.store(write + 1, std::memory_order_release);
    const AudioLoopCommand result{AudioLoopStatus::ok, item.id};
    return result;
}
AudioLoopReceipt AudioLoopTransportState::poll(std::uint64_t id) {
    if (stxlib::current_thread() != control_thread) {
        AudioLoopReceipt result{}; result.status = AudioLoopStatus::wrong_thread; return result;
    }
    collect();
    for (LoopReceiptCell& cell : receipts) {
        if (cell.id.load(std::memory_order_seq_cst) != id || id == 0) { continue; }
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            const std::uint64_t before = cell.sequence.load(std::memory_order_seq_cst);
            if ((before & 1) != 0) { continue; }
            AudioLoopReceipt result{};
            result.status = AudioLoopStatus::ok;
            result.id = cell.id.load(std::memory_order_seq_cst);
            result.epoch = cell.epoch.load(std::memory_order_seq_cst);
            result.admission_frame = cell.admission.load(std::memory_order_seq_cst);
            result.application_frame = cell.application.load(std::memory_order_seq_cst);
            result.phase = cell.phase.load(std::memory_order_seq_cst);
            result.reason = cell.reason.load(std::memory_order_seq_cst);
            const std::uint64_t after = cell.sequence.load(std::memory_order_seq_cst);
            if (before == after && result.id == id) {
                if (result.phase == AudioLoopPhase::queued || result.phase == AudioLoopPhase::admitted) {
                    AudioLoopStatus failed = failure.load(std::memory_order_acquire);
                    if (failed == AudioLoopStatus::ok) { failed = loop_engine_status(*engine); }
                    if (failed != AudioLoopStatus::ok) { result.status = failed; result.reason = failed; }
                }
                return result;
            }
        }
        AudioLoopReceipt busy{}; busy.status = AudioLoopStatus::busy; return busy;
    }
    return {};
}
void AudioLoopTransportState::publish_receipts() noexcept {
    for (std::size_t i = 0; i < receipts.size(); ++i) {
        LoopReceiptCell& cell = receipts[i];
        if (cell.owner.load(std::memory_order_acquire) != LoopReceiptOwner::callback) { continue; }
        const std::uint64_t id = cell.id.load(std::memory_order_seq_cst);
        const audio_experiment::Receipt receipt = scheduler.poll(id);
        const AudioLoopPhase phase = public_phase(receipt.phase);
        if (phase != last_phase[i]) {
            last_phase[i] = phase;
            write_receipt(cell, receipt);
        }
    }
}
void AudioLoopTransportState::retire_payloads() noexcept {
    for (std::size_t i = 0; i < payload_map.size(); ++i) {
        const audio_experiment::PayloadPhase phase = scheduler.payload_phase(i);
        if (phase != audio_experiment::PayloadPhase::retired || payload_map[i] == 32) { continue; }
        const std::size_t actual = payload_map[i];
        payload_map[i] = 32;
        payloads[actual].owner.store(LoopSlotOwner::retired, std::memory_order_release);
    }
    static_cast<void>(scheduler.collect_retired());
}
void AudioLoopTransportState::ingest() noexcept {
    const std::uint64_t available = written.load(std::memory_order_acquire);
    std::uint64_t consumed = read.load(std::memory_order_relaxed);
    while (consumed < available) {
        const LoopCommandCell item = commands[static_cast<std::size_t>(consumed % commands.size())];
        receipts[item.receipt].owner.store(LoopReceiptOwner::callback, std::memory_order_relaxed);
        last_phase[item.receipt] = AudioLoopPhase::queued;
        audio_experiment::RequestResult admitted{};
        if (item.kind == audio_experiment::CommandKind::change) {
            const LoopPayload& payload = payloads[item.payload];
            admitted = scheduler.change(payload.shape, item.timing);
            if (admitted.status == audio_experiment::ScheduleStatus::ok) { payload_map[admitted.payload] = item.payload; }
        } else {
            admitted = scheduler.command(item.kind, item.gain, item.target);
        }
        if (admitted.status != audio_experiment::ScheduleStatus::ok || admitted.id != item.id) {
            failure.store(AudioLoopStatus::backend_error, std::memory_order_release);
            return;
        }
        scheduler.ingest();
        if (item.kind == audio_experiment::CommandKind::cancel) {
            // The public receipt retention policy is authoritative. The serial
            // model's independent ring must not infer history already expired
            // from the control-visible ring, or forget retained terminal history.
            bool known{};
            bool complete{};
            for (LoopReceiptCell& target : receipts) {
                if (target.id.load(std::memory_order_seq_cst) != item.target) { continue; }
                known = true;
                complete = target.owner.load(std::memory_order_acquire) == LoopReceiptOwner::terminal;
                break;
            }
            if (!known || complete) {
                audio_experiment::Receipt receipt = scheduler.poll(item.id);
                // A successful cancellation just made its target terminal; keep
                // that applied outcome rather than reclassifying it as too late.
                if (receipt.phase != audio_experiment::RequestPhase::applied) {
                    receipt.phase = known ? audio_experiment::RequestPhase::too_late : audio_experiment::RequestPhase::expired_target;
                    write_receipt(receipts[item.receipt], receipt);
                }
            }
        }
        publish_receipts();
        retire_payloads();
        ++consumed;
        read.store(consumed, std::memory_order_release);
    }
}
void AudioLoopTransportState::render(float* output, std::size_t frames) noexcept {
    if (closing.load(std::memory_order_acquire) || failure.load(std::memory_order_acquire) != AudioLoopStatus::ok) {
        std::fill_n(output, frames * 2, 0.0f); return;
    }
    ingest();
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const audio_experiment::SamplePlan plan = scheduler.next_sample();
        double left{}, right{};
        if (plan.current != 32) {
            const LoopPayload& payload = payloads[payload_map[plan.current]];
            const std::size_t offset = static_cast<std::size_t>(plan.current_position) * 2;
            left = static_cast<double>(payload.samples[offset]) * plan.current_gain;
            right = static_cast<double>(payload.samples[offset + 1]) * plan.current_gain;
        }
        if (plan.outgoing != 32) {
            const LoopPayload& payload = payloads[payload_map[plan.outgoing]];
            const std::size_t offset = static_cast<std::size_t>(plan.outgoing_position) * 2;
            left += static_cast<double>(payload.samples[offset]) * plan.outgoing_gain;
            right += static_cast<double>(payload.samples[offset + 1]) * plan.outgoing_gain;
        }
        output[frame * 2] = static_cast<float>(left);
        output[frame * 2 + 1] = static_cast<float>(right);
    }
    publish_receipts();
    retire_payloads();
    rendered_frame.store(scheduler.frame(), std::memory_order_release);
    if (scheduler.closed()) { failure.store(AudioLoopStatus::overflow, std::memory_order_release); }
}
void AudioLoopTransportState::close() noexcept {
    closing.store(true, std::memory_order_release);
    if (sound_initialized) { ma_sound_uninit(&sound); sound_initialized = false; }
    if (source_initialized) { ma_data_source_uninit(&source.base); source_initialized = false; }
    // Graph readers have detached. Only the control thread now touches model.
    scheduler.shutdown();
    for (LoopReceiptCell& cell : receipts) {
        const LoopReceiptOwner owner = cell.owner.load(std::memory_order_acquire);
        if (owner == LoopReceiptOwner::queued || owner == LoopReceiptOwner::callback) {
            audio_experiment::Receipt receipt{};
            receipt.phase = audio_experiment::RequestPhase::closed;
            receipt.application_frame = scheduler.frame();
            write_receipt(cell, receipt);
        }
    }
    for (LoopPayload& payload : payloads) {
        payload.clip.reset(); payload.samples = nullptr;
        payload.owner.store(LoopSlotOwner::free, std::memory_order_release);
    }
    if (engine) { release_loop_slot(*engine, *this, engine_slot); }
}
AudioLoopTransport::AudioLoopTransport() = default;
AudioLoopTransport::~AudioLoopTransport() = default;
AudioLoopTransport::AudioLoopTransport(AudioLoopTransport&&) noexcept = default;
AudioLoopTransport& AudioLoopTransport::operator=(AudioLoopTransport&&) noexcept = default;
AudioLoopCommand AudioLoopTransport::change(std::shared_ptr<const AudioClip> clip, AudioLoopChange timing) {
    if (!state_) { return {}; }
    if (!clip || timing.bar_frames == 0 || timing.bar_frames > (*clip).frames() ||
        timing.lead_frames > 480000 || timing.fade_frames > 48000) { return {AudioLoopStatus::invalid_value, 0}; }
    LoopCommandCell command{};
    command.kind = audio_experiment::CommandKind::change;
    command.target = timing.bar_frames;
    command.timing = {timing.lead_frames, timing.fade_frames};
    const AudioLoopCommand result = (*state_).enqueue(command, std::move(clip));
    return result;
}
AudioLoopCommand AudioLoopTransport::pause() {
    if (!state_) { return {}; }
    LoopCommandCell command{}; command.kind = audio_experiment::CommandKind::pause;
    const AudioLoopCommand result = (*state_).enqueue(command); return result;
}
AudioLoopCommand AudioLoopTransport::resume() {
    if (!state_) { return {}; }
    LoopCommandCell command{}; command.kind = audio_experiment::CommandKind::resume;
    const AudioLoopCommand result = (*state_).enqueue(command); return result;
}
AudioLoopCommand AudioLoopTransport::stop() {
    if (!state_) { return {}; }
    LoopCommandCell command{}; command.kind = audio_experiment::CommandKind::stop;
    const AudioLoopCommand result = (*state_).enqueue(command); return result;
}
AudioLoopCommand AudioLoopTransport::set_gain(double gain) {
    if (!state_) { return {}; }
    if (!std::isfinite(gain) || gain < 0 || gain > 1) { return {AudioLoopStatus::invalid_value, 0}; }
    LoopCommandCell command{}; command.kind = audio_experiment::CommandKind::gain; command.gain = gain;
    const AudioLoopCommand result = (*state_).enqueue(command); return result;
}
AudioLoopCommand AudioLoopTransport::cancel(std::uint64_t id) {
    if (!state_) { return {}; }
    LoopCommandCell command{}; command.kind = audio_experiment::CommandKind::cancel; command.target = id;
    const AudioLoopCommand result = (*state_).enqueue(command); return result;
}
AudioLoopReceipt AudioLoopTransport::poll(std::uint64_t id) {
    if (!state_) { return {}; }
    const AudioLoopReceipt result = (*state_).poll(id); return result;
}
AudioLoopStatus AudioLoopTransport::close() {
    if (!state_) { return AudioLoopStatus::closed; }
    if (stxlib::current_thread() != (*state_).control_thread) { return AudioLoopStatus::wrong_thread; }
    (*state_).close();
    return AudioLoopStatus::ok;
}
#ifdef GUI_FORMS_AUDIO_TESTING
struct AudioLoopTransportTestAccess final {
    static void fail(AudioLoopTransport& transport, AudioStatus failure) {
        audio_loop_test_engine_failure(*(*transport.state_).engine, failure);
    }
    static AudioLoopStatus render(AudioLoopTransport& transport, std::span<float> samples) {
        if (!transport.state_ || samples.size() % 2 != 0) { return AudioLoopStatus::invalid_value; }
        ma_engine* engine_node = (*transport.state_).native_engine;
        const ma_result rendered = ma_engine_read_pcm_frames(engine_node, samples.data(), samples.size() / 2, nullptr);
        const AudioLoopStatus result = rendered == MA_SUCCESS ? AudioLoopStatus::ok : AudioLoopStatus::backend_error;
        return result;
    }
};
void audio_loop_test_fail(AudioLoopTransport& transport, AudioStatus failure) {
    AudioLoopTransportTestAccess::fail(transport, failure);
}
AudioLoopStatus audio_loop_test_native_render(AudioLoopTransport& transport, std::span<float> samples) {
    const AudioLoopStatus result = AudioLoopTransportTestAccess::render(transport, samples);
    return result;
}
#endif
}
