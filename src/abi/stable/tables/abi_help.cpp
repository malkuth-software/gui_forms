// GF_TABLE_HELP (since 1.2, roadmap rows 3.7 and 2.13): a help book
// system's viewer, popup, F1 resolution and What's This mode over the C++
// HelpBookSystem, and its language files: translation, user overrides, the
// way back, the template export and field reads. See gf_abi.h.
//
// Every function runs on the UI thread. The system is a help_book_system
// component the caller created; windows are WindowHandle objects and anchors
// and resolved controls are controls, each found per call, so a disposed
// object is refused rather than reached.

#include "abi_tables.hpp"

#include "../failure/abi_failure.hpp"
#include "../registry/abi_registry.hpp"
#include "../windows/abi_window_handle.hpp"

#include "gui_forms/controls/help/help_book_system/help_book_system.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/text/locale_file/locale_file.hpp"
#include "gui_forms/window.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity attach_slot{GF_TABLE_HELP, 0U};
constexpr SlotIdentity detach_slot{GF_TABLE_HELP, 1U};
constexpr SlotIdentity load_built_in_slot{GF_TABLE_HELP, 2U};
constexpr SlotIdentity show_slot{GF_TABLE_HELP, 3U};
constexpr SlotIdentity show_navigator_slot{GF_TABLE_HELP, 4U};
constexpr SlotIdentity show_popup_slot{GF_TABLE_HELP, 5U};
constexpr SlotIdentity resolve_slot{GF_TABLE_HELP, 6U};
constexpr SlotIdentity enter_help_mode_slot{GF_TABLE_HELP, 7U};
constexpr SlotIdentity cancel_help_mode_slot{GF_TABLE_HELP, 8U};
constexpr SlotIdentity load_translation_slot{GF_TABLE_HELP, 9U};
constexpr SlotIdentity load_user_overrides_slot{GF_TABLE_HELP, 10U};
constexpr SlotIdentity clear_user_overrides_slot{GF_TABLE_HELP, 11U};
constexpr SlotIdentity use_built_in_language_slot{GF_TABLE_HELP, 12U};
constexpr SlotIdentity export_template_slot{GF_TABLE_HELP, 13U};
constexpr SlotIdentity read_field_slot{GF_TABLE_HELP, 14U};

// The C ABI's origin for a resolved field's layer.
[[nodiscard]] gf_locale_origin origin_of(const LocaleResolvedField& field) noexcept {
    if (!field.origin.has_value()) return GF_LOCALE_ORIGIN_NONE;
    switch (*field.origin) {
    case LocaleLayerKind::built_in:
        return GF_LOCALE_ORIGIN_BUILT_IN;
    case LocaleLayerKind::program:
        return GF_LOCALE_ORIGIN_PROGRAM;
    case LocaleLayerKind::translation:
        return GF_LOCALE_ORIGIN_TRANSLATION;
    case LocaleLayerKind::user:
        return GF_LOCALE_ORIGIN_USER;
    }
    return GF_LOCALE_ORIGIN_NONE;
}

// Enters the UI thread and finds system as a live help book system.
gf_status enter_system(gf_object object, SlotIdentity slot, std::shared_ptr<HelpBookSystem>& system) {
    Component::Ptr component;
    const gf_status entered = registry().enter_component(object, slot, component);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    std::shared_ptr<HelpBookSystem> typed = std::dynamic_pointer_cast<HelpBookSystem>(component);
    if (typed == nullptr) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, "the object is not a help_book_system");
    }
    system = std::move(typed);
    return GF_STATUS_OK;
}

// The window a window object holds while it is alive.
gf_status find_window(gf_object object, SlotIdentity slot, Window*& window) {
    Component::Ptr component;
    const gf_status found = registry().find_component(object, slot, component);
    if (found != GF_STATUS_OK) {
        return found;
    }
    const std::shared_ptr<WindowHandle> handle = std::dynamic_pointer_cast<WindowHandle>(component);
    if (handle == nullptr) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, "the object is not a window");
    }
    Window* const held = (*handle).window();
    if (!(*handle).is_alive() || held == nullptr) {
        return fail(GF_STATUS_DISPOSED, slot, "the window has been disposed or has closed");
    }
    window = held;
    return GF_STATUS_OK;
}

