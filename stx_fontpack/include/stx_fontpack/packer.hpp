#pragma once

#include "stx_fontpack/limits.hpp"
#include "stx_fontpack/runtime.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace stx_fontpack {

// The packer: a build-time library (stx_fontpack::packer) used by the
// fontpack tool and the tests. It holds its working data in standard
// containers; only the loader is bound to the caller's Allocator. Coding is
// deterministic: the same inputs and groups give the same pack whatever the
// executor's worker count.

struct PackInput {
    std::string name;                 // the name a loader finds the font by (a file name)
    std::vector<std::uint8_t> data;   // the font file, byte for byte
};

// A group the packer is asked to build: fonts by index into the input list.
struct PackGroupSpec {
    std::string name;
    bool eager;
    std::vector<std::uint32_t> fonts;
};

// Reads a group manifest and places every font in the first group, in
// manifest order, that has a pattern matching its name ('*' any run of
// characters, '?' one character, anything else itself):
//
//   # comment
//   group interface eager
//   Portsmouth*.ttf
//   group cjk lazy
//   NotoSansCJK*
//
// Fails with a message if a line is malformed, a font matches no group or
// a group matches no font; groups is then empty.
[[nodiscard]] bool manifest_groups(const std::string& text, const std::vector<PackInput>& fonts,
                                   std::vector<PackGroupSpec>& groups, std::string& error);

// Whether name matches pattern under manifest_groups' rules.
[[nodiscard]] bool pattern_matches(const std::string& pattern, const std::string& name) noexcept;

// One group exactly as the container records it (src/detail/format.hpp).
struct ContainerFont {
    std::string name;
    std::uint64_t size;
    std::uint32_t crc;
};

struct ContainerGroup {
    std::string name;
    bool eager;
    std::vector<ContainerFont> fonts;
    std::uint64_t font_bytes;
    std::uint64_t stream_bytes;
    std::vector<std::uint8_t> body;
};

// Writes the container as given, without checks: the claims tests use it to
// write packs whose claims are deliberately wrong.
void container_write(const std::vector<ContainerGroup>& groups, std::vector<std::uint8_t>& out);

// Packs every group (each its own set of streams) into out, parsing chunks on
// executor's workers. Fails, with a message in error and out empty, when a
// group would break one of the loader's limits, so every pack this writes
// loads. log, when not null, receives each stream's raw and coded size.
[[nodiscard]] bool pack_fonts(const std::vector<PackInput>& fonts, const std::vector<PackGroupSpec>& groups,
                              const Executor& executor, std::FILE* log, std::vector<std::uint8_t>& out,
                              std::string& error);

}  // namespace stx_fontpack
