#include "application_menu.hpp"

#import <AppKit/AppKit.h>

namespace gui_forms::host::detail {

namespace {

// A non-empty string value of `key`, or nil. Info.plist values are
// untyped, so a non-string value counts as absent.
NSString* bundle_string(NSBundle* bundle, NSString* key) {
    const id value = [bundle objectForInfoDictionaryKey:key];
    if (![value isKindOfClass:[NSString class]] || [static_cast<NSString*>(value) length] == 0) { return nil; }
    return static_cast<NSString*>(value);
}

std::string utf8(NSString* text) {
    const char* bytes = [text UTF8String];
    return bytes == nullptr ? std::string{} : std::string{bytes};
}

} // namespace

std::string application_menu_name(const std::filesystem::path& bundle_path) {
    @autoreleasepool {
        NSBundle* bundle = bundle_path.empty()
            ? [NSBundle mainBundle]
            : [NSBundle bundleWithPath:[NSString stringWithUTF8String:bundle_path.c_str()]];
        for (NSString* key in @[@"CFBundleDisplayName", @"CFBundleName"]) {
            if (NSString* name = bundle_string(bundle, key)) { return utf8(name); }
        }
        return utf8([[NSProcessInfo processInfo] processName]);
    }
}

std::string quit_menu_title(std::string_view program_name) {
    std::string title{"Quit "};
    title.append(program_name);
    return title;
}

void install_application_menu() {
    NSMenu* menuBar = [[NSMenu alloc] init];
    NSMenuItem* applicationItem = [[NSMenuItem alloc] init];
    [menuBar addItem:applicationItem];
    NSMenu* applicationMenu = [[NSMenu alloc] init];
    const std::string title = quit_menu_title(application_menu_name());
    NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:title.c_str()]
                                                  action:@selector(terminate:)
                                           keyEquivalent:@"q"];
    [applicationMenu addItem:quit];
    [applicationItem setSubmenu:applicationMenu];
    [NSApp setMainMenu:menuBar];
}

} // namespace gui_forms::host::detail
