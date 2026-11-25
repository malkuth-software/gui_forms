// GF_TABLE_AUTOMATION (since 1.6): the marks a program sets for the
// library's MCP automation (gui_forms/automation/marks/automation_marks.hpp)
// and the program's own model tools and inspect sections
// (gui_forms/automation/tools/model_tools.hpp). See gf_abi.h.
//
// A C tool is held by an adapter the registry calls through a named
// trampoline. The adapter counts its references: one for the registration
// and one for each running call. A call takes its reference only while the
// count is above zero, so once unregister drops the registration's
// reference no new call starts, and whoever drops the last reference calls
// release, once, after the last call returned. Adapters are never freed (a
// registry entry copied out before the unregister may still reach one, and
// find its count at zero); each is a few words per registration.

#include "abi_records.hpp"
#include "abi_tables.hpp"

#include "../failure/abi_failure.hpp"
#include "../registry/abi_registry.hpp"
#include "../windows/abi_window_handle.hpp"

#include "gui_forms/automation/marks/automation_marks.hpp"
#include "gui_forms/automation/tools/model_tools.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/window.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stxlib/threads.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::abi1 {
namespace {

using automation::ModelInspectRequest;
using automation::ModelInspectText;
using automation::ModelToolCall;
using automation::ModelToolError;
using automation::ModelToolRegistration;
using automation::ModelToolReply;
using automation::ModelToolSpec;

constexpr SlotIdentity capability_built_slot{GF_TABLE_AUTOMATION, 0U};
constexpr SlotIdentity set_window_drivable_slot{GF_TABLE_AUTOMATION, 1U};
constexpr SlotIdentity set_window_destructive_slot{GF_TABLE_AUTOMATION, 2U};
constexpr SlotIdentity set_action_destructive_slot{GF_TABLE_AUTOMATION, 3U};
constexpr SlotIdentity set_command_hidden_slot{GF_TABLE_AUTOMATION, 4U};
constexpr SlotIdentity require_user_step_slot{GF_TABLE_AUTOMATION, 5U};
constexpr SlotIdentity complete_user_step_slot{GF_TABLE_AUTOMATION, 6U};
constexpr SlotIdentity user_step_pending_slot{GF_TABLE_AUTOMATION, 7U};
constexpr SlotIdentity set_clipboard_readable_slot{GF_TABLE_AUTOMATION, 8U};
constexpr SlotIdentity set_identity_slot{GF_TABLE_AUTOMATION, 9U};
constexpr SlotIdentity register_tool_slot{GF_TABLE_AUTOMATION, 10U};
constexpr SlotIdentity unregister_tool_slot{GF_TABLE_AUTOMATION, 11U};
constexpr SlotIdentity reply_text_slot{GF_TABLE_AUTOMATION, 12U};
constexpr SlotIdentity reply_structured_slot{GF_TABLE_AUTOMATION, 13U};
constexpr SlotIdentity reply_image_slot{GF_TABLE_AUTOMATION, 14U};
constexpr SlotIdentity reply_fail_slot{GF_TABLE_AUTOMATION, 15U};
constexpr SlotIdentity register_section_slot{GF_TABLE_AUTOMATION, 16U};
constexpr SlotIdentity unregister_section_slot{GF_TABLE_AUTOMATION, 17U};
constexpr SlotIdentity inspect_append_slot{GF_TABLE_AUTOMATION, 18U};

// The 1.6 sizes of the records: every field through release.
constexpr std::uint32_t tool_size_1_6 = 104U;
constexpr std::uint32_t section_size_1_6 = 64U;
static_assert(sizeof(gf_automation_tool) == tool_size_1_6);
static_assert(sizeof(gf_automation_inspect_section) == section_size_1_6);

static_assert(GF_AUTOMATION_ACTION_FOCUS == static_cast<std::uint32_t>(SemanticAction::focus));
static_assert(GF_AUTOMATION_ACTION_PRESS == static_cast<std::uint32_t>(SemanticAction::press));
static_assert(GF_AUTOMATION_ACTION_SELECT == static_cast<std::uint32_t>(SemanticAction::select));
static_assert(GF_AUTOMATION_ACTION_INCREMENT == static_cast<std::uint32_t>(SemanticAction::increment));
static_assert(GF_AUTOMATION_ACTION_DECREMENT == static_cast<std::uint32_t>(SemanticAction::decrement));
static_assert(GF_AUTOMATION_ACTION_EXPAND == static_cast<std::uint32_t>(SemanticAction::expand));
static_assert(GF_AUTOMATION_ACTION_COLLAPSE == static_cast<std::uint32_t>(SemanticAction::collapse));
static_assert(GF_AUTOMATION_ACTION_SHOW_MENU == static_cast<std::uint32_t>(SemanticAction::show_menu));
static_assert(GF_AUTOMATION_ACTION_SET_VALUE == static_cast<std::uint32_t>(SemanticAction::set_value));
static_assert(GF_AUTOMATION_ACTION_CLOSE == static_cast<std::uint32_t>(SemanticAction::close));
static_assert(GF_AUTOMATION_TOOL_ERROR_PROTECTED_CONTENT ==
              static_cast<std::uint32_t>(ModelToolError::protected_content));
static_assert(GF_AUTOMATION_TOOL_ERROR_INTERNAL == static_cast<std::uint32_t>(ModelToolError::internal));
static_assert(GF_AUTOMATION_TOOL_ERROR_EXPECTATION_FAILED ==
              static_cast<std::uint32_t>(ModelToolError::expectation_failed));

constexpr gf_automation_tool_flags known_tool_flags =
    GF_AUTOMATION_TOOL_DESTRUCTIVE | GF_AUTOMATION_TOOL_ANY_THREAD | GF_AUTOMATION_TOOL_SETTLES;

gf_utf8_view view_of(std::string_view text) noexcept {
    const gf_utf8_view view{text.data(), static_cast<std::uint64_t>(text.size())};
    return view;
}

// The stable ID of `window`'s root; empty for none.
std::string_view window_id_of(const Window* window) noexcept {
    if (window == nullptr) return std::string_view{};
    const Control::Ptr root = (*window).root();
    if (!root) return std::string_view{};
    return (*root).stable_id().value();
}

gf_status registration_status(ModelToolRegistration registration, SlotIdentity slot) {
    switch (registration) {
    case ModelToolRegistration::added: return GF_STATUS_OK;
    case ModelToolRegistration::invalid_name: return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the name breaks the rule");
    case ModelToolRegistration::duplicate: return fail(GF_STATUS_INVALID_STATE, slot, "the name is taken");
    case ModelToolRegistration::full: return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "the registry is full");
    case ModelToolRegistration::invalid_spec:
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the record breaks its rules");
    }
    return fail(GF_STATUS_INTERNAL, slot, "an unknown registration answer");
}

