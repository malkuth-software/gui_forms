// NSAccessibility publication of a window's semantic tree
// (macos_accessibility_element.hpp): the element class, its native entries
// and the reconciliation that keeps elements across rebuilds.

#include "macos_accessibility_element.hpp"
#include "macos_accessibility_text.hpp"
#include "../application/macos_values.hpp"
#include "../view/macos_view.hpp"

namespace {

NSString* native_accessibility_role(SemanticRole role) {
    switch (role) {
    case SemanticRole::group: return NSAccessibilityGroupRole;
    case SemanticRole::static_text: return NSAccessibilityStaticTextRole;
    case SemanticRole::button: return NSAccessibilityButtonRole;
    // A switch is a check box with AppKit's switch subrole.
    case SemanticRole::check_box:
    case SemanticRole::toggle_switch: return NSAccessibilityCheckBoxRole;
    case SemanticRole::radio_button: return NSAccessibilityRadioButtonRole;
    case SemanticRole::toolbar: return NSAccessibilityToolbarRole;
    case SemanticRole::radio_group: return NSAccessibilityRadioGroupRole;
    case SemanticRole::link: return NSAccessibilityLinkRole;
    // A search box is a text field with AppKit's search-field subrole.
    case SemanticRole::text_box:
    case SemanticRole::search_box:
    case SemanticRole::numeric_field: return NSAccessibilityTextFieldRole;
    case SemanticRole::list: return NSAccessibilityListRole;
    case SemanticRole::list_item: return NSAccessibilityRowRole;
    case SemanticRole::check_list_item: return NSAccessibilityCheckBoxRole;
    case SemanticRole::combo_box: return NSAccessibilityComboBoxRole;
    case SemanticRole::slider: return NSAccessibilitySliderRole;
    case SemanticRole::scroll_bar: return NSAccessibilityScrollBarRole;
    case SemanticRole::progress_bar: return NSAccessibilityProgressIndicatorRole;
    // AppKit has no chart role: a figure is an image with its summary as
    // the description.
    case SemanticRole::image:
    case SemanticRole::figure: return NSAccessibilityImageRole;
    case SemanticRole::tab_group: return NSAccessibilityTabGroupRole;
    case SemanticRole::tab: return NSAccessibilityRadioButtonRole;
    case SemanticRole::split_pane: return NSAccessibilitySplitGroupRole;
    case SemanticRole::tool_tip: return NSAccessibilityStaticTextRole;
    case SemanticRole::date_picker: return NSAccessibilityComboBoxRole;
    case SemanticRole::calendar: return NSAccessibilityGroupRole;
    case SemanticRole::date_cell: return NSAccessibilityButtonRole;
    case SemanticRole::property_grid: return NSAccessibilityGroupRole;
    case SemanticRole::property_group: return NSAccessibilityGroupRole;
    case SemanticRole::property_row: return NSAccessibilityStaticTextRole;
    case SemanticRole::menu_bar: return NSAccessibilityMenuBarRole;
    case SemanticRole::menu_bar_item: return NSAccessibilityMenuBarItemRole;
    case SemanticRole::menu: return NSAccessibilityMenuRole;
    case SemanticRole::menu_item: return NSAccessibilityMenuItemRole;
    // AppKit exposes no separator role. A visual menu separator is structural,
    // never an adjustable splitter, so retain it as a neutral group.
    case SemanticRole::separator: return NSAccessibilityGroupRole;
    // AppKit has no split-button role. The primary action is a press, so a
    // split button is a button; a menu button is AppKit's menu button.
    case SemanticRole::split_button: return NSAccessibilityButtonRole;
    case SemanticRole::menu_button: return NSAccessibilityMenuButtonRole;
    // AppKit has no pane or tab-panel role: both are groups, a tab panel
    // with the AXTabPanel subrole (accessibilitySubrole).
    case SemanticRole::pane: return NSAccessibilityGroupRole;
    case SemanticRole::tab_panel: return NSAccessibilityGroupRole;
    // The stage A vocabulary (12.13), as AppKit's outline and table views
    // and WebKit publish them: a tree and a tree grid are outlines, a tree
    // item and a table row are rows, a cell and a header are cells, and a
    // dialog, a status bar and a live region are groups told apart by their
    // subroles (accessibilitySubrole).
    case SemanticRole::tree: return NSAccessibilityOutlineRole;
    case SemanticRole::tree_item: return NSAccessibilityRowRole;
    case SemanticRole::table: return NSAccessibilityTableRole;
    case SemanticRole::row: return NSAccessibilityRowRole;
    case SemanticRole::cell: return NSAccessibilityCellRole;
    case SemanticRole::column_header: return NSAccessibilityCellRole;
    case SemanticRole::row_header: return NSAccessibilityCellRole;
    case SemanticRole::tree_grid: return NSAccessibilityOutlineRole;
    case SemanticRole::dialog: return NSAccessibilityGroupRole;
    case SemanticRole::status_bar: return NSAccessibilityGroupRole;
    case SemanticRole::live_region: return NSAccessibilityGroupRole;
    case SemanticRole::meter: return NSAccessibilityLevelIndicatorRole;
    case SemanticRole::generic: return NSAccessibilityGroupRole;
    case SemanticRole::count: return NSAccessibilityGroupRole;
    }
    return NSAccessibilityGroupRole;
}

bool semantic_has_action(const SemanticNode& node, SemanticAction action) {
    return std::find(node.actions.begin(), node.actions.end(), action) !=
           node.actions.end();
}

} // namespace

