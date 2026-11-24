#include "automation_tools.hpp"

#include "../../../core/json/json_reader/json_reader.hpp"

#include <cstring>
#include <exception>
#include <stxlib/threads.hpp>

namespace gui_forms::automation {

const char* tool_error_name(ToolError error) noexcept {
    switch (error) {
    case ToolError::not_found: return "not_found";
    case ToolError::stale_identity: return "stale_identity";
    case ToolError::ambiguous: return "ambiguous";
    case ToolError::disabled: return "disabled";
    case ToolError::not_visible: return "not_visible";
    case ToolError::unsupported_action: return "unsupported_action";
    case ToolError::dialog_open: return "dialog_open";
    case ToolError::needs_user: return "needs_user";
    case ToolError::user_took_control: return "user_took_control";
    case ToolError::declined: return "declined";
    case ToolError::protected_content: return "protected";
    case ToolError::timeout: return "timeout";
    case ToolError::not_in_this_build: return "not_in_this_build";
    case ToolError::invalid_arguments: return "invalid_arguments";
    case ToolError::unknown_tool: return "unknown_tool";
    case ToolError::window_gone: return "window_gone";
    case ToolError::cancelled: return "cancelled";
    case ToolError::busy: return "busy";
    case ToolError::internal: return "internal";
    }
    // A code outside the enumeration reads as the library's failure.
    return "internal";
}

const char* model_tool_error_name(ModelToolError error) noexcept {
    return tool_error_name(error);
}

const char* tool_registration_name(ToolRegistration registration) noexcept {
    switch (registration) {
    case ToolRegistration::added: return "added";
    case ToolRegistration::invalid_name: return "invalid_name";
    case ToolRegistration::duplicate: return "duplicate";
    case ToolRegistration::full: return "full";
    case ToolRegistration::invalid_spec: return "invalid_spec";
    }
    return "invalid_spec";
}

const char* model_tool_registration_name(ModelToolRegistration registration) noexcept {
    return tool_registration_name(registration);
}

ToolReply ToolReply::success(std::string text) {
    ToolSuccess value{};
    value.text = std::move(text);
    const ToolReply reply(std::variant<ToolSuccess, ToolFailure>(std::move(value)));
    return reply;
}

ToolReply ToolReply::success(ToolSuccess value) {
    const ToolReply reply(std::variant<ToolSuccess, ToolFailure>(std::move(value)));
    return reply;
}

ToolReply ToolReply::failure(ToolError error, std::string message, std::string hint) {
    ToolFailure value{error, std::move(message), std::move(hint)};
    const ToolReply reply(std::variant<ToolSuccess, ToolFailure>(std::move(value)));
    return reply;
}

bool valid_tool_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > maximum_tool_name_bytes) return false;
    const bool letter_first = (name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z');
    if (!letter_first) return false;
    for (const char character : name) {
        const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';
        const bool punctuation = character == '_' || character == '-' || character == '.';
        if (!letter && !digit && !punctuation) return false;
    }
    return true;
}

namespace {

// The most text and image bytes one reply carries.
constexpr std::size_t maximum_reply_text_bytes = 4U * 1024U * 1024U;
constexpr std::size_t maximum_reply_image_bytes = 16U * 1024U * 1024U;

// Whether `text` is well-formed UTF-8 (no overlong forms, surrogates or
// code points past U+10FFFF).
bool valid_utf8(std::string_view text) noexcept {
    std::size_t index = 0U;
    while (index < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::size_t length = 0U;
        unsigned char low = 0x80U;
        unsigned char high = 0xBFU;
        if (lead < 0x80U) {
            ++index;
            continue;
        }
        if (lead >= 0xC2U && lead <= 0xDFU) {
            length = 2U;
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            length = 3U;
            if (lead == 0xE0U) low = 0xA0U;
            if (lead == 0xEDU) high = 0x9FU;
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            length = 4U;
            if (lead == 0xF0U) low = 0x90U;
            if (lead == 0xF4U) high = 0x8FU;
        } else {
            return false;
        }
        if (text.size() - index < length) return false;
        for (std::size_t offset = 1U; offset < length; ++offset) {
            const unsigned char next = static_cast<unsigned char>(text[index + offset]);
            const unsigned char floor = offset == 1U ? low : static_cast<unsigned char>(0x80U);
            const unsigned char ceiling = offset == 1U ? high : static_cast<unsigned char>(0xBFU);
            if (next < floor || next > ceiling) return false;
        }
        index += length;
    }
    return true;
}

// A NUL-terminated text as a view; empty for null.
std::string_view view_of(const char* text) noexcept {
    if (text == nullptr) return std::string_view{};
    const std::string_view view(text, std::strlen(text));
    return view;
}

// Names the automation serves itself: the built-in tools and the
// program's commands (command.<id>).
bool reserved_tool_name(std::string_view name) noexcept {
    constexpr std::string_view built_in[] = {"windows", "snapshot", "act",     "pointer", "keyboard", "text",
                                             "wait",    "capture",  "inspect", "batch",   "control",
                                             "find",    "reset"};
    for (const std::string_view reserved : built_in) {
        if (name == reserved) return true;
    }
    const bool command = name.size() > 8U && name.substr(0U, 8U) == "command.";
    return command;
}

// An empty schema, or one JSON object.
bool valid_schema(std::string_view schema) {
    if (schema.empty()) return true;
    if (schema.size() > maximum_tool_schema_bytes) return false;
    const gui_forms::detail::JsonReadResult read = gui_forms::detail::read_json(schema);
    const bool object = read && read.root.kind == gui_forms::detail::JsonKind::object;
    return object;
}

} // namespace

