// The fail-fast boundary's reporter and parts. See fail_fast.hpp.

#include "fail_fast.hpp"
#include "failure_snapshot.hpp"

#include "../diagnostics/shadow_frames/shadow_frame_records.hpp"

#include <stxlib/console.hpp>
#include <stxlib/fail_fast.hpp>
#include <stxlib/images.hpp>
#include <stxlib/result.hpp>
#include <stxlib/shadow.hpp>
#include <stxlib/stack_trace.hpp>
#include <stxlib/text_view.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string_view>

namespace gui_forms::detail {
namespace {

// At most this many frames of the current stack are printed; the report
// must stay readable. STXlib's walk takes at most 32 on Windows
// (stxlib/stack_trace.hpp).
constexpr std::size_t maximum_stack_frames = 48U;
static_assert(maximum_stack_frames <= stxlib::maximum_stack_trace_addresses);

// The rooms describe_address copies an image's and a symbol's names into.
// A longer name is cut (and ReportLine cuts the line at its 1024 bytes).
constexpr std::size_t image_name_capacity = 512U;
constexpr std::size_t symbol_name_capacity = 256U;

// What a code address in the report is: a return address from a stack walk,
// which points just past its call (so the call's own function and image are
// found at the byte before it), or an exact address, such as the borrow
// report's creation site.
enum class CodeAddressKind : unsigned char {
    return_address,
    exact,
};

// The calling thread's copy of the whole what() of the exception a
// fail-fast gate classified (or of fail_fast's message), which the
// ErrorRecord's 200 bytes could not hold. Written inside the gate's handler
// and read by the reporter, which STXlib calls next on the same thread;
// pending says the copy belongs to the record being reported.
struct FailFastMessage final {
    bool pending = false;
    std::uint32_t size = 0U;
    std::array<char, failure_snapshot_message_capacity> text{};
};

thread_local FailFastMessage fail_fast_message;

// The leading bytes of text that fit in capacity and end on a whole UTF-8
// sequence.
std::size_t whole_prefix(const std::string_view text, const std::size_t capacity) noexcept {
    if (text.size() <= capacity) {
        return text.size();
    }
    std::size_t size = capacity;
    // Back up over continuation bytes to the lead byte of the cut sequence.
    while (size > 0U && (static_cast<unsigned char>(text[size]) & 0xC0U) == 0x80U) {
        --size;
    }
    return size;
}

void keep_fail_fast_message(const std::string_view text) noexcept {
    FailFastMessage& message = fail_fast_message;
    const std::size_t size = whole_prefix(text, message.text.size());
    if (size != 0U) {
        std::memcpy(message.text.data(), text.data(), size);
    }
    message.size = static_cast<std::uint32_t>(size);
    message.pending = true;
}

// The message of the record being reported: the classifier's whole copy
// when pending, else the record's. Borrows the thread's copy or record.
std::string_view take_fail_fast_message(const stxlib::ErrorRecord& record) noexcept {
    FailFastMessage& message = fail_fast_message;
    if (message.pending) {
        message.pending = false;
        const std::string_view kept(message.text.data(), message.size);
        return kept;
    }
    const stxlib::TextView recorded = record.message();
    const std::string_view view(recorded.data(), recorded.count());
    return view;
}

// STXlib's classifier hook: keeps the standard classification (returns
// false) and copies the whole text.
bool classify_fail_fast_exception(void* const context, stxlib::ExceptionClass& decided) noexcept {
    static_cast<void>(context);
    static_cast<void>(decided);
    try {
        throw;
    } catch (const std::exception& error) {
        keep_fail_fast_message(error.what());
    } catch (...) {
        keep_fail_fast_message("an exception not derived from std::exception");
    }
    return false;
}

// One line of the report, built in fixed storage and written whole through
// STXlib's console (one call, so another thread's text never comes between
// its pieces). Text past the capacity is cut at a whole UTF-8 sequence.
class ReportLine final {
public:
    void append(const std::string_view text) noexcept {
        const std::size_t room = text_.size() - size_;
        const std::size_t taken = whole_prefix(text, room);
        if (taken != 0U) {
            std::memcpy(text_.data() + size_, text.data(), taken);
        }
        size_ += taken;
    }

    void append_number(const unsigned long long value) noexcept {
        std::array<char, 24U> digits{};
        const int written = std::snprintf(digits.data(), digits.size(), "%llu", value);
        if (written > 0) {
            append(std::string_view(digits.data(), static_cast<std::size_t>(written)));
        }
    }

    void append_hex(const unsigned long long value) noexcept {
        std::array<char, 24U> digits{};
        const int written = std::snprintf(digits.data(), digits.size(), "0x%llx", value);
        if (written > 0) {
            append(std::string_view(digits.data(), static_cast<std::size_t>(written)));
        }
    }