namespace gui_forms::host::macos {

bool operator==(const MacAccessibilityTreeKey& left,
                const MacAccessibilityTreeKey& right) noexcept {
    const bool equal = left.semantic_generation == right.semantic_generation &&
                       left.content_revision == right.content_revision &&
                       left.display_generation == right.display_generation;
    return equal;
}

MacAccessibilityTreeKey read_accessibility_tree_key(const Window& window) {
    MacAccessibilityTreeKey key;
    key.semantic_generation = window.semantic_generation();
    key.content_revision = window.paint_lease_snapshot().content_revision;
    key.display_generation = window.metrics_snapshot().display_generation;
    return key;
}

} // namespace gui_forms::host::macos

// Asks VoiceOver to speak a live region's latest announcement (DK-51) at
// medium priority, the polite setting the other hosts publish. Posted to the
// view's window, or to the application while the view has none.
static void announce_live_region(GUIFormsView* owner, const std::string& text) {
    NSString* const announcement = native_string(text);
    NSDictionary* const info = @{
        NSAccessibilityAnnouncementKey : announcement,
        NSAccessibilityPriorityKey : @(NSAccessibilityPriorityMedium),
    };
    NSWindow* const window = owner == nil ? nil : [owner window];
    const id target = window != nil ? static_cast<id>(window) : static_cast<id>(NSApp);
    NSAccessibilityPostNotificationWithUserInfo(target, NSAccessibilityAnnouncementRequestedNotification, info);
}

// Whether `node` publishes text ranges.
static bool publishes_text_ranges(const SemanticNode& node) noexcept {
    const bool text = gui_forms::semantic_role_fallback(node.role) == SemanticRole::text_box &&
                      node.text_selection.has_value() &&
                      node.runtime_id != 0U && !gui_forms::is_virtual_semantic_runtime_id(node.runtime_id) &&
                      !gui_forms::has_semantic_state(node.states, SemanticState::protected_content);
    return text;
}

namespace gui_forms::host::macos {

bool contains_text_ranges(const SemanticNode& node) {
    if (publishes_text_ranges(node)) return true;
    for (const SemanticNode& child : node.children) {
        if (contains_text_ranges(child)) return true;
    }
    return false;
}

bool contains_live_region(const SemanticNode& node) {
    if (node.role == SemanticRole::live_region) return true;
    for (const SemanticNode& child : node.children) {
        if (contains_live_region(child)) return true;
    }
    return false;
}

} // namespace gui_forms::host::macos

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsAccessibilityElement's native entries: each sends the entry's answer message.
NSString* run_accessibility_element_accessibility_role(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityRole];
}
NSString* run_accessibility_element_accessibility_label(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityLabel];
}
NSString* run_accessibility_element_accessibility_help(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityHelp];
}
NSString* run_accessibility_element_accessibility_identifier(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityIdentifier];
}
NSArray* run_accessibility_element_accessibility_children(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityChildren];
}
NSRect run_accessibility_element_accessibility_frame(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityFrame];
}
BOOL run_accessibility_element_is_accessibility_enabled(GUIFormsAccessibilityElement* object) {
    return [object answerIsAccessibilityEnabled];
}
BOOL run_accessibility_element_is_accessibility_focused(GUIFormsAccessibilityElement* object) {
    return [object answerIsAccessibilityFocused];
}
BOOL run_accessibility_element_is_accessibility_selected(GUIFormsAccessibilityElement* object) {
    return [object answerIsAccessibilitySelected];
}
BOOL run_accessibility_element_is_accessibility_expanded(GUIFormsAccessibilityElement* object) {
    return [object answerIsAccessibilityExpanded];
}
NSInteger run_accessibility_element_accessibility_index(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityIndex];
}
BOOL run_accessibility_element_is_accessibility_protected_content(GUIFormsAccessibilityElement* object) {
    return [object answerIsAccessibilityProtectedContent];
}
NSString* run_accessibility_element_accessibility_subrole(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilitySubrole];
}
NSInteger run_accessibility_element_accessibility_disclosure_level(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityDisclosureLevel];
}
NSRange run_accessibility_element_accessibility_row_index_range(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityRowIndexRange];
}
NSRange run_accessibility_element_accessibility_column_index_range(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityColumnIndexRange];
}
NSInteger run_accessibility_element_accessibility_row_count(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityRowCount];
}
NSInteger run_accessibility_element_accessibility_column_count(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityColumnCount];
}
BOOL run_accessibility_element_is_accessibility_modal(GUIFormsAccessibilityElement* object) {
    return [object answerIsAccessibilityModal];
}
NSAccessibilityOrientation run_accessibility_element_accessibility_orientation(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityOrientation];
}
NSArray* run_accessibility_element_accessibility_linked_u_i_elements(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityLinkedUIElements];
}
NSArray<NSAccessibilityCustomAction*>* run_accessibility_element_accessibility_custom_actions(
    GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityCustomActions];
}
id run_accessibility_element_accessibility_value(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityValue];
}
id run_accessibility_element_accessibility_min_value(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityMinValue];
}
id run_accessibility_element_accessibility_max_value(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityMaxValue];
}
BOOL run_accessibility_element_accessibility_perform_press(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityPerformPress];
}
void run_accessibility_element_set_accessibility_selected(GUIFormsAccessibilityElement* object, BOOL selected) {
    [object answerSetAccessibilitySelected:selected];
}
BOOL run_accessibility_element_accessibility_perform_show_menu(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityPerformShowMenu];
}
BOOL run_accessibility_element_accessibility_perform_increment(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityPerformIncrement];
}
BOOL run_accessibility_element_accessibility_perform_decrement(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityPerformDecrement];
}
void run_accessibility_element_set_accessibility_focused(GUIFormsAccessibilityElement* object, BOOL focused) {
    [object answerSetAccessibilityFocused:focused];
}
void run_accessibility_element_set_accessibility_value(GUIFormsAccessibilityElement* object, id value) {
    [object answerSetAccessibilityValue:value];
}
NSInteger run_accessibility_element_accessibility_number_of_characters(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityNumberOfCharacters];
}
NSRange run_accessibility_element_accessibility_selected_text_range(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilitySelectedTextRange];
}
void run_accessibility_element_set_accessibility_selected_text_range(
    GUIFormsAccessibilityElement* object, NSRange range) {
    [object answerSetAccessibilitySelectedTextRange:range];
}
NSString* run_accessibility_element_accessibility_selected_text(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilitySelectedText];
}
NSRange run_accessibility_element_accessibility_visible_character_range(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityVisibleCharacterRange];
}
NSInteger run_accessibility_element_accessibility_insertion_point_line_number(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityInsertionPointLineNumber];
}
NSString* run_accessibility_element_accessibility_string_for_range(
    GUIFormsAccessibilityElement* object, NSRange range) {
    return [object answerAccessibilityStringForRange:range];
}
NSAttributedString* run_accessibility_element_accessibility_attributed_string_for_range(
    GUIFormsAccessibilityElement* object, NSRange range) {
    return [object answerAccessibilityAttributedStringForRange:range];
}
NSInteger run_accessibility_element_accessibility_line_for_index(
    GUIFormsAccessibilityElement* object, NSInteger index) {
    return [object answerAccessibilityLineForIndex:index];
}
NSRange run_accessibility_element_accessibility_range_for_line(GUIFormsAccessibilityElement* object, NSInteger line) {
    return [object answerAccessibilityRangeForLine:line];
}
NSRect run_accessibility_element_accessibility_frame_for_range(GUIFormsAccessibilityElement* object, NSRange range) {
    return [object answerAccessibilityFrameForRange:range];
}
NSRange run_accessibility_element_accessibility_range_for_position(
    GUIFormsAccessibilityElement* object, NSPoint point) {
    return [object answerAccessibilityRangeForPosition:point];
}
NSRange run_accessibility_element_accessibility_range_for_index(GUIFormsAccessibilityElement* object, NSInteger index) {
    return [object answerAccessibilityRangeForIndex:index];
}
NSRange run_accessibility_element_accessibility_style_range_for_index(
    GUIFormsAccessibilityElement* object, NSInteger index) {
    return [object answerAccessibilityStyleRangeForIndex:index];
}
BOOL run_accessibility_element_is_accessibility_selector_allowed(GUIFormsAccessibilityElement* object, SEL selector) {
    return [object answerIsAccessibilitySelectorAllowed:selector];
}
BOOL run_accessibility_element_accessibility_is_attribute_settable(
    GUIFormsAccessibilityElement* object, NSAccessibilityAttributeName attribute) {
    return [object answerAccessibilityIsAttributeSettable:attribute];
}
} // namespace

