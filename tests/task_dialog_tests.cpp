// TaskDialog (stage W): page validation, the in-window modal presentation,
// published parts, buttons that close and buttons that do not, the default
// button, Escape, radio buttons, the verification check box, the expander,
// the progress bar, navigation to another page, semantics and disposal.

#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

class CountingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view text, FontSpec, Color) override { texts.emplace_back(text); }
    void draw_image(ImageId, Rect, double) override {}

    std::vector<std::string> texts;
};

struct Recorder final {
    std::vector<std::string> clicks;
    std::vector<TaskDialogResult> results;
    std::vector<bool> expansions;
    std::vector<bool> verifications;
    std::vector<std::string> radios;
    int navigations{};

    void on_click(TaskDialogButtonClick& click) { clicks.push_back(click.button_id); }
    void on_closed(const TaskDialogResult& result) { results.push_back(result); }
    void on_expanded(bool expanded) { expansions.push_back(expanded); }
    void on_verification(bool checked) { verifications.push_back(checked); }
    void on_radio(const std::string& radio) { radios.push_back(radio); }
    void on_navigated() { ++navigations; }
};

// Navigates to the second page when "next" is clicked.
struct Wizard final {
    TaskDialog* dialog{};
    TaskDialogPage next_page{};
    void on_click(TaskDialogButtonClick& click) {
        if (click.button_id != "next") return;
        click.keep_open = true;
        (*dialog).set_page(next_page);
    }
};

struct CountClicks final {
    int* count{};
    void operator()(ButtonBase&) const { ++*count; }
};

void subscribe(TaskDialog& dialog, Recorder& recorder, std::vector<SubscriptionToken>& tokens) {
    tokens.push_back(dialog.button_clicked().subscribe(
        Delegate<TaskDialogButtonClick&>::bind<Recorder, &Recorder::on_click>(recorder)));
    tokens.push_back(dialog.closed().subscribe(
        Delegate<const TaskDialogResult&>::bind<Recorder, &Recorder::on_closed>(recorder)));
    tokens.push_back(
        dialog.expanded_changed().subscribe(Delegate<bool>::bind<Recorder, &Recorder::on_expanded>(recorder)));
    tokens.push_back(
        dialog.verification_changed().subscribe(Delegate<bool>::bind<Recorder, &Recorder::on_verification>(recorder)));
    tokens.push_back(dialog.radio_button_changed().subscribe(
        Delegate<const std::string&>::bind<Recorder, &Recorder::on_radio>(recorder)));
    tokens.push_back(dialog.navigated().subscribe(Delegate<>::bind<Recorder, &Recorder::on_navigated>(recorder)));
}

TaskDialogPage full_page() {
    TaskDialogPage page;
    page.caption = "Station scan";
    page.heading = "Replace the saved stations?";
    page.text = "The scan found 42 stations. Replacing the list removes the stations you saved by hand.";
    page.icon = TaskDialogIcon::warning;
    page.buttons = {{.standard = TaskDialogStandardButton::yes},
                    {.standard = TaskDialogStandardButton::no},
                    {.stable_id = "preview", .text = "Preview", .allow_close = false}};
    page.default_button = "no";
    page.radio_buttons = {{.stable_id = "fm", .text = "FM only", .checked = true},
                          {.stable_id = "all", .text = "Every band"}};
    page.verification_text = "Do not ask again";
    page.expander = TaskDialogExpander{.text = "Hand-saved stations: 7. Scanned: 42."};
    page.footnote = "The previous list is kept as a backup.";
    page.footnote_icon = TaskDialogIcon::information;
    page.progress_bar = TaskDialogProgressBar{.value = 40.0};
    return page;
}

