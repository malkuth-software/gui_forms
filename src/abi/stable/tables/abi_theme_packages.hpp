#pragma once

// Theme packages as GF_TABLE_THEMES and GF_TABLE_APPLICATION read them, and
// the report both write (abi_themes.cpp).

#include "gui_forms/c/gf_abi.h"

#include "../failure/abi_failure.hpp"

#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"

#include <cstddef>
#include <cstdint>

namespace gui_forms::abi1 {

// The 1.0 size of gf_theme_package: the least stride of a package array.
inline constexpr std::uint32_t theme_package_size_1_0 = 40U;

// Copies a caller's package into `made` after checking the whole of it: its
// record, its counts, every view, and the bounds on files and bytes. On a
// refusal (recorded against `slot`) `made` is unchanged. Throws
// std::bad_alloc.
[[nodiscard]] gf_status read_theme_package(const gf_theme_package* package, SlotIdentity slot, ThemePackage& made);
// Writes a complete report of `outcome` and `findings` held findings into
// the caller's record, which check_record has accepted.
void write_theme_report(gf_theme_outcome outcome, std::size_t findings, gf_theme_report* report) noexcept;

} // namespace gui_forms::abi1
