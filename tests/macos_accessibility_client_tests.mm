#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/plots/decimation/decimation_kernels.hpp"
#include "gui_forms/plots/series_column/series_column.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "support/accessibility_actions.hpp"
#include "support/macos_accessibility_contract.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <utility>
#include <unistd.h>
#include <vector>

// Stage A, macOS out-of-process AX client test. It launches the fixture app
// (macos_accessibility_fixture_app: the stock-control fixture, or the Gallery with
// --gallery) through LaunchServices and inspects it as an assistive client
// does, through AXUIElementCreateApplication. Every element is compared
// with the semantic node it projects (the contract in
// support/macos_accessibility_contract), against the semantic snapshot the
// app writes to its dump. In the fixture it performs each control's default
// action through AXUIElementPerformAction or an attribute setter and
// observes the change through the dump, and checks that an element held
// past its control's disposal returns an error (interview GF017), and reads
// a text box's parameterized text attributes and sets its AXSelectedTextRange
// (A-T1). In the Gallery it presses every page's navigation row and compares
// each page.
//
// An AX client needs the Accessibility permission. Without it the test
// prints "unsupported" and exits with 77, which ctest reports as skipped; it
// never passes silently.

namespace {

using gui_forms::SemanticAction;
using gui_forms::SemanticNode;
using gui_forms::SemanticRole;
using gui_forms::SemanticSnapshot;
using gui_forms::SemanticState;
using gui_forms::has_semantic_state;
using gui_forms::test_support::AccessibilityProjection;
using gui_forms::test_support::default_action_node;
using gui_forms::test_support::default_action_value;
using gui_forms::test_support::expected_ax_projection;
using gui_forms::test_support::find_semantic_node;
using gui_forms::test_support::has_semantic_action;
using gui_forms::test_support::semantic_number_text;
using gui_forms::test_support::semantic_subtree_signature;

constexpr int unsupported_exit = 77;
constexpr const char* retained_surface_label = "GUI.Forms retained surface";

// ---------------------------------------------------------------------------
// Core Foundation ownership: a Copy or Create result, released once.

template <typename Reference>
class CfOwned final {
public:
    CfOwned() noexcept = default;
    explicit CfOwned(Reference reference) noexcept : reference_(reference) {}
    ~CfOwned() { reset(); }
    CfOwned(const CfOwned&) = delete;
    CfOwned& operator=(const CfOwned&) = delete;
    CfOwned(CfOwned&& other) noexcept : reference_(other.reference_) { other.reference_ = nullptr; }
    CfOwned& operator=(CfOwned&& other) noexcept {
        if (this != &other) {
            reset();
            reference_ = other.reference_;
            other.reference_ = nullptr;
        }
        return *this;
    }
    [[nodiscard]] Reference get() const noexcept { return reference_; }
    // Storage for a Copy function's out parameter; the result is owned.
    Reference* receive() noexcept {
        reset();
        return &reference_;
    }
    void reset() noexcept {
        if (reference_ != nullptr) CFRelease(reference_);
        reference_ = nullptr;
    }

private:
    Reference reference_{};
};

using AxElement = CfOwned<AXUIElementRef>;
using CfValue = CfOwned<CFTypeRef>;

std::string utf8(CFStringRef text) {
    if (text == nullptr) return {};
    NSString* bridged = (__bridge NSString*)text;
    const char* bytes = bridged.UTF8String;
    return bytes == nullptr ? std::string{} : std::string(bytes);
}

AXError copy_attribute(AXUIElementRef element, CFStringRef attribute, CfValue& value) {
    return AXUIElementCopyAttributeValue(element, attribute, value.receive());
}

std::string string_attribute(AXUIElementRef element, CFStringRef attribute) {
    CfValue value;
    if (copy_attribute(element, attribute, value) != kAXErrorSuccess || value.get() == nullptr) return {};
    if (CFGetTypeID(value.get()) != CFStringGetTypeID()) return {};
    return utf8(static_cast<CFStringRef>(value.get()));
}

bool bool_attribute(AXUIElementRef element, CFStringRef attribute) {
    CfValue value;
    if (copy_attribute(element, attribute, value) != kAXErrorSuccess || value.get() == nullptr) return false;
    if (CFGetTypeID(value.get()) == CFBooleanGetTypeID()) return CFBooleanGetValue(static_cast<CFBooleanRef>(value.get()));
    if (CFGetTypeID(value.get()) == CFNumberGetTypeID()) {
        int number = 0;
        CFNumberGetValue(static_cast<CFNumberRef>(value.get()), kCFNumberIntType, &number);
        return number != 0;
    }
    return false;
}

// A number attribute, or 0 when the element has none.
long long integer_attribute(AXUIElementRef element, CFStringRef attribute) {
    CfValue value;
    if (copy_attribute(element, attribute, value) != kAXErrorSuccess || value.get() == nullptr) return 0;
    if (CFGetTypeID(value.get()) != CFNumberGetTypeID()) return 0;
    long long number = 0;
    CFNumberGetValue(static_cast<CFNumberRef>(value.get()), kCFNumberLongLongType, &number);
    return number;
}

// A range attribute as "location+length", or "" when the element has none
// or its location is kCFNotFound.
std::string range_attribute(AXUIElementRef element, CFStringRef attribute) {
    CfValue value;
    if (copy_attribute(element, attribute, value) != kAXErrorSuccess || value.get() == nullptr) return {};
    if (CFGetTypeID(value.get()) != AXValueGetTypeID()) return {};
    CFRange range = CFRangeMake(kCFNotFound, 0);
    if (!AXValueGetValue(static_cast<AXValueRef>(value.get()), kAXValueTypeCFRange, &range)) return {};
    // No range: kCFNotFound, or AppKit's NSNotFound (LONG_MAX), which an
    // NSRange of {NSNotFound, 0} becomes on its way through AXValue.
    if (range.location == kCFNotFound || range.location == LONG_MAX) return {};
    const std::string text = std::to_string(static_cast<long long>(range.location)) + "+" +
                             std::to_string(static_cast<long long>(range.length));
    return text;
}

std::string value_attribute(AXUIElementRef element) {
    CfValue value;
    if (copy_attribute(element, kAXValueAttribute, value) != kAXErrorSuccess || value.get() == nullptr) return {};
    const CFTypeID type = CFGetTypeID(value.get());
    if (type == CFStringGetTypeID()) return utf8(static_cast<CFStringRef>(value.get()));
    if (type == CFBooleanGetTypeID()) return CFBooleanGetValue(static_cast<CFBooleanRef>(value.get())) ? "1" : "0";
    if (type == CFNumberGetTypeID()) {
        double number = 0.0;
        CFNumberGetValue(static_cast<CFNumberRef>(value.get()), kCFNumberDoubleType, &number);
        return semantic_number_text(number);
    }
    return {};
}

bool settable(AXUIElementRef element, CFStringRef attribute) {
    Boolean result = false;
    if (AXUIElementIsAttributeSettable(element, attribute, &result) != kAXErrorSuccess) return false;
    return result;
}

// The element's children, each retained by the returned owners.
std::vector<AxElement> children_of(AXUIElementRef element) {
    std::vector<AxElement> children;
    CfValue value;
    if (copy_attribute(element, kAXChildrenAttribute, value) != kAXErrorSuccess || value.get() == nullptr) {
        return children;
    }
    if (CFGetTypeID(value.get()) != CFArrayGetTypeID()) return children;
    const CFArrayRef array = static_cast<CFArrayRef>(value.get());
    const CFIndex count = CFArrayGetCount(array);
    children.reserve(static_cast<std::size_t>(count));
    for (CFIndex index = 0; index < count; ++index) {
        AXUIElementRef child = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(array, index));
        CFRetain(child);
        children.push_back(AxElement(child));
    }
    return children;
}