ToolRegistration ToolRegistry::add(ToolSpec spec, ToolHandler handler, void* context) {
    if (!valid_tool_name(spec.name)) return ToolRegistration::invalid_name;
    if (handler == nullptr || spec.description.size() > maximum_tool_description_bytes ||
        !valid_utf8(spec.title) || !valid_utf8(spec.description) || !valid_schema(spec.input_schema)) {
        return ToolRegistration::invalid_spec;
    }
    // An acting tool runs on the UI thread, where the dialog rule, the
    // user's hold and the destructive question are applied to it.
    if (spec.access == ToolAccess::acting && spec.thread != ToolThread::ui) return ToolRegistration::invalid_spec;
    if (reserved_tool_name(spec.name)) return ToolRegistration::duplicate;
    const stxlib::ScopedLock lock(mutex_);
    for (const ToolEntry& entry : tools_) {
        if (entry.spec.name == spec.name) return ToolRegistration::duplicate;
    }
    if (tools_.size() >= maximum_registered_tools) return ToolRegistration::full;
    tools_.push_back(ToolEntry{std::move(spec), handler, context});
    ++generation_;
    return ToolRegistration::added;
}

bool ToolRegistry::remove(std::string_view name) {
    const stxlib::ScopedLock lock(mutex_);
    for (std::size_t index = 0U; index < tools_.size(); ++index) {
        if (tools_[index].spec.name != name) continue;
        tools_.erase(tools_.begin() + static_cast<std::ptrdiff_t>(index));
        ++generation_;
        return true;
    }
    return false;
}

bool ToolRegistry::find(std::string_view name, ToolEntry& entry) const {
    const stxlib::ScopedLock lock(mutex_);
    for (const ToolEntry& candidate : tools_) {
        if (candidate.spec.name != name) continue;
        entry = candidate;
        return true;
    }
    return false;
}

void ToolRegistry::fill_specs(std::vector<ToolSpec>& specs) const {
    const stxlib::ScopedLock lock(mutex_);
    specs.clear();
    specs.reserve(tools_.size());
    for (const ToolEntry& entry : tools_) specs.push_back(entry.spec);
}

ToolRegistration ToolRegistry::add_inspect_section(std::string name, std::string description,
                                                   InspectSectionHandler handler, void* context) {
    if (!valid_tool_name(name)) return ToolRegistration::invalid_name;
    if (handler == nullptr || description.size() > maximum_tool_description_bytes || !valid_utf8(description)) {
        return ToolRegistration::invalid_spec;
    }
    const stxlib::ScopedLock lock(mutex_);
    for (const InspectSectionEntry& entry : sections_) {
        if (entry.name == name) return ToolRegistration::duplicate;
    }
    if (sections_.size() >= maximum_inspect_sections) return ToolRegistration::full;
    sections_.push_back(InspectSectionEntry{std::move(name), std::move(description), handler, context});
    return ToolRegistration::added;
}

