#include "linux_accessibility.hpp"
#include <atk/atk.h>
#include <atk-bridge.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <locale>
#include <map>
#include <optional>
#include <poll.h>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
namespace gui_forms::host::linux_detail {
namespace {
// Every function in this file runs on the UI thread: ATK entry points are
// called by the AT-SPI bridge from the default GLib main context, which the
// host iterates there (see linux_accessibility.hpp).

// A published object's identity: the runtime identity of the control that
// publishes it and, for a virtual child (is_virtual_semantic_runtime_id),
// the child's stable identity (empty for the control's own node). Controls
// that show one model repeat their virtual children's identities, so a
// virtual child's identity alone does not name it. Unique within one
// window's tree.
struct NodeKey final {
    std::uint64_t control{};
    std::string virtual_child;
};
bool operator<(const NodeKey& left, const NodeKey& right) {
    if (left.control != right.control) return left.control < right.control;
    return left.virtual_child < right.virtual_child;
}

// The interfaces an object type implements, as bits of its type flags.
constexpr unsigned action_interface = 1U;
constexpr unsigned value_interface = 2U;
constexpr unsigned text_interface = 4U;

struct NodeData {
    LinuxAccessibility::Impl* owner = nullptr;
    SemanticNode node;
    NodeKey key;
    // The node's actions in published order: the default action first.
    std::vector<SemanticAction> actions;
    // The default action's key binding (action_keybinding), from the node's
    // access key; empty without one. Rebuilt with the node.
    std::string keybinding;
    AtkObject* parent = nullptr;
    std::vector<AtkObject*> children;
    int index = 0;
    bool application = false;
    bool frame = false;
    bool live = true;
    unsigned flags = 0;
};
struct NativeAccessible { AtkObject parent; NodeData* data; };
NodeData& data(gpointer object) { return *(*static_cast<NativeAccessible*>(object)).data; }
AtkObject* application_root = nullptr;
std::size_t bridge_count = 0;
AtkObject* root_object() { return application_root; }
const gchar* toolkit_name() { return "GUI.Forms"; }
const gchar* toolkit_version() { return "0.2"; }

AtkRole native_role(SemanticRole role) {
    switch (role) {
    case SemanticRole::button: return ATK_ROLE_PUSH_BUTTON;
    case SemanticRole::check_box: case SemanticRole::check_list_item: return ATK_ROLE_CHECK_BOX;
    case SemanticRole::radio_button: return ATK_ROLE_RADIO_BUTTON;
    case SemanticRole::toolbar: return ATK_ROLE_TOOL_BAR;
    case SemanticRole::radio_group: return ATK_ROLE_PANEL;
    case SemanticRole::text_box: return ATK_ROLE_ENTRY;
    case SemanticRole::numeric_field: return ATK_ROLE_SPIN_BUTTON;
    case SemanticRole::list: return ATK_ROLE_LIST;
    case SemanticRole::list_item: return ATK_ROLE_LIST_ITEM;
    case SemanticRole::combo_box: return ATK_ROLE_COMBO_BOX;
    case SemanticRole::slider: return ATK_ROLE_SLIDER;
    case SemanticRole::scroll_bar: return ATK_ROLE_SCROLL_BAR;
    case SemanticRole::progress_bar: return ATK_ROLE_PROGRESS_BAR;
    case SemanticRole::static_text: return ATK_ROLE_LABEL;
    case SemanticRole::image: return ATK_ROLE_IMAGE;
    case SemanticRole::tab: return ATK_ROLE_PAGE_TAB;
    case SemanticRole::tab_group: return ATK_ROLE_PAGE_TAB_LIST;
    case SemanticRole::menu_bar: return ATK_ROLE_MENU_BAR;
    case SemanticRole::menu: return ATK_ROLE_MENU;
    case SemanticRole::menu_item: case SemanticRole::menu_bar_item: return ATK_ROLE_MENU_ITEM;
    case SemanticRole::tool_tip: return ATK_ROLE_TOOL_TIP;
    case SemanticRole::separator: return ATK_ROLE_SEPARATOR;
    case SemanticRole::link: return ATK_ROLE_LINK;
    // ATK's push-button-menu role is too recent for the ATK versions CI
    // builds against; a push button with the has-popup state says the same.
    case SemanticRole::split_button: return ATK_ROLE_PUSH_BUTTON;
    case SemanticRole::menu_button: return ATK_ROLE_PUSH_BUTTON;
    // A tool panel is a panel; a tab's content is a scroll pane, as GTK and
    // the ARIA core mapping publish a tab panel on ATK.
    case SemanticRole::pane: return ATK_ROLE_PANEL;
    case SemanticRole::tab_panel: return ATK_ROLE_SCROLL_PANE;
    // The stage A vocabulary (12.13). A tree grid is ATK's tree table; ATK
    // has no live-region role, so a live region is a panel with the
    // "live" and "container-live" attributes, as browsers publish one.
    case SemanticRole::tree: return ATK_ROLE_TREE;
    case SemanticRole::tree_item: return ATK_ROLE_TREE_ITEM;
    case SemanticRole::table: return ATK_ROLE_TABLE;
    case SemanticRole::row: return ATK_ROLE_TABLE_ROW;
    case SemanticRole::cell: return ATK_ROLE_TABLE_CELL;
    case SemanticRole::column_header: return ATK_ROLE_COLUMN_HEADER;
    case SemanticRole::row_header: return ATK_ROLE_ROW_HEADER;
    case SemanticRole::tree_grid: return ATK_ROLE_TREE_TABLE;
    case SemanticRole::dialog: return ATK_ROLE_DIALOG;
    case SemanticRole::status_bar: return ATK_ROLE_STATUSBAR;
    case SemanticRole::live_region: return ATK_ROLE_PANEL;
    case SemanticRole::meter: return ATK_ROLE_LEVEL_BAR;
    case SemanticRole::split_pane: return ATK_ROLE_SPLIT_PANE;
    case SemanticRole::date_picker: return ATK_ROLE_DATE_EDITOR;
    case SemanticRole::calendar: return ATK_ROLE_CALENDAR;
    // A calendar's day, as GTK publishes one.
    case SemanticRole::date_cell: return ATK_ROLE_TABLE_CELL;
    // A property grid publishes rows, not the Table interface, so its rows
    // are list items of a list; a category is a grouping.
    case SemanticRole::property_grid: return ATK_ROLE_LIST;
    case SemanticRole::property_group: return ATK_ROLE_GROUPING;
    case SemanticRole::property_row: return ATK_ROLE_LIST_ITEM;
    // The ARIA core mapping publishes a group as a panel.
    case SemanticRole::group: return ATK_ROLE_PANEL;
    case SemanticRole::generic: return ATK_ROLE_PANEL;
    case SemanticRole::count: return ATK_ROLE_PANEL;
    }
    return ATK_ROLE_PANEL;
}

// The semantic states and the ATK states that publish them. The semantic
// enabled state is both ATK's enabled and sensitive, and visible is both
// visible and showing, as GTK publishes them.
struct StateMapping final { SemanticState source; AtkStateType target; };
constexpr StateMapping state_mappings[] = {
    {SemanticState::enabled, ATK_STATE_ENABLED}, {SemanticState::enabled, ATK_STATE_SENSITIVE},
    {SemanticState::visible, ATK_STATE_VISIBLE}, {SemanticState::visible, ATK_STATE_SHOWING},
    {SemanticState::focusable, ATK_STATE_FOCUSABLE}, {SemanticState::focused, ATK_STATE_FOCUSED},
    {SemanticState::selected, ATK_STATE_SELECTED}, {SemanticState::checked, ATK_STATE_CHECKED},
    {SemanticState::mixed, ATK_STATE_INDETERMINATE}, {SemanticState::expanded, ATK_STATE_EXPANDED},
    {SemanticState::busy, ATK_STATE_BUSY}, {SemanticState::read_only, ATK_STATE_READ_ONLY},
    {SemanticState::invalid, ATK_STATE_INVALID_ENTRY},
    {SemanticState::checkable, ATK_STATE_CHECKABLE}, {SemanticState::multiselectable, ATK_STATE_MULTISELECTABLE}};

// The ATK states of a live node, one bit per AtkStateType.
using StateBits = std::uint64_t;
static_assert(static_cast<int>(ATK_STATE_LAST_DEFINED) <= 64, "every ATK state fits one bit of StateBits");

StateBits state_bit(AtkStateType state) {
    const StateBits bit = StateBits{1} << static_cast<unsigned>(state);
    return bit;
}

bool has_action(const std::vector<SemanticAction>& actions, SemanticAction wanted) {
    const bool found = std::find(actions.begin(), actions.end(), wanted) != actions.end();
    return found;
}

// The mapped semantic states, and the states ATK derives from the role and
// actions: editable and single- or multi-line for a text box, checkable for
// the check and radio roles, expandable for a node that expands or
// collapses, selectable for one that can be selected, has-popup, and the
// tab list's orientation.
StateBits state_bits(const SemanticNode& node) {
    StateBits bits = 0U;
    for (const StateMapping& mapping : state_mappings) {
        if (has_semantic_state(node.states, mapping.source)) bits |= state_bit(mapping.target);
    }
    if (node.role == SemanticRole::text_box && !has_semantic_state(node.states, SemanticState::read_only)) {
        bits |= state_bit(ATK_STATE_EDITABLE);
    }
    if (node.role == SemanticRole::text_box) {
        bits |= state_bit(node.multiline ? ATK_STATE_MULTI_LINE : ATK_STATE_SINGLE_LINE);
    }
    if (node.role == SemanticRole::check_box || node.role == SemanticRole::check_list_item ||
        node.role == SemanticRole::radio_button) {
        bits |= state_bit(ATK_STATE_CHECKABLE);
    }
    if (has_semantic_state(node.states, SemanticState::expanded) || has_action(node.actions, SemanticAction::expand) ||
        has_action(node.actions, SemanticAction::collapse)) {
        bits |= state_bit(ATK_STATE_EXPANDABLE);
    }
    if (has_action(node.actions, SemanticAction::select)) bits |= state_bit(ATK_STATE_SELECTABLE);
    if (node.has_popup) bits |= state_bit(ATK_STATE_HAS_POPUP);
    if (node.modal) bits |= state_bit(ATK_STATE_MODAL);
    if (node.orientation) {
        const bool row = *node.orientation == SemanticOrientation::horizontal;
        bits |= state_bit(row ? ATK_STATE_HORIZONTAL : ATK_STATE_VERTICAL);
    }
    return bits;
}

// Whether `action` activates a node (press, select, expand, collapse, show
// menu), as opposed to focusing, adjusting, closing or setting its value.
bool activates(SemanticAction action) {
    const bool result = action == SemanticAction::press || action == SemanticAction::select ||
                        action == SemanticAction::expand || action == SemanticAction::collapse ||
                        action == SemanticAction::show_menu;
    return result;
}

// The index of the action AT-SPI clients get as action 0, the default: the
// first activating action; else focus; else the first action other than
// set_value, which needs a value a default action does not carry; else
// set_value. `semantic` is not empty.
std::size_t default_action(const std::vector<SemanticAction>& semantic) {
    for (std::size_t index = 0; index < semantic.size(); ++index) {
        if (activates(semantic[index])) return index;
    }
    for (std::size_t index = 0; index < semantic.size(); ++index) {
        if (semantic[index] == SemanticAction::focus) return index;
    }
    for (std::size_t index = 0; index < semantic.size(); ++index) {
        if (semantic[index] != SemanticAction::set_value) return index;
    }
    return 0U;
}

// Fills `published` with the node's actions in published order: the default
// action first, then the rest in semantic order.
void order_actions(const std::vector<SemanticAction>& semantic, std::vector<SemanticAction>& published) {
    published.clear();
    if (semantic.empty()) return;
    const std::size_t primary = default_action(semantic);
    published.push_back(semantic[primary]);
    for (std::size_t index = 0; index < semantic.size(); ++index) {
        if (index != primary) published.push_back(semantic[index]);
    }
}

// The interfaces a node's object implements: Action when it has actions;
// Value when it has a numeric value or (other than a text box, whose value
// is its text) a value string, read through the Value interface's text; Text
// and EditableText for a text box.
unsigned interface_flags(const SemanticNode& node) {
    unsigned flags = 0U;
    if (!node.actions.empty()) flags |= action_interface;
    if (node.numeric_value || (!node.value.empty() && node.role != SemanticRole::text_box)) flags |= value_interface;
    if (node.role == SemanticRole::text_box) flags |= text_interface;
    return flags;
}

void finalize(GObject* object) {
    delete (*reinterpret_cast<NativeAccessible*>(object)).data;
    GObjectClass* parent = G_OBJECT_CLASS(g_type_class_peek(ATK_TYPE_OBJECT));
    (*parent).finalize(object);
}
const gchar* name(AtkObject* object) { return data(object).node.name.c_str(); }
const gchar* description(AtkObject* object) { return data(object).node.description.c_str(); }
AtkObject* parent(AtkObject* object) { return data(object).live ? data(object).parent : nullptr; }
gint index_in_parent(AtkObject* object) { return data(object).live ? data(object).index : -1; }
gint child_count(AtkObject* object) { return data(object).live ? static_cast<gint>(data(object).children.size()) : 0; }
AtkObject* child_at(AtkObject* object, gint index) {
    const NodeData& value = data(object);
    if (!value.live || index < 0 || static_cast<std::size_t>(index) >= value.children.size()) { return nullptr; }
    return ATK_OBJECT(g_object_ref(value.children[index]));
}
AtkRole role(AtkObject* object) {
    const NodeData& value = data(object);
    if (value.application) return ATK_ROLE_APPLICATION;
    if (value.frame) return ATK_ROLE_FRAME;
    if (has_semantic_state(value.node.states, SemanticState::protected_content)) return ATK_ROLE_PASSWORD_TEXT;
    const AtkRole result = native_role(value.node.role);
    return result;
}
AtkStateSet* states(AtkObject* object) {
    AtkStateSet* result = atk_state_set_new();
    const NodeData& value = data(object);
    if (!value.live) { atk_state_set_add_state(result, ATK_STATE_DEFUNCT); return result; }
    const StateBits bits = state_bits(value.node);
    for (int state = 0; state < static_cast<int>(ATK_STATE_LAST_DEFINED); ++state) {
        const AtkStateType type = static_cast<AtkStateType>(state);
        if ((bits & state_bit(type)) != 0U) atk_state_set_add_state(result, type);
    }
    return result;
}
AtkObject* panel_object(const NodeData& value);
// The tab-to-panel relation as ATK's controller-for relation, to the panel's
// object when the window publishes the panel. A new set per call; the
// caller releases it.
AtkRelationSet* relations(AtkObject* object) {
    AtkRelationSet* set = atk_relation_set_new();
    const NodeData& value = data(object);
    if (!value.live || value.node.controlled_panel.empty()) { return set; }
    AtkObject* panel = panel_object(value);
    if (panel != nullptr) { atk_relation_set_add_relation_by_type(set, ATK_RELATION_CONTROLLER_FOR, panel); }
    return set;
}
// The object attributes AT-SPI clients read, as browsers publish them:
// "posinset" and "setsize" (published together), a tree item's "level", a
// table's "rowcount" and "colcount", a row's, cell's or header's
// "rowindex" and "colindex" (all counting from one), and a live region's
// "live" and "container-live" ("polite").
AtkAttributeSet* add_attribute(AtkAttributeSet* set, const char* name, const char* text) {
    AtkAttribute* attribute = g_new(AtkAttribute, 1);
    (*attribute).name = g_strdup(name);
    (*attribute).value = g_strdup(text);
    return g_slist_prepend(set, attribute);
}
AtkAttributeSet* add_count(AtkAttributeSet* set, const char* name, const std::optional<std::size_t>& number) {
    if (!number) return set;
    const std::string text = std::to_string(*number);
    AtkAttributeSet* result = add_attribute(set, name, text.c_str());
    return result;
}
AtkAttributeSet* attributes(AtkObject* object) {
    const NodeData& value = data(object);
    AtkAttributeSet* set = nullptr;
    if (!value.live) { return set; }
    const SemanticNode& node = value.node;
    if (node.position_in_set && node.set_size) {
        set = add_count(set, "setsize", node.set_size);
        set = add_count(set, "posinset", node.position_in_set);
    }
    set = add_count(set, "level", node.level);
    set = add_count(set, "rowindex", node.row_index);
    set = add_count(set, "colindex", node.column_index);
    set = add_count(set, "rowcount", node.row_count);
    set = add_count(set, "colcount", node.column_count);
    if (node.role == SemanticRole::live_region) {
        set = add_attribute(set, "live", "polite");
        set = add_attribute(set, "container-live", "polite");
    }
    return set;
}
// Asks a screen reader to speak a live region's latest announcement
// (DK-51), its new name. ATK 2.46 and later carry the "announcement" signal
// (AT-SPI object:announcement); it is looked up at run time, so that an
// older ATK, which has none, publishes the name change alone.
void announce_live_region(AtkObject* object, const std::string& text) {
    const guint signal = g_signal_lookup("announcement", ATK_TYPE_OBJECT);
    if (signal == 0U) return;
    g_signal_emit(object, signal, 0U, text.c_str());
}
void class_init(gpointer raw, gpointer) {
    AtkObjectClass& object = *static_cast<AtkObjectClass*>(raw);
    object.get_name = name; object.get_description = description; object.get_parent = parent;
    object.get_index_in_parent = index_in_parent; object.get_n_children = child_count;
    object.ref_child = child_at; object.get_role = role; object.ref_state_set = states;
    object.get_attributes = attributes; object.ref_relation_set = relations;
    (*G_OBJECT_CLASS(raw)).finalize = finalize;
}
void instance_init(GTypeInstance* instance, gpointer) {
    (*reinterpret_cast<NativeAccessible*>(instance)).data = new NodeData;
}
gboolean perform(gpointer object, SemanticAction action, const std::string& value = {});
gint action_count(AtkAction* object) { return static_cast<gint>(data(object).actions.size()); }
const gchar* action_name(AtkAction* object, gint index) {
    const std::vector<SemanticAction>& actions = data(object).actions;
    return index >= 0 && static_cast<std::size_t>(index) < actions.size() ? semantic_action_name(actions[index]) : nullptr;
}
// The default action's key binding, "<Alt>key;;" (mnemonic, no sequence, no
// shortcut), for a node with an access key; the others have none.
const gchar* action_keybinding(AtkAction* object, gint index) {
    const NodeData& value = data(object);
    if (index != 0 || value.keybinding.empty()) return nullptr;
    return value.keybinding.c_str();
}
gboolean do_action(AtkAction* object, gint index) {
    const std::vector<SemanticAction>& actions = data(object).actions;
    if (index < 0 || static_cast<std::size_t>(index) >= actions.size()) { return false; }
    const SemanticAction action = actions[index];
    return perform(object, action);
}
void action_init(gpointer raw, gpointer) {
    AtkActionIface& face = *static_cast<AtkActionIface*>(raw);
    face.get_n_actions = action_count; face.get_name = action_name; face.get_localized_name = action_name;
    face.do_action = do_action; face.get_keybinding = action_keybinding;
}
void extents(AtkComponent* object, gint* x, gint* y, gint* width, gint* height, AtkCoordType coordinates);
gboolean contains(AtkComponent* object, gint x, gint y, AtkCoordType coordinates) {
    gint left = 0, top = 0, width = 0, height = 0; extents(object, &left, &top, &width, &height, coordinates);
    return x >= left && y >= top && x < left + width && y < top + height;
}
AtkObject* at_point(AtkComponent* object, gint x, gint y, AtkCoordType coordinates) {
    const NodeData& value = data(object);
    for (std::size_t i = value.children.size(); i > 0; --i) {
        AtkObject* child = value.children[i - 1];
        if (contains(ATK_COMPONENT(child), x, y, coordinates)) {
            AtkObject* deepest = at_point(ATK_COMPONENT(child), x, y, coordinates);
            return deepest ? deepest : ATK_OBJECT(g_object_ref(child));
        }
    }
    return contains(object, x, y, coordinates) ? ATK_OBJECT(g_object_ref(object)) : nullptr;
}
gboolean focus(AtkComponent* object) { return perform(object, SemanticAction::focus); }
AtkLayer layer(AtkComponent*) { return ATK_LAYER_WIDGET; }
void component_init(gpointer raw, gpointer) {
    AtkComponentIface& face = *static_cast<AtkComponentIface*>(raw);
    face.get_extents = extents; face.contains = contains; face.ref_accessible_at_point = at_point;
    face.grab_focus = focus; face.get_layer = layer;
}
// A node without a numeric value reads as 0 with no range; its meaning is
// the text.
void value_and_text(AtkValue* object, gdouble* value, gchar** text) {
    if (value) { *value = data(object).node.numeric_value.value_or(0); }
    if (text) { *text = g_strdup(data(object).node.value.c_str()); }
}
AtkRange* range(AtkValue* object) {
    const SemanticNode& node = data(object).node;
    return node.minimum_value && node.maximum_value ? atk_range_new(*node.minimum_value, *node.maximum_value, nullptr) : nullptr;
}
void set_value(AtkValue* object, gdouble value) {
    if (!std::isfinite(value) || !data(object).node.numeric_value) { return; }
    std::ostringstream text; text.imbue(std::locale::classic()); text.precision(17); text << value;
    static_cast<void>(perform(object, SemanticAction::set_value, text.str()));
}
// A range node's small step; zero when it publishes none.
gdouble increment(AtkValue* object) {
    const gdouble step = data(object).node.small_step.value_or(0.0);
    return step;
}
void value_init(gpointer raw, gpointer) {
    AtkValueIface& face = *static_cast<AtkValueIface*>(raw);
    face.get_value_and_text = value_and_text; face.get_range = range; face.set_value = set_value;
    face.get_increment = increment;
}
gchar* text(AtkText* object, gint start, gint end) {
    const std::string& value = data(object).node.value;
    const glong count = g_utf8_strlen(value.c_str(), value.size());
    start = std::clamp(start, 0, static_cast<int>(count));
    end = end < 0 ? static_cast<int>(count) : std::clamp(end, start, static_cast<int>(count));
    return g_utf8_substring(value.c_str(), start, end);
}
gint text_count(AtkText* object) { return static_cast<gint>(g_utf8_strlen(data(object).node.value.c_str(), -1)); }
gunichar character(AtkText* object, gint offset) {
    const std::string& value = data(object).node.value;
    return offset >= 0 && offset < text_count(object) ? g_utf8_get_char(g_utf8_offset_to_pointer(value.c_str(), offset)) : 0;
}

// Editable text on the A-T1 core (A-L3). AT-SPI counts characters (Unicode
// scalar values); the core counts UTF-8 bytes on grapheme boundaries. Every
// offset is converted against the published value, which is valid UTF-8.

// The character offset of byte offset `byte` (at most the value's size).
gint character_offset(const std::string& value, std::size_t byte) {
    const glong offset = g_utf8_pointer_to_offset(value.c_str(), value.c_str() + byte);
    return static_cast<gint>(offset);
}
// The byte offset of character offset `offset`, or none when it lies
// outside 0..character count.
std::optional<std::size_t> byte_offset(const std::string& value, gint offset) {
    const glong count = g_utf8_strlen(value.c_str(), static_cast<gssize>(value.size()));
    if (offset < 0 || offset > count) return std::nullopt;
    const gchar* at = g_utf8_offset_to_pointer(value.c_str(), offset);
    const std::size_t byte = static_cast<std::size_t>(at - value.c_str());
    return byte;
}
// The published caret, or 0 without a published selection.
gint caret_offset(AtkText* object) {
    const NodeData& node = data(object);
    if (!node.live || !node.node.text_selection) return 0;
    return character_offset(node.node.value, (*node.node.text_selection).caret.value());
}
// One selection while the anchor and the caret differ, else none.
gint selection_count(AtkText* object) {
    const NodeData& node = data(object);
    if (!node.live || !node.node.text_selection) return 0;
    const SemanticTextSelection selection = *node.node.text_selection;
    return selection.anchor == selection.caret ? 0 : 1;
}
gchar* selection_text(AtkText* object, gint number, gint* start, gint* end) {
    if (start) { *start = 0; }
    if (end) { *end = 0; }
    if (number != 0 || selection_count(object) == 0) return nullptr;
    const NodeData& node = data(object);
    const SemanticTextSelection selection = *node.node.text_selection;
    const std::size_t low = std::min(selection.anchor.value(), selection.caret.value());
    const std::size_t high = std::max(selection.anchor.value(), selection.caret.value());
    const gint first = character_offset(node.node.value, low);
    const gint last = character_offset(node.node.value, high);
    if (start) { *start = first; }
    if (end) { *end = last; }
    return g_utf8_substring(node.node.value.c_str(), first, last);
}
// Defined after LinuxAccessibility::Impl: these reach the window.
gboolean set_caret(AtkText* object, gint offset);
gboolean add_selection(AtkText* object, gint start, gint end);
gboolean remove_selection(AtkText* object, gint number);
gboolean set_selection(AtkText* object, gint number, gint start, gint end);
gchar* string_at(AtkText* object, gint offset, AtkTextGranularity granularity, gint* start, gint* end);
void character_extents(AtkText* object, gint offset, gint* x, gint* y, gint* width, gint* height, AtkCoordType coordinates);
gint offset_at_point(AtkText* object, gint x, gint y, AtkCoordType coordinates);
void text_init(gpointer raw, gpointer) {
    AtkTextIface& face = *static_cast<AtkTextIface*>(raw);
    face.get_text = text; face.get_character_count = text_count; face.get_character_at_offset = character;
    face.get_caret_offset = caret_offset; face.set_caret_offset = set_caret;
    face.get_n_selections = selection_count; face.get_selection = selection_text;
    face.add_selection = add_selection; face.remove_selection = remove_selection; face.set_selection = set_selection;
    face.get_string_at_offset = string_at;
    face.get_character_extents = character_extents; face.get_offset_at_point = offset_at_point;
}

// Whether byte `index` of `bytes` continues a character (a UTF-8
// continuation byte) rather than starting one or lying at the end.
bool continues_character(const std::string& bytes, std::size_t index) {
    if (index >= bytes.size()) return false;
    const bool continuation = (static_cast<unsigned char>(bytes[index]) & 0xC0U) == 0x80U;
    return continuation;
}
// Announces a text node's change from `before` to `after` (A-L3): the
// replaced span as text-remove then text-insert (the common prefix and
// suffix, cut at character boundaries, are kept), then text-caret-moved when
// the caret moved and text-selection-changed when the selected span changed.
void announce_text(AtkObject* object, const std::string& before, const std::optional<SemanticTextSelection>& was,
                   const std::string& after, const std::optional<SemanticTextSelection>& now) {
    if (before != after) {
        const std::size_t shorter = std::min(before.size(), after.size());
        std::size_t prefix = 0U;
        while (prefix < shorter && before[prefix] == after[prefix]) ++prefix;
        // The prefix ends where a character starts in both texts.
        while (prefix > 0U && (continues_character(before, prefix) || continues_character(after, prefix))) --prefix;
        // The suffix does not overlap the prefix in the shorter text.
        std::size_t suffix = 0U;
        while (suffix < shorter - prefix && before[before.size() - 1U - suffix] == after[after.size() - 1U - suffix]) {
            ++suffix;
        }
        // The suffix starts where a character starts; its bytes are the same
        // in both texts.
        while (suffix > 0U && continues_character(after, after.size() - suffix)) --suffix;
        const gint position = character_offset(after, prefix);
        const std::string removed = before.substr(prefix, before.size() - suffix - prefix);
        const std::string inserted = after.substr(prefix, after.size() - suffix - prefix);
        if (!removed.empty()) {
            const gint length = static_cast<gint>(g_utf8_strlen(removed.c_str(), static_cast<gssize>(removed.size())));
            g_signal_emit_by_name(object, "text-remove", position, length, removed.c_str());
        }
        if (!inserted.empty()) {
            const gint length = static_cast<gint>(g_utf8_strlen(inserted.c_str(), static_cast<gssize>(inserted.size())));
            g_signal_emit_by_name(object, "text-insert", position, length, inserted.c_str());
        }
    }
    if (now) {
        const gint caret = character_offset(after, (*now).caret.value());
        const gint previous = was ? character_offset(before, (*was).caret.value()) : -1;
        if (caret != previous) { g_signal_emit_by_name(object, "text-caret-moved", caret); }
    }
    const bool selected_before = was && (*was).anchor != (*was).caret;
    const bool selected_now = now && (*now).anchor != (*now).caret;
    if ((selected_before || selected_now) && was != now) { g_signal_emit_by_name(object, "text-selection-changed"); }
}
void set_contents(AtkEditableText* object, const gchar* value) {
    if (value && g_utf8_validate(value, -1, nullptr)) { static_cast<void>(perform(object, SemanticAction::set_value, value)); }
}
void editable_init(gpointer raw, gpointer) { (*static_cast<AtkEditableTextIface*>(raw)).set_text_contents = set_contents; }
AtkObject* make_object(unsigned flags) {
    static std::map<unsigned, GType> types;
    std::map<unsigned, GType>::const_iterator found = types.find(flags);
    GType type;
    if (found == types.end()) {
        const std::string name = "GUIFormsAccessible" + std::to_string(flags);
        type = g_type_register_static_simple(ATK_TYPE_OBJECT, name.c_str(), sizeof(AtkObjectClass), class_init,
                                             sizeof(NativeAccessible), instance_init, static_cast<GTypeFlags>(0));
        const GInterfaceInfo component{component_init, nullptr, nullptr}; g_type_add_interface_static(type, ATK_TYPE_COMPONENT, &component);
        if (flags & action_interface) { const GInterfaceInfo face{action_init, nullptr, nullptr}; g_type_add_interface_static(type, ATK_TYPE_ACTION, &face); }
        if (flags & value_interface) { const GInterfaceInfo face{value_init, nullptr, nullptr}; g_type_add_interface_static(type, ATK_TYPE_VALUE, &face); }
        if (flags & text_interface) {
            const GInterfaceInfo face{text_init, nullptr, nullptr}; g_type_add_interface_static(type, ATK_TYPE_TEXT, &face);
            const GInterfaceInfo editable{editable_init, nullptr, nullptr}; g_type_add_interface_static(type, ATK_TYPE_EDITABLE_TEXT, &editable);
        }
        types.emplace(flags, type);
    } else { type = (*found).second; }
    return ATK_OBJECT(g_object_new(type, nullptr));
}
// Announces a parent's changed children: children-changed::remove for each
// former child that is gone, at its former index, then children-changed::add
// for each new child, at its index. Both lists hold objects that are still
// referenced.
void announce_children(AtkObject* parent, const std::vector<AtkObject*>& previous, const std::vector<AtkObject*>& current) {
    for (std::size_t index = 0; index < previous.size(); ++index) {
        if (std::find(current.begin(), current.end(), previous[index]) == current.end()) {
            g_signal_emit_by_name(parent, "children-changed::remove", static_cast<guint>(index), previous[index], nullptr);
        }
    }
    for (std::size_t index = 0; index < current.size(); ++index) {
        if (std::find(previous.begin(), previous.end(), current[index]) == previous.end()) {
            g_signal_emit_by_name(parent, "children-changed::add", static_cast<guint>(index), current[index], nullptr);
        }
    }
}
}
struct LinuxAccessibility::Impl {
    Display* display = nullptr;
    ::Window native{};
    Window* model = nullptr;
    AtkObject* frame = nullptr;
    // Owns one reference to each published object.
    std::map<NodeKey, AtkObject*> objects;
    // Objects leaving the tree during one update. They stay referenced until
    // every parent has announced its changed children, then are marked
    // defunct and released.
    std::vector<AtkObject*> retired;
    std::uint64_t generation = 0;
    bool published = false;
    // Reused by every text query (A-L3); filled and read within one query.
    SemanticTextLayout text_layout;

