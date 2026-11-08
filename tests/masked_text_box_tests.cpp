// MaskedTextBox (stage W): the renderer-free MaskedTextProvider (mask
// language, culture symbols, case shifts, escapes, prompts, insert and
// overwrite, removal, text formats, completion), TextBox's edit filter, and
// the control: typing through the mask, refusals, literal skipping, deletion,
// overwrite mode, paste, undo, program text, mask changes, validation,
// semantics and the published anatomy.

#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"

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

bool throws_invalid_argument(const char* mask) {
    try {
        const MaskedTextProvider provider{std::string(mask)};
        static_cast<void>(provider);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// -- MaskedTextProvider -----------------------------------------------------------

void provider_language_and_formats() {
    MaskedTextProvider phone("(000) 000-0000");
    require(phone.length() == 14U && phone.edit_position_count() == 10U,
            "a phone mask has fourteen slots, ten of them editable");
    require(phone.display_text() == "(___) ___-____", "unassigned positions show the prompt");
    require(!phone.is_edit_position(MaskPosition(0U)) && phone.is_edit_position(MaskPosition(1U)),
            "literals are not edit positions");

    MaskEditResult result = phone.set_text("5551234567");
    require(result.succeeded() && phone.display_text() == "(555) 123-4567" && phone.mask_completed() &&
                phone.mask_full(),
            "set_text fills the edit positions and passes over literals");
    require(phone.to_string(MaskTextFormat::exclude_prompt_and_literals) == "5551234567" &&
                phone.to_string(MaskTextFormat::include_literals) == "(555) 123-4567",
            "text formats with and without literals");

    result = phone.set_text("555");
    require(result.succeeded() && !phone.mask_completed() &&
                phone.to_string(MaskTextFormat::include_literals) == "(555" &&
                phone.to_string(MaskTextFormat::include_prompt_and_literals) == "(555) ___-____" &&
                phone.to_string(MaskTextFormat::include_prompt) == "555_______" &&
                phone.to_string(MaskTextFormat::exclude_prompt_and_literals) == "555",
            "without prompts the text ends at the last assigned position");
    require(phone.set_text("(555) 12").succeeded() && phone.display_text() == "(555) 12_-____",
            "input literals equal to the mask's are passed over");

    const std::string before = phone.display_text();
    result = phone.insert_at("x", MaskPosition(1U));
    require(!result.succeeded() && result.hint == MaskResultHint::digit_expected &&
                result.position == MaskPosition(1U) && phone.display_text() == before,
            "a refused character reports its position and reason and changes nothing");
    result = phone.set_text("55512345678");
    require(!result.succeeded() && result.hint == MaskResultHint::unavailable_edit_position &&
                phone.display_text() == before,
            "input longer than the mask is refused whole");
}

void provider_edits() {
    MaskedTextProvider code("000-000");
    require(code.set_text("12").succeeded(), "two digits placed");
    require(code.insert_at("9", MaskPosition(0U)).succeeded() && code.display_text() == "912-___",
            "insert moves the following characters on, over the literal");
    require(code.replace_at("7", MaskPosition(1U)).succeeded() && code.display_text() == "972-___",
            "replace_at overwrites in place");
    require(code.insert_at("5", MaskPosition(3U)).succeeded() && code.display_text() == "972-5__",
            "input at a literal goes to the next edit position");
    require(code.insert_at("-", MaskPosition(3U)).hint == MaskResultHint::character_escaped,
            "input equal to the literal at its position is passed over");
    require(code.remove_range(MaskPosition(0U), MaskPosition(1U)).succeeded() &&
                code.display_text() == "725-___",
            "remove_range moves the following characters back over the literal");
    require(code.set_text("123456").succeeded(), "the mask filled");
    const MaskEditResult full = code.insert_at("0", MaskPosition(0U));
    require(!full.succeeded() && full.hint == MaskResultHint::unavailable_edit_position &&
                code.display_text() == "123-456",
            "insert into a full mask is refused");
    require(code.replace_range("99", MaskPosition(1U), MaskPosition(5U)).succeeded() &&
                code.display_text() == "199-56_",
            "replace_range removes the range, then inserts");

    MaskedTextProvider letters("LL00");
    require(letters.set_text("ab12").succeeded(), "letters then digits");
    const MaskEditResult moved = letters.remove_range(MaskPosition(0U), MaskPosition(1U));
    require(!moved.succeeded() && moved.hint == MaskResultHint::letter_expected &&
                letters.display_text() == "ab12",
            "a removal that would move a digit into a letter position is refused");
}

void provider_kinds_case_and_culture() {
    MaskedTextProvider shifted(">LLL<LL|L");
    require(shifted.set_text("abcDEf").succeeded() && shifted.display_text() == "ABCdef",
            "> and < convert case; | stops converting");
    MaskedTextProvider kinds("09#L?&CAa");
    require(kinds.verify_character(U'x', MaskPosition(0U)).hint == MaskResultHint::digit_expected &&
                kinds.verify_character(U'-', MaskPosition(2U)).succeeded() &&
                kinds.verify_character(U'x', MaskPosition(2U)).hint == MaskResultHint::signed_digit_expected &&
                kinds.verify_character(U'1', MaskPosition(3U)).hint == MaskResultHint::letter_expected &&
                kinds.verify_character(U'é', MaskPosition(5U)).succeeded() &&
                kinds.verify_character(U'-', MaskPosition(7U)).hint == MaskResultHint::alphanumeric_expected &&
                kinds.verify_character(U'7', MaskPosition(8U)).succeeded(),
            "each mask character accepts its own kind of input");
    kinds.set_ascii_only(true);
    require(kinds.verify_character(U'é', MaskPosition(5U)).hint == MaskResultHint::ascii_character_expected,
            "ascii_only refuses input past ASCII");
    require(kinds.set_text("1 +a b").succeeded() && !kinds.is_assigned(MaskPosition(1U)) &&
                !kinds.mask_completed(),
            "a space resets an optional position");
    kinds.set_reset_on_space(false);
    require(kinds.set_text("1 +a").succeeded() && kinds.is_assigned(MaskPosition(1U)),
            "without reset_on_space the 9 position takes a space");

    MaskedTextProvider money("$999.99");
    require(money.display_text() == "\xC2\xA4___.__" && money.length() == 7U,
            "the invariant culture's currency sign and decimal point are literal slots");
    MaskCulture swedish;
    swedish.decimal_separator = ",";
    swedish.currency_symbol = "kr";
    MaskedTextProvider kronor("$999.99", swedish);
    require(kronor.display_text() == "kr___,__" && kronor.length() == 8U,
            "a culture symbol of two scalars is two literal slots");
    require(money.display_offset(MaskPosition(1U)) == Utf8Offset(2U) &&
                money.display_position(Utf8Offset(1U)) == MaskPosition(0U) &&
                money.display_position(Utf8Offset(2U)) == MaskPosition(1U),
            "positions and display byte offsets convert both ways");

    MaskedTextProvider escaped("\\0-00\\\\");
    require(escaped.display_text() == "0-__\\" && escaped.edit_position_count() == 2U,
            "a backslash makes the next character literal");
    require(throws_invalid_argument("00\\") && throws_invalid_argument("0\t0"),
            "a lone trailing backslash and control characters are refused");

    MaskedTextProvider optional("00-99");
    require(optional.set_text("12").succeeded() && optional.mask_completed() && !optional.mask_full(),
            "completed needs the required positions, full needs every position");
    optional.set_allow_prompt_as_input(false);
    require(optional.verify_character(U'_', MaskPosition(0U)).hint ==
                MaskResultHint::prompt_character_not_allowed,
            "the prompt is refused as input when not allowed");
    optional.set_allow_prompt_as_input(true);
    require(optional.replace_at("_", MaskPosition(0U)).succeeded() && !optional.is_assigned(MaskPosition(0U)),
            "typing the prompt resets its position");
    optional.set_prompt_character(U'*');
    require(optional.display_text() == "*2-**", "the prompt character is configurable");
}

// -- TextBox edit filter ------------------------------------------------------

// Refuses digits, upper-cases letters through a rewrite and records kinds.
struct UpperCaseFilter final {
    std::vector<TextEditKind> kinds{};
    TextBox* box{};

    void filter(TextEdit& edit) {
        kinds.push_back(edit.kind);
        for (const char character : edit.replacement) {
            if (character >= '0' && character <= '9') {
                edit.verdict = TextEditVerdict::refuse;
                return;
            }
        }
        std::string text((*box).text.read());
        std::string replacement(edit.replacement);
        for (char& character : replacement) {
            if (character >= 'a' && character <= 'z') character = static_cast<char>(character - 'a' + 'A');
        }
        text.replace(edit.start.value(), edit.end.value() - edit.start.value(), replacement);
        edit.verdict = TextEditVerdict::rewrite;
        edit.rewritten_text = text;
        edit.rewritten_caret = Utf8Offset(edit.start.value() + replacement.size());
    }
};

// Changes the box from inside the filter, which the box must survive.
struct MeddlingFilter final {
    TextBox* box{};
    void filter(TextEdit&) { (*box).text = "meddled"; }
};

void text_box_edit_filter() {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("filtered"));
    (*box).set_requested_bounds({0.0, 0.0, 200.0, 30.0});
    Window window(box, {200.0, 30.0});
    window.perform_layout();
    UpperCaseFilter upper{.kinds = {}, .box = box.get()};
    (*box).set_edit_filter(Delegate<TextEdit&>::bind<UpperCaseFilter, &UpperCaseFilter::filter>(upper));
    require(window.request_focus(box), "the box takes focus");
    require(window.dispatch_text({"ab"}) && (*box).text.read() == "AB" &&
                (*box).selection().caret == Utf8Offset(2U),
            "a rewrite replaces the text and places the caret");
    require(!window.dispatch_text({"7"}) && (*box).text.read() == "AB", "a refusal applies nothing");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::backspace}) && (*box).text.read() == "A",
            "deletions go through the filter");
    require((*box).on_semantic_action(SemanticAction::set_value, "xyz") && (*box).text.read() == "XYZ",
            "set_value goes through the filter");
    require(upper.kinds.size() == 4U && upper.kinds[0] == TextEditKind::typing &&
                upper.kinds[2] == TextEditKind::deletion && upper.kinds[3] == TextEditKind::value,
            "the filter learns each edit's kind");
    require((*box).undo() && (*box).text.read() == "A", "a rewrite is one undo step");
    (*box).text = "program 1";
    require((*box).text.read() == "program 1" && upper.kinds.size() == 4U, "set_text bypasses the filter");

    MeddlingFilter meddling{box.get()};
    (*box).set_edit_filter(Delegate<TextEdit&>::bind<MeddlingFilter, &MeddlingFilter::filter>(meddling));
    require(!(*box).replace_selection("x") && (*box).text.read() == "meddled",
            "an edit whose filter changed the text is abandoned");
    (*box).set_edit_filter(Delegate<TextEdit&>{});
    require((*box).replace_selection("!") && (*box).text.read() == "meddled!", "clearing the filter restores plain editing");
}