@implementation GUIFormsAccessibilityElement

// The children are set by reconcile_accessibility_element, which keys them.
- (instancetype)initWithNode:(const SemanticNode&)node
                       owner:(GUIFormsView*)owner
                      parent:(id)parent
                     control:(std::uint64_t)control
                virtualChild:(const std::string&)virtualChild {
    self = [super init];
    if (self != nil) {
        _owner = owner;
        _node = node;
        _control = control;
        _virtualChild = virtualChild;
        self.accessibilityParent = parent;
        _semanticChildren = @[];
    }
    return self;
}

// A live region whose name changed to a new, non-empty announcement posts
// it (semantic_announcement_due, the rule every host shares; a new element
// announces only when it announces on appearance, reconcile_accessibility_element).
// A text node whose value or selection changed posts it, as AppKit's text
// views do.
- (void)updateWithNode:(const SemanticNode&)node
                 owner:(GUIFormsView*)owner
                parent:(id)parent {
    const bool announce = gui_forms::semantic_announcement_due(&_node, node);
    const bool text = publishes_text_ranges(node) && publishes_text_ranges(_node);
    const bool value_changed = text && node.value != _node.value;
    const bool selection_changed = text && node.text_selection != _node.text_selection;
    _owner = owner;
    _node = node;
    self.accessibilityParent = parent;
    if (announce) announce_live_region(owner, node.name);
    if (value_changed) NSAccessibilityPostNotification(self, NSAccessibilityValueChangedNotification);
    if (selection_changed) NSAccessibilityPostNotification(self, NSAccessibilitySelectedTextChangedNotification);
}

- (void)setSemanticChildren:(NSArray*)children {
    _semanticChildren = [children copy];
}

- (BOOL)performNodeAction:(SemanticAction)action value:(NSString*)value {
    if (_owner == nil) return NO;
    return [_owner performSemanticAction:action
                                 control:_control
                            virtualChild:_virtualChild
                                stableId:_node.stable_id
                                   value:value];
}

- (BOOL)isAccessibilityElement { return YES; }
// A text box whose text runs over several lines is AppKit's text area.
- (NSString*)accessibilityRole {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityRole", &run_accessibility_element_accessibility_role, self);
}

- (NSString*)answerAccessibilityRole {
    if (_node.role == SemanticRole::text_box && _node.multiline) return NSAccessibilityTextAreaRole;
    return native_accessibility_role(_node.role);
}
- (NSString*)accessibilityLabel {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityLabel", &run_accessibility_element_accessibility_label, self);
}

- (NSString*)answerAccessibilityLabel { return native_string(_node.name); }
- (NSString*)accessibilityHelp {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityHelp", &run_accessibility_element_accessibility_help, self);
}

- (NSString*)answerAccessibilityHelp { return native_string(_node.description); }
- (NSString*)accessibilityIdentifier {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityIdentifier",
        &run_accessibility_element_accessibility_identifier, self);
}

- (NSString*)answerAccessibilityIdentifier { return native_string(_node.stable_id); }
- (NSArray*)accessibilityChildren {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityChildren", &run_accessibility_element_accessibility_children, self);
}

