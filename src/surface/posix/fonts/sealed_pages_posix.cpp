#include "sealed_pages_posix.hpp"

#include <sys/mman.h>

namespace gui_forms::host {
namespace {

// mmap returns page-aligned, zeroed memory; the kernel rounds the length up to whole pages, and
// mprotect and munmap accept the same unrounded length.
std::byte* reserve_pages(std::size_t bytes) noexcept {
    if (bytes == 0U) return nullptr;
    void* mapped = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mapped == MAP_FAILED) return nullptr;
    std::byte* pages = static_cast<std::byte*>(mapped);
    return pages;
}

bool seal_pages(std::byte* pages, std::size_t bytes) noexcept {
    const bool sealed = mprotect(pages, bytes, PROT_READ) == 0;
    return sealed;
}

void release_pages(std::byte* pages, std::size_t bytes) noexcept {
    static_cast<void>(munmap(pages, bytes));
}

} // namespace

gui_forms::detail::SealedPageFunctions posix_sealed_pages() noexcept {
    const gui_forms::detail::SealedPageFunctions functions{
        .reserve = &reserve_pages, .seal = &seal_pages, .release = &release_pages};
    return functions;
}

} // namespace gui_forms::host
