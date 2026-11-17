/* Kit part: a window's rules (GF_TABLE_RULES) on a created window: an
 * interaction trigger with SET, TOGGLE and CALLBACK actions, a negated
 * condition, consume, duplicates, refusals (and that a refused callback is
 * not released), a stale rule when a named object is disposed, freed tokens,
 * and the rules ending with their window. Key and mouse triggers need a
 * host's input and are covered by the C++ suite (tests/rules_tests.cpp). */

#include "gf_conformance_internal.h"

#include <string.h>

static gf_value boolean_value(uint32_t value) {
    gf_value made;
    memset(&made, 0, sizeof(made));
    made.struct_size = (uint32_t)sizeof(made);
    made.kind = GF_VALUE_KIND_BOOLEAN;
    made.boolean = value;
    return made;
}

/* Whether the check box's checked property is expected (0 or 1). */
static int checked_is(gf_kit* kit, gf_object check, uint32_t expected) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    char text[16];
    uint64_t required = 0u;
    if ((*kit).properties.get(check, gf_kit_text("checked"), &value, text, sizeof(text), &required) !=
        GF_STATUS_OK) {
        return 0;
    }
    return value.kind == GF_VALUE_KIND_BOOLEAN && value.boolean == expected;
}

static gf_status set_checked(gf_kit* kit, gf_object check, uint32_t checked) {
    const gf_value value = boolean_value(checked);
    return (*kit).properties.set(check, gf_kit_text("checked"), &value);
}

static gf_rule_action blank_action(gf_rule_action_kind kind) {
    gf_rule_action made;
    memset(&made, 0, sizeof(made));
    made.struct_size = (uint32_t)sizeof(made);
    made.kind = kind;
    return made;
}

/* A rule on source's interaction name running actions. */
static gf_rule interaction_rule(gf_object source, const char* name, const gf_rule_action* actions,
                                uint64_t action_count) {
    gf_rule made;
    memset(&made, 0, sizeof(made));
    made.struct_size = (uint32_t)sizeof(made);
    made.trigger = GF_RULE_TRIGGER_INTERACTION;
    made.source = source;
    made.interaction = gf_kit_text(name);
    made.actions = actions;
    made.action_count = action_count;
    return made;
}

static gf_rule_state state_of(gf_kit* kit, gf_rule_token token) {
    gf_rule_state state = 0u;
    if ((*kit).rules.state(token, &state) != GF_STATUS_OK) {
        return 0u;
    }
    return state;
}