    // Marks `object` dead: it answers as defunct and no longer reaches the
    // model. The caller still holds its reference.
    static void retire_data(NodeData& node) {
        node.live = false; node.owner = nullptr; node.children.clear(); node.parent = nullptr;
    }
    // Publishes `node` as child `index` of `parent`. `owner_control` is the
    // runtime identity of the nearest control at or above `parent`, which
    // owns `node` when `node` is a virtual child.
    void sync(const SemanticNode& node, AtkObject* parent, int index, std::uint64_t owner_control) {
        const bool virtual_child = is_virtual_semantic_runtime_id(node.runtime_id);
        NodeKey key;
        key.control = virtual_child ? owner_control : node.runtime_id;
        if (virtual_child) key.virtual_child = node.stable_id;
        const unsigned flags = interface_flags(node);
        std::map<NodeKey, AtkObject*>::iterator found = objects.find(key);
        if (found != objects.end() && data((*found).second).flags != flags) {
            // A changed set of interfaces needs an object of another type.
            AtkObject* stale = (*found).second;
            retire_data(data(stale));
            retired.push_back(stale);
            objects.erase(found);
            found = objects.end();
        }
        const bool created = found == objects.end();
        AtkObject* object = created ? make_object(flags) : (*found).second;
        if (created) { objects.emplace(key, object); atk_object_set_accessible_id(object, node.stable_id.c_str()); }
        NodeData& value = data(object);
        const StateBits old_states = created ? StateBits{0} : state_bits(value.node);
        const std::string old_value = value.node.value, old_name = value.node.name;
        const std::optional<SemanticTextSelection> old_selection = value.node.text_selection;
        std::vector<AtkObject*> previous = std::move(value.children);
        value.children.clear();
        value.flags = flags; value.owner = this; value.key = key; value.node = node; value.node.children.clear(); value.live = true;
        order_actions(node.actions, value.actions);
        value.keybinding.clear();
        if (!node.access_key.empty()) {
            gchar* key = g_utf8_strdown(node.access_key.c_str(), -1);
            value.keybinding = std::string("<Alt>") + key + ";;";
            g_free(key);
        }
        value.parent = parent; value.index = index;
        data(parent).children.push_back(object);
        if (!created && old_name != node.name) { g_object_notify(G_OBJECT(object), "accessible-name"); }
        if (!created && node.role == SemanticRole::live_region && old_name != node.name && !node.name.empty()) {
            announce_live_region(object, node.name);
        }
        if (!created && old_value != node.value) { g_object_notify(G_OBJECT(object), "accessible-value"); }
        const StateBits new_states = state_bits(value.node);
        if (!created && old_states != new_states) {
            for (int state = 0; state < static_cast<int>(ATK_STATE_LAST_DEFINED); ++state) {
                const AtkStateType type = static_cast<AtkStateType>(state);
                const bool was = (old_states & state_bit(type)) != 0U;
                const bool is = (new_states & state_bit(type)) != 0U;
                if (was != is) atk_object_notify_state_change(object, type, is);
            }
        }
        if (!created && (flags & text_interface) != 0U) {
            announce_text(object, old_value, old_selection, node.value, node.text_selection);
        }
        const std::uint64_t child_owner = key.control;
        for (std::size_t i = 0; i < node.children.size(); ++i) { sync(node.children[i], object, static_cast<int>(i), child_owner); }
        announce_children(object, previous, value.children);
    }
    void update() {
        if (model == nullptr) { return; }
        if ((*model).semantic_generation() == generation && published) { return; }
        const SemanticSnapshot snapshot = (*model).semantic_snapshot();
        if (generation == snapshot.generation && published) { return; }
        generation = snapshot.generation;
        published = true;
        for (const std::pair<const NodeKey, AtkObject*>& entry : objects) { data(entry.second).live = false; }
        NodeData& frame_data = data(frame);
        std::vector<AtkObject*> previous = std::move(frame_data.children);
        frame_data.children.clear();
        XWindowAttributes window_attributes{}; XGetWindowAttributes(display, native, &window_attributes);
        frame_data.node.bounds = {0, 0, static_cast<double>(window_attributes.width), static_cast<double>(window_attributes.height)};
        frame_data.node.states = SemanticState::enabled | SemanticState::visible;
        for (std::size_t i = 0; i < snapshot.roots.size(); ++i) { sync(snapshot.roots[i], frame, static_cast<int>(i), 0U); }
        announce_children(frame, previous, frame_data.children);
        for (std::map<NodeKey, AtkObject*>::iterator i = objects.begin(); i != objects.end();) {
            AtkObject* object = (*i).second;
            if (data(object).live) { ++i; continue; }
            retire_data(data(object));
            retired.push_back(object);
            i = objects.erase(i);
        }
        release_retired();
    }
    // Announces each retired object as defunct and drops this owner's
    // reference; a client still holding one gets errors from the bridge
    // once the last reference is gone.
    void release_retired() {
        std::vector<AtkObject*> leaving = std::move(retired);
        retired.clear();
        for (AtkObject* object : leaving) {
            atk_object_notify_state_change(object, ATK_STATE_DEFUNCT, true);
            g_object_unref(object);
        }
    }
};
namespace {
// The live object whose node has the stable identity `value` names as its
// panel, or none.
AtkObject* panel_object(const NodeData& value) {
    if (value.owner == nullptr) { return nullptr; }
    for (const std::pair<const NodeKey, AtkObject*>& entry : (*value.owner).objects) {
        const NodeData& candidate = data(entry.second);
        if (candidate.live && candidate.node.stable_id == value.node.controlled_panel) { return entry.second; }
    }
    return nullptr;
}
// Performs a client's action. The published tree is first brought up to the
// model's generation, so an object whose control was disposed or replaced
// since the last update is refused; a live object then names its control by
// runtime identity (and its virtual child by stable identity), which the
// window refuses if that control is gone. Holds a reference to `object`
// throughout, since the update may retire it.
gboolean perform(gpointer object, SemanticAction action, const std::string& value) {
    AtkObject* const accessible = ATK_OBJECT(object);
    NodeData& node = data(accessible);
    if (!node.live || node.owner == nullptr || (*node.owner).model == nullptr) { return false; }
    LinuxAccessibility::Impl& owner = *node.owner;
    g_object_ref(accessible);
    gboolean result = false;
    try {
        owner.update();
        if (node.live && node.owner != nullptr && owner.model != nullptr) {
            if (action == SemanticAction::focus) { XSetInputFocus(owner.display, owner.native, RevertToParent, CurrentTime); }
            // Copied: the action may run a nested main loop whose update
            // replaces this node's data.
            const std::string virtual_child = node.key.virtual_child;
            const std::string stable_id = node.node.stable_id;
            const RuntimeId control{node.key.control};
            if (control.value == 0U) {
                // A virtual child at the top of the tree has no exposed
                // control above it; the window finds its publisher by stable
                // identity.
                result = (*owner.model).perform_semantic_action(stable_id, action, value);
            } else {
                result = (*owner.model).perform_semantic_action(control, virtual_child, action, value);
            }
        }
    } catch (...) { result = false; }
    g_object_unref(accessible);
    return result;
}
// Where window client coordinates (the coordinates of node bounds and text
// layout boxes) put their origin in `coordinates`: the window's place on the
// screen, the client origin itself, or minus the place of `node`'s parent.
void coordinate_origin(const NodeData& node, AtkCoordType coordinates, int& left, int& top) {
    left = 0; top = 0;
    if (node.owner && coordinates == ATK_XY_SCREEN) {
        ::Window child{};
        XTranslateCoordinates((*node.owner).display, (*node.owner).native, DefaultRootWindow((*node.owner).display), 0, 0, &left, &top, &child);
    } else if (node.parent && coordinates == ATK_XY_PARENT) {
        left = -static_cast<int>(data(node.parent).node.bounds.x); top = -static_cast<int>(data(node.parent).node.bounds.y);
    }
}
void extents(AtkComponent* object, gint* x, gint* y, gint* width, gint* height, AtkCoordType coordinates) {
    const NodeData& node = data(object);
    int left = 0, top = 0;
    coordinate_origin(node, coordinates, left, top);
    if (x) { *x = left + static_cast<int>(node.node.bounds.x); }
    if (y) { *y = top + static_cast<int>(node.node.bounds.y); }
    if (width) { *width = node.live ? static_cast<int>(node.node.bounds.width) : 0; }
    if (height) { *height = node.live ? static_cast<int>(node.node.bounds.height) : 0; }
}

// -- editable text through the window (A-L3) ---------------------------------
// Each function holds a reference to its object throughout, since bringing
// the published tree up to date may retire it, and catches every exception:
// none may cross into the AT-SPI bridge.

// Whether every offset of `layout` lies within a value of `size` bytes: the
// layout comes from the control, the value from the published snapshot.
bool layout_fits(const SemanticTextLayout& layout, std::size_t size) {
    for (const SemanticTextLine& line : layout.lines) {
        if (line.range.end.value() > size) return false;
    }
    for (const SemanticTextGrapheme& grapheme : layout.graphemes) {
        if (grapheme.range.end.value() > size) return false;
    }
    for (const Utf8Offset start : layout.word_starts) {
        if (start.value() > size) return false;
    }
    return true;
}
// Brings the published tree up to the window's generation, then fills the
// owner's text layout for `accessible`. False when the object is defunct,
// the window answers anything but done, or the layout does not fit the
// published value.
bool fetch_text_layout(AtkObject* accessible) {
    NodeData& node = data(accessible);
    if (!node.live || node.owner == nullptr || (*node.owner).model == nullptr) return false;
    LinuxAccessibility::Impl& owner = *node.owner;
    owner.update();
    if (!node.live || node.owner == nullptr) return false;
    const RuntimeId control{node.key.control};
    const SemanticTextResult result = (*owner.model).semantic_text_layout(control, owner.text_layout);
    if (result != SemanticTextResult::done) return false;
    const bool fits = layout_fits(owner.text_layout, node.node.value.size());
    return fits;
}
// Moves the selection to characters anchor..caret through the window, then
// publishes it, which announces the caret move and the selection change.
gboolean select_text(AtkText* object, gint anchor, gint caret) {
    AtkObject* const accessible = ATK_OBJECT(object);
    g_object_ref(accessible);
    gboolean result = FALSE;
    try {
        NodeData& node = data(accessible);
        if (node.live && node.owner != nullptr && (*node.owner).model != nullptr) {
            LinuxAccessibility::Impl& owner = *node.owner;
            owner.update();
            const std::optional<std::size_t> anchor_byte = byte_offset(node.node.value, anchor);
            const std::optional<std::size_t> caret_byte = byte_offset(node.node.value, caret);
            if (node.live && node.owner != nullptr && anchor_byte && caret_byte) {
                const RuntimeId control{node.key.control};
                const SemanticTextSelection selection{.anchor = Utf8Offset(*anchor_byte), .caret = Utf8Offset(*caret_byte)};
                const SemanticTextResult moved = (*owner.model).set_semantic_text_selection(control, selection);
                result = moved == SemanticTextResult::done ? TRUE : FALSE;
                owner.update();
            }
        }
    } catch (...) { result = FALSE; }
    g_object_unref(accessible);
    return result;
}
gboolean set_caret(AtkText* object, gint offset) {
    const gboolean result = select_text(object, offset, offset);
    return result;
}
// A second selection is not supported; the one selection is added only
// while there is none.
gboolean add_selection(AtkText* object, gint start, gint end) {
    if (selection_count(object) != 0) return FALSE;
    const gboolean result = select_text(object, start, end);
    return result;
}
// Removing the selection leaves the caret where it was.
gboolean remove_selection(AtkText* object, gint number) {
    if (number != 0 || selection_count(object) == 0) return FALSE;
    const gint caret = caret_offset(object);
    const gboolean result = select_text(object, caret, caret);
    return result;
}
gboolean set_selection(AtkText* object, gint number, gint start, gint end) {
    if (number != 0) return FALSE;
    const gboolean result = select_text(object, start, end);
    return result;
}
// The span of `granularity` around byte `byte` of `value`, from the owner's
// layout: a grapheme, a word (between consecutive word starts), a visual
// line (with the break that ends it; the last line also holds the end of
// the text), or a paragraph (a logical line with its break). False for a
// sentence (unsupported, A-T2), for a granularity ATK adds later, and at the
// end of the text for a grapheme or a word.
bool granularity_span(const SemanticTextLayout& layout, const std::string& value, std::size_t byte,
                      AtkTextGranularity granularity, std::size_t& low, std::size_t& high) {
    switch (granularity) {
    case ATK_TEXT_GRANULARITY_CHAR:
        for (const SemanticTextGrapheme& grapheme : layout.graphemes) {
            if (grapheme.range.start.value() <= byte && byte < grapheme.range.end.value()) {
                low = grapheme.range.start.value(); high = grapheme.range.end.value();
                return true;
            }
        }
        return false;
    case ATK_TEXT_GRANULARITY_WORD:
        for (std::size_t index = 0U; index + 1U < layout.word_starts.size(); ++index) {
            if (layout.word_starts[index].value() <= byte && byte < layout.word_starts[index + 1U].value()) {
                low = layout.word_starts[index].value(); high = layout.word_starts[index + 1U].value();
                return true;
            }
        }
        return false;
    case ATK_TEXT_GRANULARITY_LINE:
        for (std::size_t index = 0U; index < layout.lines.size(); ++index) {
            const std::size_t start = layout.lines[index].range.start.value();
            const bool last = index + 1U == layout.lines.size();
            const std::size_t next = last ? value.size() : layout.lines[index + 1U].range.start.value();
            if (start <= byte && (byte < next || (last && byte == next))) {
                low = start; high = next;
                return true;
            }
        }
        return false;
    case ATK_TEXT_GRANULARITY_PARAGRAPH: {
        const std::size_t before = byte == 0U ? std::string::npos : value.rfind('\n', byte - 1U);
        low = before == std::string::npos ? 0U : before + 1U;
        const std::size_t after = value.find('\n', byte);
        high = after == std::string::npos ? value.size() : after + 1U;
        return true;
    }
    case ATK_TEXT_GRANULARITY_SENTENCE:
        return false;
    }
    return false;
}
// The span's text, newly allocated; an empty string with offsets -1 when
// there is no span (as for a sentence).
gchar* string_at(AtkText* object, gint offset, AtkTextGranularity granularity, gint* start, gint* end) {
    if (start) { *start = -1; }
    if (end) { *end = -1; }
    AtkObject* const accessible = ATK_OBJECT(object);
    g_object_ref(accessible);
    gchar* result = nullptr;
    try {
        if (fetch_text_layout(accessible)) {
            const NodeData& node = data(accessible);
            const std::string& value = node.node.value;
            const std::optional<std::size_t> byte = byte_offset(value, offset);
            std::size_t low = 0U, high = 0U;
            if (byte && granularity_span((*node.owner).text_layout, value, *byte, granularity, low, high)) {
                const gint first = character_offset(value, low);
                const gint last = character_offset(value, high);
                if (start) { *start = first; }
                if (end) { *end = last; }
                result = g_utf8_substring(value.c_str(), first, last);
            }
        }
    } catch (...) { result = nullptr; }
    g_object_unref(accessible);
    if (result == nullptr) { result = g_strdup(""); }
    return result;
}
// A character's box: the box of the grapheme it belongs to. All four are -1
// at the end of the text or when the layout cannot be read.
void character_extents(AtkText* object, gint offset, gint* x, gint* y, gint* width, gint* height, AtkCoordType coordinates) {
    if (x) { *x = -1; }
    if (y) { *y = -1; }
    if (width) { *width = -1; }
    if (height) { *height = -1; }
    AtkObject* const accessible = ATK_OBJECT(object);
    g_object_ref(accessible);
    try {
        if (fetch_text_layout(accessible)) {
            const NodeData& node = data(accessible);
            const std::optional<std::size_t> byte = byte_offset(node.node.value, offset);
            std::size_t low = 0U, high = 0U;
            if (byte && granularity_span((*node.owner).text_layout, node.node.value, *byte, ATK_TEXT_GRANULARITY_CHAR, low, high)) {
                for (const SemanticTextGrapheme& grapheme : (*node.owner).text_layout.graphemes) {
                    if (grapheme.range.start.value() != low) continue;
                    int left = 0, top = 0;
                    coordinate_origin(node, coordinates, left, top);
                    if (x) { *x = left + static_cast<int>(std::lround(grapheme.bounds.x)); }
                    if (y) { *y = top + static_cast<int>(std::lround(grapheme.bounds.y)); }
                    if (width) { *width = static_cast<int>(std::lround(grapheme.bounds.width)); }
                    if (height) { *height = static_cast<int>(std::lround(grapheme.bounds.height)); }
                    break;
                }
            }
        }
    } catch (...) {}
    g_object_unref(accessible);
}
// The first character of the grapheme whose box holds the point, or -1.
gint offset_at_point(AtkText* object, gint x, gint y, AtkCoordType coordinates) {
    AtkObject* const accessible = ATK_OBJECT(object);
    g_object_ref(accessible);
    gint result = -1;
    try {
        if (fetch_text_layout(accessible)) {
            const NodeData& node = data(accessible);
            int left = 0, top = 0;
            coordinate_origin(node, coordinates, left, top);
            const double client_x = static_cast<double>(x - left);
            const double client_y = static_cast<double>(y - top);
            for (const SemanticTextGrapheme& grapheme : (*node.owner).text_layout.graphemes) {
                const Rect box = grapheme.bounds;
                if (client_x >= box.x && client_x < box.x + box.width && client_y >= box.y && client_y < box.y + box.height) {
                    result = character_offset(node.node.value, grapheme.range.start.value());
                    break;
                }
            }
        }
    } catch (...) { result = -1; }
    g_object_unref(accessible);
    return result;
}
}
LinuxAccessibility::LinuxAccessibility(Display* display, ::Window native, Window& model, const std::string& title)
    : impl_(std::make_unique<Impl>()) {
    (*impl_).display = display; (*impl_).native = native; (*impl_).model = &model;
    if (!application_root) {
        application_root = make_object(0); data(application_root).application = true;
        data(application_root).node.name = title;
        AtkUtilClass* util = ATK_UTIL_CLASS(g_type_class_ref(ATK_TYPE_UTIL));
        (*util).get_root = root_object; (*util).get_toolkit_name = toolkit_name; (*util).get_toolkit_version = toolkit_version;
        g_type_class_unref(util);
        static_cast<void>(atk_bridge_adaptor_init(nullptr, nullptr));
    }
    ++bridge_count;
    (*impl_).frame = make_object(0);
    NodeData& frame = data((*impl_).frame); frame.frame = true; frame.owner = impl_.get(); frame.parent = application_root;
    frame.index = static_cast<int>(data(application_root).children.size()); frame.node.name = title;
    data(application_root).children.push_back((*impl_).frame);
    g_signal_emit_by_name(application_root, "children-changed::add", frame.index, (*impl_).frame, nullptr);
    update();
}
LinuxAccessibility::~LinuxAccessibility() { detach(); }
void LinuxAccessibility::set_title(const std::string& title) {
    if (!(*impl_).model || !(*impl_).frame) return;
    NodeData& frame = data((*impl_).frame);
    if (frame.node.name == title) return;
    frame.node.name = title;
    g_object_notify(G_OBJECT((*impl_).frame), "accessible-name");
}
void LinuxAccessibility::detach() {
    if (!(*impl_).model) { return; }
    (*impl_).model = nullptr;
    for (const std::pair<const NodeKey, AtkObject*>& entry : (*impl_).objects) {
        Impl::retire_data(data(entry.second));
        (*impl_).retired.push_back(entry.second);
    }
    (*impl_).objects.clear();
    (*impl_).release_retired();
    NodeData& frame = data((*impl_).frame); frame.live = false; frame.owner = nullptr; frame.children.clear();
    std::vector<AtkObject*>& frames = data(application_root).children;
    frames.erase(std::remove(frames.begin(), frames.end(), (*impl_).frame), frames.end());
    g_signal_emit_by_name(application_root, "children-changed::remove", frame.index, (*impl_).frame, nullptr);
    for (std::size_t i = 0; i < frames.size(); ++i) { data(frames[i]).index = static_cast<int>(i); }
    frame.parent = nullptr; g_object_unref((*impl_).frame); (*impl_).frame = nullptr;
    if (--bridge_count == 0) {
        atk_bridge_adaptor_cleanup(); g_object_unref(application_root); application_root = nullptr;
    }
}
void LinuxAccessibility::update() { (*impl_).update(); }
void accessibility_dispatch() {
    for (int i = 0; i < 16 && g_main_context_pending(nullptr); ++i) { g_main_context_iteration(nullptr, false); }
}
void accessibility_wait(int display_fd, int wake_fd, int milliseconds) {
    GMainContext* context = g_main_context_default();
    if (!g_main_context_acquire(context)) { return; }
    gint priority = 0, timeout = -1;
    const gboolean ready = g_main_context_prepare(context, &priority);
    int count = g_main_context_query(context, priority, &timeout, nullptr, 0);
    std::vector<GPollFD> native(static_cast<std::size_t>(std::max(0, count)));
    count = g_main_context_query(context, priority, &timeout, native.data(), static_cast<int>(native.size()));
    if (count > static_cast<int>(native.size())) { g_main_context_release(context); return; }
    std::vector<pollfd> descriptors{{display_fd, POLLIN, 0}, {wake_fd, POLLIN, 0}};
    for (const GPollFD& descriptor : native) { descriptors.push_back({descriptor.fd, static_cast<short>(descriptor.events), 0}); }
    int wait = milliseconds < 0 ? timeout : timeout < 0 ? milliseconds : std::min(milliseconds, timeout);
    if (ready) { wait = 0; }
    static_cast<void>(poll(descriptors.data(), descriptors.size(), wait));
    for (std::size_t i = 0; i < native.size(); ++i) { native[i].revents = descriptors[i + 2].revents; }
    if (g_main_context_check(context, priority, native.data(), static_cast<int>(native.size()))) { g_main_context_dispatch(context); }
    g_main_context_release(context);
}
}
