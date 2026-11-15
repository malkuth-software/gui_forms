#include "sealed_pages_web.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace gui_forms::host {
namespace {

// WebAssembly's page size.
constexpr std::size_t wasm_page_bytes = 65536U;

// Zeroed blocks of whole wasm pages; the count is checked before it is
// rounded up, so the rounding cannot wrap.
std::byte* reserve_pages(std::size_t bytes) noexcept {
    if (bytes == 0U || bytes > SIZE_MAX - wasm_page_bytes) return nullptr;
    const std::size_t rounded = (bytes + wasm_page_bytes - 1U) / wasm_page_bytes * wasm_page_bytes;
    void* block = std::aligned_alloc(wasm_page_bytes, rounded);
    if (block == nullptr) return nullptr;
    std::memset(block, 0, rounded);
    std::byte* pages = static_cast<std::byte*>(block);
    return pages;
}

// Nothing to do: linear memory cannot be made read-only (see the header).
bool seal_pages(std::byte*, std::size_t) noexcept {
    return true;
}

void release_pages(std::byte* pages, std::size_t) noexcept {
    std::free(pages);
}

} // namespace

gui_forms::detail::SealedPageFunctions web_sealed_pages() noexcept {
    const gui_forms::detail::SealedPageFunctions functions{
        .reserve = &reserve_pages, .seal = &seal_pages, .release = &release_pages};
    return functions;
}

} // namespace gui_forms::host