// The control an object names, alive.
gf_status find_control(gf_object object, SlotIdentity slot, Control::Ptr& control) {
    Component::Ptr component;
    const gf_status found = registry().find_component(object, slot, component);
    if (found != GF_STATUS_OK) {
        return found;
    }
    Control::Ptr typed = std::dynamic_pointer_cast<Control>(component);
    if (typed == nullptr) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, "the object is not a control");
    }
    if (!(*typed).is_alive()) {
        return fail(GF_STATUS_DISPOSED, slot, "the control has been disposed");
    }
    control = std::move(typed);
    return GF_STATUS_OK;
}

gf_status GF_CALL help_attach(gf_object system, gf_object window) noexcept {
    try {
        const ShadowFrameScope shadow_frame(attach_slot.table_id, attach_slot.slot_index);
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, attach_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Window* target = nullptr;
        const gf_status found = find_window(window, attach_slot, target);
        if (found != GF_STATUS_OK) {
            return found;
        }
        (*help).attach(*target);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(attach_slot);
    }
}

gf_status GF_CALL help_detach(gf_object system, gf_object window) noexcept {
    try {
        const ShadowFrameScope shadow_frame(detach_slot.table_id, detach_slot.slot_index);
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, detach_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Window* target = nullptr;
        const gf_status found = find_window(window, detach_slot, target);
        if (found != GF_STATUS_OK) {
            return found;
        }
        (*help).detach(*target);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(detach_slot);
    }
}

gf_status GF_CALL help_load_built_in(gf_object system, gf_utf8_view locale_xml) noexcept {
    try {
        const ShadowFrameScope shadow_frame(load_built_in_slot.table_id, load_built_in_slot.slot_index);
        std::string_view text;
        const gf_status read = read_text(locale_xml, load_built_in_slot, text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, load_built_in_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const LocaleReadResult loaded = (*help).load_built_in(text);
        if (!loaded) {
            return fail(GF_STATUS_INVALID_ARGUMENT, load_built_in_slot,
                        "the language file's help text was refused; nothing was loaded");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(load_built_in_slot);
    }
}

gf_status GF_CALL help_show(gf_object system, gf_utf8_view stable_id, std::uint32_t* shown) noexcept {
    try {
        const ShadowFrameScope shadow_frame(show_slot.table_id, show_slot.slot_index);
        if (shown == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, show_slot, "show needs a shown output");
        }
        std::string_view id;
        const gf_status read = read_text(stable_id, show_slot, id);
        if (read != GF_STATUS_OK) {
            return read;
        }
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, show_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const bool opened = (*help).show(id);
        *shown = opened ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(show_slot);
    }
}

gf_status GF_CALL help_show_navigator(gf_object system, gf_help_navigator navigator, gf_utf8_view text,
                                      std::uint32_t* shown) noexcept {
    try {
        const ShadowFrameScope shadow_frame(show_navigator_slot.table_id, show_navigator_slot.slot_index);
        if (shown == nullptr || navigator > GF_HELP_NAVIGATOR_SEARCH) {
            return fail(GF_STATUS_INVALID_ARGUMENT, show_navigator_slot,
                        "show_navigator needs a known navigator and a shown output");
        }
        std::string_view words;
        const gf_status read = read_text(text, show_navigator_slot, words);
        if (read != GF_STATUS_OK) {
            return read;
        }
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, show_navigator_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        bool opened = false;
        if (navigator == GF_HELP_NAVIGATOR_CONTENTS) {
            opened = (*help).show_contents();
        } else if (navigator == GF_HELP_NAVIGATOR_INDEX) {
            opened = (*help).show_index(words);
        } else {
            opened = (*help).show_search(words);
        }
        *shown = opened ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(show_navigator_slot);
    }
}

gf_status GF_CALL help_show_popup(gf_object system, gf_utf8_view stable_id, gf_object anchor,
                                  std::uint32_t* shown) noexcept {
    try {
        const ShadowFrameScope shadow_frame(show_popup_slot.table_id, show_popup_slot.slot_index);
        if (shown == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, show_popup_slot, "show_popup needs a shown output");
        }
        std::string_view id;
        const gf_status read = read_text(stable_id, show_popup_slot, id);
        if (read != GF_STATUS_OK) {
            return read;
        }
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, show_popup_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Control::Ptr target;
        const gf_status found = find_control(anchor, show_popup_slot, target);
        if (found != GF_STATUS_OK) {
            return found;
        }
        const bool opened = (*help).show_popup(id, target);
        *shown = opened ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(show_popup_slot);
    }
}

