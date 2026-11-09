#include "gui_forms/drawing_c_api.h"

#include "gui_forms/drawing.hpp"
#include "gui_forms/drawing/font_substitution/font_substitution.hpp"
#include "drawing_platform.hpp"
#include "font_resolution_record.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

thread_local gd_result last_error_code = GD_OK;
thread_local std::string last_error_message;

gd_result fail(gd_result result, std::string message) noexcept {
    last_error_code = result;
    try {
        last_error_message = std::move(message);
    } catch (...) {
        last_error_message.clear();
    }
    return result;
}

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
    } catch (const std::length_error& error) {
        return fail(GD_ERROR_LIMIT_EXCEEDED, error.what());
    } catch (const std::invalid_argument& error) {
        return fail(GD_ERROR_INVALID_ARGUMENT, error.what());
    } catch (const std::logic_error& error) {
        return fail(GD_ERROR_INVALID_ARGUMENT, error.what());
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

    gd_result retain(gd_handle handle) {
        std::scoped_lock lock(mutex_);
        Slot* slot = find_locked(handle);
        if (slot == nullptr) return stale("retain");
        std::scoped_lock operation_lock((*(*slot).record).operation_mutex);
        if ((*(*slot).record).owner_thread != std::this_thread::get_id()) {
            (*(*(*slot).record).object).handoff_to_current_thread();
            (*(*slot).record).owner_thread = std::this_thread::get_id();
        }
        if ((*(*slot).record).owner_thread != std::this_thread::get_id()) {
            return wrong_thread("retain");
        }
        if ((*(*slot).record).external_references ==
            std::numeric_limits<std::uint64_t>::max()) {
            return fail(GD_ERROR_LIMIT_EXCEEDED, "drawing reference count overflow");
        }
        ++(*(*slot).record).external_references;
        return GD_OK;
    }

    gd_result release(gd_handle handle) {
        std::scoped_lock lock(mutex_);
        Slot* slot = find_locked(handle);
        if (slot == nullptr) return stale("release");
        // Recycling removes the registry reference. Keep the record and its
        // mutex alive until the operation lock has released it.
        const std::shared_ptr<ObjectRecord> record = (*slot).record;
        const std::scoped_lock<std::recursive_mutex> operation_lock((*record).operation_mutex);
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
            recycle_locked(*slot);
        }
        return GD_OK;
    }

    gd_result dispose(gd_handle handle) {
        std::shared_ptr<ObjectRecord> record;
        if (const gd_result result = get(handle, 0U, record, true); result != GD_OK) {
            return result;
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
        *state = static_cast<std::uint32_t>((*(*record).object).state());
        return GD_OK;
    }

    gd_result object_kind(gd_handle handle, std::uint32_t* kind) {
        if (kind == nullptr) {
            return fail(GD_ERROR_INVALID_ARGUMENT, "object_kind requires an output");
        }
        std::shared_ptr<ObjectRecord> record;
        if (const gd_result result = get(handle, 0U, record, true); result != GD_OK) {
            return result;
        }
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

    static gd_result stale(std::string_view operation) {
        return fail(GD_ERROR_STALE_HANDLE,
                    std::string(operation) + " received a stale drawing handle");
    }

    static gd_result wrong_thread(std::string_view operation) {
        return fail(GD_ERROR_WRONG_THREAD,
                    std::string(operation) + " must run on the drawing owner thread");
    }

    std::mutex mutex_;
    std::vector<Slot> slots_;
};

Registry& registry() {
    static Registry value;
    return value;
}

std::mutex raster_service_mutex;
gd_raster_service_v0 raster_service{};


[[nodiscard]] bool load_raster_service(gd_raster_service_v0& output) {
    std::scoped_lock lock(raster_service_mutex);
    if (raster_service.abi_version == 0U) return false;
    output = raster_service;
    return true;
}

gd_result verify_bitmap_is_not_leased(gui_drawing::Bitmap& bitmap) {
    return gui_drawing::abi::platform::has_hdc_lease(bitmap) ?
        fail(GD_ERROR_INVALID_ARGUMENT,
             "bitmap cannot be disposed during an HDC lease") : GD_OK;
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
                                 std::uintptr_t source, std::uint32_t kind,
                                 gd_rect* refreshed_bounds) {
    gui_drawing::RectF bounds;
    const gd_result result = gui_drawing::abi::platform::refresh_surface(
        source, kind, bitmap, bounds);
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
    (*error).code = static_cast<std::uint32_t>(last_error_code);
    (*error).message = {last_error_message.data(), last_error_message.size()};
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
        const gd_result lease_check = registry().with<gui_drawing::Bitmap>(
            handle, GD_OBJECT_BITMAP, &verify_bitmap_is_not_leased);
        if (lease_check != GD_OK) return lease_check;
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
    if ((count != 0U && entries == nullptr) ||
        count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "dash pattern is not a bounded array");
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
    if (points == nullptr || count < 3U ||
        count > gui_drawing::GraphicsPath::maximum_elements || fill_mode > 1U) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "polygon requires bounded points and a declared fill mode");
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
    if ((count != 0U && entries == nullptr) || count > 4096U) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "image remap table is not bounded");
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
    if (count < 2U || count > 4096U || factors == nullptr || positions == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "gradient blend arrays are not bounded");
    }
    return registry().with<gui_drawing::LinearGradientBrush>(
        handle, GD_OBJECT_LINEAR_GRADIENT_BRUSH,
        &set_linear_gradient_blend, factors, positions, count);
}

