// RichTextBox (stage W, owner decision 2026-10-28): a markdown source edited
// with TextBox's engine and shown styled in place. The source and the plain
// text, plain text written as markdown, typing and undo, the control bar's
// span and line commands and their keys, list continuation, links and
// Ctrl+click, RTF in and out, read-only, limits, semantics and the anatomy.

#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"
#include "support/typography_painter.hpp"
#include "support/clip_tracking_painter.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
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

struct Fixture final {
    std::shared_ptr<RichTextBox> box;
    std::unique_ptr<Window> window;
};

Fixture make_fixture(std::string_view markdown) {
    Fixture fixture;
    fixture.box = make_control<RichTextBox>(StableId("rich"), markdown);
    (*fixture.box).set_requested_bounds({0.0, 0.0, 420.0, 200.0});
    fixture.window = std::make_unique<Window>(fixture.box, Size{420.0, 200.0});
    (*fixture.window).perform_layout();
    require((*fixture.window).request_focus(fixture.box), "the box takes focus");
    return fixture;
}

void select(RichTextBox& box, std::size_t anchor, std::size_t caret) {
    box.select(Utf8Offset(anchor), Utf8Offset(caret));
}

struct RecordText final {
    std::vector<std::string>* texts{};
    void operator()(const std::string& text) const { (*texts).push_back(text); }
};

struct CountChanges final {
    std::size_t* count{};
    void operator()() const { ++(*count); }
};

struct RecordLink final {
    std::vector<std::string>* targets{};
    void operator()(const std::string& target) const { (*targets).push_back(target); }
};

// -- the document -------------------------------------------------------------------

void source_and_plain_text() {
    Fixture fixture = make_fixture("## Title\n\nSome **bold** and *slanted* text.\n\n- one\n- two\n");
    RichTextBox& box = *fixture.box;
    require(box.markdown() == "## Title\n\nSome **bold** and *slanted* text.\n\n- one\n- two\n",
            "markdown() is the source");
    require(box.text.read() == "Title\nSome bold and slanted text.\none\ntwo", "text() is the plain text");
    require(box.document().blocks.size() == 4U && box.document().blocks[0].kind == MarkdownBlockKind::heading,
            "the document is the parsed source");
    require(box.selection().caret == Utf8Offset(box.markdown().size()), "the caret starts at the end");

    std::vector<std::string> texts;
    std::size_t changes = 0U;
    SubscriptionToken text_token = box.text_changed().subscribe(RecordText{&texts});
    SubscriptionToken change_token = box.content_changed().subscribe(CountChanges{&changes});
    box.set_markdown("plain");
    require(box.text.read() == "plain" && texts.size() == 1U && changes == 1U && !box.can_undo(),
            "set_markdown replaces the source, raises both events and clears the history");
    select(box, 0U, 5U);
    require(box.toggle_bold() && box.markdown() == "**plain**" && changes == 2U && texts.size() == 1U,
            "a change to the markup alone raises content_changed and not text_changed");
}

void plain_text_as_markdown() {
    Fixture fixture = make_fixture({});
    RichTextBox& box = *fixture.box;
    const std::string plain = "a*b [c](d)\n# not a heading\n\n  indented\n1. not a list";
    box.text = plain;
    require(box.text.read() == plain, "set_text writes markdown that reads back as the text");
    require(box.document().blocks.size() == 1U && box.document().blocks[0].kind == MarkdownBlockKind::paragraph,
            "escaped text is one paragraph");
    box.text = "trailing\n\n";
    require(box.text.read() == "trailing", "trailing line feeds are dropped");
}

void limits() {
    Fixture fixture = make_fixture("kept");
    RichTextBox& box = *fixture.box;
    bool threw = false;
    try {
        box.set_markdown(std::string("\xFF", 1U));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && box.markdown() == "kept", "invalid UTF-8 is refused and changes nothing");
    threw = false;
    try {
        box.set_markdown(std::string(RichTextBox::maximum_bytes + 1U, 'x'));
    } catch (const std::length_error&) {
        threw = true;
    }
    require(threw && box.markdown() == "kept", "a source past maximum_bytes is refused");
    const std::string long_line(8000U, 'y');
    box.set_markdown(long_line);
    require(box.markdown().size() == 8000U, "a paragraph is one line of any length");
    box.set_markdown(std::string(RichTextBox::maximum_bytes, 'z'));
    require(!(*fixture.window).dispatch_text({"!"}) && box.markdown().size() == RichTextBox::maximum_bytes,
            "typing past maximum_bytes is refused");
}