AccessibilityProjection published_projection(AXUIElementRef element) {
    AccessibilityProjection published;
    published.role = string_attribute(element, kAXRoleAttribute);
    published.subrole = string_attribute(element, kAXSubroleAttribute);
    published.label = string_attribute(element, kAXDescriptionAttribute);
    published.identifier = string_attribute(element, kAXIdentifierAttribute);
    published.value = value_attribute(element);
    published.enabled = bool_attribute(element, kAXEnabledAttribute);
    published.focused = bool_attribute(element, kAXFocusedAttribute);
    published.selected = bool_attribute(element, kAXSelectedAttribute);
    published.expanded = bool_attribute(element, kAXExpandedAttribute);
    CfOwned<CFArrayRef> actions;
    if (AXUIElementCopyActionNames(element, actions.receive()) == kAXErrorSuccess && actions.get() != nullptr) {
        const CFIndex count = CFArrayGetCount(actions.get());
        for (CFIndex index = 0; index < count; ++index) {
            const std::string name = utf8(static_cast<CFStringRef>(CFArrayGetValueAtIndex(actions.get(), index)));
            if (name == "AXPress") published.press = true;
            if (name == "AXShowMenu") published.show_menu = true;
            if (name == "AXIncrement") published.increment = true;
            if (name == "AXDecrement") published.decrement = true;
            if (name.find("Name:Close") != std::string::npos) published.close_action = true;
        }
    }
    published.focus_settable = settable(element, kAXFocusedAttribute);
    published.value_settable = settable(element, kAXValueAttribute);
    published.selected_settable = settable(element, kAXSelectedAttribute);
    published.disclosure_level = integer_attribute(element, kAXDisclosureLevelAttribute);
    published.row_range = range_attribute(element, kAXRowIndexRangeAttribute);
    published.column_range = range_attribute(element, kAXColumnIndexRangeAttribute);
    published.row_count = integer_attribute(element, kAXRowCountAttribute);
    published.column_count = integer_attribute(element, kAXColumnCountAttribute);
    published.modal = bool_attribute(element, kAXModalAttribute);
    published.character_count = integer_attribute(element, kAXNumberOfCharactersAttribute);
    published.selected_text_range = range_attribute(element, kAXSelectedTextRangeAttribute);
    return published;
}

// ---------------------------------------------------------------------------
// The app's dump.

struct Dump final {
    bool valid{};
    pid_t pid{};
    std::uint64_t generation{};
    std::uint64_t effects{};
    // The client's request number the app had read when it wrote this dump,
    // and whether the app was settled then (see the fixture app).
    std::uint64_t request{};
    bool settled{};
    // What the app's "settled" was made of, for a named failure.
    bool frame_pending{};
    bool occluded{};
    bool motion_armed{};
    bool freeze_pending{};
    // The app's paint count and paint lease (state and revisions).
    std::uint64_t paint_passes{};
    std::string lease;
    std::uint64_t content_revision{};
    std::uint64_t rendered_revision{};
    std::uint64_t presented_revision{};
    // Control type by stable id: (stable id, manifest id).
    std::vector<std::pair<std::string, std::string>> types;
    SemanticSnapshot snapshot;
    // The dump file's bytes, as read.
    std::string text;
};

std::string ns_text(id value) {
    if (![value isKindOfClass:[NSString class]]) return {};
    const char* bytes = ((NSString*)value).UTF8String;
    return bytes == nullptr ? std::string{} : std::string(bytes);
}

std::optional<double> ns_number(id value) {
    if (![value isKindOfClass:[NSNumber class]]) return std::nullopt;
    return ((NSNumber*)value).doubleValue;
}

SemanticRole role_named(const std::string& name) {
    for (std::size_t index = 0U; index < gui_forms::semantic_role_count; ++index) {
        const SemanticRole role = static_cast<SemanticRole>(index);
        if (name == gui_forms::semantic_role_name(role)) return role;
    }
    return SemanticRole::generic;
}

bool action_named(const std::string& name, SemanticAction& action) {
    for (std::size_t index = 0U; index < gui_forms::semantic_action_count; ++index) {
        const SemanticAction candidate = static_cast<SemanticAction>(index);
        if (name == gui_forms::semantic_action_name(candidate)) {
            action = candidate;
            return true;
        }
    }
    return false;
}

