// GF_TABLE_APPLICATION (since 1.1): the application the library's windows
// share (Application::create; APP_WINDOW_RULES_DESIGN Part A).
//
// create makes the C++ application with its presentation and the built-in
// theme, then compiles the configured base and packages into its resources
// as themes.load does, with the library's installed packages for @import
// and the names published so far: one compile, against the same library and
// published values the themes table uses. A refused compile closes and
// destroys the application again, so nothing is made. While the
// application lives, the library's resources are its resources, so every
// window the library makes joins it and shares its theme.

#include "abi_tables.hpp"
#include "abi_records.hpp"
#include "abi_theme_packages.hpp"

#include "../engine/abi_engine.hpp"
#include "../registry/abi_registry.hpp"

#include "gui_forms/application/application.hpp"
#include "gui_forms/components/business_card/business_card.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity create_slot{GF_TABLE_APPLICATION, 0U};
constexpr SlotIdentity close_slot{GF_TABLE_APPLICATION, 1U};
constexpr SlotIdentity state_slot{GF_TABLE_APPLICATION, 2U};
constexpr SlotIdentity set_program_identity_slot{GF_TABLE_APPLICATION, 3U};

// The 1.5 size of gf_program_identity: a caller passes at least this much.
constexpr std::uint32_t program_identity_size_1_5 = 104U;
static_assert(sizeof(gf_program_identity) == program_identity_size_1_5);

// The 1.1 size of gf_application_config: a caller passes at least this much.
constexpr std::uint32_t application_config_size_1_1 = 32U;
static_assert(sizeof(gf_application_config) == application_config_size_1_1);

// The configuration read from a caller's record, validated whole.
struct ReadConfig final {
    ThemeBase base{default_theme_base};
    std::vector<ThemePackage> packages{};
    gf_presentation_source presentation{GF_PRESENTATION_SOURCE_HOST};
};

// Checks the record, the base, the source and every package before anything
// is made. The package count is not bounded here: the compile refuses more
// than the maximum (E601), a finding rather than a failed call; reading is
// bounded by each package's own limits.
gf_status read_config(const gf_application_config* config, ReadConfig& made) {
    if (config == nullptr || (*config).struct_size < application_config_size_1_1 || (*config).reserved != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot,
                    "an application configuration needs a record of at least the 1.1 size with reserved zero");
    }
    ReadConfig read{};
    switch ((*config).theme_base) {
    case GF_THEME_BASE_DEFAULT:
        read.base = default_theme_base;
        break;
    case GF_THEME_BASE_WINDOWS_PROFESSIONAL:
        read.base = ThemeBase::windows_professional;
        break;
    case GF_THEME_BASE_GUI_FORMS:
        read.base = ThemeBase::gui_forms;
        break;
    default:
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "the theme base is unknown");
    }
    switch ((*config).presentation_source) {
    case GF_PRESENTATION_SOURCE_HOST:
    case GF_PRESENTATION_SOURCE_DEFAULTS:
        read.presentation = (*config).presentation_source;
        break;
    default:
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "the presentation source is unknown");
    }
    const std::uint64_t count = (*config).package_count;
    if ((*config).packages == nullptr && count != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "packages are needed for a non-zero package count");
    }
    // One past the maximum is enough for the compile to refuse the count;
    // packages beyond it are neither read nor copied.
    const std::uint64_t kept = count < GF_APPLICATION_MAXIMUM_THEME_PACKAGES + 1U
                                   ? count
                                   : GF_APPLICATION_MAXIMUM_THEME_PACKAGES + 1U;
    // Stepped by the first package's struct_size (N14).
    std::uint32_t stride = 0U;
    const gf_status checked = check_array((*config).packages, kept, theme_package_size_1_0, create_slot, stride);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    read.packages.reserve(static_cast<std::size_t>(kept));
    for (std::uint64_t index = 0U; index < kept; ++index) {
        ThemePackage package;
        const gf_status package_read =
            read_theme_package(&element_at((*config).packages, stride, index), create_slot, package);
        if (package_read != GF_STATUS_OK) {
            return package_read;
        }
        read.packages.push_back(std::move(package));
    }
    made = std::move(read);
    return GF_STATUS_OK;
}

// Ends an application whose theme was refused: it has no window yet.
void discard(std::unique_ptr<Application>& made) {
    const ApplicationResult closed = (*made).close();
    static_cast<void>(closed);
    made.reset();
}