// -- editing ------------------------------------------------------------------------

void typing_and_undo() {
    Fixture fixture = make_fixture("Hello");
    Window& window = *fixture.window;
    RichTextBox& box = *fixture.box;
    require(window.dispatch_text({" **you**"}) && box.markdown() == "Hello **you**" && box.text.read() == "Hello you",
            "typed markup is source");
    require(box.undo() && box.markdown() == "Hello", "undo restores the source");
    require(box.redo() && box.markdown() == "Hello **you**", "redo applies it again");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::backspace}) && box.markdown() == "Hello **you*",
            "Backspace deletes one character of the source");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::z, Modifier::control}) &&
                box.markdown() == "Hello **you**",
            "Ctrl+Z undoes");
}

void span_commands() {
    Fixture fixture = make_fixture("make this bold");
    Window& window = *fixture.window;
    RichTextBox& box = *fixture.box;
    select(box, 10U, 14U);
    require(box.toggle_bold() && box.markdown() == "make this **bold**", "bold wraps the selection");
    require(box.selection().start() == Utf8Offset(12U) && box.selection().end() == Utf8Offset(16U),
            "the selection stays on the text");
    require(box.selection_format().bold == RichTextFormatState::on, "the selection is bold");
    require(box.toggle_bold() && box.markdown() == "make this bold" &&
                box.selection_format().bold == RichTextFormatState::off,
            "bold again removes the delimiters");
    select(box, 5U, 9U);
    require(window.dispatch_key({KeyAction::down, PhysicalKey::i, Modifier::control}) &&
                box.markdown() == "make *this* bold" &&
                box.selection_format().italic == RichTextFormatState::on,
            "Ctrl+I makes the selection italic");
    select(box, 5U, 11U);
    require(box.toggle_italic() && box.markdown() == "make this bold",
            "a selection holding its delimiters is unwrapped");
    select(box, 10U, 14U);
    require(box.toggle_code() && box.markdown() == "make this `bold`" &&
                box.selection_format().code == RichTextFormatState::on,
            "code wraps in backticks");
    select(box, 0U, 0U);
    require(box.toggle_bold() && box.markdown() == "****make this `bold`" && box.selection().caret == Utf8Offset(2U),
            "with no selection a pair is inserted around the caret");
    require(window.dispatch_text({"x"}) && box.markdown() == "**x**make this `bold`",
            "typing between the pair makes bold text");
    select(box, 0U, 0U);
    select(box, 2U, 2U);
    require(box.undo() && box.undo() && box.markdown() == "make this `bold`", "each command is one history step");

    box.set_markdown("one\ntwo");
    select(box, 0U, 7U);
    require(box.toggle_bold() && box.markdown() == "**one**\n**two**", "each line's text is wrapped on its own");

    box.set_markdown("plain **bold**");
    select(box, 0U, 11U);
    require(box.selection_format().bold == RichTextFormatState::mixed,
            "a selection over plain and bold text is mixed");
    require(box.realized_items()[0].states.indeterminate && !box.realized_items()[0].states.checked,
            "the Bold button shows mixed, not checked");
    require(has_semantic_state(box.semantic_virtual_children()[0].states, SemanticState::mixed),
            "and publishes the mixed state");
    select(box, 9U, 9U);
    require(box.selection_format().bold == RichTextFormatState::on && box.realized_items()[0].states.checked &&
                !box.realized_items()[0].states.indeterminate,
            "the caret in the bold run shows Bold on");
}