bool ToolRegistry::remove_inspect_section(std::string_view name) {
    const stxlib::ScopedLock lock(mutex_);
    for (std::size_t index = 0U; index < sections_.size(); ++index) {
        if (sections_[index].name != name) continue;
        sections_.erase(sections_.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }
    return false;
}

bool ToolRegistry::find_inspect_section(std::string_view name, InspectSectionEntry& entry) const {
    const stxlib::ScopedLock lock(mutex_);
    for (const InspectSectionEntry& candidate : sections_) {
        if (candidate.name != name) continue;
        entry = candidate;
        return true;
    }
    return false;
}

void ToolRegistry::fill_inspect_sections(std::vector<InspectSectionEntry>& sections) const {
    const stxlib::ScopedLock lock(mutex_);
    sections = sections_;
}

std::uint64_t ToolRegistry::generation() const {
    const stxlib::ScopedLock lock(mutex_);
    const std::uint64_t generation = generation_;
    return generation;
}

ToolRegistry& model_tool_registry() noexcept {
    // Never destroyed: a program's tools stay registered through exit, and a
    // late unregister never meets a destroyed lock.
    static ToolRegistry* const registry = new ToolRegistry();
    return *registry;
}

// ---- the replies a handler fills -------------------------------------------------

bool ModelToolReply::text(const char* utf8, std::size_t bytes) {
    ModelToolReplyState& state = *state_;
    if (state.failed) return false;
    if (bytes == 0U) return true;
    if (utf8 == nullptr) return false;
    const std::string_view text(utf8, bytes);
    if (!valid_utf8(text) || state.success.text.size() + bytes > maximum_reply_text_bytes) return false;
    state.success.text.append(text);
    return true;
}

bool ModelToolReply::structured(const char* json, std::size_t bytes) {
    ModelToolReplyState& state = *state_;
    if (state.failed || json == nullptr || bytes == 0U || bytes > maximum_reply_text_bytes) return false;
    const std::string_view text(json, bytes);
    if (!valid_utf8(text)) return false;
    const gui_forms::detail::JsonReadResult read = gui_forms::detail::read_json(text);
    if (!read || read.root.kind != gui_forms::detail::JsonKind::object) return false;
    state.success.structured_json.assign(text);
    return true;
}

bool ModelToolReply::image(const unsigned char* bytes, std::size_t count, const char* mime_type) {
    ModelToolReplyState& state = *state_;
    if (state.failed || bytes == nullptr || count == 0U || count > maximum_reply_image_bytes) return false;
    const std::string_view mime = mime_type == nullptr ? std::string_view("image/png") : view_of(mime_type);
    if (mime.size() < 7U || mime.size() > 64U || mime.substr(0U, 6U) != "image/" || !valid_utf8(mime)) return false;
    ToolImage image{};
    image.mime_type.assign(mime);
    image.bytes.resize(count);
    std::memcpy(image.bytes.data(), bytes, count);
    state.success.images.push_back(std::move(image));
    return true;
}

void ModelToolReply::fail(ModelToolError error, const char* message, const char* hint) {
    ModelToolReplyState& state = *state_;
    if (state.failed) return;
    state.failed = true;
    state.failure.error = error;
    const std::string_view message_text = view_of(message);
    const std::string_view hint_text = view_of(hint);
    state.failure.message.assign(valid_utf8(message_text) ? message_text : std::string_view("the tool failed"));
    if (valid_utf8(hint_text)) state.failure.hint.assign(hint_text);
}

ToolReply finished_reply(ModelToolReplyState& state) {
    if (state.failed) {
        ToolReply failed = ToolReply::failure(state.failure.error, std::move(state.failure.message),
                                              std::move(state.failure.hint));
        return failed;
    }
    ToolReply succeeded = ToolReply::success(std::move(state.success));
    return succeeded;
}

ToolReply call_tool_handler(const ToolEntry& entry, const ModelToolCall& call) {
    ModelToolReplyState state{};
    try {
        ModelToolReply reply(state);
        entry.handler(entry.context, call, reply);
    } catch (const std::exception& failure) {
        ToolReply failed = ToolReply::failure(ToolError::internal, entry.spec.name + " failed: " + failure.what());
        return failed;
    } catch (...) {
        ToolReply failed = ToolReply::failure(ToolError::internal, entry.spec.name + " failed");
        return failed;
    }
    ToolReply reply = finished_reply(state);
    return reply;
}

bool ModelInspectText::append(const char* utf8, std::size_t bytes) {
    if (bytes == 0U) return true;
    if (utf8 == nullptr) return false;
    const std::string_view text(utf8, bytes);
    if (!valid_utf8(text)) return false;
    (*state_).text.append(text);
    return true;
}

std::string call_inspect_section(const InspectSectionEntry& entry, const InspectRequest& request) {
    ModelInspectTextState state{};
    try {
        ModelInspectText text(state);
        entry.handler(entry.context, request, text);
    } catch (const std::exception& failure) {
        state.text += "(the section failed: ";
        state.text += failure.what();
        state.text += ")\n";
    } catch (...) {
        state.text += "(the section failed)\n";
    }
    std::string text = std::move(state.text);
    return text;
}

// ---- the public calls --------------------------------------------------------------

ModelToolRegistration register_model_tool(const ModelToolSpec& spec, ModelToolHandler handler, void* context) {
    ToolSpec copied{};
    copied.name.assign(view_of(spec.name));
    copied.title.assign(view_of(spec.title));
    copied.description.assign(view_of(spec.description));
    copied.input_schema.assign(view_of(spec.input_schema));
    copied.access = spec.access;
    copied.destructive = spec.destructive;
    copied.thread = spec.thread;
    copied.settles = spec.settles;
    const ModelToolRegistration added = model_tool_registry().add(std::move(copied), handler, context);
    return added;
}

bool unregister_model_tool(const char* name) {
    const bool removed = model_tool_registry().remove(view_of(name));
    return removed;
}

ModelToolRegistration register_model_inspect_section(const char* name, const char* description,
                                                     ModelInspectHandler handler, void* context) {
    const ModelToolRegistration added = model_tool_registry().add_inspect_section(
        std::string(view_of(name)), std::string(view_of(description)), handler, context);
    return added;
}

bool unregister_model_inspect_section(const char* name) {
    const bool removed = model_tool_registry().remove_inspect_section(view_of(name));
    return removed;
}

} // namespace gui_forms::automation
