// GUIFormsView (Accessibility): the view's own element, which publishes the
// window's semantic tree as its children, and the semantic queries the
// published elements make of it.

#include "macos_view.hpp"
#include "../application/macos_values.hpp"

@implementation GUIFormsView (Accessibility)

// The surface's own element covers the content rectangle: a declared window
// outer band lies outside it (owner, 2026-11-08), as it lies outside every
// hit test.
- (NSRect)answerAccessibilityFrame {
    if (!_model) return [super accessibilityFrame];
    return [self accessibilityFrameForSemanticBounds:(*_model).content_rect()];
}

- (NSString*)answerAccessibilityLabel {
    return @"GUI.Forms retained surface";
}

- (NSArray*)answerAccessibilityChildren {
    if (!_model) return @[];
    // Every client query lands here; build a snapshot only when the key has
    // moved since the cached children were built.
    const MacAccessibilityTreeKey before = read_accessibility_tree_key(*_model);
    if (_semanticAccessibilityChildren != nil && _accessibilityCacheKey == before) {
        return _semanticAccessibilityChildren;
    }
    // Not const: reconciling moves each node's children out of it.
    gui_forms::SemanticSnapshot snapshot = (*_model).semantic_snapshot();
    // The snapshot lays the window out first, which can move the key; the
    // children describe the window as it is after that layout.
    const MacAccessibilityTreeKey after = read_accessibility_tree_key(*_model);
    NSMutableArray* result = [[NSMutableArray alloc]
        initWithCapacity:snapshot.roots.size()];
    bool live_region = false;
    bool text_ranges = false;
    NSMutableDictionary<NSString*, GUIFormsAccessibilityElement*>* next =
        [[NSMutableDictionary alloc] initWithCapacity:snapshot.node_count];
    NSMutableDictionary<NSString*, GUIFormsAccessibilityElement*>* byStableId =
        [[NSMutableDictionary alloc] initWithCapacity:snapshot.node_count];
    for (SemanticNode& node : snapshot.roots) {
        // Read before reconciling, which empties the node's children.
        if (contains_live_region(node)) live_region = true;
        if (contains_text_ranges(node)) text_ranges = true;
        GUIFormsAccessibilityElement* element = reconcile_accessibility_element(
            node, self, self, 0U, _semanticAccessibilityElements, next, byStableId);
        [result addObject:element];
    }
    _publishesLiveRegion = live_region ? YES : NO;
    _publishesTextRanges = text_ranges ? YES : NO;
    _semanticAccessibilityElements = next;
    _semanticAccessibilityElementsByStableId = byStableId;
    _semanticAccessibilityChildren = [result copy];
    _accessibilityCacheKey = after;
    return _semanticAccessibilityChildren;
}

- (NSArray*)answerAccessibilityChildrenInNavigationOrder {
    return [self accessibilityChildren];
}

- (NSArray*)answerAccessibilityVisibleChildren {
    return [self accessibilityChildren];
}

- (NSRect)answerAccessibilityFrameForSemanticBounds:(GFRect)bounds {
    if (self.window == nil) return NSZeroRect;
    const NSRect local = NSMakeRect(bounds.x, bounds.y, bounds.width, bounds.height);
    const NSRect inWindow = [self convertRect:local toView:nil];
    return [self.window convertRectToScreen:inWindow];
}

- (BOOL)readSemanticTextLayout:(std::uint64_t)control layout:(SemanticTextLayout&)layout {
    if (!_model) return NO;
    const gui_forms::RuntimeId identity{control};
    const gui_forms::SemanticTextResult result = (*_model).semantic_text_layout(identity, layout);
    return result == gui_forms::SemanticTextResult::done ? YES : NO;
}

- (BOOL)readAccessibilityTreeKey:(MacAccessibilityTreeKey&)key {
    if (!_model) return NO;
    key = read_accessibility_tree_key(*_model);
    return YES;
}

- (BOOL)selectSemanticText:(std::uint64_t)control selection:(SemanticTextSelection)selection {
    if (!_model) return NO;
    const gui_forms::RuntimeId identity{control};
    const gui_forms::SemanticTextResult result = (*_model).set_semantic_text_selection(identity, selection);
    if (result != gui_forms::SemanticTextResult::done) return NO;
    [self collectDamage];
    return YES;
}

- (NSPoint)semanticPointForScreenPoint:(NSPoint)point {
    if (self.window == nil) return NSZeroPoint;
    const NSRect on_screen = NSMakeRect(point.x, point.y, 0.0, 0.0);
    const NSRect in_window = [self.window convertRectFromScreen:on_screen];
    return [self convertPoint:in_window.origin fromView:nil];
}

// The element of the current semantic tree with this stable identity, or
// nil; read by the tab-to-panel relation.
- (id)semanticAccessibilityElementForStableId:(NSString*)stableId {
    if (stableId == nil || _semanticAccessibilityElementsByStableId == nil) return nil;
    return _semanticAccessibilityElementsByStableId[stableId];
}

- (BOOL)performSemanticAction:(SemanticAction)action
                      control:(std::uint64_t)control
                 virtualChild:(const std::string&)virtualChild
                     stableId:(const std::string&)stableId
                        value:(NSString*)value {
    if (!_model) return NO;
    const std::string actionValue = value == nil ? std::string{} : utf8_string(value);
    if (action == SemanticAction::focus) {
        [self.window makeFirstResponder:self];
    }
    bool handled = false;
    if (control == 0U) {
        handled = (*_model).perform_semantic_action(stableId, action, actionValue);
    } else {
        const gui_forms::RuntimeId identity{control};
        handled = (*_model).perform_semantic_action(identity, virtualChild, action, actionValue);
    }
    if (handled) {
        [self collectDamage];
        NSAccessibilityPostNotification(self, NSAccessibilityValueChangedNotification);
    }
    return handled ? YES : NO;
}

@end

// This unit's link anchor (macos_view.hpp): referenced from
// link_view_categories in macos_view.mm so that the archive member holding
// the Accessibility category is linked wherever GUIFormsView is.
namespace gui_forms::host::macos {
void gui_forms_macos_view_accessibility_anchor() noexcept {}
} // namespace gui_forms::host::macos
