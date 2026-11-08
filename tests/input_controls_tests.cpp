#include "gui_forms/input_controls.hpp"
#include "gui_forms/controls/range_control/scroll_bar/scroll_bar.hpp"
#include "gui_forms/window.hpp"
#include "headless_host.hpp"
#include "support/named_callbacks.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class RecordingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect rect, Color color) override {
        if (rect == Rect{0.0, 0.0, 300.0, 220.0}) {
            saw_window_backplane = true;
        }
        if (color == Color::rgba(38, 114, 185) && rect.width > 0.0) {
            saw_selection = true;
        }
    }
    void stroke_rect(Rect, Color, double) override {}
    void fill_linear_gradient_spread(
        Rect rect, Point, Point, std::span<const GradientStop>,
        GradientSpreadMode) override {
        if (rect.width > 0.0 && rect.width < 170.0 && rect.y >= 3.0) {
            saw_selection = true;
        }
    }
    void draw_line(Point, Point, Color, double) override { ++lines; }
    void draw_text_utf8(Point, std::string_view text, FontSpec, Color) override {
        painted_text += std::string(text);
    }
    void draw_image(ImageId, Rect, double) override {}

    bool saw_selection{};
    bool saw_window_backplane{};
    std::uint64_t lines{};
    std::string painted_text;
};

class AddActivatedIndex final {
public:
    explicit AddActivatedIndex(std::size_t& activations)
        : activations_(activations) {}

    void operator()(std::size_t index) const { activations_ += index + 1U; }

private:
    std::size_t& activations_;
};

class AppendDropDownState final {
public:
    explicit AppendDropDownState(std::string& order) : order_(order) {}

    void operator()(bool open) const { order_ += open ? "open\n" : "close\n"; }

private:
    std::string& order_;
};

class AppendNumericValue final {
public:
    explicit AppendNumericValue(std::string& values) : values_(values) {}

    void operator()(double value) const {
        values_ += std::to_string(value) + "\n";
    }

private:
    std::string& values_;
};

// coerce_item_height rounds a row height up to an even pixel count, and
// on_font_changed counts each changed font write after the touch.
class EvenRowListBox final : public ListBox {
public:
    using ListBox::ListBox;
    int font_changes{};

protected:
    void coerce_item_height(double& incoming) override {
        incoming = 2.0 * std::ceil(incoming * 0.5);
    }
    void on_font_changed() override {
        ListBox::on_font_changed();
        ++font_changes;
    }
};

void test_unicode_editing_selection_and_history() {
    std::shared_ptr<gui_forms::TextBox> field = make_control<TextBox>(StableId("input.unicode"),
                                       "a\xCC\x81" "bc");
    (*field).set_requested_bounds({0.0, 0.0, 180.0, 30.0});
    Window window(field, {180.0, 30.0});
    require(window.request_focus(field), "TextBox must accept retained focus");

    std::string order;
    SubscriptionToken text = (*field).text_changed().subscribe(
        test_support::AppendLiteral<const std::string&>(order, "text\n"));
    SubscriptionToken selection = (*field).selection_changed().subscribe(
        test_support::AppendLiteral<const TextSelection&>(
            order, "selection\n"));

    (*field).select(Utf8Offset(0U), Utf8Offset(3U));
    require((*field).selected_text() == "a\xCC\x81",
            "TextBox selection must preserve a combining grapheme");
    require((*field).replace_selection("Ω") && (*field).text.read() == "Ωbc" &&
                order == "selection\ntext\nselection\n" && (*field).can_undo(),
            "TextBox replacement must publish text then collapsed selection");
    require((*field).undo() && (*field).text.read() == "a\xCC\x81" "bc" &&
                (*field).can_redo(),
            "TextBox undo must restore Unicode text and directional selection");
    require((*field).redo() && (*field).text.read() == "Ωbc",
            "TextBox redo must restore the replacement");
}

void test_keyboard_text_input_and_read_only() {
    std::shared_ptr<gui_forms::TextBox> field = make_control<TextBox>(StableId("input.keyboard"), "alpha");
    (*field).set_requested_bounds({0.0, 0.0, 180.0, 30.0});
    Window window(field, {180.0, 30.0});
    require(window.request_focus(field), "TextBox keyboard fixture must focus");

    KeyEvent home{KeyAction::down, PhysicalKey::home};
    require(window.dispatch_key(home) && (*field).selection().caret == Utf8Offset(0U),
            "Home must move the TextBox caret to the first boundary");
    KeyEvent right{KeyAction::down, PhysicalKey::right, Modifier::shift};
    require(window.dispatch_key(right) && (*field).selected_text() == "a",
            "Shift+Right must extend TextBox selection by one grapheme");
    require(window.dispatch_text({"Ω"}) && (*field).text.read() == "Ωlpha",
            "normalized text input must replace the selected range");

    KeyEvent select_all{KeyAction::down, PhysicalKey::a, Modifier::control};
    require(window.dispatch_key(select_all) &&
                (*field).selected_text() == (*field).text.read(),
            "command+A must select all TextBox text");
    KeyEvent erase{KeyAction::down, PhysicalKey::backspace};
    require(window.dispatch_key(erase) && (*field).text.read().empty(),
            "Backspace must delete the active TextBox selection");
    KeyEvent undo{KeyAction::down, PhysicalKey::z, Modifier::control};
    require(window.dispatch_key(undo) && (*field).text.read() == "Ωlpha",
            "command+Z must use public TextBox history");

    (*field).read_only = true;
    require(!window.dispatch_text({"blocked"}) && (*field).text.read() == "Ωlpha",
            "read-only TextBox must reject normalized text mutation");
}