// ---- the adapters -------------------------------------------------------------------

// References: the registration's and one for each running call.
struct AdapterReferences final {
    std::atomic<std::uint32_t> count{1U};
};

struct ToolAdapter final {
    std::string name{};
    void* context{};
    void(GF_CALL* call)(void* context, const gf_automation_tool_call* call){};
    void(GF_CALL* release)(void* context){};
    AdapterReferences references{};
    // Registered and not yet unregistered; guarded by Adapters::lock.
    bool registered{};
};

struct SectionAdapter final {
    std::string name{};
    void* context{};
    void(GF_CALL* inspect)(void* context, const gf_automation_inspect_call* call){};
    void(GF_CALL* release)(void* context){};
    AdapterReferences references{};
    bool registered{};
};

struct Adapters final {
    stxlib::Mutex lock{};
    std::vector<std::unique_ptr<ToolAdapter>> tools{};
    std::vector<std::unique_ptr<SectionAdapter>> sections{};
};

[[nodiscard]] Adapters& adapters() {
    // Never destroyed (as the fonts table's held families): a late call or
    // unregister never meets a destroyed lock.
    static Adapters* const held = new Adapters();
    return *held;
}

// Takes a call's reference unless the registration's is gone.
bool take_reference(AdapterReferences& references) noexcept {
    std::uint32_t seen = references.count.load();
    while (seen != 0U) {
        if (references.count.compare_exchange_weak(seen, seen + 1U)) return true;
    }
    return false;
}

