// GF_TABLE_WINDOWS' multiple document interface (since 1.6; DK decisions
// 60 to 63): Window's MDI calls over its bound document area. See gf_abi.h,
// the windows table's slots marked since 1.6. The table itself is
// abi_windows.cpp's; these are its slots 20 to 30.

#include "abi_tables.hpp"

#include "../failure/abi_failure.hpp"
#include "../registry/abi_registry.hpp"
#include "../windows/abi_window_handle.hpp"

#include "gui_forms/controls/menu_strip.hpp"
#include "gui_forms/window.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stxlib/optional.hpp>
#include <string>
#include <string_view>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity set_mdi_container_slot{GF_TABLE_WINDOWS, 20U};
constexpr SlotIdentity mdi_container_slot{GF_TABLE_WINDOWS, 21U};
constexpr SlotIdentity mdi_child_count_slot{GF_TABLE_WINDOWS, 22U};
constexpr SlotIdentity mdi_child_at_slot{GF_TABLE_WINDOWS, 23U};
constexpr SlotIdentity active_mdi_child_slot{GF_TABLE_WINDOWS, 24U};
constexpr SlotIdentity activate_mdi_child_slot{GF_TABLE_WINDOWS, 25U};
constexpr SlotIdentity layout_mdi_slot{GF_TABLE_WINDOWS, 26U};
constexpr SlotIdentity set_anchor_bottom_slot{GF_TABLE_WINDOWS, 27U};
constexpr SlotIdentity anchor_bottom_slot{GF_TABLE_WINDOWS, 28U};
constexpr SlotIdentity set_main_menu_strip_slot{GF_TABLE_WINDOWS, 29U};
constexpr SlotIdentity main_menu_strip_slot{GF_TABLE_WINDOWS, 30U};

// gf_mdi_layout and MdiLayout number their values alike.
static_assert(static_cast<std::uint32_t>(MdiLayout::cascade) == GF_MDI_LAYOUT_CASCADE &&
              static_cast<std::uint32_t>(MdiLayout::tile_horizontal) == GF_MDI_LAYOUT_TILE_HORIZONTAL &&
              static_cast<std::uint32_t>(MdiLayout::tile_vertical) == GF_MDI_LAYOUT_TILE_VERTICAL &&
              static_cast<std::uint32_t>(MdiLayout::arrange_icons) == GF_MDI_LAYOUT_ARRANGE_ICONS);

// Enters the UI thread and finds the window of window_or_root: a window
// from create (alive, its run not ended) or the window a control is in.
gf_status find_mdi_window(gf_object window_or_root, SlotIdentity slot, Window*& found) {
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr object;
    const gf_status looked_up = registry().find_component(window_or_root, slot, object);
    if (looked_up != GF_STATUS_OK) {
        return looked_up;
    }
    const std::shared_ptr<WindowHandle> handle = std::dynamic_pointer_cast<WindowHandle>(object);
    if (handle != nullptr) {
        Window* const held = (*handle).window();
        if (!(*handle).is_alive() || held == nullptr) {
            return fail(GF_STATUS_DISPOSED, slot, "the window has been disposed or has closed");
        }
        found = held;
        return GF_STATUS_OK;
    }
    const Control::Ptr control = std::dynamic_pointer_cast<Control>(object);
    if (!control) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, "the MDI calls take a window or a control in one");
    }
    if (!(*control).is_alive()) {
        return fail(GF_STATUS_DISPOSED, slot, "the object has been disposed");
    }
    Window* const window = (*control).attached_window();
    if (window == nullptr) {
        return fail(GF_STATUS_INVALID_STATE, slot, "the control is not in a window");
    }
    found = window;
    return GF_STATUS_OK;
}

// A 0-or-1 switch from the caller.
gf_status read_switch(std::uint32_t value, SlotIdentity slot, bool& on) {
    if (value > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a switch is 0 or 1");
    }
    on = value == 1U;
    return GF_STATUS_OK;
}

// -- the container and the anchor --------------------------------------------------

gf_status windows_set_mdi_container_body(gf_object window_or_root, std::uint32_t container) {
    bool on = false;
    const gf_status read = read_switch(container, set_mdi_container_slot, on);
    if (read != GF_STATUS_OK) {
        return read;
    }
    Window* window = nullptr;
    const gf_status found = find_mdi_window(window_or_root, set_mdi_container_slot, window);
    if (found != GF_STATUS_OK) {
        return found;
    }
    (*window).set_is_mdi_container(on);
    return GF_STATUS_OK;
}