void test_pointer_drag_paint_and_caret_deadline() {
    std::shared_ptr<gui_forms::TextBox> field = make_control<TextBox>(StableId("input.pointer"), "abcdef");
    (*field).set_requested_bounds({0.0, 0.0, 180.0, 30.0});
    Window window(field, {180.0, 30.0});
    RecordingPainter painter;
    window.paint(painter, {0.0, 0.0, 180.0, 30.0});
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary,
                                     {7.0, 15.0}}) &&
                (*field).has_pointer_capture(),
            "TextBox primary down must focus and capture for selection");
    require(window.dispatch_pointer({PointerAction::move, PointerButton::none,
                                     {35.0, 15.0}}) &&
                !(*field).selection().empty(),
            "TextBox captured drag must continuously extend selection");
    require(window.dispatch_pointer({PointerAction::up, PointerButton::primary,
                                     {35.0, 15.0}}) &&
                !(*field).has_pointer_capture(),
            "TextBox pointer release must commit selection and release capture");

    RecordingPainter selected;
    window.paint(selected, {0.0, 0.0, 180.0, 30.0});
    require(selected.saw_selection && selected.painted_text.find("abcdef") !=
                std::string::npos,
            "focused TextBox must paint selected and unselected glyph passes");
    require(window.next_wake().has_value(),
            "focused TextBox caret must use a bounded scheduled deadline");
    const FrameTime deadline = *window.next_wake();
    const FramePollResult poll = window.poll_frame_schedule(deadline);
    require(poll.ui_timer_ticks == 1U && window.next_wake().has_value(),
            "TextBox caret timer must tick and keep the next blink scheduled");
}

void test_placeholder_and_validation() {
    std::shared_ptr<gui_forms::TextBox> field = make_control<TextBox>(StableId("input.placeholder"));
    (*field).placeholder_text = "Type a local path";
    (*field).set_requested_bounds({0.0, 0.0, 180.0, 30.0});
    Window window(field, {180.0, 30.0});
    RecordingPainter painter;
    window.paint(painter, {0.0, 0.0, 180.0, 30.0});
    require(painter.painted_text == "Type a local path",
            "empty TextBox must paint its placeholder without mutating text");

    bool invalid = false;
    try {
        (*field).select(Utf8Offset(1U), Utf8Offset(1U));
    } catch (const std::out_of_range&) {
        invalid = true;
    }
    require(invalid, "TextBox must reject selections outside its text store");
}

void test_text_box_maximum_length_bounds_user_edits() {
    std::shared_ptr<gui_forms::TextBox> field = make_control<TextBox>(StableId("input.maximum-length"));
    (*field).maximum_length = 3U;
    (*field).set_requested_bounds({0.0, 0.0, 180.0, 30.0});
    Window window(field, {180.0, 30.0});
    require(window.request_focus(field) && window.dispatch_text({"ab"}) &&
                window.dispatch_text({"🚀"}) && (*field).text.read() == "ab🚀" &&
                !window.dispatch_text({"c"}) && (*field).text.read() == "ab🚀",
            "TextBox maximum length must count Unicode scalars and reject only the overflowing user edit");
    (*field).text = "programmatic value";
    require((*field).text.read() == "programmatic value",
            "TextBox maximum length must not truncate explicit programmatic assignment");
    bool invalid{};
    try {
        (*field).maximum_length = 16U * 1024U * 1024U + 1U;
    } catch (const std::out_of_range&) {
        invalid = true;
    }
    require(invalid && (*field).maximum_length.read() == 3U,
            "TextBox maximum-length validation must be bounded and transactional");
}

void test_word_navigation_and_deletion() {
    std::shared_ptr<gui_forms::TextBox> field = make_control<TextBox>(StableId("input.words"),
                                       "alpha  日本語, bravo");
    (*field).set_requested_bounds({0.0, 0.0, 260.0, 30.0});
    Window window(field, {260.0, 30.0});
    require(window.request_focus(field), "word-navigation fixture must focus");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::home}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::right,
                                     Modifier::control}) &&
                (*field).selection().caret == Utf8Offset(7U),
            "Ctrl+Right must cross one word and its following spaces");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::right,
                                 Modifier::control | Modifier::shift}) &&
                (*field).selected_text() == "日本語",
            "Ctrl+Shift+Right must extend by one Unicode word without splitting graphemes");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::right,
                                 Modifier::control}) &&
                (*field).selection().empty() &&
                (*field).selection().caret == Utf8Offset(16U),
            "unshifted word navigation must first collapse a directional selection");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::right,
                                 Modifier::alt}) &&
                (*field).selection().caret == Utf8Offset(18U),
            "Alt+Right must share the deterministic cross-platform word policy");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::end}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::backspace,
                                     Modifier::control}) &&
                (*field).text.read() == "alpha  日本語, ",
            "Ctrl+Backspace must remove a complete word as one undoable edit");
    require((*field).undo() && (*field).text.read() == "alpha  日本語, bravo",
            "word deletion must participate in ordinary TextBox history");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::left,
                                 Modifier::meta}) &&
                (*field).selection().caret == Utf8Offset(0U),
            "Meta+Left must provide deterministic single-line start navigation");
}

