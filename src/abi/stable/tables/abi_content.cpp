// GF_TABLE_CONTENT (since 1.4): the content boundary over the C++
// ContentSurface, ContentTextEditor, ContentFrameSink and hosted content
// (<gui_forms/content.hpp>; docs/user/content-surfaces.md). See gf_abi.h.
//
// The sinks are the one part of this table any thread calls: a process-wide
// table of handles, each holding a copy of a ContentFrameSink (which shares
// its channel's state, never the surface). The table is created on first
// use and never destroyed, so an abandoned content thread that calls a sink
// slot while the process exits finds it still there.

#include "abi_control_entry.hpp"
#include "abi_tables.hpp"

#include "gui_forms/content/editing/content_text_layout.hpp"
#include "gui_forms/content/frame_sink/content_frame_sink.hpp"
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
#include "gui_forms/content/hosted/hosted_content.hpp"
#endif
#include "gui_forms/content/operation/content_operation.hpp"
#include "gui_forms/content/types/content_types.hpp"
#include "gui_forms/content/watchdog/content_watchdog.hpp"
#include "gui_forms/controls/content_surface.hpp"
#include "gui_forms/controls/content_text_editor.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity open_content_slot{GF_TABLE_CONTENT, 0U};
constexpr SlotIdentity close_content_slot{GF_TABLE_CONTENT, 1U};
constexpr SlotIdentity start_program_slot{GF_TABLE_CONTENT, 2U};
constexpr SlotIdentity start_hosted_slot{GF_TABLE_CONTENT, 3U};
constexpr SlotIdentity terminate_slot{GF_TABLE_CONTENT, 4U};
constexpr SlotIdentity set_termination_grace_slot{GF_TABLE_CONTENT, 5U};
constexpr SlotIdentity set_stale_interval_slot{GF_TABLE_CONTENT, 6U};
constexpr SlotIdentity status_slot{GF_TABLE_CONTENT, 7U};
constexpr SlotIdentity failure_slot{GF_TABLE_CONTENT, 8U};
constexpr SlotIdentity statistics_slot{GF_TABLE_CONTENT, 9U};
constexpr SlotIdentity request_slot{GF_TABLE_CONTENT, 10U};
constexpr SlotIdentity set_status_caption_slot{GF_TABLE_CONTENT, 11U};
constexpr SlotIdentity set_semantics_slot{GF_TABLE_CONTENT, 12U};
constexpr SlotIdentity program_running_slot{GF_TABLE_CONTENT, 13U};
constexpr SlotIdentity hosted_input_slot{GF_TABLE_CONTENT, 14U};
constexpr SlotIdentity hosted_support_slot{GF_TABLE_CONTENT, 15U};
constexpr SlotIdentity sink_publish_slot{GF_TABLE_CONTENT, 16U};
constexpr SlotIdentity sink_heartbeat_slot{GF_TABLE_CONTENT, 17U};
constexpr SlotIdentity sink_publish_semantics_slot{GF_TABLE_CONTENT, 18U};
constexpr SlotIdentity sink_request_slot{GF_TABLE_CONTENT, 19U};
constexpr SlotIdentity sink_wait_for_request_slot{GF_TABLE_CONTENT, 20U};
constexpr SlotIdentity sink_state_slot{GF_TABLE_CONTENT, 21U};
constexpr SlotIdentity sink_statistics_slot{GF_TABLE_CONTENT, 22U};
constexpr SlotIdentity sink_release_slot{GF_TABLE_CONTENT, 23U};
constexpr SlotIdentity program_sink_slot{GF_TABLE_CONTENT, 24U};
constexpr SlotIdentity program_fail_slot{GF_TABLE_CONTENT, 25U};
constexpr SlotIdentity editor_text_revision_slot{GF_TABLE_CONTENT, 26U};
constexpr SlotIdentity editor_text_slot{GF_TABLE_CONTENT, 27U};
constexpr SlotIdentity editor_set_text_slot{GF_TABLE_CONTENT, 28U};
constexpr SlotIdentity editor_submit_layout_slot{GF_TABLE_CONTENT, 29U};
constexpr SlotIdentity editor_layout_current_slot{GF_TABLE_CONTENT, 30U};
constexpr SlotIdentity editor_open_frames_slot{GF_TABLE_CONTENT, 31U};
constexpr SlotIdentity editor_caret_rectangle_slot{GF_TABLE_CONTENT, 32U};
constexpr SlotIdentity editor_selection_rectangles_slot{GF_TABLE_CONTENT, 33U};
constexpr SlotIdentity editor_position_at_slot{GF_TABLE_CONTENT, 34U};
constexpr SlotIdentity editor_set_draws_selection_slot{GF_TABLE_CONTENT, 35U};
constexpr SlotIdentity editor_set_multiline_slot{GF_TABLE_CONTENT, 36U};
constexpr SlotIdentity editor_set_read_only_slot{GF_TABLE_CONTENT, 37U};
// Provisional slots (gf_abi.h: 1.5 while it is unreleased, else 1.6).
constexpr SlotIdentity editor_set_accepts_tab_slot{GF_TABLE_CONTENT, 38U};
constexpr SlotIdentity editor_replace_selection_slot{GF_TABLE_CONTENT, 39U};
constexpr SlotIdentity editor_delete_selection_slot{GF_TABLE_CONTENT, 40U};
constexpr SlotIdentity editor_composition_range_slot{GF_TABLE_CONTENT, 41U};
constexpr SlotIdentity editor_caret_quad_slot{GF_TABLE_CONTENT, 42U};
constexpr SlotIdentity editor_selection_quads_slot{GF_TABLE_CONTENT, 43U};
constexpr SlotIdentity editor_composition_quads_slot{GF_TABLE_CONTENT, 44U};

// The 1.4 sizes of the records this table reads and writes.
constexpr std::uint32_t frame_size_1_4 = 64U;
constexpr std::uint32_t request_size_1_4 = 32U;
constexpr std::uint32_t semantics_size_1_4 = 56U;
constexpr std::uint32_t statistics_size_1_4 = 80U;
constexpr std::uint32_t program_size_1_4 = 40U;
constexpr std::uint32_t hosted_options_size_1_4 = 64U;
constexpr std::uint32_t hosted_input_report_size_1_4 = 16U;
constexpr std::uint32_t text_layout_size_1_4 = 64U;
static_assert(sizeof(gf_content_frame) == frame_size_1_4);
static_assert(sizeof(gf_content_request) == request_size_1_4);
static_assert(sizeof(gf_content_semantics) == semantics_size_1_4);
static_assert(sizeof(gf_content_statistics) == statistics_size_1_4);
static_assert(sizeof(gf_content_program) == program_size_1_4);
static_assert(sizeof(gf_hosted_options) == hosted_options_size_1_4);
static_assert(sizeof(gf_hosted_input_report) == hosted_input_report_size_1_4);
// The layout's provisional transform follows its 1.4 fields; a record of
// at least this size carries it.
constexpr std::uint32_t text_layout_size_transformed = 120U;
static_assert(offsetof(gf_content_text_layout, transformed) == text_layout_size_1_4);
static_assert(sizeof(gf_content_text_layout) == text_layout_size_transformed);
static_assert(GF_CONTENT_MAXIMUM_FRAME_EXTENT == maximum_content_frame_extent);
static_assert(GF_CONTENT_MAXIMUM_FRAME_BYTES == maximum_content_frame_bytes);
static_assert(GF_CONTENT_MAXIMUM_SEMANTIC_TEXT_BYTES == maximum_content_semantic_text_bytes);
static_assert(GF_CONTENT_MAXIMUM_LAYOUT_ENTRIES == maximum_content_layout_entries);
static_assert(GF_CONTENT_MAXIMUM_TERMINATION_GRACE_MS == maximum_content_termination_grace.count());

// ---------------------------------------------------------------------------
// Sinks.

