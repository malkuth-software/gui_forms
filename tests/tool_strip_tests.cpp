// ToolStrip and StatusStrip (stage W): item layout and parts, clicks and
// commands, check-on-click, split and drop-down buttons, overflow, keyboard,
// semantics, the status layout's spring labels and sizing grip, and hosted
// combo box, text box and progress bar items.

#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

struct RecordClick final {
    std::vector<std::string>* seen{};
    void operator()(const ToolStripItemClick& click) const { (*seen).push_back(click.item_id); }
};

struct CountInvocation final {
    int* count{};
    void operator()(const CommandInvocation&) const { ++*count; }
};

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

const ToolStrip::Item* find_item(const std::vector<ToolStrip::Item>& items, std::string_view id) {
    for (const ToolStrip::Item& item : items) {
        if (item.stable_id == id) return &item;
    }
    return nullptr;
}
const ToolStrip::Item* find_item(std::vector<ToolStrip::Item>&& items, std::string_view id) = delete;

void press(Window& window, Point point) {
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}),
            "the strip takes the press");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
}

std::vector<ToolStripItemSpec> sample_items(const std::shared_ptr<Command>& save,
                                            const std::shared_ptr<Command>& choice) {
    std::vector<ToolStripItemSpec> items;
    items.push_back({.stable_id = "new", .text = "New"});
    items.push_back({.stable_id = "save", .command = save});
    items.push_back({.stable_id = "rule", .kind = ToolStripItemKind::separator});
    items.push_back({.stable_id = "bold", .text = "Bold", .check_on_click = true});
    items.push_back({.stable_id = "export", .kind = ToolStripItemKind::drop_down_button, .text = "Export",
                     .drop_down = {MenuItemSpec("export.pdf", MenuItemKind::command, choice, "PDF")}});
    items.push_back({.stable_id = "run", .kind = ToolStripItemKind::split_button, .text = "Run",
                     .drop_down = {MenuItemSpec("run.watch", MenuItemKind::command, choice, "Watch")}});
    items.push_back({.stable_id = "zoom", .kind = ToolStripItemKind::label, .text = "Zoom"});
    return items;
}

void layout_and_parts() {
    const std::shared_ptr<Command> save = std::make_shared<Command>("save", "Save");
    const std::shared_ptr<Command> choice = std::make_shared<Command>("choice", "Choice");
    std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(StableId("strip"));
    (*strip).set_items(sample_items(save, choice));
    Window window(strip, {640.0, 25.0});
    window.perform_layout();
    const ControlAnatomy& anatomy = (*strip).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none && anatomy.control_id == "tool_strip",
            "ToolStrip publishes a valid anatomy");
    require((*strip).overflow_item_ids().empty() && !(*strip).parts().overflow_button.present,
            "a wide strip overflows nothing and shows no overflow button");
    require((*strip).parts().grip.present, "a tool strip shows its grip");
    const std::vector<ToolStrip::Item> items = (*strip).realized_items();
    require(items.size() == 7U, "every item is laid out");
    const ToolStrip::Item* rule = find_item(items, "rule");
    const ToolStrip::Item* run = find_item(items, "run");
    const ToolStrip::Item* exported = find_item(items, "export");
    const ToolStrip::Item* zoom = find_item(items, "zoom");
    const ToolStrip::Item* saved = find_item(items, "save");
    require(rule != nullptr && (*rule).parts.item_separator.present && !(*rule).parts.item_face.present,
            "a separator is a divider part with no face");
    require(run != nullptr && (*run).parts.item_arrow.present && (*run).parts.item_arrow_glyph.present &&
                (*run).parts.item_face.bounds.x + (*run).parts.item_face.bounds.width <= (*run).parts.item_arrow.bounds.x + 0.001,
            "a split button's face and arrow sit side by side");
    require(exported != nullptr && !(*exported).parts.item_arrow.present && (*exported).parts.item_arrow_glyph.present,
            "a drop-down button is one actuator with an arrow glyph");
    require(zoom != nullptr && !(*zoom).parts.item_face.present && (*zoom).parts.item_label.present,
            "a label shows text without a face");
    require(saved != nullptr && (*saved).parts.item_label.present,
            "an item with a command shows the command's text");
}

