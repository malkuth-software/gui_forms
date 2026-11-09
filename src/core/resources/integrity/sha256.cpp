#include "gui_forms/integrity/sha256/sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms {
namespace {

// FIPS 180-4 section 4.2.2: the first 32 bits of the fractional parts of the cube roots of the
// first 64 primes.
constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

// Section 5.3.3: the first 32 bits of the fractional parts of the square roots of the first
// eight primes.
constexpr std::array<std::uint32_t, 8> initial_state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                                                     0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                                                     0x1f83d9abU, 0x5be0cd19U};

using State = std::array<std::uint32_t, 8>;

std::uint32_t rotate_right(std::uint32_t value, unsigned count) noexcept {
    const std::uint32_t rotated = (value >> count) | (value << (32U - count));
    return rotated;
}

std::uint32_t load_big_endian(const std::uint8_t* bytes) noexcept {
    const std::uint32_t word = (static_cast<std::uint32_t>(bytes[0]) << 24U) |
                               (static_cast<std::uint32_t>(bytes[1]) << 16U) |
                               (static_cast<std::uint32_t>(bytes[2]) << 8U) |
                               static_cast<std::uint32_t>(bytes[3]);
    return word;
}

// Section 6.2.2: folds one 64-byte block into `state`.
void compress(State& state, const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t index = 0; index < 16U; ++index) {
        schedule[index] = load_big_endian(block + index * 4U);
    }
    for (std::size_t index = 16; index < 64U; ++index) {
        const std::uint32_t early = schedule[index - 15U];
        const std::uint32_t late = schedule[index - 2U];
        const std::uint32_t sigma0 =
            rotate_right(early, 7U) ^ rotate_right(early, 18U) ^ (early >> 3U);
        const std::uint32_t sigma1 =
            rotate_right(late, 17U) ^ rotate_right(late, 19U) ^ (late >> 10U);
        schedule[index] = schedule[index - 16U] + sigma0 + schedule[index - 7U] + sigma1;
    }

    // The eight working variables, named a to h as in section 6.2.2.
    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];
    for (std::size_t index = 0; index < 64U; ++index) {
        const std::uint32_t sum1 =
            rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
        const std::uint32_t choose = (e & f) ^ (~e & g);
        const std::uint32_t temporary1 =
            h + sum1 + choose + round_constants[index] + schedule[index];
        const std::uint32_t sum0 =
            rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temporary2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

// The value of one hexadecimal digit, or 16 for any other character.
unsigned hex_value(char digit) noexcept {
    unsigned value = 16U;
    if (digit >= '0' && digit <= '9') {
        value = static_cast<unsigned>(digit - '0');
    } else if (digit >= 'a' && digit <= 'f') {
        value = static_cast<unsigned>(digit - 'a') + 10U;
    } else if (digit >= 'A' && digit <= 'F') {
        value = static_cast<unsigned>(digit - 'A') + 10U;
    }
    return value;
}

// Writes the state's eight words, high byte first, as the digest.
Sha256Digest digest_of_state(const State& state) noexcept {
    Sha256Digest digest{};
    for (std::size_t word = 0; word < state.size(); ++word) {
        const std::uint32_t value = state[word];
        digest.bytes[word * 4U] = static_cast<std::uint8_t>(value >> 24U);
        digest.bytes[word * 4U + 1U] = static_cast<std::uint8_t>(value >> 16U);
        digest.bytes[word * 4U + 2U] = static_cast<std::uint8_t>(value >> 8U);
        digest.bytes[word * 4U + 3U] = static_cast<std::uint8_t>(value);
    }
    return digest;
}

} // namespace

Sha256Hasher::Sha256Hasher() noexcept : state_(initial_state) {}

void Sha256Hasher::reset() noexcept {
    state_ = initial_state;
    buffered_ = 0U;
    byte_count_ = 0U;
}

