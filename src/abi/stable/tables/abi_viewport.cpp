// The visible-area guarantee's slots (since 1.7; stage VA): the windows
// table's overflow_records, unanswered_overflow_count and safe_insets (slots
// 31 to 33) and the controls table's set_viewport_fit and viewport_fit
// (slots 19 and 20). See gf_abi.h, the slots marked since 1.7. The tables
// themselves are abi_windows.cpp's and abi_controls.cpp's.

#include "abi_control_entry.hpp"
#include "abi_records.hpp"
#include "abi_tables.hpp"

#include "../failure/abi_failure.hpp"
#include "../registry/abi_registry.hpp"
#include "../windows/abi_window_handle.hpp"

#include "gui_forms/layout/viewport_fit/viewport_fit.hpp"
#include "gui_forms/window.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity overflow_records_slot{GF_TABLE_WINDOWS, 31U};
constexpr SlotIdentity unanswered_overflow_count_slot{GF_TABLE_WINDOWS, 32U};
constexpr SlotIdentity safe_insets_slot{GF_TABLE_WINDOWS, 33U};
constexpr SlotIdentity set_viewport_fit_slot{GF_TABLE_CONTROLS, 19U};
constexpr SlotIdentity viewport_fit_slot{GF_TABLE_CONTROLS, 20U};

// The C values number their choices as the C++ enumerations do.
static_assert(static_cast<std::uint32_t>(OverflowAnswer::scroll) == GF_OVERFLOW_ANSWER_SCROLL &&
              static_cast<std::uint32_t>(OverflowAnswer::contain) == GF_OVERFLOW_ANSWER_CONTAIN &&
              static_cast<std::uint32_t>(OverflowAnswer::cover) == GF_OVERFLOW_ANSWER_COVER &&
              static_cast<std::uint32_t>(OverflowAnswer::clip) == GF_OVERFLOW_ANSWER_CLIP);
static_assert(static_cast<std::uint32_t>(OverflowCause::minimum_content_exceeds_viewport) ==
                  GF_OVERFLOW_CAUSE_MINIMUM_CONTENT_EXCEEDS_VIEWPORT &&
              static_cast<std::uint32_t>(OverflowCause::design_size_exceeds_viewport) ==
                  GF_OVERFLOW_CAUSE_DESIGN_SIZE_EXCEEDS_VIEWPORT &&
              static_cast<std::uint32_t>(OverflowCause::placed_child_outside_viewport) ==
                  GF_OVERFLOW_CAUSE_PLACED_CHILD_OUTSIDE_VIEWPORT &&
              static_cast<std::uint32_t>(OverflowCause::minimum_size_exceeds_work_area) ==
                  GF_OVERFLOW_CAUSE_MINIMUM_SIZE_EXCEEDS_WORK_AREA);
static_assert(static_cast<std::uint32_t>(ContentAlignment::top_left) == GF_CONTENT_ALIGNMENT_TOP_LEFT &&
              static_cast<std::uint32_t>(ContentAlignment::top_center) == GF_CONTENT_ALIGNMENT_TOP_CENTER &&
              static_cast<std::uint32_t>(ContentAlignment::top_right) == GF_CONTENT_ALIGNMENT_TOP_RIGHT &&
              static_cast<std::uint32_t>(ContentAlignment::middle_left) == GF_CONTENT_ALIGNMENT_MIDDLE_LEFT &&
              static_cast<std::uint32_t>(ContentAlignment::middle_center) == GF_CONTENT_ALIGNMENT_MIDDLE_CENTER &&
              static_cast<std::uint32_t>(ContentAlignment::middle_right) == GF_CONTENT_ALIGNMENT_MIDDLE_RIGHT &&
              static_cast<std::uint32_t>(ContentAlignment::bottom_left) == GF_CONTENT_ALIGNMENT_BOTTOM_LEFT &&
              static_cast<std::uint32_t>(ContentAlignment::bottom_center) == GF_CONTENT_ALIGNMENT_BOTTOM_CENTER &&
              static_cast<std::uint32_t>(ContentAlignment::bottom_right) == GF_CONTENT_ALIGNMENT_BOTTOM_RIGHT);
