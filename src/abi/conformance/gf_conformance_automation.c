/* Kit part: GF_TABLE_AUTOMATION (since 1.6), the automation marks and a
 * program's own model tools and inspect sections. No client calls the
 * tools here (the automation core's tests do); the kit checks the
 * registrations, their refusals and release. */

#include "gf_conformance_internal.h"

#include <string.h>

typedef struct automation_counts {
    uint32_t calls;
    uint32_t releases;
} automation_counts;

static void GF_CALL count_call(void* context, const gf_automation_tool_call* call) {
    (void)call;
    ++(*(automation_counts*)context).calls;
}

static void GF_CALL count_release(void* context) {
    ++(*(automation_counts*)context).releases;
}

static void GF_CALL count_inspect(void* context, const gf_automation_inspect_call* call) {
    (void)call;
    ++(*(automation_counts*)context).calls;
}

static gf_automation_tool blank_tool(const char* name, automation_counts* counts) {
    gf_automation_tool tool;
    memset(&tool, 0, sizeof(tool));
    tool.struct_size = (uint32_t)sizeof(tool);
    tool.access = GF_AUTOMATION_TOOL_READ_ONLY;
    tool.name = gf_kit_text(name);
    tool.description = gf_kit_text("A conformance kit tool.");
    tool.context = counts;
    tool.call = &count_call;
    tool.release = &count_release;
    return tool;
}

static void marks(gf_kit* kit) {
    uint32_t built = 9u;
    gf_kit_status(kit, (*kit).automation.capability_built(&built), GF_STATUS_OK, "read whether MCP is built");
    gf_kit_check(kit, built == 0u || built == 1u, "capability_built answers 0 or 1");
    gf_kit_status(kit, (*kit).automation.capability_built(NULL), GF_STATUS_INVALID_ARGUMENT,
                  "capability_built needs an output");
    gf_kit_status(kit, (*kit).automation.set_action_destructive(gf_kit_text("kit.automation.erase"),
                                                                GF_AUTOMATION_ACTION_PRESS, 1u),
                  GF_STATUS_OK, "mark an action destructive");
    gf_kit_status(kit, (*kit).automation.set_action_destructive(gf_kit_text("kit.automation.erase"), 99u, 1u),
                  GF_STATUS_INVALID_ARGUMENT, "an unknown action is refused");
    gf_kit_status(kit, (*kit).automation.set_action_destructive(gf_kit_text(""), GF_AUTOMATION_ACTION_PRESS, 1u),
                  GF_STATUS_INVALID_ARGUMENT, "an empty stable id is refused");
    gf_kit_status(kit, (*kit).automation.set_action_destructive(gf_kit_text("kit.automation.erase"),
                                                                GF_AUTOMATION_ACTION_PRESS, 0u),
                  GF_STATUS_OK, "clear the mark");
    uint32_t pending = 9u;
    gf_kit_status(kit, (*kit).automation.require_user_step(gf_kit_text("kit.automation.pay"), gf_kit_text("pay")),
                  GF_STATUS_OK, "require a user step");
    gf_kit_status(kit, (*kit).automation.user_step_pending(gf_kit_text("kit.automation.pay"), &pending),
                  GF_STATUS_OK, "read the step");
    gf_kit_check(kit, pending == 1u, "the step waits for the user");
    gf_kit_status(kit, (*kit).automation.complete_user_step(gf_kit_text("kit.automation.pay")), GF_STATUS_OK,
                  "complete the step");
    gf_kit_status(kit, (*kit).automation.user_step_pending(gf_kit_text("kit.automation.pay"), &pending),
                  GF_STATUS_OK, "read the step again");
    gf_kit_check(kit, pending == 0u, "the step is done");
    gf_kit_status(kit, (*kit).automation.set_identity(gf_kit_text("Not An Identity")), GF_STATUS_INVALID_ARGUMENT,
                  "an identity breaking the rule is refused");
    gf_kit_status(kit, (*kit).automation.set_clipboard_readable(2u), GF_STATUS_INVALID_ARGUMENT,
                  "readable is 0 or 1");
    gf_kit_status(kit, (*kit).automation.set_clipboard_readable(0u), GF_STATUS_OK, "the clipboard stays unread");
}