void Sha256Hasher::update(std::span<const std::uint8_t> bytes) noexcept {
    const std::size_t length = bytes.size();
    if (length == 0U) {
        return;
    }
    byte_count_ += static_cast<std::uint64_t>(length);
    std::size_t consumed = 0U;
    // First complete a partial block, if one is waiting.
    if (buffered_ != 0U) {
        const std::size_t wanted = sha256_block_bytes - buffered_;
        const std::size_t taken = length < wanted ? length : wanted;
        std::memcpy(buffer_.data() + buffered_, bytes.data(), taken);
        buffered_ += taken;
        consumed = taken;
        if (buffered_ < sha256_block_bytes) {
            return;
        }
        compress(state_, buffer_.data());
        buffered_ = 0U;
    }
    // Whole blocks are compressed where they lie, without a copy.
    const std::size_t remaining = length - consumed;
    const std::size_t whole_blocks = remaining / sha256_block_bytes;
    for (std::size_t block = 0; block < whole_blocks; ++block) {
        compress(state_, bytes.data() + consumed + block * sha256_block_bytes);
    }
    consumed += whole_blocks * sha256_block_bytes;
    const std::size_t tail_length = length - consumed;
    if (tail_length != 0U) {
        std::memcpy(buffer_.data(), bytes.data() + consumed, tail_length);
        buffered_ = tail_length;
    }
}

void Sha256Hasher::update(std::string_view bytes) noexcept {
    const std::span<const std::uint8_t> view(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                             bytes.size());
    update(view);
}

Sha256Digest Sha256Hasher::finish() noexcept {
    // Section 5.1.1 padding: the tail, a 0x80 byte, zeros, then the message length in bits as a
    // 64-bit big-endian number. It fills one block, or two when the tail leaves fewer than nine
    // bytes. The length is taken modulo 2^64 bits, as the standard limits messages to that.
    std::array<std::uint8_t, sha256_block_bytes * 2U> tail{};
    if (buffered_ != 0U) {
        std::memcpy(tail.data(), buffer_.data(), buffered_);
    }
    tail[buffered_] = 0x80U;
    const std::size_t tail_blocks = buffered_ + 9U <= sha256_block_bytes ? 1U : 2U;
    const std::size_t tail_bytes = tail_blocks * sha256_block_bytes;
    const std::uint64_t bit_length = byte_count_ * 8U;
    for (std::size_t index = 0; index < 8U; ++index) {
        const unsigned shift = static_cast<unsigned>((7U - index) * 8U);
        tail[tail_bytes - 8U + index] = static_cast<std::uint8_t>(bit_length >> shift);
    }
    for (std::size_t block = 0; block < tail_blocks; ++block) {
        compress(state_, tail.data() + block * sha256_block_bytes);
    }
    const Sha256Digest digest = digest_of_state(state_);
    reset();
    return digest;
}

Sha256Digest sha256(std::span<const std::uint8_t> input) noexcept {
    Sha256Hasher hasher{};
    hasher.update(input);
    const Sha256Digest digest = hasher.finish();
    return digest;
}

Sha256Digest sha256_of_text(std::string_view input) noexcept {
    Sha256Hasher hasher{};
    hasher.update(input);
    const Sha256Digest digest = hasher.finish();
    return digest;
}

bool parse_sha256_hex(std::span<const char> text, Sha256Digest& digest) noexcept {
    if (text.size() != sha256_hex_digits) {
        return false;
    }
    Sha256Digest parsed{};
    for (std::size_t index = 0; index < sha256_digest_bytes; ++index) {
        const unsigned high = hex_value(text[index * 2U]);
        const unsigned low = hex_value(text[index * 2U + 1U]);
        if (high > 15U || low > 15U) {
            return false;
        }
        parsed.bytes[index] = static_cast<std::uint8_t>((high << 4U) | low);
    }
    digest = parsed;
    return true;
}

void format_sha256_hex(const Sha256Digest& digest, std::array<char, sha256_hex_digits>& text) noexcept {
    constexpr std::string_view hex_digits = "0123456789abcdef";
    for (std::size_t index = 0; index < sha256_digest_bytes; ++index) {
        const std::uint8_t value = digest.bytes[index];
        text[index * 2U] = hex_digits[value >> 4U];
        text[index * 2U + 1U] = hex_digits[value & 0x0FU];
    }
}

std::string sha256_hex(const Sha256Digest& digest) {
    std::array<char, sha256_hex_digits> digits{};
    format_sha256_hex(digest, digits);
    const std::string text(digits.data(), digits.size());
    return text;
}

} // namespace gui_forms