void test_clipboard_commands_and_protected_text() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("input.clipboard.root"));
    std::shared_ptr<gui_forms::TextBox> field = make_control<TextBox>(StableId("input.clipboard"), "alpha bravo");
    (*field).set_requested_bounds({0.0, 0.0, 240.0, 30.0});
    (*root).add_child(field);
    std::shared_ptr<gui_forms::TextBox> password = make_control<TextBox>(StableId("input.password"),
                                          "sëcret🚀");
    (*password).set_requested_bounds({0.0, 40.0, 240.0, 30.0});
    (*password).accessible_name = "Protected credential";
    (*password).use_system_password_character = true;
    (*root).add_child(password);
    Window window(root, {260.0, 90.0});
    host::HeadlessHost host(window);
    HostServices& services = host.services();

    require(window.request_focus(field), "clipboard fixture must focus TextBox");
    (*field).select(Utf8Offset(0U), Utf8Offset(5U));
    require(window.dispatch_key({KeyAction::down, PhysicalKey::c,
                                 Modifier::control}),
            "Ctrl+C must be consumed by the focused TextBox");
    HostClipboardTextResult clipboard = services.read_clipboard_text();
    require(clipboard.status.accepted() && clipboard.has_text &&
                clipboard.text_utf8 == "alpha",
            "copy must use the portable HostServices clipboard seam");

    (*field).select(Utf8Offset(6U), Utf8Offset(11U));
    require(window.dispatch_key({KeyAction::down, PhysicalKey::x,
                                 Modifier::meta}) && (*field).text.read() == "alpha ",
            "Cmd/Ctrl+X must copy then remove the selected range");
    require(services.write_clipboard_text("Ω paste").accepted(),
            "clipboard fixture must install Unicode paste content");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::v,
                                 Modifier::control}) &&
                (*field).text.read() == "alpha Ω paste",
            "Ctrl+V must replace selection from portable UTF-8 clipboard data");
    (*field).read_only = true;
    require(window.dispatch_key({KeyAction::down, PhysicalKey::v,
                                 Modifier::control}) &&
                (*field).text.read() == "alpha Ω paste",
            "read-only paste must be consumed without mutating text");

    require(services.write_clipboard_text("sentinel").accepted() &&
                window.request_focus(password),
            "protected-field fixture must retain a sentinel clipboard");
    (*password).select_all();
    RecordingPainter painter;
    window.paint(painter, {0.0, 0.0, 260.0, 90.0});
    require(painter.painted_text.find("sëcret🚀") == std::string::npos &&
                painter.painted_text.find("•") != std::string::npos,
            "protected TextBox paint must emit mask glyphs and never secret text");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::c,
                                 Modifier::meta}) && !(*password).copy(),
            "protected copy must be consumed and reject programmatic export");
    clipboard = services.read_clipboard_text();
    require(clipboard.text_utf8 == "sentinel",
            "protected copy must leave existing clipboard content untouched");
    const SemanticDescriptor descriptor = (*password).semantic_descriptor();
    const std::string semantic_json = window.semantic_snapshot().to_json();
    require(descriptor.value.empty() &&
                has_semantic_state(descriptor.states,
                                   SemanticState::protected_content) &&
                semantic_json.find("sëcret🚀") == std::string::npos,
            "protected semantics must identify protection without leaking the value");
    (*password).use_system_password_character = false;
    (*password).password_character = U'*';
    RecordingPainter custom_mask;
    window.paint(custom_mask, {0.0, 0.0, 260.0, 90.0});
    require(custom_mask.painted_text.find("*******") != std::string::npos,
            "custom password character must preserve one mask per grapheme");
    bool invalid_mask = false;
    try {
        (*password).password_character = static_cast<char32_t>(0xd800U);
    } catch (const std::invalid_argument&) {
        invalid_mask = true;
    }
    require(invalid_mask,
            "password mask must reject invalid Unicode scalar values");
}