    // Writes the line and its newline; a failed write is ignored (the
    // process is ending and has nowhere else to report it).
    void write() const noexcept {
        const std::string_view view(text_.data(), size_);
        const stxlib::Status written = stxlib::write_console_line(stxlib::ConsoleStream::error, well_formed_text(view));
        static_cast<void>(written);
    }

    // Writes the text without a newline.
    void write_partial() const noexcept {
        const std::string_view view(text_.data(), size_);
        const stxlib::Status written = stxlib::write_console(stxlib::ConsoleStream::error, well_formed_text(view));
        static_cast<void>(written);
    }

private:
    std::array<char, 1024U> text_{};
    std::size_t size_ = 0U;
};

// One code address as image+offset (and symbol), appended to line, as
// STXlib's describe_address finds them (dladdr on POSIX hosts, the module
// and its exports on Windows; it allocates nothing, so the reporter may
// call it). The offset printed is the address's own, so a symbolizer reads
// it as before; a return address is looked up one byte back. An address no
// image holds, or a host without address lookup, prints the bare address.
void append_code_address(ReportLine& line, const void* const address, const CodeAddressKind kind) noexcept {
    if (address == nullptr) {
        line.append("unknown");
        return;
    }
    const stxlib::uptr value = reinterpret_cast<stxlib::uptr>(address);
    stxlib::uptr looked_up = value;
    if (kind == CodeAddressKind::return_address) {
        looked_up = value - 1U;
    }
    std::array<char, image_name_capacity> image_room{};
    std::array<char, symbol_name_capacity> symbol_room{};
    const stxlib::MutableView<char> image_view(image_room.data(), image_room.size());
    const stxlib::MutableView<char> symbol_view(symbol_room.data(), symbol_room.size());
    const stxlib::Result<stxlib::AddressDescription> described =
        stxlib::describe_address(looked_up, image_view, symbol_view);
    if (!described.succeeded()) {
        line.append_hex(static_cast<unsigned long long>(value));
        return;
    }
    const stxlib::AddressDescription& description = *described.value();
    const std::string_view image(description.image_name.data(), description.image_name.count());
    const stxlib::uptr offset = value - description.image_base;
    line.append(image.empty() ? std::string_view("?") : image);
    line.append("+");
    line.append_hex(static_cast<unsigned long long>(offset));
    if (description.symbol_name.count() != 0U) {
        const std::string_view symbol(description.symbol_name.data(), description.symbol_name.count());
        line.append(" (");
        line.append(symbol);
        line.append(")");
    }
}

// The current stack, one line per frame, through STXlib's walk (the one
// the shadow records use); a host that cannot walk says so in one line.
void write_stack() noexcept {
    std::array<stxlib::uptr, maximum_stack_frames> frames{};
    const stxlib::MutableView<stxlib::uptr> frame_room(frames.data(), frames.size());
    const stxlib::usize count = stxlib::capture_stack_trace(frame_room);
    if (count == 0U) {
        ReportLine line;
        line.append("  (no stack on this platform)");
        line.write();
        return;
    }
    for (stxlib::usize index = 0U; index < count; ++index) {
        ReportLine line;
        line.append("  #");
        line.append_number(static_cast<unsigned long long>(index));
        line.append(" ");
        append_code_address(line, reinterpret_cast<const void*>(frames[index]), CodeAddressKind::return_address);
        line.write();
    }
}

// The snapshot's shadow frames, outermost first, one line each: the kind's
// number, the C ABI function for a library call, the subject, the name and
// the sequence.
void write_shadow_frames(const FailureSnapshot& snapshot) noexcept {
    ReportLine heading;
    heading.append("GUI.Forms: shadow frames (");
    heading.append_number(snapshot.depth);
    heading.append(" open, ");
    heading.append_number(snapshot.frame_count);
    heading.append(" recorded):");
    heading.write();
    for (std::uint32_t index = 0U; index < snapshot.frame_count; ++index) {
        const ShadowFrame& frame = snapshot.frames[index];
        ReportLine line;
        line.append("  #");
        line.append_number(index);
        line.append(" kind ");
        line.append_number(static_cast<unsigned long long>(frame.kind));
        if (frame.kind == ShadowFrameKind::library_call) {
            line.append(" table ");
            line.append_number(frame.interface_id);
            line.append(" slot ");
            line.append_number(frame.function_index);
        }
        line.append(" ");
        line.append(std::string_view(frame.subject.data(), frame.subject_size));
        line.append(" ");
        line.append(std::string_view(frame.name.data(), frame.name_size));
        line.append(" ");
        line.append_number(frame.sequence);
        line.write();
    }
}

// The record's kind, code and foreign code, and in a development build the
// exception's type.
void write_record_facts(const stxlib::ErrorRecord& record) noexcept {
    ReportLine facts;
    facts.append("GUI.Forms: record: ");
    const stxlib::TextView kind = stxlib::error_kind_name(record.kind());
    facts.append(std::string_view(kind.data(), kind.count()));
    if (record.code() != 0U) {
        facts.append(" code ");
        facts.append_number(record.code());
    }
    const stxlib::ForeignCode foreign = record.foreign();
    if (foreign.present()) {
        facts.append(" foreign ");
        facts.append_number(static_cast<unsigned long long>(foreign.family()));
        facts.append(" ");
        facts.append_number(static_cast<unsigned long long>(foreign.value()));
    }
    facts.append(" sequence ");
    facts.append_number(record.sequence());
    facts.write();

    std::array<char, stxlib::exception_type_display_capacity> type_storage{};
    const stxlib::MutableView<char> type_output(type_storage.data(), type_storage.size());
    const stxlib::TextView type = stxlib::exception_type_for_display(record, type_output);
    if (!type.empty()) {
        ReportLine line;
        line.append("GUI.Forms: exception type: ");
        line.append(std::string_view(type.data(), type.count()));
        line.write();
    }
}

// The native trace at the throw, which STXlib keeps in a development build.
void write_throw_trace(const stxlib::ErrorRecord& record) noexcept {
    if (!record.shadow().present()) {
        return;
    }
    std::array<stxlib::uptr, stxlib::shadow_trace_capacity> trace{};
    const stxlib::MutableView<stxlib::ShadowFrameRecord> no_frames{};
    const stxlib::MutableView<stxlib::uptr> trace_view(trace.data(), trace.size());
    const stxlib::Result<stxlib::ShadowSummary> read = stxlib::read_shadow(record.shadow(), no_frames, trace_view);
    if (!read.succeeded()) {
        return;
    }
    const std::size_t count = (*read.value()).trace_count < trace.size() ? (*read.value()).trace_count : trace.size();
    if (count == 0U) {
        return;
    }
    ReportLine heading;
    heading.append("GUI.Forms: trace at the throw:");
    heading.write();
    for (std::size_t index = 0U; index < count; ++index) {
        ReportLine line;
        line.append("  #");
        line.append_number(static_cast<unsigned long long>(index));
        line.append(" ");
        append_code_address(line, reinterpret_cast<const void*>(trace[index]), CodeAddressKind::return_address);
        line.write();
    }
}

// GUI.Forms' fail-fast reporter (STXlib calls it, then ends the process):
// the snapshot first, so a dump holds it, then the whole record.
void report_fail_fast(void* const context, const stxlib::ErrorRecord& record) noexcept {
    static_cast<void>(context);
    const stxlib::TextView operation = record.operation().name();
    const std::string_view site(operation.data(), operation.count());
    const std::string_view message = take_fail_fast_message(record);
    const FailureBoundary boundary{site, failure_native_exception_status, 0U, 0U};
    if (record.shadow().present()) {
        record_failure_snapshot(boundary, message, record);
    } else {
        // No shadow record (fail_fast without an exception, or a build that
        // keeps none): the frames open now.
        record_failure_snapshot(boundary, message);
    }
    ReportLine heading;
    heading.append("GUI.Forms: fatal exception at ");
    heading.append(site);
    heading.append(": ");
    heading.append(message);
    heading.write();
    write_record_facts(record);
    write_shadow_frames(thread_failure_snapshot());
    write_throw_trace(record);
    ReportLine stack_heading;
    stack_heading.append("GUI.Forms: stack:");
    stack_heading.write();
    write_stack();
}

// Installs the reporter; run once, by the static below.
bool install_reporter() noexcept {
    const stxlib::FailFastReporter reporter{&report_fail_fast, nullptr};
    const stxlib::Status installed = stxlib::install_fail_fast_reporter(reporter);
    // already_exists: another reporter was installed first and stays.
    const bool ours = installed.succeeded();
    return ours;
}

} // namespace

const stxlib::ExceptionClassifier fail_fast_classifier{&classify_fail_fast_exception, nullptr};

void ensure_fail_fast_reporter() noexcept {
    // A function-local static: initialised once, by the first caller, with
    // concurrent callers waiting ([stmt.dcl]).
    static const bool installed = install_reporter();
    static_cast<void>(installed);
}

void fail_fast(const stxlib::Operation site, const std::string_view message) noexcept {
    ensure_fail_fast_reporter();
    keep_fail_fast_message(message);
    const stxlib::ErrorDetail detail{.message = well_formed_text(message)};
    const stxlib::ErrorRecord record = stxlib::ErrorRecord::make(stxlib::ErrorKind::internal_failure, site, detail);
    stxlib::fail_fast(record);
}

void write_fail_fast_stack() noexcept {
    write_stack();
}

void write_code_address(const void* address) noexcept {
    ReportLine line;
    append_code_address(line, address, CodeAddressKind::exact);
    line.write_partial();
}

} // namespace gui_forms::detail
