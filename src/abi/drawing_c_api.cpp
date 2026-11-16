#include "gui_forms/drawing_c_api.h"
#include "gui_forms/c/gf_abi.h"

#include "gui_forms/drawing.hpp"
#include "gui_forms/drawing/font_substitution/font_substitution.hpp"
#include "drawing_platform.hpp"
#include "drawing_raster_service.h"
#include "font_resolution_record.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// The longest last-error message a thread keeps, in bytes. A longer one is
// cut at the last whole UTF-8 sequence that fits.
constexpr std::size_t last_error_capacity = 512U;

// One thread's last error, which gd_last_error reports. Plain data (owner
// rule 2: every thread_local trivially destructible), so recording a failure
// never allocates and a thread's exit releases nothing. Each thread reads
// and writes only its own; gd_last_error lends the message until the
// thread's next failure.
struct LastError final {
    gd_result code{GD_OK};
    std::size_t message_size{};
    std::array<char, last_error_capacity> message{};
};

thread_local LastError last_error{};

// The number of leading bytes of text that end on a whole UTF-8 sequence and
// fit in capacity: a cut never lands on a continuation byte (10xxxxxx).
std::size_t utf8_prefix_length(std::string_view text, std::size_t capacity) noexcept {
    if (text.size() <= capacity) return text.size();
    std::size_t length = capacity;
    while (length > 0U && (static_cast<unsigned char>(text[length]) & 0xC0U) == 0x80U) {
        --length;
    }
    return length;
}

// Records the failure as the calling thread's last error and returns it.
gd_result fail(gd_result result, std::string_view message) noexcept {
    const std::size_t kept = utf8_prefix_length(message, last_error_capacity);
    last_error.code = result;
    if (kept != 0U) std::memcpy(last_error.message.data(), message.data(), kept);
    last_error.message_size = kept;
    return result;
}

// Records the failure with a message of two parts, joined in place in the
// last-error buffer: no allocation, so an error path called outside
// translate cannot throw std::bad_alloc through a C entry
// (A-gf-abi-01-F13). Each part is cut on a whole UTF-8 sequence.
gd_result fail_joined(gd_result result, std::string_view first, std::string_view second) noexcept {
    const std::size_t first_kept = utf8_prefix_length(first, last_error_capacity);
    const std::size_t room = last_error_capacity - first_kept;
    const std::size_t second_kept = utf8_prefix_length(second, room);
    last_error.code = result;
    if (first_kept != 0U) std::memcpy(last_error.message.data(), first.data(), first_kept);
    if (second_kept != 0U) std::memcpy(last_error.message.data() + first_kept, second.data(), second_kept);
    last_error.message_size = first_kept + second_kept;
    return result;
}

// A count of caller elements checked before any element is read: below
// minimum, or nonzero with no array, it is an argument fault; above maximum
// it is over its limit (seal decision 4: every over-limit count gives
// GD_ERROR_LIMIT_EXCEEDED).
gd_result check_count(const void* elements, std::uint64_t count, std::uint64_t minimum,
                      std::uint64_t maximum, const char* refusal) {
    if (count > maximum) {
        return fail_joined(GD_ERROR_LIMIT_EXCEEDED, refusal, " (the count is over its limit)");
    }
    if (count < minimum || (count != 0U && elements == nullptr)) {
        return fail(GD_ERROR_INVALID_ARGUMENT, refusal);
    }
    return GD_OK;
}

// The C++ failures as codes (seal decision 4). A plain std::logic_error is
// GUI.Drawing's state refusal (a closed recorder, a bitmap under a lock or
// an edit); its argument refusals are std::invalid_argument, std::out_of_range
// and std::domain_error, and its limits std::length_error.
template <typename Operation, typename... Arguments>
gd_result translate(Operation operation, Arguments&&... arguments) noexcept {
    try {
        if constexpr (std::is_void_v<
                          std::invoke_result_t<Operation, Arguments...>>) {
            std::invoke(operation, std::forward<Arguments>(arguments)...);
            return GD_OK;
        } else {
            return std::invoke(operation,
                               std::forward<Arguments>(arguments)...);
        }
    } catch (const std::bad_alloc& error) {
        return fail(GD_ERROR_OUT_OF_MEMORY, error.what());
    } catch (const std::length_error& error) {
        return fail(GD_ERROR_LIMIT_EXCEEDED, error.what());
    } catch (const std::invalid_argument& error) {
        return fail(GD_ERROR_INVALID_ARGUMENT, error.what());
    } catch (const std::out_of_range& error) {
        return fail(GD_ERROR_INVALID_ARGUMENT, error.what());
    } catch (const std::domain_error& error) {
        return fail(GD_ERROR_INVALID_ARGUMENT, error.what());
    } catch (const std::logic_error& error) {
        return fail(GD_ERROR_INVALID_STATE, error.what());
    } catch (const std::exception& error) {
        return fail(GD_ERROR_INTERNAL, error.what());
    } catch (...) {
        return fail(GD_ERROR_INTERNAL, "GUI.Drawing ABI caught a non-standard exception");
    }
}

[[nodiscard]] gui_drawing::Color color_from_c(gd_color color) {
    if (color.is_empty > 1U) {
        throw std::invalid_argument("color is_empty must be zero or one");
    }
    return color.is_empty != 0U ? gui_drawing::Color::empty() :
                                 gui_drawing::Color::from_argb(color.argb);
}

[[nodiscard]] gd_color color_to_c(gui_drawing::Color color) noexcept {
    return {color.argb(), color.is_empty() ? 1U : 0U};
}

[[nodiscard]] gui_drawing::PointF point_from_c(gd_point point) {
    return {point.x, point.y};
}

[[nodiscard]] gui_drawing::RectF rect_from_c(gd_rect rect) {
    return {rect.x, rect.y, rect.width, rect.height};
}

[[nodiscard]] gui_drawing::Matrix matrix_from_c(gd_matrix matrix) {
    return {matrix.m11, matrix.m12, matrix.m21, matrix.m22,
            matrix.dx, matrix.dy};
}

[[nodiscard]] std::string string_from_c(gd_string_view value,
                                        std::string_view field) {
    if ((value.size != 0U && value.data == nullptr) ||
        value.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(std::string(field) + " is not a bounded string view");
    }
    std::string result;
    if (value.size != 0U) {
        result.assign(value.data, static_cast<std::size_t>(value.size));
    }
    if (result.find('\0') != std::string::npos) {
        throw std::invalid_argument(std::string(field) + " may not contain NUL bytes");
    }
    return result;
}

struct ObjectRecord final {
    std::shared_ptr<gui_drawing::DrawingObject> object;
    std::thread::id owner_thread;
    std::uint32_t kind{};
    std::uint64_t external_references{1};
    // Drawing facade objects may be handed between framework threads. This
    // lock makes the selected operation order the order in which ABI calls
    // acquire each object. Concurrent access is deliberately serialized.
    std::recursive_mutex operation_mutex;
    // A bitmap's native surface refresh staging (drawing_platform.hpp):
    // empty until its first copying refresh, then kept at its size; used
    // under operation_mutex and destroyed once, with this record.
    gui_drawing::abi::platform::SurfaceStagingOwner surface_staging{};
};

struct Slot final {
    std::uint32_t generation{1};
    std::shared_ptr<ObjectRecord> record;
};

class Registry final {
public:
    template <typename Object, typename... Arguments>
    gd_result create(std::uint32_t kind, gd_handle* output, Arguments&&... arguments) {
        if (output == nullptr) {
            return fail(GD_ERROR_INVALID_ARGUMENT, "drawing creation requires an output handle");
        }
        return translate(
            &Registry::create_record<Object, Arguments...>, this, kind, output,
            std::forward<Arguments>(arguments)...);
    }

    template <typename Object>
    gd_result adopt(std::uint32_t kind, gd_handle* output,
                    std::unique_ptr<Object> object) {
        if (output == nullptr || !object) {
            return fail(GD_ERROR_INVALID_ARGUMENT,
                        "drawing adoption requires an object and output handle");
        }
        return translate(&Registry::adopt_record<Object>, this, kind, output,
                         std::move(object));
    }

    // Lock order (A-gf-abi-01-F1): an operation holds its object's
    // operation_mutex and may then take mutex_ (adopting a clone, a nested
    // with). So nothing waits for an operation_mutex while it holds mutex_:
    // retain and release find the record under mutex_, let it go, take the
    // operation lock, and check under mutex_ again that the handle still
    // names that record.
    gd_result retain(gd_handle handle) {
        std::shared_ptr<ObjectRecord> record;
        if (!find_record(handle, record)) return stale("retain");
        const std::scoped_lock<std::recursive_mutex> operation_lock((*record).operation_mutex);
        if (!still_names(handle, record)) return stale("retain");
        if ((*record).owner_thread != std::this_thread::get_id()) {
            (*(*record).object).handoff_to_current_thread();
            (*record).owner_thread = std::this_thread::get_id();
        }
        if ((*record).owner_thread != std::this_thread::get_id()) {
            return wrong_thread("retain");
        }
        if ((*record).external_references ==
            std::numeric_limits<std::uint64_t>::max()) {
            return fail(GD_ERROR_LIMIT_EXCEEDED, "drawing reference count overflow");
        }
        ++(*record).external_references;
        return GD_OK;
    }

    gd_result release(gd_handle handle) {
        // Recycling removes the registry reference; the local one keeps the
        // record and its mutex alive until the operation lock has released
        // it.
        std::shared_ptr<ObjectRecord> record;
        if (!find_record(handle, record)) return stale("release");
        const std::scoped_lock<std::recursive_mutex> operation_lock((*record).operation_mutex);
        if (!still_names(handle, record)) return stale("release");
        if ((*record).owner_thread != std::this_thread::get_id()) {
            (*(*record).object).handoff_to_current_thread();
            (*record).owner_thread = std::this_thread::get_id();
        }
        if ((*record).owner_thread != std::this_thread::get_id()) {
            return wrong_thread("release");
        }
        if (--(*record).external_references == 0U) {
            if (!(*(*record).object).is_disposed()) {
                (*(*record).object).dispose();
            }
            // Taken after the operation lock, the order every operation
            // uses. The slot is recycled only while it still holds this
            // record: every other path that recycles also holds the record's
            // operation lock, so it cannot have changed meanwhile.
            const std::scoped_lock<std::mutex> lock(mutex_);
            Slot* const slot = find_locked(handle);
            if (slot != nullptr && (*slot).record == record) {
                recycle_locked(*slot);
            }
        }
        return GD_OK;
    }

    // dispose, object_state and object_kind hold the object for the whole
    // call (drawing_c_api.h: "Calls on one object are serialized"), with the
    // owner thread checked under the lock (A-gf-abi-01-F2).
    gd_result dispose(gd_handle handle) {
        std::shared_ptr<ObjectRecord> record;
        if (const gd_result result = get(handle, 0U, record, true); result != GD_OK) {
            return result;
        }
        const std::scoped_lock<std::recursive_mutex> operation_lock((*record).operation_mutex);
        if ((*record).owner_thread != std::this_thread::get_id()) {
            return wrong_thread("dispose");
        }
        return translate(&gui_drawing::DrawingObject::dispose,
                         (*record).object.get());
    }

    gd_result object_state(gd_handle handle, std::uint32_t* state) {
        if (state == nullptr) {
            return fail(GD_ERROR_INVALID_ARGUMENT, "object_state requires an output");
        }
        std::shared_ptr<ObjectRecord> record;
        if (const gd_result result = get(handle, 0U, record, true); result != GD_OK) {
            return result;
        }
        const std::scoped_lock<std::recursive_mutex> operation_lock((*record).operation_mutex);
        if ((*record).owner_thread != std::this_thread::get_id()) {
            return wrong_thread("object_state");
        }
        // state() verifies its thread and may throw: through translate, so
        // no exception leaves the C entry.
        return translate(&Registry::read_object_state, (*record).object.get(), state);
    }

    gd_result object_kind(gd_handle handle, std::uint32_t* kind) {
        if (kind == nullptr) {
            return fail(GD_ERROR_INVALID_ARGUMENT, "object_kind requires an output");
        }
        std::shared_ptr<ObjectRecord> record;
        if (const gd_result result = get(handle, 0U, record, true); result != GD_OK) {
            return result;
        }
        const std::scoped_lock<std::recursive_mutex> operation_lock((*record).operation_mutex);
        *kind = (*record).kind;
        return GD_OK;
    }

    template <typename Object, typename Operation, typename... Arguments>
    gd_result with(gd_handle handle, std::uint32_t kind, Operation operation,
                   Arguments&&... arguments) {
        std::shared_ptr<ObjectRecord> record;
        constexpr bool serialized_handoff = true;
        if (const gd_result result = get(handle, kind, record, false,
                                         serialized_handoff); result != GD_OK) {
            return result;
        }
        std::scoped_lock operation_lock((*record).operation_mutex);
        if ((*record).owner_thread != std::this_thread::get_id()) {
            (*(*record).object).handoff_to_current_thread();
            (*record).owner_thread = std::this_thread::get_id();
        }
        if ((*(*record).object).is_disposed()) {
            return fail(GD_ERROR_DISPOSED, "drawing object is disposed");
        }
        std::shared_ptr<Object> object =
            kind == 0U ? std::dynamic_pointer_cast<Object>((*record).object) :
                         std::static_pointer_cast<Object>((*record).object);
        if (!object) return fail(GD_ERROR_WRONG_HANDLE_KIND, "drawing handle type mismatch");
        return translate(operation, *object,
                         std::forward<Arguments>(arguments)...);
    }

    // with<Bitmap> for a native surface refresh: the operation also gets
    // the handle record's surface staging, under the same operation lock.
    template <typename Operation, typename... Arguments>
    gd_result with_surface_staging(gd_handle handle, Operation operation, Arguments&&... arguments) {
        std::shared_ptr<ObjectRecord> record;
        constexpr bool serialized_handoff = true;
        if (const gd_result result = get(handle, GD_OBJECT_BITMAP, record, false,
                                         serialized_handoff); result != GD_OK) {
            return result;
        }
        std::scoped_lock operation_lock((*record).operation_mutex);
        if ((*record).owner_thread != std::this_thread::get_id()) {
            (*(*record).object).handoff_to_current_thread();
            (*record).owner_thread = std::this_thread::get_id();
        }
        if ((*(*record).object).is_disposed()) {
            return fail(GD_ERROR_DISPOSED, "drawing object is disposed");
        }
        std::shared_ptr<gui_drawing::Bitmap> bitmap =
            std::static_pointer_cast<gui_drawing::Bitmap>((*record).object);
        return translate(operation, *bitmap, (*record).surface_staging,
                         std::forward<Arguments>(arguments)...);
    }

    template <typename Left, typename Right, typename Operation,
              typename... Arguments>
    gd_result with_two(gd_handle left_handle, std::uint32_t left_kind,
                       gd_handle right_handle, std::uint32_t right_kind,
                       Operation operation, Arguments&&... arguments) {
        std::shared_ptr<ObjectRecord> left_record;
        if (const gd_result result = get(left_handle, left_kind, left_record, false,
                                         true);
            result != GD_OK) return result;
        std::shared_ptr<ObjectRecord> right_record;
        if (const gd_result result = get(right_handle, right_kind, right_record, false,
                                         true);
            result != GD_OK) return result;
        std::scoped_lock operation_lock((*left_record).operation_mutex,
                                        (*right_record).operation_mutex);
        if ((*left_record).owner_thread != std::this_thread::get_id()) {
            (*(*left_record).object).handoff_to_current_thread();
            (*left_record).owner_thread = std::this_thread::get_id();
        }
        if ((*right_record).owner_thread != std::this_thread::get_id()) {
            (*(*right_record).object).handoff_to_current_thread();
            (*right_record).owner_thread = std::this_thread::get_id();
        }
        if ((*(*left_record).object).is_disposed() ||
            (*(*right_record).object).is_disposed()) {
            return fail(GD_ERROR_DISPOSED, "drawing object is disposed");
        }
        std::shared_ptr<Left> left =
            left_kind == 0U ?
                std::dynamic_pointer_cast<Left>((*left_record).object) :
                std::static_pointer_cast<Left>((*left_record).object);
        std::shared_ptr<Right> right =
            right_kind == 0U ?
                std::dynamic_pointer_cast<Right>((*right_record).object) :
                std::static_pointer_cast<Right>((*right_record).object);
        if (!left || !right) {
            return fail(GD_ERROR_WRONG_HANDLE_KIND, "drawing handle type mismatch");
        }
        return translate(operation, *left, *right,
                         std::forward<Arguments>(arguments)...);
    }

    template <typename Operation, typename... Arguments>
    gd_result with_text_objects(gd_handle recorder_handle,
                                gd_handle font_handle,
                                gd_handle brush_handle,
                                gd_handle format_handle,
                                Operation operation,
                                Arguments&&... arguments) {
        std::shared_ptr<ObjectRecord> recorder_record;
        std::shared_ptr<ObjectRecord> font_record;
        std::shared_ptr<ObjectRecord> brush_record;
        std::shared_ptr<ObjectRecord> format_record;
        if (const gd_result result = get(recorder_handle, GD_OBJECT_RECORDER,
                                    recorder_record, false, true); result != GD_OK) return result;
        if (const gd_result result = get(font_handle, GD_OBJECT_FONT,
                                    font_record, false, true); result != GD_OK) return result;
        if (const gd_result result = get(brush_handle, GD_OBJECT_SOLID_BRUSH,
                                    brush_record, false, true); result != GD_OK) return result;
        if (const gd_result result = get(format_handle, GD_OBJECT_STRING_FORMAT,
                                    format_record, false, true); result != GD_OK) return result;
        std::scoped_lock operation_lock((*recorder_record).operation_mutex,
                                        (*font_record).operation_mutex,
                                        (*brush_record).operation_mutex,
                                        (*format_record).operation_mutex);
        const std::thread::id current_thread = std::this_thread::get_id();
        for (ObjectRecord* record : {recorder_record.get(), font_record.get(),
                                     brush_record.get(), format_record.get()}) {
            if ((*record).owner_thread != current_thread) {
                (*(*record).object).handoff_to_current_thread();
                (*record).owner_thread = current_thread;
            }
            if ((*(*record).object).is_disposed()) {
                return fail(GD_ERROR_DISPOSED, "drawing object is disposed");
            }
        }
        std::shared_ptr<gui_drawing::GraphicsRecorder> recorder = std::dynamic_pointer_cast<gui_drawing::GraphicsRecorder>((*recorder_record).object);
        std::shared_ptr<gui_drawing::Font> font = std::dynamic_pointer_cast<gui_drawing::Font>((*font_record).object);
        std::shared_ptr<gui_drawing::SolidBrush> brush = std::dynamic_pointer_cast<gui_drawing::SolidBrush>((*brush_record).object);
        std::shared_ptr<gui_drawing::StringFormat> format = std::dynamic_pointer_cast<gui_drawing::StringFormat>((*format_record).object);
        if (!recorder || !font || !brush || !format) {
            return fail(GD_ERROR_WRONG_HANDLE_KIND, "drawing text handle type mismatch");
        }
        return translate(operation, *recorder, *font, *brush, *format,
                         std::forward<Arguments>(arguments)...);
    }