SemanticNode node_from_json(NSDictionary* object) {
    SemanticNode node;
    node.stable_id = ns_text(object[@"stable_id"]);
    node.role = role_named(ns_text(object[@"role"]));
    node.name = ns_text(object[@"name"]);
    node.value = ns_text(object[@"value"]);
    node.description = ns_text(object[@"description"]);
    node.numeric_value = ns_number(object[@"numeric_value"]);
    // The stage A fields the AX projection reads (12.13); null in the dump
    // is no value.
    const std::optional<double> level = ns_number(object[@"level"]);
    if (level) node.level = static_cast<std::size_t>(*level);
    const std::optional<double> row_index = ns_number(object[@"row_index"]);
    if (row_index) node.row_index = static_cast<std::size_t>(*row_index);
    const std::optional<double> column_index = ns_number(object[@"column_index"]);
    if (column_index) node.column_index = static_cast<std::size_t>(*column_index);
    const std::optional<double> row_count = ns_number(object[@"row_count"]);
    if (row_count) node.row_count = static_cast<std::size_t>(*row_count);
    const std::optional<double> column_count = ns_number(object[@"column_count"]);
    if (column_count) node.column_count = static_cast<std::size_t>(*column_count);
    node.modal = ns_number(object[@"modal"]).value_or(0.0) != 0.0;
    node.multiline = ns_number(object[@"multiline"]).value_or(0.0) != 0.0;
    NSDictionary* selection = object[@"text_selection"];
    if ([selection isKindOfClass:[NSDictionary class]]) {
        const double anchor = ns_number(selection[@"anchor"]).value_or(0.0);
        const double caret = ns_number(selection[@"caret"]).value_or(0.0);
        node.text_selection = gui_forms::SemanticTextSelection{
            .anchor = gui_forms::Utf8Offset(static_cast<std::size_t>(anchor)),
            .caret = gui_forms::Utf8Offset(static_cast<std::size_t>(caret))};
    }
    node.minimum_value = ns_number(object[@"minimum_value"]);
    node.maximum_value = ns_number(object[@"maximum_value"]);
    const std::optional<double> states = ns_number(object[@"states"]);
    node.states = static_cast<SemanticState>(states ? static_cast<std::uint32_t>(*states) : 0U);
    for (id name in (NSArray*)object[@"actions"]) {
        SemanticAction action = SemanticAction::focus;
        if (action_named(ns_text(name), action)) node.actions.push_back(action);
    }
    for (NSDictionary* child in (NSArray*)object[@"children"]) node.children.push_back(node_from_json(child));
    return node;
}

Dump read_dump(const std::string& path) {
    Dump dump;
    NSData* data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path.c_str()]];
    if (data == nil) return dump;
    dump.text.assign(static_cast<const char*>(data.bytes), static_cast<std::size_t>(data.length));
    NSError* error = nil;
    NSDictionary* object = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
    if (![object isKindOfClass:[NSDictionary class]]) return dump;
    dump.pid = static_cast<pid_t>(ns_number(object[@"pid"]).value_or(0.0));
    dump.generation = static_cast<std::uint64_t>(ns_number(object[@"generation"]).value_or(0.0));
    dump.effects = static_cast<std::uint64_t>(ns_number(object[@"effects"]).value_or(0.0));
    dump.request = static_cast<std::uint64_t>(ns_number(object[@"request"]).value_or(0.0));
    dump.settled = ns_number(object[@"settled"]).value_or(0.0) != 0.0;
    dump.frame_pending = ns_number(object[@"frame_pending"]).value_or(0.0) != 0.0;
    dump.occluded = ns_number(object[@"occluded"]).value_or(0.0) != 0.0;
    dump.motion_armed = ns_number(object[@"motion_armed"]).value_or(0.0) != 0.0;
    dump.freeze_pending = ns_number(object[@"freeze_pending"]).value_or(0.0) != 0.0;
    dump.paint_passes = static_cast<std::uint64_t>(ns_number(object[@"paint_passes"]).value_or(0.0));
    dump.lease = ns_text(object[@"lease"]);
    dump.content_revision = static_cast<std::uint64_t>(ns_number(object[@"content_revision"]).value_or(0.0));
    dump.rendered_revision = static_cast<std::uint64_t>(ns_number(object[@"rendered_revision"]).value_or(0.0));
    dump.presented_revision =
        static_cast<std::uint64_t>(ns_number(object[@"presented_revision"]).value_or(0.0));
    NSDictionary* types = object[@"types"];
    for (NSString* key in types) dump.types.emplace_back(ns_text(key), ns_text(types[key]));
    NSDictionary* snapshot = object[@"snapshot"];
    dump.snapshot.generation = dump.generation;
    for (NSDictionary* root in (NSArray*)snapshot[@"roots"]) dump.snapshot.roots.push_back(node_from_json(root));
    dump.valid = dump.pid > 0;
    return dump;
}

std::string control_type(const Dump& dump, std::string_view stable_id) {
    for (const std::pair<std::string, std::string>& entry : dump.types) {
        if (entry.first == stable_id) return entry.second;
    }
    return {};
}

void sleep_milliseconds(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

// The dump after `previous`: the first whose generation or effect count
// differs, or the latest when `timeout_ms` passes.
Dump wait_for_new_dump(const std::string& path, const Dump& previous, int timeout_ms) {
    Dump latest = previous;
    for (int waited = 0; waited < timeout_ms; waited += 20) {
        const Dump current = read_dump(path);
        if (current.valid) {
            latest = current;
            if (current.generation != previous.generation || current.effects != previous.effects) return current;
        }
        sleep_milliseconds(20);
    }
    return latest;
}

// ---------------------------------------------------------------------------
// Finding the app's retained surface.

AxElement find_described(AXUIElementRef element, std::string_view description, int depth) {
    if (string_attribute(element, kAXDescriptionAttribute) == description) {
        CFRetain(element);
        return AxElement(element);
    }
    if (depth == 0) return AxElement();
    for (const AxElement& child : children_of(element)) {
        AxElement found = find_described(child.get(), description, depth - 1);
        if (found.get() != nullptr) return found;
    }
    return AxElement();
}

AxElement find_identified(AXUIElementRef element, std::string_view identifier) {
    for (const AxElement& child : children_of(element)) {
        if (string_attribute(child.get(), kAXIdentifierAttribute) == identifier) {
            CFRetain(child.get());
            return AxElement(child.get());
        }
        AxElement found = find_identified(child.get(), identifier);
        if (found.get() != nullptr) return found;
    }
    return AxElement();
}

AxElement find_surface(AXUIElementRef application) {
    CfValue windows;
    if (copy_attribute(application, kAXWindowsAttribute, windows) != kAXErrorSuccess || windows.get() == nullptr) {
        return AxElement();
    }
    const CFArrayRef array = static_cast<CFArrayRef>(windows.get());
    for (CFIndex index = 0; index < CFArrayGetCount(array); ++index) {
        AXUIElementRef window = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(array, index));
        AxElement surface = find_described(window, retained_surface_label, 4);
        if (surface.get() != nullptr) return surface;
    }
    return AxElement();
}