// The handles the library has given out. Any thread; every member takes the
// lock for a lookup or one map change only.
class SinkTable final {
public:
    SinkTable() = default;
    SinkTable(const SinkTable&) = delete;
    SinkTable& operator=(const SinkTable&) = delete;

    // A new handle to `sink`; zero when GF_CONTENT_MAXIMUM_SINKS are held.
    // Throws std::bad_alloc (nothing changes then).
    [[nodiscard]] gf_content_sink add(ContentFrameSink sink) {
        const std::scoped_lock lock(mutex_);
        if (sinks_.size() >= GF_CONTENT_MAXIMUM_SINKS) return 0U;
        const gf_content_sink handle = next_;
        sinks_.emplace(handle, std::move(sink));
        ++next_;
        return handle;
    }

    // A copy of the sink behind `handle`; none for zero, a released or an
    // unknown handle.
    [[nodiscard]] std::optional<ContentFrameSink> find(gf_content_sink handle) {
        const std::scoped_lock lock(mutex_);
        const std::unordered_map<gf_content_sink, ContentFrameSink>::const_iterator found = sinks_.find(handle);
        if (found == sinks_.end()) return std::nullopt;
        std::optional<ContentFrameSink> copy((*found).second);
        return copy;
    }

    // Ends `handle`; false when it is not held. The sink's last copy, if
    // this is it, is destroyed after the lock is let go.
    [[nodiscard]] bool remove(gf_content_sink handle) {
        ContentFrameSink released{};
        {
            const std::scoped_lock lock(mutex_);
            const std::unordered_map<gf_content_sink, ContentFrameSink>::iterator found = sinks_.find(handle);
            if (found == sinks_.end()) return false;
            released = std::move((*found).second);
            sinks_.erase(found);
        }
        return true;
    }

private:
    std::mutex mutex_{};
    std::unordered_map<gf_content_sink, ContentFrameSink> sinks_{};
    // Handles are never reused: 2^64 hand-outs do not happen.
    gf_content_sink next_{1U};
};

[[nodiscard]] SinkTable& sinks() {
    // Never destroyed (the file comment).
    static SinkTable* const table = new SinkTable();
    return *table;
}

// Gives the caller a handle to `sink`; GF_STATUS_LIMIT_EXCEEDED when the
// table is full (the channel stays open; the caller may release others).
gf_status hand_out(ContentFrameSink sink, gf_content_sink* handle, SlotIdentity slot) {
    const gf_content_sink made = sinks().add(std::move(sink));
    if (made == 0U) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "GF_CONTENT_MAXIMUM_SINKS sinks are held; release one first");
    }
    *handle = made;
    return GF_STATUS_OK;
}

gf_status find_sink(gf_content_sink handle, SlotIdentity slot, ContentFrameSink& sink) {
    std::optional<ContentFrameSink> found = sinks().find(handle);
    if (!found) return fail(GF_STATUS_STALE_HANDLE, slot, "the sink was released or never given out");
    sink = std::move(*found);
    return GF_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Values.

// One byte of a UTF-8 path read through char, as char8_t (same value).
struct CharToChar8 final {
    [[nodiscard]] char8_t operator()(char byte) const noexcept { return static_cast<char8_t>(byte); }
};

gf_content_status status_value(ContentSurfaceStatus status) noexcept {
    switch (status) {
    case ContentSurfaceStatus::waiting: return GF_CONTENT_STATUS_WAITING;
    case ContentSurfaceStatus::live: return GF_CONTENT_STATUS_LIVE;
    case ContentSurfaceStatus::stale: return GF_CONTENT_STATUS_STALE;
    case ContentSurfaceStatus::terminating: return GF_CONTENT_STATUS_TERMINATING;
    case ContentSurfaceStatus::ended: return GF_CONTENT_STATUS_ENDED;
    case ContentSurfaceStatus::abandoned: return GF_CONTENT_STATUS_ABANDONED;
    case ContentSurfaceStatus::failed: return GF_CONTENT_STATUS_FAILED;
    case ContentSurfaceStatus::disconnected: return GF_CONTENT_STATUS_DISCONNECTED;
    default:
        // A status this table does not know reads as no content.
        return GF_CONTENT_STATUS_DISCONNECTED;
    }
}

// The C++ status of a C one; none for a value out of range.
std::optional<ContentSurfaceStatus> status_of(gf_content_status status) noexcept {
    switch (status) {
    case GF_CONTENT_STATUS_DISCONNECTED: return ContentSurfaceStatus::disconnected;
    case GF_CONTENT_STATUS_WAITING: return ContentSurfaceStatus::waiting;
    case GF_CONTENT_STATUS_LIVE: return ContentSurfaceStatus::live;
    case GF_CONTENT_STATUS_STALE: return ContentSurfaceStatus::stale;
    case GF_CONTENT_STATUS_TERMINATING: return ContentSurfaceStatus::terminating;
    case GF_CONTENT_STATUS_ENDED: return ContentSurfaceStatus::ended;
    case GF_CONTENT_STATUS_ABANDONED: return ContentSurfaceStatus::abandoned;
    case GF_CONTENT_STATUS_FAILED: return ContentSurfaceStatus::failed;
    default: return std::nullopt;
    }
}

gf_content_publish_result publish_value(ContentPublishResult result) noexcept {
    switch (result) {
    case ContentPublishResult::published: return GF_CONTENT_PUBLISH_PUBLISHED;
    case ContentPublishResult::dropped_busy: return GF_CONTENT_PUBLISH_DROPPED_BUSY;
    case ContentPublishResult::refused_empty: return GF_CONTENT_PUBLISH_REFUSED_EMPTY;
    case ContentPublishResult::refused_too_large: return GF_CONTENT_PUBLISH_REFUSED_TOO_LARGE;
    case ContentPublishResult::refused_row_bytes: return GF_CONTENT_PUBLISH_REFUSED_ROW_BYTES;
    case ContentPublishResult::refused_extent: return GF_CONTENT_PUBLISH_REFUSED_EXTENT;
    case ContentPublishResult::refused_format: return GF_CONTENT_PUBLISH_REFUSED_FORMAT;
    case ContentPublishResult::refused_damage: return GF_CONTENT_PUBLISH_REFUSED_DAMAGE;
    case ContentPublishResult::failed_allocation: return GF_CONTENT_PUBLISH_FAILED_ALLOCATION;
    case ContentPublishResult::revoked: return GF_CONTENT_PUBLISH_REVOKED;
    default:
        // A result this table does not know: nothing was shown.
        return GF_CONTENT_PUBLISH_REVOKED;
    }
}

gf_content_semantics_result semantics_value(ContentSemanticsResult result) noexcept {
    switch (result) {
    case ContentSemanticsResult::published: return GF_CONTENT_SEMANTICS_PUBLISHED;
    case ContentSemanticsResult::refused_invalid_utf8: return GF_CONTENT_SEMANTICS_REFUSED_INVALID_UTF8;
    case ContentSemanticsResult::refused_too_long: return GF_CONTENT_SEMANTICS_REFUSED_TOO_LONG;
    case ContentSemanticsResult::revoked: return GF_CONTENT_SEMANTICS_REVOKED;
    default: return GF_CONTENT_SEMANTICS_REVOKED;
    }
}

gf_content_layout_result layout_value(ContentLayoutResult result) noexcept {
    switch (result) {
    case ContentLayoutResult::accepted: return GF_CONTENT_LAYOUT_ACCEPTED;
    case ContentLayoutResult::stale_revision: return GF_CONTENT_LAYOUT_STALE_REVISION;
    case ContentLayoutResult::refused_too_large: return GF_CONTENT_LAYOUT_REFUSED_TOO_LARGE;
    case ContentLayoutResult::refused_range: return GF_CONTENT_LAYOUT_REFUSED_RANGE;
    case ContentLayoutResult::refused_boundary: return GF_CONTENT_LAYOUT_REFUSED_BOUNDARY;
    case ContentLayoutResult::refused_cluster: return GF_CONTENT_LAYOUT_REFUSED_CLUSTER;
    case ContentLayoutResult::refused_geometry: return GF_CONTENT_LAYOUT_REFUSED_GEOMETRY;
    case ContentLayoutResult::refused_caret: return GF_CONTENT_LAYOUT_REFUSED_CARET;
    default:
        // A refusal this table does not know reads as the general one.
        return GF_CONTENT_LAYOUT_REFUSED_RANGE;
    }
}

void write_statistics(const ContentSurfaceStatistics& source, gf_content_statistics& target) noexcept {
    target.last_refusal = publish_value(source.last_refusal);
    target.frames_published = source.frames_published;
    target.frames_refused = source.frames_refused;
    target.frames_dropped = source.frames_dropped;
    target.heartbeats = source.heartbeats;
    target.stale_transitions = source.stale_transitions;
    target.recoveries = source.recoveries;
    target.semantics_published = source.semantics_published;
    target.semantics_refused = source.semantics_refused;
    target.after_revocation = source.after_revocation;
}

void write_request(const ContentRequest& source, gf_content_request& target) noexcept {
    target.width = source.size.width;
    target.height = source.size.height;
    target.reserved = 0U;
    target.scale = source.scale;
    target.generation = source.generation;
}

gf_content_quad quad_value(const ContentQuad& quad) noexcept {
    gf_content_quad value{};
    value.top_left = gf_logical_point{quad.corners[0].x, quad.corners[0].y};
    value.top_right = gf_logical_point{quad.corners[1].x, quad.corners[1].y};
    value.bottom_right = gf_logical_point{quad.corners[2].x, quad.corners[2].y};
    value.bottom_left = gf_logical_point{quad.corners[3].x, quad.corners[3].y};
    return value;
}

gf_logical_rect rect_value(const Rect& rect) noexcept {
    const gf_logical_rect value{rect.x, rect.y, rect.width, rect.height};
    return value;
}

Rect rect_of(const gf_logical_rect& rect) noexcept {
    const Rect value{rect.x, rect.y, rect.width, rect.height};
    return value;
}

// Borrowed bytes as text without judging them: data may be null only when
// size is zero. The C++ side validates the UTF-8 and reports a refusal as
// its result rather than as a failed call.
gf_status borrowed_text(gf_utf8_view view, SlotIdentity slot, std::string& text) {
    if (view.data == nullptr && view.size != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a text view with no data has size zero");
    }
    if (view.size > GF_CONTENT_MAXIMUM_SEMANTIC_TEXT_BYTES + 1U) {
        // Longer than any accepted text; the C++ check would refuse it too,
        // after a copy this keeps from being made.
        text.assign(GF_CONTENT_MAXIMUM_SEMANTIC_TEXT_BYTES + 1U, ' ');
        return GF_STATUS_OK;
    }
    text.assign(view.data == nullptr ? "" : view.data, static_cast<std::size_t>(view.size));
    return GF_STATUS_OK;
}

gf_status read_semantics(const gf_content_semantics* semantics, SlotIdentity slot, ContentSemantics& out) {
    if (semantics == nullptr || (*semantics).struct_size < semantics_size_1_4 || (*semantics).reserved != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a gf_content_semantics needs struct_size of at least 56 and reserved zero");
    }
    const gf_status name = borrowed_text((*semantics).name, slot, out.name);
    if (name != GF_STATUS_OK) return name;
    const gf_status text = borrowed_text((*semantics).text, slot, out.text);
    if (text != GF_STATUS_OK) return text;
    const gf_status description = borrowed_text((*semantics).description, slot, out.description);
    return description;
}

// A time in milliseconds from the C side; GF_STATUS_INVALID_ARGUMENT over
// GF_CONTENT_MAXIMUM_WAIT_MS.
gf_status read_milliseconds(std::uint64_t value, SlotIdentity slot, std::chrono::milliseconds& out) {
    if (value > GF_CONTENT_MAXIMUM_WAIT_MS) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a time is at most GF_CONTENT_MAXIMUM_WAIT_MS milliseconds");
    }
    out = std::chrono::milliseconds(static_cast<std::int64_t>(value));
    return GF_STATUS_OK;
}