- (NSArray*)answerAccessibilityChildren { return _semanticChildren; }
- (NSRect)accessibilityFrame {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityFrame", &run_accessibility_element_accessibility_frame, self);
}

- (NSRect)answerAccessibilityFrame {
    return _owner == nil ? NSZeroRect
                         : [_owner accessibilityFrameForSemanticBounds:_node.bounds];
}
- (BOOL)isAccessibilityEnabled {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.isAccessibilityEnabled",
        &run_accessibility_element_is_accessibility_enabled, self);
}

- (BOOL)answerIsAccessibilityEnabled {
    return gui_forms::has_semantic_state(_node.states, SemanticState::enabled);
}
- (BOOL)isAccessibilityFocused {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.isAccessibilityFocused",
        &run_accessibility_element_is_accessibility_focused, self);
}

- (BOOL)answerIsAccessibilityFocused {
    return gui_forms::has_semantic_state(_node.states, SemanticState::focused);
}
- (BOOL)isAccessibilitySelected {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.isAccessibilitySelected",
        &run_accessibility_element_is_accessibility_selected, self);
}

- (BOOL)answerIsAccessibilitySelected {
    return gui_forms::has_semantic_state(_node.states, SemanticState::selected);
}
- (BOOL)isAccessibilityExpanded {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.isAccessibilityExpanded",
        &run_accessibility_element_is_accessibility_expanded, self);
}

- (BOOL)answerIsAccessibilityExpanded {
    return gui_forms::has_semantic_state(_node.states, SemanticState::expanded);
}
// AppKit's index attribute carries the zero-based position in a set; it has
// no set-size or has-popup attribute (a menu button's role says it opens a
// menu).
- (NSInteger)accessibilityIndex {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityIndex", &run_accessibility_element_accessibility_index, self);
}

- (NSInteger)answerAccessibilityIndex {
    if (!_node.position_in_set || *_node.position_in_set == 0) return 0;
    return static_cast<NSInteger>(*_node.position_in_set - 1);
}
- (BOOL)isAccessibilityProtectedContent {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.isAccessibilityProtectedContent",
        &run_accessibility_element_is_accessibility_protected_content, self);
}

- (BOOL)answerIsAccessibilityProtectedContent {
    return gui_forms::has_semantic_state(
        _node.states, SemanticState::protected_content);
}
// AppKit declares no constant for the tab-panel subrole; WebKit publishes
// ARIA tab panels with this one. A tab is a radio button with AppKit's
// tab-button subrole, as NSTabView's tabs are.
// A tree item is an outline row; a dialog, a status bar and a live region
// carry WebKit's subroles for them. A switch and a search box carry
// AppKit's own.
- (NSString*)accessibilitySubrole {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilitySubrole", &run_accessibility_element_accessibility_subrole, self);
}

- (NSString*)answerAccessibilitySubrole {
    if (_node.role == SemanticRole::toggle_switch) return NSAccessibilitySwitchSubrole;
    if (_node.role == SemanticRole::search_box) return NSAccessibilitySearchFieldSubrole;
    if (_node.role == SemanticRole::tab_panel) return @"AXTabPanel";
    if (_node.role == SemanticRole::tab) return NSAccessibilityTabButtonSubrole;
    if (_node.role == SemanticRole::tree_item) return NSAccessibilityOutlineRowSubrole;
    if (_node.role == SemanticRole::dialog) return @"AXApplicationDialog";
    if (_node.role == SemanticRole::status_bar) return @"AXApplicationStatus";
    if (_node.role == SemanticRole::live_region) return @"AXApplicationLog";
    return [super accessibilitySubrole];
}
// A tree item's depth, counting from zero as AppKit does.
- (NSInteger)accessibilityDisclosureLevel {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityDisclosureLevel",
        &run_accessibility_element_accessibility_disclosure_level, self);
}

- (NSInteger)answerAccessibilityDisclosureLevel {
    if (!_node.level || *_node.level == 0) return 0;
    return static_cast<NSInteger>(*_node.level - 1);
}
// A row's, cell's or header's place in its table, counting from zero, and a
// table's size. A node without an index does not publish the attribute
// (isAccessibilitySelectorAllowed:): AppKit would otherwise send the
// no-range value, {NSNotFound, 0}, which reaches a client as a location of
// LONG_MAX rather than kCFNotFound.
- (NSRange)accessibilityRowIndexRange {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityRowIndexRange",
        &run_accessibility_element_accessibility_row_index_range, self);
}

- (NSRange)answerAccessibilityRowIndexRange {
    if (!_node.row_index || *_node.row_index == 0) return NSMakeRange(NSNotFound, 0);
    return NSMakeRange(static_cast<NSUInteger>(*_node.row_index - 1), 1);
}
- (NSRange)accessibilityColumnIndexRange {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityColumnIndexRange",
        &run_accessibility_element_accessibility_column_index_range, self);
}

- (NSRange)answerAccessibilityColumnIndexRange {
    if (!_node.column_index || *_node.column_index == 0) return NSMakeRange(NSNotFound, 0);
    return NSMakeRange(static_cast<NSUInteger>(*_node.column_index - 1), 1);
}
- (NSInteger)accessibilityRowCount {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityRowCount", &run_accessibility_element_accessibility_row_count, self);
}

- (NSInteger)answerAccessibilityRowCount {
    return _node.row_count ? static_cast<NSInteger>(*_node.row_count) : 0;
}
- (NSInteger)accessibilityColumnCount {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityColumnCount",
        &run_accessibility_element_accessibility_column_count, self);
}

- (NSInteger)answerAccessibilityColumnCount {
    return _node.column_count ? static_cast<NSInteger>(*_node.column_count) : 0;
}
// A dialog that blocks the rest of its window.
- (BOOL)isAccessibilityModal {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.isAccessibilityModal", &run_accessibility_element_is_accessibility_modal, self);
}