gf_status GF_CALL help_resolve(gf_object system, gf_object control, char* buffer, std::uint64_t capacity,
                               std::uint64_t* required, std::uint32_t* home) noexcept {
    try {
        const ShadowFrameScope shadow_frame(resolve_slot.table_id, resolve_slot.slot_index);
        if (home == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, resolve_slot, "resolve needs a home output");
        }
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, resolve_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Control::Ptr target;
        const gf_status found = find_control(control, resolve_slot, target);
        if (found != GF_STATUS_OK) {
            return found;
        }
        const HelpTarget resolved = (*help).resolve(*target);
        const gf_status written = write_text(resolved.page, buffer, capacity, required, resolve_slot);
        if (written != GF_STATUS_OK) {
            return written;
        }
        *home = resolved.home ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(resolve_slot);
    }
}

gf_status GF_CALL help_enter_help_mode(gf_object system, gf_object window, std::uint32_t* entered_mode) noexcept {
    try {
        const ShadowFrameScope shadow_frame(enter_help_mode_slot.table_id, enter_help_mode_slot.slot_index);
        if (entered_mode == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, enter_help_mode_slot, "enter_help_mode needs an entered output");
        }
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, enter_help_mode_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Window* target = nullptr;
        const gf_status found = find_window(window, enter_help_mode_slot, target);
        if (found != GF_STATUS_OK) {
            return found;
        }
        const bool started = (*help).enter_help_mode(*target);
        *entered_mode = started ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(enter_help_mode_slot);
    }
}

gf_status GF_CALL help_cancel_help_mode(gf_object system) noexcept {
    try {
        const ShadowFrameScope shadow_frame(cancel_help_mode_slot.table_id, cancel_help_mode_slot.slot_index);
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, cancel_help_mode_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*help).cancel_help_mode();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(cancel_help_mode_slot);
    }
}