    // with_text_objects without a brush (recorder_draw_strings, ABI 0.5):
    // the recorder, the font and the format, locked together for the call.
    template <typename Operation, typename... Arguments>
    gd_result with_strings_objects(gd_handle recorder_handle, gd_handle font_handle, gd_handle format_handle,
                                   Operation operation, Arguments&&... arguments) {
        std::shared_ptr<ObjectRecord> recorder_record;
        std::shared_ptr<ObjectRecord> font_record;
        std::shared_ptr<ObjectRecord> format_record;
        if (const gd_result result = get(recorder_handle, GD_OBJECT_RECORDER,
                                    recorder_record, false, true); result != GD_OK) return result;
        if (const gd_result result = get(font_handle, GD_OBJECT_FONT,
                                    font_record, false, true); result != GD_OK) return result;
        if (const gd_result result = get(format_handle, GD_OBJECT_STRING_FORMAT,
                                    format_record, false, true); result != GD_OK) return result;
        std::scoped_lock operation_lock((*recorder_record).operation_mutex,
                                        (*font_record).operation_mutex,
                                        (*format_record).operation_mutex);
        const std::thread::id current_thread = std::this_thread::get_id();
        for (ObjectRecord* record : {recorder_record.get(), font_record.get(), format_record.get()}) {
            if ((*record).owner_thread != current_thread) {
                (*(*record).object).handoff_to_current_thread();
                (*record).owner_thread = current_thread;
            }
            if ((*(*record).object).is_disposed()) {
                return fail(GD_ERROR_DISPOSED, "drawing object is disposed");
            }
        }
        std::shared_ptr<gui_drawing::GraphicsRecorder> recorder = std::dynamic_pointer_cast<gui_drawing::GraphicsRecorder>((*recorder_record).object);
        std::shared_ptr<gui_drawing::Font> font = std::dynamic_pointer_cast<gui_drawing::Font>((*font_record).object);
        std::shared_ptr<gui_drawing::StringFormat> format = std::dynamic_pointer_cast<gui_drawing::StringFormat>((*format_record).object);
        if (!recorder || !font || !format) {
            return fail(GD_ERROR_WRONG_HANDLE_KIND, "drawing text handle type mismatch");
        }
        return translate(operation, *recorder, *font, *format, std::forward<Arguments>(arguments)...);
    }

private:
    template <typename Object, typename... Arguments>
    gd_result create_record(std::uint32_t kind, gd_handle* output,
                            Arguments&&... arguments) {
        std::shared_ptr<ObjectRecord> record =
            std::make_shared<ObjectRecord>();
        (*record).object =
            std::make_shared<Object>(std::forward<Arguments>(arguments)...);
        (*record).owner_thread = std::this_thread::get_id();
        (*record).kind = kind;
        std::scoped_lock lock(mutex_);
        *output = allocate_locked(std::move(record));
        return GD_OK;
    }

    template <typename Object>
    gd_result adopt_record(std::uint32_t kind, gd_handle* output,
                           std::unique_ptr<Object> object) {
        std::shared_ptr<ObjectRecord> record =
            std::make_shared<ObjectRecord>();
        (*record).object = std::shared_ptr<Object>(std::move(object));
        (*record).owner_thread = std::this_thread::get_id();
        (*record).kind = kind;
        std::scoped_lock lock(mutex_);
        *output = allocate_locked(std::move(record));
        return GD_OK;
    }

    gd_result get(gd_handle handle, std::uint32_t expected_kind,
                  std::shared_ptr<ObjectRecord>& record,
                  bool permit_disposed,
                  bool permit_serialized_handoff = false) {
        {
            std::scoped_lock lock(mutex_);
            Slot* slot = find_locked(handle);
            if (slot == nullptr) return stale("operation");
            record = (*slot).record;
        }
        std::scoped_lock operation_lock((*record).operation_mutex);
        if (!permit_serialized_handoff &&
            (*record).owner_thread != std::this_thread::get_id()) {
            return wrong_thread("operation");
        }
        if (expected_kind != 0U && (*record).kind != expected_kind) {
            return fail(GD_ERROR_WRONG_HANDLE_KIND,
                        "drawing handle has the wrong object kind");
        }
        // A recorder's disposed check is performed by handoff_to_current_thread
        // after the serialized ownership transfer. Calling is_disposed here
        // would itself violate the old owner's affinity.
        if (!permit_disposed &&
            !permit_serialized_handoff &&
            (*(*record).object).is_disposed()) {
            return fail(GD_ERROR_DISPOSED, "drawing object is disposed");
        }
        return GD_OK;
    }

    gd_handle allocate_locked(std::shared_ptr<ObjectRecord> record) {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            if (!slots_[index].record) {
                slots_[index].record = std::move(record);
                return {static_cast<std::uint32_t>(index + 1U), slots_[index].generation};
            }
        }
        if (slots_.size() >= std::numeric_limits<std::uint32_t>::max() - 1U) {
            throw std::length_error("drawing handle table exhausted");
        }
        slots_.push_back({});
        slots_.back().record = std::move(record);
        return {static_cast<std::uint32_t>(slots_.size()), slots_.back().generation};
    }

    // The record handle names, copied under mutex_ (released on return).
    bool find_record(gd_handle handle, std::shared_ptr<ObjectRecord>& record) {
        const std::scoped_lock<std::mutex> lock(mutex_);
        Slot* const slot = find_locked(handle);
        if (slot == nullptr) return false;
        record = (*slot).record;
        return true;
    }

    // Whether handle still names record (called under the record's
    // operation lock, so mutex_ is taken in the operation order).
    bool still_names(gd_handle handle, const std::shared_ptr<ObjectRecord>& record) {
        const std::scoped_lock<std::mutex> lock(mutex_);
        Slot* const slot = find_locked(handle);
        const bool same = slot != nullptr && (*slot).record == record;
        return same;
    }

    static void read_object_state(gui_drawing::DrawingObject* object, std::uint32_t* state) {
        *state = static_cast<std::uint32_t>((*object).state());
    }

    Slot* find_locked(gd_handle handle) noexcept {
        if (handle.slot == 0U || handle.slot > slots_.size()) return nullptr;
        Slot& slot = slots_[handle.slot - 1U];
        return slot.record && slot.generation == handle.generation ? &slot : nullptr;
    }

    static void recycle_locked(Slot& slot) noexcept {
        slot.record.reset();
        ++slot.generation;
        if (slot.generation == 0U) ++slot.generation;
    }

    static gd_result stale(std::string_view operation) noexcept {
        return fail_joined(GD_ERROR_STALE_HANDLE, operation, " received a stale drawing handle");
    }

    static gd_result wrong_thread(std::string_view operation) noexcept {
        return fail_joined(GD_ERROR_WRONG_THREAD, operation, " must run on the drawing owner thread");
    }

    std::mutex mutex_;
    std::vector<Slot> slots_;
};

// The registry and the raster service's lock are never destroyed, as the
// stable ABI's registry is not (termination releases none of our state,
// rule 1; A-gf-abi-01-F5): a thread still inside a gd_* call at exit never
// meets a destroyed mutex or record, and no record's staging is released
// under Windows' loader lock at process detach.
Registry& registry() {
    static Registry* const value = new Registry();
    return *value;
}

std::mutex& raster_service_mutex() {
    static std::mutex* const value = new std::mutex();
    return *value;
}

gd_raster_service_v0 raster_service{};


[[nodiscard]] bool load_raster_service(gd_raster_service_v0& output) {
    std::scoped_lock lock(raster_service_mutex());
    if (raster_service.abi_version == 0U) return false;
    output = raster_service;
    return true;
}

// A-gf-abi-01-F12: an HDC lease, a pixel lock and a bounded edit each lend
// pointers into the bitmap's storage, so dispose waits for all three to be
// returned. A release that reaches zero is not refused: the caller's
// reference is gone either way, and refusing would leave it counted.
gd_result verify_bitmap_is_not_lent(gui_drawing::Bitmap& bitmap) {
    if (gui_drawing::abi::platform::has_hdc_lease(bitmap)) {
        return fail(GD_ERROR_INVALID_STATE,
                    "bitmap cannot be disposed during an HDC lease");
    }
    if (bitmap.locked()) {
        return fail(GD_ERROR_INVALID_STATE,
                    "bitmap cannot be disposed during a pixel lock");
    }
    if (bitmap.editing()) {
        return fail(GD_ERROR_INVALID_STATE,
                    "bitmap cannot be disposed during a bounded edit");
    }
    return GD_OK;
}

gd_result create_solid_brush(gd_color color, gd_handle* output) {
    const gui_drawing::Color native = color_from_c(color);
    return registry().create<gui_drawing::SolidBrush>(
        GD_OBJECT_SOLID_BRUSH, output, native);
}

gd_result create_pen(gd_color color, double width, gd_handle* output) {
    const gui_drawing::Color native = color_from_c(color);
    return registry().create<gui_drawing::Pen>(GD_OBJECT_PEN, output, native,
                                               width);
}

gd_result create_font(gd_string_view family, double size,
                      std::uint32_t style, std::uint32_t unit,
                      std::uint32_t charset, gd_handle* output) {
    std::string native_family = string_from_c(family, "font family");
    return registry().create<gui_drawing::Font>(
        GD_OBJECT_FONT, output, std::move(native_family), size,
        static_cast<gui_drawing::FontStyle>(style),
        static_cast<gui_drawing::GraphicsUnit>(unit),
        static_cast<std::uint8_t>(charset));
}

gd_result configure_string_format(gui_drawing::StringFormat& format,
                                  std::uint32_t alignment,
                                  std::uint32_t line_alignment,
                                  std::uint32_t trimming,
                                  std::uint32_t flags) {
    format.set_alignment(
        static_cast<gui_drawing::StringAlignment>(alignment));
    format.set_line_alignment(
        static_cast<gui_drawing::StringAlignment>(line_alignment));
    format.set_trimming(static_cast<gui_drawing::StringTrimming>(trimming));
    format.set_flags(flags);
    return GD_OK;
}

gd_result save_recorder(gui_drawing::GraphicsRecorder& recorder,
                        std::uint64_t* token) {
    *token = recorder.save().value;
    return GD_OK;
}

gd_result restore_recorder(gui_drawing::GraphicsRecorder& recorder,
                           std::uint64_t token) {
    recorder.restore({token});
    return GD_OK;
}

gd_result set_recorder_transform(gui_drawing::GraphicsRecorder& recorder,
                                 gd_matrix transform) {
    recorder.set_transform(matrix_from_c(transform));
    return GD_OK;
}

gd_result set_recorder_clip(gui_drawing::GraphicsRecorder& recorder,
                            gd_rect clip) {
    recorder.set_clip(rect_from_c(clip));
    return GD_OK;
}

gd_result set_recorder_quality(gui_drawing::GraphicsRecorder& recorder,
                               std::uint32_t smoothing,
                               std::uint32_t interpolation,
                               std::uint32_t pixel_offset,
                               std::uint32_t compositing,
                               std::uint32_t compositing_quality) {
    recorder.set_quality(
        static_cast<gui_drawing::SmoothingMode>(smoothing),
        static_cast<gui_drawing::InterpolationMode>(interpolation),
        static_cast<gui_drawing::PixelOffsetMode>(pixel_offset),
        static_cast<gui_drawing::CompositingMode>(compositing),
        static_cast<gui_drawing::CompositingQuality>(compositing_quality));
    return GD_OK;
}

gd_result query_recorder_visibility(gui_drawing::GraphicsRecorder& recorder,
                                    gd_point point,
                                    std::uint32_t* visible) {
    *visible = recorder.is_visible(point_from_c(point)) ? 1U : 0U;
    return GD_OK;
}

gd_result clear_recorder_native(gui_drawing::GraphicsRecorder& recorder,
                                gui_drawing::Color color) {
    recorder.clear(color);
    return GD_OK;
}

gd_result clear_recorder(gd_handle handle, gd_color color) {
    const gui_drawing::Color native = color_from_c(color);
    return registry().with<gui_drawing::GraphicsRecorder>(
        handle, GD_OBJECT_RECORDER, &clear_recorder_native, native);
}

gd_result fill_recorder_rectangle(gui_drawing::GraphicsRecorder& graphics,
                                  gui_drawing::Brush& fill, gd_rect rect) {
    graphics.fill_rectangle(fill, rect_from_c(rect));
    return GD_OK;
}

gd_result draw_recorder_rectangle(gui_drawing::GraphicsRecorder& graphics,
                                  gui_drawing::Pen& stroke, gd_rect rect) {
    graphics.draw_rectangle(stroke, rect_from_c(rect));
    return GD_OK;
}

gd_result draw_recorder_line(gui_drawing::GraphicsRecorder& graphics,
                             gui_drawing::Pen& stroke, gd_point from,
                             gd_point to) {
    graphics.draw_line(stroke, point_from_c(from), point_from_c(to));
    return GD_OK;
}

gd_result draw_recorder_string(gui_drawing::GraphicsRecorder& graphics,
                               gui_drawing::Font& font,
                               gui_drawing::SolidBrush& brush,
                               gui_drawing::StringFormat& format,
                               const std::string& text, gd_point origin) {
    graphics.draw_string(text, font, brush, point_from_c(origin), format);
    return GD_OK;
}

gd_result draw_recorder_string_from_c(gd_handle recorder,
                                      gd_string_view text, gd_handle font,
                                      gd_handle brush, gd_point origin,
                                      gd_handle format) {
    const std::string native_text = string_from_c(text, "draw string text");
    return registry().with_text_objects(
        recorder, font, brush, format, &draw_recorder_string, native_text,
        origin);
}

gd_result draw_recorder_string_rotated(gui_drawing::GraphicsRecorder& graphics,
                                       gui_drawing::Font& font,
                                       gui_drawing::SolidBrush& brush,
                                       gui_drawing::StringFormat& format,
                                       const std::string& text, gd_point origin,
                                       double degrees) {
    graphics.draw_string_rotated(text, font, brush, point_from_c(origin), degrees, format);
    return GD_OK;
}

gd_result draw_recorder_string_rotated_from_c(gd_handle recorder,
                                              gd_string_view text, gd_handle font,
                                              gd_handle brush, gd_point origin,
                                              double degrees, gd_handle format) {
    const std::string native_text = string_from_c(text, "draw string text");
    return registry().with_text_objects(
        recorder, font, brush, format, &draw_recorder_string_rotated, native_text,
        origin, degrees);
}

gd_result query_recorder_command_count(
    gui_drawing::GraphicsRecorder& recorder, std::uint64_t* count) {
    *count = recorder.commands().size();
    return GD_OK;
}

gd_result read_recorder_trace(gui_drawing::GraphicsRecorder& recorder,
                              char* buffer, std::uint64_t capacity,
                              std::uint64_t* required_size) {
    const std::string trace = recorder.deterministic_trace();
    *required_size = trace.size();
    if (capacity < trace.size() ||
        (trace.size() != 0U && buffer == nullptr)) {
        return fail(GD_ERROR_BUFFER_TOO_SMALL,
                    "trace buffer is smaller than the required byte count");
    }
    if (!trace.empty()) std::memcpy(buffer, trace.data(), trace.size());
    return GD_OK;
}

gd_result add_path_line(gui_drawing::GraphicsPath& path, gd_point from,
                        gd_point to) {
    path.add_line(point_from_c(from), point_from_c(to));
    return GD_OK;
}

gd_result add_path_rectangle(gui_drawing::GraphicsPath& path,
                             gd_rect rectangle) {
    path.add_rectangle(rect_from_c(rectangle));
    return GD_OK;
}

gd_result add_path_ellipse(gui_drawing::GraphicsPath& path, gd_rect bounds) {
    path.add_ellipse(rect_from_c(bounds));
    return GD_OK;
}

gd_result query_path_bounds(gui_drawing::GraphicsPath& path, gd_rect* bounds) {
    const gui_drawing::RectF value = path.bounds();
    *bounds = {value.x, value.y, value.width, value.height};
    return GD_OK;
}

gd_result draw_recorder_ellipse(gui_drawing::GraphicsRecorder& graphics,
                                gui_drawing::Pen& stroke, gd_rect bounds) {
    graphics.draw_ellipse(stroke, rect_from_c(bounds));
    return GD_OK;
}

gd_result fill_recorder_ellipse(gui_drawing::GraphicsRecorder& graphics,
                                gui_drawing::Brush& fill, gd_rect bounds) {
    graphics.fill_ellipse(fill, rect_from_c(bounds));
    return GD_OK;
}

gd_result fill_recorder_polygon(
    gui_drawing::GraphicsRecorder& graphics, gui_drawing::Brush& fill,
    const std::vector<gui_drawing::PointF>& points, std::uint32_t fill_mode) {
    graphics.fill_polygon(fill, points,
                          static_cast<gui_drawing::FillMode>(fill_mode));
    return GD_OK;
}

gd_result fill_recorder_polygon_from_c(gd_handle recorder, gd_handle brush,
                                       const gd_point* points,
                                       std::uint64_t count,
                                       std::uint32_t fill_mode) {
    std::vector<gui_drawing::PointF> native_points;
    native_points.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        native_points.push_back(point_from_c(points[index]));
    }
    return registry().with_two<gui_drawing::GraphicsRecorder,
                               gui_drawing::Brush>(
        recorder, GD_OBJECT_RECORDER, brush, 0U, &fill_recorder_polygon,
        native_points, fill_mode);
}

gd_result draw_resolved_path(gui_drawing::GraphicsPath& path,
                             gui_drawing::GraphicsRecorder* graphics,
                             gui_drawing::Pen* stroke) {
    (*graphics).draw_path(*stroke, path);
    return GD_OK;
}

