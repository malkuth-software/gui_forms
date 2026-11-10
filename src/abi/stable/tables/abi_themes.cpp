// GF_TABLE_THEMES: theme packages, the installed library and published
// values, over the engine state. See gf_abi.h.

#include "abi_tables.hpp"
#include "abi_records.hpp"
#include "abi_theme_packages.hpp"

#include "../engine/abi_engine.hpp"
#include "../registry/abi_registry.hpp"

#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity load_slot{GF_TABLE_THEMES, 0U};
constexpr SlotIdentity reset_slot{GF_TABLE_THEMES, 1U};
constexpr SlotIdentity install_slot{GF_TABLE_THEMES, 2U};
constexpr SlotIdentity remove_slot{GF_TABLE_THEMES, 3U};
constexpr SlotIdentity diagnostic_at_slot{GF_TABLE_THEMES, 4U};
constexpr SlotIdentity diagnostic_text_slot{GF_TABLE_THEMES, 5U};
constexpr SlotIdentity theme_id_slot{GF_TABLE_THEMES, 6U};
constexpr SlotIdentity publish_value_slot{GF_TABLE_THEMES, 7U};
constexpr SlotIdentity withdraw_value_slot{GF_TABLE_THEMES, 8U};
constexpr SlotIdentity published_value_slot{GF_TABLE_THEMES, 9U};
constexpr SlotIdentity load_image_slot{GF_TABLE_THEMES, 10U};
constexpr SlotIdentity remove_image_slot{GF_TABLE_THEMES, 11U};
constexpr SlotIdentity load_pixels_slot{GF_TABLE_THEMES, 12U};

// The 1.0 size of gf_theme_package: a caller passes at least this much.
static_assert(sizeof(gf_theme_package) == theme_package_size_1_0);
static_assert(sizeof(gf_theme_file) == 32U);
static_assert(sizeof(gf_theme_report) == 16U);
static_assert(sizeof(gf_theme_diagnostic) == 16U);

constexpr std::uint64_t maximum_package_files = GF_THEME_PACKAGE_MAXIMUM_FILES;
constexpr std::uint64_t maximum_package_bytes = GF_THEME_PACKAGE_MAXIMUM_BYTES;

// Validates borrowed bytes: data may be null only when size is zero.
gf_status read_bytes(gf_bytes_view view, SlotIdentity slot, std::string_view& bytes) noexcept {
    if (view.data == nullptr && view.size != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "bytes with a size need data");
    }
    const char* data = reinterpret_cast<const char*>(view.data);
    bytes = std::string_view(data, static_cast<std::size_t>(view.size));
    return GF_STATUS_OK;
}

} // namespace

// Copies a caller's package into the C++ form after checking the whole of
// it: its record, its counts, every view, and the bounds on files and bytes.
gf_status read_theme_package(const gf_theme_package* package, SlotIdentity slot, ThemePackage& made) {
    if (package == nullptr || (*package).struct_size < theme_package_size_1_0 || (*package).reserved != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a theme package needs a record of at least the 1.0 size with reserved zero");
    }
    const std::uint64_t file_count = (*package).file_count;
    if ((*package).files == nullptr && file_count != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a theme package with files needs its file array");
    }
    if (file_count > maximum_package_files) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "a theme package holds at most 256 files");
    }
    std::string_view manifest;
    const gf_status manifest_read = read_bytes((*package).manifest, slot, manifest);
    if (manifest_read != GF_STATUS_OK) {
        return manifest_read;
    }
    // Checked one file at a time, so the running total never overflows: each
    // addend is at most the bound, and the total stops at the bound.
    std::uint64_t total = manifest.size();
    if (total > maximum_package_bytes) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "a theme package holds at most 16 MiB");
    }
    const std::span<const gf_theme_file> files((*package).files, static_cast<std::size_t>(file_count));
    for (const gf_theme_file& file : files) {
        std::string_view path;
        const gf_status path_read = read_text(file.path, slot, path);
        if (path_read != GF_STATUS_OK) {
            return path_read;
        }
        std::string_view bytes;
        const gf_status bytes_read = read_bytes(file.bytes, slot, bytes);
        if (bytes_read != GF_STATUS_OK) {
            return bytes_read;
        }
        if (bytes.size() > maximum_package_bytes - total) {
            return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "a theme package holds at most 16 MiB");
        }
        total = total + bytes.size();
    }
    ThemePackage copied;
    copied.manifest = std::string(manifest);
    copied.files.reserve(files.size());
    for (const gf_theme_file& file : files) {
        ThemePackageFile entry;
        entry.path = std::string(file.path.data, static_cast<std::size_t>(file.path.size));
        entry.bytes = std::string(reinterpret_cast<const char*>(file.bytes.data), static_cast<std::size_t>(file.bytes.size));
        copied.files.push_back(std::move(entry));
    }
    made = std::move(copied);
    return GF_STATUS_OK;
}

