#include "sealed_pages_windows.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace gui_forms::host {
namespace {

// VirtualAlloc returns zeroed memory aligned to the allocation granularity, rounded up to whole
// pages; VirtualProtect takes the same length, and MEM_RELEASE frees the whole allocation.
std::byte* reserve_pages(std::size_t bytes) noexcept {
    if (bytes == 0U) return nullptr;
    void* allocated = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    std::byte* pages = static_cast<std::byte*>(allocated);
    return pages;
}

bool seal_pages(std::byte* pages, std::size_t bytes) noexcept {
    DWORD previous = 0;
    const bool sealed = VirtualProtect(pages, bytes, PAGE_READONLY, &previous) != 0;
    return sealed;
}

void release_pages(std::byte* pages, std::size_t bytes) noexcept {
    static_cast<void>(bytes);
    static_cast<void>(VirtualFree(pages, 0, MEM_RELEASE));
}

} // namespace

gui_forms::detail::SealedPageFunctions windows_sealed_pages() noexcept {
    const gui_forms::detail::SealedPageFunctions functions{
        .reserve = &reserve_pages, .seal = &seal_pages, .release = &release_pages};
    return functions;
}

} // namespace gui_forms::host