void test_list_box_selection_navigation_and_mutation() {
    std::shared_ptr<gui_forms::ListBox> list = make_control<ListBox>(StableId("input.list"));
    (*list).items = {"Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot"};
    (*list).set_requested_bounds({0.0, 0.0, 180.0, 86.0});
    Window window(list, {180.0, 86.0});
    require(window.request_focus(list), "ListBox must accept retained focus");
    std::size_t changes{};
    std::size_t activations{};
    SubscriptionToken changed = (*list).selection_changed().subscribe(
        test_support::IncrementCounter<std::size_t,
                                       const ListSelectionChange&>(changes));
    SubscriptionToken activated = (*list).item_activated().subscribe(
        AddActivatedIndex(activations));

    (*list).select_index(1U);
    KeyEvent down{KeyAction::down, PhysicalKey::down};
    require(window.dispatch_key(down) && (*list).selected_index() == 2U &&
                changes == 2U,
            "ListBox Down must select the next item exactly once");
    KeyEvent enter{KeyAction::down, PhysicalKey::enter};
    require(window.dispatch_key(enter) && activations == 3U,
            "ListBox Enter must activate its active item");

    (*list).selection_mode = ListSelectionMode::multiple_extended;
    (*list).select_index(4U, true, false);
    require((*list).selected_indices().size() == 3U &&
                (*list).selected_indices().front() == 2U &&
                (*list).selected_indices().back() == 4U,
            "extended ListBox selection must include its deterministic anchor range");
    (*list).select_index(3U, false, true);
    require((*list).selected_indices().size() == 2U &&
                std::find((*list).selected_indices().begin(),
                          (*list).selected_indices().end(), 3U) ==
                    (*list).selected_indices().end(),
            "command-toggle must remove one item from a multi-selection");

    (*list).items.erase(0U, 1U);
    require((*list).selected_indices().front() == 1U &&
                (*list).selected_indices().back() == 3U,
            "ListBox collection mutation must remap retained selection indices");
    (*list).top_index = 2U;
    RecordingPainter painter;
    window.paint(painter, {0.0, 0.0, 180.0, 86.0});
    require(painter.painted_text.find("Delta") != std::string::npos &&
                painter.painted_text.find("Bravo") == std::string::npos,
            "ListBox top index must bound virtualized row painting");
}

void test_combo_box_scrollbar_reaches_last_item() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("scroll.root"));
    const std::shared_ptr<ComboBox> combo = make_control<ComboBox>(StableId("scroll.combo"));
    for (int i = 0; i < 24; ++i) (*combo).items.append("Language " + std::to_string(i));
    (*combo).maximum_drop_down_items = 64;
    (*combo).set_requested_bounds({20, 20, 240, 32});
    (*root).add_child(combo);
    Window window(root, {300, 220});
    window.perform_layout();
    (*combo).dropped_down = true;
    window.perform_layout();
    const std::shared_ptr<ListBox> list = std::dynamic_pointer_cast<ListBox>(window.find("scroll.combo.overlay.list"));
    const std::shared_ptr<ScrollBar> bar = std::dynamic_pointer_cast<ScrollBar>(window.find("scroll.combo.overlay.list.scrollbar"));
    require(list && bar && (*bar).visible.read(), "overflow choices have a visible, operable scrollbar");
    const Rect bounds = (*list).absolute_bounds();
    require(bounds.y >= 0 && bounds.y + bounds.height <= 220, "tall overlay fits a short window");
    (*bar).value = (*bar).maximum.read();
    require((*list).top_index.read() > 0, "scrollbar changes the list viewport");
    const Point last{bounds.x + 15, bounds.y + 2 + (23 - (*list).top_index.read()) * 26.0 + 13};
    window.dispatch_pointer({PointerAction::down, PointerButton::primary, last});
    window.dispatch_pointer({PointerAction::up, PointerButton::primary, last});
    require((*combo).selected_index.read() == 23 && !(*combo).dropped_down.read(),
            "the last language is reachable without a mouse wheel");
}