void write_theme_report(gf_theme_outcome outcome, std::size_t findings, gf_theme_report* report) noexcept {
    gf_theme_report complete{};
    complete.struct_size = sizeof(gf_theme_report);
    complete.outcome = outcome;
    complete.diagnostic_count = findings;
    write_record(complete, report);
}

namespace {

// Whether any finding is an error (the outcome of an install).
bool has_error(std::span<const ThemeCompileDiagnostic> findings) noexcept {
    for (const ThemeCompileDiagnostic& finding : findings) {
        if (finding.severity == ThemeDiagnosticSeverity::error) {
            return true;
        }
    }
    return false;
}

// Validates a published name as Window::publish_value does.
gf_status read_published_name(gf_utf8_view view, SlotIdentity slot, std::string_view& name) noexcept {
    const gf_status read = read_text(view, slot, name);
    if (read != GF_STATUS_OK) {
        return read;
    }
    if (!valid_style_class_name(name)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a published name is 1 to 64 lowercase letters, digits, hyphens and underscores, "
                    "starting with a letter");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL themes_load(const gf_theme_package* package, gf_theme_report* report) noexcept {
    try {
        const ShadowFrameScope shadow_frame(load_slot.table_id, load_slot.slot_index);
        const gf_status checked = check_record(report, load_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        ThemePackage made;
        const gf_status read = read_theme_package(package, load_slot, made);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const gf_status entered = registry().enter_ui(load_slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        EngineState& state = engine();
        ApplicationResources& resources = state.resources();
        ThemeCompileOptions options;
        options.controls = state.controls();
        options.published_values = state.published_names();
        options.icons = resources.icon_declarations();
        const ThemeCompileResult result = resources.compile_theme_package(made, state.library(), options);
        if (result.succeeded()) {
            try {
                resources.set_theme(result.theme());
            } catch (const std::invalid_argument& refusal) {
                return fail(GF_STATUS_INVALID_STATE, load_slot, refusal.what());
            }
        }
        state.replace_diagnostics(result.diagnostics());
        const gf_theme_outcome outcome = result.succeeded() ? GF_THEME_OUTCOME_APPLIED : GF_THEME_OUTCOME_REFUSED;
        write_theme_report(outcome, state.diagnostics().size(), report);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(load_slot);
    }
}

gf_status GF_CALL themes_reset() noexcept {
    try {
        const ShadowFrameScope shadow_frame(reset_slot.table_id, reset_slot.slot_index);
        const gf_status entered = registry().enter_ui(reset_slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        EngineState& state = engine();
        try {
            state.resources().set_theme(default_theme());
        } catch (const std::invalid_argument& refusal) {
            return fail(GF_STATUS_INVALID_STATE, reset_slot, refusal.what());
        }
        state.clear_diagnostics();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(reset_slot);
    }
}

gf_status GF_CALL themes_install(const gf_theme_package* package, gf_theme_report* report) noexcept {
    try {
        const ShadowFrameScope shadow_frame(install_slot.table_id, install_slot.slot_index);
        const gf_status checked = check_record(report, install_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        ThemePackage made;
        const gf_status read = read_theme_package(package, install_slot, made);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const gf_status entered = registry().enter_ui(install_slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        EngineState& state = engine();
        static_cast<void>(state.resources());
        const std::vector<ThemeCompileDiagnostic> findings = state.library().install(made);
        state.replace_diagnostics(findings);
        const gf_theme_outcome outcome = has_error(findings) ? GF_THEME_OUTCOME_REFUSED : GF_THEME_OUTCOME_APPLIED;
        write_theme_report(outcome, state.diagnostics().size(), report);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(install_slot);
    }
}

gf_status GF_CALL themes_remove(gf_utf8_view name, std::uint32_t* removed) noexcept {
    try {
        const ShadowFrameScope shadow_frame(remove_slot.table_id, remove_slot.slot_index);
        if (removed == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, remove_slot, "removed is null");
        }
        std::string_view text;
        const gf_status read = read_text(name, remove_slot, text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const gf_status entered = registry().enter_ui(remove_slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        EngineState& state = engine();
        static_cast<void>(state.resources());
        const bool was_installed = state.library().remove(text);
        *removed = was_installed ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(remove_slot);
    }
}

// The finding at index, once the UI thread is entered.
gf_status find_diagnostic(std::uint64_t index, SlotIdentity slot, const ThemeCompileDiagnostic*& finding) {
    const gf_status entered = registry().enter_ui(slot, true);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    EngineState& state = engine();
    static_cast<void>(state.resources());
    const std::span<const ThemeCompileDiagnostic> findings = state.diagnostics();
    if (index >= findings.size()) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "no finding has that index");
    }
    finding = &findings[static_cast<std::size_t>(index)];
    return GF_STATUS_OK;
}

gf_status GF_CALL themes_diagnostic_at(std::uint64_t index, gf_theme_diagnostic* record) noexcept {
    try {
        const ShadowFrameScope shadow_frame(diagnostic_at_slot.table_id, diagnostic_at_slot.slot_index);
        const gf_status checked = check_record(record, diagnostic_at_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        const ThemeCompileDiagnostic* finding = nullptr;
        const gf_status found = find_diagnostic(index, diagnostic_at_slot, finding);
        if (found != GF_STATUS_OK) {
            return found;
        }
        gf_theme_diagnostic complete{};
        complete.struct_size = sizeof(gf_theme_diagnostic);
        complete.severity = (*finding).severity == ThemeDiagnosticSeverity::error ? GF_THEME_SEVERITY_ERROR
                                                                                   : GF_THEME_SEVERITY_WARNING;
        complete.line = (*finding).line;
        complete.column = (*finding).column;
        write_record(complete, record);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(diagnostic_at_slot);
    }
}

// The text of a finding a gf_theme_diagnostic_text names; null for an
// unknown one.
const std::string* diagnostic_text_of(const ThemeCompileDiagnostic& finding, gf_theme_diagnostic_text text) noexcept {
    switch (text) {
    case GF_THEME_DIAGNOSTIC_TEXT_CODE:
        return &finding.code;
    case GF_THEME_DIAGNOSTIC_TEXT_NAME:
        return &finding.name;
    case GF_THEME_DIAGNOSTIC_TEXT_FILE:
        return &finding.file;
    case GF_THEME_DIAGNOSTIC_TEXT_RULE:
        return &finding.rule;
    case GF_THEME_DIAGNOSTIC_TEXT_PROPERTY:
        return &finding.property;
    case GF_THEME_DIAGNOSTIC_TEXT_DETAIL:
        return &finding.detail;
    default:
        return nullptr;
    }
}

gf_status GF_CALL themes_diagnostic_text(std::uint64_t index, gf_theme_diagnostic_text text, char* buffer,
                                         std::uint64_t capacity, std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(diagnostic_text_slot.table_id, diagnostic_text_slot.slot_index);
        if (text > GF_THEME_DIAGNOSTIC_TEXT_DETAIL) {
            return fail(GF_STATUS_INVALID_ARGUMENT, diagnostic_text_slot, "unknown diagnostic text");
        }
        const ThemeCompileDiagnostic* finding = nullptr;
        const gf_status found = find_diagnostic(index, diagnostic_text_slot, finding);
        if (found != GF_STATUS_OK) {
            return found;
        }
        const std::string* chosen = diagnostic_text_of(*finding, text);
        const gf_status written = write_text(*chosen, buffer, capacity, required, diagnostic_text_slot);
        return written;
    } catch (...) {
        return fail_with_current_exception(diagnostic_text_slot);
    }
}

gf_status GF_CALL themes_theme_id(char* buffer, std::uint64_t capacity, std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(theme_id_slot.table_id, theme_id_slot.slot_index);
        const gf_status entered = registry().enter_ui(theme_id_slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const Theme& theme = engine().resources().theme();
        const gf_status written = write_text(theme.id(), buffer, capacity, required, theme_id_slot);
        return written;
    } catch (...) {
        return fail_with_current_exception(theme_id_slot);
    }
}

gf_status GF_CALL themes_publish_value(gf_utf8_view name, double value) noexcept {
    try {
        const ShadowFrameScope shadow_frame(publish_value_slot.table_id, publish_value_slot.slot_index);
        std::string_view text;
        const gf_status read = read_published_name(name, publish_value_slot, text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        if (!std::isfinite(value)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, publish_value_slot, "a published value is finite");
        }
        const gf_status entered = registry().enter_ui(publish_value_slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        EngineState& state = engine();
        static_cast<void>(state.resources());
        PublishedInput* input = state.find_published(text);
        if (input == nullptr) {
            if (state.published_count() >= maximum_published_values) {
                return fail(GF_STATUS_LIMIT_EXCEEDED, publish_value_slot, "the library publishes at most 256 names");
            }
            input = &state.add_published(text, value);
        } else {
            (*input).value = value;
            (*input).present = true;
        }
        Window* window = state.running_window();
        if (window != nullptr) {
            static_cast<void>((*window).publish_value(text, value));
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(publish_value_slot);
    }
}

gf_status GF_CALL themes_withdraw_value(gf_utf8_view name) noexcept {
    try {
        const ShadowFrameScope shadow_frame(withdraw_value_slot.table_id, withdraw_value_slot.slot_index);
        std::string_view text;
        const gf_status read = read_published_name(name, withdraw_value_slot, text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const gf_status entered = registry().enter_ui(withdraw_value_slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        EngineState& state = engine();
        static_cast<void>(state.resources());
        PublishedInput* input = state.find_published(text);
        if (input == nullptr || !(*input).present) {
            return GF_STATUS_OK;
        }
        (*input).present = false;
        Window* window = state.running_window();
        if (window != nullptr) {
            (*window).withdraw_value(text);
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(withdraw_value_slot);
    }
}

gf_status GF_CALL themes_published_value(gf_utf8_view name, double* value, std::uint32_t* present) noexcept {
    try {
        const ShadowFrameScope shadow_frame(published_value_slot.table_id, published_value_slot.slot_index);
        if (value == nullptr || present == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, published_value_slot, "value and present are outputs");
        }
        std::string_view text;
        const gf_status read = read_published_name(name, published_value_slot, text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const gf_status entered = registry().enter_ui(published_value_slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        EngineState& state = engine();
        static_cast<void>(state.resources());
        const PublishedInput* input = state.find_published(text);
        if (input == nullptr || !(*input).present) {
            *present = 0U;
            return GF_STATUS_OK;
        }
        *value = (*input).value;
        *present = 1U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(published_value_slot);
    }
}

// The status a refused image load gives.
gf_status image_failure(ImageResourceError error) noexcept {
    switch (error) {
    case ImageResourceError::encoded_limit_exceeded:
    case ImageResourceError::chunk_limit_exceeded:
    case ImageResourceError::dimension_limit_exceeded:
    case ImageResourceError::resource_count_exceeded:
    case ImageResourceError::registry_encoded_limit_exceeded:
    case ImageResourceError::registry_decoded_limit_exceeded:
        return GF_STATUS_LIMIT_EXCEEDED;
    case ImageResourceError::allocation_failed:
        return GF_STATUS_OUT_OF_MEMORY;
    case ImageResourceError::none:
    case ImageResourceError::empty_input:
    case ImageResourceError::invalid_signature:
    case ImageResourceError::truncated_chunk:
    case ImageResourceError::invalid_chunk_type:
    case ImageResourceError::crc_mismatch:
    case ImageResourceError::ihdr_not_first:
    case ImageResourceError::duplicate_ihdr:
    case ImageResourceError::invalid_ihdr:
    case ImageResourceError::unsupported_color_format:
    case ImageResourceError::unsupported_color_profile:
    case ImageResourceError::invalid_palette:
    case ImageResourceError::invalid_transparency:
    case ImageResourceError::invalid_color_metadata:
    case ImageResourceError::noncontiguous_idat:
    case ImageResourceError::missing_idat:
    case ImageResourceError::missing_iend:
    case ImageResourceError::trailing_data:
    case ImageResourceError::unknown_critical_chunk:
    case ImageResourceError::stale_image_id:
        return GF_STATUS_INVALID_ARGUMENT;
    }
    return GF_STATUS_INVALID_ARGUMENT;
}

gf_status GF_CALL themes_load_image(const std::uint8_t* bytes, std::uint64_t size, std::uint64_t* image) noexcept {
    try {
        const ShadowFrameScope shadow_frame(load_image_slot.table_id, load_image_slot.slot_index);
        if (image == nullptr || (bytes == nullptr && size != 0U)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, load_image_slot,
                        "load_image needs an image output, and bytes when size is not zero");
        }
        const gf_status entered = registry().enter_ui(load_image_slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::span<const std::byte> encoded(reinterpret_cast<const std::byte*>(bytes),
                                                 static_cast<std::size_t>(size));
        const ImageLoadResult loaded = engine().resources().load_png(encoded);
        if (!loaded) {
            return fail(image_failure(loaded.error), load_image_slot, image_resource_error_name(loaded.error));
        }
        *image = loaded.image.value;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(load_image_slot);
    }
}

gf_status GF_CALL themes_load_pixels(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height,
                                     std::uint64_t row_bytes, std::uint64_t* image) noexcept {
    try {
        const ShadowFrameScope shadow_frame(load_pixels_slot.table_id, load_pixels_slot.slot_index);
        const bool empty = width == 0U || height == 0U;
        if (image == nullptr || (pixels == nullptr && !empty)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, load_pixels_slot,
                        "load_pixels needs an image output, and pixels unless the image is empty");
        }
        if (!empty && row_bytes < static_cast<std::uint64_t>(width) * 4U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, load_pixels_slot, "a row holds at least width * 4 bytes");
        }
        // The rows end within the address space: row_bytes * height bytes.
        const std::uint64_t address_limit = static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
        if (row_bytes > address_limit || (height != 0U && row_bytes > address_limit / height)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, load_pixels_slot, "the pixel rows exceed the address space");
        }
        const gf_status entered = registry().enter_ui(load_pixels_slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::size_t byte_count = static_cast<std::size_t>(row_bytes) * height;
        const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(pixels), empty ? 0U : byte_count);
        const ImageLoadResult loaded = engine().resources().load_bgra32_premultiplied(width, height, row_bytes, bytes);
        if (!loaded) {
            return fail(image_failure(loaded.error), load_pixels_slot, image_resource_error_name(loaded.error));
        }
        *image = loaded.image.value;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(load_pixels_slot);
    }
}

gf_status GF_CALL themes_remove_image(std::uint64_t image) noexcept {
    try {
        const ShadowFrameScope shadow_frame(remove_image_slot.table_id, remove_image_slot.slot_index);
        const gf_status entered = registry().enter_ui(remove_image_slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if (!engine().resources().remove_image(ImageId{image})) {
            return fail(GF_STATUS_NOT_FOUND, remove_image_slot, "the image registry does not hold that image");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(remove_image_slot);
    }
}

constexpr gf_themes_table table{
    .struct_size = sizeof(gf_themes_table),
    .abi_minor = GF_ABI_MINOR,
    .load = &themes_load,
    .reset = &themes_reset,
    .install = &themes_install,
    .remove = &themes_remove,
    .diagnostic_at = &themes_diagnostic_at,
    .diagnostic_text = &themes_diagnostic_text,
    .theme_id = &themes_theme_id,
    .publish_value = &themes_publish_value,
    .withdraw_value = &themes_withdraw_value,
    .published_value = &themes_published_value,
    .load_image = &themes_load_image,
    .remove_image = &themes_remove_image,
    .load_pixels = &themes_load_pixels,
};

} // namespace

const gf_themes_table& themes_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
