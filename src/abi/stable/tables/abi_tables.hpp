#pragma once

// The function tables of the C ABI (major 1) beyond the runtime table, one
// source file each. Each table is a constant with static storage, so
// gf_abi_get_table can copy it at any time.

#include "gui_forms/c/gf_abi.h"

#include <cstdint>

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
[[nodiscard]] const gf_application_table& application_table() noexcept;
[[nodiscard]] const gf_rules_table& rules_table() noexcept;
[[nodiscard]] const gf_data_grid_table& data_grid_table() noexcept;

// GF_TABLE_RUNTIME's splashscreen slots (since 1.1, abi_splashscreen.cpp);
// the runtime table itself is the entry's.
gf_status GF_CALL runtime_splashscreen_show(const gf_splashscreen_options* options,
                                            gf_splashscreen* splashscreen) noexcept;
gf_status GF_CALL runtime_splashscreen_set_status(gf_splashscreen splashscreen, gf_utf8_view status) noexcept;
gf_status GF_CALL runtime_splashscreen_set_progress(gf_splashscreen splashscreen, std::uint32_t show_progress,
                                                    double progress) noexcept;
gf_status GF_CALL runtime_splashscreen_close(gf_splashscreen splashscreen) noexcept;
#if defined(GF_ABI_HAS_WINDOWS_PAINT_ENDPOINT)
[[nodiscard]] const gf_windows_paint_endpoint_table& windows_paint_endpoint_table() noexcept;
#endif

} // namespace gui_forms::abi1