gf_status read_flag(std::uint32_t value, SlotIdentity slot, bool& out) {
    if (value > 1U) return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a flag is 0 or 1");
    out = value == 1U;
    return GF_STATUS_OK;
}

gf_status enter_surface(gf_object surface, SlotIdentity slot, std::shared_ptr<ContentSurface>& found) {
    const gf_status entered = enter_typed_control<ContentSurface>(surface, slot, "the object is not a content_surface",
                                                                   found);
    return entered;
}

gf_status enter_editor(gf_object editor, SlotIdentity slot, std::shared_ptr<ContentTextEditor>& found) {
    const gf_status entered =
        enter_typed_control<ContentTextEditor>(editor, slot, "the object is not a content_text_editor", found);
    return entered;
}

// ---------------------------------------------------------------------------
// Programs.

class AbiContentProgram;

// The program whose run callback runs on this thread now, and its context;
// null outside a run.
thread_local AbiContentProgram* running_program = nullptr;
thread_local ContentContext* running_context = nullptr;

// A C caller's gf_content_program run as a C++ ContentProgram, on the
// thread its ContentWorker starts. The worker always calls run once the
// thread exists; a program destroyed without having run (the worker's
// thread was refused) never releases the callback, so the caller keeps it.
class AbiContentProgram final : public ContentProgram {
public:
    explicit AbiContentProgram(const gf_callback& run) : run_(std::make_unique<CallbackBinding>(run)) {}

    ~AbiContentProgram() override {
        if (run_ && !started_) (*run_).disarm();
    }

    AbiContentProgram(const AbiContentProgram&) = delete;
    AbiContentProgram& operator=(const AbiContentProgram&) = delete;

    void run(ContentContext& context) override {
        started_ = true;
        {
            const RunScope scope(*this, context);
            gf_event event{};
            event.struct_size = sizeof(gf_event);
            event.sender = 0U;
            event.name.data = "run";
            event.name.size = 3U;
            (*run_).invoke(event);
        }
        // Released after its one invoke, on this thread.
        run_.reset();
        if (failed_) throw std::runtime_error(failure_);
    }

    // program_fail: the failure run reports when it returns.
    void name_failure(std::string message) {
        failure_ = std::move(message);
        failed_ = true;
    }

private:
    // Marks the program as running on this thread for the callback's
    // duration, whatever the callback does.
    class RunScope final {
    public:
        RunScope(AbiContentProgram& program, ContentContext& context) noexcept
            : previous_program_(running_program), previous_context_(running_context) {
            running_program = &program;
            running_context = &context;
        }
        ~RunScope() {
            running_program = previous_program_;
            running_context = previous_context_;
        }
        RunScope(const RunScope&) = delete;
        RunScope& operator=(const RunScope&) = delete;

    private:
        AbiContentProgram* previous_program_;
        ContentContext* previous_context_;
    };

    std::unique_ptr<CallbackBinding> run_;
    std::string failure_{};
    bool failed_{};
    bool started_{};
};

// ---------------------------------------------------------------------------
// Surface slots.

gf_status content_open_content_body(gf_object surface, std::uint32_t opaque, gf_content_sink* sink) {
    if (sink == nullptr) return fail(GF_STATUS_INVALID_ARGUMENT, open_content_slot, "open_content needs an output");
    bool opaque_value = false;
    const gf_status flag = read_flag(opaque, open_content_slot, opaque_value);
    if (flag != GF_STATUS_OK) return flag;
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, open_content_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    ContentFrameSink opened = (*found).open_content(opaque_value);
    const gf_status handed = hand_out(std::move(opened), sink, open_content_slot);
    return handed;
}

gf_status GF_CALL content_open_content(gf_object surface, std::uint32_t opaque, gf_content_sink* sink) noexcept {
    return call_slot(open_content_slot, content_open_content_body, surface, opaque, sink);
}