static_assert(sizeof(gf_viewport_fit) == 32U && sizeof(gf_overflow_record) == 72U,
              "the 1.7 records keep their sizes");

// The window `window_or_root` names: a window from create, or the window a
// control is in. Fails with the slot's status otherwise.
gf_status find_viewport_window(gf_object window_or_root, SlotIdentity slot, Window*& found) {
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
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, "the visible-area calls take a window or a control in one");
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

// -- the window's overflows and safe insets ----------------------------------------

gf_status windows_overflow_records_body(gf_object window_or_root, gf_overflow_record* records,
                                        std::uint64_t capacity, std::uint64_t* required, char* text_buffer,
                                        std::uint64_t text_capacity, std::uint64_t* text_required) {
    const SlotIdentity slot = overflow_records_slot;
    if (required == nullptr || text_required == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "overflow_records needs both size outputs");
    }
    if (text_capacity > 0U && text_buffer == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a text capacity needs a text buffer");
    }
    std::uint32_t stride = 0U;
    const gf_status arrayed =
        check_array(records, capacity, static_cast<std::uint32_t>(sizeof(gf_overflow_record)), slot, stride);
    if (arrayed != GF_STATUS_OK) {
        return arrayed;
    }
    Window* window = nullptr;
    const gf_status found = find_viewport_window(window_or_root, slot, window);
    if (found != GF_STATUS_OK) {
        return found;
    }
    // The records and their owners' identities are copied out of the window
    // before anything of the caller's is written.
    const stxlib::View<OverflowRecord> held = (*window).overflow_records();
    std::vector<OverflowRecord> kept;
    kept.reserve(held.count());
    for (stxlib::usize index = 0U; index < held.count(); ++index) {
        kept.push_back(held[index]);
    }
    std::vector<std::string> owners;
    owners.reserve(kept.size());
    std::uint64_t text_size = 0U;
    for (const OverflowRecord& record : kept) {
        const Control::Ptr owner = (*window).overflow_owner(record);
        std::string identity = owner ? std::string((*owner).stable_id().value()) : std::string{};
        text_size += static_cast<std::uint64_t>(identity.size());
        owners.push_back(std::move(identity));
    }
    const std::uint64_t count = static_cast<std::uint64_t>(kept.size());
    *required = count;
    *text_required = text_size;
    if (count > capacity || text_size > text_capacity) {
        return fail(GF_STATUS_BUFFER_TOO_SMALL, slot, "the record array or the text buffer is too small");
    }
    std::uint64_t offset = 0U;
    for (std::size_t index = 0U; index < kept.size(); ++index) {
        const OverflowRecord& record = kept[index];
        const std::string& identity = owners[index];
        gf_overflow_record complete{};
        complete.struct_size = static_cast<std::uint32_t>(sizeof(gf_overflow_record));
        complete.cause = static_cast<gf_overflow_cause>(record.cause);
        complete.answer = static_cast<gf_overflow_answer>(record.answer);
        complete.answered = record.answered ? 1U : 0U;
        complete.content_extent = gf_logical_size{record.content_extent.width, record.content_extent.height};
        complete.viewport_extent = gf_logical_size{record.viewport_extent.width, record.viewport_extent.height};
        complete.effective_scale = record.effective_scale;
        if (identity.empty()) {
            complete.owner = gf_utf8_view{nullptr, 0U};
        } else {
            char* const destination = text_buffer + offset;
            std::memcpy(destination, identity.data(), identity.size());
            complete.owner = gf_utf8_view{destination, static_cast<std::uint64_t>(identity.size())};
            offset += static_cast<std::uint64_t>(identity.size());
        }
        // Leaves the caller's struct_size and any later bytes (A-gf-abi-03-F9).
        write_record(complete, &mutable_element_at(records, stride, index));
    }
    return GF_STATUS_OK;
}

gf_status windows_unanswered_overflow_count_body(gf_object window_or_root, std::uint64_t* count) {
    if (count == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, unanswered_overflow_count_slot,
                    "unanswered_overflow_count needs an output");
    }
    Window* window = nullptr;
    const gf_status found = find_viewport_window(window_or_root, unanswered_overflow_count_slot, window);
    if (found != GF_STATUS_OK) {
        return found;
    }
    *count = static_cast<std::uint64_t>((*window).unanswered_overflow_count());
    return GF_STATUS_OK;
}