void clicks_and_commands() {
    const std::shared_ptr<Command> save = std::make_shared<Command>("save", "Save");
    const std::shared_ptr<Command> choice = std::make_shared<Command>("choice", "Choice");
    int saves = 0;
    SubscriptionToken counted = (*save).invoked().subscribe(CountInvocation{&saves});
    std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(StableId("strip"));
    (*strip).set_items(sample_items(save, choice));
    Window window(strip, {640.0, 25.0});
    window.perform_layout();
    std::vector<std::string> clicks;
    SubscriptionToken token = (*strip).item_clicked().subscribe(RecordClick{&clicks});

    std::vector<ToolStrip::Item> items = (*strip).realized_items();
    press(window, centre((*find_item(items, "save")).parts.item_face.bounds));
    require(saves == 1 && clicks.size() == 1U && clicks[0] == "save",
            "clicking an item executes its command and reports the click");

    press(window, centre((*find_item(items, "bold")).parts.item_face.bounds));
    const std::vector<ToolStrip::Item> bold_items = (*strip).realized_items();
    require((*strip).item_checked("bold") && (*find_item(bold_items, "bold")).states.checked,
            "check-on-click toggles and publishes checked");

    (*save).set_enabled(false);
    items = (*strip).realized_items();
    require((*find_item(items, "save")).states.disabled, "a disabled command disables its item");
    const Point disabled = centre((*find_item(items, "save")).parts.item_face.bounds);
    require(!window.dispatch_pointer({PointerAction::down, PointerButton::primary, disabled}),
            "a disabled item does not take the press");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, disabled}));
    require(saves == 1, "a disabled item does not click");

    const ToolStrip::Item run = *find_item(items, "run");
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, centre(run.parts.item_arrow.bounds)}),
            "the split arrow takes the press");
    const std::vector<ToolStrip::Item> run_items = (*strip).realized_items();
    require((*strip).drop_down_open() && (*find_item(run_items, "run")).states.open,
            "pressing a split button's arrow opens its menu");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, centre(run.parts.item_arrow.bounds)}));
    (*strip).close_drop_down();
    require(!(*strip).drop_down_open(), "the menu closes");
    const std::size_t before = clicks.size();
    press(window, centre(run.parts.item_face.bounds));
    require(clicks.size() == before + 1U && clicks.back() == "run",
            "pressing a split button's face clicks it without opening the menu");
}

void overflow_and_keyboard() {
    const std::shared_ptr<Command> save = std::make_shared<Command>("save", "Save");
    const std::shared_ptr<Command> choice = std::make_shared<Command>("choice", "Choice");
    std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(StableId("strip"));
    (*strip).set_items(sample_items(save, choice));
    Window window(strip, {160.0, 25.0});
    window.perform_layout();
    const std::vector<std::string> overflowed = (*strip).overflow_item_ids();
    require(!overflowed.empty() && overflowed.back() == "zoom", "a narrow strip overflows from its last item");
    require((*strip).parts().overflow_button.present && (*strip).parts().overflow_glyph.present,
            "an overflowing strip shows its overflow button");
    require((*strip).realized_items().size() + overflowed.size() == 7U,
            "an item is either on the strip or in the overflow menu");
    require((*strip).show_overflow() && (*strip).drop_down_open(), "the overflow menu opens");
    (*strip).close_drop_down();

    std::vector<std::string> clicks;
    SubscriptionToken token = (*strip).item_clicked().subscribe(RecordClick{&clicks});
    require(window.request_focus(strip), "the strip takes focus");
    KeyEvent enter{KeyAction::down, PhysicalKey::enter};
    require(window.dispatch_key(enter) && clicks.size() == 1U && clicks[0] == "new",
            "Enter clicks the first item when the strip takes focus");
    KeyEvent right{KeyAction::down, PhysicalKey::right};
    require(window.dispatch_key(right) && window.dispatch_key(enter) && clicks.back() == "save",
            "Right moves to the next item");

    const SemanticDescriptor descriptor = (*strip).semantic_descriptor();
    const std::vector<SemanticNode> children = (*strip).semantic_virtual_children();
    require(descriptor.role == SemanticRole::toolbar && !children.empty() &&
                children[0].role == SemanticRole::button,
            "a strip is a toolbar of buttons to accessibility");
}

void status_strip() {
    std::shared_ptr<StatusStrip> status = make_control<StatusStrip>(StableId("status"));
    (*status).set_items({
        {.stable_id = "state", .kind = ToolStripItemKind::label, .text = "Ready", .spring = true},
        {.stable_id = "position", .kind = ToolStripItemKind::label, .text = "Ln 1, Col 1"},
        {.stable_id = "encoding", .kind = ToolStripItemKind::label, .text = "UTF-8"},
    });
    Window window(status, {600.0, 24.0});
    window.perform_layout();
    require((*status).anatomy().control_id == "status_strip" &&
                (*status).anatomy().item_systems.size() == 1U &&
                validate_control_anatomy((*status).anatomy()).defect == AnatomyDefect::none,
            "StatusStrip publishes its own anatomy with ToolStrip's item system");
    require((*status).parts().sizing_grip.present, "a status strip shows its sizing grip");
    const std::vector<ToolStrip::Item> items = (*status).realized_items();
    require(items.size() == 3U, "every status item is laid out");
    const ToolStrip::Item* encoding = find_item(items, "encoding");
    require(encoding != nullptr &&
                (*encoding).parts.item_label.bounds.x + (*encoding).parts.item_label.bounds.width >
                    600.0 - 12.0 - 60.0,
            "a spring label pushes the items after it to the end");
    require((*status).overflow_item_ids().empty(), "a status strip never overflows");
    (*status).sizing_grip_visible = false;
    require(!(*status).parts().sizing_grip.present, "the sizing grip can be hidden");
    require((*status).semantic_descriptor().role == SemanticRole::status_bar, "a status strip is a status bar");
}


