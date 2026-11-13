// stx_fontpack: TrueType point writing shared by the packer (to test
// that a glyph is reproducible) and the loader (to reproduce it).
#pragma once

#include "detail/format.hpp"

namespace stx_fontpack {

// The flag a canonical writer gives a point: shortest coordinate forms.
inline std::uint32_t tt_canonical_flag(bool on_curve, std::int32_t dx, std::int32_t dy) {
    std::uint32_t flag = on_curve ? kTtOnCurve : 0u;
    if (dx == 0) {
        flag |= kTtXSame;
    } else if (dx >= -255 && dx <= 255) {
        flag |= kTtXShort;
        if (dx > 0) {
            flag |= kTtXSame;
        }
    }
    if (dy == 0) {
        flag |= kTtYSame;
    } else if (dy >= -255 && dy <= 255) {
        flag |= kTtYShort;
        if (dy > 0) {
            flag |= kTtYSame;
        }
    }
    return flag;
}

// Fills flags with each point's canonical flag. triplet_flags holds one
// triplet flag per point (top bit clear = on curve).
inline void tt_flags_fill(std::uint8_t* flags, const std::uint8_t* triplet_flags, const std::int16_t* dx,
                          const std::int16_t* dy, std::uint32_t count, bool overlap_first) {
    for (std::uint32_t k = 0; k < count; k += 1) {
        flags[k] = static_cast<std::uint8_t>(tt_canonical_flag((triplet_flags[k] & 128u) == 0, dx[k], dy[k]));
    }
    if (count != 0 && overlap_first) {
        flags[0] = static_cast<std::uint8_t>(flags[0] | kTtOverlap);
    }
}

// Writes the flag, x and y arrays of a simple glyph and returns the byte
// count. flags comes from tt_flags_fill; dx and dy are the deltas.
// repeat_policy 0 never uses the repeat form; r >= 2 uses it for runs of at
// least r equal flags (at most 256 per run).
// out must have room for count * 5 bytes.
inline std::size_t tt_points_write(std::uint8_t* out, const std::uint8_t* flags, const std::int16_t* dx,
                                   const std::int16_t* dy, std::uint32_t count, std::uint32_t repeat_policy) {
    std::uint8_t* cursor = out;
    std::uint32_t index = 0;
    while (index < count) {
        const std::uint32_t flag = flags[index];
        std::uint32_t run = 1;
        if (repeat_policy != 0) {
            while (index + run < count && run < 256 && flags[index + run] == flag) {
                run += 1;
            }
        }
        if (repeat_policy != 0 && run >= repeat_policy) {
            cursor[0] = static_cast<std::uint8_t>(flag | kTtRepeat);
            cursor[1] = static_cast<std::uint8_t>(run - 1);
            cursor += 2;
        } else {
            for (std::uint32_t k = 0; k < run; k += 1) {
                cursor[k] = static_cast<std::uint8_t>(flag);
            }
            cursor += run;
        }
        index += run;
    }
    for (std::uint32_t k = 0; k < count; k += 1) {
        const std::int32_t d = dx[k];
        if (d == 0) {
            continue;
        }
        if (d >= -255 && d <= 255) {
            cursor[0] = static_cast<std::uint8_t>(d < 0 ? -d : d);
            cursor += 1;
        } else {
            put_be16(cursor, static_cast<std::uint32_t>(d) & 0xFFFFu);
            cursor += 2;
        }
    }
    for (std::uint32_t k = 0; k < count; k += 1) {
        const std::int32_t d = dy[k];
        if (d == 0) {
            continue;
        }
        if (d >= -255 && d <= 255) {
            cursor[0] = static_cast<std::uint8_t>(d < 0 ? -d : d);
            cursor += 1;
        } else {
            put_be16(cursor, static_cast<std::uint32_t>(d) & 0xFFFFu);
            cursor += 2;
        }
    }
    return static_cast<std::size_t>(cursor - out);
}

// Byte length of a composite glyph's component records starting at data,
// or 0 if they run past size. has_instructions reports the last record's flag.
inline std::size_t tt_composite_length(const std::uint8_t* data, std::size_t size, bool& has_instructions) {
    std::size_t at = 0;
    for (;;) {
        if (size - at < 4) {
            return 0;
        }
        const std::uint32_t flags = be16(data + at);
        std::size_t record = 4;
        record += (flags & 0x0001u) != 0 ? 4 : 2;
        if ((flags & 0x0008u) != 0) {
            record += 2;
        } else if ((flags & 0x0040u) != 0) {
            record += 4;
        } else if ((flags & 0x0080u) != 0) {
            record += 8;
        }
        if (size - at < record) {
            return 0;
        }
        at += record;
        if ((flags & 0x0020u) == 0) {
            has_instructions = (flags & 0x0100u) != 0;
            return at;
        }
    }
}

}  // namespace stx_fontpack

