// Styled text runs: the TextDecorations collection (replace, replace_range,
// clear, edit shifting, composition, text_change_between) and a multiline
// TextBox drawing them: inks, grounds, underlines and squiggles; edits,
// undo and assignment carrying decorations along; weight changing the
// layout the caret, hit testing and accessibility read; a colour change
// damaging only its rows; the selection drawing each glyph once over
// decorations.

#include "gui_forms/input_controls.hpp"
#include "gui_forms/window.hpp"
#include "headless_host.hpp"
#include "support/clip_tracking_painter.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

constexpr Color red = Color::rgba(200, 0, 0);
constexpr Color yellow = Color::rgba(250, 240, 100);
constexpr Color blue = Color::rgba(0, 0, 220);

TextDecoration decoration(std::size_t start, std::size_t end, TextDecorationStyle style, std::uint32_t tag = 0U) {
    const TextDecoration result{.range = Utf8Range{Utf8Offset(start), Utf8Offset(end)}, .style = style, .tag = tag};
    return result;
}

TextDecorationStyle coloured(Color colour) {
    TextDecorationStyle style{};
    style.foreground = colour;
    return style;
}

TextDecorationStyle bold() {
    TextDecorationStyle style{};
    style.weight = static_cast<std::uint16_t>(700U);
    return style;
}

Utf8Range bytes(std::size_t start, std::size_t end) {
    const Utf8Range range{Utf8Offset(start), Utf8Offset(end)};
    return range;
}

// Advances of ten units, twelve for bold: weight is a layout input.
class Metrics final : public TextMetricsProvider {
public:
    ResolvedTextLayout resolve_text_layout_utf8(std::string_view text, FontSpec font) override {
        ResolvedTextLayout result = estimate_text_layout_utf8(text, font);
        const TextStore store(text);
        const double advance = font.weight >= 600U ? 12.0 : 10.0;
        result.logical_size = {static_cast<double>(store.grapheme_count().value()) * advance, 20.0};
        result.ascent = 15.0;
        result.descent = 5.0;
        result.line_gap = 0.0;
        return result;
    }
};

struct PaintedText final {
    std::string text{};
    Point origin{};
    FontSpec font{};
    Color colour{};
};
struct PaintedLine final {
    Point from{};
    Point to{};
    Color colour{};
};
struct PaintedFill final {
    Rect bounds{};
    Color colour{};
};

class RecordingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect bounds, Color colour) override { fills.push_back({bounds, colour}); }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point from, Point to, Color colour, double) override { lines.push_back({from, to, colour}); }
    void draw_image(ImageId, Rect, double) override {}
    void draw_text_utf8(Point origin, std::string_view text, FontSpec font, Color colour) override {
        texts.push_back({std::string(text), origin, font, colour});
    }
    [[nodiscard]] std::size_t lines_in(Color colour) const {
        std::size_t count = 0U;
        for (const PaintedLine& line : lines) {
            if (line.colour == colour) ++count;
        }
        return count;
    }
    std::vector<PaintedText> texts{};
    std::vector<PaintedLine> lines{};
    std::vector<PaintedFill> fills{};
};

struct Fixture {
    std::shared_ptr<TextBox> field = make_control<TextBox>(StableId("decorated"));
    TextBox& editor{*field};
    Metrics metrics{};
    // Window borrows metrics; declaration order destroys it before the provider.
    Window window{field, {200.0, 88.0}};
    Fixture() {
        editor.set_requested_bounds({0, 0, 200, 88});
        editor.multiline = true;
        window.set_text_metrics_provider(&metrics);
        PresentationSettings settings = window.presentation_settings();
        settings.reduced_motion = true;
        window.set_presentation_settings(settings);
        window.perform_layout();
        const bool focused = window.request_focus(field);
        require(focused, "focus");
    }
    void paint(Painter& painter) {
        window.perform_layout();
        const DamageRegion damage = window.take_damage();
        const std::optional<PaintReceipt> receipt = window.paint(painter, {0, 0, 200, 88});
        static_cast<void>(damage);
        require(receipt.has_value(), "the fixture paints");
    }
};