std::vector<ToolStripItemSpec> hosted_items() {
    std::vector<ToolStripItemSpec> items;
    items.push_back({.stable_id = "new", .text = "New"});
    items.push_back({.stable_id = "zoom", .kind = ToolStripItemKind::combo_box, .text = "Zoom",
                     .choices = {"50%", "100%", "200%"}});
    items.push_back({.stable_id = "find", .kind = ToolStripItemKind::text_box, .text = "Find", .width = 80.0});
    items.push_back({.stable_id = "load", .kind = ToolStripItemKind::progress_bar, .text = "Loading"});
    items.push_back({.stable_id = "help", .text = "Help"});
    return items;
}

template <typename Expected>
bool refuses(std::shared_ptr<ToolStrip>& strip, std::vector<ToolStripItemSpec> items) {
    try {
        (*strip).set_items(std::move(items));
    } catch (const Expected&) {
        return true;
    }
    return false;
}

void hosted_controls() {
    std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(StableId("strip"));
    (*strip).set_items(hosted_items());
    Window window(strip, {640.0, 25.0});
    window.perform_layout();
    const std::shared_ptr<ComboBox> zoom = (*strip).combo_box("zoom");
    const std::shared_ptr<TextBox> find = (*strip).text_box("find");
    const std::shared_ptr<ProgressBar> load = (*strip).progress_bar("load");
    require(zoom && find && load && (*strip).children().size() == 3U,
            "each hosted item owns one child control of its kind");
    require((*zoom).items.read().size() == 3U && (*zoom).accessible_name.read() == "Zoom" &&
                (*zoom).stable_id().value() == "strip.zoom",
            "a combo box takes its choices and name from the item, and its ID from the strip and item");
    const std::vector<ToolStrip::Item> items = (*strip).realized_items();
    const ToolStrip::Item* combo = find_item(items, "zoom");
    const ToolStrip::Item* text = find_item(items, "find");
    const ToolStrip::Item* progress = find_item(items, "load");
    require(combo != nullptr && (*combo).parts.item_combo_box.present && !(*combo).parts.item_face.present &&
                (*combo).parts.item_combo_box.bounds == (*zoom).committed_arranged_bounds(),
            "a combo box item publishes its hosted part where the strip placed the control");
    require(text != nullptr && (*text).parts.item_text_box.present &&
                (*text).parts.item_text_box.bounds.width == 80.0 &&
                (*text).parts.item_text_box.bounds == (*find).committed_arranged_bounds(),
            "a text box item takes its width from the item");
    require(progress != nullptr && (*progress).parts.item_progress_bar.present,
            "a progress bar item publishes its hosted part");
    (*load).value = 40.0;
    require((*(*strip).progress_bar("load")).value.read() == 40.0, "the application drives the hosted control");

    const Point inside = centre((*text).parts.item_text_box.bounds);
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, inside}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, inside}));
    require(window.focused_control() == find, "a click on a hosted text box focuses it");

    const std::vector<SemanticNode> children = (*strip).semantic_virtual_children();
    require(children.size() == 2U, "hosted controls describe themselves, not as virtual children");

    (*find).text = "needle";
    std::vector<ToolStripItemSpec> next = hosted_items();
    next[1].choices = {"Fit"};
    next[3].kind = ToolStripItemKind::text_box;
    (*strip).set_items(std::move(next));
    require((*strip).text_box("find") == find && (*find).text.read() == "needle",
            "set_items keeps a hosted control of the same ID and kind, with its state");
    require((*(*strip).combo_box("zoom")).items.read().size() == 1U, "set_items updates a combo box's choices");
    require((*strip).text_box("load") != nullptr && (*strip).children().size() == 3U,
            "an item that changes kind gets a new control and the old one is removed");

    (*strip).set_items(hosted_items());
    window.resize({200.0, 25.0});
    window.perform_layout();
    const std::vector<std::string> overflowed = (*strip).overflow_item_ids();
    for (const std::string& id : overflowed) {
        require(id != "zoom" && id != "find" && id != "load", "hosted items never move to the overflow menu");
    }
    require(!overflowed.empty(), "a narrow strip still overflows its buttons");

    std::vector<ToolStripItemSpec> commanded = hosted_items();
    commanded[1].command = std::make_shared<Command>("zoom", "Zoom");
    require(refuses<std::invalid_argument>(strip, std::move(commanded)), "a hosted item takes no command");
    std::vector<ToolStripItemSpec> chosen = hosted_items();
    chosen[0].choices = {"x"};
    require(refuses<std::invalid_argument>(strip, std::move(chosen)), "only a combo box takes choices");
    std::vector<ToolStripItemSpec> wide = hosted_items();
    wide[2].width = -1.0;
    require(refuses<std::invalid_argument>(strip, std::move(wide)), "a negative width is refused");
    bool threw = false;
    try {
        static_cast<void>((*strip).combo_box("find"));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "asking for the wrong kind of hosted control is refused");
}
} // namespace