[[nodiscard]] gd_result set_gradient_interpolation(
    gd_handle handle, std::uint32_t kind, const gd_color* colors,
    const double* positions, std::uint64_t count) {
    if (count < 2U || count > 4096U || colors == nullptr || positions == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "gradient interpolation arrays are not bounded");
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
    if (points == nullptr || count < 3U ||
        count > gui_drawing::GraphicsPath::maximum_elements ||
        wrap_mode > GD_WRAP_CLAMP) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "path gradient inputs are not bounded");
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
    if (count == 0U || count > gui_drawing::GraphicsPath::maximum_elements ||
        colors == nullptr) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "surround color array is not bounded");
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
    std::scoped_lock lock(raster_service_mutex);
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
        return fail(GD_ERROR_INTERNAL, "GUI.Drawing raster service is not installed");
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
        return fail(GD_ERROR_INTERNAL, "GUI.Drawing raster service is not installed");
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
        return fail(GD_ERROR_INTERNAL, "GUI.Drawing raster service is not installed");
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
        return fail(GD_ERROR_INTERNAL, "GUI.Drawing raster service is not installed");
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
    return registry().with<gui_drawing::Bitmap>(
        handle, GD_OBJECT_BITMAP, &refresh_native_surface, source, kind,
        refreshed_bounds);
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

// A caller array of `count` elements, the batch bounds checked before any
// element is read.
[[nodiscard]] bool bounded_batch(const void* elements, std::uint64_t count, std::uint64_t minimum) noexcept {
    return elements != nullptr && count >= minimum &&
           count <= gui_drawing::GraphicsRecorder::maximum_batch_elements;
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
    if (!bounded_batch(points, count, 2U)) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "polyline requires 2 or more bounded points");
    }
    return translate(&draw_recorder_lines_from_c, recorder, pen, points, count, false);
}

gd_result api_recorder_draw_line_segments(gd_handle recorder, gd_handle pen, const gd_point* points,
                                          std::uint64_t count) {
    if (!bounded_batch(points, count, 2U) || count % 2U != 0U) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "line segments require bounded points in pairs");
    }
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
    if (!bounded_batch(rects, count, 1U)) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "fill rectangles requires 1 or more bounded rectangles");
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
    if (!bounded_batch(centers, count, 1U) || style == nullptr || (*style).shape > GD_MARKER_ASTERISK) {
        return fail(GD_ERROR_INVALID_ARGUMENT, "markers require bounded centres and a declared shape");
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

const gd_api_v0 api_table{
    sizeof(gd_api_v0), GD_ABI_VERSION_0_2,
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
};

} // namespace

extern "C" GD_C_API_EXPORT gd_result gd_get_api_v0(
    std::uint32_t requested_version, gd_api_v0* table) {
    if (table == nullptr || (*table).struct_size < sizeof(std::uint32_t) * 2U) {
        return fail(GD_ERROR_INVALID_ARGUMENT,
                    "drawing ABI negotiation requires a size-prefixed table");
    }
    if (requested_version != GD_ABI_VERSION_0_1 &&
        requested_version != GD_ABI_VERSION_0_2) {
        return fail(GD_ERROR_UNSUPPORTED_VERSION,
                    "requested GUI.Drawing ABI version is unsupported");
    }
    const std::uint32_t caller_size = (*table).struct_size;
    const std::size_t available = requested_version == GD_ABI_VERSION_0_1
        ? offsetof(gd_api_v0, bitmap_edit_begin) : sizeof(api_table);
    const std::size_t copied = std::min<std::size_t>(caller_size, available);
    std::memcpy(table, &api_table, copied);
    (*table).struct_size = static_cast<std::uint32_t>(available);
    (*table).abi_version = requested_version;
    return GD_OK;
}