- (BOOL)answerIsAccessibilityModal {
    return _node.modal ? YES : NO;
}
// A tab list's orientation.
- (NSAccessibilityOrientation)accessibilityOrientation {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityOrientation",
        &run_accessibility_element_accessibility_orientation, self);
}

- (NSAccessibilityOrientation)answerAccessibilityOrientation {
    if (!_node.orientation) return NSAccessibilityOrientationUnknown;
    if (*_node.orientation == SemanticOrientation::horizontal) return NSAccessibilityOrientationHorizontal;
    return NSAccessibilityOrientationVertical;
}
// The tab-to-panel relation as AppKit's linked elements: the panel's element
// when the window publishes the panel.
- (NSArray*)accessibilityLinkedUIElements {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityLinkedUIElements",
        &run_accessibility_element_accessibility_linked_u_i_elements, self);
}

- (NSArray*)answerAccessibilityLinkedUIElements {
    if (_owner == nil || _node.controlled_panel.empty()) return nil;
    id panel = [_owner semanticAccessibilityElementForStableId:native_string(_node.controlled_panel)];
    if (panel == nil) return nil;
    return @[ panel ];
}
// The close action has no AppKit action name; it is a custom action named
// "Close".
- (BOOL)performSemanticClose {
    if (_owner == nil || !semantic_has_action(_node, SemanticAction::close)) return NO;
    return [self performNodeAction:SemanticAction::close value:nil];
}
- (NSArray<NSAccessibilityCustomAction*>*)accessibilityCustomActions {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityCustomActions",
        &run_accessibility_element_accessibility_custom_actions, self);
}

- (NSArray<NSAccessibilityCustomAction*>*)answerAccessibilityCustomActions {
    if (!semantic_has_action(_node, SemanticAction::close)) return nil;
    NSAccessibilityCustomAction* close =
        [[NSAccessibilityCustomAction alloc] initWithName:@"Close"
                                                   target:self
                                                 selector:@selector(performSemanticClose)];
    return @[ close ];
}
- (id)accessibilityValue {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityValue", &run_accessibility_element_accessibility_value, self);
}

- (id)answerAccessibilityValue {
    const SemanticRole acting = gui_forms::semantic_role_fallback(_node.role);
    if (acting == SemanticRole::check_box ||
        acting == SemanticRole::check_list_item ||
        acting == SemanticRole::radio_button) {
        if (gui_forms::has_semantic_state(_node.states, SemanticState::mixed)) {
            return @2;
        }
        return @(gui_forms::has_semantic_state(_node.states, SemanticState::checked));
    }
    // A tab publishes AppKit's radio-button value: 1 when it is the
    // selected tab.
    if (_node.role == SemanticRole::tab) {
        return @(gui_forms::has_semantic_state(_node.states, SemanticState::selected));
    }
    if (_node.numeric_value) return @(*_node.numeric_value);
    return native_string(_node.value);
}
- (id)accessibilityMinValue {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityMinValue", &run_accessibility_element_accessibility_min_value, self);
}

- (id)answerAccessibilityMinValue {
    return _node.minimum_value ? @(*_node.minimum_value) : nil;
}
- (id)accessibilityMaxValue {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityMaxValue", &run_accessibility_element_accessibility_max_value, self);
}

- (id)answerAccessibilityMaxValue {
    return _node.maximum_value ? @(*_node.maximum_value) : nil;
}
- (BOOL)accessibilityPerformPress {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityPerformPress",
        &run_accessibility_element_accessibility_perform_press, self);
}

- (BOOL)answerAccessibilityPerformPress {
    if (_owner == nil) return NO;
    SemanticAction action = SemanticAction::press;
    if (!semantic_has_action(_node, action)) {
        if (semantic_has_action(_node, SemanticAction::select)) {
            action = SemanticAction::select;
        } else if (semantic_has_action(_node, SemanticAction::collapse)) {
            action = SemanticAction::collapse;
        } else if (semantic_has_action(_node, SemanticAction::expand)) {
            action = SemanticAction::expand;
        } else {
            return NO;
        }
    }
    return [self performNodeAction:action value:nil];
}
- (void)setAccessibilitySelected:(BOOL)selected {
    gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.setAccessibilitySelected:",
        &run_accessibility_element_set_accessibility_selected, self, selected);
}

- (void)answerSetAccessibilitySelected:(BOOL)selected {
    if (selected && _owner != nil &&
        semantic_has_action(_node, SemanticAction::select)) {
        static_cast<void>([self performNodeAction:SemanticAction::select value:nil]);
    }
}
- (BOOL)accessibilityPerformShowMenu {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityPerformShowMenu",
        &run_accessibility_element_accessibility_perform_show_menu, self);
}

- (BOOL)answerAccessibilityPerformShowMenu {
    if (_owner == nil) return NO;
    if (semantic_has_action(_node, SemanticAction::show_menu)) {
        return [self performNodeAction:SemanticAction::show_menu value:nil];
    }
    if (semantic_has_action(_node, SemanticAction::expand)) {
        return [self performNodeAction:SemanticAction::expand value:nil];
    }
    // No menu of the element's own: the show-menu action is macOS's
    // keyboard context request for the element's control, which takes
    // focus first so that the request reaches it.
    if (!semantic_has_action(_node, SemanticAction::focus)) return NO;
    if (![self performNodeAction:SemanticAction::focus value:nil]) return NO;
    return [_owner dispatchKeyboardContextRequest];
}
- (BOOL)accessibilityPerformIncrement {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityPerformIncrement",
        &run_accessibility_element_accessibility_perform_increment, self);
}