// Audit N98: the layout is cached, keyed by what it reads; each input that
// moves items still moves them. An item's text, its command's text and
// visibility, and the grip are written after a layout has been read.
void cached_layout_follows_inputs() {
    const std::shared_ptr<Command> save = std::make_shared<Command>("save", "Save");
    const std::shared_ptr<Command> choice = std::make_shared<Command>("choice", "Choice");
    std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(StableId("cached"));
    (*strip).set_items(sample_items(save, choice));
    Window window(strip, {640.0, 25.0});
    window.perform_layout();
    const std::vector<ToolStrip::Item> first = (*strip).realized_items();
    const double new_width = (*find_item(first, "new")).parts.item_face.bounds.width;
    const double save_x = (*find_item(first, "save")).parts.item_face.bounds.x;

    (*strip).set_item_text("new", "A much longer label");
    window.perform_layout();
    const std::vector<ToolStrip::Item> longer = (*strip).realized_items();
    require((*find_item(longer, "new")).parts.item_face.bounds.width > new_width &&
                (*find_item(longer, "save")).parts.item_face.bounds.x > save_x,
            "a longer item text widens the item and moves the next one");

    const double bold_x = (*find_item(longer, "bold")).parts.item_face.bounds.x;
    (*save).set_text("Save everything now");
    window.perform_layout();
    const std::vector<ToolStrip::Item> command_text = (*strip).realized_items();
    require((*find_item(command_text, "bold")).parts.item_face.bounds.x > bold_x,
            "a longer command text moves the items after it");

    (*save).set_visible(false);
    window.perform_layout();
    const std::vector<ToolStrip::Item> hidden = (*strip).realized_items();
    require(find_item(hidden, "save") == nullptr || !(*find_item(hidden, "save")).parts.item_face.present,
            "a hidden command's item leaves the layout");

    const double grip_off = (*find_item(hidden, "new")).parts.item_face.bounds.x;
    (*strip).grip_visible = !(*strip).grip_visible.read();
    window.perform_layout();
    const std::vector<ToolStrip::Item> gripped = (*strip).realized_items();
    require((*find_item(gripped, "new")).parts.item_face.bounds.x != grip_off,
            "toggling the grip moves the first item");
}

// Audit S5, N98: the strip keeps its layout and measured widths, and drops
// them when an item's text or its command's text changes.
// The laid-out face width of one item (0 when it is not laid out).
double face_width(const ToolStrip& strip, std::string_view item_id) {
    const std::vector<ToolStrip::Item> items = strip.realized_items();
    const ToolStrip::Item* item = find_item(items, item_id);
    if (item == nullptr) return 0.0;
    const double width = (*item).parts.item_face.bounds.width;
    return width;
}

void kept_layout_follows_changes() {
    const std::shared_ptr<Command> save = std::make_shared<Command>("save", "Save");
    const std::shared_ptr<Command> choice = std::make_shared<Command>("choice", "Choice");
    std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(StableId("kept"));
    (*strip).set_items(sample_items(save, choice));
    Window window(strip, {640.0, 25.0});
    window.perform_layout();
    const double new_width = face_width(*strip, "new");
    const double save_width = face_width(*strip, "save");
    (*strip).set_item_text("new", "A much longer New");
    window.perform_layout();
    require(face_width(*strip, "new") > new_width,
            "an item's new text is measured again");
    (*save).set_text("Save everything now");
    window.perform_layout();
    require(face_width(*strip, "save") > save_width,
            "a command's new text is measured again");
    const double widened = face_width(*strip, "save");
    window.resize({300.0, 25.0});
    window.perform_layout();
    require(face_width(*strip, "save") == widened,
            "a resize lays out again with the kept widths");
}

int main() {
    layout_and_parts();
    clicks_and_commands();
    overflow_and_keyboard();
    status_strip();
    hosted_controls();
    cached_layout_follows_inputs();
    kept_layout_follows_changes();
    std::cout << "ToolStrip and StatusStrip: parts, clicks, commands, split and drop-down, overflow, keys, semantics and status layout passed\n";
    return 0;
}