// -- MaskedTextBox ------------------------------------------------------------------

struct RecordRejection final {
    std::vector<MaskInputRejection>* seen{};
    void operator()(const MaskInputRejection& rejection) const { (*seen).push_back(rejection); }
};

struct RecordBool final {
    std::vector<bool>* seen{};
    void operator()(bool value) const { (*seen).push_back(value); }
};

struct RecordText final {
    std::vector<std::string>* seen{};
    void operator()(const std::string& text) const { (*seen).push_back(text); }
};

// Accepts a complete date whose month is 01 to 12; keeps the focus otherwise.
struct MonthValidator final {
    int runs{};
    void validate(MaskValidation& validation) {
        ++runs;
        const bool month_ok = validation.text.size() >= 2U &&
                              (validation.text.substr(0U, 2U) >= "01" && validation.text.substr(0U, 2U) <= "12");
        validation.valid = validation.mask_completed && month_ok;
        if (!validation.valid) {
            validation.message = "not a date";
            validation.cancel = true;
        }
    }
};

struct RecordValidation final {
    std::vector<bool>* seen{};
    void operator()(const MaskValidation& validation) const { (*seen).push_back(validation.valid); }
};

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<MaskedTextBox> box;
    std::shared_ptr<TextBox> other;
    std::unique_ptr<Window> window;
};