// ---------------------------------------------------------------------------
// Comparison and the per-control report.

struct Mismatch final {
    std::string phase;
    std::string control;
    std::string stable_id;
    std::string field;
    std::string expected;
    std::string published;
};

struct TypeTally final {
    std::string control_type;
    std::vector<std::string> instances;
    std::size_t nodes{};
    std::size_t matched{};
    std::string default_action;
    bool action_observed{};
};

struct Report final {
    std::vector<TypeTally> types;
    std::vector<Mismatch> mismatches;
    std::vector<std::string> failures;
    std::vector<std::string> notes;
    // Set by the first settle that timed out: the run then stops, since
    // every later comparison would read an unsettled app.
    bool settle_failed{};
};

TypeTally& tally_for(Report& report, const std::string& control_type) {
    for (TypeTally& tally : report.types) {
        if (tally.control_type == control_type) return tally;
    }
    report.types.push_back(TypeTally{control_type});
    return report.types.back();
}

// ---------------------------------------------------------------------------
// Settling. The client asks the app for a settled dump by writing a new
// request number beside the dump; the app's writer echoes the number it
// read and says whether it was settled (painted, nothing invalidated, no
// value moving on the motion clock; an endless ambient effect may run).
// Quiet is never inferred from timing.

constexpr int settle_timeout_ms = 10000;

std::string request_path(const std::string& dump_path) {
    const std::string path = dump_path + ".request";
    return path;
}

// The number the client last wrote, or 0 before the first request. Only
// this client writes the file.
std::uint64_t last_request(const std::string& dump_path) {
    const std::string path = request_path(dump_path);
    std::FILE* file = std::fopen(path.c_str(), "r");
    if (file == nullptr) return 0U;
    unsigned long long value = 0U;
    const int fields = std::fscanf(file, "%llu", &value);
    static_cast<void>(std::fclose(file));
    if (fields != 1) return 0U;
    const std::uint64_t request = static_cast<std::uint64_t>(value);
    return request;
}

// Replaces the request file by rename, so the app reads a whole number.
bool write_request(const std::string& dump_path, std::uint64_t request) {
    const std::string path = request_path(dump_path);
    const std::string temporary = path + ".tmp";
    std::FILE* file = std::fopen(temporary.c_str(), "w");
    if (file == nullptr) return false;
    const int printed = std::fprintf(file, "%llu\n", static_cast<unsigned long long>(request));
    const int closed = std::fclose(file);
    if (printed <= 0 || closed != 0) return false;
    const int renamed = std::rename(temporary.c_str(), path.c_str());
    return renamed == 0;
}

const char* yes_no(bool value) noexcept { return value ? "yes" : "no"; }

// The first dump that answers a new request and is settled. A request the
// app has not answered settled within settle_timeout_ms (measured on the
// steady clock, file reads included) is a failure named for `where`, with
// what the app's last dump said held it; the latest dump is returned then
// and report.settle_failed is set. After that, it returns the current dump
// at once, so that a run that cannot settle ends well inside the test's
// timeout.
Dump settled_dump(const std::string& path, Report& report, const std::string& where) {
    if (report.settle_failed) {
        const Dump current = read_dump(path);
        return current;
    }
    const std::uint64_t request = last_request(path) + 1U;
    if (!write_request(path, request)) {
        report.failures.push_back("settle (" + where + "): could not write the request file");
        const Dump current = read_dump(path);
        return current;
    }
    Dump latest = read_dump(path);
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(settle_timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const Dump current = read_dump(path);
        if (current.valid) {
            latest = current;
            if (current.request >= request && current.settled) return current;
        }
        sleep_milliseconds(20);
    }
    report.settle_failed = true;
    report.failures.push_back("settle (" + where + "): the app answered request " + std::to_string(request) +
                              " with no settled dump within " + std::to_string(settle_timeout_ms) +
                              " ms (last dump: request " + std::to_string(latest.request) + ", settled " +
                              yes_no(latest.settled) + ", frame pending " + yes_no(latest.frame_pending) +
                              ", occluded " + yes_no(latest.occluded) + ", motion armed " +
                              yes_no(latest.motion_armed) + ", freeze pending " + yes_no(latest.freeze_pending) +
                              ", paints " + std::to_string(latest.paint_passes) + ", paint lease " + latest.lease +
                              " content " + std::to_string(latest.content_revision) + " rendered " +
                              std::to_string(latest.rendered_revision) + " presented " +
                              std::to_string(latest.presented_revision) + "); the run stops here");
    return latest;
}

void note_instance(TypeTally& tally, const std::string& stable_id) {
    for (const std::string& instance : tally.instances) {
        if (instance == stable_id) return;
    }
    tally.instances.push_back(stable_id);
}

class TreeComparison final {
public:
    TreeComparison(const Dump& dump, Report& report, std::string phase)
        : dump_(dump), report_(report), phase_(std::move(phase)) {}

    // `owner` is the nearest control at or above the node: its manifest id
    // and stable id.
    void compare(const SemanticNode& node, AXUIElementRef element, std::string owner_type,
                 std::string owner_id) {
        const std::string type = control_type(dump_, node.stable_id);
        if (!type.empty()) {
            owner_type = type == "control" ? "control:" + node.stable_id : type;
            owner_id = node.stable_id;
        }
        bool matches = true;
        std::vector<AxElement> children;
        if (element == nullptr) {
            add(owner_type, node.stable_id, "element", "present", "missing");
            matches = false;
        } else {
            const AccessibilityProjection expected = expected_ax_projection(node);
            const AccessibilityProjection published = published_projection(element);
            matches = compare_projection(owner_type, node.stable_id, expected, published);
            children = children_of(element);
        }
        if (children.size() != node.children.size()) {
            add(owner_type, node.stable_id, "children", std::to_string(node.children.size()),
                std::to_string(children.size()));
            matches = false;
        }
        if (!owner_type.empty()) {
            TypeTally& tally = tally_for(report_, owner_type);
            note_instance(tally, owner_id);
            ++tally.nodes;
            if (matches) ++tally.matched;
        }
        for (std::size_t index = 0U; index < node.children.size(); ++index) {
            AXUIElementRef child = index < children.size() ? children[index].get() : nullptr;
            compare(node.children[index], child, owner_type, owner_id);
        }
    }

private:
    void add(const std::string& owner, const std::string& stable_id, const char* field,
             const std::string& expected, const std::string& published) {
        report_.mismatches.push_back(Mismatch{phase_, owner, stable_id, field, expected, published});
    }