void line_commands() {
    Fixture fixture = make_fixture("Title\nbody");
    RichTextBox& box = *fixture.box;
    select(box, 2U, 2U);
    require(box.set_heading_level(2U) && box.markdown() == "## Title\nbody", "a heading marker is written");
    require(box.selection().caret == Utf8Offset(5U), "the caret stays in the text");
    require(box.selection_format().heading == RichTextFormatState::on && box.selection_format().heading_level == 2U,
            "the line is a level 2 heading");
    require(box.set_heading_level(1U) && box.markdown() == "# Title\nbody", "the level changes");
    require(box.set_heading_level(0U) && box.markdown() == "Title\nbody", "level 0 makes body text");
    bool threw = false;
    try {
        static_cast<void>(box.set_heading_level(7U));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a heading level past 6 is refused");

    box.set_markdown("alpha\n\nbeta\ngamma");
    box.select_all();
    require(box.toggle_numbered_list() && box.markdown() == "1. alpha\n\n2. beta\n3. gamma",
            "numbered markers are written on the non-blank lines");
    require(box.selection_format().numbered == RichTextFormatState::on, "the lines are numbered");
    require(box.toggle_bulleted_list() && box.markdown() == "- alpha\n\n- beta\n- gamma",
            "bullets replace numbers");
    require(box.toggle_bulleted_list() && box.markdown() == "alpha\n\nbeta\ngamma",
            "bullets again remove the markers");
    box.set_markdown("# Title\nbody\n## Sub");
    box.select_all();
    require(box.selection_format().heading == RichTextFormatState::mixed &&
                box.selection_format().heading_level == 0U,
            "a heading beside body text is mixed");
    require(box.realized_items()[2].states.indeterminate && box.semantic_virtual_children()[2].value == "Mixed",
            "the Heading button shows and publishes mixed");
    box.set_markdown("- a\nb");
    box.select_all();
    require(box.selection_format().bulleted == RichTextFormatState::mixed, "one bulleted line of two is mixed");
}

void list_continuation() {
    Fixture fixture = make_fixture("- first");
    Window& window = *fixture.window;
    RichTextBox& box = *fixture.box;
    require(window.dispatch_key({KeyAction::down, PhysicalKey::enter}) && box.markdown() == "- first\n- ",
            "Enter at the end of an item starts the next");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::enter}) && box.markdown() == "- first\n",
            "Enter on an empty item ends the list");
    box.set_markdown("3. three");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::enter}) && box.markdown() == "3. three\n4. ",
            "a numbered item continues with the next number");
    box.set_markdown("> quoted");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::enter}) && box.markdown() == "> quoted\n> ",
            "a quote continues");
    box.set_markdown("- item");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::enter, Modifier::shift}) &&
                box.markdown() == "- item\n",
            "Shift+Enter is a plain line feed");
}