Fixture make_fixture(const char* mask) {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId("root"));
    fixture.box = make_control<MaskedTextBox>(StableId("masked"), std::string(mask));
    (*fixture.box).set_requested_bounds({0.0, 0.0, 220.0, 30.0});
    fixture.other = make_control<TextBox>(StableId("other"));
    (*fixture.other).set_requested_bounds({0.0, 40.0, 220.0, 30.0});
    (*fixture.root).add_child(fixture.box);
    (*fixture.root).add_child(fixture.other);
    fixture.window = std::make_unique<Window>(fixture.root, Size{240.0, 80.0});
    (*fixture.window).perform_layout();
    return fixture;
}

void masked_typing_and_refusal() {
    Fixture fixture = make_fixture("(000) 000-0000");
    Window& window = *fixture.window;
    MaskedTextBox& box = *fixture.box;
    std::vector<MaskInputRejection> rejected;
    std::vector<std::string> texts;
    SubscriptionToken rejections = box.mask_input_rejected().subscribe(RecordRejection{&rejected});
    SubscriptionToken changes = box.text_changed().subscribe(RecordText{&texts});
    TextBox& editor = *box.editor();
    require(editor.text.read() == "(___) ___-____" && editor.selection().caret == Utf8Offset(1U),
            "the field shows the prompts and waits at the first edit position");
    require(window.request_focus(box.editor()), "the editor takes focus");
    require(window.dispatch_text({"5"}) && window.dispatch_text({"5"}) && window.dispatch_text({"5"}),
            "digits are taken");
    require(editor.text.read() == "(555) ___-____" && editor.selection().caret == Utf8Offset(6U),
            "after the third digit the caret passes over the literals");
    require(box.text() == "(555" && texts.size() == 3U && texts.back() == "(555",
            "text follows text_mask_format and text_changed reports each change");
    require(!window.dispatch_text({"x"}) && rejected.size() == 1U &&
                rejected[0].position == MaskPosition(6U) && rejected[0].hint == MaskResultHint::digit_expected &&
                rejected[0].text == "x" && editor.text.read() == "(555) ___-____",
            "a letter is refused with its position and reason and the field is unchanged");
    require(window.dispatch_text({"1234567"}) && box.mask_completed() && box.mask_full() &&
                box.text() == "(555) 123-4567",
            "composed input of several characters is placed in turn");
    require(!window.dispatch_text({"8"}) && rejected.size() == 2U &&
                rejected[1].hint == MaskResultHint::unavailable_edit_position,
            "input past the end is refused");
    box.set_text_mask_format(MaskTextFormat::exclude_prompt_and_literals);
    require(box.text() == "5551234567" && texts.back() == "5551234567",
            "changing the text format republishes the text");
}

