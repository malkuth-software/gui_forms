#pragma once

// The function tables of the C ABI (major 1) beyond the runtime table, one
// source file each. Each table is a constant with static storage, so
// gf_abi_get_table can copy it at any time.

#include "gui_forms/c/gf_abi.h"

namespace gui_forms::abi1 {

[[nodiscard]] const gf_objects_table& objects_table() noexcept;
[[nodiscard]] const gf_properties_table& properties_table() noexcept;
[[nodiscard]] const gf_events_table& events_table() noexcept;
[[nodiscard]] const gf_dispatch_table& dispatch_table() noexcept;
[[nodiscard]] const gf_windows_table& windows_table() noexcept;
[[nodiscard]] const gf_schema_table& schema_table() noexcept;
[[nodiscard]] const gf_themes_table& themes_table() noexcept;
[[nodiscard]] const gf_host_table& host_table() noexcept;
[[nodiscard]] const gf_diagnostics_table& diagnostics_table() noexcept;
[[nodiscard]] const gf_controls_table& controls_table() noexcept;
[[nodiscard]] const gf_text_editing_table& text_editing_table() noexcept;
[[nodiscard]] const gf_overlays_table& overlays_table() noexcept;
[[nodiscard]] const gf_property_grid_table& property_grid_table() noexcept;
[[nodiscard]] const gf_custom_controls_table& custom_controls_table() noexcept;
[[nodiscard]] const gf_bindings_table& bindings_table() noexcept;
[[nodiscard]] const gf_subsurfaces_table& subsurfaces_table() noexcept;
#if defined(GF_ABI_HAS_WINDOWS_PAINT_ENDPOINT)
[[nodiscard]] const gf_windows_paint_endpoint_table& windows_paint_endpoint_table() noexcept;
#endif

} // namespace gui_forms::abi1