// Drops a reference; true for the last.
bool drop_reference(AdapterReferences& references) noexcept {
    const bool last = references.count.fetch_sub(1U) == 1U;
    return last;
}

void release_tool(ToolAdapter& adapter) noexcept {
    if (adapter.release != nullptr) adapter.release(adapter.context);
}

void release_section(SectionAdapter& adapter) noexcept {
    if (adapter.release != nullptr) adapter.release(adapter.context);
}

// The registry's handler of every C tool.
void tool_trampoline(void* context, const ModelToolCall& call, ModelToolReply& reply) {
    ToolAdapter& adapter = *static_cast<ToolAdapter*>(context);
    if (!take_reference(adapter.references)) {
        reply.fail(ModelToolError::unknown_tool, "the tool was withdrawn", nullptr);
        return;
    }
    gf_automation_tool_call record{};
    record.struct_size = sizeof(gf_automation_tool_call);
    record.arguments_json = view_of(std::string_view(call.arguments_json, call.arguments_bytes));
    record.client_name = view_of(call.client_name == nullptr ? std::string_view{} : std::string_view(call.client_name));
    record.model_name = view_of(call.model_name == nullptr ? std::string_view{} : std::string_view(call.model_name));
    record.window_id = view_of(window_id_of(call.window));
    record.reply = &reply;
    adapter.call(adapter.context, &record);
    if (drop_reference(adapter.references)) release_tool(adapter);
}

// The registry's handler of every C inspect section.
void section_trampoline(void* context, const ModelInspectRequest& request, ModelInspectText& text) {
    SectionAdapter& adapter = *static_cast<SectionAdapter*>(context);
    if (!take_reference(adapter.references)) return;
    gf_automation_inspect_call record{};
    record.struct_size = sizeof(gf_automation_inspect_call);
    record.window_id = view_of(window_id_of(request.window));
    record.node_id = view_of(request.node == nullptr ? std::string_view{} : std::string_view((*request.node).stable_id));
    record.arguments_json = view_of(std::string_view(request.arguments_json, request.arguments_bytes));
    record.text = &text;
    adapter.inspect(adapter.context, &record);
    if (drop_reference(adapter.references)) release_section(adapter);
}

// ---- the marks ----------------------------------------------------------------------

gf_status capability_built_body(std::uint32_t* built) {
    if (built == nullptr) return fail(GF_STATUS_INVALID_ARGUMENT, capability_built_slot, "built is required");
    *built = automation::mcp_capability_built() ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_capability_built(std::uint32_t* built) noexcept {
    return call_slot(capability_built_slot, capability_built_body, built);
}

// The window `object` names, on the UI thread.
gf_status find_window(gf_object object, SlotIdentity slot, Window*& window) {
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) return entered;
    Component::Ptr found;
    const gf_status looked = registry().find_component(object, slot, found);
    if (looked != GF_STATUS_OK) return looked;
    const std::shared_ptr<WindowHandle> handle = std::dynamic_pointer_cast<WindowHandle>(found);
    if (handle == nullptr) return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, "a window is required");
    Window* const held = (*handle).window();
    if (!(*handle).is_alive() || held == nullptr) {
        return fail(GF_STATUS_DISPOSED, slot, "the window has been disposed or has closed");
    }
    window = held;
    return GF_STATUS_OK;
}