void masked_deletion_overwrite_and_undo() {
    Fixture fixture = make_fixture("000-000");
    Window& window = *fixture.window;
    MaskedTextBox& box = *fixture.box;
    TextBox& editor = *box.editor();
    require(box.set_text("123456").succeeded() && editor.text.read() == "123-456", "program text placed");
    require(window.request_focus(box.editor()), "the editor takes focus");
    editor.select(Utf8Offset(4U), Utf8Offset(4U));
    require(window.dispatch_key({KeyAction::down, PhysicalKey::backspace}) &&
                editor.text.read() == "123-456" && editor.selection().caret == Utf8Offset(3U),
            "Backspace over a literal moves the caret back over it");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::backspace}) &&
                editor.text.read() == "124-56_" && editor.selection().caret == Utf8Offset(2U),
            "Backspace removes the character and moves the rest back over the literal");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::delete_forward}) &&
                editor.text.read() == "125-6__",
            "Delete removes the character after the caret");
    require(editor.undo() && editor.text.read() == "124-56_" && box.text() == "124-56",
            "undo restores the field and the provider follows it");

    std::vector<bool> modes;
    SubscriptionToken mode_token = box.overwrite_mode_changed().subscribe(RecordBool{&modes});
    require(window.dispatch_key({KeyAction::down, PhysicalKey::insert}) && box.overwrite_mode() &&
                modes.size() == 1U && modes[0],
            "the Insert key toggles overwrite mode");
    editor.select(Utf8Offset(0U), Utf8Offset(0U));
    require(window.dispatch_text({"9"}) && editor.text.read() == "924-56_" &&
                editor.selection().caret == Utf8Offset(1U),
            "overwrite mode replaces the character at the caret");
    box.set_overwrite_mode(false);
    require(window.dispatch_text({"0"}) && editor.text.read() == "902-456",
            "insert mode moves the following characters on");
}