    bool text(const std::string& owner, const std::string& id, const char* field,
              const std::string& expected, const std::string& published) {
        if (expected == published) return true;
        add(owner, id, field, expected, published);
        return false;
    }

    bool flag(const std::string& owner, const std::string& id, const char* field, bool expected,
              bool published) {
        if (expected == published) return true;
        add(owner, id, field, expected ? "true" : "false", published ? "true" : "false");
        return false;
    }

    bool number(const std::string& owner, const std::string& id, const char* field, long long expected,
                long long published) {
        if (expected == published) return true;
        add(owner, id, field, std::to_string(expected), std::to_string(published));
        return false;
    }

    bool compare_projection(const std::string& owner, const std::string& id,
                            const AccessibilityProjection& expected,
                            const AccessibilityProjection& published) {
        bool same = true;
        same = text(owner, id, "role", expected.role, published.role) && same;
        same = text(owner, id, "subrole", expected.subrole, published.subrole) && same;
        same = text(owner, id, "label", expected.label, published.label) && same;
        same = text(owner, id, "identifier", expected.identifier, published.identifier) && same;
        same = text(owner, id, "value", expected.value, published.value) && same;
        same = flag(owner, id, "enabled", expected.enabled, published.enabled) && same;
        same = flag(owner, id, "focused", expected.focused, published.focused) && same;
        same = flag(owner, id, "selected", expected.selected, published.selected) && same;
        same = flag(owner, id, "expanded", expected.expanded, published.expanded) && same;
        same = flag(owner, id, "AXPress", expected.press, published.press) && same;
        same = flag(owner, id, "AXShowMenu", expected.show_menu, published.show_menu) && same;
        same = flag(owner, id, "AXIncrement", expected.increment, published.increment) && same;
        same = flag(owner, id, "AXDecrement", expected.decrement, published.decrement) && same;
        same = flag(owner, id, "AXFocused settable", expected.focus_settable, published.focus_settable) && same;
        same = flag(owner, id, "AXValue settable", expected.value_settable, published.value_settable) && same;
        same = flag(owner, id, "AXSelected settable", expected.selected_settable, published.selected_settable) && same;
        same = flag(owner, id, "Close action", expected.close_action, published.close_action) && same;
        same = number(owner, id, "AXDisclosureLevel", expected.disclosure_level, published.disclosure_level) && same;
        same = text(owner, id, "AXRowIndexRange", expected.row_range, published.row_range) && same;
        same = text(owner, id, "AXColumnIndexRange", expected.column_range, published.column_range) && same;
        same = number(owner, id, "AXRowCount", expected.row_count, published.row_count) && same;
        same = number(owner, id, "AXColumnCount", expected.column_count, published.column_count) && same;
        same = flag(owner, id, "AXModal", expected.modal, published.modal) && same;
        same = number(owner, id, "AXNumberOfCharacters", expected.character_count, published.character_count) && same;
        same = text(owner, id, "AXSelectedTextRange", expected.selected_text_range,
                    published.selected_text_range) && same;
        return same;
    }

    const Dump& dump_;
    Report& report_;
    std::string phase_;
};

void compare_surface(const Dump& dump, AXUIElementRef surface, Report& report, const std::string& phase) {
    const std::vector<AxElement> roots = children_of(surface);
    if (roots.size() != dump.snapshot.roots.size()) {
        report.mismatches.push_back(Mismatch{phase, "", "(window)", "roots",
                                             std::to_string(dump.snapshot.roots.size()),
                                             std::to_string(roots.size())});
    }
    TreeComparison comparison(dump, report, phase);
    for (std::size_t index = 0U; index < dump.snapshot.roots.size(); ++index) {
        AXUIElementRef root = index < roots.size() ? roots[index].get() : nullptr;
        comparison.compare(dump.snapshot.roots[index], root, "", "");
    }
}

// ---------------------------------------------------------------------------
// Default actions through AX (the fixture).

AXError perform_through_ax(AXUIElementRef element, SemanticAction action, const SemanticNode& node) {
    switch (action) {
    case SemanticAction::press:
    case SemanticAction::expand:
    case SemanticAction::collapse:
        return AXUIElementPerformAction(element, kAXPressAction);
    case SemanticAction::select:
        return AXUIElementSetAttributeValue(element, kAXSelectedAttribute, kCFBooleanTrue);
    case SemanticAction::increment:
        return AXUIElementPerformAction(element, kAXIncrementAction);
    case SemanticAction::set_value: {
        const std::string text = default_action_value(node);
        CfOwned<CFStringRef> value(CFStringCreateWithCString(nullptr, text.c_str(), kCFStringEncodingUTF8));
        return AXUIElementSetAttributeValue(element, kAXValueAttribute, value.get());
    }
    case SemanticAction::show_menu:
        return AXUIElementPerformAction(element, kAXShowMenuAction);
    case SemanticAction::focus:
    case SemanticAction::decrement:
    case SemanticAction::close:
    case SemanticAction::count:
        return kAXErrorActionUnsupported;
    }
    return kAXErrorActionUnsupported;
}

void post_escape(pid_t pid) {
    CfOwned<CGEventRef> down(CGEventCreateKeyboardEvent(nullptr, 53, true));
    CfOwned<CGEventRef> up(CGEventCreateKeyboardEvent(nullptr, 53, false));
    CGEventPostToPid(pid, down.get());
    CGEventPostToPid(pid, up.get());
}

// The fixture's stock controls: the first control on each path from the
// roots, below the fixture's own root panel "stock".
void collect_entries(const Dump& dump, const SemanticNode& node, std::vector<std::string>& entries) {
    const std::string type = control_type(dump, node.stable_id);
    if (!type.empty() && node.stable_id != "stock" && node.stable_id.rfind("ax.", 0) != 0) {
        entries.push_back(node.stable_id);
        return;
    }
    for (const SemanticNode& child : node.children) collect_entries(dump, child, entries);
}