gf_status content_close_content_body(gf_object surface) {
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, close_content_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    (*found).close_content();
    return GF_STATUS_OK;
}

gf_status GF_CALL content_close_content(gf_object surface) noexcept {
    return call_slot(close_content_slot, content_close_content_body, surface);
}

gf_status content_start_program_body(gf_object surface, const gf_content_program* program) {
    if (program == nullptr || (*program).struct_size < program_size_1_4 || (*program).opaque > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, start_program_slot,
                    "a gf_content_program needs struct_size of at least 40 and opaque 0 or 1");
    }
    gf_callback run{};
    const gf_status read = read_callback(&(*program).run, start_program_slot, run);
    if (read != GF_STATUS_OK) return read;
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, start_program_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    // A program that never runs (start_program throws) disarms itself:
    // the caller keeps the callback (N20).
    std::unique_ptr<ContentProgram> made = std::make_unique<AbiContentProgram>(run);
    (*found).start_program(std::move(made), (*program).opaque == 1U);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_start_program(gf_object surface, const gf_content_program* program) noexcept {
    return call_slot(start_program_slot, content_start_program_body, surface, program);
}

// Hosted content is not compiled in a high-integrity build
// (cmake/HighIntegrity.cmake): start_hosted and hosted_input are null slots,
// and hosted_support reports GF_HOSTED_SUPPORT_NOT_REPORTED.
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
gf_status content_start_hosted_body(gf_object surface, const gf_hosted_options* options) {
    if (options == nullptr || (*options).struct_size < hosted_options_size_1_4 || (*options).opaque > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, start_hosted_slot,
                    "a gf_hosted_options needs struct_size of at least 64 and opaque 0 or 1");
    }
    if ((*options).arguments == nullptr && (*options).argument_count != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, start_hosted_slot, "arguments with no data have count zero");
    }
    if ((*options).argument_count > GF_HOSTED_MAXIMUM_ARGUMENTS) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, start_hosted_slot,
                    "a hosted program takes at most GF_HOSTED_MAXIMUM_ARGUMENTS arguments");
    }
    if ((*options).slot_bytes > GF_CONTENT_MAXIMUM_FRAME_BYTES) {
        return fail(GF_STATUS_INVALID_ARGUMENT, start_hosted_slot,
                    "slot_bytes is at most GF_CONTENT_MAXIMUM_FRAME_BYTES");
    }
    // Everything is read and checked before the surface changes.
    HostedContentOptions made{};
    std::string_view executable{};
    const gf_status path = read_text((*options).executable, start_hosted_slot, executable);
    if (path != GF_STATUS_OK) return path;
    std::u8string executable_utf8(executable.size(), u8'\0');
    std::transform(executable.begin(), executable.end(), executable_utf8.begin(), CharToChar8{});
    made.executable = std::filesystem::path(executable_utf8);
    const std::span<const gf_utf8_view> arguments((*options).arguments,
                                                  static_cast<std::size_t>((*options).argument_count));
    made.arguments.reserve(arguments.size());
    for (const gf_utf8_view& argument : arguments) {
        std::string_view text{};
        const gf_status read = read_text(argument, start_hosted_slot, text);
        if (read != GF_STATUS_OK) return read;
        made.arguments.emplace_back(text);
    }
    if ((*options).slot_bytes != 0U) made.slot_bytes = (*options).slot_bytes;
    if ((*options).start_timeout_ms != 0U) {
        const gf_status timeout = read_milliseconds((*options).start_timeout_ms, start_hosted_slot,
                                                    made.start_timeout);
        if (timeout != GF_STATUS_OK) return timeout;
    }
    if ((*options).stop_grace_ms != 0U) {
        const gf_status grace = read_milliseconds((*options).stop_grace_ms, start_hosted_slot, made.stop_grace);
        if (grace != GF_STATUS_OK) return grace;
    }
    if (hosted_content_support() != HostedContentSupport::available) {
        return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, start_hosted_slot,
                    "hosted content is not available on this platform (GF_HOST_CAPABILITY_HOSTED_CONTENT)");
    }
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, start_hosted_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    static_cast<void>((*found).start_hosted_content(std::move(made), (*options).opaque == 1U));
    return GF_STATUS_OK;
}

gf_status GF_CALL content_start_hosted(gf_object surface, const gf_hosted_options* options) noexcept {
    return call_slot(start_hosted_slot, content_start_hosted_body, surface, options);
}
#endif

gf_status content_terminate_body(gf_object surface) {
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, terminate_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    (*found).terminate();
    return GF_STATUS_OK;
}

gf_status GF_CALL content_terminate(gf_object surface) noexcept {
    return call_slot(terminate_slot, content_terminate_body, surface);
}

gf_status content_set_termination_grace_body(gf_object surface, std::uint64_t milliseconds) {
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, set_termination_grace_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    // Longer graces are lowered (gf_abi.h), as the C++ setter lowers them.
    const std::uint64_t capped = std::min<std::uint64_t>(milliseconds, GF_CONTENT_MAXIMUM_TERMINATION_GRACE_MS);
    (*found).set_termination_grace(std::chrono::milliseconds(static_cast<std::int64_t>(capped)));
    return GF_STATUS_OK;
}

gf_status GF_CALL content_set_termination_grace(gf_object surface, std::uint64_t milliseconds) noexcept {
    return call_slot(set_termination_grace_slot, content_set_termination_grace_body, surface, milliseconds);
}

gf_status content_set_stale_interval_body(gf_object surface, std::uint64_t milliseconds) {
    std::chrono::milliseconds interval{};
    const gf_status read = read_milliseconds(milliseconds, set_stale_interval_slot, interval);
    if (read != GF_STATUS_OK) return read;
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, set_stale_interval_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    (*found).set_stale_interval(std::chrono::duration_cast<FrameInterval>(interval));
    return GF_STATUS_OK;
}

gf_status GF_CALL content_set_stale_interval(gf_object surface, std::uint64_t milliseconds) noexcept {
    return call_slot(set_stale_interval_slot, content_set_stale_interval_body, surface, milliseconds);
}

gf_status content_status_body(gf_object surface, gf_content_status* status) {
    if (status == nullptr) return fail(GF_STATUS_INVALID_ARGUMENT, status_slot, "status needs an output");
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, status_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    *status = status_value((*found).status());
    return GF_STATUS_OK;
}

gf_status GF_CALL content_status(gf_object surface, gf_content_status* status) noexcept {
    return call_slot(status_slot, content_status_body, surface, status);
}

gf_status content_failure_body(gf_object surface, char* buffer, std::uint64_t capacity,
                               std::uint64_t* required) {
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, failure_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    std::string_view text{};
    if ((*found).status() == ContentSurfaceStatus::failed) text = (*found).failure();
    const gf_status written = write_text(text, buffer, capacity, required, failure_slot);
    return written;
}

gf_status GF_CALL content_failure(gf_object surface, char* buffer, std::uint64_t capacity,
                                  std::uint64_t* required) noexcept {
    return call_slot(failure_slot, content_failure_body, surface, buffer, capacity, required);
}