gf_status GF_CALL help_load_translation(gf_object system, gf_utf8_view locale_xml) noexcept {
    try {
        const ShadowFrameScope shadow_frame(load_translation_slot.table_id, load_translation_slot.slot_index);
        std::string_view text;
        const gf_status read = read_text(locale_xml, load_translation_slot, text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, load_translation_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const LocaleReadResult loaded = (*help).load_translation(text);
        if (!loaded) {
            return fail(GF_STATUS_INVALID_ARGUMENT, load_translation_slot,
                        "the translation was refused; nothing was loaded");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(load_translation_slot);
    }
}

gf_status GF_CALL help_load_user_overrides(gf_object system, gf_utf8_view locale_xml) noexcept {
    try {
        const ShadowFrameScope shadow_frame(load_user_overrides_slot.table_id, load_user_overrides_slot.slot_index);
        std::string_view text;
        const gf_status read = read_text(locale_xml, load_user_overrides_slot, text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, load_user_overrides_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const LocaleReadResult loaded = (*help).load_user_overrides(text);
        if (!loaded) {
            return fail(GF_STATUS_INVALID_ARGUMENT, load_user_overrides_slot,
                        "the user's language file was refused or is for another language; nothing was loaded");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(load_user_overrides_slot);
    }
}

gf_status GF_CALL help_clear_user_overrides(gf_object system) noexcept {
    try {
        const ShadowFrameScope shadow_frame(clear_user_overrides_slot.table_id, clear_user_overrides_slot.slot_index);
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, clear_user_overrides_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*help).clear_user_overrides();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(clear_user_overrides_slot);
    }
}

gf_status GF_CALL help_use_built_in_language(gf_object system, std::uint32_t built_in_only) noexcept {
    try {
        const ShadowFrameScope shadow_frame(use_built_in_language_slot.table_id,
                                            use_built_in_language_slot.slot_index);
        if (built_in_only > 1U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, use_built_in_language_slot, "built_in_only must be 0 or 1");
        }
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, use_built_in_language_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*help).use_built_in_language(built_in_only == 1U);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(use_built_in_language_slot);
    }
}

gf_status GF_CALL help_export_template(gf_object system, gf_utf8_view language, char* buffer, std::uint64_t capacity,
                                       std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(export_template_slot.table_id, export_template_slot.slot_index);
        std::string_view tag;
        const gf_status read = read_text(language, export_template_slot, tag);
        if (read != GF_STATUS_OK) {
            return read;
        }
        if (!tag.empty() && !valid_language_tag(tag)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, export_template_slot, "the language is not a language tag");
        }
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, export_template_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const XmlWriteResult exported = (*help).export_template(tag);
        if (!exported) {
            return fail(GF_STATUS_LIMIT_EXCEEDED, export_template_slot,
                        "the template passes the locale file's bounds and was not written");
        }
        const gf_status written = write_text(exported.text, buffer, capacity, required, export_template_slot);
        return written;
    } catch (...) {
        return fail_with_current_exception(export_template_slot);
    }
}

gf_status GF_CALL help_read_field(gf_object system, gf_utf8_view stable_id, gf_locale_field field, char* buffer,
                                  std::uint64_t capacity, std::uint64_t* required, gf_locale_origin* origin) noexcept {
    try {
        const ShadowFrameScope shadow_frame(read_field_slot.table_id, read_field_slot.slot_index);
        if (origin == nullptr || field > GF_LOCALE_FIELD_KEYWORDS) {
            return fail(GF_STATUS_INVALID_ARGUMENT, read_field_slot, "read_field needs a known field and an origin output");
        }
        std::string_view id;
        const gf_status read = read_text(stable_id, read_field_slot, id);
        if (read != GF_STATUS_OK) {
            return read;
        }
        std::shared_ptr<HelpBookSystem> help;
        const gf_status entered = enter_system(system, read_field_slot, help);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const LocaleResolvedField resolved = (*help).resolve_field(id, static_cast<LocaleField>(field));
        const gf_status written = write_text(resolved.text, buffer, capacity, required, read_field_slot);
        if (written != GF_STATUS_OK) {
            return written;
        }
        *origin = origin_of(resolved);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(read_field_slot);
    }
}

constexpr gf_help_table table{
    .struct_size = sizeof(gf_help_table),
    .abi_minor = GF_ABI_MINOR,
    .attach = &help_attach,
    .detach = &help_detach,
    .load_built_in = &help_load_built_in,
    .show = &help_show,
    .show_navigator = &help_show_navigator,
    .show_popup = &help_show_popup,
    .resolve = &help_resolve,
    .enter_help_mode = &help_enter_help_mode,
    .cancel_help_mode = &help_cancel_help_mode,
    .load_translation = &help_load_translation,
    .load_user_overrides = &help_load_user_overrides,
    .clear_user_overrides = &help_clear_user_overrides,
    .use_built_in_language = &help_use_built_in_language,
    .export_template = &help_export_template,
    .read_field = &help_read_field,
};

} // namespace

const gf_help_table& help_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