gf_status windows_mdi_container_body(gf_object window_or_root, std::uint32_t* container) {
    if (container == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, mdi_container_slot, "mdi_container needs an output");
    }
    Window* window = nullptr;
    const gf_status found = find_mdi_window(window_or_root, mdi_container_slot, window);
    if (found != GF_STATUS_OK) {
        return found;
    }
    *container = (*window).is_mdi_container() ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status windows_set_anchor_bottom_body(gf_object window_or_root, std::uint32_t bottom) {
    bool on = false;
    const gf_status read = read_switch(bottom, set_anchor_bottom_slot, on);
    if (read != GF_STATUS_OK) {
        return read;
    }
    Window* window = nullptr;
    const gf_status found = find_mdi_window(window_or_root, set_anchor_bottom_slot, window);
    if (found != GF_STATUS_OK) {
        return found;
    }
    (*window).set_mdi_children_minimized_anchor_bottom(on);
    return GF_STATUS_OK;
}

gf_status windows_anchor_bottom_body(gf_object window_or_root, std::uint32_t* bottom) {
    if (bottom == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, anchor_bottom_slot,
                    "mdi_children_minimized_anchor_bottom needs an output");
    }
    Window* window = nullptr;
    const gf_status found = find_mdi_window(window_or_root, anchor_bottom_slot, window);
    if (found != GF_STATUS_OK) {
        return found;
    }
    *bottom = (*window).mdi_children_minimized_anchor_bottom() ? 1U : 0U;
    return GF_STATUS_OK;
}

// -- the children ---------------------------------------------------------------------

gf_status windows_mdi_child_count_body(gf_object window_or_root, std::uint64_t* count) {
    if (count == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, mdi_child_count_slot, "mdi_child_count needs a count output");
    }
    Window* window = nullptr;
    const gf_status found = find_mdi_window(window_or_root, mdi_child_count_slot, window);
    if (found != GF_STATUS_OK) {
        return found;
    }
    const WindowDocumentArea* const area = (*window).document_area();
    const std::size_t children = area != nullptr ? (*area).document_count() : 0U;
    *count = static_cast<std::uint64_t>(children);
    return GF_STATUS_OK;
}

gf_status windows_mdi_child_at_body(gf_object window_or_root, std::uint64_t index, char* buffer,
                                    std::uint64_t capacity, std::uint64_t* required) {
    Window* window = nullptr;
    const gf_status found = find_mdi_window(window_or_root, mdi_child_at_slot, window);
    if (found != GF_STATUS_OK) {
        return found;
    }
    const WindowDocumentArea* const area = (*window).document_area();
    const std::size_t children = area != nullptr ? (*area).document_count() : 0U;
    if (index >= children) {
        return fail(GF_STATUS_NOT_FOUND, mdi_child_at_slot, "the index is past the window's MDI children");
    }
    // Copied before the text is written: the buffer is the caller's.
    const std::string id = (*area).document_at(static_cast<std::size_t>(index)).value;
    return write_text(id, buffer, capacity, required, mdi_child_at_slot);
}

gf_status windows_active_mdi_child_body(gf_object window_or_root, char* buffer, std::uint64_t capacity,
                                        std::uint64_t* required) {
    Window* window = nullptr;
    const gf_status found = find_mdi_window(window_or_root, active_mdi_child_slot, window);
    if (found != GF_STATUS_OK) {
        return found;
    }
    const stxlib::Optional<DockPanelId> active = (*window).active_mdi_child();
    const std::string id = active.present() ? (*active.value()).value : std::string{};
    return write_text(id, buffer, capacity, required, active_mdi_child_slot);
}

