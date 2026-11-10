// ComboBox field text colour: a themed combo box leaves its selected text
// to the control colours, so its own fore colour, else its parent's, inks
// the text as a TextBox's does (WinForms' ForeColor); with neither the
// theme's editor text stands. The placeholder keeps the theme's muted text
// and a disabled combo box the theme's disabled text.
#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
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

// Records the colour of every text run; drawing otherwise does nothing.
class TextInkPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view text, FontSpec, Color color) override {
        texts.push_back(std::string(text));
        inks.push_back(color);
    }
    void draw_image(ImageId, Rect, double) override {}
    void draw_box_shadow(Rect, double, Point, double, double, Color) override {}

    std::vector<std::string> texts;
    std::vector<Color> inks;
};

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<ComboBox> combo;
    std::unique_ptr<Window> window;
};

Fixture make_fixture() {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId("combo.ink.root"));
    fixture.combo = make_control<ComboBox>(StableId("combo.ink"));
    (*fixture.combo).set_requested_bounds({10.0, 10.0, 200.0, 28.0});
    (*fixture.combo).drop_down_style = ComboBoxStyle::drop_down_list;
    (*fixture.combo).items = {"Blackman-Harris 4", "Hann"};
    (*fixture.combo).placeholder_text = "Choose";
    (*fixture.combo).selected_index = 0U;
    (*fixture.root).add_child(fixture.combo);
    fixture.window = std::make_unique<Window>(fixture.root, Size{240.0, 60.0});
    (*fixture.window).perform_layout();
    return fixture;
}

// The ink of the field's text run `text` in one paint of the combo box.
Color field_ink(ComboBox& combo, std::string_view text) {
    TextInkPainter painter;
    combo.on_paint(painter, combo.absolute_bounds());
    for (std::size_t index = 0U; index < painter.texts.size(); ++index) {
        if (painter.texts[index] == text) return painter.inks[index];
    }
    require(false, "the field draws its text");
    return Color{};
}

void selected_text_takes_control_colours() {
    Fixture fixture = make_fixture();
    ComboBox& combo = *fixture.combo;
    const Color parent_ink = Color::rgba(250, 250, 250);
    const Color own_ink = Color::rgba(200, 40, 60);
    const Color themed = field_ink(combo, "Blackman-Harris 4");
    require(themed != parent_ink && themed != own_ink, "with no control colours the theme's editor text stands");

    (*fixture.root).fore_color = parent_ink;
    require(field_ink(combo, "Blackman-Harris 4") == parent_ink, "the selected text takes the parent's fore colour");

    combo.fore_color = own_ink;
    require(field_ink(combo, "Blackman-Harris 4") == own_ink, "the combo box's own fore colour stands over its parent's");

    combo.fore_color = std::nullopt;
    (*fixture.root).fore_color = std::nullopt;
    require(field_ink(combo, "Blackman-Harris 4") == themed, "clearing the colours restores the theme's text");
}

void placeholder_and_disabled_keep_the_theme() {
    Fixture fixture = make_fixture();
    ComboBox& combo = *fixture.combo;
    const Color parent_ink = Color::rgba(250, 250, 250);
    (*fixture.root).fore_color = parent_ink;

    combo.selected_index = std::nullopt;
    require(field_ink(combo, "Choose") != parent_ink, "the placeholder keeps the theme's muted text");

    combo.selected_index = 1U;
    combo.enabled = false;
    require(field_ink(combo, "Hann") != parent_ink, "a disabled combo box keeps the theme's disabled text");
}

}  // namespace

int main() {
    selected_text_takes_control_colours();
    placeholder_and_disabled_keep_the_theme();
    std::cout << "combo box paint tests passed\n";
    return 0;
}
