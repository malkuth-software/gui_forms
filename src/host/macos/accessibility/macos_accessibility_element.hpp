#pragma once

// NSAccessibility publication of a window's semantic tree
// (macos_accessibility_element.mm): one element per published node, kept
// across rebuilds by its key, and the tree key the view caches against.

#include "../application/macos_host_private.hpp"

@class GUIFormsView;

namespace gui_forms::host::macos {

// What the published accessibility tree was built from. The semantic
// generation moves with every semantic invalidation (focus, text, enabled,
// visibility, bounds requests, attach and detach, focus scopes), but an
// arrangement that moves a control without such an invalidation of its own
// only advances the content revision, and the elements keep each node's
// bounds. The display generation moves when a control is painted again, in
// case a node describes what was painted (as the Windows tree's key does).
// The elements convert their window-local bounds to the screen when read,
// so window moves and scale changes need no entry here.
struct MacAccessibilityTreeKey final {
    std::uint64_t semantic_generation{0};
    std::uint64_t content_revision{0};
    std::uint64_t display_generation{0};
};

bool operator==(const MacAccessibilityTreeKey& left,
                const MacAccessibilityTreeKey& right) noexcept;

// The key `window` gives now; builds no snapshot. metrics_snapshot copies
// the metrics record, including its renderer-name string (at most one small
// allocation), since Window has no narrower reader of the display generation.
MacAccessibilityTreeKey read_accessibility_tree_key(const Window& window);

} // namespace gui_forms::host::macos

using gui_forms::host::macos::MacAccessibilityTreeKey;
using gui_forms::host::macos::read_accessibility_tree_key;

// One published semantic node. Its identity is the runtime identity of the
// control that publishes it and, for a virtual child, the child's stable
// identity (accessibility_element_key). Runtime identities are never reused,
// so an element a client keeps after its control is disposed or replaced
// names a control the window no longer has, and its actions are refused
// (interview GF017).
@interface GUIFormsAccessibilityElement : NSAccessibilityElement {
    __weak GUIFormsView* _owner;
    SemanticNode _node;
    // The runtime identity of the control that publishes this node: the
    // node's own, or, for a virtual child, the nearest control node's above
    // it (0 when there is none).
    std::uint64_t _control;
    // The virtual child's stable identity; empty for a control's own node.
    std::string _virtualChild;
    NSArray* _semanticChildren;
    // A text node's layout (readSemanticText), kept while the window's
    // accessibility tree key reads as it did after the layout was read
    // (N101): the key moves with the semantic generation, the content
    // revision and the display generation, so the value and the layout
    // pass are then unchanged. Every text attribute reads it, so VoiceOver
    // reading a character at a time no longer lays the text out for each
    // attribute.
    SemanticTextLayout _textLayout;
    MacAccessibilityTreeKey _textLayoutKey;
    BOOL _textLayoutCurrent;
}
- (instancetype)initWithNode:(const SemanticNode&)node
                       owner:(GUIFormsView*)owner
                      parent:(id)parent
                     control:(std::uint64_t)control
                virtualChild:(const std::string&)virtualChild;
- (void)updateWithNode:(const SemanticNode&)node
                 owner:(GUIFormsView*)owner
                parent:(id)parent;