gd_result draw_recorder_path(gui_drawing::GraphicsRecorder& graphics,
                             gui_drawing::Pen& stroke, gd_handle path) {
    return registry().with<gui_drawing::GraphicsPath>(
        path, GD_OBJECT_GRAPHICS_PATH, &draw_resolved_path, &graphics,
        &stroke);
}

gd_result fill_resolved_path(gui_drawing::GraphicsPath& path,
                             gui_drawing::GraphicsRecorder* graphics,
                             gui_drawing::Brush* fill) {
    (*graphics).fill_path(*fill, path);
    return GD_OK;
}

gd_result fill_recorder_path(gui_drawing::GraphicsRecorder& graphics,
                             gui_drawing::Brush& fill, gd_handle path) {
    return registry().with<gui_drawing::GraphicsPath>(
        path, GD_OBJECT_GRAPHICS_PATH, &fill_resolved_path, &graphics, &fill);
}

gd_result draw_resolved_image(
    gui_drawing::ImageAttributes& attributes,
    gui_drawing::GraphicsRecorder* graphics,
    gui_drawing::ImageReference* image, gd_rect destination, gd_rect source) {
    (*graphics).draw_image(*image, rect_from_c(destination),
                           rect_from_c(source), attributes);
    return GD_OK;
}

gd_result draw_recorder_image(gui_drawing::GraphicsRecorder& graphics,
                              gui_drawing::ImageReference& image,
                              gd_rect destination, gd_rect source,
                              gd_handle attributes) {
    return registry().with<gui_drawing::ImageAttributes>(
        attributes, GD_OBJECT_IMAGE_ATTRIBUTES, &draw_resolved_image,
        &graphics, &image, destination, source);
}

gd_result query_bitmap_dimensions(gui_drawing::Bitmap& bitmap,
                                  std::uint32_t* width,
                                  std::uint32_t* height,
                                  std::uint32_t* pixel_format,
                                  std::uint64_t* generation) {
    *width = bitmap.width();
    *height = bitmap.height();
    *pixel_format = static_cast<std::uint32_t>(bitmap.pixel_format());
    *generation = bitmap.generation();
    return GD_OK;
}

gd_result query_bitmap_pixel(gui_drawing::Bitmap& bitmap, std::uint32_t x,
                             std::uint32_t y, gd_color* color) {
    *color = color_to_c(bitmap.get_pixel(x, y));
    return GD_OK;
}

gd_result set_bitmap_pixel_native(gui_drawing::Bitmap& bitmap,
                                  std::uint32_t x, std::uint32_t y,
                                  gui_drawing::Color color) {
    bitmap.set_pixel(x, y, color);
    return GD_OK;
}

gd_result set_bitmap_pixel(gd_handle handle, std::uint32_t x,
                           std::uint32_t y, gd_color color) {
    const gui_drawing::Color native = color_from_c(color);
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &set_bitmap_pixel_native, x, y, native);
}

gd_result lock_bitmap(gui_drawing::Bitmap& bitmap, std::uint32_t mode,
                      gd_bitmap_lock_view* view) {
    const gui_drawing::BitmapLockView native = bitmap.lock(
        static_cast<gui_drawing::BitmapLockMode>(mode));
    *view = {native.data, native.writable_data, native.row_bytes,
             native.width, native.height,
             static_cast<std::uint32_t>(native.pixel_format), native.token};
    return GD_OK;
}

gd_result begin_bitmap_edit(gui_drawing::Bitmap& bitmap, gd_rect_i bounds,
                            gd_bitmap_edit_view* view) {
    const gui_drawing::BitmapEditView native = bitmap.begin_edit(
        {bounds.x, bounds.y, bounds.width, bounds.height});
    *view = {native.data, native.writable_data, native.row_bytes,
             {native.bounds.x, native.bounds.y, native.bounds.width,
              native.bounds.height},
             static_cast<std::uint32_t>(native.pixel_format), native.token};
    return GD_OK;
}

gd_result commit_bitmap_edit(gui_drawing::Bitmap& bitmap,
                             std::uint64_t token,
                             std::uint64_t* generation) {
    *generation = bitmap.commit_edit(token);
    return GD_OK;
}

gd_result query_bitmap_changes(gui_drawing::Bitmap& bitmap,
                               std::uint64_t generation,
                               gd_rect_i* rectangles,
                               std::uint64_t capacity,
                               std::uint64_t* required_count,
                               gd_bitmap_damage_summary* summary) {
    const gui_drawing::BitmapDamageSnapshot damage =
        bitmap.changes_since(generation);
    *required_count = damage.rectangles.size();
    *summary = {damage.from_generation, damage.to_generation,
                damage.history_complete ? 1U : 0U};
    if (capacity < damage.rectangles.size() ||
        (!damage.rectangles.empty() && rectangles == nullptr)) {
        return fail(GD_ERROR_BUFFER_TOO_SMALL,
                    "bitmap damage buffer is smaller than the required count");
    }
    for (std::size_t index = 0; index < damage.rectangles.size(); ++index) {
        const gui_drawing::RectI& rect = damage.rectangles[index];
        rectangles[index] = {rect.x, rect.y, rect.width, rect.height};
    }
    return GD_OK;
}

gd_result clone_bitmap(gui_drawing::Bitmap& bitmap, gd_rect_i source,
                       gd_handle* output) {
    std::unique_ptr<gui_drawing::Bitmap> clone = bitmap.clone(
        {source.x, source.y, source.width, source.height});
    return registry().adopt(GD_OBJECT_BITMAP, output, std::move(clone));
}

gd_result make_bitmap_transparent_native(gui_drawing::Bitmap& bitmap,
                                         gui_drawing::Color key) {
    bitmap.make_transparent(key);
    return GD_OK;
}

gd_result make_bitmap_transparent(gd_handle handle, gd_color key) {
    const gui_drawing::Color native = color_from_c(key);
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &make_bitmap_transparent_native, native);
}

gd_result create_bitmap_thumbnail(gui_drawing::Bitmap& bitmap,
                                  std::uint32_t width,
                                  std::uint32_t height,
                                  gd_handle* output) {
    std::unique_ptr<gui_drawing::Bitmap> thumbnail =
        bitmap.thumbnail(width, height);
    return registry().adopt(GD_OBJECT_BITMAP, output, std::move(thumbnail));
}

gd_result create_adjusted_bitmap(gui_drawing::Bitmap& bitmap,
                                 gui_drawing::ImageAttributes& attributes,
                                 gd_handle* output) {
    std::unique_ptr<gui_drawing::Bitmap> adjusted =
        bitmap.adjusted(attributes);
    return registry().adopt(GD_OBJECT_BITMAP, output, std::move(adjusted));
}

gd_result draw_resolved_bitmap(
    gui_drawing::ImageAttributes& attributes,
    gui_drawing::GraphicsRecorder* graphics, gui_drawing::Bitmap* bitmap,
    gd_rect destination, gd_rect source) {
    (*graphics).draw_image(*bitmap, rect_from_c(destination),
                           rect_from_c(source), attributes);
    return GD_OK;
}

gd_result draw_recorder_bitmap(gui_drawing::GraphicsRecorder& graphics,
                               gui_drawing::Bitmap& bitmap,
                               gd_rect destination, gd_rect source,
                               gd_handle attributes) {
    return registry().with<gui_drawing::ImageAttributes>(
        attributes, GD_OBJECT_IMAGE_ATTRIBUTES, &draw_resolved_bitmap,
        &graphics, &bitmap, destination, source);
}

gd_result add_path_arc(gui_drawing::GraphicsPath& path, gd_rect bounds,
                       double start_angle, double sweep_angle) {
    path.add_arc(rect_from_c(bounds), start_angle, sweep_angle);
    return GD_OK;
}

gd_result append_path(gui_drawing::GraphicsPath& path,
                      gui_drawing::GraphicsPath& source,
                      std::uint32_t connect) {
    path.add_path(source, connect != 0U);
    return GD_OK;
}

gd_result transform_path(gui_drawing::GraphicsPath& path,
                         gd_matrix transform) {
    path.transform(matrix_from_c(transform));
    return GD_OK;
}

gd_result query_path_visibility(gui_drawing::GraphicsPath& path,
                                gd_point point, std::uint32_t* visible) {
    *visible = path.is_visible(point_from_c(point)) ? 1U : 0U;
    return GD_OK;
}

gd_result read_path_points(gui_drawing::GraphicsPath& path, gd_point* points,
                           std::uint64_t capacity,
                           std::uint64_t* required_count) {
    const std::vector<gui_drawing::PointF> native = path.path_points();
    *required_count = native.size();
    if (capacity < native.size() || (!native.empty() && points == nullptr)) {
        return fail(GD_ERROR_BUFFER_TOO_SMALL,
                    "path point buffer is smaller than the required count");
    }
    for (std::size_t index = 0; index < native.size(); ++index) {
        points[index] = {native[index].x, native[index].y};
    }
    return GD_OK;
}

// graphics_path_flatten: counted first, so short arrays are reported
// before anything is written; the C++ flattening writes into scratch that
// is then copied into the caller's three arrays.
gd_result flatten_path(gui_drawing::GraphicsPath& path, double tolerance, gd_point* points,
                       std::uint64_t point_capacity, std::uint64_t* figure_ends,
                       std::uint32_t* figure_closed, std::uint64_t figure_capacity,
                       std::uint64_t* point_count, std::uint64_t* figure_count) {
    *point_count = 0U;
    *figure_count = 0U;
    const gui_drawing::PathFlattening needed = path.flattened_size(tolerance);
    if (needed.status == gui_drawing::PathFlatteningStatus::limit_exceeded) {
        return fail(GD_ERROR_LIMIT_EXCEEDED, "path flattening exceeds its point limit");
    }
    *point_count = needed.point_count;
    *figure_count = needed.figure_count;
    if (point_capacity < needed.point_count || figure_capacity < needed.figure_count ||
        (needed.point_count != 0U && points == nullptr) ||
        (needed.figure_count != 0U && (figure_ends == nullptr || figure_closed == nullptr))) {
        return fail(GD_ERROR_BUFFER_TOO_SMALL, "path flattening arrays are smaller than the counts");
    }
    std::vector<gui_drawing::PointF> native_points(needed.point_count);
    std::vector<gui_drawing::FlattenedFigure> native_figures(needed.figure_count);
    const gui_drawing::PathFlattening written = path.flatten(tolerance, native_points, native_figures);
    if (written.status != gui_drawing::PathFlatteningStatus::complete) {
        return fail(GD_ERROR_INTERNAL, "path flattening changed between counting and writing");
    }
    for (std::size_t index = 0U; index < native_points.size(); ++index) {
        points[index] = {native_points[index].x, native_points[index].y};
    }
    for (std::size_t index = 0U; index < native_figures.size(); ++index) {
        figure_ends[index] = native_figures[index].end;
        figure_closed[index] = native_figures[index].closed ? 1U : 0U;
    }
    return GD_OK;
}

gd_result clone_path(gui_drawing::GraphicsPath& path, gd_handle* output) {
    return registry().adopt(GD_OBJECT_GRAPHICS_PATH, output, path.clone());
}

gd_result set_image_remap_table(gd_handle handle,
                                const gd_color_remap* entries,
                                std::uint64_t count) {
    using ColorRemap =
        gui_drawing::ImageAttributesSnapshot::ColorRemap;
    std::vector<ColorRemap> native;
    native.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        native.push_back({color_from_c(entries[index].old_color),
                          color_from_c(entries[index].new_color)});
    }
    return registry().with<gui_drawing::ImageAttributes>(
        handle, GD_OBJECT_IMAGE_ATTRIBUTES,
        &gui_drawing::ImageAttributes::set_remap_table, native);
}

// The batched marks and their companions (the drawing-primitives append).

[[nodiscard]] gui_drawing::CornerRadii radii_from_c(gd_corner_radii radii) {
    return {radii.top_left, radii.top_right, radii.bottom_right, radii.bottom_left};
}

// Copies a caller's point array once; the count is already bounded.
[[nodiscard]] std::vector<gui_drawing::PointF> points_from_c(const gd_point* points, std::uint64_t count) {
    std::vector<gui_drawing::PointF> native;
    native.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) native.push_back(point_from_c(points[index]));
    return native;
}

gd_result set_pen_line_join(gui_drawing::Pen& pen, std::uint32_t join, double miter_limit) {
    pen.set_line_join(static_cast<gui_drawing::LineJoin>(join), miter_limit);
    return GD_OK;
}

gd_result set_pen_line_cap(gui_drawing::Pen& pen, std::uint32_t cap) {
    pen.set_line_cap(static_cast<gui_drawing::LineCap>(cap));
    return GD_OK;
}

gd_result set_pen_trim(gui_drawing::Pen& pen, double start, double end, double offset) {
    pen.set_trim(start, end, offset);
    return GD_OK;
}

gd_result draw_recorder_lines(gui_drawing::GraphicsRecorder& graphics, gui_drawing::Pen& stroke,
                              const std::vector<gui_drawing::PointF>& points) {
    graphics.draw_lines(stroke, points);
    return GD_OK;
}

gd_result draw_recorder_line_segments(gui_drawing::GraphicsRecorder& graphics, gui_drawing::Pen& stroke,
                                      const std::vector<gui_drawing::PointF>& points) {
    graphics.draw_line_segments(stroke, points);
    return GD_OK;
}

gd_result fill_recorder_rectangles(gui_drawing::GraphicsRecorder& graphics, gui_drawing::Brush& fill,
                                   const std::vector<gui_drawing::RectF>& rects) {
    graphics.fill_rectangles(fill, rects);
    return GD_OK;
}

gd_result draw_recorder_markers(gui_drawing::GraphicsRecorder& graphics,
                                const std::vector<gui_drawing::PointF>& centers,
                                const gui_drawing::MarkerStyle& style) {
    graphics.draw_markers(centers, style);
    return GD_OK;
}

gd_result fill_recorder_grid(gui_drawing::GraphicsRecorder& graphics, gd_rect rect,
                             const gui_drawing::GridPatternStyle& grid) {
    graphics.fill_grid_pattern(rect_from_c(rect), grid);
    return GD_OK;
}

gd_result fill_recorder_proportion(gui_drawing::GraphicsRecorder& graphics, gui_drawing::Brush& fill,
                                   gd_rect track, gd_corner_radii radii, double proportion,
                                   std::uint32_t direction) {
    graphics.fill_proportion(fill, rect_from_c(track), radii_from_c(radii), proportion,
                             static_cast<gui_drawing::FillDirection>(direction));
    return GD_OK;
}

// The on-fill colour arrives as a value; its brush lives for this call on
// the calling thread, which owns every object the call touches.
gd_result draw_recorder_string_over_fill(gui_drawing::GraphicsRecorder& graphics, gui_drawing::Font& font,
                                         gui_drawing::SolidBrush& brush, gui_drawing::StringFormat& format,
                                         const std::string& text, gd_color on_fill_color, gd_point origin,
                                         gd_rect fill) {
    const gui_drawing::SolidBrush on_fill(color_from_c(on_fill_color));
    graphics.draw_string_over_fill(text, font, brush, on_fill, point_from_c(origin), format, rect_from_c(fill));
    return GD_OK;
}

gd_result add_path_rounded_rectangle(gui_drawing::GraphicsPath& path, gd_rect rectangle, gd_corner_radii radii) {
    path.add_rounded_rectangle(rect_from_c(rectangle), radii_from_c(radii));
    return GD_OK;
}

gd_result create_conic_gradient_brush(gd_point center, double start_angle, gd_color first, gd_color second,
                                      gd_handle* output) {
    return registry().create<gui_drawing::ConicGradientBrush>(GD_OBJECT_CONIC_GRADIENT_BRUSH, output,
                                                              point_from_c(center), start_angle,
                                                              color_from_c(first), color_from_c(second));
}

gd_result create_checker_brush(double cell_size, gd_color first, gd_color second, gd_handle* output) {
    return registry().create<gui_drawing::CheckerBrush>(GD_OBJECT_CHECKER_BRUSH, output, cell_size,
                                                        color_from_c(first), color_from_c(second));
}

gd_result create_texture_brush(gui_drawing::Bitmap& bitmap, gui_drawing::WrapMode wrap_mode, gd_handle* output) {
    std::unique_ptr<gui_drawing::TextureBrush> brush = std::make_unique<gui_drawing::TextureBrush>(bitmap, wrap_mode);
    return registry().adopt(GD_OBJECT_TEXTURE_BRUSH, output, std::move(brush));
}

gd_result set_texture_brush_transform(gui_drawing::TextureBrush& brush, gd_matrix transform) {
    brush.set_transform(matrix_from_c(transform));
    return GD_OK;
}

gd_result create_radial_gradient_brush(gd_point center, double radius, gd_color center_color,
                                       gd_color edge_color, gui_drawing::WrapMode wrap_mode, gd_handle* output) {
    return registry().create<gui_drawing::RadialGradientBrush>(GD_OBJECT_RADIAL_GRADIENT_BRUSH, output,
                                                               point_from_c(center), radius,
                                                               color_from_c(center_color),
                                                               color_from_c(edge_color), wrap_mode);
}

gd_result set_radial_gradient_focus(gui_drawing::RadialGradientBrush& brush, gd_point focus) {
    brush.set_focus(point_from_c(focus));
    return GD_OK;
}

// The two radii of an elliptical radial gradient.
struct RadialRadii final {
    double across{};
    double down{};
};

gd_result set_radial_gradient_radii(gui_drawing::RadialGradientBrush& brush, RadialRadii radii) {
    brush.set_radii(radii.across, radii.down);
    return GD_OK;
}

gd_result read_contrast_ratio(gd_color first, gd_color second, double* ratio) {
    *ratio = gui_drawing::contrast_ratio(color_from_c(first), color_from_c(second));
    return GD_OK;
}

gd_result choose_contrasting_text_color(gd_color background, gd_color light, gd_color dark, gd_color* chosen) {
    const gui_drawing::Color native =
        gui_drawing::contrasting_text_color(color_from_c(background), color_from_c(light), color_from_c(dark));
    *chosen = color_to_c(native);
    return GD_OK;
}