void test_combo_box_overlay_commit_dismiss_and_owner_revocation() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("combo.root"));
    std::shared_ptr<gui_forms::ComboBox> combo = make_control<ComboBox>(StableId("combo.field"));
    (*combo).items = {"Low latency", "Balanced", "High fidelity", "Archive"};
    (*combo).drop_down_width = 260.0;
    (*combo).placeholder_text = "Choose a profile";
    (*combo).set_requested_bounds({20.0, 20.0, 220.0, 32.0});
    (*root).add_child(combo);
    Window window(root, {300.0, 220.0});
    const std::optional<PropertyDescriptor> items_descriptor = (*combo).property_descriptor("Items");
    Component items_observer;
    std::size_t item_changes{};
    SubscriptionToken items_changed = (*combo).subscribe_property_changed(
        "Items", items_observer,
        test_support::IncrementCounter<std::size_t>(item_changes));
    const PropertyCollectionValue replacement = make_property_collection(
        "String", BindingValueKind::text,
        {BindingValue{std::string("One")}, BindingValue{std::string("Two")}});
    (*combo).set_property_value("Items", replacement);
    require(items_descriptor &&
                (*items_descriptor).kind == BindingValueKind::collection &&
                (*items_descriptor).serialization_visibility ==
                    PropertySerializationVisibility::content &&
                !(*items_descriptor).bindable && (*items_descriptor).resettable &&
                (*items_descriptor).change_notifications &&
                (*combo).items.read().size() == 2U && (*combo).items[1] == "Two" &&
                items_changed.connected() && item_changes == 1U &&
                (*combo).reset_property("Items") && (*combo).items.read().empty() &&
                item_changes == 2U,
            "ComboBox.Items must be a truthful content-serialized collection property with reset and change observation");
    (*combo).items = {"Low latency", "Balanced", "High fidelity", "Archive"};
    require(item_changes == 3U,
            "ordinary ComboBox collection mutation must use the same Items change contract as generic property access");
    RecordingPainter initial_painter;
    const DamageRegion initial_damage = window.take_damage();
    window.paint(initial_painter, initial_damage.bounds());
    window.notify_presented();
    require(window.request_focus(combo), "ComboBox must accept retained focus");
    std::string order;
    SubscriptionToken selection = (*combo).selected_index_changed().subscribe(
        test_support::AppendLiteral<std::optional<std::size_t>>(
            order, "selection\n"));
    SubscriptionToken dropdown = (*combo).drop_down_changed().subscribe(
        AppendDropDownState(order));

    (*combo).dropped_down = true;
    require((*combo).dropped_down.read() && window.focus_scope_depth() == 1U &&
                window.find("combo.field.overlay.list"),
            "ComboBox must publish a retained overlay and contained focus scope");
    const Control::Ptr overlay = window.find("combo.field.overlay.list");
    window.perform_layout();
    const Rect bounds = (*overlay).absolute_bounds();
    require(bounds.width == 260.0 && (*combo).drop_down_width.read() == 260.0,
            "ComboBox must honor an explicit bounded overlay width independently of its field width");
    const DamageRegion opening_damage = window.take_damage();
    RecordingPainter opening_painter;
    window.paint(opening_painter, opening_damage.bounds());
    window.notify_presented();
    const Point third{bounds.x + 20.0, bounds.y + 2.0 + 2.0 * 26.0 + 13.0};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, third}) &&
                window.dispatch_pointer({PointerAction::up, PointerButton::primary, third}) &&
                (*combo).selected_index.read() == 2U && !(*combo).dropped_down.read() &&
                window.focus_scope_depth() == 0U &&
                !window.find("combo.field.overlay.layer") &&
                order == "open\nselection\nclose\n",
            "ComboBox overlay activation must commit, close, restore focus, and detach overlay");
    const Rect close_damage = window.take_damage().bounds();
    require(Rect::intersection(close_damage, bounds) == bounds,
            "detaching a ComboBox overlay must damage every formerly painted overlay pixel");

    (*combo).dropped_down = true;
    window.perform_layout();
    const Rect semantic_overlay_bounds =
        (*window.find("combo.field.overlay.list")).absolute_bounds();
    const DamageRegion semantic_open_damage = window.take_damage();
    RecordingPainter semantic_open_painter;
    window.paint(semantic_open_painter, semantic_open_damage.bounds());
    window.notify_presented();
    require(window.perform_semantic_action(
                "combo.field.overlay.list.item.1", SemanticAction::press) &&
                !(*combo).dropped_down.read(),
            "semantic ComboBox row press must commit and close its overlay");
    const DamageRegion semantic_close_region = window.take_damage();
    const Rect semantic_close_damage = semantic_close_region.bounds();
    require(Rect::intersection(semantic_close_damage, semantic_overlay_bounds) ==
                semantic_overlay_bounds,
            "semantic overlay close must damage every formerly painted overlay pixel");
    RecordingPainter semantic_close_painter;
    window.paint(semantic_close_painter, semantic_close_damage);
    require(semantic_close_painter.saw_window_backplane,
            "overlay removal repaint must restore an opaque themed Window backplane");

    (*combo).dropped_down = true;
    const Point outside{5.0, 5.0};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, outside}) &&
                !(*combo).dropped_down.read(),
            "ComboBox overlay layer must dismiss an outside pointer press");

    (*combo).dropped_down = true;
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) &&
                !(*combo).dropped_down.read() && window.focus_scope_depth() == 0U,
            "Escape inside the overlay focus scope must close ComboBox and restore focus");

    (*combo).dropped_down = true;
    (*combo).visible = false;
    require(!(*combo).dropped_down.read() && !window.find("combo.field.overlay.layer") &&
                window.focus_scope_depth() == 0U,
            "overlay controller must revoke a ComboBox overlay when its owner becomes ineligible");
}