gf_status set_window_drivable_body(gf_object window, std::uint32_t drivable) {
    if (drivable > 1U) return fail(GF_STATUS_INVALID_ARGUMENT, set_window_drivable_slot, "drivable is 0 or 1");
    Window* found = nullptr;
    const gf_status looked = find_window(window, set_window_drivable_slot, found);
    if (looked != GF_STATUS_OK) return looked;
    automation::set_model_drivable(*found, drivable == 1U);
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_set_window_drivable(gf_object window, std::uint32_t drivable) noexcept {
    return call_slot(set_window_drivable_slot, set_window_drivable_body, window, drivable);
}

gf_status set_window_destructive_body(gf_object window, std::uint32_t destructive) {
    if (destructive > 1U) return fail(GF_STATUS_INVALID_ARGUMENT, set_window_destructive_slot, "destructive is 0 or 1");
    Window* found = nullptr;
    const gf_status looked = find_window(window, set_window_destructive_slot, found);
    if (looked != GF_STATUS_OK) return looked;
    automation::set_window_destructive(*found, destructive == 1U);
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_set_window_destructive(gf_object window, std::uint32_t destructive) noexcept {
    return call_slot(set_window_destructive_slot, set_window_destructive_body, window, destructive);
}

// A text view copied into a terminated string for the marks.
gf_status read_terminated(gf_utf8_view view, SlotIdentity slot, std::string& text) {
    std::string_view read{};
    const gf_status checked = read_text(view, slot, read);
    if (checked != GF_STATUS_OK) return checked;
    if (read.find('\0') != std::string_view::npos) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the text holds a NUL");
    }
    text.assign(read);
    return GF_STATUS_OK;
}

gf_status set_action_destructive_body(gf_utf8_view stable_id, gf_automation_action action,
                                      std::uint32_t destructive) {
    if (action >= static_cast<std::uint32_t>(SemanticAction::count) || destructive > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_action_destructive_slot, "an unknown action or flag");
    }
    std::string identity{};
    const gf_status read = read_terminated(stable_id, set_action_destructive_slot, identity);
    if (read != GF_STATUS_OK) return read;
    if (!automation::set_action_destructive(identity.c_str(), static_cast<SemanticAction>(action), destructive == 1U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_action_destructive_slot, "the stable id is empty or too long");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_set_action_destructive(gf_utf8_view stable_id, gf_automation_action action,
                                                    std::uint32_t destructive) noexcept {
    return call_slot(set_action_destructive_slot, set_action_destructive_body, stable_id, action, destructive);
}

gf_status set_command_hidden_body(gf_utf8_view stable_id, std::uint32_t hidden) {
    if (hidden > 1U) return fail(GF_STATUS_INVALID_ARGUMENT, set_command_hidden_slot, "hidden is 0 or 1");
    std::string identity{};
    const gf_status read = read_terminated(stable_id, set_command_hidden_slot, identity);
    if (read != GF_STATUS_OK) return read;
    if (!automation::set_command_hidden_from_model(identity.c_str(), hidden == 1U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_command_hidden_slot, "the stable id is empty or too long");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_set_command_hidden(gf_utf8_view stable_id, std::uint32_t hidden) noexcept {
    return call_slot(set_command_hidden_slot, set_command_hidden_body, stable_id, hidden);
}

gf_status require_user_step_body(gf_utf8_view stable_id, gf_utf8_view what) {
    std::string identity{};
    std::string step{};
    gf_status read = read_terminated(stable_id, require_user_step_slot, identity);
    if (read == GF_STATUS_OK) read = read_terminated(what, require_user_step_slot, step);
    if (read != GF_STATUS_OK) return read;
    if (!automation::require_user_step(identity.c_str(), step.c_str())) {
        return fail(GF_STATUS_INVALID_ARGUMENT, require_user_step_slot, "each text is 1 to 4096 bytes");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_require_user_step(gf_utf8_view stable_id, gf_utf8_view what) noexcept {
    return call_slot(require_user_step_slot, require_user_step_body, stable_id, what);
}

gf_status complete_user_step_body(gf_utf8_view stable_id) {
    std::string identity{};
    const gf_status read = read_terminated(stable_id, complete_user_step_slot, identity);
    if (read != GF_STATUS_OK) return read;
    automation::complete_user_step(identity.c_str());
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_complete_user_step(gf_utf8_view stable_id) noexcept {
    return call_slot(complete_user_step_slot, complete_user_step_body, stable_id);
}

gf_status user_step_pending_body(gf_utf8_view stable_id, std::uint32_t* pending) {
    if (pending == nullptr) return fail(GF_STATUS_INVALID_ARGUMENT, user_step_pending_slot, "pending is required");
    std::string identity{};
    const gf_status read = read_terminated(stable_id, user_step_pending_slot, identity);
    if (read != GF_STATUS_OK) return read;
    *pending = automation::user_step_pending(identity.c_str()) ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_user_step_pending(gf_utf8_view stable_id, std::uint32_t* pending) noexcept {
    return call_slot(user_step_pending_slot, user_step_pending_body, stable_id, pending);
}

gf_status set_clipboard_readable_body(std::uint32_t readable) {
    if (readable > 1U) return fail(GF_STATUS_INVALID_ARGUMENT, set_clipboard_readable_slot, "readable is 0 or 1");
    automation::set_clipboard_readable_by_model(readable == 1U);
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_set_clipboard_readable(std::uint32_t readable) noexcept {
    return call_slot(set_clipboard_readable_slot, set_clipboard_readable_body, readable);
}

gf_status set_identity_body(gf_utf8_view identity) {
    std::string text{};
    const gf_status read = read_terminated(identity, set_identity_slot, text);
    if (read != GF_STATUS_OK) return read;
    if (!automation::set_mcp_identity(text.c_str())) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_identity_slot, "the identity breaks the rule");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_set_identity(gf_utf8_view identity) noexcept {
    return call_slot(set_identity_slot, set_identity_body, identity);
}

// ---- the tools ----------------------------------------------------------------------

gf_status register_tool_body(const gf_automation_tool* tool) {
    if (tool == nullptr || (*tool).struct_size < tool_size_1_6 || (*tool).call == nullptr ||
        (*tool).reserved != 0U || ((*tool).flags & ~known_tool_flags) != 0U ||
        (*tool).access > GF_AUTOMATION_TOOL_ACTING) {
        return fail(GF_STATUS_INVALID_ARGUMENT, register_tool_slot, "the tool record breaks its rules");
    }
    std::string name{};
    std::string title{};
    std::string description{};
    std::string schema{};
    gf_status read = read_terminated((*tool).name, register_tool_slot, name);
    if (read == GF_STATUS_OK) read = read_terminated((*tool).title, register_tool_slot, title);
    if (read == GF_STATUS_OK) read = read_terminated((*tool).description, register_tool_slot, description);
    if (read == GF_STATUS_OK) read = read_terminated((*tool).input_schema, register_tool_slot, schema);
    if (read != GF_STATUS_OK) return read;
    std::unique_ptr<ToolAdapter> made = std::make_unique<ToolAdapter>();
    (*made).name = name;
    (*made).context = (*tool).context;
    (*made).call = (*tool).call;
    (*made).release = (*tool).release;
    ModelToolSpec spec{};
    spec.name = name.c_str();
    spec.title = title.c_str();
    spec.description = description.c_str();
    spec.input_schema = schema.c_str();
    spec.access = (*tool).access == GF_AUTOMATION_TOOL_ACTING ? automation::ModelToolAccess::acting
                                                              : automation::ModelToolAccess::read_only;
    spec.destructive = ((*tool).flags & GF_AUTOMATION_TOOL_DESTRUCTIVE) != 0U;
    spec.thread = ((*tool).flags & GF_AUTOMATION_TOOL_ANY_THREAD) != 0U ? automation::ModelToolThread::any
                                                                         : automation::ModelToolThread::ui;
    spec.settles = ((*tool).flags & GF_AUTOMATION_TOOL_SETTLES) != 0U;
    Adapters& held = adapters();
    const stxlib::ScopedLock lock(held.lock);
    ToolAdapter* const adapter = made.get();
    held.tools.push_back(std::move(made));
    const ModelToolRegistration added = automation::register_model_tool(spec, &tool_trampoline, adapter);
    if (added != ModelToolRegistration::added) {
        // Never called: the adapter is dropped without its release.
        held.tools.pop_back();
    } else {
        (*adapter).registered = true;
    }
    return registration_status(added, register_tool_slot);
}

gf_status GF_CALL automation_register_tool(const gf_automation_tool* tool) noexcept {
    return call_slot(register_tool_slot, register_tool_body, tool);
}

gf_status unregister_tool_body(gf_utf8_view name) {
    std::string text{};
    const gf_status read = read_terminated(name, unregister_tool_slot, text);
    if (read != GF_STATUS_OK) return read;
    ToolAdapter* adapter = nullptr;
    {
        Adapters& held = adapters();
        const stxlib::ScopedLock lock(held.lock);
        for (const std::unique_ptr<ToolAdapter>& candidate : held.tools) {
            if ((*candidate).registered && (*candidate).name == text) adapter = candidate.get();
        }
        if (adapter == nullptr || !automation::unregister_model_tool(text.c_str())) {
            return fail(GF_STATUS_NOT_FOUND, unregister_tool_slot, "no C tool has that name");
        }
        (*adapter).registered = false;
    }
    if (drop_reference((*adapter).references)) release_tool(*adapter);
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_unregister_tool(gf_utf8_view name) noexcept {
    return call_slot(unregister_tool_slot, unregister_tool_body, name);
}

gf_status reply_text_body(void* reply, gf_utf8_view text) {
    if (reply == nullptr) return fail(GF_STATUS_INVALID_ARGUMENT, reply_text_slot, "reply is a call's token");
    std::string_view read{};
    const gf_status checked = read_text(text, reply_text_slot, read);
    if (checked != GF_STATUS_OK) return checked;
    if (!(*static_cast<ModelToolReply*>(reply)).text(read.data(), read.size())) {
        return fail(GF_STATUS_INVALID_ARGUMENT, reply_text_slot, "the reply failed already or is full");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_reply_text(void* reply, gf_utf8_view text) noexcept {
    return call_slot(reply_text_slot, reply_text_body, reply, text);
}

gf_status reply_structured_body(void* reply, gf_utf8_view json) {
    if (reply == nullptr) return fail(GF_STATUS_INVALID_ARGUMENT, reply_structured_slot, "reply is a call's token");
    std::string_view read{};
    const gf_status checked = read_text(json, reply_structured_slot, read);
    if (checked != GF_STATUS_OK) return checked;
    if (!(*static_cast<ModelToolReply*>(reply)).structured(read.data(), read.size())) {
        return fail(GF_STATUS_INVALID_ARGUMENT, reply_structured_slot, "one JSON object, on a reply not failed");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_reply_structured(void* reply, gf_utf8_view json) noexcept {
    return call_slot(reply_structured_slot, reply_structured_body, reply, json);
}

gf_status reply_image_body(void* reply, const std::uint8_t* bytes, std::uint64_t count, gf_utf8_view mime_type) {
    if (reply == nullptr || bytes == nullptr || count == 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, reply_image_slot, "a call's token and the image's bytes");
    }
    std::string mime{};
    const gf_status read = read_terminated(mime_type, reply_image_slot, mime);
    if (read != GF_STATUS_OK) return read;
    if (!(*static_cast<ModelToolReply*>(reply))
             .image(bytes, static_cast<std::size_t>(count), mime.empty() ? nullptr : mime.c_str())) {
        return fail(GF_STATUS_INVALID_ARGUMENT, reply_image_slot, "an image/ type of at most 16 MiB");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_reply_image(void* reply, const std::uint8_t* bytes, std::uint64_t count,
                                         gf_utf8_view mime_type) noexcept {
    return call_slot(reply_image_slot, reply_image_body, reply, bytes, count, mime_type);
}

gf_status reply_fail_body(void* reply, gf_automation_tool_error error, gf_utf8_view message, gf_utf8_view hint) {
    if (reply == nullptr || error > GF_AUTOMATION_TOOL_ERROR_EXPECTATION_FAILED) {
        return fail(GF_STATUS_INVALID_ARGUMENT, reply_fail_slot, "a call's token and a known error");
    }
    std::string message_text{};
    std::string hint_text{};
    gf_status read = read_terminated(message, reply_fail_slot, message_text);
    if (read == GF_STATUS_OK) read = read_terminated(hint, reply_fail_slot, hint_text);
    if (read != GF_STATUS_OK) return read;
    (*static_cast<ModelToolReply*>(reply))
        .fail(static_cast<ModelToolError>(error), message_text.c_str(), hint_text.c_str());
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_reply_fail(void* reply, gf_automation_tool_error error, gf_utf8_view message,
                                        gf_utf8_view hint) noexcept {
    return call_slot(reply_fail_slot, reply_fail_body, reply, error, message, hint);
}

// ---- the inspect sections -----------------------------------------------------------

gf_status register_section_body(const gf_automation_inspect_section* section) {
    if (section == nullptr || (*section).struct_size < section_size_1_6 || (*section).inspect == nullptr ||
        (*section).reserved != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, register_section_slot, "the section record breaks its rules");
    }
    std::string name{};
    std::string description{};
    gf_status read = read_terminated((*section).name, register_section_slot, name);
    if (read == GF_STATUS_OK) read = read_terminated((*section).description, register_section_slot, description);
    if (read != GF_STATUS_OK) return read;
    std::unique_ptr<SectionAdapter> made = std::make_unique<SectionAdapter>();
    (*made).name = name;
    (*made).context = (*section).context;
    (*made).inspect = (*section).inspect;
    (*made).release = (*section).release;
    Adapters& held = adapters();
    const stxlib::ScopedLock lock(held.lock);
    SectionAdapter* const adapter = made.get();
    held.sections.push_back(std::move(made));
    const ModelToolRegistration added =
        automation::register_model_inspect_section(name.c_str(), description.c_str(), &section_trampoline, adapter);
    if (added != ModelToolRegistration::added) {
        held.sections.pop_back();
    } else {
        (*adapter).registered = true;
    }
    return registration_status(added, register_section_slot);
}

gf_status GF_CALL automation_register_inspect_section(const gf_automation_inspect_section* section) noexcept {
    return call_slot(register_section_slot, register_section_body, section);
}

gf_status unregister_section_body(gf_utf8_view name) {
    std::string text{};
    const gf_status read = read_terminated(name, unregister_section_slot, text);
    if (read != GF_STATUS_OK) return read;
    SectionAdapter* adapter = nullptr;
    {
        Adapters& held = adapters();
        const stxlib::ScopedLock lock(held.lock);
        for (const std::unique_ptr<SectionAdapter>& candidate : held.sections) {
            if ((*candidate).registered && (*candidate).name == text) adapter = candidate.get();
        }
        if (adapter == nullptr || !automation::unregister_model_inspect_section(text.c_str())) {
            return fail(GF_STATUS_NOT_FOUND, unregister_section_slot, "no C section has that name");
        }
        (*adapter).registered = false;
    }
    if (drop_reference((*adapter).references)) release_section(*adapter);
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_unregister_inspect_section(gf_utf8_view name) noexcept {
    return call_slot(unregister_section_slot, unregister_section_body, name);
}

gf_status inspect_append_body(void* text, gf_utf8_view utf8) {
    if (text == nullptr) return fail(GF_STATUS_INVALID_ARGUMENT, inspect_append_slot, "text is a request's token");
    std::string_view read{};
    const gf_status checked = read_text(utf8, inspect_append_slot, read);
    if (checked != GF_STATUS_OK) return checked;
    static_cast<void>((*static_cast<ModelInspectText*>(text)).append(read.data(), read.size()));
    return GF_STATUS_OK;
}

gf_status GF_CALL automation_inspect_append(void* text, gf_utf8_view utf8) noexcept {
    return call_slot(inspect_append_slot, inspect_append_body, text, utf8);
}

constexpr gf_automation_table table{
    .struct_size = sizeof(gf_automation_table),
    .abi_minor = GF_ABI_MINOR,
    .capability_built = &automation_capability_built,
    .set_window_drivable = &automation_set_window_drivable,
    .set_window_destructive = &automation_set_window_destructive,
    .set_action_destructive = &automation_set_action_destructive,
    .set_command_hidden = &automation_set_command_hidden,
    .require_user_step = &automation_require_user_step,
    .complete_user_step = &automation_complete_user_step,
    .user_step_pending = &automation_user_step_pending,
    .set_clipboard_readable = &automation_set_clipboard_readable,
    .set_identity = &automation_set_identity,
    .register_tool = &automation_register_tool,
    .unregister_tool = &automation_unregister_tool,
    .reply_text = &automation_reply_text,
    .reply_structured = &automation_reply_structured,
    .reply_image = &automation_reply_image,
    .reply_fail = &automation_reply_fail,
    .register_inspect_section = &automation_register_inspect_section,
    .unregister_inspect_section = &automation_unregister_inspect_section,
    .inspect_append = &automation_inspect_append,
};

} // namespace

const gf_automation_table& automation_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
