#pragma once

#include <cstdint>

namespace stx_fontpack {

// The container limits (src/detail/format.hpp describes the layout). A loader
// checks every claim against them before it allocates anything for a group,
// and the packer refuses to write a pack that breaks one, so every pack the
// packer writes loads.
enum : std::uint32_t {
    kMaxGroups = 16,
    kMaxFonts = 256,                    // in the whole pack
    kMaxNameBytes = 128,
    kMaxFontBytes = 64u << 20,          // one font file (the largest bundled font is 16.5 MB)
    kMaxGroupFontBytes = 256u << 20,    // all fonts of one group
    // Decoded streams may exceed the fonts they describe (a CFF number of one
    // byte is held as 2), but never by more than this factor.
    kMaxStreamExpansion = 4,
    // A group's fonts are at most this many times its body size. Ties every
    // size claim to bytes actually present in the pack, so a short damaged
    // pack cannot claim a large group. The bundled set packs at 2.35x.
    kMaxDecodeRatio = 64,
    kGroupEager = 1,
    // The most workers a load or a pack uses at once, whatever the executor offers.
    kMaxWorkers = 64
};

}  // namespace stx_fontpack