gd_result create_hatch_brush(std::uint32_t style, gd_color foreground,
                             gd_color background, gd_handle* output) {
    return registry().create<gui_drawing::HatchBrush>(
        GD_OBJECT_HATCH_BRUSH, output,
        static_cast<gui_drawing::HatchStyle>(style),
        color_from_c(foreground), color_from_c(background));
}

gd_result create_linear_gradient_brush(gd_rect bounds, gd_color first,
                                       gd_color second, double angle,
                                       std::uint32_t wrap_mode,
                                       gd_handle* output) {
    return registry().create<gui_drawing::LinearGradientBrush>(
        GD_OBJECT_LINEAR_GRADIENT_BRUSH, output, rect_from_c(bounds),
        color_from_c(first), color_from_c(second), angle,
        static_cast<gui_drawing::WrapMode>(wrap_mode));
}

gd_result set_linear_gradient_blend(
    gui_drawing::LinearGradientBrush& brush, const double* factors,
    const double* positions, std::uint64_t count) {
    brush.set_blend({factors, static_cast<std::size_t>(count)},
                    {positions, static_cast<std::size_t>(count)});
    return GD_OK;
}

gd_result apply_linear_gradient_interpolation(
    gui_drawing::LinearGradientBrush& brush,
    const gui_drawing::ColorBlend& blend) {
    brush.set_interpolation_colors(blend);
    return GD_OK;
}

gd_result apply_conic_gradient_interpolation(
    gui_drawing::ConicGradientBrush& brush,
    const gui_drawing::ColorBlend& blend) {
    brush.set_interpolation_colors(blend);
    return GD_OK;
}

gd_result apply_radial_gradient_interpolation(
    gui_drawing::RadialGradientBrush& brush,
    const gui_drawing::ColorBlend& blend) {
    brush.set_interpolation_colors(blend);
    return GD_OK;
}

gd_result apply_path_gradient_interpolation(
    gui_drawing::PathGradientBrush& brush,
    const gui_drawing::ColorBlend& blend) {
    brush.set_interpolation_colors(blend);
    return GD_OK;
}

gd_result apply_gradient_interpolation(gd_handle handle,
                                       std::uint32_t kind,
                                       const gd_color* colors,
                                       const double* positions,
                                       std::uint64_t count) {
    gui_drawing::ColorBlend blend;
    blend.colors.reserve(static_cast<std::size_t>(count));
    blend.positions.assign(positions, positions + count);
    for (std::uint64_t index = 0; index < count; ++index) {
        blend.colors.push_back(color_from_c(colors[index]));
    }
    if (kind == GD_OBJECT_LINEAR_GRADIENT_BRUSH) {
        return registry().with<gui_drawing::LinearGradientBrush>(
            handle, kind, &apply_linear_gradient_interpolation, blend);
    }
    if (kind == GD_OBJECT_CONIC_GRADIENT_BRUSH) {
        return registry().with<gui_drawing::ConicGradientBrush>(
            handle, kind, &apply_conic_gradient_interpolation, blend);
    }
    if (kind == GD_OBJECT_RADIAL_GRADIENT_BRUSH) {
        return registry().with<gui_drawing::RadialGradientBrush>(
            handle, kind, &apply_radial_gradient_interpolation, blend);
    }
    return registry().with<gui_drawing::PathGradientBrush>(
        handle, kind, &apply_path_gradient_interpolation, blend);
}

gd_result create_path_gradient_brush(const gd_point* points,
                                     std::uint64_t count,
                                     std::uint32_t wrap_mode,
                                     gd_handle* output) {
    std::vector<gui_drawing::PointF> native;
    native.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        native.push_back(point_from_c(points[index]));
    }
    return registry().create<gui_drawing::PathGradientBrush>(
        GD_OBJECT_PATH_GRADIENT_BRUSH, output,
        std::span<const gui_drawing::PointF>(native),
        static_cast<gui_drawing::WrapMode>(wrap_mode));
}

gd_result set_path_gradient_center_color_native(
    gui_drawing::PathGradientBrush& brush, gui_drawing::Color color) {
    brush.set_center_color(color);
    return GD_OK;
}

gd_result set_path_gradient_center_color(gd_handle handle, gd_color color) {
    const gui_drawing::Color native = color_from_c(color);
    return registry().with<gui_drawing::PathGradientBrush>(
        handle, GD_OBJECT_PATH_GRADIENT_BRUSH,
        &set_path_gradient_center_color_native, native);
}

gd_result set_path_gradient_center_point(
    gui_drawing::PathGradientBrush& brush, gd_point point) {
    brush.set_center_point(point_from_c(point));
    return GD_OK;
}

gd_result set_path_gradient_surround_colors(gd_handle handle,
                                            const gd_color* colors,
                                            std::uint64_t count) {
    std::vector<gui_drawing::Color> native;
    native.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        native.push_back(color_from_c(colors[index]));
    }
    return registry().with<gui_drawing::PathGradientBrush>(
        handle, GD_OBJECT_PATH_GRADIENT_BRUSH,
        &gui_drawing::PathGradientBrush::set_surround_colors,
        std::span<const gui_drawing::Color>(native));
}

gd_result create_region_from_path(gui_drawing::GraphicsPath& path,
                                  gd_handle* output) {
    return registry().create<gui_drawing::Region>(GD_OBJECT_REGION, output,
                                                  path);
}

gd_result unite_region_rectangle(gui_drawing::Region& region,
                                 gd_rect rectangle) {
    region.unite(rect_from_c(rectangle));
    return GD_OK;
}

gd_result unite_region_path(gui_drawing::Region& region,
                            gui_drawing::GraphicsPath& path) {
    region.unite(path);
    return GD_OK;
}

gd_result exclude_region_rectangle(gui_drawing::Region& region,
                                   gd_rect rectangle) {
    region.exclude(rect_from_c(rectangle));
    return GD_OK;
}

gd_result query_region_visibility(gui_drawing::Region& region,
                                  gd_point point,
                                  std::uint32_t* visible) {
    *visible = region.is_visible(point_from_c(point)) ? 1U : 0U;
    return GD_OK;
}

gd_result query_region_bounds(gui_drawing::Region& region, gd_rect* bounds) {
    const gui_drawing::RectF value = region.bounds();
    *bounds = {value.x, value.y, value.width, value.height};
    return GD_OK;
}

gd_result execute_recorded_commands(
    gui_drawing::GraphicsRecorder& recorder, gui_drawing::Bitmap& bitmap,
    const gd_raster_service_v0& service, std::uint64_t first_command,
    std::uint64_t* commands_executed) {
    const gd_result result = service.execute(
        &recorder, &bitmap, first_command, commands_executed);
    return result == GD_OK ? GD_OK :
        fail(result, "GUI.Drawing retained raster execution failed");
}

gd_result encode_bitmap_png(gui_drawing::Bitmap& bitmap,
                            const gd_raster_service_v0& service,
                            void* buffer, std::uint64_t capacity,
                            std::uint64_t* required_size) {
    const gd_result result = service.encode_png(
        &bitmap, buffer, capacity, required_size);
    return result == GD_OK || result == GD_ERROR_BUFFER_TOO_SMALL ? result :
        fail(result, "GUI.Drawing PNG encode failed");
}

gd_result clone_image_attributes(gui_drawing::ImageAttributes& attributes,
                                 gd_handle* output) {
    return registry().adopt(GD_OBJECT_IMAGE_ATTRIBUTES, output,
                            attributes.clone());
}

gd_result measure_string_without_format(
    gui_drawing::Font& font, const gd_raster_service_v0& service,
    gd_string_view text, double layout_width, gd_size* measured) {
    const gd_result result = service.measure_string(
        &font, nullptr, text, layout_width, measured);
    return result == GD_OK ? GD_OK :
        fail(result, "GUI.Drawing raster text measurement failed");
}

gd_result measure_string_with_format(
    gui_drawing::Font& font, gui_drawing::StringFormat& format,
    const gd_raster_service_v0& service, gd_string_view text,
    double layout_width, gd_size* measured) {
    const gd_result result = service.measure_string(
        &font, &format, text, layout_width, measured);
    return result == GD_OK ? GD_OK :
        fail(result, "GUI.Drawing raster text measurement failed");
}

gd_result resolve_font_with_service(gui_drawing::Font& font,
                                    const gd_raster_service_v0& service,
                                    gd_font_resolution* resolution) {
    const gd_result result = service.resolve_font(&font, resolution);
    return result == GD_OK ? GD_OK :
        fail(result, "GUI.Drawing raster font resolution failed");
}

// Without a raster module nothing can ask the host or load a face, so an
// unmapped family stays unmapped.
gd_result resolve_font_from_table(gui_drawing::Font& font,
                                  gd_font_resolution* resolution) {
    const gui_drawing::FontSnapshot snapshot = font.snapshot();
    const gui_drawing::FontFamilyResolution answer =
        gui_drawing::resolve_font_family(
            snapshot.family, gui_drawing::font_face_style(snapshot.style));
    gui_drawing::abi::fill_font_resolution(answer, false, *resolution);
    return GD_OK;
}

gd_result export_bitmap_hbitmap(gui_drawing::Bitmap& bitmap,
                                gd_color background,
                                std::uintptr_t* output) {
    const gd_result result = gui_drawing::abi::platform::export_hbitmap(
        bitmap, color_from_c(background), *output);
    return result == GD_OK ? GD_OK :
        fail(result, "native bitmap export is unavailable");
}

gd_result import_bitmap_hbitmap(std::uintptr_t source, gd_handle* output) {
    std::unique_ptr<gui_drawing::Bitmap> bitmap;
    const gd_result result = gui_drawing::abi::platform::import_hbitmap(
        source, bitmap);
    if (result != GD_OK) {
        return fail(result, "native bitmap import is unavailable");
    }
    return registry().adopt(GD_OBJECT_BITMAP, output, std::move(bitmap));
}

gd_result capture_native_surface(std::uintptr_t source, std::uint32_t kind,
                                 gd_handle* output,
                                 gd_rect* captured_bounds) {
    gui_drawing::abi::platform::CapturedSurface captured;
    const gd_result result = gui_drawing::abi::platform::capture_surface(
        source, kind, captured);
    if (result != GD_OK) {
        return fail(result, "native surface capture is unavailable");
    }
    const gd_result adopted = registry().adopt(
        GD_OBJECT_BITMAP, output, std::move(captured.bitmap));
    if (adopted == GD_OK) {
        *captured_bounds = {captured.bounds.x, captured.bounds.y,
                            captured.bounds.width, captured.bounds.height};
    }
    return adopted;
}

gd_result present_native_surface(gui_drawing::Bitmap& bitmap,
                                 std::uintptr_t destination,
                                 std::uint32_t kind) {
    const gd_result result = gui_drawing::abi::platform::present_surface(
        destination, kind, bitmap);
    return result == GD_OK ? GD_OK :
        fail(result, "native surface presentation is unavailable");
}

gd_result refresh_native_surface(gui_drawing::Bitmap& bitmap,
                                 gui_drawing::abi::platform::SurfaceStagingOwner& staging,
                                 std::uintptr_t source, std::uint32_t kind,
                                 gd_rect* refreshed_bounds) {
    gui_drawing::RectF bounds;
    const gd_result result = gui_drawing::abi::platform::refresh_surface(
        source, kind, bitmap, staging, bounds);
    if (result != GD_OK) {
        return fail(result, "native surface refresh is unavailable");
    }
    *refreshed_bounds = {bounds.x, bounds.y, bounds.width, bounds.height};
    return GD_OK;
}

gd_result publish_native_surface(gui_drawing::Bitmap& bitmap,
                                 std::uintptr_t destination,
                                 std::uint32_t kind) {
    return gui_drawing::abi::platform::publish_retained_surface(
        destination, kind, bitmap);
}

gd_result acquire_bitmap_hdc(gui_drawing::Bitmap& bitmap,
                             std::uintptr_t* output,
                             std::uint64_t* lease_token) {
    const gd_result result = gui_drawing::abi::platform::acquire_hdc(
        bitmap, *output, *lease_token);
    return result == GD_OK ? GD_OK :
        fail(result, "bitmap-backed device-context lease is unavailable");
}

gd_result release_bitmap_hdc_raw(gui_drawing::Bitmap& bitmap,
                                 std::uint64_t lease_token) {
    return gui_drawing::abi::platform::release_hdc(bitmap, lease_token);
}

gd_result release_bitmap_hdc(gui_drawing::Bitmap& bitmap,
                             std::uint64_t lease_token) {
    const gd_result result = release_bitmap_hdc_raw(bitmap, lease_token);
    return result == GD_OK ? GD_OK :
        fail(result, "bitmap-backed device-context release failed");
}

gd_result api_last_error(gd_error_view* error) {
    if (error == nullptr) return fail(GD_ERROR_INVALID_ARGUMENT, "last_error requires output");
    (*error).code = static_cast<std::uint32_t>(last_error.code);
    (*error).message = {last_error.message.data(), last_error.message_size};
    return GD_OK;
}

gd_result api_retain(gd_handle handle) { return registry().retain(handle); }
gd_result api_release(gd_handle handle) { return registry().release(handle); }
gd_result api_dispose(gd_handle handle) {
    std::uint32_t state{};
    if (const gd_result result = registry().object_state(handle, &state);
        result != GD_OK) return result;
    if (state == GD_OBJECT_DISPOSED) return registry().dispose(handle);
    std::uint32_t kind{};
    if (const gd_result result = registry().object_kind(handle, &kind);
        result != GD_OK) return result;
    if (kind == GD_OBJECT_BITMAP) {
        const gd_result lend_check = registry().with<gui_drawing::Bitmap>(
            handle, GD_OBJECT_BITMAP, &verify_bitmap_is_not_lent);
        if (lend_check != GD_OK) return lend_check;
    }
    return registry().dispose(handle);
}
gd_result api_object_state(gd_handle handle, std::uint32_t* state) {
    return registry().object_state(handle, state);
}
gd_result api_object_kind(gd_handle handle, std::uint32_t* kind) {
    return registry().object_kind(handle, kind);
}

gd_result api_solid_brush_create(gd_color color, gd_handle* output) {
    return translate(&create_solid_brush, color, output);
}

gd_result api_pen_create(gd_color color, double width, gd_handle* output) {
    return translate(&create_pen, color, width, output);
}

gd_result api_pen_set_width(gd_handle handle, double width) {
    return registry().with<gui_drawing::Pen>(
        handle, GD_OBJECT_PEN, &gui_drawing::Pen::set_width, width);
}

gd_result api_pen_set_dash_style(gd_handle handle, std::uint32_t style) {
    if (style > GD_DASH_CUSTOM) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "dash style is outside the declared enum");
    }
    const gui_drawing::DashStyle native_style =
        static_cast<gui_drawing::DashStyle>(style);
    return registry().with<gui_drawing::Pen>(
        handle, GD_OBJECT_PEN, &gui_drawing::Pen::set_dash_style,
        native_style);
}

gd_result api_pen_set_dash_pattern(gd_handle handle, const double* entries,
                                   std::uint64_t count) {
    if (const gd_result counted = check_count(entries, count, 1U, 256U,
                                              "dash pattern requires 1 through 256 entries");
        counted != GD_OK) {
        return counted;
    }
    const std::span<const double> pattern(
        entries, static_cast<std::size_t>(count));
    return registry().with<gui_drawing::Pen>(
        handle, GD_OBJECT_PEN, &gui_drawing::Pen::set_dash_pattern, pattern);
}

gd_result api_font_create(gd_string_view family, double size,
                          std::uint32_t style, std::uint32_t unit,
                          std::uint32_t charset, gd_handle* output) {
    if ((style & ~UINT32_C(0x0f)) != 0U || unit > 6U || charset > 255U) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "font style, unit, or charset is invalid");
    }
    return translate(&create_font, family, size, style, unit, charset, output);
}

gd_result api_string_format_create(std::uint32_t flags, gd_handle* output) {
    return registry().create<gui_drawing::StringFormat>(
        GD_OBJECT_STRING_FORMAT, output, flags);
}

gd_result api_string_format_set(gd_handle handle, std::uint32_t alignment,
                                std::uint32_t line_alignment,
                                std::uint32_t trimming, std::uint32_t flags) {
    if (alignment > GD_STRING_FAR || line_alignment > GD_STRING_FAR ||
        trimming > GD_TRIM_ELLIPSIS_PATH) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "string format enum is invalid");
    }
    return registry().with<gui_drawing::StringFormat>(
        handle, GD_OBJECT_STRING_FORMAT, &configure_string_format, alignment,
        line_alignment, trimming, flags);
}

gd_result api_recorder_create(gd_handle* output) {
    return registry().create<gui_drawing::GraphicsRecorder>(GD_OBJECT_RECORDER, output);
}

gd_result api_recorder_save(gd_handle handle, std::uint64_t* token) {
    if (token == nullptr) return fail(GD_ERROR_INVALID_ARGUMENT, "save requires token output");
    return registry().with<gui_drawing::GraphicsRecorder>(
        handle, GD_OBJECT_RECORDER, &save_recorder, token);
}

gd_result api_recorder_restore(gd_handle handle, std::uint64_t token) {
    return registry().with<gui_drawing::GraphicsRecorder>(
        handle, GD_OBJECT_RECORDER, &restore_recorder, token);
}

gd_result api_recorder_translate(gd_handle handle, double x, double y) {
    return registry().with<gui_drawing::GraphicsRecorder>(
        handle, GD_OBJECT_RECORDER, &gui_drawing::GraphicsRecorder::translate,
        x, y);
}

gd_result api_recorder_set_transform(gd_handle handle, gd_matrix transform) {
    return registry().with<gui_drawing::GraphicsRecorder>(
        handle, GD_OBJECT_RECORDER, &set_recorder_transform, transform);
}

gd_result api_recorder_set_clip(gd_handle handle, gd_rect clip) {
    return registry().with<gui_drawing::GraphicsRecorder>(
        handle, GD_OBJECT_RECORDER, &set_recorder_clip, clip);
}

gd_result api_recorder_reset_clip(gd_handle handle) {
    return registry().with<gui_drawing::GraphicsRecorder>(
        handle, GD_OBJECT_RECORDER,
        &gui_drawing::GraphicsRecorder::reset_clip);
}