- (BOOL)answerAccessibilityPerformIncrement {
    if (_owner == nil || !semantic_has_action(_node, SemanticAction::increment)) return NO;
    return [self performNodeAction:SemanticAction::increment value:nil];
}
- (BOOL)accessibilityPerformDecrement {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityPerformDecrement",
        &run_accessibility_element_accessibility_perform_decrement, self);
}

- (BOOL)answerAccessibilityPerformDecrement {
    if (_owner == nil || !semantic_has_action(_node, SemanticAction::decrement)) return NO;
    return [self performNodeAction:SemanticAction::decrement value:nil];
}
- (void)setAccessibilityFocused:(BOOL)focused {
    gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.setAccessibilityFocused:",
        &run_accessibility_element_set_accessibility_focused, self, focused);
}

- (void)answerSetAccessibilityFocused:(BOOL)focused {
    if (focused && _owner != nil &&
        semantic_has_action(_node, SemanticAction::focus)) {
        static_cast<void>([self performNodeAction:SemanticAction::focus value:nil]);
    }
}
- (void)setAccessibilityValue:(id)value {
    gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.setAccessibilityValue:",
        &run_accessibility_element_set_accessibility_value, self, value);
}

- (void)answerSetAccessibilityValue:(id)value {
    if (_owner == nil || !semantic_has_action(_node, SemanticAction::set_value)) return;
    NSString* text = [value isKindOfClass:[NSString class]]
        ? (NSString*)value : [value stringValue];
    static_cast<void>([self performNodeAction:SemanticAction::set_value value:text]);
}

// Brings this element's node up to date and makes _textLayout its current
// layout: the one kept, while the tree key reads the same, else read again.
// Returns NO when the node publishes no text ranges, its control has left
// the window or the window has gone.
- (BOOL)readSemanticText {
    if (_owner == nil || !publishes_text_ranges(_node)) return NO;
    try {
        // The view republishes its tree when the semantic generation has
        // moved, which updates this element's node in place.
        static_cast<void>([_owner accessibilityChildren]);
        if (!publishes_text_ranges(_node)) return NO;
        MacAccessibilityTreeKey key;
        if (![_owner readAccessibilityTreeKey:key]) return NO;
        if (_textLayoutCurrent && key == _textLayoutKey) return YES;
        _textLayoutCurrent = NO;
        if (![_owner readSemanticTextLayout:_control layout:_textLayout]) return NO;
        // The key after the read, in case the query itself settled a layout.
        if (![_owner readAccessibilityTreeKey:_textLayoutKey]) return NO;
        _textLayoutCurrent = YES;
        return YES;
    } catch (...) {
        return NO;
    }
}
// The selection's ends in bytes, low first, within the value.
- (void)selectionBytesLow:(std::size_t&)low high:(std::size_t&)high {
    const SemanticTextSelection selection = *_node.text_selection;
    const std::size_t size = _node.value.size();
    low = std::min(std::min(selection.anchor.value(), selection.caret.value()), size);
    high = std::min(std::max(selection.anchor.value(), selection.caret.value()), size);
}
- (NSInteger)accessibilityNumberOfCharacters {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityNumberOfCharacters",
        &run_accessibility_element_accessibility_number_of_characters, self);
}

- (NSInteger)answerAccessibilityNumberOfCharacters {
    if (![self readSemanticText]) return 0;
    const std::size_t units = utf16_units_before(_node.value, _node.value.size());
    return static_cast<NSInteger>(units);
}
- (NSRange)accessibilitySelectedTextRange {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilitySelectedTextRange",
        &run_accessibility_element_accessibility_selected_text_range, self);
}

- (NSRange)answerAccessibilitySelectedTextRange {
    if (![self readSemanticText]) return NSMakeRange(0, 0);
    std::size_t low = 0U;
    std::size_t high = 0U;
    [self selectionBytesLow:low high:high];
    return utf16_range(_node.value, low, high);
}
// The selection becomes the range, anchored at its start with the caret at
// its end.
- (void)setAccessibilitySelectedTextRange:(NSRange)range {
    gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.setAccessibilitySelectedTextRange:",
        &run_accessibility_element_set_accessibility_selected_text_range, self, range);
}

- (void)answerSetAccessibilitySelectedTextRange:(NSRange)range {
    if (![self readSemanticText]) return;
    std::size_t start = 0U;
    std::size_t end = 0U;
    if (!utf8_range(_node.value, range, start, end)) return;
    const SemanticTextSelection selection{.anchor = gui_forms::Utf8Offset(start),
                                          .caret = gui_forms::Utf8Offset(end)};
    static_cast<void>([_owner selectSemanticText:_control selection:selection]);
}
- (NSString*)accessibilitySelectedText {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilitySelectedText",
        &run_accessibility_element_accessibility_selected_text, self);
}

- (NSString*)answerAccessibilitySelectedText {
    if (![self readSemanticText]) return nil;
    std::size_t low = 0U;
    std::size_t high = 0U;
    [self selectionBytesLow:low high:high];
    const std::string_view selected = std::string_view(_node.value).substr(low, high - low);
    return native_string(selected);
}
// From the first grapheme whose box meets the node's bounds to the last.
- (NSRange)accessibilityVisibleCharacterRange {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityVisibleCharacterRange",
        &run_accessibility_element_accessibility_visible_character_range, self);
}

- (NSRange)answerAccessibilityVisibleCharacterRange {
    if (![self readSemanticText]) return NSMakeRange(0, 0);
    std::optional<std::size_t> first;
    std::size_t last = 0U;
    for (const SemanticTextGrapheme& grapheme : _textLayout.graphemes) {
        const GFRect shown = GFRect::intersection(grapheme.bounds, _node.bounds);
        if (shown.empty()) continue;
        if (!first) first = grapheme.range.start.value();
        last = grapheme.range.end.value();
    }
    if (!first) return NSMakeRange(0, 0);
    return utf16_range(_node.value, *first, last);
}
- (NSInteger)accessibilityInsertionPointLineNumber {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityInsertionPointLineNumber",
        &run_accessibility_element_accessibility_insertion_point_line_number, self);
}

