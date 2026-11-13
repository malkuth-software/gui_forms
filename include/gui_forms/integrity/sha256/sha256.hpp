#pragma once

#include "gui_forms/export/application_export.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms {

// SHA-256 (FIPS 180-4) over bytes held in memory or arriving in pieces: the digest the bundled
// resources' install manifests record, and the one a theme package manifest records for each
// sheet. One implementation serves the library and its clients (a theme editor writing a
// package manifest uses the same function the theme package compiler checks it with).

inline constexpr std::size_t sha256_digest_bytes = 32U;
inline constexpr std::size_t sha256_block_bytes = 64U;
// A digest written as hexadecimal: two digits per byte.
inline constexpr std::size_t sha256_hex_digits = sha256_digest_bytes * 2U;

// The digest in the standard byte order (the first byte is the high byte of the first word).
struct GUI_FORMS_API Sha256Digest final {
    std::array<std::uint8_t, sha256_digest_bytes> bytes{};

    friend bool operator==(const Sha256Digest& left, const Sha256Digest& right) noexcept {
        const bool equal = left.bytes == right.bytes;
        return equal;
    }
    friend bool operator!=(const Sha256Digest& left, const Sha256Digest& right) noexcept {
        const bool different = !(left == right);
        return different;
    }
};

// A streaming hasher: feed the message in any number of pieces of any length, then finish. The
// digest equals sha256 over the pieces joined. It owns its 64-byte block buffer, allocates
// nothing and is one execution context's (it holds no lock). Copying it copies the state, so a
// copy finishes the prefix fed so far while the original goes on.
class GUI_FORMS_API Sha256Hasher final {
public:
    Sha256Hasher() noexcept;

    // Adds `bytes` (borrowed for the call) to the message. Any length, including zero.
    void update(std::span<const std::uint8_t> bytes) noexcept;
    // The same, for text: its UTF-8 (or any) bytes as they are.
    void update(std::string_view bytes) noexcept;
    // The digest of everything fed since construction or the last finish or reset. The hasher
    // then starts a new, empty message.
    [[nodiscard]] Sha256Digest finish() noexcept;
    // Forgets the message fed so far.
    void reset() noexcept;
    // How many bytes the current message holds so far (modulo 2^64).
    [[nodiscard]] std::uint64_t byte_count() const noexcept { return byte_count_; }

private:
    std::array<std::uint32_t, 8> state_{};
    // The partial block: its first buffered_ bytes are live.
    std::array<std::uint8_t, sha256_block_bytes> buffer_{};
    std::size_t buffered_{};
    std::uint64_t byte_count_{};
};

// The digest of the whole of `input`. Reads `input` once and allocates nothing; any length,
// including zero, is accepted.
[[nodiscard]] GUI_FORMS_API Sha256Digest sha256(std::span<const std::uint8_t> input) noexcept;
// The same, for text: its bytes as they are.
[[nodiscard]] GUI_FORMS_API Sha256Digest sha256_of_text(std::string_view input) noexcept;

// Parses 64 lowercase or uppercase hexadecimal digits into `digest`. Returns false, leaving
// `digest` unchanged, for any other length or character.
[[nodiscard]] GUI_FORMS_API bool parse_sha256_hex(std::span<const char> text, Sha256Digest& digest) noexcept;

// Writes `digest` as 64 lowercase hexadecimal digits into `text`, the form manifests record.
GUI_FORMS_API void format_sha256_hex(const Sha256Digest& digest, std::array<char, sha256_hex_digits>& text) noexcept;
// The same, as a new string.
[[nodiscard]] GUI_FORMS_API std::string sha256_hex(const Sha256Digest& digest);

} // namespace gui_forms