gd_result api_recorder_set_quality(gd_handle handle,
                                   std::uint32_t smoothing,
                                   std::uint32_t interpolation,
                                   std::uint32_t pixel_offset,
                                   std::uint32_t compositing,
                                   std::uint32_t compositing_quality) {
    if (smoothing > 4U || interpolation > 7U || pixel_offset > 4U ||
        compositing > 1U || compositing_quality > 4U) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "graphics quality enum is invalid");
    }
    return registry().with<gui_drawing::GraphicsRecorder>(
        handle, GD_OBJECT_RECORDER, &set_recorder_quality, smoothing,
        interpolation, pixel_offset, compositing, compositing_quality);
}

gd_result api_recorder_is_visible(gd_handle handle, gd_point point,
                                  std::uint32_t* visible) {
    if (visible == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "visibility query requires output");
    }
    return registry().with<gui_drawing::GraphicsRecorder>(
        handle, GD_OBJECT_RECORDER, &query_recorder_visibility, point,
        visible);
}

gd_result api_recorder_clear(gd_handle handle, gd_color color) {
    return translate(&clear_recorder, handle, color);
}

gd_result api_recorder_fill_rectangle(gd_handle recorder, gd_handle brush,
                                      gd_rect rect) {
    return registry().with_two<gui_drawing::GraphicsRecorder, gui_drawing::Brush>(
        recorder, GD_OBJECT_RECORDER, brush, 0U,
        &fill_recorder_rectangle, rect);
}

gd_result api_recorder_draw_rectangle(gd_handle recorder, gd_handle pen,
                                      gd_rect rect) {
    return registry().with_two<gui_drawing::GraphicsRecorder, gui_drawing::Pen>(
        recorder, GD_OBJECT_RECORDER, pen, GD_OBJECT_PEN,
        &draw_recorder_rectangle, rect);
}

gd_result api_recorder_draw_line(gd_handle recorder, gd_handle pen,
                                 gd_point from, gd_point to) {
    return registry().with_two<gui_drawing::GraphicsRecorder, gui_drawing::Pen>(
        recorder, GD_OBJECT_RECORDER, pen, GD_OBJECT_PEN,
        &draw_recorder_line, from, to);
}

gd_result api_recorder_draw_string(gd_handle recorder, gd_string_view text,
                                   gd_handle font, gd_handle brush,
                                   gd_point origin, gd_handle format) {
    return translate(&draw_recorder_string_from_c, recorder, text, font,
                     brush, origin, format);
}

gd_result api_recorder_draw_string_rotated(gd_handle recorder, gd_string_view text,
                                           gd_handle font, gd_handle brush,
                                           gd_point origin, double degrees,
                                           gd_handle format) {
    if (!std::isfinite(degrees)) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "text rotation must be finite");
    }
    return translate(&draw_recorder_string_rotated_from_c, recorder, text, font,
                     brush, origin, degrees, format);
}

gd_result api_recorder_close(gd_handle handle) {
    return registry().with<gui_drawing::GraphicsRecorder>(
        handle, GD_OBJECT_RECORDER, &gui_drawing::GraphicsRecorder::close);
}

gd_result api_recorder_command_count(gd_handle handle, std::uint64_t* count) {
    if (count == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "command count requires output");
    }
    return registry().with<gui_drawing::GraphicsRecorder>(
        handle, GD_OBJECT_RECORDER, &query_recorder_command_count, count);
}

gd_result api_recorder_trace(gd_handle handle, char* buffer,
                             std::uint64_t capacity,
                             std::uint64_t* required_size) {
    if (required_size == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "trace read requires a size output");
    }
    return registry().with<gui_drawing::GraphicsRecorder>(
        handle, GD_OBJECT_RECORDER, &read_recorder_trace, buffer, capacity,
        required_size);
}

gd_result api_graphics_path_create(std::uint32_t fill_mode, gd_handle* output) {
    if (fill_mode > 1U) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "path fill mode is outside the declared enum");
    }
    return registry().create<gui_drawing::GraphicsPath>(
        GD_OBJECT_GRAPHICS_PATH, output,
        static_cast<gui_drawing::FillMode>(fill_mode));
}

gd_result api_graphics_path_reset(gd_handle handle) {
    return registry().with<gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_GRAPHICS_PATH, &gui_drawing::GraphicsPath::reset);
}

gd_result api_graphics_path_start_figure(gd_handle handle) {
    return registry().with<gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_GRAPHICS_PATH,
        &gui_drawing::GraphicsPath::start_figure);
}

gd_result api_graphics_path_close_figure(gd_handle handle) {
    return registry().with<gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_GRAPHICS_PATH,
        &gui_drawing::GraphicsPath::close_figure);
}

gd_result api_graphics_path_add_line(gd_handle handle, gd_point from, gd_point to) {
    return registry().with<gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_GRAPHICS_PATH, &add_path_line, from, to);
}

gd_result api_graphics_path_add_rectangle(gd_handle handle, gd_rect rectangle) {
    return registry().with<gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_GRAPHICS_PATH, &add_path_rectangle, rectangle);
}

gd_result api_graphics_path_add_ellipse(gd_handle handle, gd_rect bounds) {
    return registry().with<gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_GRAPHICS_PATH, &add_path_ellipse, bounds);
}

gd_result api_graphics_path_bounds(gd_handle handle, gd_rect* bounds) {
    if (bounds == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "path bounds requires output");
    }
    return registry().with<gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_GRAPHICS_PATH, &query_path_bounds, bounds);
}

gd_result api_image_reference_create(std::uint64_t stable_id,
                                     std::uint32_t width,
                                     std::uint32_t height,
                                     std::uint32_t pixel_format,
                                     std::uint64_t generation,
                                     gd_handle* output) {
    if (pixel_format != GD_PIXEL_BGRA32_PREMULTIPLIED) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "image pixel format is outside the declared enum");
    }
    return registry().create<gui_drawing::ImageReference>(
        GD_OBJECT_IMAGE_REFERENCE, output, stable_id, width, height,
        static_cast<gui_drawing::PixelFormat>(pixel_format), generation);
}

gd_result api_image_attributes_create(gd_handle* output) {
    return registry().create<gui_drawing::ImageAttributes>(
        GD_OBJECT_IMAGE_ATTRIBUTES, output);
}

gd_result api_image_attributes_set_color_matrix(gd_handle handle,
                                                const double* entries,
                                                std::uint64_t count) {
    if (entries == nullptr || count != 25U) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "image color matrix requires exactly 25 entries");
    }
    return registry().with<gui_drawing::ImageAttributes>(
        handle, GD_OBJECT_IMAGE_ATTRIBUTES,
        &gui_drawing::ImageAttributes::set_color_matrix,
        std::span<const double, 25>(entries, 25U));
}

gd_result api_image_attributes_reset(gd_handle handle) {
    return registry().with<gui_drawing::ImageAttributes>(
        handle, GD_OBJECT_IMAGE_ATTRIBUTES,
        &gui_drawing::ImageAttributes::reset_color_matrix);
}

gd_result api_recorder_draw_ellipse(gd_handle recorder, gd_handle pen,
                                    gd_rect bounds) {
    return registry().with_two<gui_drawing::GraphicsRecorder, gui_drawing::Pen>(
        recorder, GD_OBJECT_RECORDER, pen, GD_OBJECT_PEN,
        &draw_recorder_ellipse, bounds);
}

gd_result api_recorder_fill_ellipse(gd_handle recorder, gd_handle brush,
                                    gd_rect bounds) {
    return registry().with_two<gui_drawing::GraphicsRecorder, gui_drawing::Brush>(
        recorder, GD_OBJECT_RECORDER, brush, 0U,
        &fill_recorder_ellipse, bounds);
}

gd_result api_recorder_fill_polygon(gd_handle recorder, gd_handle brush,
                                    const gd_point* points, std::uint64_t count,
                                    std::uint32_t fill_mode) {
    if (const gd_result counted = check_count(points, count, 3U, gui_drawing::GraphicsPath::maximum_elements,
                                              "polygon requires 3 or more bounded points");
        counted != GD_OK) {
        return counted;
    }
    if (fill_mode > 1U) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "polygon fill mode is outside the declared enum");
    }
    return translate(&fill_recorder_polygon_from_c, recorder, brush, points,
                     count, fill_mode);
}

gd_result api_recorder_draw_path(gd_handle recorder, gd_handle pen,
                                 gd_handle path) {
    return registry().with_two<gui_drawing::GraphicsRecorder, gui_drawing::Pen>(
        recorder, GD_OBJECT_RECORDER, pen, GD_OBJECT_PEN,
        &draw_recorder_path, path);
}

gd_result api_recorder_fill_path(gd_handle recorder, gd_handle brush,
                                 gd_handle path) {
    return registry().with_two<gui_drawing::GraphicsRecorder,
                               gui_drawing::Brush>(
        recorder, GD_OBJECT_RECORDER, brush, 0U,
        &fill_recorder_path, path);
}

gd_result api_recorder_draw_image(gd_handle recorder, gd_handle image,
                                  gd_rect destination, gd_rect source,
                                  gd_handle attributes) {
    return registry().with_two<gui_drawing::GraphicsRecorder,
                               gui_drawing::ImageReference>(
        recorder, GD_OBJECT_RECORDER, image, GD_OBJECT_IMAGE_REFERENCE,
        &draw_recorder_image, destination, source, attributes);
}

gd_result api_bitmap_create(std::uint32_t width, std::uint32_t height,
                            std::uint32_t pixel_format, gd_handle* output) {
    if (pixel_format != GD_PIXEL_BGRA32_PREMULTIPLIED) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "bitmap pixel format is outside the declared enum");
    }
    return registry().create<gui_drawing::Bitmap>(
        GD_OBJECT_BITMAP, output, width, height,
        static_cast<gui_drawing::PixelFormat>(pixel_format));
}

gd_result api_bitmap_dimensions(gd_handle handle, std::uint32_t* width,
                                std::uint32_t* height,
                                std::uint32_t* pixel_format,
                                std::uint64_t* generation) {
    if (width == nullptr || height == nullptr || pixel_format == nullptr ||
        generation == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "bitmap dimensions requires all output fields");
    }
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &query_bitmap_dimensions, width, height,
        pixel_format, generation);
}

gd_result api_bitmap_get_pixel(gd_handle handle, std::uint32_t x,
                               std::uint32_t y, gd_color* color) {
    if (color == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "bitmap pixel read requires output");
    }
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &query_bitmap_pixel, x, y, color);
}

gd_result api_bitmap_set_pixel(gd_handle handle, std::uint32_t x,
                               std::uint32_t y, gd_color color) {
    return translate(&set_bitmap_pixel, handle, x, y, color);
}

gd_result api_bitmap_lock(gd_handle handle, std::uint32_t mode,
                          gd_bitmap_lock_view* view) {
    if (view == nullptr || mode > GD_BITMAP_LOCK_READ_WRITE) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "bitmap lock requires output and a declared mode");
    }
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &lock_bitmap, mode, view);
}

gd_result api_bitmap_unlock(gd_handle handle, std::uint64_t token) {
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &gui_drawing::Bitmap::unlock, token);
}

gd_result api_bitmap_edit_begin(gd_handle handle, gd_rect_i bounds,
                                gd_bitmap_edit_view* view) {
    if (view == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "bounded bitmap edit requires a view output");
    }
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &begin_bitmap_edit, bounds, view);
}

gd_result api_bitmap_edit_commit(gd_handle handle, std::uint64_t token,
                                 std::uint64_t* generation) {
    if (generation == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "bounded bitmap commit requires a generation output");
    }
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &commit_bitmap_edit, token, generation);
}

gd_result api_bitmap_edit_cancel(gd_handle handle, std::uint64_t token) {
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &gui_drawing::Bitmap::cancel_edit, token);
}

gd_result api_bitmap_changes_since(gd_handle handle, std::uint64_t generation,
                                   gd_rect_i* rectangles,
                                   std::uint64_t capacity,
                                   std::uint64_t* required_count,
                                   gd_bitmap_damage_summary* summary) {
    if (required_count == nullptr || summary == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "bitmap damage query requires count and summary outputs");
    }
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &query_bitmap_changes, generation,
        rectangles, capacity, required_count, summary);
}

gd_result api_bitmap_clone(gd_handle handle, gd_rect_i source,
                           gd_handle* output) {
    if (output == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "bitmap clone requires output");
    }
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &clone_bitmap, source, output);
}

gd_result api_bitmap_make_transparent(gd_handle handle, gd_color key) {
    return translate(&make_bitmap_transparent, handle, key);
}

gd_result api_bitmap_thumbnail(gd_handle handle, std::uint32_t width,
                               std::uint32_t height, gd_handle* output) {
    if (output == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "bitmap thumbnail requires output");
    }
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &create_bitmap_thumbnail, width, height,
        output);
}

gd_result api_bitmap_adjusted(gd_handle bitmap_handle,
                              gd_handle attributes_handle,
                              gd_handle* output) {
    if (output == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "adjusted bitmap requires output");
    }
    return registry().with_two<gui_drawing::Bitmap,
                               gui_drawing::ImageAttributes>(
        bitmap_handle, GD_OBJECT_BITMAP,
        attributes_handle, GD_OBJECT_IMAGE_ATTRIBUTES,
        &create_adjusted_bitmap, output);
}

gd_result api_recorder_draw_bitmap(gd_handle recorder, gd_handle bitmap,
                                   gd_rect destination, gd_rect source,
                                   gd_handle attributes) {
    return registry().with_two<gui_drawing::GraphicsRecorder,
                               gui_drawing::Bitmap>(
        recorder, GD_OBJECT_RECORDER, bitmap, GD_OBJECT_BITMAP,
        &draw_recorder_bitmap, destination, source, attributes);
}

gd_result api_graphics_path_add_arc(gd_handle handle, gd_rect bounds,
                                    double start_angle, double sweep_angle) {
    return registry().with<gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_GRAPHICS_PATH, &add_path_arc, bounds, start_angle,
        sweep_angle);
}

gd_result api_graphics_path_add_path(gd_handle handle, gd_handle appended,
                                     std::uint32_t connect) {
    if (connect > 1U) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "path connect must be zero or one");
    }
    return registry().with_two<gui_drawing::GraphicsPath, gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_GRAPHICS_PATH, appended, GD_OBJECT_GRAPHICS_PATH,
        &append_path, connect);
}

gd_result api_graphics_path_transform(gd_handle handle, gd_matrix transform) {
    return registry().with<gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_GRAPHICS_PATH, &transform_path, transform);
}

gd_result api_graphics_path_is_visible(gd_handle handle, gd_point point,
                                       std::uint32_t* visible) {
    if (visible == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "path visibility requires output");
    }
    return registry().with<gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_GRAPHICS_PATH, &query_path_visibility, point,
        visible);
}

gd_result api_graphics_path_points(gd_handle handle, gd_point* points,
                                   std::uint64_t capacity,
                                   std::uint64_t* required_count) {
    if (required_count == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "path points requires count output");
    }
    return registry().with<gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_GRAPHICS_PATH, &read_path_points, points, capacity,
        required_count);
}

gd_result api_graphics_path_flatten(gd_handle handle, double tolerance, gd_point* points,
                                    std::uint64_t point_capacity, std::uint64_t* figure_ends,
                                    std::uint32_t* figure_closed, std::uint64_t figure_capacity,
                                    std::uint64_t* point_count, std::uint64_t* figure_count) {
    if (point_count == nullptr || figure_count == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "path flattening requires count outputs");
    }
    if (!std::isfinite(tolerance) || !(tolerance > 0.0)) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "path flattening tolerance must be positive and finite");
    }
    return registry().with<gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_GRAPHICS_PATH, &flatten_path, tolerance, points, point_capacity,
        figure_ends, figure_closed, figure_capacity, point_count, figure_count);
}

gd_result api_graphics_path_clone(gd_handle handle, gd_handle* output) {
    if (output == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "path clone requires output");
    }
    return registry().with<gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_GRAPHICS_PATH, &clone_path, output);
}

gd_result api_image_attributes_set_remap_table(gd_handle handle,
                                               const gd_color_remap* entries,
                                               std::uint64_t count) {
    if (const gd_result counted = check_count(entries, count, 0U, 4096U, "image remap table is not bounded");
        counted != GD_OK) {
        return counted;
    }
    return translate(&set_image_remap_table, handle, entries, count);
}

gd_result api_image_attributes_reset_remap_table(gd_handle handle) {
    return registry().with<gui_drawing::ImageAttributes>(
        handle, GD_OBJECT_IMAGE_ATTRIBUTES,
        &gui_drawing::ImageAttributes::reset_remap_table);
}

gd_result api_hatch_brush_create(std::uint32_t style, gd_color foreground,
                                 gd_color background, gd_handle* output) {
    if (style > GD_HATCH_DIAGONAL_CROSS) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "hatch style is outside the declared enum");
    }
    return translate(&create_hatch_brush, style, foreground, background,
                     output);
}

gd_result api_linear_gradient_brush_create(gd_rect bounds, gd_color first,
                                           gd_color second, double angle,
                                           std::uint32_t wrap_mode,
                                           gd_handle* output) {
    if (wrap_mode > GD_WRAP_CLAMP) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "gradient wrap mode is invalid");
    }
    return translate(&create_linear_gradient_brush, bounds, first, second,
                     angle, wrap_mode, output);
}

gd_result api_linear_gradient_set_blend(gd_handle handle,
                                        const double* factors,
                                        const double* positions,
                                        std::uint64_t count) {
    if (const gd_result counted = check_count(factors, count, 2U, 4096U, "gradient blend arrays are not bounded");
        counted != GD_OK) {
        return counted;
    }
    if (positions == nullptr) return fail(GD_ERROR_INVALID_ARGUMENT, "gradient blend arrays are not bounded");
    return registry().with<gui_drawing::LinearGradientBrush>(
        handle, GD_OBJECT_LINEAR_GRADIENT_BRUSH,
        &set_linear_gradient_blend, factors, positions, count);
}

[[nodiscard]] gd_result set_gradient_interpolation(
    gd_handle handle, std::uint32_t kind, const gd_color* colors,
    const double* positions, std::uint64_t count) {
    if (const gd_result counted = check_count(colors, count, 2U, 4096U,
                                              "gradient interpolation arrays are not bounded");
        counted != GD_OK) {
        return counted;
    }
    if (positions == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "gradient interpolation arrays are not bounded");
    }
    return translate(&apply_gradient_interpolation, handle, kind, colors,
                     positions, count);
}