// A document of several paragraphs, typed with Enter: a heading, a
// paragraph that wraps, a list, and enough pasted text to scroll.
void multi_paragraph_document() {
    Fixture fixture = make_fixture({});
    Window& window = *fixture.window;
    host::HeadlessHost host(window);
    RichTextBox& box = *fixture.box;
    const KeyEvent enter{KeyAction::down, PhysicalKey::enter};
    const std::string paragraph =
        "This paragraph is long enough to wrap across several rows of a box four hundred "
        "points wide, so the source line it is shows as more than one visual line.";
    require(window.dispatch_text({"# Notes"}) && window.dispatch_key(enter) && window.dispatch_key(enter) &&
                window.dispatch_text({paragraph}) && window.dispatch_key(enter) && window.dispatch_key(enter) &&
                window.dispatch_text({"- one"}) && window.dispatch_key(enter) && window.dispatch_text({"two"}),
            "a document is typed with Enter between its lines");
    const std::string typed = "# Notes\n\n" + paragraph + "\n\n- one\n- two";
    require(box.markdown() == typed, "Enter starts a new source line; in the list it continues the list");
    require(box.document().blocks.size() == 4U && box.document().blocks[0].kind == MarkdownBlockKind::heading &&
                box.document().blocks[1].kind == MarkdownBlockKind::paragraph &&
                box.document().blocks[2].kind == MarkdownBlockKind::list_item &&
                box.document().blocks[3].kind == MarkdownBlockKind::list_item,
            "the lines parse as a heading, a paragraph and two items");
    require(box.text.read() == "Notes\n" + paragraph + "\none\ntwo", "the plain text has one line per block");
    window.perform_layout();
    require(box.visual_line_count() >= 8U, "the long paragraph wraps onto more than one row");
    SemanticTextLayout layout{};
    require(box.on_semantic_text_layout(layout) == SemanticTextResult::done && layout.lines.size() >= 8U,
            "the layout publishes every visual line");

    const KeyEvent up{KeyAction::down, PhysicalKey::up};
    require(window.dispatch_key(up) && window.dispatch_key(up) && window.dispatch_key(up) &&
                box.selection().caret.value() > 9U + paragraph.size() / 2U &&
                box.selection().caret.value() <= 9U + paragraph.size(),
            "Up from the list passes the blank line into the paragraph's last row");
    require(window.dispatch_key(up) && box.selection().caret.value() > 9U &&
                box.selection().caret.value() < 9U + paragraph.size() - 20U,
            "Up again stays in the paragraph, one row higher");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::end, Modifier::control}) &&
                box.selection().caret == Utf8Offset(typed.size()),
            "Ctrl+End goes to the end of the document");

    std::string more;
    for (std::size_t index = 0U; index < 30U; ++index) more += "\n\nAnother paragraph, number " + std::to_string(index) + ".";
    require(host.services().write_clipboard_text(more).accepted(), "the clipboard holds many paragraphs");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::v, Modifier::control}) &&
                box.markdown() == typed + more && box.document().blocks.size() == 34U,
            "pasted line feeds stay line feeds: thirty more paragraphs");
    // As TextBox's, the caret is revealed when the box paints.
    test_support::TypographyPainter painter{};
    window.perform_layout();
    box.on_paint(painter, {0.0, 0.0, 420.0, 200.0});
    require(box.scroll_offset().y > 0.0, "the caret at the end of a long document scrolls it into view");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::home, Modifier::control}) &&
                box.selection().caret == Utf8Offset(0U),
            "Ctrl+Home goes to the start");
    window.perform_layout();
    box.on_paint(painter, {0.0, 0.0, 420.0, 200.0});
    require(box.scroll_offset().y == 0.0, "and scrolls back to the top");
    require(box.undo() && box.markdown() == typed, "the paste is one undo step");
}

// -- links --------------------------------------------------------------------------

void links() {
    Fixture fixture = make_fixture("see site");
    Window& window = *fixture.window;
    RichTextBox& box = *fixture.box;
    select(box, 4U, 8U);
    require(window.dispatch_key({KeyAction::down, PhysicalKey::k, Modifier::control}) &&
                box.markdown() == "see [site]()" && box.selection().caret == Utf8Offset(11U),
            "Ctrl+K makes a link and puts the caret where the destination goes");
    require(window.dispatch_text({"https://example.org"}) && box.markdown() == "see [site](https://example.org)",
            "the destination is typed");
    require(box.document().links.size() == 1U && box.document().links[0].target == "https://example.org",
            "the document holds the link");
    require(box.text.read() == "see site", "the link's markup is not plain text");
    select(box, 6U, 6U);
    require(box.selection_format().link == RichTextFormatState::on, "the caret is in the link's text");

    std::vector<std::string> targets;
    SubscriptionToken token = box.link_clicked().subscribe(RecordLink{&targets});
    window.perform_layout();
    SemanticTextLayout layout{};
    require(box.on_semantic_text_layout(layout) == SemanticTextResult::done, "the layout is published");
    Rect site{};
    for (const SemanticTextGrapheme& grapheme : layout.graphemes) {
        if (grapheme.range.start == Utf8Offset(6U)) site = grapheme.bounds;
    }
    require(site.width > 0.0, "the link's text is laid out");
    const Point inside{site.x + site.width * 0.5, site.y + site.height * 0.5};
    PointerEvent plain{.action = PointerAction::down, .button = PointerButton::primary, .position = inside};
    static_cast<void>(window.dispatch_pointer(plain));
    PointerEvent release{.action = PointerAction::up, .button = PointerButton::primary, .position = inside};
    static_cast<void>(window.dispatch_pointer(release));
    require(targets.empty() && box.selection().empty(), "a plain click places the caret");
    PointerEvent command{.action = PointerAction::down, .button = PointerButton::primary, .position = inside,
                         .modifiers = Modifier::control};
    static_cast<void>(window.dispatch_pointer(command));
    require(targets.size() == 1U && targets[0] == "https://example.org", "Ctrl+click opens the link");
}

