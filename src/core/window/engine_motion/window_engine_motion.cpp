// The theme engine's motion in a window (T2 plan, phase E7): the
// repaints a part's transitions and its ambient effect ask for, on the
// window's one motion clock.

#include "gui_forms/window.hpp"

namespace gui_forms {

void Window::engine_part_moved(PartRecordHandle handle, bool settled) {
    EnginePartRecord* const record = engine_parts_.record(handle);
    if (record == nullptr) return;
    EngineDamage damage{};
    static_cast<void>(damage_engine_part(*record, damage));
    if (settled) (*record).motion_unsettled = (*record).motion && (*(*record).motion).moving();
}

void Window::run_engine_ambient(PartRecordHandle handle, FrameInterval interval) {
    motion_clock_.enlist_ambient(handle.slot, handle.generation, interval);
}

void Window::stop_engine_ambient(PartRecordHandle handle) noexcept {
    motion_clock_.withdraw_ambient(handle.slot, handle.generation);
}

bool Window::engine_ambient_frame(std::uint32_t slot, std::uint32_t generation, std::uint64_t& seen) {
    const PartRecordHandle handle{slot, generation};
    const EnginePartRecord* const record = engine_parts_.record(handle);
    if (record == nullptr || !(*record).ambient) return false;
    // The part was asked to repaint by the last frame and was not: it is no
    // longer shown, and the effect ends rather than paint on.
    const std::uint64_t painted = (*record).last_painted;
    if (seen != ~std::uint64_t{0} && painted == seen) return false;
    EngineDamage damage{};
    if (!damage_engine_part(*record, damage)) return false;
    seen = painted;
    metrics_.record_engine_ambient_frame();
    return true;
}

void Window::repaint_engine_part(std::uint32_t slot, std::uint32_t generation) {
    const PartRecordHandle handle{slot, generation};
    const EnginePartRecord* const record = engine_parts_.record(handle);
    if (record == nullptr) return;
    EngineDamage damage{};
    static_cast<void>(damage_engine_part(*record, damage));
}

void Window::resume_engine_ambient() {
    motion_clock_.repaint_parked();
    EngineDamage damage{};
    for (std::size_t slot = 0; slot < engine_parts_.slot_count(); ++slot) {
        const EnginePartRecord* const record = engine_parts_.record_in_slot(slot);
        if (record == nullptr || !(*record).ambient) continue;
        static_cast<void>(damage_engine_part(*record, damage));
    }
}

} // namespace gui_forms