gd_result api_linear_gradient_set_interpolation(
    gd_handle handle, const gd_color* colors, const double* positions,
    std::uint64_t count) {
    return set_gradient_interpolation(handle, GD_OBJECT_LINEAR_GRADIENT_BRUSH,
                                      colors, positions, count);
}

gd_result api_linear_gradient_set_wrap_mode(gd_handle handle,
                                            std::uint32_t wrap_mode) {
    if (wrap_mode > GD_WRAP_CLAMP) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "gradient wrap mode is invalid");
    }
    const gui_drawing::WrapMode native_mode =
        static_cast<gui_drawing::WrapMode>(wrap_mode);
    return registry().with<gui_drawing::LinearGradientBrush>(
        handle, GD_OBJECT_LINEAR_GRADIENT_BRUSH,
        &gui_drawing::LinearGradientBrush::set_wrap_mode, native_mode);
}

gd_result api_path_gradient_brush_create(const gd_point* points,
                                         std::uint64_t count,
                                         std::uint32_t wrap_mode,
                                         gd_handle* output) {
    if (const gd_result counted = check_count(points, count, 3U, gui_drawing::GraphicsPath::maximum_elements,
                                              "path gradient requires 3 or more bounded points");
        counted != GD_OK) {
        return counted;
    }
    if (wrap_mode > GD_WRAP_CLAMP) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "gradient wrap mode is invalid");
    }
    return translate(&create_path_gradient_brush, points, count, wrap_mode,
                     output);
}

gd_result api_path_gradient_set_center_color(gd_handle handle, gd_color color) {
    return translate(&set_path_gradient_center_color, handle, color);
}

gd_result api_path_gradient_set_center_point(gd_handle handle, gd_point point) {
    return registry().with<gui_drawing::PathGradientBrush>(
        handle, GD_OBJECT_PATH_GRADIENT_BRUSH,
        &set_path_gradient_center_point, point);
}

gd_result api_path_gradient_set_surround_colors(gd_handle handle,
                                                const gd_color* colors,
                                                std::uint64_t count) {
    if (const gd_result counted = check_count(colors, count, 1U, gui_drawing::GraphicsPath::maximum_elements,
                                              "surround color array is not bounded");
        counted != GD_OK) {
        return counted;
    }
    return translate(&set_path_gradient_surround_colors, handle, colors,
                     count);
}

gd_result api_path_gradient_set_interpolation(
    gd_handle handle, const gd_color* colors, const double* positions,
    std::uint64_t count) {
    return set_gradient_interpolation(handle, GD_OBJECT_PATH_GRADIENT_BRUSH,
                                      colors, positions, count);
}

gd_result api_region_create_rectangle(gd_rect rectangle, gd_handle* output) {
    return registry().create<gui_drawing::Region>(
        GD_OBJECT_REGION, output, rect_from_c(rectangle));
}

gd_result api_region_create_path(gd_handle path, gd_handle* output) {
    if (output == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "region path constructor requires output");
    }
    return registry().with<gui_drawing::GraphicsPath>(
        path, GD_OBJECT_GRAPHICS_PATH,
        &create_region_from_path, output);
}

gd_result api_region_union_rectangle(gd_handle handle, gd_rect rectangle) {
    return registry().with<gui_drawing::Region>(
        handle, GD_OBJECT_REGION, &unite_region_rectangle, rectangle);
}

gd_result api_region_union_path(gd_handle handle, gd_handle path) {
    return registry().with_two<gui_drawing::Region, gui_drawing::GraphicsPath>(
        handle, GD_OBJECT_REGION, path, GD_OBJECT_GRAPHICS_PATH,
        &unite_region_path);
}

gd_result api_region_exclude_rectangle(gd_handle handle, gd_rect rectangle) {
    return registry().with<gui_drawing::Region>(
        handle, GD_OBJECT_REGION, &exclude_region_rectangle, rectangle);
}

gd_result api_region_is_visible(gd_handle handle, gd_point point,
                                std::uint32_t* visible) {
    if (visible == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "region visibility requires output");
    }
    return registry().with<gui_drawing::Region>(
        handle, GD_OBJECT_REGION, &query_region_visibility, point, visible);
}

gd_result api_region_bounds(gd_handle handle, gd_rect* bounds) {
    if (bounds == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "region bounds requires output");
    }
    return registry().with<gui_drawing::Region>(
        handle, GD_OBJECT_REGION, &query_region_bounds, bounds);
}

gd_result api_raster_service_install(const gd_raster_service_v0* service) {
    if (service == nullptr || (*service).struct_size < sizeof(gd_raster_service_v0) ||
        (*service).abi_version != GD_ABI_VERSION_0_1 || (*service).execute == nullptr ||
        (*service).encode_png == nullptr || (*service).decode_png == nullptr ||
        (*service).measure_string == nullptr || (*service).resolve_font == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "raster service requires a complete ABI 0.1 callback table");
    }
    std::scoped_lock lock(raster_service_mutex());
    if (raster_service.abi_version != 0U &&
        (raster_service.execute != (*service).execute ||
         raster_service.encode_png != (*service).encode_png ||
         raster_service.decode_png != (*service).decode_png ||
         raster_service.measure_string != (*service).measure_string ||
         raster_service.resolve_font != (*service).resolve_font)) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "a different GUI.Drawing raster service is already installed");
    }
    raster_service = *service;
    return GD_OK;
}

gd_result api_recorder_execute_from(gd_handle recorder, gd_handle bitmap,
                                    std::uint64_t first_command,
                                    std::uint64_t* commands_executed) {
    if (commands_executed == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "raster execution requires count output");
    }
    gd_raster_service_v0 service{};
    if (!load_raster_service(service)) {
        return fail(GD_ERROR_NOT_AVAILABLE, "GUI.Drawing raster is not installed (raster_initialize)");
    }
    return registry().with_two<gui_drawing::GraphicsRecorder, gui_drawing::Bitmap>(
        recorder, GD_OBJECT_RECORDER, bitmap, GD_OBJECT_BITMAP,
        &execute_recorded_commands, service, first_command,
        commands_executed);
}

gd_result api_recorder_execute(gd_handle recorder, gd_handle bitmap,
                               std::uint64_t* commands_executed) {
    return api_recorder_execute_from(recorder, bitmap, 0U, commands_executed);
}

gd_result api_bitmap_encode_png(gd_handle bitmap, void* buffer,
                                std::uint64_t capacity,
                                std::uint64_t* required_size) {
    if (required_size == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "PNG encode requires size output");
    }
    gd_raster_service_v0 service{};
    if (!load_raster_service(service)) {
        return fail(GD_ERROR_NOT_AVAILABLE, "GUI.Drawing raster is not installed (raster_initialize)");
    }
    return registry().with<gui_drawing::Bitmap>(
        bitmap, GD_OBJECT_BITMAP, &encode_bitmap_png, service, buffer,
        capacity, required_size);
}

gd_result api_bitmap_decode_png(const void* data, std::uint64_t size,
                                gd_handle* output) {
    if (data == nullptr || size == 0U || output == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "PNG decode requires nonempty bytes and output handle");
    }
    gd_raster_service_v0 service{};
    if (!load_raster_service(service)) {
        return fail(GD_ERROR_NOT_AVAILABLE, "GUI.Drawing raster is not installed (raster_initialize)");
    }
    void* decoded = nullptr;
    const gd_result result = service.decode_png(data, size, &decoded);
    if (result != GD_OK || decoded == nullptr) {
        delete static_cast<gui_drawing::Bitmap*>(decoded);
        return fail(result == GD_OK ? GD_ERROR_INTERNAL : result,
                    "GUI.Drawing PNG decode failed");
    }
    std::unique_ptr<gui_drawing::Bitmap> bitmap(
        static_cast<gui_drawing::Bitmap*>(decoded));
    return registry().adopt(GD_OBJECT_BITMAP, output, std::move(bitmap));
}

gd_result api_image_attributes_clone(gd_handle handle, gd_handle* output) {
    if (output == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "image attributes clone requires output");
    }
    return registry().with<gui_drawing::ImageAttributes>(
        handle, GD_OBJECT_IMAGE_ATTRIBUTES,
        &clone_image_attributes, output);
}

gd_result api_measure_string(gd_string_view text, gd_handle font,
                             gd_handle format, double layout_width,
                             gd_size* measured) {
    if (measured == nullptr || !std::isfinite(layout_width) || layout_width < 0.0) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "text measurement requires valid width and output");
    }
    std::string native_text;
    try {
        native_text = string_from_c(text, "measure string text");
    } catch (const std::exception& error) {
        return fail(GD_ERROR_INVALID_ARGUMENT, error.what());
    }
    if (native_text.size() > gui_drawing::GraphicsRecorder::maximum_text_bytes) {
        return fail(GD_ERROR_LIMIT_EXCEEDED, "measure string text exceeds its byte limit");
    }
    gd_raster_service_v0 service{};
    if (!load_raster_service(service)) {
        return fail(GD_ERROR_NOT_AVAILABLE, "GUI.Drawing raster is not installed (raster_initialize)");
    }
    const gd_string_view view{native_text.data(), native_text.size()};
    if (format.slot == 0U && format.generation == 0U) {
        return registry().with<gui_drawing::Font>(
            font, GD_OBJECT_FONT, &measure_string_without_format, service,
            view, layout_width, measured);
    }
    return registry().with_two<gui_drawing::Font, gui_drawing::StringFormat>(
        font, GD_OBJECT_FONT, format, GD_OBJECT_STRING_FORMAT,
        &measure_string_with_format, service, view, layout_width, measured);
}

gd_result api_font_resolve(gd_handle font, gd_font_resolution* resolution) {
    if (resolution == nullptr ||
        (*resolution).struct_size < sizeof(gd_font_resolution)) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "font resolution requires a size-prefixed output record");
    }
    (*resolution).struct_size = sizeof(gd_font_resolution);
    gd_raster_service_v0 service{};
    if (!load_raster_service(service)) {
        return registry().with<gui_drawing::Font>(
            font, GD_OBJECT_FONT, &resolve_font_from_table, resolution);
    }
    return registry().with<gui_drawing::Font>(
        font, GD_OBJECT_FONT, &resolve_font_with_service, service, resolution);
}

gd_result api_bitmap_export_hbitmap(gd_handle handle, gd_color background,
                                    std::uintptr_t* output) {
    if (output == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "HBITMAP export requires output");
    }
    *output = 0U;
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &export_bitmap_hbitmap, background, output);
}

gd_result api_bitmap_import_hbitmap(std::uintptr_t source,
                                    gd_handle* output) {
    if (source == 0U || output == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "HBITMAP import requires a native handle and output");
    }
    *output = {};
    return translate(&import_bitmap_hbitmap, source, output);
}

gd_result api_native_surface_capture(std::uintptr_t source, std::uint32_t kind,
                                     gd_handle* output, gd_rect* captured_bounds) {
    if (source == 0U || output == nullptr || captured_bounds == nullptr ||
        kind > GD_NATIVE_SURFACE_HWND) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "native surface capture requires a handle, kind, and output");
    }
    *output = {};
    *captured_bounds = {};
    return translate(&capture_native_surface, source, kind, output,
                     captured_bounds);
}

gd_result api_native_surface_present(std::uintptr_t destination,
                                     std::uint32_t kind, gd_handle handle) {
    if (destination == 0U || kind > GD_NATIVE_SURFACE_HWND) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "native surface presentation requires a handle and kind");
    }
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &present_native_surface, destination, kind);
}

gd_result api_native_surface_refresh(std::uintptr_t source, std::uint32_t kind,
                                     gd_handle handle, gd_rect* refreshed_bounds) {
    if (source == 0U || refreshed_bounds == nullptr ||
        kind > GD_NATIVE_SURFACE_HWND) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "native surface refresh requires a handle, kind, and bounds output");
    }
    *refreshed_bounds = {};
    return registry().with_surface_staging(
        handle, &refresh_native_surface, source, kind, refreshed_bounds);
}

gd_result api_native_surface_publish_retained(std::uintptr_t destination,
                                              std::uint32_t kind,
                                              gd_handle handle) {
    if (destination == 0U || kind > GD_NATIVE_SURFACE_HWND) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "retained surface publication requires a handle and kind");
    }
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &publish_native_surface, destination, kind);
}

gd_result api_bitmap_acquire_hdc(gd_handle handle, std::uintptr_t* output,
                                 std::uint64_t* lease_token) {
    if (output == nullptr || lease_token == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "HDC acquisition requires handle and token outputs");
    }
    *output = 0U;
    *lease_token = 0U;
    const gd_result acquired = registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &acquire_bitmap_hdc, output, lease_token);
    if (acquired != GD_OK) return acquired;
    const gd_result retained = registry().retain(handle);
    if (retained == GD_OK) return GD_OK;
    registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &release_bitmap_hdc_raw, *lease_token);
    *output = 0U;
    *lease_token = 0U;
    return retained;
}

gd_result api_bitmap_release_hdc(gd_handle handle, std::uint64_t lease_token) {
    if (lease_token == 0U) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "HDC release requires a lease token");
    }
    const gd_result released = registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &release_bitmap_hdc, lease_token);
    if (released != GD_OK) return released;
    return registry().release(handle);
}

// A caller array of `count` batch elements, checked before any element is
// read (check_count with the batch limit).
gd_result check_batch(const void* elements, std::uint64_t count, std::uint64_t minimum, const char* refusal) {
    return check_count(elements, count, minimum, gui_drawing::GraphicsRecorder::maximum_batch_elements, refusal);
}

gd_result api_pen_set_line_join(gd_handle handle, std::uint32_t join, double miter_limit) {
    if (join > GD_LINE_JOIN_ROUND) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "line join is outside the declared enum");
    }
    return registry().with<gui_drawing::Pen>(handle, GD_OBJECT_PEN, &set_pen_line_join, join, miter_limit);
}

gd_result api_pen_set_line_cap(gd_handle handle, std::uint32_t cap) {
    if (cap > GD_LINE_CAP_ROUND) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "line cap is outside the declared enum");
    }
    return registry().with<gui_drawing::Pen>(handle, GD_OBJECT_PEN, &set_pen_line_cap, cap);
}

gd_result draw_recorder_lines_from_c(gd_handle recorder, gd_handle pen, const gd_point* points,
                                     std::uint64_t count, bool pairs) {
    const std::vector<gui_drawing::PointF> native = points_from_c(points, count);
    if (pairs) {
        return registry().with_two<gui_drawing::GraphicsRecorder, gui_drawing::Pen>(
            recorder, GD_OBJECT_RECORDER, pen, GD_OBJECT_PEN, &draw_recorder_line_segments, native);
    }
    return registry().with_two<gui_drawing::GraphicsRecorder, gui_drawing::Pen>(
        recorder, GD_OBJECT_RECORDER, pen, GD_OBJECT_PEN, &draw_recorder_lines, native);
}

gd_result api_recorder_draw_lines(gd_handle recorder, gd_handle pen, const gd_point* points,
                                  std::uint64_t count) {
    if (const gd_result counted = check_batch(points, count, 2U, "polyline requires 2 or more bounded points");
        counted != GD_OK) {
        return counted;
    }
    return translate(&draw_recorder_lines_from_c, recorder, pen, points, count, false);
}

gd_result api_recorder_draw_line_segments(gd_handle recorder, gd_handle pen, const gd_point* points,
                                          std::uint64_t count) {
    if (const gd_result counted = check_batch(points, count, 2U, "line segments require bounded points in pairs");
        counted != GD_OK) {
        return counted;
    }
    if (count % 2U != 0U) return fail(GD_ERROR_INVALID_ARGUMENT, "line segments require bounded points in pairs");
    return translate(&draw_recorder_lines_from_c, recorder, pen, points, count, true);
}

gd_result fill_recorder_rectangles_from_c(gd_handle recorder, gd_handle brush, const gd_rect* rects,
                                          std::uint64_t count) {
    std::vector<gui_drawing::RectF> native;
    native.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) native.push_back(rect_from_c(rects[index]));
    return registry().with_two<gui_drawing::GraphicsRecorder, gui_drawing::Brush>(
        recorder, GD_OBJECT_RECORDER, brush, 0U, &fill_recorder_rectangles, native);
}

gd_result api_recorder_fill_rectangles(gd_handle recorder, gd_handle brush, const gd_rect* rects,
                                       std::uint64_t count) {
    if (const gd_result counted = check_batch(rects, count, 1U, "fill rectangles requires 1 or more bounded rectangles");
        counted != GD_OK) {
        return counted;
    }
    return translate(&fill_recorder_rectangles_from_c, recorder, brush, rects, count);
}

gd_result draw_recorder_markers_from_c(gd_handle recorder, const gd_point* centers, std::uint64_t count,
                                       const gd_marker_style* style) {
    gui_drawing::MarkerStyle native;
    native.shape = static_cast<gui_drawing::MarkerShape>((*style).shape);
    native.size = (*style).size;
    native.fill = color_from_c((*style).fill);
    native.edge = color_from_c((*style).edge);
    native.edge_width = (*style).edge_width;
    const std::vector<gui_drawing::PointF> native_centers = points_from_c(centers, count);
    return registry().with<gui_drawing::GraphicsRecorder>(recorder, GD_OBJECT_RECORDER, &draw_recorder_markers,
                                                          native_centers, native);
}

gd_result api_recorder_draw_markers(gd_handle recorder, const gd_point* centers, std::uint64_t count,
                                    const gd_marker_style* style) {
    if (const gd_result counted = check_batch(centers, count, 1U, "markers require 1 or more bounded centres");
        counted != GD_OK) {
        return counted;
    }
    if (style == nullptr || (*style).shape > GD_MARKER_ASTERISK) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "markers require a style with a declared shape");
    }
    return translate(&draw_recorder_markers_from_c, recorder, centers, count, style);
}

gd_result api_conic_gradient_brush_create(gd_point center, double start_angle, gd_color first,
                                          gd_color second, gd_handle* output) {
    return translate(&create_conic_gradient_brush, center, start_angle, first, second, output);
}

gd_result api_conic_gradient_set_interpolation(gd_handle handle, const gd_color* colors,
                                               const double* positions, std::uint64_t count) {
    return set_gradient_interpolation(handle, GD_OBJECT_CONIC_GRADIENT_BRUSH, colors, positions, count);
}

gd_result api_checker_brush_create(double cell_size, gd_color first, gd_color second, gd_handle* output) {
    return translate(&create_checker_brush, cell_size, first, second, output);
}