// Puts the app back after an action: a node left expanded is pressed again
// (AXPress collapses it), and an open overlay, which replaces the roots, is
// closed with Escape.
Dump restore_after_action(const std::string& path, AXUIElementRef surface_hint, const Dump& before,
                          const std::string& target_id, Dump current, pid_t pid) {
    const SemanticNode* target = find_semantic_node(current.snapshot.roots, target_id);
    if (target != nullptr && has_semantic_state((*target).states, SemanticState::expanded) &&
        has_semantic_action(*target, SemanticAction::collapse)) {
        AxElement element = find_identified(surface_hint, target_id);
        if (element.get() != nullptr) {
            static_cast<void>(AXUIElementPerformAction(element.get(), kAXPressAction));
            current = wait_for_new_dump(path, current, 1000);
        }
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (gui_forms::test_support::same_semantic_roots(current.snapshot, before.snapshot)) break;
        post_escape(pid);
        current = wait_for_new_dump(path, current, 1000);
    }
    return current;
}

Dump perform_default_actions(const std::string& path, AXUIElementRef application, Dump dump,
                             Report& report) {
    std::vector<std::string> entries;
    for (const SemanticNode& root : dump.snapshot.roots) collect_entries(dump, root, entries);
    for (const std::string& entry : entries) {
        dump = settled_dump(path, report, "before " + entry);
        if (report.settle_failed) break;
        AxElement surface = find_surface(application);
        const SemanticNode* control = find_semantic_node(dump.snapshot.roots, entry);
        const std::string type = control_type(dump, entry);
        TypeTally& tally = tally_for(report, type == "control" ? "control:" + entry : type);
        if (control == nullptr || surface.get() == nullptr) continue;
        SemanticAction action = SemanticAction::focus;
        const SemanticNode* target = default_action_node(*control, action);
        if (target == nullptr) {
            if (tally.default_action.empty()) tally.default_action = "none";
            continue;
        }
        const SemanticNode target_before = *target;
        tally.default_action = gui_forms::semantic_action_name(action);
        AxElement element = find_identified(surface.get(), target_before.stable_id);
        if (element.get() == nullptr) {
            report.failures.push_back(entry + ": no AX element for " + target_before.stable_id);
            continue;
        }
        const std::string signature_before = semantic_subtree_signature(dump.snapshot.roots, entry);
        const AXError performed = perform_through_ax(element.get(), action, target_before);
        Dump after = wait_for_new_dump(path, dump, 1500);
        const bool effect = after.effects > dump.effects;
        const bool changed = semantic_subtree_signature(after.snapshot.roots, entry) != signature_before;
        const bool observed = performed == kAXErrorSuccess && (effect || changed);
        tally.action_observed = tally.action_observed || observed;
        if (!observed) {
            report.failures.push_back(entry + ": default action " + tally.default_action + " on " +
                                      target_before.stable_id + " (AX error " +
                                      std::to_string(static_cast<int>(performed)) + ") showed no effect");
        }
        dump = restore_after_action(path, surface.get(), dump, target_before.stable_id, after, dump.pid);
    }
    return dump;
}

// ---------------------------------------------------------------------------
// A stale element (GF017): the client holds "ax.disposable", presses
// "ax.dispose" through AX, and then reads and presses the held element.

Dump check_stale_element(const std::string& path, AXUIElementRef application, Dump dump, Report& report) {
    dump = settled_dump(path, report, "stale element");
    AxElement surface = find_surface(application);
    AxElement stale = find_identified(surface.get(), "ax.disposable");
    AxElement dispose = find_identified(surface.get(), "ax.dispose");
    if (stale.get() == nullptr || dispose.get() == nullptr) {
        report.failures.push_back("stale element: the disposable sample or its dispose button is not published");
        return dump;
    }
    const AXError pressed = AXUIElementPerformAction(dispose.get(), kAXPressAction);
    Dump after = wait_for_new_dump(path, dump, 1500);
    if (pressed != kAXErrorSuccess || find_semantic_node(after.snapshot.roots, "ax.disposable") != nullptr) {
        report.failures.push_back("stale element: the sample was not disposed");
        return after;
    }
    // Let the host reconcile its elements with the new tree.
    AxElement refreshed = find_surface(application);
    static_cast<void>(children_of(refreshed.get()));
    CfValue role;
    const AXError read = copy_attribute(stale.get(), kAXRoleAttribute, role);
    const AXError press = AXUIElementPerformAction(stale.get(), kAXPressAction);
    const bool alive = kill(after.pid, 0) == 0;
    report.notes.push_back("stale element: read role -> AX error " + std::to_string(static_cast<int>(read)) +
                           ", press -> AX error " + std::to_string(static_cast<int>(press)) +
                           ", app alive " + (alive ? "yes" : "no"));
    if (read == kAXErrorSuccess) report.failures.push_back("stale element: reading the disposed control's role succeeded");
    if (press == kAXErrorSuccess) report.failures.push_back("stale element: pressing the disposed control succeeded");
    if (!alive) report.failures.push_back("stale element: the app died");
    return after;
}

// ---------------------------------------------------------------------------
// Editable text (A-T1): the fixture's text box through AX's parameterized
// text attributes, and AXSelectedTextRange set through AX and observed in
// the dump.

// A parameterized attribute of `element` for a CFRange, a CFIndex or a
// CGPoint parameter (an AXValue or a CFNumber), into `value`.
AXError copy_parameterized(AXUIElementRef element, CFStringRef attribute, CFTypeRef parameter, CfValue& value) {
    return AXUIElementCopyParameterizedAttributeValue(element, attribute, parameter, value.receive());
}

// `value` as a CFRange, or {kCFNotFound, 0} when it is none.
CFRange range_value(const CfValue& value) {
    CFRange range = CFRangeMake(kCFNotFound, 0);
    if (value.get() == nullptr || CFGetTypeID(value.get()) != AXValueGetTypeID()) return range;
    if (!AXValueGetValue(static_cast<AXValueRef>(value.get()), kAXValueTypeCFRange, &range)) {
        range = CFRangeMake(kCFNotFound, 0);
    }
    return range;
}

// Whether a byte is ASCII (std::all_of's predicate).
struct IsAsciiByte final {
    bool operator()(char byte) const noexcept { return static_cast<unsigned char>(byte) < 0x80U; }
};

bool is_range(CFRange range, CFIndex location, CFIndex length) {
    const bool same = range.location == location && range.length == length;
    return same;
}