static void tools(gf_kit* kit) {
    automation_counts counts;
    memset(&counts, 0, sizeof(counts));
    gf_automation_tool tool = blank_tool("kit.automation.echo", &counts);
    gf_kit_status(kit, (*kit).automation.register_tool(&tool), GF_STATUS_OK, "register a tool");
    gf_kit_status(kit, (*kit).automation.register_tool(&tool), GF_STATUS_INVALID_STATE, "a name is registered once");
    gf_automation_tool shadow = blank_tool("snapshot", &counts);
    gf_kit_status(kit, (*kit).automation.register_tool(&shadow), GF_STATUS_INVALID_STATE,
                  "a built-in tool's name is refused");
    gf_automation_tool off_thread = blank_tool("kit.automation.off", &counts);
    off_thread.access = GF_AUTOMATION_TOOL_ACTING;
    off_thread.flags = GF_AUTOMATION_TOOL_ANY_THREAD;
    gf_kit_status(kit, (*kit).automation.register_tool(&off_thread), GF_STATUS_INVALID_ARGUMENT,
                  "an acting tool runs on the UI thread");
    gf_automation_tool short_record = blank_tool("kit.automation.short", &counts);
    short_record.struct_size = 16u;
    gf_kit_status(kit, (*kit).automation.register_tool(&short_record), GF_STATUS_INVALID_ARGUMENT,
                  "a short tool record is refused");
    gf_automation_tool no_call = blank_tool("kit.automation.silent", &counts);
    no_call.call = NULL;
    gf_kit_status(kit, (*kit).automation.register_tool(&no_call), GF_STATUS_INVALID_ARGUMENT,
                  "a tool needs its call");
    gf_kit_check(kit, counts.releases == 0u, "refused registrations are not released");
    gf_kit_status(kit, (*kit).automation.unregister_tool(gf_kit_text("kit.automation.echo")), GF_STATUS_OK,
                  "unregister the tool");
    gf_kit_check(kit, counts.releases == 1u && counts.calls == 0u, "the tool is released once");
    gf_kit_status(kit, (*kit).automation.unregister_tool(gf_kit_text("kit.automation.echo")), GF_STATUS_NOT_FOUND,
                  "an unregistered tool is not found");
    gf_kit_status(kit, (*kit).automation.reply_text(NULL, gf_kit_text("x")), GF_STATUS_INVALID_ARGUMENT,
                  "a reply needs a call's token");

    gf_automation_inspect_section section;
    memset(&section, 0, sizeof(section));
    section.struct_size = (uint32_t)sizeof(section);
    section.name = gf_kit_text("kit.automation.note");
    section.description = gf_kit_text("A conformance kit section.");
    section.context = &counts;
    section.inspect = &count_inspect;
    section.release = &count_release;
    gf_kit_status(kit, (*kit).automation.register_inspect_section(&section), GF_STATUS_OK,
                  "register an inspect section");
    gf_kit_status(kit, (*kit).automation.register_inspect_section(&section), GF_STATUS_INVALID_STATE,
                  "a section name is registered once");
    gf_kit_status(kit, (*kit).automation.unregister_inspect_section(gf_kit_text("kit.automation.note")),
                  GF_STATUS_OK, "unregister the section");
    gf_kit_check(kit, counts.releases == 2u, "the section is released once");
    gf_kit_status(kit, (*kit).automation.inspect_append(NULL, gf_kit_text("x")), GF_STATUS_INVALID_ARGUMENT,
                  "inspect_append needs a request's token");
}

void gf_kit_automation(gf_kit* kit) {
    marks(kit);
    tools(kit);
}