gd_result fill_recorder_grid_from_c(gd_handle recorder, gd_rect rect, const gd_grid_pattern* grid) {
    gui_drawing::GridPatternStyle native;
    native.origin = point_from_c((*grid).origin);
    native.spacing = (*grid).spacing;
    native.major_every = (*grid).major_every;
    native.minor = color_from_c((*grid).minor);
    native.major = color_from_c((*grid).major);
    native.line_width = (*grid).line_width;
    return registry().with<gui_drawing::GraphicsRecorder>(recorder, GD_OBJECT_RECORDER, &fill_recorder_grid, rect,
                                                          native);
}

gd_result api_recorder_fill_grid_pattern(gd_handle recorder, gd_rect rect, const gd_grid_pattern* grid) {
    if (grid == nullptr) return fail(GD_ERROR_INVALID_ARGUMENT, "grid fill requires a grid");
    return translate(&fill_recorder_grid_from_c, recorder, rect, grid);
}

gd_result api_recorder_fill_proportion(gd_handle recorder, gd_handle brush, gd_rect track, gd_corner_radii radii,
                                       double proportion, std::uint32_t direction) {
    if (direction > GD_FILL_TOP_TO_BOTTOM) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "fill direction is outside the declared enum");
    }
    return registry().with_two<gui_drawing::GraphicsRecorder, gui_drawing::Brush>(
        recorder, GD_OBJECT_RECORDER, brush, 0U, &fill_recorder_proportion, track, radii, proportion, direction);
}

gd_result draw_recorder_string_over_fill_from_c(gd_handle recorder, gd_string_view text, gd_handle font,
                                                gd_handle brush, gd_color on_fill_color, gd_point origin,
                                                gd_handle format, gd_rect fill) {
    const std::string native_text = string_from_c(text, "draw string text");
    return registry().with_text_objects(recorder, font, brush, format, &draw_recorder_string_over_fill,
                                        native_text, on_fill_color, origin, fill);
}

gd_result api_recorder_draw_string_over_fill(gd_handle recorder, gd_string_view text, gd_handle font,
                                             gd_handle brush, gd_color on_fill_color, gd_point origin,
                                             gd_handle format, gd_rect fill) {
    return translate(&draw_recorder_string_over_fill_from_c, recorder, text, font, brush, on_fill_color, origin,
                     format, fill);
}

gd_result api_contrast_ratio(gd_color first, gd_color second, double* ratio) {
    if (ratio == nullptr) return fail(GD_ERROR_INVALID_ARGUMENT, "contrast ratio requires output");
    return translate(&read_contrast_ratio, first, second, ratio);
}

gd_result api_contrasting_text_color(gd_color background, gd_color light, gd_color dark, gd_color* chosen) {
    if (chosen == nullptr) return fail(GD_ERROR_INVALID_ARGUMENT, "contrasting text colour requires output");
    return translate(&choose_contrasting_text_color, background, light, dark, chosen);
}

gd_result api_graphics_path_add_rounded_rectangle(gd_handle handle, gd_rect rectangle, gd_corner_radii radii) {
    return registry().with<gui_drawing::GraphicsPath>(handle, GD_OBJECT_GRAPHICS_PATH, &add_path_rounded_rectangle,
                                                      rectangle, radii);
}

gd_result add_path_quadratic(gui_drawing::GraphicsPath& path, gd_point from, gd_point control, gd_point to) {
    path.add_quadratic(point_from_c(from), point_from_c(control), point_from_c(to));
    return GD_OK;
}

gd_result add_path_bezier(gui_drawing::GraphicsPath& path, gd_point from, gd_point first_control,
                          gd_point second_control, gd_point to) {
    path.add_bezier(point_from_c(from), point_from_c(first_control), point_from_c(second_control),
                    point_from_c(to));
    return GD_OK;
}

gd_result add_path_beziers(gui_drawing::GraphicsPath& path, const std::vector<gui_drawing::PointF>& points) {
    path.add_beziers(points);
    return GD_OK;
}

gd_result add_path_polygon(gui_drawing::GraphicsPath& path, const std::vector<gui_drawing::PointF>& points) {
    path.add_polygon(points);
    return GD_OK;
}

gd_result add_path_pie(gui_drawing::GraphicsPath& path, gd_rect bounds, double start_angle, double sweep_angle) {
    path.add_pie(rect_from_c(bounds), start_angle, sweep_angle);
    return GD_OK;
}

gd_result set_path_fill_mode(gui_drawing::GraphicsPath& path, std::uint32_t fill_mode) {
    path.set_fill_mode(static_cast<gui_drawing::FillMode>(fill_mode));
    return GD_OK;
}

gd_result read_path_fill_mode(gui_drawing::GraphicsPath& path, std::uint32_t* fill_mode) {
    *fill_mode = static_cast<std::uint32_t>(path.fill_mode());
    return GD_OK;
}

gd_result add_path_point_list(gd_handle handle, const gd_point* points, std::uint64_t count, bool beziers) {
    const std::vector<gui_drawing::PointF> native = points_from_c(points, count);
    if (beziers) {
        return registry().with<gui_drawing::GraphicsPath>(handle, GD_OBJECT_GRAPHICS_PATH, &add_path_beziers, native);
    }
    return registry().with<gui_drawing::GraphicsPath>(handle, GD_OBJECT_GRAPHICS_PATH, &add_path_polygon, native);
}

gd_result api_graphics_path_add_quadratic(gd_handle handle, gd_point from, gd_point control, gd_point to) {
    return registry().with<gui_drawing::GraphicsPath>(handle, GD_OBJECT_GRAPHICS_PATH, &add_path_quadratic, from,
                                                      control, to);
}

gd_result api_graphics_path_add_bezier(gd_handle handle, gd_point from, gd_point first_control,
                                       gd_point second_control, gd_point to) {
    return registry().with<gui_drawing::GraphicsPath>(handle, GD_OBJECT_GRAPHICS_PATH, &add_path_bezier, from,
                                                      first_control, second_control, to);
}

gd_result api_graphics_path_add_beziers(gd_handle handle, const gd_point* points, std::uint64_t count) {
    if (const gd_result counted = check_count(points, count, 4U, gui_drawing::GraphicsPath::maximum_elements,
                                              "joined Bezier curves require 4 + 3n bounded points");
        counted != GD_OK) {
        return counted;
    }
    if ((count - 1U) % 3U != 0U) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "joined Bezier curves require 4 + 3n bounded points");
    }
    return translate(&add_path_point_list, handle, points, count, true);
}

gd_result api_graphics_path_add_polygon(gd_handle handle, const gd_point* points, std::uint64_t count) {
    if (const gd_result counted = check_count(points, count, 3U, gui_drawing::GraphicsPath::maximum_elements,
                                              "a path polygon requires 3 or more bounded points");
        counted != GD_OK) {
        return counted;
    }
    return translate(&add_path_point_list, handle, points, count, false);
}

gd_result api_graphics_path_add_pie(gd_handle handle, gd_rect bounds, double start_angle, double sweep_angle) {
    return registry().with<gui_drawing::GraphicsPath>(handle, GD_OBJECT_GRAPHICS_PATH, &add_path_pie, bounds,
                                                      start_angle, sweep_angle);
}

gd_result api_graphics_path_set_fill_mode(gd_handle handle, std::uint32_t fill_mode) {
    if (fill_mode > 1U) return fail(GD_ERROR_INVALID_ARGUMENT, "path fill mode is outside the declared enum");
    return registry().with<gui_drawing::GraphicsPath>(handle, GD_OBJECT_GRAPHICS_PATH, &set_path_fill_mode,
                                                      fill_mode);
}

gd_result api_graphics_path_fill_mode(gd_handle handle, std::uint32_t* fill_mode) {
    if (fill_mode == nullptr) return fail(GD_ERROR_INVALID_ARGUMENT, "path fill mode requires output");
    return registry().with<gui_drawing::GraphicsPath>(handle, GD_OBJECT_GRAPHICS_PATH, &read_path_fill_mode,
                                                      fill_mode);
}

// ABI 0.4: the gapped and per-element marks. Each copies the caller's
// arrays once, after the counts are bounded, and the recorder validates
// the copies before it records.

gd_result fill_recorder_band(gui_drawing::GraphicsRecorder& graphics, gui_drawing::Brush& fill,
                             const std::vector<gui_drawing::PointF>& upper,
                             const std::vector<gui_drawing::PointF>& lower) {
    graphics.fill_band(fill, upper, lower);
    return GD_OK;
}

gd_result fill_recorder_band_from_c(gd_handle recorder, gd_handle brush, const gd_point* upper,
                                    const gd_point* lower, std::uint64_t count) {
    const std::vector<gui_drawing::PointF> native_upper = points_from_c(upper, count);
    const std::vector<gui_drawing::PointF> native_lower = points_from_c(lower, count);
    return registry().with_two<gui_drawing::GraphicsRecorder, gui_drawing::Brush>(
        recorder, GD_OBJECT_RECORDER, brush, 0U, &fill_recorder_band, native_upper, native_lower);
}

gd_result api_recorder_fill_band(gd_handle recorder, gd_handle brush, const gd_point* upper,
                                 const gd_point* lower, std::uint64_t count) {
    const char* const band_refusal = "band requires 2 through 500000 samples in both arrays";
    const gd_result upper_counted = check_batch(upper, count, 2U, band_refusal);
    if (upper_counted != GD_OK) {
        return upper_counted;
    }
    const gd_result lower_counted = check_batch(lower, count, 2U, band_refusal);
    if (lower_counted != GD_OK) {
        return lower_counted;
    }
    if (count > gui_drawing::GraphicsRecorder::maximum_band_samples) {
        return fail(GD_ERROR_INVALID_ARGUMENT, band_refusal);
    }
    return translate(&fill_recorder_band_from_c, recorder, brush, upper, lower, count);
}

gd_result draw_recorder_polyline_runs(gui_drawing::GraphicsRecorder& graphics, gui_drawing::Pen& stroke,
                                      const std::vector<gui_drawing::PointF>& points) {
    graphics.draw_polyline_runs(stroke, points);
    return GD_OK;
}

gd_result draw_recorder_polyline_runs_from_c(gd_handle recorder, gd_handle pen, const gd_point* points,
                                             std::uint64_t count) {
    const std::vector<gui_drawing::PointF> native = points_from_c(points, count);
    return registry().with_two<gui_drawing::GraphicsRecorder, gui_drawing::Pen>(
        recorder, GD_OBJECT_RECORDER, pen, GD_OBJECT_PEN, &draw_recorder_polyline_runs, native);
}

gd_result api_recorder_draw_polyline_runs(gd_handle recorder, gd_handle pen, const gd_point* points,
                                          std::uint64_t count) {
    const gd_result counted = check_batch(points, count, 2U, "polyline runs require 2 or more bounded points");
    if (counted != GD_OK) {
        return counted;
    }
    return translate(&draw_recorder_polyline_runs_from_c, recorder, pen, points, count);
}

gd_result fill_recorder_rectangles_each(gui_drawing::GraphicsRecorder& graphics,
                                        const std::vector<gui_drawing::RectF>& rects,
                                        const std::vector<gui_drawing::Color>& colors) {
    graphics.fill_rectangles_each(rects, colors);
    return GD_OK;
}

gd_result fill_recorder_rectangles_each_from_c(gd_handle recorder, const gd_rect* rects, const gd_color* colors,
                                               std::uint64_t count) {
    std::vector<gui_drawing::RectF> native_rects;
    std::vector<gui_drawing::Color> native_colors;
    native_rects.reserve(static_cast<std::size_t>(count));
    native_colors.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        native_rects.push_back(rect_from_c(rects[index]));
        native_colors.push_back(color_from_c(colors[index]));
    }
    return registry().with<gui_drawing::GraphicsRecorder>(
        recorder, GD_OBJECT_RECORDER, &fill_recorder_rectangles_each, native_rects, native_colors);
}

gd_result api_recorder_fill_rectangles_each(gd_handle recorder, const gd_rect* rects, const gd_color* colors,
                                            std::uint64_t count) {
    const char* const each_refusal = "fill rectangles each requires 1 or more bounded rectangles and colours";
    const gd_result rects_counted = check_batch(rects, count, 1U, each_refusal);
    if (rects_counted != GD_OK) {
        return rects_counted;
    }
    const gd_result colors_counted = check_batch(colors, count, 1U, each_refusal);
    if (colors_counted != GD_OK) {
        return colors_counted;
    }
    return translate(&fill_recorder_rectangles_each_from_c, recorder, rects, colors, count);
}

// ABI 0.5: text in a box, batched strings, reuse and the fitted measure.

gd_result draw_recorder_string_in_rect(gui_drawing::GraphicsRecorder& graphics, gui_drawing::Font& font,
                                       gui_drawing::SolidBrush& brush, gui_drawing::StringFormat& format,
                                       const std::string& text, gd_rect layout) {
    graphics.draw_string_in_rect(text, font, brush, rect_from_c(layout), format);
    return GD_OK;
}

gd_result draw_recorder_string_in_rect_from_c(gd_handle recorder, gd_string_view text, gd_handle font,
                                              gd_handle brush, gd_rect layout, gd_handle format) {
    const std::string native_text = string_from_c(text, "draw string text");
    return registry().with_text_objects(recorder, font, brush, format, &draw_recorder_string_in_rect, native_text,
                                        layout);
}

gd_result api_recorder_draw_string_in_rect(gd_handle recorder, gd_string_view text, gd_handle font,
                                           gd_handle brush, gd_rect layout, gd_handle format) {
    return translate(&draw_recorder_string_in_rect_from_c, recorder, text, font, brush, layout, format);
}

// The items' texts are copied once into `texts` (which the recorder's items
// borrow for the call) after the count is bounded.
gd_result draw_recorder_strings(gui_drawing::GraphicsRecorder& graphics, gui_drawing::Font& font,
                                gui_drawing::StringFormat& format,
                                const std::vector<gui_drawing::GraphicsRecorder::TextItem>& items) {
    graphics.draw_strings(font, format, items);
    return GD_OK;
}

gd_result draw_recorder_strings_from_c(gd_handle recorder, gd_handle font, gd_handle format,
                                       const gd_text_item* items, std::uint64_t count) {
    std::vector<std::string> texts;
    std::vector<gui_drawing::GraphicsRecorder::TextItem> native;
    texts.reserve(static_cast<std::size_t>(count));
    native.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        texts.push_back(string_from_c(items[index].text, "draw strings text"));
    }
    for (std::uint64_t index = 0; index < count; ++index) {
        gui_drawing::GraphicsRecorder::TextItem item{};
        item.text = texts[static_cast<std::size_t>(index)];
        item.origin = point_from_c(items[index].origin);
        item.color = color_from_c(items[index].color);
        native.push_back(item);
    }
    return registry().with_strings_objects(recorder, font, format, &draw_recorder_strings, native);
}

gd_result api_recorder_draw_strings(gd_handle recorder, gd_handle font, gd_handle format,
                                    const gd_text_item* items, std::uint64_t count) {
    if (items == nullptr || count == 0U || count > gui_drawing::GraphicsRecorder::maximum_text_items) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "draw strings requires 1 through 65536 items");
    }
    return translate(&draw_recorder_strings_from_c, recorder, font, format, items, count);
}

gd_result api_recorder_reset(gd_handle handle) {
    return registry().with<gui_drawing::GraphicsRecorder>(handle, GD_OBJECT_RECORDER,
                                                         &gui_drawing::GraphicsRecorder::reset);
}

// The fitted prefix by binary search over byte lengths moved back to code
// point boundaries: about log2(bytes) one-line measures.
gd_result measure_fitted_prefix(gui_drawing::Font& font, gui_drawing::StringFormat* format,
                                const gd_raster_service_v0& service, const std::string& text, double max_width,
                                std::uint64_t* bytes_fitted, double* fitted_width) {
    std::size_t low = 0U;
    std::size_t high = text.size();
    double low_width = 0.0;
    // low and high are code-point boundaries (high starts at the end and is
    // only ever moved back to one), and the answer lies in [low, high].
    while (low < high) {
        const std::size_t first = low + (high - low + 1U) / 2U;
        std::size_t middle = first;
        while (middle > low && middle < text.size() && (static_cast<unsigned char>(text[middle]) & 0xC0U) == 0x80U) {
            --middle;
        }
        // The midpoint fell inside the character that starts at low: measure
        // at the next boundary after it instead (at most high, itself a
        // boundary), so a final multi-byte character is measured too
        // (A-gf-abi-01-F3).
        if (middle == low) {
            middle = first;
            while (middle < high && (static_cast<unsigned char>(text[middle]) & 0xC0U) == 0x80U) {
                ++middle;
            }
        }
        if (middle == low) break;
        gd_size measured{};
        const gd_string_view prefix{text.data(), static_cast<std::uint64_t>(middle)};
        const gd_result result = service.measure_string(&font, format, prefix, 0.0, &measured);
        if (result != GD_OK) return fail(result, "GUI.Drawing raster text measurement failed");
        if (measured.width <= max_width) {
            low = middle;
            low_width = measured.width;
        } else {
            high = middle - 1U;
            while (high > low && high < text.size() && (static_cast<unsigned char>(text[high]) & 0xC0U) == 0x80U) {
                --high;
            }
        }
    }
    *bytes_fitted = static_cast<std::uint64_t>(low);
    *fitted_width = low_width;
    return GD_OK;
}

gd_result measure_fitted_without_format(gui_drawing::Font& font, const gd_raster_service_v0& service,
                                        const std::string& text, double max_width, std::uint64_t* bytes_fitted,
                                        double* fitted_width) {
    return measure_fitted_prefix(font, nullptr, service, text, max_width, bytes_fitted, fitted_width);
}

gd_result measure_fitted_with_format(gui_drawing::Font& font, gui_drawing::StringFormat& format,
                                     const gd_raster_service_v0& service, const std::string& text, double max_width,
                                     std::uint64_t* bytes_fitted, double* fitted_width) {
    return measure_fitted_prefix(font, &format, service, text, max_width, bytes_fitted, fitted_width);
}