// -- the control bar ----------------------------------------------------------------

void control_bar() {
    Fixture fixture = make_fixture("word");
    Window& window = *fixture.window;
    RichTextBox& box = *fixture.box;
    const std::vector<RichTextBox::Item> items = box.realized_items();
    require(items.size() == rich_text_bar_button_count && items[0].stable_id == "rich.bar.bold" &&
                items[2].button == RichTextBarButton::heading && items[2].parts.button_glyph.present &&
                !items[0].parts.button_glyph.present,
            "the bar's buttons are realized items");
    const RichTextBox::Parts parts = box.parts();
    require(parts.bar.present && parts.content.bounds.y >= parts.bar.bounds.y + parts.bar.bounds.height,
            "the text sits below the bar");
    box.select_all();
    const Rect bold = items[0].parts.button.bounds;
    const Point centre{bold.x + bold.width * 0.5, bold.y + bold.height * 0.5};
    PointerEvent press{.action = PointerAction::down, .button = PointerButton::primary, .position = centre};
    PointerEvent release{.action = PointerAction::up, .button = PointerButton::primary, .position = centre};
    static_cast<void>(window.dispatch_pointer(press));
    require(box.realized_items()[0].states.pressed, "the pressed button shows pressed");
    static_cast<void>(window.dispatch_pointer(release));
    require(box.markdown() == "**word**" && box.realized_items()[0].states.checked,
            "a click on Bold makes the selection bold and the button checked");
    require(box.on_semantic_child_action("rich.bar.code", SemanticAction::press, {}) &&
                box.markdown() == "**`word`**",
            "the semantic press runs the button");
    const std::vector<SemanticNode> nodes = box.semantic_virtual_children();
    require(nodes.size() == rich_text_bar_button_count && nodes[0].role == SemanticRole::button &&
                has_semantic_state(nodes[0].states, SemanticState::checked) &&
                nodes[2].role == SemanticRole::menu_button && nodes[2].has_popup,
            "the buttons are published as buttons and a menu button");
    box.control_bar_visible = false;
    window.perform_layout();
    require(box.realized_item_count() == 0U && !box.parts().bar.present, "a hidden bar publishes nothing");
}

void read_only() {
    Fixture fixture = make_fixture("fixed");
    Window& window = *fixture.window;
    RichTextBox& box = *fixture.box;
    box.read_only = true;
    box.select_all();
    require(!box.toggle_bold() && !box.set_heading_level(1U) && !box.toggle_bulleted_list() && !box.insert_link(),
            "read-only refuses the commands");
    require(!window.dispatch_text({"x"}) && box.markdown() == "fixed", "read-only refuses typing");
    require(box.realized_items()[0].states.disabled, "the bar's buttons are disabled");
}

// -- RTF ----------------------------------------------------------------------------

void rtf_round_trip() {
    Fixture fixture = make_fixture("# Notes\n\nSome **bold** text.\n");
    RichTextBox& box = *fixture.box;
    std::vector<std::byte> rtf;
    require(box.rtf(rtf).succeeded() && !rtf.empty(), "the document is written as RTF");
    Fixture other = make_fixture({});
    const RtfConversionResult loaded = (*other.box).set_rtf(rtf);
    require(loaded.succeeded() && (*other.box).text.read() == box.text.read() && !(*other.box).can_undo(),
            "RTF read back gives the same text and clears the history");
    const std::string before((*other.box).markdown());
    const std::string junk = "not rtf";
    const std::vector<std::byte> bytes(reinterpret_cast<const std::byte*>(junk.data()),
                                       reinterpret_cast<const std::byte*>(junk.data()) + junk.size());
    require(!(*other.box).set_rtf(bytes).succeeded() && (*other.box).markdown() == before,
            "a failed import leaves the box as it was");
}

// -- painting -----------------------------------------------------------------------

// A selected row draws each glyph once, in three clips that do not overlap.
void selected_text_drawn_once() {
    Fixture fixture = make_fixture("plain **bold** text");
    RichTextBox& box = *fixture.box;
    select(box, 2U, 10U);
    test_support::ClipTrackingPainter painter{};
    box.on_paint(painter, {0.0, 0.0, 420.0, 200.0});
    require(painter.count("bold") == 3U && !painter.overdraws(),
            "a selected row draws each piece once in each of three disjoint clips");
}

