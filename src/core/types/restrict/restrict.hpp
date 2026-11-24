#pragma once

#include <cstddef>
#include <cstdlib>
#include <functional>
#include <span>

// GUI_FORMS_RESTRICT marks a private kernel's pointer parameter as the only
// way, for the call, that the elements it reaches are read or written: no
// other pointer used in the call reaches them. The compiler may then keep values
// in registers across writes through the other pointers, and vectorize
// without a run-time overlap test. It expands to `__restrict__` on Clang and
// GCC and `__restrict` on MSVC.
//
// Caution: restrict is a promise to the compiler, and a false promise is
// undefined behaviour that tests and sanitizers generally do not catch. Each
// use therefore
// - states its overlap rule beside the kernel (house style section 6);
// - checks that rule with require_disjoint where the check is cheap, which
//   ends the process in builds without NDEBUG and in AddressSanitizer
//   builds, so the optimized sanitizer runs check the promise too;
// - stays on parameters of functions with internal linkage inside private
//   kernels (src/), called with pointers taken from checked spans. Clang
//   marks restrict parameters noalias; restrict on a local pointer is not
//   relied on.
//   On MSVC's ABI `__restrict` on a parameter changes the mangled name, so a
//   public signature carries it only after review.
//
// The macro follows bfft's (third_party/bfft/src/detail/bruun_simd_backend.hpp)
// and the STX_CONV_RESTRICT of stx_conv (the CONV library before it became
// stxlib::conv): MSVC proper takes `__restrict`; Clang
// (clang-cl included) and GCC take `__restrict__`, because macOS's
// <sys/cdefs.h> defines `__restrict` as nothing in C++, which silently drops
// the promise.
#if defined(_MSC_VER) && !defined(__clang__)
#define GUI_FORMS_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define GUI_FORMS_RESTRICT __restrict__
#else
#define GUI_FORMS_RESTRICT
#endif

namespace gui_forms::detail {

// Overlap checks run unless NDEBUG is defined, and in AddressSanitizer
// builds whatever NDEBUG says: Clang reports it through __has_feature, GCC
// and MSVC define __SANITIZE_ADDRESS__.
#if defined(__SANITIZE_ADDRESS__)
#define GUI_FORMS_KERNEL_OVERLAP_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GUI_FORMS_KERNEL_OVERLAP_SANITIZED 1
#endif
#endif

#if !defined(NDEBUG) || defined(GUI_FORMS_KERNEL_OVERLAP_SANITIZED)
inline constexpr bool kernel_overlap_checks = true;
#else
inline constexpr bool kernel_overlap_checks = false;
#endif

// Whether the byte ranges [first, first + first_bytes) and
// [second, second + second_bytes) share no byte. An empty range shares
// none. std::less orders pointers into different objects.
[[nodiscard]] inline bool bytes_disjoint(const void* first, std::size_t first_bytes, const void* second,
                                         std::size_t second_bytes) noexcept {
    if (first_bytes == 0U || second_bytes == 0U) return true;
    const std::byte* const first_start = static_cast<const std::byte*>(first);
    const std::byte* const second_start = static_cast<const std::byte*>(second);
    const std::less<const std::byte*> before{};
    const bool first_ends_before = !before(second_start, first_start + first_bytes);
    const bool second_ends_before = !before(first_start, second_start + second_bytes);
    return first_ends_before || second_ends_before;
}

// Whether two spans share no element.
template <class First, class Second>
[[nodiscard]] bool spans_disjoint(std::span<First> first, std::span<Second> second) noexcept {
    return bytes_disjoint(first.data(), first.size_bytes(), second.data(), second.size_bytes());
}

// A kernel's debug overlap assertion: ends the process when `first` and
// `second` share an element and checks are on. Release builds skip the test.
template <class First, class Second>
void require_disjoint(std::span<First> first, std::span<Second> second) noexcept {
    if constexpr (kernel_overlap_checks) {
        if (!spans_disjoint(first, second)) std::abort();
    }
}

// One buffer's bytes, for checking several buffers at once.
struct ByteRange final {
    const void* data = nullptr;
    std::size_t bytes = 0U;
};

template <class Element>
[[nodiscard]] ByteRange byte_range(std::span<Element> elements) noexcept {
    const ByteRange range{elements.data(), elements.size_bytes()};
    return range;
}

// The overlap assertion for a kernel with several buffers: ends the process
// when a buffer in `written` shares a byte with another written buffer or
// with one in `read`, and checks are on. Read buffers may share bytes with
// each other: a restricted pointer that only reads promises nothing about
// other readers.
inline void require_disjoint_writes(std::span<const ByteRange> written, std::span<const ByteRange> read) noexcept {
    if constexpr (kernel_overlap_checks) {
        for (std::size_t index = 0U; index < written.size(); ++index) {
            const ByteRange& first = written[index];
            for (std::size_t other = index + 1U; other < written.size(); ++other) {
                const ByteRange& second = written[other];
                if (!bytes_disjoint(first.data, first.bytes, second.data, second.bytes)) std::abort();
            }
            for (const ByteRange& second : read) {
                if (!bytes_disjoint(first.data, first.bytes, second.data, second.bytes)) std::abort();
            }
        }
    }
}

} // namespace gui_forms::detail