static void refusals(gf_kit* kit, gf_object window, gf_object first, gf_object second) {
    gf_kit_recorder recorder;
    memset(&recorder, 0, sizeof(recorder));
    recorder.kit = kit;
    const gf_callback callback = gf_kit_recording_callback(&recorder);
    gf_rule_action call = blank_action(GF_RULE_ACTION_CALLBACK);
    call.callback = &callback;
    gf_rule_token token = 7u;

    gf_rule rule = interaction_rule(first, "checked_changed", &call, 1u);
    rule.flags = 2u;
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &token), GF_STATUS_INVALID_ARGUMENT, "an unknown flag is refused");
    rule = interaction_rule(first, "checked_changed", &call, 0u);
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &token), GF_STATUS_INVALID_ARGUMENT, "a rule has an action");
    rule = interaction_rule(first, "no_such_interaction", &call, 1u);
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &token), GF_STATUS_NOT_FOUND,
                  "an interaction the source does not raise is refused");
    rule = interaction_rule(first, "checked_changed", &call, 1u);
    gf_kit_status(kit, (*kit).rules.add(first, &rule, &token), GF_STATUS_WRONG_HANDLE_KIND,
                  "rules are added to a window");
    rule.trigger = GF_RULE_TRIGGER_KEY_PRESSED;
    rule.source = 0u;
    rule.interaction = gf_kit_text("");
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &token), GF_STATUS_INVALID_ARGUMENT,
                  "a key trigger names its key");

    gf_rule_condition conditions[9];
    memset(conditions, 0, sizeof(conditions));
    for (int index = 0; index < 9; ++index) {
        conditions[index].struct_size = (uint32_t)sizeof(gf_rule_condition);
        conditions[index].kind = GF_RULE_CONDITION_WINDOW_ACTIVE;
    }
    rule = interaction_rule(first, "checked_changed", &call, 1u);
    rule.conditions = conditions;
    rule.condition_count = 9u;
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &token), GF_STATUS_LIMIT_EXCEEDED,
                  "a rule has at most eight conditions");
    conditions[0].reserved = 1u;
    rule.condition_count = 1u;
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &token), GF_STATUS_INVALID_ARGUMENT,
                  "a condition's reserved field is zero");

    gf_rule_action toggle_text = blank_action(GF_RULE_ACTION_TOGGLE);
    toggle_text.target = second;
    toggle_text.property = gf_kit_text("text");
    rule = interaction_rule(first, "checked_changed", &toggle_text, 1u);
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &token), GF_STATUS_INVALID_ARGUMENT,
                  "only a boolean property is toggled");
    toggle_text.property = gf_kit_text("no_such_property");
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &token), GF_STATUS_NOT_FOUND,
                  "a property the target lacks is refused");

    /* A callback action built before a refused action is not taken (N34). */
    gf_rule_action pair[2];
    pair[0] = call;
    pair[1] = blank_action(GF_RULE_ACTION_SET);
    pair[1].target = second;
    pair[1].property = gf_kit_text("no_such_property");
    gf_value flag = boolean_value(1u);
    pair[1].value = &flag;
    rule = interaction_rule(first, "checked_changed", pair, 2u);
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &token), GF_STATUS_NOT_FOUND,
                  "a callback action before a refused action is refused with it");
    /* A SET action's value is converted by add (N29). */
    gf_value point;
    memset(&point, 0, sizeof(point));
    point.struct_size = (uint32_t)sizeof(point);
    point.kind = GF_VALUE_KIND_POINT;
    pair[1].property = gf_kit_text("checked");
    pair[1].value = &point;
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &token), GF_STATUS_INVALID_ARGUMENT,
                  "a set action whose value does not convert to its property is refused by add");
    /* Arrays repeat their first element's struct_size (N27). */
    gf_rule_action mixed[2];
    mixed[0] = call;
    mixed[1] = call;
    mixed[1].struct_size = (uint32_t)sizeof(gf_rule_action) + 8u;
    rule = interaction_rule(first, "checked_changed", mixed, 2u);
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &token), GF_STATUS_INVALID_ARGUMENT,
                  "every action repeats the first action's struct_size");

    gf_kit_check(kit, token == 7u, "a refused add leaves its output");
    gf_kit_check(kit, recorder.invoked == 0u && recorder.released == 0u,
                 "a refused add neither calls nor releases its callback");
}

/* Actions of a later minor's larger size: the library steps by the first
 * action's struct_size, not its own sizeof (N27). */
typedef struct wide_action {
    gf_rule_action action;
    uint64_t tail;
} wide_action;

static void wide_actions(gf_kit* kit, gf_object window, gf_object first, gf_object second) {
    const gf_value yes = boolean_value(1u);
    const gf_value no = boolean_value(0u);
    wide_action actions[2];
    memset(actions, 0, sizeof(actions));
    actions[0].action = blank_action(GF_RULE_ACTION_SET);
    actions[0].action.struct_size = (uint32_t)sizeof(wide_action);
    actions[0].action.target = second;
    actions[0].action.property = gf_kit_text("checked");
    actions[0].action.value = &no;
    actions[0].tail = 0xFFFFFFFFFFFFFFFFull;
    actions[1] = actions[0];
    actions[1].action.value = &yes;
    /* A condition no other rule here has, so the trigger is not a duplicate. */
    gf_rule_condition second_enabled;
    memset(&second_enabled, 0, sizeof(second_enabled));
    second_enabled.struct_size = (uint32_t)sizeof(second_enabled);
    second_enabled.kind = GF_RULE_CONDITION_ENABLED;
    second_enabled.object = second;
    gf_rule rule = interaction_rule(first, "checked_changed", &actions[0].action, 2u);
    rule.conditions = &second_enabled;
    rule.condition_count = 1u;
    gf_rule_token token = 0u;
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &token), GF_STATUS_OK,
                  "actions larger than this library's record are admitted");
    gf_kit_check(kit, token != 0u, "the wide rule has a token");
    if (token != 0u) {
        gf_kit_status(kit, (*kit).rules.remove(token), GF_STATUS_OK, "remove the wide rule");
    }
}