void masked_paste_and_program_text() {
    Fixture fixture = make_fixture("000-000");
    Window& window = *fixture.window;
    host::HeadlessHost host(window);
    HostServices& services = host.services();
    MaskedTextBox& box = *fixture.box;
    TextBox& editor = *box.editor();
    std::vector<MaskInputRejection> rejected;
    SubscriptionToken rejections = box.mask_input_rejected().subscribe(RecordRejection{&rejected});
    require(window.request_focus(box.editor()), "the editor takes focus");
    require(services.write_clipboard_text("1a2-3").accepted(), "the clipboard holds mixed text");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::v, Modifier::control}) &&
                editor.text.read() == "123-___" && rejected.size() == 2U && rejected[0].text == "a" &&
                rejected[0].hint == MaskResultHint::digit_expected,
            "a paste keeps the characters the mask takes and reports each one it drops");
    box.set_reject_input_on_first_failure(true);
    editor.select_all();
    require(services.write_clipboard_text("9x").accepted(), "the clipboard holds a bad character");
    static_cast<void>(window.dispatch_key({KeyAction::down, PhysicalKey::v, Modifier::control}));
    require(editor.text.read() == "123-___" && rejected.size() == 3U && rejected[2].position == MaskPosition(1U),
            "with reject_input_on_first_failure one refused character refuses the paste");

    const MaskEditResult refused = box.set_text("12x");
    require(!refused.succeeded() && editor.text.read() == "123-___" && rejected.size() == 4U,
            "refused program text leaves the box unchanged and is reported");
    require(box.set_text("123456").succeeded() && box.text() == "123-456", "program text placed");
    std::vector<std::string> masks;
    SubscriptionToken mask_token = box.mask_changed().subscribe(RecordText{&masks});
    box.set_mask("00/00/00");
    require(editor.text.read() == "12/34/56" && box.mask() == "00/00/00" && masks.size() == 1U,
            "a new mask keeps the characters it takes; / is the invariant date separator");
    box.set_mask("LL");
    require(editor.text.read() == "__" && masks.size() == 2U, "a mask that refuses the old characters starts empty");
    box.set_mask("");
    require(window.request_focus(box.editor()) && window.dispatch_text({"any text"}) &&
                box.text() == "any text",
            "an empty mask takes any text");
    box.set_prompt_character(U'#');
    box.set_mask("00");
    require(editor.text.read() == "##", "the prompt character is shown in the field");
}

void masked_validation_semantics_and_anatomy() {
    Fixture fixture = make_fixture("00/00");
    Window& window = *fixture.window;
    MaskedTextBox& box = *fixture.box;
    MonthValidator validator;
    box.set_validator(Delegate<MaskValidation&>::bind<MonthValidator, &MonthValidator::validate>(validator));
    std::vector<bool> results;
    SubscriptionToken token = box.type_validation_completed().subscribe(RecordValidation{&results});
    require(window.request_focus(box.editor()) && window.dispatch_text({"1301"}), "a date typed");
    require(!window.request_focus(fixture.other) && window.focused_control() == box.editor() &&
                results.size() == 1U && !results[0],
            "an invalid date keeps the focus when the hook asks to cancel");
    require(box.set_text("1201").succeeded() && box.validate_text() && results.size() == 2U && results[1],
            "validate_text runs the hook and reports the result");
    require(window.request_focus(fixture.other), "a valid date lets the focus go");
    box.set_validator(Delegate<MaskValidation&>{});

    const SemanticDescriptor descriptor = box.semantic_descriptor();
    require(descriptor.role == SemanticRole::text_box && descriptor.value == "12/01" &&
                !descriptor.include_descendants,
            "a masked box is one text box to accessibility, valued as shown");
    require(box.on_semantic_action(SemanticAction::set_value, "0315") && box.text() == "03/15",
            "set_value goes through the mask");
    require(!box.on_semantic_action(SemanticAction::set_value, "ab") && box.text() == "03/15",
            "set_value refuses text the mask refuses");

    const ControlAnatomy& anatomy = box.anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none &&
                anatomy.control_id == "masked_text_box",
            "MaskedTextBox publishes a valid anatomy");
    require(box.parts().field.present, "the field part is the editor");
    box.set_read_only(true);
    require(box.states().read_only && (*box.editor()).read_only.read() &&
                !box.on_semantic_action(SemanticAction::set_value, "0101"),
            "a read-only box publishes read_only and refuses set_value");
    bool threw = false;
    try {
        box.set_text_mask_format(static_cast<MaskTextFormat>(9));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "an unknown text format is refused");
}

} // namespace

int main() {
    provider_language_and_formats();
    provider_edits();
    provider_kinds_case_and_culture();
    text_box_edit_filter();
    masked_typing_and_refusal();
    masked_deletion_overwrite_and_undo();
    masked_paste_and_program_text();
    masked_validation_semantics_and_anatomy();
    std::cout << "MaskedTextBox: provider, edit filter, typing, refusals, deletion, overwrite, paste, "
                 "program text, masks, validation, semantics and anatomy passed\n";
    return 0;
}
