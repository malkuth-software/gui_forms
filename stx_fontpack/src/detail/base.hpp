// stx_fontpack: shared definitions for the packer and the loader.
//
// Every multi-byte integer the pack format itself defines is little-endian.
// Font data (sfnt, CFF, glyf) is big-endian and read with the be* helpers.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace stx_fontpack {

typedef std::vector<std::uint8_t> Bytes;

// rANS parameters shared by the encoder and the decoder.
// Probabilities are 12-bit; the state is 32-bit and renormalises 16 bits at a time.
enum : std::uint32_t {
    kProbBits = 12,
    kProbScale = 1u << kProbBits,
    kRansLow = 1u << 16
};

// Sequence code alphabets. A value below the direct count is its own code;
// larger values use two codes per power of two plus raw extra bits.
enum : std::uint32_t {
    kLitLenDirect = 16,
    kMatchLenDirect = 32,   // code 0 ends the chunk; code c is match length c + 1
    kOffsetDirect = 8,      // codes 0..2 are the repeat offsets; code 3 + c is offset c + 1
    kMinMatch = 2,
    kSeqAlphabet = 80,
    kLiteralStates = 8,     // interleaved rANS states for a chunk's literals
    kMaxTables = 255,
    // The decoder reads rANS units without a bounds test per unit, so a coded
    // stream must be followed by this many readable bytes (the pack ends with them).
    kStreamPadding = 32
};

struct SlotCode {
    std::uint32_t code;
    std::uint32_t extra_bits;
    std::uint32_t extra_value;
};

inline SlotCode slot_encode(std::uint32_t value, std::uint32_t direct) {
    SlotCode result;
    if (value < direct) {
        result.code = value;
        result.extra_bits = 0;
        result.extra_value = 0;
        return result;
    }
    const std::uint32_t wide = value - direct + 2;
    const std::uint32_t top = 31u - static_cast<std::uint32_t>(std::countl_zero(wide));
    const std::uint32_t half = (wide >> (top - 1)) & 1u;
    result.code = direct + 2 * (top - 1) + half;
    result.extra_bits = top - 1;
    result.extra_value = wide & ((1u << (top - 1)) - 1u);
    return result;
}

inline std::uint32_t slot_extra_bits(std::uint32_t code, std::uint32_t direct) {
    if (code < direct) {
        return 0;
    }
    return (code - direct) >> 1;
}

inline std::uint32_t slot_decode(std::uint32_t code, std::uint32_t direct, std::uint32_t extra_value) {
    if (code < direct) {
        return code;
    }
    const std::uint32_t relative = code - direct;
    const std::uint32_t top = (relative >> 1) + 1;
    const std::uint32_t wide = ((2u | (relative & 1u)) << (top - 1)) | extra_value;
    return wide + direct - 2;
}

// A CFF number token: the number's first byte in the low 8 bits and, for the
// two-byte forms (first byte 247..254), its second byte in the high 8 bits.
// Three- and five-byte numbers are their first byte only; their payload
// travels separately. One- and two-byte forms are in one-to-one
// correspondence with the values -1131..1131.
inline bool token_has_value(std::uint32_t token) {
    const std::uint32_t first = token & 0xFFu;
    return first >= 32 && first <= 254;
}

inline bool token_two_bytes(std::uint32_t token) {
    const std::uint32_t first = token & 0xFFu;
    return first >= 247 && first <= 254;
}

inline std::int32_t token_value(std::uint32_t token) {
    const std::uint32_t first = token & 0xFFu;
    const std::uint32_t second = token >> 8;
    if (first < 247) {
        return static_cast<std::int32_t>(first) - 139;
    }
    if (first < 251) {
        return static_cast<std::int32_t>((first - 247) * 256 + second + 108);
    }
    return -static_cast<std::int32_t>((first - 251) * 256 + second + 108);
}

// value must be in -1131..1131.
inline std::uint32_t token_from_value(std::int32_t value) {
    if (value >= -107 && value <= 107) {
        return static_cast<std::uint32_t>(value + 139);
    }
    if (value > 0) {
        const std::uint32_t wide = static_cast<std::uint32_t>(value - 108);
        return (247 + (wide >> 8)) | ((wide & 0xFFu) << 8);
    }
    const std::uint32_t wide = static_cast<std::uint32_t>(-value - 108);
    return (251 + (wide >> 8)) | ((wide & 0xFFu) << 8);
}

// Number-stream constants. A fuzzy match copies earlier numbers and adds a
// correction to each. A correction travels as its zigzag value (0, +1, -1,
// +2, ...), coded like a sequence length: small values directly, larger ones
// as a code plus raw extra bits.
enum : std::uint32_t {
    kResidualDirect = 32,
    kResidualAlphabet = 64,
    kResidualContexts = 11,    // see residual_context
    kLowTables = 8,            // one per two-byte first byte 247..254
    kKindAlphabet = 3,         // sequence ends with: no match, exact match, fuzzy match
    kNumberStates = 4,         // interleaved rANS states for low bytes
    // A chunk's fuzzy matches are dealt in turn to this many lanes. Each lane's
    // corrections are a separate rANS stream and bit stream, so a decoder can
    // advance all lanes at once although each correction's table depends on
    // the one before it.
    kResidualLanes = 4,
    // Per chunk: sequence codes, sequence extra bits, literal first bytes,
    // literal second bytes, then each lane's codes, then each lane's extra bits.
    kNumberPayloads = 4 + 2 * kResidualLanes
};