void collection_replace_and_validation() {
    TextDecorations decorations{};
    const TextDecorationLayer syntax{1U};
    const std::array<TextDecoration, 3> given{
        decoration(5, 8, coloured(red), 2U),
        decoration(4, 4, coloured(blue)),  // empty: dropped
        decoration(0, 3, coloured(blue), 1U),
    };
    const TextDecorationChange change = decorations.replace(syntax, given);
    require(change.changed && !change.layout && change.range == bytes(0, 8), "replace reports its extent");
    const std::span<const TextDecoration> items = decorations.items(syntax);
    require(items.size() == 2U && items[0].tag == 1U && items[1].tag == 2U, "replace keeps start order and drops empties");
    const TextDecorationChange again = decorations.replace(syntax, given);
    require(!again.changed, "replacing with the same decorations changes nothing");

    const std::array<TextDecoration, 2> backwards{decoration(1, 2, coloured(red)), decoration(6, 3, coloured(red))};
    bool refused = false;
    try {
        static_cast<void>(decorations.replace(syntax, backwards));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && decorations.items(syntax).size() == 2U, "a backwards range is refused before any change");
    TextDecorationStyle weightless{};
    weightless.weight = static_cast<std::uint16_t>(0U);
    const std::array<TextDecoration, 1> bad_weight{decoration(0, 1, weightless)};
    refused = false;
    try {
        static_cast<void>(decorations.replace(syntax, bad_weight));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a weight outside 1 to 1000 is refused");

    const std::array<TextDecoration, 1> heavy{decoration(2, 4, bold())};
    const std::uint64_t revision = decorations.layout_revision();
    const TextDecorationChange font_change = decorations.replace(TextDecorationLayer{0U}, heavy);
    require(font_change.layout && decorations.changes_font() && decorations.layout_revision() == revision + 1U,
            "a weight is a layout change");
    require(decorations.layer_count() == 2U && decorations.layer_at(0U) == TextDecorationLayer{0U} &&
            decorations.layer_at(1U) == syntax, "layers are kept in ascending order");
    const TextDecorationChange cleared = decorations.clear(TextDecorationLayer{0U});
    require(cleared.changed && cleared.layout && !decorations.changes_font() && decorations.layer_count() == 1U,
            "clearing a layer removes it");
    const TextDecorationChange all = decorations.clear_all();
    require(all.changed && all.range == bytes(0, 8) && decorations.empty(), "clear_all empties every layer");
}

void collection_replace_range() {
    TextDecorations decorations{};
    const TextDecorationLayer layer{0U};
    const std::array<TextDecoration, 3> initial{
        decoration(0, 10, coloured(red), 1U),
        decoration(12, 14, coloured(blue), 2U),
        decoration(20, 30, coloured(blue), 3U),
    };
    static_cast<void>(decorations.replace(layer, initial));
    const std::array<TextDecoration, 1> outside{decoration(4, 16, coloured(red))};
    bool refused = false;
    try {
        static_cast<void>(decorations.replace_range(layer, bytes(5, 15), outside));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && decorations.items(layer).size() == 3U, "a replacement outside the range is refused");

    const std::array<TextDecoration, 1> inside{decoration(6, 8, coloured(yellow), 9U)};
    const TextDecorationChange change = decorations.replace_range(layer, bytes(5, 15), inside);
    require(change.changed && change.range == bytes(5, 15), "the change is the replaced range");
    const std::span<const TextDecoration> items = decorations.items(layer);
    require(items.size() == 3U && items[0].range == bytes(0, 5) && items[0].tag == 1U &&
            items[1].range == bytes(6, 8) && items[1].tag == 9U && items[2].range == bytes(20, 30),
            "a crossing decoration keeps its outer part; those inside go");

    const TextDecorationChange same = decorations.replace_range(layer, bytes(5, 15), inside);
    require(!same.changed, "a range that already looks that way does not change");

    // A decoration spanning the whole range splits in two around it.
    const std::array<TextDecoration, 1> wide{decoration(20, 30, coloured(blue), 3U)};
    static_cast<void>(decorations.replace(layer, wide));
    const std::span<const TextDecoration> none{};
    static_cast<void>(decorations.replace_range(layer, bytes(24, 26), none));
    const std::span<const TextDecoration> split = decorations.items(layer);
    require(split.size() == 2U && split[0].range == bytes(20, 24) && split[1].range == bytes(26, 30),
            "a decoration over the range splits around it");
}

void collection_shift_for_edit() {
    TextDecorations decorations{};
    const TextDecorationLayer layer{0U};
    const std::array<TextDecoration, 3> initial{
        decoration(2, 5, coloured(red), 1U),
        decoration(8, 10, coloured(red), 2U),
        decoration(12, 14, coloured(red), 3U),
    };
    static_cast<void>(decorations.replace(layer, initial));
    // Insert three bytes at 2: the first decoration's start does not take
    // them in, so it moves.
    decorations.shift_for_edit(TextChange{.start = Utf8Offset(2), .removed_bytes = 0U, .inserted_bytes = 3U});
    std::span<const TextDecoration> items = decorations.items(layer);
    require(items[0].range == bytes(5, 8) && items[1].range == bytes(11, 13) && items[2].range == bytes(15, 17),
            "an insertion before a range shifts it");
    // Insert at the first decoration's end: it grows.
    decorations.shift_for_edit(TextChange{.start = Utf8Offset(8), .removed_bytes = 0U, .inserted_bytes = 2U});
    items = decorations.items(layer);
    require(items[0].range == bytes(5, 10) && items[1].range == bytes(13, 15), "an insertion at a range's end joins it");
    // Delete [12, 18): removes the second decoration entirely and cuts the
    // third's start.
    decorations.shift_for_edit(TextChange{.start = Utf8Offset(12), .removed_bytes = 6U, .inserted_bytes = 0U});
    items = decorations.items(layer);
    require(items.size() == 2U && items[0].range == bytes(5, 10) && items[1].range == bytes(12, 13) &&
            items[1].tag == 3U, "a deletion drops what it removes and keeps outer bytes");
    // Replace the inside of the first: it takes the replacement in.
    decorations.shift_for_edit(TextChange{.start = Utf8Offset(6), .removed_bytes = 2U, .inserted_bytes = 5U});
    items = decorations.items(layer);
    require(items[0].range == bytes(5, 13) && items[1].range == bytes(15, 16), "a replacement inside a range stays in it");
    decorations.shift_for_edit(TextChange{.start = Utf8Offset(0), .removed_bytes = 20U, .inserted_bytes = 0U});
    require(decorations.empty(), "a layer whose decorations all go is removed");
}

void collection_compose() {
    TextDecorations decorations{};
    TextDecorationStyle under{};
    under.underline = TextUnderline::squiggle;
    under.underline_color = blue;
    const std::array<TextDecoration, 2> syntax{decoration(0, 6, coloured(red)), decoration(8, 10, coloured(blue))};
    const std::array<TextDecoration, 1> diagnostics{decoration(4, 9, under)};
    static_cast<void>(decorations.replace(TextDecorationLayer{0U}, syntax));
    static_cast<void>(decorations.replace(TextDecorationLayer{5U}, diagnostics));
    std::vector<StyledTextRun> runs{};
    decorations.compose(bytes(2, 12), runs);
    require(runs.size() == 6U, "runs cover the window at every change of style");
    require(runs[0].range == bytes(2, 4) && runs[0].style == coloured(red), "the first run keeps the syntax colour");
    TextDecorationStyle both = coloured(red);
    both.underline = TextUnderline::squiggle;
    both.underline_color = blue;
    require(runs[1].range == bytes(4, 6) && runs[1].style == both, "a later layer's fields join an earlier one's");
    require(runs[2].range == bytes(6, 8) && runs[2].style == under, "a gap in one layer shows the other alone");
    TextDecorationStyle blue_under = coloured(blue);
    blue_under.underline = TextUnderline::squiggle;
    blue_under.underline_color = blue;
    require(runs[3].range == bytes(8, 9) && runs[3].style == blue_under, "the overlap composes");
    require(runs[4].range == bytes(9, 10) && runs[4].style == coloured(blue), "the syntax run continues");
    require(runs[5].range == bytes(10, 12) && runs[5].style.empty(), "undecorated text has an empty style");
    // A later layer's field stands over an earlier one's.
    const std::array<TextDecoration, 1> over{decoration(0, 12, coloured(yellow))};
    static_cast<void>(decorations.replace(TextDecorationLayer{9U}, over));
    decorations.compose(bytes(0, 4), runs);
    require(runs.size() == 1U && runs[0].style == coloured(yellow), "the later layer's colour stands");
    decorations.compose(bytes(3, 3), runs);
    require(runs.empty(), "an empty window gives no runs");
}

void change_between_texts() {
    const TextChange typed = text_change_between("let x", "let yx");
    require(typed == TextChange{.start = Utf8Offset(4), .removed_bytes = 0U, .inserted_bytes = 1U}, "an insertion");
    const TextChange erased = text_change_between("aaaa", "aa");
    require(erased.start.value() + erased.removed_bytes <= 4U && erased.removed_bytes == 2U &&
            erased.inserted_bytes == 0U, "a deletion inside a run of equal bytes");
    const TextChange same = text_change_between("same", "same");
    require(same.start.value() == 4U && same.removed_bytes == 0U && same.inserted_bytes == 0U,
            "equal texts give an empty change at the end");
    const TextChange whole = text_change_between("abc", "xyz");
    require(whole.start.value() == 0U && whole.removed_bytes == 3U && whole.inserted_bytes == 3U, "a whole replacement");
}

void box_paints_styled_runs() {
    Fixture f{};
    f.editor.text = "let x = 1;";
    TextDecorationStyle ground{};
    ground.background = yellow;
    TextDecorationStyle squiggle{};
    squiggle.underline = TextUnderline::squiggle;
    squiggle.underline_color = blue;
    const std::array<TextDecoration, 2> syntax{decoration(0, 3, coloured(red)), decoration(8, 9, ground)};
    const std::array<TextDecoration, 1> errors{decoration(4, 5, squiggle)};
    f.editor.set_decorations(TextDecorationLayer{0U}, syntax);
    f.editor.set_decorations(TextDecorationLayer{1U}, errors);
    require(f.editor.text.read() == "let x = 1;", "decorations never change the text");
    RecordingPainter painter{};
    f.paint(painter);
    bool red_text = false;
    bool plain_text = false;
    for (const PaintedText& text : painter.texts) {
        if (text.text == "let x = 1;" && text.colour == red) red_text = true;
        if (text.text == "let x = 1;" && !(text.colour == red)) plain_text = true;
    }
    require(red_text && plain_text, "the decorated run draws in its colour and the rest in the editor's ink");
    bool ground_drawn = false;
    for (const PaintedFill& fill : painter.fills) {
        if (fill.colour == yellow && fill.bounds.x == 6.0 + 80.0 && fill.bounds.width == 10.0) ground_drawn = true;
    }
    require(ground_drawn, "a ground fills exactly its run");
    require(painter.lines_in(blue) >= 4U, "a squiggle draws its wave in the underline colour");
    for (const PaintedLine& line : painter.lines) {
        if (!(line.colour == blue)) continue;
        require(line.from.x >= 6.0 + 40.0 && line.to.x <= 6.0 + 50.0, "the squiggle stays under its run");
    }

    TextDecorationStyle themed{};
    themed.ink = TextInkRole::link;
    const std::array<TextDecoration, 1> link{decoration(0, 3, themed)};
    f.editor.set_decorations(TextDecorationLayer{0U}, link);
    RecordingPainter themed_painter{};
    f.paint(themed_painter);
    std::optional<Color> link_colour{};
    std::optional<Color> text_colour{};
    for (std::size_t index = 0U; index < themed_painter.texts.size(); ++index) {
        if (index == 0U) link_colour = themed_painter.texts[index].colour;
        if (index + 1U == themed_painter.texts.size()) text_colour = themed_painter.texts[index].colour;
    }
    require(link_colour.has_value() && text_colour.has_value() && !(*link_colour == *text_colour),
            "a theme ink draws in the theme's link colour, not the editor's");
}

void box_follows_edits() {
    Fixture f{};
    f.editor.text = "alpha beta";
    const std::array<TextDecoration, 1> beta{decoration(6, 10, coloured(red), 7U)};
    f.editor.set_decorations(TextDecorationLayer{0U}, beta);
    f.editor.select(Utf8Offset(0), Utf8Offset(0));
    const bool typed = f.editor.replace_selection("an ");
    require(typed && f.editor.decorations().items(TextDecorationLayer{0U})[0].range == bytes(9, 13),
            "typing before a decoration moves it");
    const bool undone = f.editor.undo();
    require(undone && f.editor.decorations().items(TextDecorationLayer{0U})[0].range == bytes(6, 10),
            "undo carries the decoration back");
    const bool redone = f.editor.redo();
    require(redone && f.editor.decorations().items(TextDecorationLayer{0U})[0].range == bytes(9, 13),
            "redo carries it forward again");
    f.editor.text = "an alpha";
    require(f.editor.decorations().items(TextDecorationLayer{0U}).empty(),
            "assigning text that removes the decorated bytes drops the decoration");

    const std::array<TextDecoration, 1> past{decoration(2, 40, coloured(red))};
    bool refused = false;
    try {
        f.editor.set_decorations(TextDecorationLayer{0U}, past);
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && f.editor.decorations().empty(), "a range past the text is refused before any change");
}

void box_weight_changes_layout() {
    Fixture f{};
    f.editor.text = "ab cd";
    f.editor.select(Utf8Offset(3), Utf8Offset(3));
    SemanticTextLayout plain{};
    const SemanticTextResult plain_result = f.editor.on_semantic_text_layout(plain);
    require(plain_result == SemanticTextResult::done && plain.graphemes.size() == 5U, "the layout is readable");
    const double plain_x = plain.graphemes[3].bounds.x;
    const std::array<TextDecoration, 1> heavy{decoration(0, 2, bold())};
    f.editor.set_decorations(TextDecorationLayer{0U}, heavy);
    f.window.perform_layout();
    SemanticTextLayout weighted{};
    const SemanticTextResult weighted_result = f.editor.on_semantic_text_layout(weighted);
    require(weighted_result == SemanticTextResult::done && weighted.graphemes.size() == 5U, "the layout stays readable");
    require(weighted.graphemes[3].bounds.x == plain_x + 4.0, "bold text moves the glyphs after it");
    RecordingPainter painter{};
    f.paint(painter);
    const PaintedText* bold_run = nullptr;
    const PaintedText* plain_run = nullptr;
    for (const PaintedText& text : painter.texts) {
        if (text.text == "ab") bold_run = &text;
        if (text.text == " cd") plain_run = &text;
    }
    require(bold_run != nullptr && plain_run != nullptr, "a font change splits the shaping run");
    require((*bold_run).font.weight == 700U && (*plain_run).font.weight != 700U, "each run draws in its own font");
    require((*plain_run).origin.x - (*bold_run).origin.x == 24.0, "the plain run starts after the bold advances");
}

void box_colour_change_damages_its_rows() {
    Fixture f{};
    f.editor.text = "one\ntwo\nthree";
    RecordingPainter first{};
    f.paint(first);
    static_cast<void>(f.window.take_damage());
    const std::array<TextDecoration, 1> two{decoration(4, 7, coloured(red))};
    f.editor.replace_decorations(TextDecorationLayer{0U}, bytes(4, 7), two);
    const DamageRegion damage = f.window.take_damage();
    require(!damage.empty(), "a colour change damages something");
    const Rect bounds = damage.bounds();
    // The second row's line box: 4 + 20 down, 20 tall.
    require(bounds.y >= 24.0 - 2.0 && bounds.y + bounds.height <= 44.0 + 2.0,
            "a colour change damages only the row it touched");
    RecordingPainter second{};
    f.paint(second);
    static_cast<void>(f.window.take_damage());
    const std::array<TextDecoration, 1> heavy{decoration(4, 7, bold())};
    f.editor.replace_decorations(TextDecorationLayer{0U}, bytes(4, 7), heavy);
    require(has_dirty(f.editor.dirty(), Dirty::measure), "a weight change lays the text out again");
}

void selected_decorated_text_drawn_once() {
    Fixture f{};
    f.editor.text = "abc def";
    const std::array<TextDecoration, 2> runs{decoration(0, 3, coloured(red)), decoration(4, 7, coloured(blue))};
    f.editor.set_decorations(TextDecorationLayer{0U}, runs);
    f.window.perform_layout();
    f.editor.select(Utf8Offset(2U), Utf8Offset(5U));
    test_support::ClipTrackingPainter painter{};
    f.editor.on_paint(painter, {0, 0, 200, 88});
    require(painter.texts.size() >= 5U && !painter.overdraws(), "a decorated row draws each glyph once");
    bool selected_ink = false;
    for (const test_support::ClippedText& text : painter.texts) {
        const bool in_band = text.clip.x >= 6.0 + 20.0 - 0.001 && text.clip.x + text.clip.width <= 6.0 + 50.0 + 0.001;
        if (in_band && !(text.color == red) && !(text.color == blue)) selected_ink = true;
    }
    require(selected_ink, "the selection's ink stands over decoration colours inside the band");
}

} // namespace

int main() {
    try {
        collection_replace_and_validation();
        collection_replace_range();
        collection_shift_for_edit();
        collection_compose();
        change_between_texts();
        box_paints_styled_runs();
        box_follows_edits();
        box_weight_changes_layout();
        box_colour_change_damages_its_rows();
        selected_decorated_text_drawn_once();
        std::cout << "gui_forms_text_decorations_tests: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