- (NSInteger)answerAccessibilityInsertionPointLineNumber {
    if (![self readSemanticText]) return 0;
    const std::size_t caret = (*_node.text_selection).caret.value();
    return static_cast<NSInteger>(line_of_offset(_textLayout, caret));
}
- (NSString*)accessibilityStringForRange:(NSRange)range {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityStringForRange:",
        &run_accessibility_element_accessibility_string_for_range, self, range);
}

- (NSString*)answerAccessibilityStringForRange:(NSRange)range {
    if (![self readSemanticText]) return nil;
    std::size_t start = 0U;
    std::size_t end = 0U;
    if (!utf8_range(_node.value, range, start, end)) return nil;
    const std::string_view text = std::string_view(_node.value).substr(start, end - start);
    return native_string(text);
}
// The text has one style run, so the attributed string carries no
// attributes.
- (NSAttributedString*)accessibilityAttributedStringForRange:(NSRange)range {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityAttributedStringForRange:",
        &run_accessibility_element_accessibility_attributed_string_for_range, self, range);
}

- (NSAttributedString*)answerAccessibilityAttributedStringForRange:(NSRange)range {
    NSString* const text = [self accessibilityStringForRange:range];
    if (text == nil) return nil;
    return [[NSAttributedString alloc] initWithString:text];
}
- (NSInteger)accessibilityLineForIndex:(NSInteger)index {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityLineForIndex:",
        &run_accessibility_element_accessibility_line_for_index, self, index);
}

- (NSInteger)answerAccessibilityLineForIndex:(NSInteger)index {
    if (![self readSemanticText] || index < 0) return 0;
    const std::size_t offset =
        utf8_offset_at_units(_node.value, static_cast<std::size_t>(index), SurrogateRounding::down);
    return static_cast<NSInteger>(line_of_offset(_textLayout, offset));
}
// A line runs to the next line's start, so it holds its break.
- (NSRange)accessibilityRangeForLine:(NSInteger)line {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityRangeForLine:",
        &run_accessibility_element_accessibility_range_for_line, self, line);
}

- (NSRange)answerAccessibilityRangeForLine:(NSInteger)line {
    if (![self readSemanticText] || line < 0) return NSMakeRange(NSNotFound, 0);
    const std::size_t index = static_cast<std::size_t>(line);
    const std::vector<SemanticTextLine>& lines = _textLayout.lines;
    if (index >= lines.size()) return NSMakeRange(NSNotFound, 0);
    const std::size_t start = lines[index].range.start.value();
    const std::size_t end = index + 1U < lines.size() ? lines[index + 1U].range.start.value() : _node.value.size();
    return utf16_range(_node.value, start, end);
}
// The union of the range's grapheme boxes (the caret's box for an empty
// range), cut to the node's bounds, in screen coordinates.
- (NSRect)accessibilityFrameForRange:(NSRange)range {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityFrameForRange:",
        &run_accessibility_element_accessibility_frame_for_range, self, range);
}

- (NSRect)answerAccessibilityFrameForRange:(NSRange)range {
    if (![self readSemanticText]) return NSZeroRect;
    std::size_t start = 0U;
    std::size_t end = 0U;
    if (!utf8_range(_node.value, range, start, end)) return NSZeroRect;
    const std::optional<GFRect> box = range_box(_textLayout, start, end);
    if (!box) return NSZeroRect;
    const GFRect shown = GFRect::intersection(*box, _node.bounds);
    if (shown.empty()) return NSZeroRect;
    return [_owner accessibilityFrameForSemanticBounds:shown];
}
// The composed character at a screen point.
- (NSRange)accessibilityRangeForPosition:(NSPoint)point {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityRangeForPosition:",
        &run_accessibility_element_accessibility_range_for_position, self, point);
}

- (NSRange)answerAccessibilityRangeForPosition:(NSPoint)point {
    if (![self readSemanticText]) return NSMakeRange(NSNotFound, 0);
    const NSPoint local = [_owner semanticPointForScreenPoint:point];
    const SemanticTextGrapheme* const hit = grapheme_at_point(_textLayout, local.x, local.y);
    if (hit == nullptr) return NSMakeRange(NSNotFound, 0);
    return utf16_range(_node.value, (*hit).range.start.value(), (*hit).range.end.value());
}
// The composed character holding a UTF-16 index: its grapheme, or the line
// break there, which is one character.
- (NSRange)accessibilityRangeForIndex:(NSInteger)index {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityRangeForIndex:",
        &run_accessibility_element_accessibility_range_for_index, self, index);
}

- (NSRange)answerAccessibilityRangeForIndex:(NSInteger)index {
    if (![self readSemanticText] || index < 0) return NSMakeRange(NSNotFound, 0);
    const std::size_t offset =
        utf8_offset_at_units(_node.value, static_cast<std::size_t>(index), SurrogateRounding::down);
    if (offset >= _node.value.size()) return NSMakeRange(NSNotFound, 0);
    for (const SemanticTextGrapheme& grapheme : _textLayout.graphemes) {
        if (grapheme.range.start.value() <= offset && offset < grapheme.range.end.value()) {
            return utf16_range(_node.value, grapheme.range.start.value(), grapheme.range.end.value());
        }
    }
    const std::size_t next = offset + utf8_sequence_bytes(static_cast<unsigned char>(_node.value[offset]));
    return utf16_range(_node.value, offset, next);
}
- (NSRange)accessibilityStyleRangeForIndex:(NSInteger)index {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityStyleRangeForIndex:",
        &run_accessibility_element_accessibility_style_range_for_index, self, index);
}