Dump check_text_ranges(const std::string& path, AXUIElementRef application, Dump dump, Report& report) {
    dump = settled_dump(path, report, "text ranges");
    if (report.settle_failed) return dump;
    AxElement surface = find_surface(application);
    AxElement field = find_identified(surface.get(), "text-box");
    const SemanticNode* node = find_semantic_node(dump.snapshot.roots, "text-box");
    if (field.get() == nullptr || node == nullptr || !(*node).text_selection) {
        report.failures.push_back("text ranges: the fixture's text box is not published with a selection");
        return dump;
    }
    const std::string value = (*node).value;
    const bool ascii = std::all_of(value.begin(), value.end(), IsAsciiByte{});
    const CFIndex units = static_cast<CFIndex>(gui_forms::test_support::expected_utf16_units(value, value.size()));
    if (!ascii || units < 2) {
        report.failures.push_back("text ranges: the fixture's text box must hold at least two ASCII characters");
        return dump;
    }
    CFRange whole = CFRangeMake(0, units);
    CfValue whole_range(AXValueCreate(kAXValueTypeCFRange, &whole));
    CfValue text;
    const AXError read = copy_parameterized(field.get(), kAXStringForRangeParameterizedAttribute, whole_range.get(), text);
    const bool text_read = read == kAXErrorSuccess && text.get() != nullptr &&
                           CFGetTypeID(text.get()) == CFStringGetTypeID() &&
                           utf8(static_cast<CFStringRef>(text.get())) == value;
    if (!text_read) report.failures.push_back("text ranges: AXStringForRange over the whole text is not the value");
    CFRange first = CFRangeMake(0, 1);
    CfValue first_range(AXValueCreate(kAXValueTypeCFRange, &first));
    CfValue bounds;
    CGRect box = CGRectZero;
    const AXError bounded = copy_parameterized(field.get(), kAXBoundsForRangeParameterizedAttribute, first_range.get(), bounds);
    const bool boxed = bounded == kAXErrorSuccess && bounds.get() != nullptr &&
                       CFGetTypeID(bounds.get()) == AXValueGetTypeID() &&
                       AXValueGetValue(static_cast<AXValueRef>(bounds.get()), kAXValueTypeCGRect, &box) &&
                       box.size.width > 0.0 && box.size.height > 0.0;
    if (!boxed) report.failures.push_back("text ranges: AXBoundsForRange gives no box for the first character");
    int zero = 0;
    CfValue line_zero(CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &zero));
    CfValue line_range;
    static_cast<void>(copy_parameterized(field.get(), kAXRangeForLineParameterizedAttribute, line_zero.get(), line_range));
    if (!is_range(range_value(line_range), 0, units)) {
        report.failures.push_back("text ranges: AXRangeForLine 0 is not the whole single line");
    }
    CfValue index_range;
    static_cast<void>(copy_parameterized(field.get(), kAXRangeForIndexParameterizedAttribute, line_zero.get(), index_range));
    if (!is_range(range_value(index_range), 0, 1)) {
        report.failures.push_back("text ranges: AXRangeForIndex 0 is not the first character");
    }
    if (boxed) {
        CGPoint middle = CGPointMake(CGRectGetMidX(box), CGRectGetMidY(box));
        CfValue position(AXValueCreate(kAXValueTypeCGPoint, &middle));
        CfValue hit;
        static_cast<void>(copy_parameterized(field.get(), kAXRangeForPositionParameterizedAttribute, position.get(), hit));
        if (!is_range(range_value(hit), 0, 1)) {
            report.failures.push_back("text ranges: AXRangeForPosition at the first character's middle misses it");
        }
    }
    CFRange wanted = CFRangeMake(1, 1);
    CfValue wanted_range(AXValueCreate(kAXValueTypeCFRange, &wanted));
    const AXError set = AXUIElementSetAttributeValue(field.get(), kAXSelectedTextRangeAttribute, wanted_range.get());
    Dump after = wait_for_new_dump(path, dump, 1500);
    const SemanticNode* changed = find_semantic_node(after.snapshot.roots, "text-box");
    const bool selected = set == kAXErrorSuccess && changed != nullptr && (*changed).text_selection &&
                          (*(*changed).text_selection).anchor.value() == 1U &&
                          (*(*changed).text_selection).caret.value() == 2U;
    if (!selected) {
        report.failures.push_back("text ranges: setting AXSelectedTextRange {1, 1} (AX error " +
                                  std::to_string(static_cast<int>(set)) + ") did not select byte 1 to 2");
    }
    report.notes.push_back("text ranges: the text box's parameterized attributes and selection checked");
    return after;
}

// ---------------------------------------------------------------------------
// The Gallery: every page through its navigation row.

void collect_page_rows(const SemanticNode& node, std::vector<std::string>& rows) {
    if (node.stable_id.rfind("gallery.page.", 0) == 0) rows.push_back(node.stable_id);
    for (const SemanticNode& child : node.children) collect_page_rows(child, rows);
}

void collect_ids(const SemanticNode& node, std::string& ids) {
    ids += node.stable_id;
    ids += '\n';
    for (const SemanticNode& child : node.children) collect_ids(child, ids);
}

// Every stable id in the snapshot, in order; a shown page changes it.
std::string snapshot_ids(const SemanticSnapshot& snapshot) {
    std::string ids;
    for (const SemanticNode& root : snapshot.roots) collect_ids(root, ids);
    return ids;
}

// The navigation rows are buttons: pressing one shows its page, which
// replaces the page's nodes. The first row's page is the one shown at
// start, so pressing it changes nothing.
Dump compare_gallery_pages(const std::string& path, AXUIElementRef application, Dump dump, Report& report) {
    std::vector<std::string> rows;
    for (const SemanticNode& root : dump.snapshot.roots) collect_page_rows(root, rows);
    report.notes.push_back("gallery pages: " + std::to_string(rows.size()));
    for (std::size_t index = 0U; index < rows.size(); ++index) {
        const std::string& row = rows[index];
        dump = settled_dump(path, report, "before pressing " + row);
        if (report.settle_failed) break;
        AxElement surface = find_surface(application);
        AxElement element = find_identified(surface.get(), row);
        if (element.get() == nullptr) {
            report.failures.push_back("gallery: no AX element for " + row);
            continue;
        }
        const std::string ids_before = snapshot_ids(dump.snapshot);
        const AXError pressed = AXUIElementPerformAction(element.get(), kAXPressAction);
        static_cast<void>(wait_for_new_dump(path, dump, 2000));
        dump = settled_dump(path, report, "after pressing " + row);
        if (report.settle_failed) break;
        const bool shown = index == 0U || snapshot_ids(dump.snapshot) != ids_before;
        if (pressed != kAXErrorSuccess || !shown) {
            report.failures.push_back("gallery: pressing " + row + " through AX did not show its page");
        }
        AxElement page_surface = find_surface(application);
        compare_surface(dump, page_surface.get(), report, row);
    }
    return dump;
}