gf_status create_application(const gf_application_config* config, gf_theme_report* report) {
    const gf_status checked = check_record(report, create_slot);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    ReadConfig read{};
    const gf_status config_read = read_config(config, read);
    if (config_read != GF_STATUS_OK) {
        return config_read;
    }
    const gf_status entered = registry().enter_ui(create_slot, true);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    EngineState& state = engine();
    if (state.application() != nullptr || Application::current() != nullptr) {
        return fail(GF_STATUS_INVALID_STATE, create_slot, "an application is current already");
    }
    if (registry().holds_objects()) {
        return fail(GF_STATUS_INVALID_STATE, create_slot, "the library holds objects made without an application");
    }
    ApplicationConfig application_config{};
    if (read.presentation == GF_PRESENTATION_SOURCE_DEFAULTS) {
        application_config.presentation = PresentationSettings{};
    }
    ApplicationCreateResult created = Application::create(std::move(application_config));
    if (created.error == ApplicationCreateError::already_exists) {
        return fail(GF_STATUS_INVALID_STATE, create_slot, "an application is current already");
    }
    if (!created) {
        return fail(GF_STATUS_INTERNAL, create_slot, "the application with the built-in theme was refused");
    }
    std::unique_ptr<Application> made = std::move(created.application);
    const bool compiles = !read.packages.empty() || read.base != default_theme_base;
    std::vector<ThemeCompileDiagnostic> findings{};
    if (compiles) {
        ApplicationResources& resources = *(*made).resources();
        ThemeCompileOptions options;
        options.base = read.base;
        options.controls = state.controls();
        options.published_values = state.published_names();
        options.icons = resources.icon_declarations();
        const ThemeCompileResult compiled = resources.compile_theme_packages(read.packages, state.library(), options);
        findings.assign(compiled.diagnostics().begin(), compiled.diagnostics().end());
        if (!compiled.succeeded()) {
            discard(made);
            state.replace_diagnostics(findings);
            write_theme_report(GF_THEME_OUTCOME_REFUSED, state.diagnostics().size(), report);
            return GF_STATUS_OK;
        }
        resources.set_theme(compiled.theme());
    }
    // The process's theme request (--theme, GUI_FORMS_THEME_SHEET) against
    // the library's packages and the configured base, in place of the one
    // Application::create held without them; the first window settles it.
    state.hold_theme_request(*(*made).resources(), read.base);
    state.adopt_application(std::move(made));
    state.replace_diagnostics(findings);
    write_theme_report(GF_THEME_OUTCOME_APPLIED, state.diagnostics().size(), report);
    return GF_STATUS_OK;
}

gf_status application_create_body(const gf_application_config* config, gf_theme_report* report) {
    const gf_status created = create_application(config, report);
    return created;
}

gf_status GF_CALL application_create(const gf_application_config* config, gf_theme_report* report) noexcept {
    return call_slot(create_slot, application_create_body, config, report);
}

gf_status application_close_body(std::uint64_t* windows_alive) {
    if (windows_alive == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, close_slot, "close needs a window count output");
    }
    const gf_status entered = registry().enter_ui(close_slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    EngineState& state = engine();
    Application* const application = state.application();
    if (application == nullptr) {
        *windows_alive = 0U;
        return GF_STATUS_OK;
    }
    const std::size_t alive = (*application).window_count();
    const ApplicationResult closed = (*application).close();
    if (closed.error == ApplicationError::windows_alive) {
        *windows_alive = alive;
        return fail(GF_STATUS_INVALID_STATE, close_slot, "windows of the application are alive");
    }
    if (!closed.accepted()) {
        return fail(GF_STATUS_INTERNAL, close_slot, "the application refused to close");
    }
    state.release_application();
    *windows_alive = 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL application_close(std::uint64_t* windows_alive) noexcept {
    return call_slot(close_slot, application_close_body, windows_alive);
}

gf_status application_state_body(gf_application_state* state) {
    if (state == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, state_slot, "state needs an output");
    }
    const gf_status entered = registry().enter_ui(state_slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const bool current = engine().application() != nullptr;
    *state = current ? GF_APPLICATION_STATE_CURRENT : GF_APPLICATION_STATE_NONE;
    return GF_STATUS_OK;
}

gf_status GF_CALL application_state(gf_application_state* state) noexcept {
    return call_slot(state_slot, application_state_body, state);
}

// Reads one identity field into `field` (copied: the record is read during
// the call only).
gf_status read_identity_field(gf_utf8_view view, std::string& field) {
    std::string_view text;
    const gf_status read = read_text(view, set_program_identity_slot, text);
    if (read != GF_STATUS_OK) {
        return read;
    }
    field.assign(text);
    return GF_STATUS_OK;
}

gf_status apply_program_identity(const gf_program_identity* identity) {
    if (identity == nullptr || (*identity).struct_size < program_identity_size_1_5 || (*identity).reserved != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_program_identity_slot,
                    "set_program_identity needs a record of at least its 1.5 size with reserved zero");
    }
    const gf_status entered = registry().enter_ui(set_program_identity_slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    ProgramIdentity made;
    const std::array<gf_utf8_view, 6> views{(*identity).product, (*identity).version, (*identity).vendor,
                                            (*identity).website, (*identity).copyright, (*identity).credits};
    const std::array<std::string*, 6> fields{&made.product, &made.version, &made.vendor,
                                             &made.website, &made.copyright, &made.credits};
    for (std::size_t index = 0U; index < views.size(); ++index) {
        const gf_status read = read_identity_field(views[index], *fields[index]);
        if (read != GF_STATUS_OK) {
            return read;
        }
    }
    try {
        set_program_identity(made);
    } catch (const std::invalid_argument& refused) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_program_identity_slot, refused.what());
    }
    return GF_STATUS_OK;
}

gf_status application_set_program_identity_body(const gf_program_identity* identity) {
    const gf_status applied = apply_program_identity(identity);
    return applied;
}

gf_status GF_CALL application_set_program_identity(const gf_program_identity* identity) noexcept {
    return call_slot(set_program_identity_slot, application_set_program_identity_body, identity);
}

constexpr gf_application_table table{
    .struct_size = sizeof(gf_application_table),
    .abi_minor = GF_ABI_MINOR,
    .create = &application_create,
    .close = &application_close,
    .state = &application_state,
    .set_program_identity = &application_set_program_identity,
};

} // namespace

const gf_application_table& application_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