Point centre(const Control& control) {
    const Rect bounds = control.absolute_bounds();
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

void click_at(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
}

std::shared_ptr<Control> as_control(const std::shared_ptr<Button>& button) {
    const std::shared_ptr<Control> control = button;
    return control;
}

struct SetPage final {
    TaskDialog* dialog{};
    TaskDialogPage page{};
    void operator()() const { (*dialog).set_page(page); }
};

template <typename Action>
bool throws_invalid(Action action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void validation() {
    std::shared_ptr<TaskDialog> dialog = make_control<TaskDialog>(StableId("dialog.check"));
    const ControlAnatomy& anatomy = (*dialog).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none && anatomy.control_id == "task_dialog",
            "TaskDialog publishes a valid anatomy");
    TaskDialogPage repeated;
    repeated.buttons = {{.standard = TaskDialogStandardButton::ok}, {.stable_id = "ok", .text = "Also OK"}};
    require(throws_invalid(SetPage{dialog.get(), repeated}), "a repeated button ID is refused");
    TaskDialogPage radios;
    radios.radio_buttons = {{.stable_id = "a", .checked = true}, {.stable_id = "b", .checked = true}};
    require(throws_invalid(SetPage{dialog.get(), radios}), "two checked radio buttons are refused");
    TaskDialogPage missing;
    missing.default_button = "nowhere";
    require(throws_invalid(SetPage{dialog.get(), missing}), "a default button the page lacks is refused");
    TaskDialogPage progress;
    progress.progress_bar = TaskDialogProgressBar{.minimum = 5.0, .maximum = 5.0};
    require(throws_invalid(SetPage{dialog.get(), progress}), "empty progress bounds are refused");
    TaskDialogPage done;
    done.heading = "Done";
    (*dialog).set_page(done);
    require((*dialog).page().buttons.size() == 1U && (*dialog).page().buttons[0].stable_id == "ok" &&
                (*dialog).page().buttons[0].text == "OK",
            "a page without buttons gets an OK button");
    (*dialog).expanded = true;
    require(!(*dialog).expanded.read(), "a page without an expander ignores an expanded write and reads false");
    (*dialog).verification_checked = true;
    require((*dialog).verification_checked.read() && (*dialog).page().verification_checked,
            "on a page without a check box a verification write stores the page's state");
}

void modal_presentation() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("dialog.root"));
    std::shared_ptr<Button> owner = make_control<Button>(StableId("dialog.owner"), "Scan");
    (*root).add_child(owner);
    (*owner).set_requested_bounds({20.0, 20.0, 120.0, 32.0});
    Window window(root, {720.0, 560.0});
    window.perform_layout();
    int owner_clicks = 0;
    SubscriptionToken owner_token = (*owner).clicked().subscribe(CountClicks{&owner_clicks});

    std::shared_ptr<TaskDialog> dialog = make_control<TaskDialog>(StableId("dialog.scan"));
    Recorder recorder;
    std::vector<SubscriptionToken> tokens;
    subscribe(*dialog, recorder, tokens);
    (*dialog).set_page(full_page());
    require((*dialog).show(owner) && (*dialog).shown(), "the dialog shows over its owner's window");
    require(!(*dialog).show(owner), "a shown dialog does not show twice");
    window.perform_layout();
    require(window.focused_control() == as_control((*dialog).button("no")), "the default button takes focus");

    const TaskDialog::Parts parts = (*dialog).parts();
    require(parts.caption_band.present && parts.caption.present && parts.heading.present && parts.text.present,
            "the caption, heading and text are laid out");
    require(parts.icon_plate.present && parts.icon_label.present && !parts.icon_glyph.present,
            "a warning icon is a plate with a letter");
    require(parts.progress.present && parts.radio_group.present && parts.expander_glyph.present &&
                parts.expander_button.present && !parts.expanded_text.present && parts.verification.present,
            "progress, radio buttons, the collapsed expander and the check box are laid out");
    require(parts.button_band.present && parts.button_row.present && parts.footnote_band.present &&
                parts.footnote_plate.present && parts.footnote.present,
            "the button band and the footnote are laid out");
    require(parts.heading.bounds.y < parts.text.bounds.y && parts.text.bounds.y < parts.button_band.bounds.y &&
                parts.button_band.bounds.y < parts.footnote_band.bounds.y,
            "the parts stack from heading to footnote");
    const Rect dialog_bounds = (*dialog).absolute_bounds();
    require(dialog_bounds.x > 0.0 && dialog_bounds.y > 0.0 && dialog_bounds.x + dialog_bounds.width < 720.0,
            "the dialog is centred in the window");

    CountingPainter painter;
    (*dialog).on_paint(painter, (*dialog).client_rectangle());
    bool caption = false;
    bool letter = false;
    for (const std::string& text : painter.texts) {
        if (text == "Station scan") caption = true;
        if (text == "!") letter = true;
    }
    require(caption && letter, "the caption and the icon letter are painted by role");

    click_at(window, centre(*owner));
    require(owner_clicks == 0 && (*dialog).shown(), "the window behind the dialog takes no clicks");

    click_at(window, centre(*(*dialog).button("preview")));
    require(recorder.clicks.size() == 1U && recorder.clicks[0] == "preview" && (*dialog).shown(),
            "a button that does not close raises button_clicked and stays open");

    (*dialog).expanded = true;
    window.perform_layout();
    require((*dialog).parts().expanded_text.present &&
                (*dialog).parts().expander_glyph.states.contains(AnatomyState::open) &&
                recorder.expansions.size() == 1U && recorder.expansions[0],
            "expanding shows the expanded text and opens the glyph");
    click_at(window, centre(*(*dialog).expander_button()));
    require(!(*dialog).expanded.read() && recorder.expansions.size() == 2U, "the expander button collapses it again");

    (*(*dialog).radio_button("all")).checked = true;
    require((*dialog).checked_radio_button() == "all" && recorder.radios.size() == 1U && recorder.radios[0] == "all",
            "checking a radio button reports it");
    (*(*dialog).verification_check_box()).checked = true;
    require((*dialog).verification_checked.read() && recorder.verifications.size() == 1U,
            "the verification check box reports its state");
    (*dialog).expanded = false;
    require(recorder.expansions.size() == 2U, "an equal expanded write raises nothing");
    (*dialog).verification_checked = false;
    require(!(*dialog).verification_checked.read() && !(*(*dialog).verification_check_box()).checked.read() &&
                recorder.verifications.size() == 2U && !recorder.verifications[1],
            "a verification write goes through the check box and raises verification_changed");
    (*dialog).verification_checked = true;
    require((*(*dialog).verification_check_box()).checked.read() && recorder.verifications.size() == 3U,
            "the verification state is checked again for the result below");
    (*dialog).set_progress_value(250.0);
    require((*dialog).page().progress_bar && (*(*dialog).page().progress_bar).value == 100.0,
            "a progress value is clamped to the bounds");

    static_cast<void>(window.dispatch_key({KeyAction::down, PhysicalKey::escape}));
    require((*dialog).shown(), "Escape does not close a page without a cancel button or allow_cancel");
    const std::shared_ptr<Control> radio = (*dialog).radio_button("all");
    require(window.request_focus(radio), "a radio button takes focus");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::enter}), "Enter is taken");
    require(!(*dialog).shown() && recorder.results.size() == 1U && recorder.results[0].button_id == "no" &&
                recorder.results[0].verification_checked && recorder.results[0].radio_button_id == "all",
            "Enter away from a button clicks the default button and closes with the page's state");
    require((*dialog).result() && (*(*dialog).result()).button_id == "no" && !(*dialog).parent(),
            "the result stays readable and the dialog leaves the window");
    window.perform_layout();
    click_at(window, centre(*owner));
    require(owner_clicks == 1, "the window takes clicks again once the dialog closed");
}

