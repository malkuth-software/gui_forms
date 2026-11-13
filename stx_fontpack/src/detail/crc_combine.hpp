// stx_fontpack: joining CRC-32 values of consecutive pieces.
//
// stx_zlib::crc32 computes each piece; this joins two finished values into
// the value of the concatenation, as zlib's crc32_combine does (the method,
// polynomial multiplication modulo the CRC polynomial, follows zlib 1.2.12's
// multmodp and x2nmodp; PROVENANCE.md). The loader checks a large font in
// pieces on several workers and joins the pieces' values in order.
#pragma once

#include <array>
#include <cstdint>

namespace stx_fontpack {

enum : std::uint32_t { kCrcPolynomial = 0xEDB88320u };

// Polynomial product a * b modulo the CRC polynomial, both in the reflected
// bit order (bit 31 is x^0).
constexpr std::uint32_t crc_multiply(std::uint32_t a, std::uint32_t b) noexcept {
    std::uint32_t m = 1u << 31;
    std::uint32_t product = 0;
    while (m != 0) {
        if ((a & m) != 0) {
            product ^= b;
            if ((a & (m - 1)) == 0) {
                break;
            }
        }
        m >>= 1;
        b = (b & 1u) != 0 ? (b >> 1) ^ kCrcPolynomial : b >> 1;
    }
    return product;
}

// power[k] is x^(2^k) modulo the polynomial.
constexpr std::array<std::uint32_t, 64> crc_make_powers() noexcept {
    std::array<std::uint32_t, 64> power{};
    power[0] = 1u << 30;   // x^1
    for (std::uint32_t k = 1; k < 64; k += 1) {
        power[k] = crc_multiply(power[k - 1], power[k - 1]);
    }
    return power;
}

inline constexpr std::array<std::uint32_t, 64> kCrcPowers = crc_make_powers();

// x^(8 * bytes) modulo the polynomial: the operator that appends bytes zero
// bytes to a CRC register. Starts at power 3 because 8 is three doublings;
// bytes below 2^60 keeps the index inside the table.
constexpr std::uint32_t crc_zeros_operator(std::uint64_t bytes) noexcept {
    std::uint32_t result = 1u << 31;   // x^0
    std::uint32_t k = 3;
    while (bytes != 0 && k < 64) {
        if ((bytes & 1u) != 0) {
            result = crc_multiply(kCrcPowers[k], result);
        }
        bytes >>= 1;
        k += 1;
    }
    return result;
}

// CRC-32 of A followed by B from crc(A), crc(B) and B's length in bytes.
constexpr std::uint32_t crc32_combine(std::uint32_t crc_a, std::uint32_t crc_b, std::uint64_t size_b) noexcept {
    return crc_multiply(crc_zeros_operator(size_b), crc_a) ^ crc_b;
}

}  // namespace stx_fontpack