void test_numeric_up_down_composite_edit_spinner_and_keys() {
    std::shared_ptr<gui_forms::NumericUpDown> numeric = make_control<NumericUpDown>(StableId("input.numeric"));
    (*numeric).set_range(-10.0, 10.0);
    (*numeric).increment = 0.5;
    (*numeric).decimal_places = 1U;
    (*numeric).button_width = 30.0;
    (*numeric).value = 1.5;
    (*numeric).set_requested_bounds({0.0, 0.0, 180.0, 32.0});
    Window window(numeric, {180.0, 32.0});
    window.perform_layout();
    require((*numeric).editor() && (*(*numeric).editor()).text.read() == "1.5" &&
                (*window.find("input.numeric.spinner")).absolute_bounds().width == 30.0,
            "NumericUpDown must expose its public TextBox editor and formatted value");
    std::string values;
    SubscriptionToken changed = (*numeric).value_changed().subscribe(
        AppendNumericValue(values));
    require(window.request_focus((*numeric).editor()),
            "NumericUpDown editor must accept retained focus");
    KeyEvent up{KeyAction::down, PhysicalKey::up};
    require(window.dispatch_key(up) && (*numeric).value.read() == 2.0 &&
                (*(*numeric).editor()).text.read() == "2.0",
            "NumericUpDown parent preview must step before TextBox caret routing");

    (*(*numeric).editor()).select_all();
    require(window.dispatch_text({"7.5"}) && (*numeric).value.read() == 7.5,
            "NumericUpDown must commit a complete in-range editor value");
    (*(*numeric).editor()).select_all();
    require(window.dispatch_text({"99"}) && (*numeric).value.read() == 7.5,
            "NumericUpDown must preserve value during an out-of-range intermediate edit");
    KeyEvent enter{KeyAction::down, PhysicalKey::enter};
    require(window.dispatch_key(enter) && (*(*numeric).editor()).text.read() == "7.5",
            "NumericUpDown Enter must reject and restore an invalid editor value");

    const Control::Ptr spinner = window.find("input.numeric.spinner");
    const Rect spin = (*spinner).absolute_bounds();
    const Point lower{spin.x + spin.width * 0.5, spin.y + spin.height * 0.75};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, lower}) &&
                window.dispatch_pointer({PointerAction::up, PointerButton::primary, lower}) &&
                (*numeric).value.read() == 7.0,
            "NumericUpDown spinner lower button must decrement by the public increment");
    (*numeric).hexadecimal = true;
    (*numeric).value = 10.0;
    require((*(*numeric).editor()).text.read() == "A" && values.find("7.000000") != std::string::npos,
            "NumericUpDown hexadecimal formatting must retain ordered value events");
}

void test_list_box_model_stable_item_ids() {
    std::shared_ptr<gui_forms::ListBox> list = make_control<ListBox>(StableId("input.model.list"));
    (*list).items = {"Orchard Study", "North Shore", "Print Masters"};
    (*list).set_item_stable_ids({"completion.orchard", "completion.north",
                               "completion.print"});
    Window window(list, {240.0, 90.0});
    window.perform_layout();
    const std::vector<SemanticNode> children = (*list).semantic_virtual_children();
    require(children.size() == 3U &&
                children[0].stable_id == "completion.orchard" &&
                children[2].stable_id == "completion.print" &&
                window.perform_semantic_action("completion.north",
                                               SemanticAction::select) &&
                (*list).selected_index() == 1U,
            "model-backed ListBox rows must retain exact semantic identities");
    bool duplicate_rejected{};
    try {
        (*list).set_item_stable_ids({"same", "same", "third"});
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected,
            "ListBox model identities must reject ambiguous duplicates");
    (*list).items.append("Field Notes");
    require((*list).item_stable_id(0) == "input.model.list.item.0" &&
                (*list).item_stable_id(3) == "input.model.list.item.3",
            "legacy append must return the collection to index-derived IDs");
}

} // namespace

// TextBox's properties are capabilities: a write runs the old setter's
// checks and effects, and the table reaches the same hooks.
void test_text_box_property_capabilities() {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("capabilities.text"), "start");
    require((*box).text.read() == "start" && (*box).selection().caret.value() == 5U,
            "the constructor's initial_text is the engine's text, with the caret at its end");
    std::vector<std::string> order;
    const SubscriptionToken text_token = (*box).text_changed().subscribe(
        test_support::PushConstant<std::vector<std::string>, std::string, const std::string&>(order, "text"));
    const SubscriptionToken selection_token = (*box).selection_changed().subscribe(
        test_support::PushConstant<std::vector<std::string>, std::string, const TextSelection&>(order, "selection"));
    (*box).text = "changed";
    require((*box).text.read() == "changed" && order.size() == 2U && order[0] == "text" &&
                order[1] == "selection",
            "a text write raises text_changed, then selection_changed");
    (*box).text = "changed";
    require(order.size() == 2U, "writing the current text raises nothing");
    (*box).set_property_value("text", BindingValue{std::string("table")});
    require((*box).text.read() == "table" && order.size() == 4U, "a table write runs the same hooks");
    const std::optional<PropertyDescriptor> text_descriptor = (*box).property_descriptor("text");
    require(text_descriptor && (*text_descriptor).change_notifications,
            "TextBox text reports change notifications (TextChanged)");
    static_cast<void>(text_token);
    static_cast<void>(selection_token);

    (*box).read_only = true;
    require((*box).read_only.read() && (*box).property_value("read_only") == std::optional<BindingValue>{true},
            "read_only is a view onto the engine, read by the table");
    (*box).read_only = false;

    bool refused = false;
    try {
        (*box).maximum_length = 16U * 1024U * 1024U + 1U;
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*box).maximum_length.read() == 0U, "a maximum length above 16 Mi is refused");
    (*box).maximum_length = 4U;
    require((*box).maximum_length.read() == 4U, "maximum_length is a view onto the engine");
    (*box).maximum_length = 0U;

    (*box).use_system_password_character = true;
    refused = false;
    try {
        (*box).multiline = true;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && !(*box).multiline.read(), "a masked box refuses multiline");
    (*box).use_system_password_character = false;
    (*box).multiline = true;
    refused = false;
    try {
        (*box).password_character = U'*';
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*box).password_character.read() == U'\0', "a multiline box refuses a password character");
    refused = false;
    try {
        (*box).use_system_password_character = true;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && !(*box).password_protected(), "a multiline box refuses system masking");
    (*box).multiline = false;

    refused = false;
    try {
        (*box).newline_sequence = std::string("x");
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*box).newline_sequence.read() == "\n", "a newline sequence other than LF, CR or CRLF is refused");
    refused = false;
    try {
        (*box).placeholder_text = std::string("\xFF");
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*box).placeholder_text.read().empty(), "an invalid UTF-8 placeholder is refused");
    refused = false;
    try {
        (*box).font = FontSpec{FontRole::content, 0.0, 400, false};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*box).font.read() == FontSpec{FontRole::content, 12.0, 400, false},
            "an invalid font is refused");

    refused = false;
    try {
        (*box).auto_complete_source = AutoCompleteSource::list_items;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*box).auto_complete_source.read() == AutoCompleteSource::none,
            "a TextBox refuses the list_items source");
    refused = false;
    try {
        (*box).auto_complete_custom_source = std::shared_ptr<AutoCompleteStringCollection>{};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*box).auto_complete_custom_source.read() != nullptr,
            "a null custom source is refused and the box keeps its own");
}