gf_status windows_activate_mdi_child_body(gf_object window_or_root, gf_utf8_view child,
                                          std::uint32_t* activated) {
    std::string_view text;
    const gf_status read = read_text(child, activate_mdi_child_slot, text);
    if (read != GF_STATUS_OK) {
        return read;
    }
    Window* window = nullptr;
    const gf_status found = find_mdi_window(window_or_root, activate_mdi_child_slot, window);
    if (found != GF_STATUS_OK) {
        return found;
    }
    const DockPanelId id{std::string(text)};
    const bool done = (*window).activate_mdi_child(id);
    if (activated != nullptr) *activated = done ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status windows_layout_mdi_body(gf_object window_or_root, gf_mdi_layout layout, std::uint32_t* arranged) {
    if (layout > GF_MDI_LAYOUT_ARRANGE_ICONS) {
        return fail(GF_STATUS_INVALID_ARGUMENT, layout_mdi_slot,
                    "a layout is CASCADE, TILE_HORIZONTAL, TILE_VERTICAL or ARRANGE_ICONS");
    }
    Window* window = nullptr;
    const gf_status found = find_mdi_window(window_or_root, layout_mdi_slot, window);
    if (found != GF_STATUS_OK) {
        return found;
    }
    const bool done = (*window).layout_mdi(static_cast<MdiLayout>(layout));
    if (arranged != nullptr) *arranged = done ? 1U : 0U;
    return GF_STATUS_OK;
}

// -- the main menu strip ----------------------------------------------------------------

gf_status windows_set_main_menu_strip_body(gf_object window_or_root, gf_object strip) {
    Window* window = nullptr;
    const gf_status found = find_mdi_window(window_or_root, set_main_menu_strip_slot, window);
    if (found != GF_STATUS_OK) {
        return found;
    }
    Control::Ptr control;
    if (strip != 0U) {
        const gf_status looked_up = registry().find_object(strip, set_main_menu_strip_slot, control);
        if (looked_up != GF_STATUS_OK) {
            return looked_up;
        }
        if (std::dynamic_pointer_cast<MenuStrip>(control) == nullptr) {
            return fail(GF_STATUS_WRONG_HANDLE_KIND, set_main_menu_strip_slot, "the strip is not a menu strip");
        }
        if (!(*control).is_alive()) {
            return fail(GF_STATUS_DISPOSED, set_main_menu_strip_slot, "the strip has been disposed");
        }
    }
    (*window).set_main_menu_strip(control);
    return GF_STATUS_OK;
}

gf_status windows_main_menu_strip_body(gf_object window_or_root, gf_object* strip) {
    if (strip == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, main_menu_strip_slot, "main_menu_strip needs a strip output");
    }
    Window* window = nullptr;
    const gf_status found = find_mdi_window(window_or_root, main_menu_strip_slot, window);
    if (found != GF_STATUS_OK) {
        return found;
    }
    const Control::Ptr held = (*window).main_menu_strip();
    const bool live = held && (*held).is_alive();
    // The reference is made before the output is written: a failed
    // allocation leaves the output as it was.
    const gf_object made = live ? registry().add_reference(held) : gf_object{0U};
    *strip = made;
    return GF_STATUS_OK;
}

} // namespace

gf_status GF_CALL windows_set_mdi_container(gf_object window_or_root, std::uint32_t container) noexcept {
    return call_slot(set_mdi_container_slot, windows_set_mdi_container_body, window_or_root, container);
}

gf_status GF_CALL windows_mdi_container(gf_object window_or_root, std::uint32_t* container) noexcept {
    return call_slot(mdi_container_slot, windows_mdi_container_body, window_or_root, container);
}

gf_status GF_CALL windows_mdi_child_count(gf_object window_or_root, std::uint64_t* count) noexcept {
    return call_slot(mdi_child_count_slot, windows_mdi_child_count_body, window_or_root, count);
}

gf_status GF_CALL windows_mdi_child_at(gf_object window_or_root, std::uint64_t index, char* buffer,
                                       std::uint64_t capacity, std::uint64_t* required) noexcept {
    return call_slot(mdi_child_at_slot, windows_mdi_child_at_body, window_or_root, index, buffer, capacity,
                     required);
}

gf_status GF_CALL windows_active_mdi_child(gf_object window_or_root, char* buffer, std::uint64_t capacity,
                                           std::uint64_t* required) noexcept {
    return call_slot(active_mdi_child_slot, windows_active_mdi_child_body, window_or_root, buffer, capacity,
                     required);
}

gf_status GF_CALL windows_activate_mdi_child(gf_object window_or_root, gf_utf8_view child,
                                             std::uint32_t* activated) noexcept {
    return call_slot(activate_mdi_child_slot, windows_activate_mdi_child_body, window_or_root, child, activated);
}

gf_status GF_CALL windows_layout_mdi(gf_object window_or_root, gf_mdi_layout layout,
                                     std::uint32_t* arranged) noexcept {
    return call_slot(layout_mdi_slot, windows_layout_mdi_body, window_or_root, layout, arranged);
}

gf_status GF_CALL windows_set_mdi_children_minimized_anchor_bottom(gf_object window_or_root,
                                                                   std::uint32_t bottom) noexcept {
    return call_slot(set_anchor_bottom_slot, windows_set_anchor_bottom_body, window_or_root, bottom);
}

gf_status GF_CALL windows_mdi_children_minimized_anchor_bottom(gf_object window_or_root,
                                                               std::uint32_t* bottom) noexcept {
    return call_slot(anchor_bottom_slot, windows_anchor_bottom_body, window_or_root, bottom);
}

gf_status GF_CALL windows_set_main_menu_strip(gf_object window_or_root, gf_object strip) noexcept {
    return call_slot(set_main_menu_strip_slot, windows_set_main_menu_strip_body, window_or_root, strip);
}

gf_status GF_CALL windows_main_menu_strip(gf_object window_or_root, gf_object* strip) noexcept {
    return call_slot(main_menu_strip_slot, windows_main_menu_strip_body, window_or_root, strip);
}

} // namespace gui_forms::abi1