gf_status windows_safe_insets_body(gf_object window_or_root, gf_logical_insets* insets) {
    if (insets == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, safe_insets_slot, "safe_insets needs an output");
    }
    Window* window = nullptr;
    const gf_status found = find_viewport_window(window_or_root, safe_insets_slot, window);
    if (found != GF_STATUS_OK) {
        return found;
    }
    const SafeInsets reported = (*window).safe_insets();
    *insets = gf_logical_insets{reported.left, reported.top, reported.right, reported.bottom};
    return GF_STATUS_OK;
}

// -- a control's viewport fit -----------------------------------------------------

gf_status controls_set_viewport_fit_body(gf_object object, const gf_viewport_fit* fit) {
    const SlotIdentity slot = set_viewport_fit_slot;
    // The caller's record is read and checked before the control changes.
    if (fit == nullptr || (*fit).struct_size < sizeof(gf_viewport_fit)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the viewport fit is null or smaller than its 1.7 size");
    }
    const gf_viewport_fit& given = *fit;
    if (given.answer > GF_OVERFLOW_ANSWER_CLIP || given.below_minimum > GF_OVERFLOW_ANSWER_CLIP ||
        given.alignment > GF_CONTENT_ALIGNMENT_BOTTOM_RIGHT) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "an answer or the alignment is outside its enumeration");
    }
    ViewportFit converted{};
    converted.answer = static_cast<OverflowAnswer>(given.answer);
    converted.minimum_scale = given.minimum_scale;
    converted.maximum_scale = given.maximum_scale;
    converted.below_minimum = static_cast<OverflowAnswer>(given.below_minimum);
    converted.alignment = static_cast<ContentAlignment>(given.alignment);
    if (!valid_viewport_fit(converted)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the viewport fit is not valid (valid_viewport_fit)");
    }
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    (*control).viewport_fit = converted;
    return GF_STATUS_OK;
}

gf_status controls_viewport_fit_body(gf_object object, gf_viewport_fit* fit) {
    const gf_status checked = check_record(fit, viewport_fit_slot);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, viewport_fit_slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const ViewportFit& held = (*control).viewport_fit.read();
    gf_viewport_fit complete{};
    complete.struct_size = static_cast<std::uint32_t>(sizeof(gf_viewport_fit));
    complete.answer = static_cast<gf_overflow_answer>(held.answer);
    complete.minimum_scale = held.minimum_scale;
    complete.maximum_scale = held.maximum_scale;
    complete.below_minimum = static_cast<gf_overflow_answer>(held.below_minimum);
    complete.alignment = static_cast<gf_content_alignment>(held.alignment);
    // Leaves the caller's struct_size and any later bytes.
    write_record(complete, fit);
    return GF_STATUS_OK;
}

} // namespace

gf_status GF_CALL windows_overflow_records(gf_object window_or_root, gf_overflow_record* records,
                                           std::uint64_t capacity, std::uint64_t* required, char* text_buffer,
                                           std::uint64_t text_capacity, std::uint64_t* text_required) noexcept {
    return call_slot(overflow_records_slot, windows_overflow_records_body, window_or_root, records, capacity,
                     required, text_buffer, text_capacity, text_required);
}

gf_status GF_CALL windows_unanswered_overflow_count(gf_object window_or_root, std::uint64_t* count) noexcept {
    return call_slot(unanswered_overflow_count_slot, windows_unanswered_overflow_count_body, window_or_root,
                     count);
}

gf_status GF_CALL windows_safe_insets(gf_object window_or_root, gf_logical_insets* insets) noexcept {
    return call_slot(safe_insets_slot, windows_safe_insets_body, window_or_root, insets);
}

gf_status GF_CALL controls_set_viewport_fit(gf_object object, const gf_viewport_fit* fit) noexcept {
    return call_slot(set_viewport_fit_slot, controls_set_viewport_fit_body, object, fit);
}

gf_status GF_CALL controls_viewport_fit(gf_object object, gf_viewport_fit* fit) noexcept {
    return call_slot(viewport_fit_slot, controls_viewport_fit_body, object, fit);
}

} // namespace gui_forms::abi1