gd_result api_measure_string_fitted(gd_string_view text, gd_handle font, gd_handle format, double max_width,
                                    std::uint64_t* bytes_fitted, double* fitted_width) {
    if (bytes_fitted == nullptr || fitted_width == nullptr || !std::isfinite(max_width) || max_width < 0.0) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "fitted measurement requires a valid width and outputs");
    }
    std::string native_text;
    try {
        native_text = string_from_c(text, "measure string text");
    } catch (const std::exception& error) {
        return fail(GD_ERROR_INVALID_ARGUMENT, error.what());
    }
    if (native_text.size() > gui_drawing::GraphicsRecorder::maximum_text_bytes) {
        return fail(GD_ERROR_LIMIT_EXCEEDED, "measure string text exceeds its byte limit");
    }
    gd_raster_service_v0 service{};
    if (!load_raster_service(service)) {
        // As every other raster entry answers (A-gf-abi-01-F14).
        return fail(GD_ERROR_NOT_AVAILABLE, "GUI.Drawing raster is not installed (raster_initialize)");
    }
    if (format.slot == 0U && format.generation == 0U) {
        return registry().with<gui_drawing::Font>(font, GD_OBJECT_FONT, &measure_fitted_without_format, service,
                                                  native_text, max_width, bytes_fitted, fitted_width);
    }
    return registry().with_two<gui_drawing::Font, gui_drawing::StringFormat>(
        font, GD_OBJECT_FONT, format, GD_OBJECT_STRING_FORMAT, &measure_fitted_with_format, service, native_text,
        max_width, bytes_fitted, fitted_width);
}

// ABI 1.0, the bridge to a GUI.Forms paint target (seal decision 12: the
// drawing ABI may take GUI.Forms' own types).

// A gf_paint_target checked and reduced to what recorder_paint_target draws
// with: the lent pixels, the scale, and the damage in whole device pixels
// inside the target.
struct PaintTargetFrame final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t row_bytes{};
    std::byte* pixels{};
    double scale{};
    gui_drawing::RectF device_clip{};
};

// Checks the lent target before anything is drawn: argument faults give
// GD_ERROR_INVALID_ARGUMENT and sizes over the bitmap limits
// GD_ERROR_LIMIT_EXCEEDED. Fills frame only on GD_OK.
gd_result frame_paint_target(const gf_paint_target* target, PaintTargetFrame& frame) {
    if (target == nullptr || (*target).struct_size < sizeof(gf_paint_target)) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "a paint target requires a gf_paint_target of its 1.0 size");
    }
    const gf_paint_target& lent = *target;
    if (lent.pixels == nullptr || lent.width == 0U || lent.height == 0U) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "a paint target requires pixels and a nonzero size");
    }
    if (lent.width > gui_drawing::Bitmap::maximum_dimension || lent.height > gui_drawing::Bitmap::maximum_dimension) {
        return fail(GD_ERROR_LIMIT_EXCEEDED, "a paint target is larger than a bitmap may be");
    }
    const std::uint64_t pixel_count = static_cast<std::uint64_t>(lent.width) * lent.height;
    if (pixel_count > gui_drawing::Bitmap::maximum_pixels) {
        return fail(GD_ERROR_LIMIT_EXCEEDED, "a paint target has more pixels than a bitmap may");
    }
    const std::uint64_t row_minimum = static_cast<std::uint64_t>(lent.width) * 4U;
    if (lent.row_bytes < row_minimum || lent.row_bytes % 4U != 0U) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "a paint target's row_bytes must be a multiple of 4 holding a row");
    }
    if (lent.row_bytes > std::numeric_limits<std::size_t>::max() / lent.height) {
        return fail(GD_ERROR_LIMIT_EXCEEDED, "a paint target's rows do not fit in memory");
    }
    if (!std::isfinite(lent.scale) || lent.scale <= 0.0) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "a paint target's scale must be finite and positive");
    }
    const gf_logical_rect damage = lent.damage;
    if (!std::isfinite(damage.x) || !std::isfinite(damage.y) || !std::isfinite(damage.width) ||
        !std::isfinite(damage.height) || damage.width < 0.0 || damage.height < 0.0) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "a paint target's damage must be finite with a non-negative size");
    }
    // Rounded out to whole device pixels and held to the target (a product
    // that overflows to infinity is held to the target's edge).
    const double target_width = static_cast<double>(lent.width);
    const double target_height = static_cast<double>(lent.height);
    const double left = std::clamp(std::floor(damage.x * lent.scale), 0.0, target_width);
    const double top = std::clamp(std::floor(damage.y * lent.scale), 0.0, target_height);
    const double right = std::clamp(std::ceil((damage.x + damage.width) * lent.scale), left, target_width);
    const double bottom = std::clamp(std::ceil((damage.y + damage.height) * lent.scale), top, target_height);
    frame.width = lent.width;
    frame.height = lent.height;
    frame.row_bytes = static_cast<std::size_t>(lent.row_bytes);
    frame.pixels = reinterpret_cast<std::byte*>(lent.pixels);
    frame.scale = lent.scale;
    frame.device_clip = {left, top, right - left, bottom - top};
    return GD_OK;
}

// The recording framed into device pixels and the damage (a copy, so the
// recording is unchanged), then drawn by the raster module into a bitmap
// borrowing the target's pixels. Both the copy and the bitmap live for the
// call only.
gd_result paint_recorder_onto_target(gui_drawing::GraphicsRecorder& recorder,
                                     const gd_raster_service_v0& service,
                                     const PaintTargetFrame& frame,
                                     std::uint64_t* commands_executed) {
    gui_drawing::GraphicsRecorder framed;
    recorder.frame_into(framed, frame.scale, frame.device_clip);
    const std::unique_ptr<gui_drawing::Bitmap> surface =
        gui_drawing::Bitmap::borrow(frame.width, frame.height, frame.row_bytes, frame.pixels);
    const gd_result result = service.execute(&framed, surface.get(), 0U, commands_executed);
    if (result == GD_OK) return GD_OK;
    return fail(result, "GUI.Drawing could not draw the recording into the paint target");
}

gd_result api_recorder_paint_target(gd_handle recorder, const gf_paint_target* target,
                                    std::uint64_t* commands_executed) {
    if (commands_executed == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "painting a target requires a count output");
    }
    *commands_executed = 0U;
    PaintTargetFrame frame{};
    if (const gd_result framed = frame_paint_target(target, frame); framed != GD_OK) return framed;
    gd_raster_service_v0 service{};
    if (!load_raster_service(service)) {
        return fail(GD_ERROR_NOT_AVAILABLE, "GUI.Drawing raster is not installed (raster_initialize)");
    }
    return registry().with<gui_drawing::GraphicsRecorder>(
        recorder, GD_OBJECT_RECORDER, &paint_recorder_onto_target, service, frame, commands_executed);
}

// The texture and radial gradient brushes (1.1, provisional).
gd_result api_texture_brush_create(gd_handle bitmap, std::uint32_t wrap_mode, gd_handle* output) {
    if (output == nullptr) return fail(GD_ERROR_INVALID_ARGUMENT, "texture brush creation requires an output handle");
    if (wrap_mode > GD_WRAP_CLAMP) return fail(GD_ERROR_INVALID_ARGUMENT, "texture wrap mode is invalid");
    const gui_drawing::WrapMode native_mode = static_cast<gui_drawing::WrapMode>(wrap_mode);
    return registry().with<gui_drawing::Bitmap>(bitmap, GD_OBJECT_BITMAP, &create_texture_brush, native_mode,
                                                output);
}

gd_result api_texture_brush_set_wrap_mode(gd_handle brush, std::uint32_t wrap_mode) {
    if (wrap_mode > GD_WRAP_CLAMP) return fail(GD_ERROR_INVALID_ARGUMENT, "texture wrap mode is invalid");
    const gui_drawing::WrapMode native_mode = static_cast<gui_drawing::WrapMode>(wrap_mode);
    return registry().with<gui_drawing::TextureBrush>(brush, GD_OBJECT_TEXTURE_BRUSH,
                                                      &gui_drawing::TextureBrush::set_wrap_mode, native_mode);
}

gd_result api_texture_brush_set_transform(gd_handle brush, gd_matrix transform) {
    return registry().with<gui_drawing::TextureBrush>(brush, GD_OBJECT_TEXTURE_BRUSH, &set_texture_brush_transform,
                                                      transform);
}

gd_result api_radial_gradient_brush_create(gd_point center, double radius, gd_color center_color,
                                           gd_color edge_color, std::uint32_t wrap_mode, gd_handle* output) {
    if (wrap_mode > GD_WRAP_CLAMP) return fail(GD_ERROR_INVALID_ARGUMENT, "gradient wrap mode is invalid");
    const gui_drawing::WrapMode native_mode = static_cast<gui_drawing::WrapMode>(wrap_mode);
    return translate(&create_radial_gradient_brush, center, radius, center_color, edge_color, native_mode, output);
}

gd_result api_radial_gradient_brush_set_interpolation(gd_handle brush, const gd_color* colors,
                                                      const double* positions, std::uint64_t count) {
    return set_gradient_interpolation(brush, GD_OBJECT_RADIAL_GRADIENT_BRUSH, colors, positions, count);
}

gd_result api_radial_gradient_brush_set_focus(gd_handle brush, gd_point focus) {
    return registry().with<gui_drawing::RadialGradientBrush>(brush, GD_OBJECT_RADIAL_GRADIENT_BRUSH,
                                                             &set_radial_gradient_focus, focus);
}

gd_result api_radial_gradient_brush_set_radii(gd_handle brush, double radius_x, double radius_y) {
    const RadialRadii radii{radius_x, radius_y};
    return registry().with<gui_drawing::RadialGradientBrush>(brush, GD_OBJECT_RADIAL_GRADIENT_BRUSH,
                                                             &set_radial_gradient_radii, radii);
}

// pen_set_trim (1.1, provisional): Pen::set_trim, whose refusal (a value
// out of range or not finite) becomes GD_ERROR_INVALID_ARGUMENT.
gd_result api_pen_set_trim(gd_handle pen, double start, double end, double offset) {
    return registry().with<gui_drawing::Pen>(pen, GD_OBJECT_PEN, &set_pen_trim, start, end, offset);
}

// raster_initialize (1.1, provisional): the raster objects' installer, or
// the absent one in a build without the renderer.
gd_result api_raster_initialize() {
    const gd_result installed = gui_drawing_raster_module_initialize();
    if (installed == GD_ERROR_NOT_AVAILABLE) {
        return fail(GD_ERROR_NOT_AVAILABLE, "GUI.Drawing was built without its raster");
    }
    if (installed != GD_OK) return fail(installed, "GUI.Drawing raster could not be installed");
    return GD_OK;
}

const gd_api_v0 api_table{
    sizeof(gd_api_v0), GD_ABI_VERSION_1_1,
    &api_last_error, &api_retain, &api_release, &api_dispose,
    &api_object_state, &api_object_kind,
    &api_solid_brush_create, &api_pen_create, &api_pen_set_width,
    &api_pen_set_dash_style, &api_pen_set_dash_pattern,
    &api_font_create, &api_string_format_create, &api_string_format_set,
    &api_recorder_create, &api_recorder_save, &api_recorder_restore,
    &api_recorder_translate, &api_recorder_set_transform,
    &api_recorder_set_clip, &api_recorder_reset_clip,
    &api_recorder_set_quality, &api_recorder_is_visible,
    &api_recorder_clear, &api_recorder_fill_rectangle,
    &api_recorder_draw_rectangle, &api_recorder_draw_line,
    &api_recorder_draw_string, &api_recorder_close,
    &api_recorder_command_count, &api_recorder_trace,
    &api_graphics_path_create, &api_graphics_path_reset,
    &api_graphics_path_start_figure, &api_graphics_path_close_figure,
    &api_graphics_path_add_line, &api_graphics_path_add_rectangle,
    &api_graphics_path_add_ellipse, &api_graphics_path_bounds,
    &api_image_reference_create, &api_image_attributes_create,
    &api_image_attributes_set_color_matrix, &api_image_attributes_reset,
    &api_recorder_draw_ellipse, &api_recorder_fill_ellipse,
    &api_recorder_fill_polygon, &api_recorder_draw_path,
    &api_recorder_fill_path, &api_recorder_draw_image,
    &api_bitmap_create, &api_bitmap_dimensions, &api_bitmap_get_pixel,
    &api_bitmap_set_pixel, &api_bitmap_lock, &api_bitmap_unlock,
    &api_bitmap_clone, &api_bitmap_make_transparent,
    &api_bitmap_thumbnail, &api_bitmap_adjusted,
    &api_recorder_draw_bitmap,
    &api_graphics_path_add_arc, &api_graphics_path_add_path,
    &api_graphics_path_transform, &api_graphics_path_is_visible,
    &api_graphics_path_points, &api_graphics_path_clone,
    &api_image_attributes_set_remap_table,
    &api_image_attributes_reset_remap_table,
    &api_hatch_brush_create, &api_linear_gradient_brush_create,
    &api_linear_gradient_set_blend,
    &api_linear_gradient_set_interpolation,
    &api_linear_gradient_set_wrap_mode,
    &api_path_gradient_brush_create,
    &api_path_gradient_set_center_color,
    &api_path_gradient_set_center_point,
    &api_path_gradient_set_surround_colors,
    &api_path_gradient_set_interpolation,
    &api_region_create_rectangle, &api_region_create_path,
    &api_region_union_rectangle, &api_region_union_path,
    &api_region_exclude_rectangle, &api_region_is_visible,
    &api_region_bounds,
    &api_raster_service_install, &api_recorder_execute,
    &api_bitmap_encode_png, &api_bitmap_decode_png,
    &api_image_attributes_clone,
    &api_bitmap_export_hbitmap, &api_bitmap_import_hbitmap,
    &api_native_surface_capture, &api_native_surface_present,
    &api_bitmap_acquire_hdc, &api_bitmap_release_hdc,
    &api_measure_string,
    &api_recorder_execute_from,
    &api_bitmap_edit_begin, &api_bitmap_edit_commit,
    &api_bitmap_edit_cancel, &api_bitmap_changes_since,
    &api_native_surface_refresh,
    &api_native_surface_publish_retained,
    &api_font_resolve,
    &api_pen_set_line_join, &api_pen_set_line_cap,
    &api_recorder_draw_lines, &api_recorder_fill_rectangles,
    &api_recorder_draw_line_segments, &api_recorder_draw_markers,
    &api_conic_gradient_brush_create, &api_conic_gradient_set_interpolation,
    &api_checker_brush_create,
    &api_recorder_fill_grid_pattern, &api_recorder_fill_proportion,
    &api_recorder_draw_string_over_fill,
    &api_contrast_ratio, &api_contrasting_text_color,
    &api_graphics_path_add_rounded_rectangle,
    &api_recorder_fill_band, &api_recorder_draw_polyline_runs,
    &api_recorder_fill_rectangles_each,
    &api_recorder_draw_string_in_rect, &api_recorder_draw_strings,
    &api_recorder_reset, &api_measure_string_fitted,
    &api_graphics_path_add_quadratic, &api_graphics_path_add_bezier,
    &api_graphics_path_add_beziers, &api_graphics_path_add_polygon,
    &api_graphics_path_add_pie,
    &api_graphics_path_set_fill_mode, &api_graphics_path_fill_mode,
    &api_recorder_draw_string_rotated,
    &api_recorder_paint_target,
    // The <kind>_gradient_brush_set_* aliases: the 0.x setters' functions.
    &api_linear_gradient_set_blend, &api_linear_gradient_set_interpolation,
    &api_linear_gradient_set_wrap_mode,
    &api_path_gradient_set_center_color, &api_path_gradient_set_center_point,
    &api_path_gradient_set_surround_colors, &api_path_gradient_set_interpolation,
    &api_conic_gradient_set_interpolation,
    // 1.1, provisional.
    &api_graphics_path_flatten,
    &api_raster_initialize,
    &api_texture_brush_create, &api_texture_brush_set_wrap_mode,
    &api_texture_brush_set_transform,
    &api_radial_gradient_brush_create, &api_radial_gradient_brush_set_interpolation,
    &api_radial_gradient_brush_set_focus,
    &api_radial_gradient_brush_set_radii,
    &api_pen_set_trim,
};

} // namespace

extern "C" GD_C_API_EXPORT gd_result gd_get_api_v0(
    std::uint32_t requested_version, gd_api_v0* table) {
    if (table == nullptr || (*table).struct_size < sizeof(std::uint32_t) * 2U) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "drawing ABI negotiation requires a size-prefixed table");
    }
    if (requested_version != GD_ABI_VERSION_0_1 &&
        requested_version != GD_ABI_VERSION_0_2 &&
        requested_version != GD_ABI_VERSION_0_3 &&
        requested_version != GD_ABI_VERSION_0_4 &&
        requested_version != GD_ABI_VERSION_0_5 &&
        requested_version != GD_ABI_VERSION_0_6 &&
        requested_version != GD_ABI_VERSION_0_7 &&
        requested_version != GD_ABI_VERSION_1_0 &&
        requested_version != GD_ABI_VERSION_1_1) {
        return fail(GD_ERROR_UNSUPPORTED_VERSION,
                    "requested GUI.Drawing ABI version is unsupported");
    }
    const std::uint32_t caller_size = (*table).struct_size;
    // Each version's table is the prefix it introduced.
    std::size_t available = sizeof(api_table);
    if (requested_version == GD_ABI_VERSION_0_1) {
        available = offsetof(gd_api_v0, bitmap_edit_begin);
    } else if (requested_version == GD_ABI_VERSION_0_2) {
        available = offsetof(gd_api_v0, pen_set_line_join);
    } else if (requested_version == GD_ABI_VERSION_0_3) {
        available = offsetof(gd_api_v0, recorder_fill_band);
    } else if (requested_version == GD_ABI_VERSION_0_4) {
        available = offsetof(gd_api_v0, recorder_draw_string_in_rect);
    } else if (requested_version == GD_ABI_VERSION_0_5) {
        available = offsetof(gd_api_v0, graphics_path_add_quadratic);
    } else if (requested_version == GD_ABI_VERSION_0_6) {
        available = offsetof(gd_api_v0, recorder_draw_string_rotated);
    } else if (requested_version == GD_ABI_VERSION_0_7) {
        available = offsetof(gd_api_v0, recorder_paint_target);
    } else if (requested_version == GD_ABI_VERSION_1_0) {
        available = offsetof(gd_api_v0, graphics_path_flatten);
    }
    const std::size_t copied = std::min<std::size_t>(caller_size, available);
    std::memcpy(table, &api_table, copied);
    (*table).struct_size = static_cast<std::uint32_t>(available);
    (*table).abi_version = requested_version;
    return GD_OK;
}
