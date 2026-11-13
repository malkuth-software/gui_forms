// GUIFormsView (Input): pointer, keyboard, text input (NSTextInputClient's
// answers) and the drag destination, translated to the portable host
// events at the adapter boundary.

#include "macos_view.hpp"
#include "../application/macos_values.hpp"

static Modifier modifiers_for(NSEventModifierFlags flags) {
    Modifier result = Modifier::none;
    if ((flags & NSEventModifierFlagShift) != 0) {
        result = result | Modifier::shift;
    }
    if ((flags & NSEventModifierFlagControl) != 0) {
        result = result | Modifier::control;
    }
    if ((flags & NSEventModifierFlagOption) != 0) {
        result = result | Modifier::alt;
    }
    if ((flags & NSEventModifierFlagCommand) != 0) {
        result = result | Modifier::meta;
    }
    return result;
}

static PointerButton button_for(NSInteger button_number) {
    if (button_number == 0) {
        return PointerButton::primary;
    }
    if (button_number == 1) {
        return PointerButton::secondary;
    }
    if (button_number == 2) {
        return PointerButton::middle;
    }
    return PointerButton::none;
}

// AppKit keyCode values describe positions in Apple's hardware vocabulary.
// Translate them at the adapter boundary so retained controls see the same USB
// HID usages on every host. Text remains owned by NSTextInputClient below.
static std::uint32_t physical_key_for(unsigned short key_code) {
    switch (key_code) {
    case 0: return 0x04U;   // A
    case 1: return 0x16U;   // S
    case 2: return 0x07U;   // D
    case 3: return 0x09U;   // F
    case 4: return 0x0BU;   // H
    case 5: return 0x0AU;   // G
    case 6: return 0x1DU;   // Z
    case 7: return 0x1BU;   // X
    case 8: return 0x06U;   // C
    case 9: return 0x19U;   // V
    case 10: return 0x64U;  // ISO section key (Non-US backslash; N108)
    case 11: return 0x05U;  // B
    case 12: return 0x14U;  // Q
    case 13: return 0x1AU;  // W
    case 14: return 0x08U;  // E
    case 15: return 0x15U;  // R
    case 16: return 0x1CU;  // Y
    case 17: return 0x17U;  // T
    case 18: return 0x1EU;  // 1
    case 19: return 0x1FU;  // 2
    case 20: return 0x20U;  // 3
    case 21: return 0x21U;  // 4
    case 22: return 0x23U;  // 6
    case 23: return 0x22U;  // 5
    case 24: return 0x2EU;  // =
    case 25: return 0x26U;  // 9
    case 26: return 0x24U;  // 7
    case 27: return 0x2DU;  // -
    case 28: return 0x25U;  // 8
    case 29: return 0x27U;  // 0
    case 30: return 0x30U;  // ]
    case 31: return 0x12U;  // O
    case 32: return 0x18U;  // U
    case 33: return 0x2FU;  // [
    case 34: return 0x0CU;  // I
    case 35: return 0x13U;  // P
    case 36: return 0x28U;  // Return
    case 37: return 0x0FU;  // L
    case 38: return 0x0DU;  // J
    case 39: return 0x34U;  // '
    case 40: return 0x0EU;  // K
    case 41: return 0x33U;  // ;
    case 42: return 0x31U;  // backslash
    case 43: return 0x36U;  // comma
    case 44: return 0x38U;  // slash
    case 45: return 0x11U;  // N
    case 46: return 0x10U;  // M
    case 47: return 0x37U;  // period
    case 48: return 0x2BU;  // Tab
    case 49: return 0x2CU;  // Space
    case 50: return 0x35U;  // grave
    case 51: return 0x2AU;  // Backspace
    case 53: return 0x29U;  // Escape
    case 54: return 0xE7U;  // Right Command
    case 55: return 0xE3U;  // Left Command
    case 56: return 0xE1U;  // Left Shift
    case 57: return 0x39U;  // Caps Lock
    case 58: return 0xE2U;  // Left Option
    case 59: return 0xE0U;  // Left Control
    case 60: return 0xE5U;  // Right Shift
    case 61: return 0xE6U;  // Right Option
    case 62: return 0xE4U;  // Right Control
    case 64: return 0x6CU;  // F17
    case 65: return 0x63U;  // Keypad decimal
    case 67: return 0x55U;  // Keypad multiply
    case 69: return 0x57U;  // Keypad plus
    case 71: return 0x53U;  // Keypad clear
    case 75: return 0x54U;  // Keypad divide
    case 76: return 0x58U;  // Keypad enter
    case 78: return 0x56U;  // Keypad minus
    case 79: return 0x6DU;  // F18 (N108)
    case 80: return 0x6EU;  // F19 (N108)
    case 81: return 0x67U;  // Keypad equals
    case 82: return 0x62U;  // Keypad 0
    case 83: return 0x59U;  // Keypad 1
    case 84: return 0x5AU;  // Keypad 2
    case 85: return 0x5BU;  // Keypad 3
    case 86: return 0x5CU;  // Keypad 4
    case 87: return 0x5DU;  // Keypad 5
    case 88: return 0x5EU;  // Keypad 6
    case 89: return 0x5FU;  // Keypad 7
    case 90: return 0x6FU;  // F20 (N108)
    case 91: return 0x60U;  // Keypad 8
    case 92: return 0x61U;  // Keypad 9
    case 93: return 0x89U;  // JIS Yen (International3; N108)
    case 94: return 0x87U;  // JIS underscore (International1; N108)
    case 95: return 0x85U;  // JIS keypad comma (N108)
    case 96: return 0x3EU;  // F5
    case 97: return 0x3FU;  // F6
    case 98: return 0x40U;  // F7
    case 99: return 0x3CU;  // F3
    case 100: return 0x41U; // F8
    case 101: return 0x42U; // F9
    case 102: return 0x91U; // JIS Eisu (LANG2; N108)
    case 103: return 0x44U; // F11
    case 104: return 0x90U; // JIS Kana (LANG1; N108)
    case 105: return 0x68U; // F13
    case 106: return 0x6BU; // F16
    case 107: return 0x69U; // F14
    case 109: return 0x43U; // F10
    // Apple keyboards have no context-menu key; a PC keyboard's reports this
    // code (kVK_ContextualMenu in recent SDKs).
    case 110: return 0x65U; // Context menu
    case 111: return 0x45U; // F12
    case 113: return 0x6AU; // F15
    case 114: return 0x49U; // Help/Insert
    case 115: return 0x4AU; // Home
    case 116: return 0x4BU; // Page Up
    case 117: return 0x4CU; // Forward Delete
    case 118: return 0x3DU; // F4
    case 119: return 0x4DU; // End
    case 120: return 0x3BU; // F2
    case 121: return 0x4EU; // Page Down
    case 122: return 0x3AU; // F1
    case 123: return 0x50U; // Left
    case 124: return 0x4FU; // Right
    case 125: return 0x51U; // Down
    case 126: return 0x52U; // Up
    default: return 0U;
    }
}