// NumericUpDown's properties are capabilities. minimum and maximum move
// each other as Windows Forms' do, then clamp value (raising value_changed).
void test_numeric_up_down_property_capabilities() {
    std::shared_ptr<gui_forms::NumericUpDown> numeric = make_control<NumericUpDown>(StableId("input.numeric.caps"));
    std::vector<double> values;
    const SubscriptionToken changed = (*numeric).value_changed().subscribe(
        test_support::PushBack<std::vector<double>, double>(values));
    (*numeric).value = 50.0;
    require(values.size() == 1U && values[0] == 50.0, "a value write raises value_changed");
    (*numeric).value = 50.0;
    require(values.size() == 1U, "writing the current value raises nothing");
    bool refused = false;
    try {
        (*numeric).value = 101.0;
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*numeric).value.read() == 50.0, "a value outside the range is refused");

    (*numeric).minimum = 150.0;
    require((*numeric).minimum.read() == 150.0 && (*numeric).maximum.read() == 150.0 &&
                (*numeric).value.read() == 150.0 && values.size() == 2U && values[1] == 150.0,
            "a minimum above maximum raises maximum to it and clamps value");
    (*numeric).maximum = 20.0;
    require((*numeric).minimum.read() == 20.0 && (*numeric).maximum.read() == 20.0 &&
                (*numeric).value.read() == 20.0 && values.size() == 3U,
            "a maximum below minimum lowers minimum to it and clamps value");
    (*numeric).set_range(0.0, 100.0);
    require((*numeric).minimum.read() == 0.0 && (*numeric).maximum.read() == 100.0 &&
                (*numeric).value.read() == 20.0 && values.size() == 3U,
            "set_range writes both bounds; a value inside the range is kept");
    refused = false;
    try {
        (*numeric).minimum = std::numeric_limits<double>::infinity();
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*numeric).minimum.read() == 0.0, "a bound that is not finite is refused");

    (*numeric).decimal_places = 2U;
    (*numeric).hexadecimal = true;
    require((*numeric).hexadecimal.read() && (*numeric).decimal_places.read() == 0U,
            "hexadecimal turns decimal places off");
    (*numeric).decimal_places = 1U;
    require(!(*numeric).hexadecimal.read() && (*numeric).decimal_places.read() == 1U,
            "decimal places turn hexadecimal off");
    refused = false;
    try {
        (*numeric).increment = 0.0;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*numeric).increment.read() == 1.0, "a zero increment is refused");
    refused = false;
    try {
        (*numeric).button_width = 8.0;
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*numeric).button_width.read() == 18.0, "a button width under 12 is refused");
    (*numeric).set_property_value("value", BindingValue{30.0});
    require((*numeric).value.read() == 30.0 && values.size() == 4U, "a table write runs the same hooks");
    static_cast<void>(changed);
}

