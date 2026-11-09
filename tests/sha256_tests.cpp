// SHA-256 against the FIPS 180-2 / NIST CAVP examples ("abc", the empty message, the 448-bit
// two-block message and one million 'a'), every padding boundary from 0 to 130 bytes (checked
// for agreement between one call and a call over the same bytes held elsewhere), and the
// hexadecimal parser, including the rejections; the streaming hasher against the one-shot
// digest at every split point, its copy and reset; the hexadecimal writer.

#include "gui_forms/integrity/sha256/sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gui_forms::Sha256Digest;
using gui_forms::parse_sha256_hex;
using gui_forms::sha256;
using gui_forms::Sha256Hasher;

struct KnownAnswer final {
    std::string_view message;
    std::string_view digest;
};

Sha256Digest digest_of(std::string_view text) {
    const std::span<const std::uint8_t> bytes(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    const Sha256Digest digest = sha256(bytes);
    return digest;
}

bool matches(const Sha256Digest& actual, std::string_view expected_hex) {
    Sha256Digest expected{};
    if (!parse_sha256_hex(std::span<const char>(expected_hex.data(), expected_hex.size()),
                          expected)) {
        return false;
    }
    const bool equal = actual == expected;
    return equal;
}

bool known_answers() {
    const std::array<KnownAnswer, 4> answers{{
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopq"
         "klmnopqrlmnopqrsmnopqrstnopqrstu",
         "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
    }};
    for (const KnownAnswer& answer : answers) {
        if (!matches(digest_of(answer.message), answer.digest)) {
            std::printf("sha256: wrong digest for a %zu-byte message\n", answer.message.size());
            return false;
        }
    }
    const std::vector<std::uint8_t> million(1000000U, static_cast<std::uint8_t>('a'));
    if (!matches(sha256(million),
                 "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0")) {
        std::puts("sha256: wrong digest for one million 'a'");
        return false;
    }
    return true;
}

// Every tail length around the one- and two-block padding boundaries (55, 56, 63, 64, 119,
// 120): the digest of a prefix must not depend on where the bytes live or what follows them.
bool padding_boundaries() {
    std::vector<std::uint8_t> source(140U);
    for (std::size_t index = 0; index < source.size(); ++index) {
        source[index] = static_cast<std::uint8_t>(index * 37U + 11U);
    }
    std::vector<std::uint8_t> copy(140U, 0xA5U);
    std::vector<Sha256Digest> seen{};
    seen.reserve(131U);
    for (std::size_t length = 0; length <= 130U; ++length) {
        std::memcpy(copy.data(), source.data(), length);
        const Sha256Digest first = sha256(std::span<const std::uint8_t>(source.data(), length));
        const Sha256Digest second = sha256(std::span<const std::uint8_t>(copy.data(), length));
        if (first != second) {
            std::printf("sha256: a %zu-byte prefix depends on its storage\n", length);
            return false;
        }
        for (const Sha256Digest& earlier : seen) {
            if (earlier == first) {
                std::printf("sha256: a %zu-byte prefix repeats an earlier digest\n", length);
                return false;
            }
        }
        seen.push_back(first);
    }
    return true;
}

bool hex_parsing() {
    const std::string_view upper =
        "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";
    if (!matches(digest_of("abc"), upper)) return false;
    Sha256Digest untouched{};
    untouched.bytes[0] = 0x5AU;
    const std::string_view short_text = "ba7816bf";
    const std::string_view bad_digit =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ag";
    if (parse_sha256_hex(std::span<const char>(short_text.data(), short_text.size()),
                         untouched)) {
        return false;
    }
    if (parse_sha256_hex(std::span<const char>(bad_digit.data(), bad_digit.size()), untouched)) {
        return false;
    }
    const bool unchanged = untouched.bytes[0] == 0x5AU && untouched.bytes[1] == 0U;
    return unchanged;
}

// The hasher fed in two pieces, split at every point of a 200-byte message (crossing one, two
// and three blocks), then in single bytes, gives the one-shot digest.
bool streaming_matches_one_shot() {
    std::vector<std::uint8_t> message(200U);
    for (std::size_t index = 0; index < message.size(); ++index) {
        message[index] = static_cast<std::uint8_t>(index * 91U + 7U);
    }
    const std::span<const std::uint8_t> whole(message.data(), message.size());
    const Sha256Digest expected = sha256(whole);
    Sha256Hasher hasher{};
    for (std::size_t split = 0; split <= message.size(); ++split) {
        hasher.update(whole.subspan(0U, split));
        hasher.update(whole.subspan(split));
        if (hasher.byte_count() != message.size()) {
            std::printf("sha256: the hasher counted %llu bytes, not 200\n",
                        static_cast<unsigned long long>(hasher.byte_count()));
            return false;
        }
        const Sha256Digest streamed = hasher.finish();
        if (streamed != expected) {
            std::printf("sha256: the hasher split at %zu differs from the one-shot digest\n", split);
            return false;
        }
    }
    for (const std::uint8_t byte : message) {
        const std::array<std::uint8_t, 1> one{byte};
        hasher.update(std::span<const std::uint8_t>(one.data(), one.size()));
    }
    if (hasher.finish() != expected) {
        std::puts("sha256: the hasher fed byte by byte differs from the one-shot digest");
        return false;
    }
    // finish starts a new message: an empty one now.
    if (!matches(hasher.finish(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")) {
        std::puts("sha256: a finished hasher does not start an empty message");
        return false;
    }
    return true;
}

// A copy finishes the prefix while the original goes on; reset forgets; text updates hash the
// bytes as they are.
bool copy_reset_and_text() {
    Sha256Hasher hasher{};
    hasher.update(std::string_view("ab"));
    Sha256Hasher prefix = hasher;
    hasher.update(std::string_view("c"));
    if (!matches(hasher.finish(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) {
        std::puts("sha256: the original hasher lost its message when copied");
        return false;
    }
    if (prefix.finish() != gui_forms::sha256_of_text("ab")) {
        std::puts("sha256: a copied hasher does not finish the prefix");
        return false;
    }
    hasher.update(std::string_view("discarded"));
    hasher.reset();
    hasher.update(std::string_view("abc"));
    const bool reset_holds = hasher.finish() == digest_of("abc") && hasher.byte_count() == 0U;
    if (!reset_holds) {
        std::puts("sha256: reset did not forget the message");
        return false;
    }
    return true;
}

// The writer gives lowercase digits the parser reads back to the same digest.
bool hex_writing() {
    const Sha256Digest digest = digest_of("abc");
    const std::string text = gui_forms::sha256_hex(digest);
    if (text != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
        std::puts("sha256: sha256_hex wrote the wrong digits");
        return false;
    }
    std::array<char, gui_forms::sha256_hex_digits> digits{};
    gui_forms::format_sha256_hex(digest, digits);
    Sha256Digest parsed{};
    const bool round_trip = parse_sha256_hex(std::span<const char>(digits.data(), digits.size()), parsed) &&
                            parsed == digest;
    if (!round_trip) {
        std::puts("sha256: written digits do not parse back to the digest");
        return false;
    }
    return true;
}

} // namespace

int main() {
    if (!known_answers()) return 1;
    if (!padding_boundaries()) return 2;
    if (!hex_parsing()) {
        std::puts("sha256: the hexadecimal parser accepted bad text or changed its output");
        return 3;
    }
    if (!streaming_matches_one_shot()) return 4;
    if (!copy_reset_and_text()) return 5;
    if (!hex_writing()) return 6;
    std::puts("sha256: FIPS examples, padding boundaries, streaming and hexadecimal parsing and writing hold");
    return 0;
}