static DragEffect drag_effects_for(NSDragOperation operations) {
    DragEffect result = DragEffect::none;
    if ((operations & NSDragOperationCopy) != 0) {
        result = result | DragEffect::copy;
    }
    if ((operations & NSDragOperationMove) != 0) {
        result = result | DragEffect::move;
    }
    if ((operations & NSDragOperationLink) != 0) {
        result = result | DragEffect::link;
    }
    return result;
}

static NSDragOperation native_drag_operation(DragEffect effect) {
    switch (effect) {
    case DragEffect::copy: return NSDragOperationCopy;
    case DragEffect::move: return NSDragOperationMove;
    case DragEffect::link: return NSDragOperationLink;
    case DragEffect::none: return NSDragOperationNone;
    }
    return NSDragOperationNone;
}

static std::vector<DragDataItem> drag_items_for(NSPasteboard* pasteboard) {
    std::vector<DragDataItem> result;
    std::size_t total_bytes = 0;
    NSArray<NSURL*>* urls = [pasteboard
        readObjectsForClasses:@[[NSURL class]]
                    options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    DragFileListData files;
    if (urls.count > DragLimits::maximum_paths) {
        return {};
    }
    for (NSURL* url in urls) {
        NSString* native_path = url.path;
        const NSUInteger byte_count =
            [native_path lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
        if (byte_count == 0 || byte_count > DragLimits::maximum_path_bytes ||
            byte_count > DragLimits::maximum_total_bytes - total_bytes) {
            return {};
        }
        const char* path = native_path.UTF8String;
        if (path == nullptr) {
            return {};
        }
        files.paths_utf8.emplace_back(path, byte_count);
        total_bytes += byte_count;
    }
    if (!files.paths_utf8.empty()) {
        result.emplace_back(std::move(files));
    }
    NSString* text = [pasteboard stringForType:NSPasteboardTypeString];
    if (text != nil) {
        const NSUInteger byte_count =
            [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
        if (byte_count > DragLimits::maximum_text_bytes ||
            byte_count > DragLimits::maximum_total_bytes - total_bytes) {
            return {};
        }
        const char* bytes = text.UTF8String;
        if (bytes != nullptr) {
            result.emplace_back(DragTextData{std::string(bytes, byte_count)});
            total_bytes += byte_count;
        }
    }
    NSPasteboardType type = [pasteboard availableTypeFromArray:@[@"public.data"]];
    if (type != nil && ![type isEqualToString:NSPasteboardTypeString] &&
        ![type isEqualToString:NSPasteboardTypeFileURL]) {
        NSData* data = [pasteboard dataForType:type];
        const char* media = type.UTF8String;
        if (data != nil && data.length != 0 && media != nullptr &&
            [type lengthOfBytesUsingEncoding:NSUTF8StringEncoding] <=
                DragLimits::maximum_media_type_bytes &&
            data.length <= DragLimits::maximum_total_bytes - total_bytes) {
            const std::uint8_t* begin = static_cast<const std::uint8_t*>(data.bytes);
            result.emplace_back(DragBinaryData{
                media, std::vector<std::uint8_t>(begin, begin + data.length)});
        }
    }
    if (result.size() > DragLimits::maximum_items) {
        return {};
    }
    return result;
}

@implementation GUIFormsView (Input)

- (DragEvent)dragEventFor:(id<NSDraggingInfo>)sender action:(DragAction)action {
    DragEvent event;
    event.action = action;
    event.session_id = std::max<std::uint64_t>(
        1, static_cast<std::uint64_t>(sender.draggingSequenceNumber));
    const NSPoint point = [self convertPoint:sender.draggingLocation fromView:nil];
    event.position = {point.x, point.y};
    event.allowed_effects = drag_effects_for(sender.draggingSourceOperationMask);
    if (action != DragAction::leave) {
        event.items = drag_items_for(sender.draggingPasteboard);
    }
    return event;
}

- (NSDragOperation)answerDraggingEntered:(id<NSDraggingInfo>)sender {
    DragEvent event = [self dragEventFor:sender action:DragAction::enter];
    const HostDispatchResult result = [self dispatchHostPayload:std::move(event)
        timestampNanoseconds:host_now_nanoseconds()];
    [self collectDamage];
    return native_drag_operation(result.drag_effect);
}

- (NSDragOperation)answerDraggingUpdated:(id<NSDraggingInfo>)sender {
    DragEvent event = [self dragEventFor:sender action:DragAction::over];
    const HostDispatchResult result = [self dispatchHostPayload:std::move(event)
        timestampNanoseconds:host_now_nanoseconds()];
    [self collectDamage];
    return native_drag_operation(result.drag_effect);
}

- (void)answerDraggingExited:(id<NSDraggingInfo>)sender {
    DragEvent event = [self dragEventFor:sender action:DragAction::leave];
    static_cast<void>([self dispatchHostPayload:std::move(event)
        timestampNanoseconds:host_now_nanoseconds()]);
    [self collectDamage];
}

- (BOOL)answerPrepareForDragOperation:(id<NSDraggingInfo>)sender {
    return _hostSession != nullptr;
}

- (BOOL)answerPerformDragOperation:(id<NSDraggingInfo>)sender {
    DragEvent event = [self dragEventFor:sender action:DragAction::drop];
    const HostDispatchResult result = [self dispatchHostPayload:std::move(event)
        timestampNanoseconds:host_now_nanoseconds()];
    [self collectDamage];
    return result.accepted() && result.handled;
}

- (GFPoint)modelPointForEvent:(NSEvent*)event {
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    return GFPoint{point.x, point.y};
}

- (void)dispatchPointerEvent:(NSEvent*)event action:(PointerAction)action {
    PointerEvent input;
    input.action = action;
    input.button = button_for(event.buttonNumber);
    input.position = [self modelPointForEvent:event];
    input.modifiers = modifiers_for(event.modifierFlags);
    input.pointer_id = 1;
    input.click_count = static_cast<std::uint32_t>(
        std::max<NSInteger>(1, event.clickCount));
    const GFPoint position = input.position;
    static_cast<void>([self dispatchHostPayload:std::move(input)
                               timestampNanoseconds:host_event_nanoseconds(event)]);
    [self updateCursorAtPoint:position];
    [self collectDamage];
}

- (void)updateCursorAtPoint:(GFPoint)position {
    if (_hostServices != nullptr) {
        const gui_forms::Control::Ptr captured = (*_model).captured_control();
        const gui_forms::Control::Ptr target = captured ? captured : (*_model).hit_test(position);
        static_cast<void>((*_hostServices).set_custom_cursor(
            target ? (*target).effective_cursor_images() : gui_forms::CursorImagesPtr{},
            self.window.backingScaleFactor,
            target ? (*target).effective_cursor() : CursorKind::arrow));
    }
}

- (void)answerCursorUpdate:(NSEvent*)event {
    [self updateCursorAtPoint:[self modelPointForEvent:event]];
}

- (void)answerMouseExited:(NSEvent*)event {
    if (_model && (*_model).captured_control()) return;
    if (_hostServices != nullptr) {
        static_cast<void>((*_hostServices).set_cursor(CursorKind::arrow));
    }
}

- (void)answerMouseDown:(NSEvent*)event {
    [self.window makeFirstResponder:self];
    if ((event.modifierFlags & NSEventModifierFlagControl) != 0) {
        _controlClickInProgress = YES;
        static_cast<void>([self menuForEvent:event]);
        return;
    }
    if (event.buttonNumber == 0 && !_windowDragRegionIds.empty() && _model) {
        const GFPoint point = [self modelPointForEvent:event];
        const gui_forms::WindowChromeHit hit =
            gui_forms::resolve_window_chrome_hit(
                *_model, point, _windowDragRegionIds);
        if (hit.begins_native_drag()) {
            [self.window performWindowDragWithEvent:event];
            return;
        }
    }
    [self dispatchPointerEvent:event action:PointerAction::down];
}

- (void)answerMouseUp:(NSEvent*)event {
    if (_controlClickInProgress) {
        _controlClickInProgress = NO;
        return;
    }
    [self dispatchPointerEvent:event action:PointerAction::up];
}

- (void)answerRightMouseDown:(NSEvent*)event {
    [self dispatchPointerEvent:event action:PointerAction::down];
    // NSView's own rightMouseDown: asks menuForEvent: for the menu, which
    // reports the context request (owner's mouse-event rule).
    [super rightMouseDown:event];
}

// AppKit's context-menu request: the right button's press, or a
// Control-click, reported to the window as a pointer HostContextRequestEvent.
// GUI.Forms shows its own menus, so AppKit is given none.
- (NSMenu*)answerMenuForEvent:(NSEvent*)event {
    if (!_model) return nil;
    HostContextRequestEvent request{};
    request.origin = ContextRequestOrigin::pointer;
    request.position = [self modelPointForEvent:event];
    static_cast<void>([self dispatchHostPayload:std::move(request)
                               timestampNanoseconds:host_event_nanoseconds(event)]);
    [self collectDamage];
    return nil;
}

- (BOOL)dispatchKeyboardContextRequest {
    if (!_model) return NO;
    HostContextRequestEvent request{};
    request.origin = ContextRequestOrigin::keyboard;
    const HostDispatchResult result = [self dispatchHostPayload:std::move(request)
                                           timestampNanoseconds:host_now_nanoseconds()];
    [self collectDamage];
    return result.handled;
}

- (void)answerRightMouseUp:(NSEvent*)event {
    [self dispatchPointerEvent:event action:PointerAction::up];
}

- (void)answerOtherMouseDown:(NSEvent*)event {
    [self dispatchPointerEvent:event action:PointerAction::down];
}

- (void)answerOtherMouseUp:(NSEvent*)event {
    [self dispatchPointerEvent:event action:PointerAction::up];
}

- (void)answerMouseMoved:(NSEvent*)event {
    [self dispatchPointerEvent:event action:PointerAction::move];
}

- (void)answerMouseDragged:(NSEvent*)event {
    if (_controlClickInProgress) return;
    [self dispatchPointerEvent:event action:PointerAction::move];
}

- (void)answerRightMouseDragged:(NSEvent*)event {
    [self dispatchPointerEvent:event action:PointerAction::move];
}

- (void)answerOtherMouseDragged:(NSEvent*)event {
    [self dispatchPointerEvent:event action:PointerAction::move];
}

- (void)answerScrollWheel:(NSEvent*)event {
    PointerEvent input;
    input.action = PointerAction::wheel;
    input.position = [self modelPointForEvent:event];
    // A trackpad or smooth-scrolling mouse reports precise deltas in points
    // (logical pixels), its momentum phase included, scrolled exactly; a
    // detented wheel reports lines, possibly fractional where AppKit
    // accelerates them. Every event is dispatched: none is coalesced.
    input.wheel_delta = GFPoint{event.scrollingDeltaX, event.scrollingDeltaY};
    input.wheel_unit = event.hasPreciseScrollingDeltas ? WheelDeltaUnit::pixels : WheelDeltaUnit::lines;
    input.modifiers = modifiers_for(event.modifierFlags);
    input.pointer_id = 1;
    static_cast<void>([self dispatchHostPayload:std::move(input)
                               timestampNanoseconds:host_event_nanoseconds(event)]);
    [self collectDamage];
}

- (void)answerKeyDown:(NSEvent*)event {
    KeyEvent input;
    input.action = KeyAction::down;
    input.physical_key = physical_key_for(event.keyCode);
    input.scan_code = event.keyCode;
    input.modifiers = modifiers_for(event.modifierFlags);
    input.repeat = event.isARepeat;
    // While the input method holds marked text it owns every key: Return
    // commits, Escape cancels, arrows and Backspace edit the composition
    // (audit N105). The model sees the key only when no composition is
    // open, as before.
    if ([self hasMarkedText]) {
        [self interpretKeyEvents:@[event]];
        [self collectDamage];
        return;
    }
    const bool handled = [self dispatchHostPayload:std::move(input)
                               timestampNanoseconds:host_event_nanoseconds(event)].handled;
    if (!handled) {
        [self interpretKeyEvents:@[event]];
    }
    [self collectDamage];
}

- (void)answerKeyUp:(NSEvent*)event {
    KeyEvent input;
    input.action = KeyAction::up;
    input.physical_key = physical_key_for(event.keyCode);
    input.scan_code = event.keyCode;
    input.modifiers = modifiers_for(event.modifierFlags);
    static_cast<void>([self dispatchHostPayload:std::move(input)
                               timestampNanoseconds:host_event_nanoseconds(event)]);
    [self collectDamage];
}

// A modifier key went down or up: AppKit reports it as a change of the
// modifier flags rather than as keyDown/keyUp. The key is down when its
// own device-dependent flag (the side's bit, IOKit's NX_DEVICE*KEYMASK) is
// set; Caps Lock toggles, so it is down when its flag turned on.
- (void)answerFlagsChanged:(NSEvent*)event {
    const std::uint32_t physical = physical_key_for(event.keyCode);
    const NSUInteger flags = event.modifierFlags;
    bool down = false;
    switch (event.keyCode) {
    case 54: down = (flags & 0x10U) != 0U; break;   // Right Command
    case 55: down = (flags & 0x08U) != 0U; break;   // Left Command
    case 56: down = (flags & 0x02U) != 0U; break;   // Left Shift
    case 57: down = (flags & NSEventModifierFlagCapsLock) != 0U; break;
    case 58: down = (flags & 0x20U) != 0U; break;   // Left Option
    case 59: down = (flags & 0x01U) != 0U; break;   // Left Control
    case 60: down = (flags & 0x04U) != 0U; break;   // Right Shift
    case 61: down = (flags & 0x40U) != 0U; break;   // Right Option
    case 62: down = (flags & 0x2000U) != 0U; break; // Right Control
    default: break;
    }
    if (physical == 0U) {
        [super flagsChanged:event];
        return;
    }
    KeyEvent input;
    input.action = down ? KeyAction::down : KeyAction::up;
    input.physical_key = physical;
    input.scan_code = event.keyCode;
    input.modifiers = modifiers_for(flags);
    static_cast<void>([self dispatchHostPayload:std::move(input)
                               timestampNanoseconds:host_event_nanoseconds(event)]);
    [self collectDamage];
}

- (void)answerInsertText:(id)string replacementRange:(NSRange)replacementRange {
    TextInputEvent input;
    input.text_utf8 = utf8_string(string);
    input.composing = false;
    if (replacementRange.location != NSNotFound) {
        input.replacement_start = static_cast<std::int32_t>(replacementRange.location);
        input.replacement_length = static_cast<std::int32_t>(replacementRange.length);
    }
    static_cast<void>([self dispatchHostPayload:std::move(input)
                               timestampNanoseconds:host_now_nanoseconds()]);
    [_markedText setAttributedString:[[NSAttributedString alloc] initWithString:@""]];
    [self collectDamage];
}

- (void)answerSetMarkedText:(id)string
         selectedRange:(NSRange)selectedRange
       replacementRange:(NSRange)replacementRange {
    NSString* plain = [string isKindOfClass:[NSAttributedString class]]
        ? [(NSAttributedString*)string string]
        : (NSString*)string;
    [_markedText setAttributedString:[[NSAttributedString alloc]
        initWithString:(plain == nil ? @"" : plain)]];
    _selectedRange = selectedRange;
    TextInputEvent input;
    input.text_utf8 = utf8_string(string);
    input.composing = true;
    if (replacementRange.location != NSNotFound) {
        input.replacement_start = static_cast<std::int32_t>(replacementRange.location);
        input.replacement_length = static_cast<std::int32_t>(replacementRange.length);
    }
    static_cast<void>([self dispatchHostPayload:std::move(input)
                               timestampNanoseconds:host_now_nanoseconds()]);
    [self collectDamage];
}

// The system asks to keep the marked text as typed (NSTextInputClient):
// it is committed as ordinary input, replacing the composition (N105).
- (void)answerUnmarkText {
    if (_markedText.length != 0) {
        TextInputEvent input;
        input.text_utf8 = utf8_string(_markedText.string);
        input.composing = false;
        static_cast<void>([self dispatchHostPayload:std::move(input)
                                   timestampNanoseconds:host_now_nanoseconds()]);
    }
    [_markedText setAttributedString:[[NSAttributedString alloc] initWithString:@""]];
    [self collectDamage];
}

- (BOOL)answerHasMarkedText {
    return _markedText.length != 0;
}

- (NSRange)answerMarkedRange {
    return [self hasMarkedText] ? NSMakeRange(0, _markedText.length)
                                : NSMakeRange(NSNotFound, 0);
}

- (NSRange)answerSelectedRange {
    return _selectedRange;
}

- (nullable NSAttributedString*)answerAttributedSubstringForProposedRange:(NSRange)range
                                                        actualRange:(nullable NSRangePointer)actualRange {
    if (actualRange != nullptr) {
        *actualRange = NSMakeRange(NSNotFound, 0);
    }
    return nil;
}

- (NSArray<NSAttributedStringKey>*)answerValidAttributesForMarkedText {
    return @[];
}

- (NSRect)answerFirstRectForCharacterRange:(NSRange)range
                          actualRange:(nullable NSRangePointer)actualRange {
    if (actualRange != nullptr) {
        *actualRange = range;
    }
    // The window's text input caret (SurfaceRoot::text_input_caret): the
    // focused control's own caret, a sub-surface host's forwarded caret, or
    // the focused control's left edge. Without one, a box at the origin.
    NSRect caret = NSMakeRect(0.0, 0.0, 1.0, 18.0);
    if (_model) {
        const std::optional<GFRect> inputCaret = (*_model).text_input_caret();
        if (inputCaret) {
            caret = NSMakeRect((*inputCaret).x, (*inputCaret).y, (*inputCaret).width, (*inputCaret).height);
        }
    }
    const NSRect inWindow = [self convertRect:caret toView:nil];
    return [self.window convertRectToScreen:inWindow];
}

- (void)answerDoCommandBySelector:(SEL)selector {
    // Physical command keys are handled by the retained control first. The
    // initial text milestone intentionally leaves unimplemented Cocoa editing
    // selectors inert instead of mutating an invisible native text field.
}

@end

// This unit's link anchor (macos_view.hpp): referenced from
// link_view_categories in macos_view.mm so that the archive member holding
// the Input category is linked wherever GUIFormsView is.
namespace gui_forms::host::macos {
void gui_forms_macos_view_input_anchor() noexcept {}
} // namespace gui_forms::host::macos