inline std::uint32_t residual_symbol(std::int32_t correction) {
    if (correction > 0) {
        return static_cast<std::uint32_t>(2 * correction - 1);
    }
    return static_cast<std::uint32_t>(-2 * correction);
}

// Context of a correction: 0 for the first of a match; afterwards
// residual_next_context(code of the previous correction, whether the one
// before that was non-zero). The bucket depends only on the code, never on
// extra bits, so a decoder can pick the next table before reading them.
inline std::uint32_t residual_bucket(std::uint32_t code) {
    if (code == 0) {
        return 0;
    }
    if (code <= 2) {
        return 1;   // magnitude 1
    }
    if (code <= 6) {
        return 2;   // magnitude 2..3
    }
    if (code <= 16) {
        return 3;   // magnitude 4..8
    }
    return 4;
}

inline std::uint32_t residual_next_context(std::uint32_t code, std::uint32_t before_nonzero) {
    return 1 + residual_bucket(code) * 2 + before_nonzero;
}

// A decoded number stream holds values, not tokens: the value of a one- or
// two-byte number, or one of these for the first byte of a longer number.
enum : std::int32_t {
    kValueLimit = 1131,
    kValueThreeBytes = 0x7FFE,   // first byte 28
    kValueFiveBytes = 0x7FFF     // first byte 255
};

inline std::int32_t residual_value(std::uint32_t symbol) {
    if ((symbol & 1u) != 0) {
        return static_cast<std::int32_t>((symbol + 1) >> 1);
    }
    return -static_cast<std::int32_t>(symbol >> 1);
}

// Previous-byte quantisers for literal contexts. Selector 0 means no
// previous-byte context. The returned class count excludes the extra
// "previous byte was copied by a match" class the codec adds.
inline std::uint32_t quantiser_classes(std::uint32_t selector) {
    switch (selector) {
        case 1: return 8;
        case 2: return 16;
        case 3: return 64;
        case 4: return 256;
        default: return 0;
    }
}

inline std::uint32_t quantise(std::uint32_t selector, std::uint32_t byte) {
    switch (selector) {
        case 1: return byte >> 5;
        case 2: return byte >> 4;
        case 3: return byte >> 2;
        case 4: return byte;
        default: return 0;
    }
}

inline void put_varint(Bytes& out, std::uint64_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<std::uint8_t>(value | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(value));
}

// Bounded forward reader over a byte range. A read past the end sets failed
// and returns zero; callers check failed once after a group of reads.
struct Reader {
    const std::uint8_t* cursor;
    const std::uint8_t* end;
    bool failed;
};

inline Reader reader_make(const std::uint8_t* data, std::size_t size) {
    Reader reader;
    reader.cursor = data;
    reader.end = data + size;
    reader.failed = false;
    return reader;
}

inline std::uint64_t get_varint(Reader& reader) {
    std::uint64_t value = 0;
    std::uint32_t shift = 0;
    for (;;) {
        if (reader.cursor >= reader.end || shift > 56) {
            reader.failed = true;
            return 0;
        }
        const std::uint8_t byte = *reader.cursor;
        reader.cursor += 1;
        value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return value;
        }
        shift += 7;
    }
}

inline std::uint32_t get_u8(Reader& reader) {
    if (reader.cursor >= reader.end) {
        reader.failed = true;
        return 0;
    }
    const std::uint32_t value = *reader.cursor;
    reader.cursor += 1;
    return value;
}

inline const std::uint8_t* get_span(Reader& reader, std::size_t size) {
    if (static_cast<std::size_t>(reader.end - reader.cursor) < size) {
        reader.failed = true;
        return reader.cursor;
    }
    const std::uint8_t* start = reader.cursor;
    reader.cursor += size;
    return start;
}

inline std::uint32_t be16(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 8) | p[1];
}

inline std::int32_t be16s(const std::uint8_t* p) {
    return static_cast<std::int16_t>(be16(p));
}

inline std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

inline void put_be16(std::uint8_t* p, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>(value >> 8);
    p[1] = static_cast<std::uint8_t>(value);
}

inline void put_be32(std::uint8_t* p, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>(value >> 24);
    p[1] = static_cast<std::uint8_t>(value >> 16);
    p[2] = static_cast<std::uint8_t>(value >> 8);
    p[3] = static_cast<std::uint8_t>(value);
}

inline void put_le32(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

// Byte order spelled out, so a big-endian host reads the same value.
inline std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace stx_fontpack