// ---------------------------------------------------------------------------

struct Options final {
    std::string app;
    std::string dump;
    bool gallery{};
};

bool parse_options(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--gallery") {
            options.gallery = true;
        } else if (argument == "--app" && index + 1 < argc) {
            options.app = argv[++index];
        } else if (argument == "--dump" && index + 1 < argc) {
            options.dump = argv[++index];
        }
    }
    return !options.app.empty() && !options.dump.empty();
}

bool launch_app(const Options& options) {
    std::remove(options.dump.c_str());
    std::vector<std::string> arguments{"/usr/bin/open", "-n", options.app, "--args", "--semantic-dump", options.dump};
    std::vector<char*> argv_pointers;
    for (std::string& argument : arguments) argv_pointers.push_back(argument.data());
    argv_pointers.push_back(nullptr);
    pid_t opener = 0;
    if (posix_spawn(&opener, "/usr/bin/open", nullptr, nullptr, argv_pointers.data(), nullptr) != 0) return false;
    int status = 0;
    waitpid(opener, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void close_app(AXUIElementRef application, pid_t pid) {
    CfValue windows;
    if (copy_attribute(application, kAXWindowsAttribute, windows) == kAXErrorSuccess && windows.get() != nullptr) {
        const CFArrayRef array = static_cast<CFArrayRef>(windows.get());
        for (CFIndex index = 0; index < CFArrayGetCount(array); ++index) {
            AXUIElementRef window = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(array, index));
            CfValue button;
            if (copy_attribute(window, kAXCloseButtonAttribute, button) == kAXErrorSuccess && button.get() != nullptr) {
                static_cast<void>(AXUIElementPerformAction(static_cast<AXUIElementRef>(button.get()), kAXPressAction));
            }
        }
    }
    for (int waited = 0; waited < 5000 && kill(pid, 0) == 0; waited += 50) sleep_milliseconds(50);
    if (kill(pid, 0) == 0) kill(pid, SIGTERM);
}

void print_report(const Report& report) {
    std::size_t nodes = 0U;
    std::size_t matched = 0U;
    std::size_t instances = 0U;
    for (const TypeTally& tally : report.types) {
        nodes += tally.nodes;
        matched += tally.matched;
        instances += tally.instances.size();
        std::cout << "control " << tally.control_type << " instances=" << tally.instances.size()
                  << " nodes=" << tally.nodes << " matched=" << tally.matched;
        if (!tally.default_action.empty()) {
            std::cout << " default_action=" << tally.default_action
                      << " observed=" << (tally.action_observed ? "yes" : "no");
        }
        std::cout << '\n';
    }
    for (const Mismatch& mismatch : report.mismatches) {
        std::cout << "mismatch phase=" << mismatch.phase << " control=" << mismatch.control
                  << " node=" << mismatch.stable_id << " field=" << mismatch.field << " expected=\""
                  << mismatch.expected << "\" published=\"" << mismatch.published << "\"\n";
    }
    for (const std::string& note : report.notes) std::cout << "note " << note << '\n';
    for (const std::string& failure : report.failures) std::cout << "failure " << failure << '\n';
    std::cout << "types=" << report.types.size() << " instances=" << instances << " nodes=" << nodes
              << " matched=" << matched << " mismatches=" << report.mismatches.size()
              << " failures=" << report.failures.size() << '\n';
}

int run(const Options& options) {
    if (!AXIsProcessTrusted()) {
        std::cout << "unsupported: this process is not trusted for accessibility "
                     "(System Settings > Privacy & Security > Accessibility); the AX client test did not run\n";
        return unsupported_exit;
    }
    if (!launch_app(options)) {
        std::cerr << "could not launch " << options.app << '\n';
        return 1;
    }
    Dump dump;
    for (int waited = 0; waited < 15000 && !dump.valid; waited += 50) {
        dump = read_dump(options.dump);
        if (!dump.valid) sleep_milliseconds(50);
    }
    if (!dump.valid) {
        std::cerr << "the app wrote no semantic dump\n";
        return 1;
    }
    AxElement application(AXUIElementCreateApplication(dump.pid));
    AXUIElementSetMessagingTimeout(application.get(), 5.0F);
    AxElement surface;
    for (int waited = 0; waited < 10000 && surface.get() == nullptr; waited += 100) {
        surface = find_surface(application.get());
        if (surface.get() == nullptr) sleep_milliseconds(100);
    }
    Report report;
    dump = settled_dump(options.dump, report, "initial");
    if (report.settle_failed) {
        // Named by settled_dump; nothing later could be compared.
    } else if (surface.get() == nullptr) {
        report.failures.push_back("the app's retained surface is not published through AX");
    } else if (options.gallery) {
        compare_surface(dump, surface.get(), report, "initial");
        dump = compare_gallery_pages(options.dump, application.get(), dump, report);
    } else {
        compare_surface(dump, surface.get(), report, "initial");
        dump = perform_default_actions(options.dump, application.get(), dump, report);
        dump = settled_dump(options.dump, report, "after-actions");
        if (!report.settle_failed) {
            AxElement after_surface = find_surface(application.get());
            compare_surface(dump, after_surface.get(), report, "after-actions");
            dump = check_stale_element(options.dump, application.get(), dump, report);
            dump = check_text_ranges(options.dump, application.get(), dump, report);
        }
    }
    close_app(application.get(), dump.pid);
    print_report(report);
    return report.mismatches.empty() && report.failures.empty() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        Options options;
        if (!parse_options(argc, argv, options)) {
            std::cerr << "usage: gui_forms_macos_accessibility_client_tests --app <bundle> --dump <path> [--gallery]\n";
            return 2;
        }
        return run(options);
    }
}