// -- semantics ----------------------------------------------------------------------

void semantics() {
    Fixture fixture = make_fixture("**a**");
    RichTextBox& box = *fixture.box;
    const SemanticDescriptor descriptor = box.semantic_descriptor();
    require(descriptor.role == SemanticRole::text_box && descriptor.value == "**a**" && descriptor.multiline,
            "the box publishes its source as a multiline text box");
    require(box.on_semantic_action(SemanticAction::set_value, "# b") && box.markdown() == "# b" &&
                box.text.read() == "b",
            "set_value replaces the source");
}

} // namespace

// RichTextBox's properties are capabilities: text is a view onto the parsed
// document, read_only onto the engine, and the rest cells whose hooks keep
// the old setters' effects.
void property_capabilities() {
    Fixture fixture = make_fixture("**Draft**");
    RichTextBox& box = *fixture.box;
    std::vector<std::string> texts;
    const SubscriptionToken text_token = box.text_changed().subscribe(RecordText{&texts});
    box.text = "Plain *text*";
    require(box.text.read() == "Plain *text*" && box.markdown() == "Plain \\*text\\*" && texts.size() == 1U &&
                texts[0] == "Plain *text*",
            "a text write escapes the markup and raises text_changed");
    box.set_property_value("text", BindingValue{std::string("From the table")});
    require(box.text.read() == "From the table" && texts.size() == 2U, "a table write runs the same hooks");
    const std::optional<PropertyDescriptor> text_descriptor = box.property_descriptor("text");
    require(text_descriptor && (*text_descriptor).change_notifications && (*text_descriptor).bindable,
            "RichTextBox text is bindable and reports change notifications");
    bool refused = false;
    try {
        box.text = std::string("\xFF");
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && box.text.read() == "From the table", "invalid UTF-8 text is refused");
    static_cast<void>(text_token);

    box.read_only = true;
    require(box.read_only.read() && box.states().read_only, "read_only is a view onto the engine");
    box.read_only = false;
    box.word_wrap = false;
    require(!box.word_wrap.read(), "word_wrap is a cell");
    box.accepts_tab = true;
    require(box.accepts_tab.read(), "accepts_tab is a cell");
    box.control_bar_visible = false;
    (*fixture.window).perform_layout();
    require(!box.control_bar_visible.read() && !box.parts().bar.present, "hiding the bar removes it");
    refused = false;
    try {
        box.font = FontSpec{FontRole::content, 0.0, 400, false};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && box.font.read() == FontSpec{FontRole::content, 12.0, 400, false}, "an invalid font is refused");
    box.font = FontSpec{FontRole::content, 14.0, 400, false};
    require(box.font.read().size == 14.0, "a valid font is stored");
}

namespace {

// -- the caret ----------------------------------------------------------------------

// As TextBox's, the caret blink rests while the window is occluded: no wake
// and no tick from the transition on, and it blinks again once shown.
void occluded_caret_rests() {
    Fixture fixture = make_fixture("Some text");
    Window& window = *fixture.window;
    require(window.next_wake().has_value(), "a focused box blinks");
    window.set_occluded(true, FrameClock::now());
    require(!window.next_wake().has_value(), "an occluded box's caret holds no wake");
    const FramePollResult hidden = window.poll_frame_schedule(FrameClock::now() + std::chrono::seconds(5));
    require(hidden.ui_timer_ticks == 0U, "an occluded box's caret does not tick");
    const FrameTime shown = FrameClock::now();
    window.set_occluded(false, shown);
    const std::optional<FrameTime> resumed = window.next_wake();
    require(resumed.has_value() && *resumed >= shown, "a shown box's caret blinks again");
}

} // namespace

int main() {
    source_and_plain_text();
    plain_text_as_markdown();
    limits();
    typing_and_undo();
    span_commands();
    line_commands();
    list_continuation();
    multi_paragraph_document();
    links();
    control_bar();
    read_only();
    rtf_round_trip();
    selected_text_drawn_once();
    semantics();
    property_capabilities();
    occluded_caret_rests();
    std::cout << "rich_text_box_tests passed\n";
    return 0;
}
