#include "stxlib_state_probe.hpp"

#include <stxlib/memory.hpp>
#include <stxlib/threads.hpp>

#include <cstring>

namespace gui_forms::runtime {

namespace {
constexpr std::size_t probe_alignment = 16;
} // namespace

void* stxlib_state_probe_allocate(const std::size_t size, const unsigned char fill) noexcept {
    void* const block = stxlib::memory::allocate(size, probe_alignment);
    if (block != nullptr && size != 0) {
        std::memset(block, fill, size);
    }
    return block;
}

void stxlib_state_probe_release(void* const block) noexcept {
    stxlib::memory::release(block);
}

std::uint64_t stxlib_state_probe_thread() noexcept {
    const stxlib::ThreadIdentity identity = stxlib::current_thread();
    const std::uint64_t value = identity.value();
    return value;
}

} // namespace gui_forms::runtime