- (void)setSemanticChildren:(NSArray*)children;
- (BOOL)performNodeAction:(SemanticAction)action value:(NSString*)value;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (NSString*)answerAccessibilityRole;
- (NSString*)answerAccessibilityLabel;
- (NSString*)answerAccessibilityHelp;
- (NSString*)answerAccessibilityIdentifier;
- (NSArray*)answerAccessibilityChildren;
- (NSRect)answerAccessibilityFrame;
- (BOOL)answerIsAccessibilityEnabled;
- (BOOL)answerIsAccessibilityFocused;
- (BOOL)answerIsAccessibilitySelected;
- (BOOL)answerIsAccessibilityExpanded;
- (NSInteger)answerAccessibilityIndex;
- (BOOL)answerIsAccessibilityProtectedContent;
- (NSString*)answerAccessibilitySubrole;
- (NSInteger)answerAccessibilityDisclosureLevel;
- (NSRange)answerAccessibilityRowIndexRange;
- (NSRange)answerAccessibilityColumnIndexRange;
- (NSInteger)answerAccessibilityRowCount;
- (NSInteger)answerAccessibilityColumnCount;
- (BOOL)answerIsAccessibilityModal;
- (NSAccessibilityOrientation)answerAccessibilityOrientation;
- (NSArray*)answerAccessibilityLinkedUIElements;
- (NSArray<NSAccessibilityCustomAction*>*)answerAccessibilityCustomActions;
- (id)answerAccessibilityValue;
- (id)answerAccessibilityMinValue;
- (id)answerAccessibilityMaxValue;
- (BOOL)answerAccessibilityPerformPress;
- (void)answerSetAccessibilitySelected:(BOOL)selected;
- (BOOL)answerAccessibilityPerformShowMenu;
- (BOOL)answerAccessibilityPerformIncrement;
- (BOOL)answerAccessibilityPerformDecrement;
- (void)answerSetAccessibilityFocused:(BOOL)focused;
- (void)answerSetAccessibilityValue:(id)value;
- (NSInteger)answerAccessibilityNumberOfCharacters;
- (NSRange)answerAccessibilitySelectedTextRange;
- (void)answerSetAccessibilitySelectedTextRange:(NSRange)range;
- (NSString*)answerAccessibilitySelectedText;
- (NSRange)answerAccessibilityVisibleCharacterRange;
- (NSInteger)answerAccessibilityInsertionPointLineNumber;
- (NSString*)answerAccessibilityStringForRange:(NSRange)range;
- (NSAttributedString*)answerAccessibilityAttributedStringForRange:(NSRange)range;
- (NSInteger)answerAccessibilityLineForIndex:(NSInteger)index;
- (NSRange)answerAccessibilityRangeForLine:(NSInteger)line;
- (NSRect)answerAccessibilityFrameForRange:(NSRange)range;
- (NSRange)answerAccessibilityRangeForPosition:(NSPoint)point;
- (NSRange)answerAccessibilityRangeForIndex:(NSInteger)index;
- (NSRange)answerAccessibilityStyleRangeForIndex:(NSInteger)index;
- (BOOL)answerIsAccessibilitySelectorAllowed:(SEL)selector;
- (BOOL)answerAccessibilityIsAttributeSettable:(NSAccessibilityAttributeName)attribute;
@end

namespace gui_forms::host::macos {

// Whether `node` or a node below it publishes text ranges.
// Recurses once per semantic level: at most the control nesting limit
// (64) plus maximum_virtual_semantic_depth (16) levels, both enforced
// before a snapshot is published (semantic_types.hpp).
bool contains_text_ranges(const SemanticNode& node);

// Whether `node` or a node below it is a live region. Recurses as
// contains_text_ranges does.
bool contains_live_region(const SemanticNode& node);

// Publishes `node` under `parent`, reusing the element `previous` holds for
// its key and recording it in `next`; `by_stable_id` records the first
// element in tree order for each stable identity (the tab-to-panel
// relation). `owner_control` is the runtime identity of the nearest control
// node at or above `parent`, which publishes `node` when it is a virtual
// child. The node's children are moved out of it first, so the element
// keeps a copy of the node's own fields only (audit N100: no element holds
// a deep copy of its subtree); the snapshot is not read again.
GUIFormsAccessibilityElement* reconcile_accessibility_element(
    SemanticNode& node,
    GUIFormsView* owner,
    id parent,
    std::uint64_t owner_control,
    NSDictionary<NSString*, GUIFormsAccessibilityElement*>* previous,
    NSMutableDictionary<NSString*, GUIFormsAccessibilityElement*>* next,
    NSMutableDictionary<NSString*, GUIFormsAccessibilityElement*>* by_stable_id);

} // namespace gui_forms::host::macos

using gui_forms::host::macos::contains_live_region;
using gui_forms::host::macos::contains_text_ranges;
using gui_forms::host::macos::reconcile_accessibility_element;
