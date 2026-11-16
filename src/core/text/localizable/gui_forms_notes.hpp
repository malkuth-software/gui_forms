#pragma once

// The note tables the gui_forms Clang plugin writes into the binaries it
// compiles (tools/localizable_plugin): ELF-note-shaped blobs named
// "GUIForms", one per translation unit and record kind, in a section of
// their own (Mach-O __DATA,__gui_forms_loc; COFF .gfloc$m; ELF
// .note.gui_forms.loc, in a PT_NOTE segment). The kind is the note's type.
// Internal to the toolkit.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gui_forms {

// The localizable-literal tables (version 1).
inline constexpr std::uint32_t localizable_note_type = 0x47464c01U;
// The effect verification records (design note §2): a count, then per
// effect its name (length, bytes), its static operation bound, its setup
// bound and its state's size, every integer a little-endian uint32.
inline constexpr std::uint32_t effect_verification_note_type = 0x47464c02U;

// Appends the descriptions of the GUI.Forms notes of `type` in one section
// of our own to `out`. Returns false, leaving `out` as it was, when the
// bytes are not a well-formed run of notes (zero padding between notes and
// GUI.Forms notes of other types are allowed).
[[nodiscard]] bool parse_gui_forms_notes(std::span<const std::byte> section, std::uint32_t type,
                                         std::vector<std::span<const std::byte>>& out);

// Copies of the descriptions of every GUI.Forms note of `type` in the
// loaded images, found through STXlib's image regions (stxlib/images.hpp).
// On Windows a non-null `within` reads only the module holding it; a null
// one, and every call elsewhere, reads every image. A malformed run
// contributes nothing. Throws std::bad_alloc when a copy cannot be made.
[[nodiscard]] std::vector<std::vector<std::byte>> gui_forms_note_descriptions(std::uint32_t type,
                                                                            const void* within);

} // namespace gui_forms