void test_list_box_property_capabilities() {
    std::shared_ptr<EvenRowListBox> list = make_control<EvenRowListBox>(StableId("input.list.caps"));
    (*list).items = {"one", "two", "three", "four"};
    std::size_t changes{};
    const SubscriptionToken changed = (*list).selection_changed().subscribe(
        test_support::IncrementCounter<std::size_t, const ListSelectionChange&>(changes));
    (*list).selection_mode = ListSelectionMode::multiple_extended;
    (*list).select_index(1U);
    (*list).select_index(3U, false, true);
    require((*list).selected_indices().size() == 2U && changes == 2U, "an extended list keeps two rows");
    (*list).selection_mode = ListSelectionMode::one;
    require((*list).selected_indices().size() == 1U && (*list).selected_indices()[0] == 3U && changes == 3U,
            "switching to one keeps the active row and raises selection_changed");
    (*list).set_property_value("selection_mode", BindingValue{PropertyEnumValue{"list_selection_mode", "multiple_extended", 1}});
    require((*list).selection_mode.read() == ListSelectionMode::multiple_extended, "a table write sets the mode");

    (*list).top_index = 2U;
    require((*list).top_index.read() == 2U, "top_index is written");
    bool refused = false;
    try {
        (*list).top_index = 4U;
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*list).top_index.read() == 2U, "a top index outside the items is refused");
    (*list).items = {"only"};
    require((*list).top_index.read() == 0U, "shrinking the items moves top_index back into range");

    (*list).item_height = 27.0;
    require((*list).item_height.read() == 28.0, "coerce_item_height rewrites the incoming height");
    refused = false;
    try {
        (*list).item_height = 8.0;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*list).item_height.read() == 28.0, "a height under 12 is refused");

    const FontSpec larger{FontRole::content, 14.0, 400, false};
    (*list).font = larger;
    (*list).font = larger;
    require((*list).font.read() == larger && (*list).font_changes == 1,
            "on_font_changed runs once per changed font");
    static_cast<void>(changed);
}

void test_combo_box_property_capabilities() {
    std::shared_ptr<gui_forms::ComboBox> combo = make_control<ComboBox>(StableId("input.combo.caps"));
    (*combo).items = {"Compact", "Comfortable", "Spacious"};
    std::string order;
    const SubscriptionToken index_token = (*combo).selected_index_changed().subscribe(
        test_support::AppendLiteral<std::optional<std::size_t>>(order, "index\n"));
    const SubscriptionToken text_token = (*combo).text_changed().subscribe(
        test_support::AppendLiteral<const std::string&>(order, "text\n"));
    (*combo).selected_index = 1U;
    require(order == "index\ntext\n" && (*combo).text.read() == "Comfortable",
            "a selected_index write raises selected_index_changed, then text_changed");
    (*combo).selected_index = 1U;
    require(order == "index\ntext\n", "writing the current index raises nothing");
    bool refused = false;
    try {
        (*combo).selected_index = 3U;
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*combo).selected_index.read() == 1U, "an index past the end is refused");
    (*combo).text = "Spacious";
    require((*combo).selected_index.read() == 2U, "a text write selects the equal item");
    require((*combo).should_serialize_property("text") && (*combo).reset_property("text") &&
                !(*combo).selected_index.read() && !(*combo).should_serialize_property("text"),
            "the table reset clears the selection; should_serialize follows it");
    (*combo).set_property_value("text", BindingValue{std::string("Compact")});
    require((*combo).selected_index.read() == 0U, "a table text write runs the same hooks");

    (*combo).placeholder_text = "Choose";
    (*combo).drop_down_style = ComboBoxStyle::drop_down;
    const std::shared_ptr<TextBox> editor = (*combo).editor();
    require(editor && (*editor).placeholder_text.read() == "Choose" && (*editor).text.read() == "Compact",
            "the drop_down style creates an editor with the hint and the selected item");
    (*combo).placeholder_text = "Pick one";
    const FontSpec larger{FontRole::content, 14.0, 400, false};
    (*combo).font = larger;
    require((*editor).placeholder_text.read() == "Pick one" && (*editor).font.read() == larger,
            "hint and font writes reach the editor");
    (*combo).drop_down_style = ComboBoxStyle::drop_down_list;
    require(!(*combo).editor(), "the drop_down_list style removes the editor");

    refused = false;
    try {
        (*combo).maximum_drop_down_items = 0U;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*combo).maximum_drop_down_items.read() == 8U, "zero drop-down rows are refused");
    refused = false;
    try {
        (*combo).drop_down_width = -1.0;
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*combo).drop_down_width.read() == 0.0, "a negative drop-down width is refused");
    refused = false;
    try {
        (*combo).auto_complete_custom_source = std::shared_ptr<AutoCompleteStringCollection>{};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*combo).auto_complete_custom_source.read(), "a null custom source is refused");
    (*combo).dropped_down = true;
    require(!(*combo).dropped_down.read(), "a detached combo box does not open its list");
    static_cast<void>(index_token);
    static_cast<void>(text_token);
}

int main() {
    try {
        test_unicode_editing_selection_and_history();
        test_keyboard_text_input_and_read_only();
        test_pointer_drag_paint_and_caret_deadline();
        test_placeholder_and_validation();
        test_text_box_maximum_length_bounds_user_edits();
        test_word_navigation_and_deletion();
        test_clipboard_commands_and_protected_text();
        test_list_box_selection_navigation_and_mutation();
        test_combo_box_overlay_commit_dismiss_and_owner_revocation();
        test_combo_box_scrollbar_reaches_last_item();
        test_numeric_up_down_composite_edit_spinner_and_keys();
        test_list_box_model_stable_item_ids();
        test_text_box_property_capabilities();
        test_numeric_up_down_property_capabilities();
        test_list_box_property_capabilities();
        test_combo_box_property_capabilities();
        std::cout << "gui_forms_input_controls_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_input_controls_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