/* The drag levels and the keyboard drag action (since 1.2): with no drag
 * running, dragging does not hold and its negation does; beginning a
 * keyboard drag on a control that declares none changes nothing. Kinds the
 * C ABI does not have (the C++-only 10 and 11, an action past 6) are
 * refused. */
static void drag_rules(gf_kit* kit, gf_object window, gf_object first, gf_object second) {
    gf_kit_recorder held;
    memset(&held, 0, sizeof(held));
    held.kit = kit;
    gf_kit_recorder idle;
    memset(&idle, 0, sizeof(idle));
    idle.kit = kit;
    const gf_callback held_callback = gf_kit_recording_callback(&held);
    const gf_callback idle_callback = gf_kit_recording_callback(&idle);

    gf_rule_condition dragging;
    memset(&dragging, 0, sizeof(dragging));
    dragging.struct_size = (uint32_t)sizeof(dragging);
    dragging.kind = GF_RULE_CONDITION_DRAGGING;
    dragging.object = second;
    gf_rule_action while_dragging[2];
    while_dragging[0] = blank_action(GF_RULE_ACTION_BEGIN_KEYBOARD_DRAG);
    while_dragging[0].target = second;
    while_dragging[1] = blank_action(GF_RULE_ACTION_CALLBACK);
    while_dragging[1].callback = &held_callback;
    gf_rule rule = interaction_rule(first, "checked_changed", while_dragging, 2u);
    rule.conditions = &dragging;
    rule.condition_count = 1u;
    gf_rule_token dragging_token = 0u;
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &dragging_token), GF_STATUS_OK,
                  "add a rule on the dragging level with the keyboard drag action");

    gf_rule_condition not_dragging = dragging;
    not_dragging.negated = 1u;
    gf_rule_condition not_candidate = dragging;
    not_candidate.kind = GF_RULE_CONDITION_DROP_CANDIDATE;
    not_candidate.negated = 1u;
    gf_rule_condition idle_levels[2];
    idle_levels[0] = not_dragging;
    idle_levels[1] = not_candidate;
    gf_rule_action when_idle = blank_action(GF_RULE_ACTION_CALLBACK);
    when_idle.callback = &idle_callback;
    rule = interaction_rule(first, "checked_changed", &when_idle, 1u);
    rule.conditions = idle_levels;
    rule.condition_count = 2u;
    gf_rule_token idle_token = 0u;
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &idle_token), GF_STATUS_OK,
                  "add a rule on the negated drag levels");

    /* Two changes of the first box, whichever state it is in now. */
    const uint32_t was_checked = checked_is(kit, first, 1u) ? 1u : 0u;
    gf_kit_status(kit, set_checked(kit, first, 1u - was_checked), GF_STATUS_OK,
                  "toggle the first box for the drag rules");
    gf_kit_status(kit, set_checked(kit, first, was_checked), GF_STATUS_OK, "toggle the first box back");
    gf_kit_check(kit, held.invoked == 0u, "dragging does not hold with no drag running");
    gf_kit_check(kit, idle.invoked == 2u, "the negated drag levels hold with no drag running");

    gf_rule_condition keyed = dragging;
    keyed.physical_key = 4u;
    rule = interaction_rule(first, "checked_changed", &when_idle, 1u);
    rule.conditions = &keyed;
    rule.condition_count = 1u;
    gf_rule_token refused = 7u;
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &refused), GF_STATUS_INVALID_ARGUMENT,
                  "a drag condition names a control only");
    keyed = dragging;
    keyed.kind = 10u;
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &refused), GF_STATUS_INVALID_ARGUMENT,
                  "a C++-only condition kind is refused");
    gf_rule_action unknown = blank_action(GF_RULE_ACTION_BEGIN_KEYBOARD_DRAG + 1u);
    unknown.target = second;
    rule = interaction_rule(first, "checked_changed", &unknown, 1u);
    gf_kit_status(kit, (*kit).rules.add(window, &rule, &refused), GF_STATUS_INVALID_ARGUMENT,
                  "an action kind past the vocabulary is refused");

    gf_kit_status(kit, (*kit).rules.remove(dragging_token), GF_STATUS_OK, "remove the dragging rule");
    gf_kit_status(kit, (*kit).rules.remove(idle_token), GF_STATUS_OK, "remove the idle rule");
    gf_kit_check(kit, held.released == 1u && idle.released == 1u, "removing the drag rules releases their callbacks");
}