void navigation_and_cancel() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("wizard.root"));
    Window window(root, {640.0, 480.0});
    window.perform_layout();
    std::shared_ptr<TaskDialog> dialog = make_control<TaskDialog>(StableId("wizard.dialog"));
    Recorder recorder;
    std::vector<SubscriptionToken> tokens;
    subscribe(*dialog, recorder, tokens);
    TaskDialogPage first;
    first.heading = "Step one";
    first.icon = TaskDialogIcon::error;
    first.buttons = {{.stable_id = "next", .text = "Next"}};
    TaskDialogPage second;
    second.heading = "Step two";
    second.buttons = {{.standard = TaskDialogStandardButton::ok}};
    second.allow_cancel = true;
    Wizard wizard{dialog.get(), second};
    tokens.push_back(
        (*dialog).button_clicked().subscribe(Delegate<TaskDialogButtonClick&>::bind<Wizard, &Wizard::on_click>(wizard)));
    (*dialog).set_page(first);
    require((*dialog).show(root), "the first page shows");
    window.perform_layout();
    require((*dialog).parts().icon_glyph.present && !(*dialog).parts().icon_label.present,
            "an error icon is a plate with a mark");
    require((*dialog).click("next") && (*dialog).shown() && recorder.navigations == 1 &&
                (*dialog).page().heading == "Step two",
            "a button handler navigates to another page");
    window.perform_layout();
    require(window.focused_control() == as_control((*dialog).button("ok")),
            "the new page's default button takes focus");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) && !(*dialog).shown() &&
                recorder.results.back().button_id == "cancel",
            "Escape on a page that allows cancel closes it as cancelled");

    TaskDialogPage cancellable;
    cancellable.buttons = {{.standard = TaskDialogStandardButton::ok}, {.standard = TaskDialogStandardButton::cancel}};
    (*dialog).set_page(cancellable);
    require((*dialog).show(root), "the dialog shows again");
    window.perform_layout();
    const std::size_t clicks = recorder.clicks.size();
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) && !(*dialog).shown() &&
                recorder.clicks.size() == clicks + 1U && recorder.clicks.back() == "cancel" &&
                recorder.results.back().button_id == "cancel",
            "Escape clicks the cancel button");

    require((*dialog).show(root), "the dialog shows once more");
    (*dialog).close("custom");
    require(!(*dialog).shown() && recorder.results.back().button_id == "custom", "close names its result");

    const SemanticDescriptor descriptor = (*dialog).semantic_descriptor();
    require(descriptor.role == SemanticRole::dialog && descriptor.exposed && !descriptor.modal,
            "a closed dialog is a dialog, not modal");

    require((*dialog).show(root), "the dialog shows before disposal");
    require((*dialog).semantic_descriptor().modal, "a shown dialog is modal");
    const std::size_t results = recorder.results.size();
    (*dialog).dispose();
    require(recorder.results.size() == results && !(*dialog).shown(), "a disposed dialog closes without an event");
}

} // namespace

int main() {
    validation();
    modal_presentation();
    navigation_and_cancel();
    std::cout << "TaskDialog: validation, modal presentation, parts, buttons, Enter and Escape, radio buttons, "
                 "verification, expander, progress, navigation, semantics and disposal passed\n";
    return 0;
}