gf_status content_statistics_body(gf_object surface, gf_content_statistics* statistics) {
    if (statistics == nullptr || (*statistics).struct_size < statistics_size_1_4) {
        return fail(GF_STATUS_INVALID_ARGUMENT, statistics_slot,
                    "a gf_content_statistics needs struct_size of at least 80");
    }
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, statistics_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    write_statistics((*found).statistics(), *statistics);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_statistics(gf_object surface, gf_content_statistics* statistics) noexcept {
    return call_slot(statistics_slot, content_statistics_body, surface, statistics);
}

gf_status content_request_body(gf_object surface, gf_content_request* request) {
    if (request == nullptr || (*request).struct_size < request_size_1_4) {
        return fail(GF_STATUS_INVALID_ARGUMENT, request_slot, "a gf_content_request needs struct_size of at least 32");
    }
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, request_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    write_request((*found).request(), *request);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_request(gf_object surface, gf_content_request* request) noexcept {
    return call_slot(request_slot, content_request_body, surface, request);
}

gf_status content_set_status_caption_body(gf_object surface, gf_content_status status,
                                          gf_utf8_view caption) {
    const std::optional<ContentSurfaceStatus> which = status_of(status);
    if (!which) return fail(GF_STATUS_INVALID_ARGUMENT, set_status_caption_slot, "an unknown gf_content_status");
    std::string_view text{};
    const gf_status read = read_text(caption, set_status_caption_slot, text);
    if (read != GF_STATUS_OK) return read;
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, set_status_caption_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    (*found).set_status_caption(*which, std::string(text));
    return GF_STATUS_OK;
}

gf_status GF_CALL content_set_status_caption(gf_object surface, gf_content_status status,
                                             gf_utf8_view caption) noexcept {
    return call_slot(set_status_caption_slot, content_set_status_caption_body, surface, status, caption);
}

gf_status content_set_semantics_body(gf_object surface, const gf_content_semantics* semantics) {
    ContentSemantics read{};
    const gf_status record = read_semantics(semantics, set_semantics_slot, read);
    if (record != GF_STATUS_OK) return record;
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, set_semantics_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    // std::invalid_argument for text the C++ check refuses.
    (*found).set_content_semantics(std::move(read));
    return GF_STATUS_OK;
}

gf_status GF_CALL content_set_semantics(gf_object surface, const gf_content_semantics* semantics) noexcept {
    return call_slot(set_semantics_slot, content_set_semantics_body, surface, semantics);
}

gf_status content_program_running_body(gf_object surface, std::uint32_t* running) {
    if (running == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, program_running_slot, "program_running needs an output");
    }
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, program_running_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    *running = (*found).program_running() ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL content_program_running(gf_object surface, std::uint32_t* running) noexcept {
    return call_slot(program_running_slot, content_program_running_body, surface, running);
}

#if !defined(GUI_FORMS_HIGH_INTEGRITY)
gf_status content_hosted_input_body(gf_object surface, gf_hosted_input_report* report) {
    if (report == nullptr || (*report).struct_size < hosted_input_report_size_1_4) {
        return fail(GF_STATUS_INVALID_ARGUMENT, hosted_input_slot,
                    "a gf_hosted_input_report needs struct_size of at least 16");
    }
    std::shared_ptr<ContentSurface> found;
    const gf_status entered = enter_surface(surface, hosted_input_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    const HostedInput& input = (*found).hosted_input();
    (*report).connected = input.connected() ? 1U : 0U;
    (*report).dropped = input.dropped();
    return GF_STATUS_OK;
}

gf_status GF_CALL content_hosted_input(gf_object surface, gf_hosted_input_report* report) noexcept {
    return call_slot(hosted_input_slot, content_hosted_input_body, surface, report);
}
#endif

gf_status content_hosted_support_body(gf_hosted_support* support) {
    if (support == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, hosted_support_slot, "hosted_support needs an output");
    }
#if defined(GUI_FORMS_HIGH_INTEGRITY)
    *support = GF_HOSTED_SUPPORT_NOT_REPORTED;
#else
    const bool available = hosted_content_support() == HostedContentSupport::available;
    *support = available ? GF_HOSTED_SUPPORT_AVAILABLE : GF_HOSTED_SUPPORT_NOT_REPORTED;
#endif
    return GF_STATUS_OK;
}

gf_status GF_CALL content_hosted_support(gf_hosted_support* support) noexcept {
    return call_slot(hosted_support_slot, content_hosted_support_body, support);
}

// ---------------------------------------------------------------------------
// Sink slots: any thread.

gf_status content_sink_publish_body(gf_content_sink sink, const gf_content_frame* frame,
                                    gf_content_publish_result* result) {
    if (frame == nullptr || result == nullptr || (*frame).struct_size < frame_size_1_4 ||
        (*frame).reserved != 0U || (*frame).opaque > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, sink_publish_slot,
                    "a gf_content_frame needs struct_size of at least 64, reserved zero, opaque 0 or 1 and a "
                    "result");
    }
    if ((*frame).pixels.data == nullptr && (*frame).pixels.size != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, sink_publish_slot, "pixels with no data have size zero");
    }
    ContentFrameSink found{};
    const gf_status looked_up = find_sink(sink, sink_publish_slot, found);
    if (looked_up != GF_STATUS_OK) return looked_up;
    ContentFrame made{};
    made.width = (*frame).width;
    made.height = (*frame).height;
    made.row_bytes = (*frame).row_bytes;
    // A value past the C++ enumeration's range is held at its top, which
    // names no format either: the C++ check refuses and counts it.
    const std::uint32_t format = std::min<std::uint32_t>((*frame).pixel_format, 0xFFU);
    made.pixel_format = static_cast<LiveSurfacePixelFormat>(format);
    made.opaque = (*frame).opaque == 1U;
    made.damage = ContentPixelRect{(*frame).damage.x, (*frame).damage.y, (*frame).damage.width,
                                   (*frame).damage.height};
    const std::byte* const first = reinterpret_cast<const std::byte*>((*frame).pixels.data);
    made.pixels = std::span<const std::byte>(first, static_cast<std::size_t>((*frame).pixels.size));
    const ContentPublishResult published = found.publish(made);
    *result = publish_value(published);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_sink_publish(gf_content_sink sink, const gf_content_frame* frame,
                                       gf_content_publish_result* result) noexcept {
    return call_slot(sink_publish_slot, content_sink_publish_body, sink, frame, result);
}

gf_status content_sink_heartbeat_body(gf_content_sink sink) {
    ContentFrameSink found{};
    const gf_status looked_up = find_sink(sink, sink_heartbeat_slot, found);
    if (looked_up != GF_STATUS_OK) return looked_up;
    found.heartbeat();
    return GF_STATUS_OK;
}

gf_status GF_CALL content_sink_heartbeat(gf_content_sink sink) noexcept {
    return call_slot(sink_heartbeat_slot, content_sink_heartbeat_body, sink);
}

gf_status content_sink_publish_semantics_body(gf_content_sink sink, const gf_content_semantics* semantics,
                                              gf_content_semantics_result* result) {
    if (result == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, sink_publish_semantics_slot, "publish_semantics needs a result");
    }
    ContentSemantics read{};
    const gf_status record = read_semantics(semantics, sink_publish_semantics_slot, read);
    if (record != GF_STATUS_OK) return record;
    ContentFrameSink found{};
    const gf_status looked_up = find_sink(sink, sink_publish_semantics_slot, found);
    if (looked_up != GF_STATUS_OK) return looked_up;
    const ContentSemanticsResult published = found.publish_semantics(std::move(read));
    *result = semantics_value(published);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_sink_publish_semantics(gf_content_sink sink, const gf_content_semantics* semantics,
                                                 gf_content_semantics_result* result) noexcept {
    return call_slot(sink_publish_semantics_slot, content_sink_publish_semantics_body, sink, semantics, result);
}

gf_status content_sink_request_body(gf_content_sink sink, gf_content_request* request) {
    if (request == nullptr || (*request).struct_size < request_size_1_4) {
        return fail(GF_STATUS_INVALID_ARGUMENT, sink_request_slot,
                    "a gf_content_request needs struct_size of at least 32");
    }
    ContentFrameSink found{};
    const gf_status looked_up = find_sink(sink, sink_request_slot, found);
    if (looked_up != GF_STATUS_OK) return looked_up;
    write_request(found.request(), *request);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_sink_request(gf_content_sink sink, gf_content_request* request) noexcept {
    return call_slot(sink_request_slot, content_sink_request_body, sink, request);
}

gf_status content_sink_wait_for_request_body(gf_content_sink sink, std::uint64_t seen_generation,
                                             std::uint64_t timeout_ms, gf_content_request* request) {
    if (request == nullptr || (*request).struct_size < request_size_1_4) {
        return fail(GF_STATUS_INVALID_ARGUMENT, sink_wait_for_request_slot,
                    "a gf_content_request needs struct_size of at least 32");
    }
    std::chrono::milliseconds timeout{};
    const gf_status read = read_milliseconds(timeout_ms, sink_wait_for_request_slot, timeout);
    if (read != GF_STATUS_OK) return read;
    ContentFrameSink found{};
    const gf_status looked_up = find_sink(sink, sink_wait_for_request_slot, found);
    if (looked_up != GF_STATUS_OK) return looked_up;
    // The copy keeps the channel alive through the wait even when the
    // handle is released meanwhile.
    write_request(found.wait_for_request(seen_generation, timeout), *request);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_sink_wait_for_request(gf_content_sink sink, std::uint64_t seen_generation,
                                                std::uint64_t timeout_ms, gf_content_request* request) noexcept {
    return call_slot(sink_wait_for_request_slot, content_sink_wait_for_request_body, sink, seen_generation, timeout_ms, request);
}

gf_status content_sink_state_body(gf_content_sink sink, std::uint32_t* revoked,
                                  std::uint32_t* stop_requested) {
    if (revoked == nullptr || stop_requested == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, sink_state_slot, "sink_state needs both outputs");
    }
    ContentFrameSink found{};
    const gf_status looked_up = find_sink(sink, sink_state_slot, found);
    if (looked_up != GF_STATUS_OK) return looked_up;
    *revoked = found.revoked() ? 1U : 0U;
    *stop_requested = found.stop_requested() ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL content_sink_state(gf_content_sink sink, std::uint32_t* revoked,
                                     std::uint32_t* stop_requested) noexcept {
    return call_slot(sink_state_slot, content_sink_state_body, sink, revoked, stop_requested);
}

gf_status content_sink_statistics_body(gf_content_sink sink, gf_content_statistics* statistics) {
    if (statistics == nullptr || (*statistics).struct_size < statistics_size_1_4) {
        return fail(GF_STATUS_INVALID_ARGUMENT, sink_statistics_slot,
                    "a gf_content_statistics needs struct_size of at least 80");
    }
    ContentFrameSink found{};
    const gf_status looked_up = find_sink(sink, sink_statistics_slot, found);
    if (looked_up != GF_STATUS_OK) return looked_up;
    write_statistics(found.statistics(), *statistics);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_sink_statistics(gf_content_sink sink, gf_content_statistics* statistics) noexcept {
    return call_slot(sink_statistics_slot, content_sink_statistics_body, sink, statistics);
}

gf_status content_sink_release_body(gf_content_sink sink) {
    if (!sinks().remove(sink)) {
        return fail(GF_STATUS_STALE_HANDLE, sink_release_slot, "the sink was released or never given out");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL content_sink_release(gf_content_sink sink) noexcept {
    return call_slot(sink_release_slot, content_sink_release_body, sink);
}

gf_status content_program_sink_body(gf_content_sink* sink) {
    if (sink == nullptr) return fail(GF_STATUS_INVALID_ARGUMENT, program_sink_slot, "program_sink needs an output");
    if (running_context == nullptr) {
        return fail(GF_STATUS_INVALID_STATE, program_sink_slot,
                    "program_sink is called inside a content program's run callback only");
    }
    const gf_status handed = hand_out((*running_context).sink(), sink, program_sink_slot);
    return handed;
}

gf_status GF_CALL content_program_sink(gf_content_sink* sink) noexcept {
    return call_slot(program_sink_slot, content_program_sink_body, sink);
}

gf_status content_program_fail_body(gf_utf8_view message) {
    if (running_program == nullptr) {
        return fail(GF_STATUS_INVALID_STATE, program_fail_slot,
                    "program_fail is called inside a content program's run callback only");
    }
    std::string_view text{};
    const gf_status read = read_text(message, program_fail_slot, text);
    if (read != GF_STATUS_OK) return read;
    (*running_program).name_failure(std::string(text));
    return GF_STATUS_OK;
}

gf_status GF_CALL content_program_fail(gf_utf8_view message) noexcept {
    return call_slot(program_fail_slot, content_program_fail_body, message);
}

// ---------------------------------------------------------------------------
// Editor slots.

gf_status content_editor_text_revision_body(gf_object editor, std::uint64_t* revision) {
    if (revision == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, editor_text_revision_slot, "editor_text_revision needs an output");
    }
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_text_revision_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    *revision = (*found).text_revision();
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_text_revision(gf_object editor, std::uint64_t* revision) noexcept {
    return call_slot(editor_text_revision_slot, content_editor_text_revision_body, editor, revision);
}

gf_status content_editor_text_body(gf_object editor, char* buffer, std::uint64_t capacity,
                                   std::uint64_t* required) {
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_text_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    const std::string text = (*found).text();
    const gf_status written = write_text(text, buffer, capacity, required, editor_text_slot);
    return written;
}

gf_status GF_CALL content_editor_text(gf_object editor, char* buffer, std::uint64_t capacity,
                                      std::uint64_t* required) noexcept {
    return call_slot(editor_text_slot, content_editor_text_body, editor, buffer, capacity, required);
}

gf_status content_editor_set_text_body(gf_object editor, gf_utf8_view text) {
    std::string_view read{};
    const gf_status valid = read_text(text, editor_set_text_slot, read);
    if (valid != GF_STATUS_OK) return valid;
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_set_text_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    (*found).set_text(read);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_set_text(gf_object editor, gf_utf8_view text) noexcept {
    return call_slot(editor_set_text_slot, content_editor_set_text_body, editor, text);
}

// The C layout's arrays, checked for presence; GF_STATUS_INVALID_ARGUMENT
// for an array with no data and a nonzero count.
template <typename Element>
gf_status layout_array(const Element* data, std::uint64_t count, SlotIdentity slot, std::span<const Element>& out) {
    if (data == nullptr && count != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a layout array with no data has count zero");
    }
    out = std::span<const Element>(data, static_cast<std::size_t>(count));
    return GF_STATUS_OK;
}

gf_status content_editor_submit_layout_body(gf_object editor, const gf_content_text_layout* layout,
                                            gf_content_layout_result* result) {
    if (layout == nullptr || result == nullptr || (*layout).struct_size < text_layout_size_1_4 ||
        (*layout).reserved != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, editor_submit_layout_slot,
                    "a gf_content_text_layout needs struct_size of at least 64, reserved zero and a result");
    }
    std::span<const gf_content_text_line> lines{};
    std::span<const gf_content_text_cluster> clusters{};
    std::span<const gf_content_caret_stop> carets{};
    const gf_status line_array = layout_array((*layout).lines, (*layout).line_count, editor_submit_layout_slot,
                                              lines);
    if (line_array != GF_STATUS_OK) return line_array;
    const gf_status cluster_array = layout_array((*layout).clusters, (*layout).cluster_count,
                                                 editor_submit_layout_slot, clusters);
    if (cluster_array != GF_STATUS_OK) return cluster_array;
    const gf_status caret_array = layout_array((*layout).carets, (*layout).caret_count, editor_submit_layout_slot,
                                               carets);
    if (caret_array != GF_STATUS_OK) return caret_array;
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_submit_layout_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    // Counts first, so an oversized layout is refused before a copy.
    if (lines.size() > maximum_content_layout_entries || clusters.size() > maximum_content_layout_entries ||
        carets.size() > maximum_content_layout_entries) {
        *result = GF_CONTENT_LAYOUT_REFUSED_TOO_LARGE;
        return GF_STATUS_OK;
    }
    ContentTextLayout made{};
    made.text_revision = (*layout).text_revision;
    // The transform, when the caller's record is large enough to carry it.
    if ((*layout).struct_size >= text_layout_size_transformed) {
        if ((*layout).transformed > 1U || (*layout).transform_reserved != 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, editor_submit_layout_slot,
                        "transformed is 0 or 1 and transform_reserved zero");
        }
        if ((*layout).transformed == 1U) {
            const gf_content_layout_transform& given = (*layout).transform;
            ContentLayoutTransform transform{};
            transform.xx = given.xx;
            transform.xy = given.xy;
            transform.yx = given.yx;
            transform.yy = given.yy;
            transform.dx = given.dx;
            transform.dy = given.dy;
            made.transform.set(transform);
        }
    }
    made.lines.reserve(lines.size());
    made.clusters.reserve(clusters.size());
    made.carets.reserve(carets.size());
    for (const gf_content_text_line& line : lines) {
        ContentTextLine copy{};
        copy.range = Utf8Range{Utf8Offset(static_cast<std::size_t>(line.start)),
                               Utf8Offset(static_cast<std::size_t>(line.end))};
        copy.bounds = rect_of(line.bounds);
        made.lines.push_back(copy);
    }
    for (const gf_content_text_cluster& cluster : clusters) {
        if (cluster.right_to_left > 1U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, editor_submit_layout_slot, "right_to_left is 0 or 1");
        }
        ContentTextCluster copy{};
        copy.range = Utf8Range{Utf8Offset(static_cast<std::size_t>(cluster.start)),
                               Utf8Offset(static_cast<std::size_t>(cluster.end))};
        copy.line = cluster.line;
        copy.bounds = rect_of(cluster.bounds);
        copy.right_to_left = cluster.right_to_left == 1U;
        made.clusters.push_back(copy);
    }
    for (const gf_content_caret_stop& caret : carets) {
        ContentCaretStop copy{};
        copy.offset = Utf8Offset(static_cast<std::size_t>(caret.offset));
        copy.caret = rect_of(caret.caret);
        made.carets.push_back(copy);
    }
    const ContentLayoutResult submitted = (*found).submit_layout(std::move(made));
    *result = layout_value(submitted);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_submit_layout(gf_object editor, const gf_content_text_layout* layout,
                                               gf_content_layout_result* result) noexcept {
    return call_slot(editor_submit_layout_slot, content_editor_submit_layout_body, editor, layout, result);
}

gf_status content_editor_layout_current_body(gf_object editor, std::uint32_t* current) {
    if (current == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, editor_layout_current_slot, "editor_layout_current needs an output");
    }
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_layout_current_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    *current = (*found).layout_current() ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_layout_current(gf_object editor, std::uint32_t* current) noexcept {
    return call_slot(editor_layout_current_slot, content_editor_layout_current_body, editor, current);
}

gf_status content_editor_open_frames_body(gf_object editor, std::uint32_t opaque, gf_content_sink* sink) {
    if (sink == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, editor_open_frames_slot, "editor_open_frames needs an output");
    }
    bool opaque_value = false;
    const gf_status flag = read_flag(opaque, editor_open_frames_slot, opaque_value);
    if (flag != GF_STATUS_OK) return flag;
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_open_frames_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    ContentFrameSink opened = (*found).open_frames(opaque_value);
    const gf_status handed = hand_out(std::move(opened), sink, editor_open_frames_slot);
    return handed;
}

gf_status GF_CALL content_editor_open_frames(gf_object editor, std::uint32_t opaque, gf_content_sink* sink) noexcept {
    return call_slot(editor_open_frames_slot, content_editor_open_frames_body, editor, opaque, sink);
}

gf_status content_editor_caret_rectangle_body(gf_object editor, std::uint32_t* present,
                                              gf_logical_rect* rectangle) {
    if (present == nullptr || rectangle == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, editor_caret_rectangle_slot,
                    "editor_caret_rectangle needs both outputs");
    }
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_caret_rectangle_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    const stxlib::Optional<Rect> caret = (*found).caret_rectangle();
    *present = caret.present() ? 1U : 0U;
    if (caret.present()) *rectangle = rect_value(*caret.value());
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_caret_rectangle(gf_object editor, std::uint32_t* present,
                                                 gf_logical_rect* rectangle) noexcept {
    return call_slot(editor_caret_rectangle_slot, content_editor_caret_rectangle_body, editor, present, rectangle);
}

gf_status content_editor_selection_rectangles_body(gf_object editor, gf_logical_rect* buffer,
                                                   std::uint64_t capacity, std::uint64_t* required) {
    if (required == nullptr || (buffer == nullptr && capacity != 0U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, editor_selection_rectangles_slot,
                    "editor_selection_rectangles needs required, and a buffer for a nonzero capacity");
    }
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_selection_rectangles_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    const std::vector<Rect> boxes = (*found).selection_rectangles();
    *required = boxes.size();
    if (capacity < boxes.size()) {
        return fail(GF_STATUS_BUFFER_TOO_SMALL, editor_selection_rectangles_slot,
                    "the buffer holds fewer boxes than the selection has");
    }
    const std::span<gf_logical_rect> out(buffer, boxes.size());
    for (std::size_t index = 0U; index < boxes.size(); ++index) out[index] = rect_value(boxes[index]);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_selection_rectangles(gf_object editor, gf_logical_rect* buffer,
                                                      std::uint64_t capacity, std::uint64_t* required) noexcept {
    return call_slot(editor_selection_rectangles_slot, content_editor_selection_rectangles_body, editor, buffer, capacity, required);
}

gf_status content_editor_position_at_body(gf_object editor, gf_logical_point point,
                                          std::uint64_t* offset) {
    if (offset == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, editor_position_at_slot, "editor_position_at needs an output");
    }
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_position_at_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    const Utf8Offset position = (*found).position_at(Point{point.x, point.y});
    *offset = position.value();
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_position_at(gf_object editor, gf_logical_point point,
                                             std::uint64_t* offset) noexcept {
    return call_slot(editor_position_at_slot, content_editor_position_at_body, editor, point, offset);
}

gf_status content_editor_set_draws_selection_body(gf_object editor, std::uint32_t draws) {
    bool value = false;
    const gf_status flag = read_flag(draws, editor_set_draws_selection_slot, value);
    if (flag != GF_STATUS_OK) return flag;
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_set_draws_selection_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    (*found).set_draws_selection(value);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_set_draws_selection(gf_object editor, std::uint32_t draws) noexcept {
    return call_slot(editor_set_draws_selection_slot, content_editor_set_draws_selection_body, editor, draws);
}

gf_status content_editor_set_multiline_body(gf_object editor, std::uint32_t multiline) {
    bool value = false;
    const gf_status flag = read_flag(multiline, editor_set_multiline_slot, value);
    if (flag != GF_STATUS_OK) return flag;
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_set_multiline_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    (*found).set_multiline(value);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_set_multiline(gf_object editor, std::uint32_t multiline) noexcept {
    return call_slot(editor_set_multiline_slot, content_editor_set_multiline_body, editor, multiline);
}

gf_status content_editor_set_read_only_body(gf_object editor, std::uint32_t read_only) {
    bool value = false;
    const gf_status flag = read_flag(read_only, editor_set_read_only_slot, value);
    if (flag != GF_STATUS_OK) return flag;
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_set_read_only_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    (*found).set_read_only(value);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_set_read_only(gf_object editor, std::uint32_t read_only) noexcept {
    return call_slot(editor_set_read_only_slot, content_editor_set_read_only_body, editor, read_only);
}

gf_status content_editor_set_accepts_tab_body(gf_object editor, std::uint32_t accepts) {
    bool value = false;
    const gf_status flag = read_flag(accepts, editor_set_accepts_tab_slot, value);
    if (flag != GF_STATUS_OK) return flag;
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_set_accepts_tab_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    (*found).set_accepts_tab(value);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_set_accepts_tab(gf_object editor, std::uint32_t accepts) noexcept {
    return call_slot(editor_set_accepts_tab_slot, content_editor_set_accepts_tab_body, editor, accepts);
}

gf_status content_editor_replace_selection_body(gf_object editor, gf_utf8_view replacement,
                                                std::uint32_t* applied) {
    if (applied == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, editor_replace_selection_slot,
                    "editor_replace_selection needs an output");
    }
    std::string_view read{};
    const gf_status valid = read_text(replacement, editor_replace_selection_slot, read);
    if (valid != GF_STATUS_OK) return valid;
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_replace_selection_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    const bool made = (*found).replace_selection(read);
    *applied = made ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_replace_selection(gf_object editor, gf_utf8_view replacement,
                                                   std::uint32_t* applied) noexcept {
    return call_slot(editor_replace_selection_slot, content_editor_replace_selection_body, editor, replacement,
                     applied);
}

gf_status content_editor_delete_selection_body(gf_object editor, std::uint32_t* applied) {
    if (applied == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, editor_delete_selection_slot,
                    "editor_delete_selection needs an output");
    }
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_delete_selection_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    const bool made = (*found).delete_selection();
    *applied = made ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_delete_selection(gf_object editor, std::uint32_t* applied) noexcept {
    return call_slot(editor_delete_selection_slot, content_editor_delete_selection_body, editor, applied);
}

gf_status content_editor_composition_range_body(gf_object editor, std::uint32_t* present, std::uint64_t* start,
                                                std::uint64_t* end) {
    if (present == nullptr || start == nullptr || end == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, editor_composition_range_slot,
                    "editor_composition_range needs all three outputs");
    }
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_composition_range_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    const std::optional<Utf8Range> range = (*found).composition_range();
    *present = range ? 1U : 0U;
    if (range) {
        *start = (*range).start.value();
        *end = (*range).end.value();
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_composition_range(gf_object editor, std::uint32_t* present, std::uint64_t* start,
                                                   std::uint64_t* end) noexcept {
    return call_slot(editor_composition_range_slot, content_editor_composition_range_body, editor, present, start,
                     end);
}

gf_status content_editor_caret_quad_body(gf_object editor, std::uint32_t* present, gf_content_quad* quad) {
    if (present == nullptr || quad == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, editor_caret_quad_slot, "editor_caret_quad needs both outputs");
    }
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_caret_quad_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    const std::optional<ContentQuad> caret = (*found).caret_quad();
    *present = caret ? 1U : 0U;
    if (caret) *quad = quad_value(*caret);
    return GF_STATUS_OK;
}

gf_status GF_CALL content_editor_caret_quad(gf_object editor, std::uint32_t* present,
                                            gf_content_quad* quad) noexcept {
    return call_slot(editor_caret_quad_slot, content_editor_caret_quad_body, editor, present, quad);
}

// Copies `quads` into the caller's buffer, as editor_selection_rectangles
// copies its boxes: required first, then nothing on a short buffer.
gf_status write_quads(const std::vector<ContentQuad>& quads, gf_content_quad* buffer, std::uint64_t capacity,
                      std::uint64_t* required, SlotIdentity slot) {
    *required = quads.size();
    if (capacity < quads.size()) {
        return fail(GF_STATUS_BUFFER_TOO_SMALL, slot, "the buffer holds fewer quads than there are");
    }
    const std::span<gf_content_quad> out(buffer, quads.size());
    for (std::size_t index = 0U; index < quads.size(); ++index) out[index] = quad_value(quads[index]);
    return GF_STATUS_OK;
}

gf_status content_editor_selection_quads_body(gf_object editor, gf_content_quad* buffer, std::uint64_t capacity,
                                              std::uint64_t* required) {
    if (required == nullptr || (buffer == nullptr && capacity != 0U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, editor_selection_quads_slot,
                    "editor_selection_quads needs required, and a buffer for a nonzero capacity");
    }
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_selection_quads_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    const std::vector<ContentQuad> quads = (*found).selection_quads();
    const gf_status written = write_quads(quads, buffer, capacity, required, editor_selection_quads_slot);
    return written;
}

gf_status GF_CALL content_editor_selection_quads(gf_object editor, gf_content_quad* buffer, std::uint64_t capacity,
                                                 std::uint64_t* required) noexcept {
    return call_slot(editor_selection_quads_slot, content_editor_selection_quads_body, editor, buffer, capacity,
                     required);
}

gf_status content_editor_composition_quads_body(gf_object editor, gf_content_quad* buffer, std::uint64_t capacity,
                                                std::uint64_t* required) {
    if (required == nullptr || (buffer == nullptr && capacity != 0U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, editor_composition_quads_slot,
                    "editor_composition_quads needs required, and a buffer for a nonzero capacity");
    }
    std::shared_ptr<ContentTextEditor> found;
    const gf_status entered = enter_editor(editor, editor_composition_quads_slot, found);
    if (entered != GF_STATUS_OK) return entered;
    const std::vector<ContentQuad> quads = (*found).composition_quads();
    const gf_status written = write_quads(quads, buffer, capacity, required, editor_composition_quads_slot);
    return written;
}

gf_status GF_CALL content_editor_composition_quads(gf_object editor, gf_content_quad* buffer,
                                                   std::uint64_t capacity, std::uint64_t* required) noexcept {
    return call_slot(editor_composition_quads_slot, content_editor_composition_quads_body, editor, buffer, capacity,
                     required);
}

constexpr gf_content_table table{
    .struct_size = sizeof(gf_content_table),
    .abi_minor = GF_ABI_MINOR,
    .open_content = &content_open_content,
    .close_content = &content_close_content,
    .start_program = &content_start_program,
#if defined(GUI_FORMS_HIGH_INTEGRITY)
    .start_hosted = nullptr,
#else
    .start_hosted = &content_start_hosted,
#endif
    .terminate = &content_terminate,
    .set_termination_grace = &content_set_termination_grace,
    .set_stale_interval = &content_set_stale_interval,
    .status = &content_status,
    .failure = &content_failure,
    .statistics = &content_statistics,
    .request = &content_request,
    .set_status_caption = &content_set_status_caption,
    .set_semantics = &content_set_semantics,
    .program_running = &content_program_running,
#if defined(GUI_FORMS_HIGH_INTEGRITY)
    .hosted_input = nullptr,
#else
    .hosted_input = &content_hosted_input,
#endif
    .hosted_support = &content_hosted_support,
    .sink_publish = &content_sink_publish,
    .sink_heartbeat = &content_sink_heartbeat,
    .sink_publish_semantics = &content_sink_publish_semantics,
    .sink_request = &content_sink_request,
    .sink_wait_for_request = &content_sink_wait_for_request,
    .sink_state = &content_sink_state,
    .sink_statistics = &content_sink_statistics,
    .sink_release = &content_sink_release,
    .program_sink = &content_program_sink,
    .program_fail = &content_program_fail,
    .editor_text_revision = &content_editor_text_revision,
    .editor_text = &content_editor_text,
    .editor_set_text = &content_editor_set_text,
    .editor_submit_layout = &content_editor_submit_layout,
    .editor_layout_current = &content_editor_layout_current,
    .editor_open_frames = &content_editor_open_frames,
    .editor_caret_rectangle = &content_editor_caret_rectangle,
    .editor_selection_rectangles = &content_editor_selection_rectangles,
    .editor_position_at = &content_editor_position_at,
    .editor_set_draws_selection = &content_editor_set_draws_selection,
    .editor_set_multiline = &content_editor_set_multiline,
    .editor_set_read_only = &content_editor_set_read_only,
    .editor_set_accepts_tab = &content_editor_set_accepts_tab,
    .editor_replace_selection = &content_editor_replace_selection,
    .editor_delete_selection = &content_editor_delete_selection,
    .editor_composition_range = &content_editor_composition_range,
    .editor_caret_quad = &content_editor_caret_quad,
    .editor_selection_quads = &content_editor_selection_quads,
    .editor_composition_quads = &content_editor_composition_quads,
};

} // namespace

const gf_content_table& content_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