- (NSRange)answerAccessibilityStyleRangeForIndex:(NSInteger)index {
    if (![self readSemanticText] || index < 0) return NSMakeRange(NSNotFound, 0);
    return utf16_range(_node.value, 0U, _node.value.size());
}

- (BOOL)isAccessibilitySelectorAllowed:(SEL)selector {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.isAccessibilitySelectorAllowed:",
        &run_accessibility_element_is_accessibility_selector_allowed, self, selector);
}

- (BOOL)answerIsAccessibilitySelectorAllowed:(SEL)selector {
    if (text_range_selector(selector)) {
        return publishes_text_ranges(_node) ? YES : NO;
    }
    if (selector == @selector(setAccessibilityFocused:)) {
        return semantic_has_action(_node, SemanticAction::focus);
    }
    if (selector == @selector(setAccessibilityValue:)) {
        return semantic_has_action(_node, SemanticAction::set_value);
    }
    if (selector == @selector(setAccessibilitySelected:)) {
        return semantic_has_action(_node, SemanticAction::select);
    }
    if (selector == @selector(accessibilityPerformPress)) {
        return semantic_has_action(_node, SemanticAction::press) ||
               semantic_has_action(_node, SemanticAction::select) ||
               semantic_has_action(_node, SemanticAction::expand) ||
               semantic_has_action(_node, SemanticAction::collapse);
    }
    if (selector == @selector(accessibilityPerformShowMenu)) {
        return semantic_has_action(_node, SemanticAction::show_menu) ||
               semantic_has_action(_node, SemanticAction::expand) ||
               semantic_has_action(_node, SemanticAction::focus);
    }
    if (selector == @selector(accessibilityPerformIncrement)) {
        return semantic_has_action(_node, SemanticAction::increment);
    }
    if (selector == @selector(accessibilityPerformDecrement)) {
        return semantic_has_action(_node, SemanticAction::decrement);
    }
    if (selector == @selector(accessibilityRowIndexRange)) {
        return _node.row_index && *_node.row_index != 0 ? YES : NO;
    }
    if (selector == @selector(accessibilityColumnIndexRange)) {
        return _node.column_index && *_node.column_index != 0 ? YES : NO;
    }
    return [super isAccessibilitySelectorAllowed:selector];
}

- (BOOL)accessibilityIsAttributeSettable:(NSAccessibilityAttributeName)attribute {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityIsAttributeSettable:",
        &run_accessibility_element_accessibility_is_attribute_settable, self, attribute);
}

- (BOOL)answerAccessibilityIsAttributeSettable:(NSAccessibilityAttributeName)attribute {
    if ([attribute isEqualToString:NSAccessibilityFocusedAttribute]) {
        return semantic_has_action(_node, SemanticAction::focus);
    }
    if ([attribute isEqualToString:NSAccessibilityValueAttribute]) {
        return semantic_has_action(_node, SemanticAction::set_value);
    }
    if ([attribute isEqualToString:NSAccessibilitySelectedAttribute]) {
        return semantic_has_action(_node, SemanticAction::select);
    }
    if ([attribute isEqualToString:NSAccessibilitySelectedTextRangeAttribute]) {
        return publishes_text_ranges(_node) ? YES : NO;
    }
    return [super accessibilityIsAttributeSettable:attribute];
}

@end

// The key of an element in the view's element table: the publishing
// control's runtime identity, then the virtual child's stable identity
// (empty for a control's own node). Unique within one window's tree; a
// stable identity alone is not, since controls that show one model repeat
// their virtual children's identities.
static NSString* accessibility_element_key(std::uint64_t control, std::string_view virtual_child) {
    std::string key = std::to_string(control);
    key += '/';
    key += virtual_child;
    NSString* result = native_string(key);
    return result;
}

namespace gui_forms::host::macos {

GUIFormsAccessibilityElement* reconcile_accessibility_element(
    SemanticNode& node,
    GUIFormsView* owner,
    id parent,
    std::uint64_t owner_control,
    NSDictionary<NSString*, GUIFormsAccessibilityElement*>* previous,
    NSMutableDictionary<NSString*, GUIFormsAccessibilityElement*>* next,
    NSMutableDictionary<NSString*, GUIFormsAccessibilityElement*>* by_stable_id) {
    std::vector<SemanticNode> child_nodes = std::move(node.children);
    node.children.clear();
    const bool virtual_child = gui_forms::is_virtual_semantic_runtime_id(node.runtime_id);
    const std::uint64_t control = virtual_child ? owner_control : node.runtime_id;
    const std::string child_identity = virtual_child ? node.stable_id : std::string{};
    NSString* key = accessibility_element_key(control, child_identity);
    GUIFormsAccessibilityElement* element = previous[key];
    if (element == nil) {
        element = [[GUIFormsAccessibilityElement alloc] initWithNode:node
                                                               owner:owner
                                                              parent:parent
                                                             control:control
                                                        virtualChild:child_identity];
        // A new live region that announces on appearance (a toast) is
        // spoken as it appears.
        if (gui_forms::semantic_announcement_due(nullptr, node)) announce_live_region(owner, node.name);
    } else {
        [element updateWithNode:node owner:owner parent:parent];
    }
    next[key] = element;
    NSString* stable_identity = native_string(node.stable_id);
    if (by_stable_id[stable_identity] == nil) by_stable_id[stable_identity] = element;
    NSMutableArray* children = [[NSMutableArray alloc]
        initWithCapacity:child_nodes.size()];
    for (SemanticNode& childNode : child_nodes) {
        [children addObject:reconcile_accessibility_element(
            childNode, owner, element, control, previous, next, by_stable_id)];
    }
    [element setSemanticChildren:children];
    return element;
}

} // namespace gui_forms::host::macos