void gf_kit_rules(gf_kit* kit) {
    const gf_object root = gf_kit_create(kit, "panel", "kit.rules.root");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "make the rules window");
    const gf_object first = gf_kit_create(kit, "check_box", "kit.rules.first");
    const gf_object second = gf_kit_create(kit, "check_box", "kit.rules.second");
    const gf_object third = gf_kit_create(kit, "check_box", "kit.rules.third");

    /* first's checked_changed sets second's checked. */
    const gf_value yes = boolean_value(1u);
    gf_rule_action set_second = blank_action(GF_RULE_ACTION_SET);
    set_second.target = second;
    set_second.property = gf_kit_text("checked");
    set_second.value = &yes;
    gf_rule set_rule = interaction_rule(first, "checked_changed", &set_second, 1u);
    gf_rule_token set_token = 0u;
    gf_kit_status(kit, (*kit).rules.add(window, &set_rule, &set_token), GF_STATUS_OK, "add a set rule");
    gf_kit_check(kit, set_token != 0u && state_of(kit, set_token) == GF_RULE_STATE_ARMED, "the rule is armed");
    gf_rule_token again = 7u;
    gf_kit_status(kit, (*kit).rules.add(window, &set_rule, &again), GF_STATUS_INVALID_STATE,
                  "the same trigger and conditions are refused");
    gf_kit_check(kit, again == 7u, "a duplicate leaves its output");

    /* first's checked_changed toggles third's checked while third is disabled. */
    gf_rule_condition third_disabled;
    memset(&third_disabled, 0, sizeof(third_disabled));
    third_disabled.struct_size = (uint32_t)sizeof(third_disabled);
    third_disabled.kind = GF_RULE_CONDITION_ENABLED;
    third_disabled.object = third;
    third_disabled.negated = 1u;
    gf_rule_action toggle_third = blank_action(GF_RULE_ACTION_TOGGLE);
    toggle_third.target = third;
    toggle_third.property = gf_kit_text("checked");
    gf_rule toggle_rule = interaction_rule(first, "checked_changed", &toggle_third, 1u);
    toggle_rule.conditions = &third_disabled;
    toggle_rule.condition_count = 1u;
    gf_rule_token toggle_token = 0u;
    gf_kit_status(kit, (*kit).rules.add(window, &toggle_rule, &toggle_token), GF_STATUS_OK,
                  "a rule with the same trigger and another condition set is admitted");

    /* first's checked_changed calls back. */
    gf_kit_recorder recorder;
    memset(&recorder, 0, sizeof(recorder));
    recorder.kit = kit;
    const gf_callback callback = gf_kit_recording_callback(&recorder);
    gf_rule_action call = blank_action(GF_RULE_ACTION_CALLBACK);
    call.callback = &callback;
    gf_rule call_rule = interaction_rule(first, "checked_changed", &call, 1u);
    gf_rule_condition active;
    memset(&active, 0, sizeof(active));
    active.struct_size = (uint32_t)sizeof(active);
    active.kind = GF_RULE_CONDITION_WINDOW_ACTIVE;
    active.negated = 0u; /* a window starts active */
    call_rule.conditions = &active;
    call_rule.condition_count = 1u;
    gf_rule_token call_token = 0u;
    gf_kit_status(kit, (*kit).rules.add(window, &call_rule, &call_token), GF_STATUS_OK, "add a callback rule");

    gf_kit_status(kit, set_checked(kit, first, 1u), GF_STATUS_OK, "check the first box");
    gf_kit_check(kit, checked_is(kit, second, 1u), "the set rule checked the second box");
    gf_kit_check(kit, checked_is(kit, third, 0u), "the toggle rule's condition did not hold");
    gf_kit_check(kit, recorder.invoked == 1u && recorder.last_sender == first &&
                          strcmp(recorder.last_name, "interaction") == 0,
                 "the callback ran with the trigger's source and kind");

    const gf_value no = boolean_value(0u);
    gf_kit_status(kit, (*kit).properties.set(third, gf_kit_text("enabled"), &no), GF_STATUS_OK, "disable the third box");
    gf_kit_status(kit, set_checked(kit, first, 0u), GF_STATUS_OK, "uncheck the first box");
    gf_kit_check(kit, checked_is(kit, third, 1u), "the toggle rule fired once its condition held");
    gf_kit_check(kit, recorder.invoked == 2u, "the callback ran again");

    /* Consume: on second's checked_changed, the first rule ends the occurrence. */
    gf_kit_recorder consumer;
    memset(&consumer, 0, sizeof(consumer));
    consumer.kit = kit;
    gf_kit_recorder later;
    memset(&later, 0, sizeof(later));
    later.kit = kit;
    const gf_callback consumer_callback = gf_kit_recording_callback(&consumer);
    const gf_callback later_callback = gf_kit_recording_callback(&later);
    gf_rule_action consume_call = blank_action(GF_RULE_ACTION_CALLBACK);
    consume_call.callback = &consumer_callback;
    gf_rule consume_rule = interaction_rule(second, "checked_changed", &consume_call, 1u);
    consume_rule.flags = GF_RULE_FLAGS_CONSUME;
    gf_rule_token consume_token = 0u;
    gf_kit_status(kit, (*kit).rules.add(window, &consume_rule, &consume_token), GF_STATUS_OK, "add a consuming rule");
    gf_rule_action later_call = blank_action(GF_RULE_ACTION_CALLBACK);
    later_call.callback = &later_callback;
    gf_rule later_rule = interaction_rule(second, "checked_changed", &later_call, 1u);
    later_rule.conditions = &active;
    later_rule.condition_count = 1u;
    gf_rule_token later_token = 0u;
    gf_kit_status(kit, (*kit).rules.add(window, &later_rule, &later_token), GF_STATUS_OK, "add a later rule");
    gf_kit_status(kit, set_checked(kit, second, 0u), GF_STATUS_OK, "uncheck the second box");
    gf_kit_check(kit, consumer.invoked == 1u && later.invoked == 0u, "the consuming rule stopped the later one");

    gf_kit_status(kit, (*kit).rules.remove(consume_token), GF_STATUS_OK, "remove the consuming rule");
    gf_kit_check(kit, consumer.released == 1u, "a removed rule's callback is released");
    gf_kit_status(kit, (*kit).rules.remove(consume_token), GF_STATUS_STALE_HANDLE, "a freed token is stale");
    gf_rule_state freed = 0u;
    gf_kit_status(kit, (*kit).rules.state(consume_token, &freed), GF_STATUS_STALE_HANDLE, "a freed token has no state");
    gf_kit_status(kit, set_checked(kit, second, 1u), GF_STATUS_OK, "check the second box");
    gf_kit_check(kit, later.invoked == 1u, "with the consuming rule gone the later rule fires");

    refusals(kit, window, first, second);
    wide_actions(kit, window, first, second);
    drag_rules(kit, window, first, second);

    /* Disposing an object a rule names leaves the rule stale. */
    gf_kit_status(kit, (*kit).objects.dispose(third), GF_STATUS_OK, "dispose the third box");
    gf_kit_check(kit, state_of(kit, toggle_token) == GF_RULE_STATE_STALE, "the rule naming it is stale");
    gf_kit_check(kit, state_of(kit, set_token) == GF_RULE_STATE_ARMED, "the others stay armed");
    gf_kit_status(kit, (*kit).rules.remove(toggle_token), GF_STATUS_OK, "a stale rule's token is freed");

    /* The window's rules end with it. */
    gf_kit_status(kit, (*kit).objects.dispose(window), GF_STATUS_OK, "dispose the rules window");
    gf_kit_check(kit, state_of(kit, set_token) == GF_RULE_STATE_STALE, "a rule of a disposed window is stale");
    gf_kit_check(kit, recorder.released == 1u && later.released == 1u, "its callbacks are released");
    gf_kit_status(kit, (*kit).rules.remove(set_token), GF_STATUS_OK, "free the set rule's token");
    gf_kit_status(kit, (*kit).rules.remove(call_token), GF_STATUS_OK, "free the callback rule's token");
    gf_kit_status(kit, (*kit).rules.remove(later_token), GF_STATUS_OK, "free the later rule's token");

    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the rules window");
    gf_kit_status(kit, (*kit).objects.release(third), GF_STATUS_OK, "release the third box");
    gf_kit_status(kit, (*kit).objects.release(second), GF_STATUS_OK, "release the second box");
    gf_kit_status(kit, (*kit).objects.release(first), GF_STATUS_OK, "release the first box");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the rules root");
}
