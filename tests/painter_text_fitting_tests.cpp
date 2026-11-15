// The flame graph demonstrator's F2 and F3: Painter::fit_text_utf8,
// draw_text_utf8_fitted (cut and ellipsis) and draw_text_runs, against a
// painter that measures every code point as 10 units wide.

#include "gui_forms/types/painter/painter.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace gui_forms;

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "painter text fitting test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct DrawnText final {
    Point origin{};
    std::string text;
    Color color{};
};

class FixedWidthPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_text_utf8(Point origin, std::string_view text, FontSpec, Color color) override {
        DrawnText drawn{};
        drawn.origin = origin;
        drawn.text.assign(text);
        drawn.color = color;
        drawn_.push_back(drawn);
    }
    [[nodiscard]] Size measure_text_utf8(std::string_view text, FontSpec) override {
        ++measures_;
        std::size_t scalars = 0U;
        for (const char byte : text) {
            if ((static_cast<unsigned char>(byte) & 0xC0U) != 0x80U) ++scalars;
        }
        const Size result{static_cast<double>(scalars) * 10.0, 12.0};
        return result;
    }

    std::vector<DrawnText> drawn_;
    std::size_t measures_{};
};

void test_fit() {
    FixedWidthPainter painter;
    const FontSpec font{};
    require(painter.fit_text_utf8("abcdef", font, 35.0) == 3U, "three code points fit in 35");
    require(painter.fit_text_utf8("abcdef", font, 60.0) == 6U, "the whole text fits in 60");
    require(painter.fit_text_utf8("abcdef", font, 5.0) == 0U, "nothing fits in 5");
    require(painter.fit_text_utf8("abcdef", font, -1.0) == 0U, "a negative width fits nothing");
    require(painter.fit_text_utf8("abcdef", font, std::nan("")) == 0U, "NaN fits nothing");
    // Two-byte code points: the cut never splits one.
    const std::string_view greek{"\xce\xb1\xce\xb2\xce\xb3"};
    require(painter.fit_text_utf8(greek, font, 25.0) == 4U, "two two-byte code points fit, whole");
    painter.measures_ = 0U;
    static_cast<void>(painter.fit_text_utf8(std::string(1024U, 'x'), font, 5000.0));
    require(painter.measures_ <= 12U, "the search measures about log2(bytes) times");
}

void test_fitted_draw() {
    FixedWidthPainter painter;
    const FontSpec font{};
    const Color ink = Color::rgba(10, 20, 30);
    double width = painter.draw_text_utf8_fitted(Point{5.0, 9.0}, "main", font, ink, 100.0, TextTrim::ellipsis);
    require(width == 40.0 && painter.drawn_.size() == 1U && painter.drawn_[0].text == "main",
            "text that fits is drawn whole");
    painter.drawn_.clear();
    width = painter.draw_text_utf8_fitted(Point{5.0, 9.0}, "render_frame", font, ink, 45.0, TextTrim::cut);
    require(width == 40.0 && painter.drawn_.size() == 1U && painter.drawn_[0].text == "rend", "cut keeps what fits");
    painter.drawn_.clear();
    width = painter.draw_text_utf8_fitted(Point{5.0, 9.0}, "render_frame", font, ink, 45.0, TextTrim::ellipsis);
    require(painter.drawn_.size() == 2U && painter.drawn_[0].text == "ren" &&
                painter.drawn_[1].text == "\xE2\x80\xA6",
            "the ellipsis follows the prefix that leaves room for it");
    require(painter.drawn_[1].origin.x == 35.0 && painter.drawn_[1].origin.y == 9.0 && width == 40.0,
            "the ellipsis is drawn after the prefix");
    painter.drawn_.clear();
    width = painter.draw_text_utf8_fitted(Point{0.0, 0.0}, "ab cd", font, ink, 45.0, TextTrim::ellipsis);
    require(painter.drawn_.size() == 2U && painter.drawn_[0].text == "ab", "a trailing space before the ellipsis is dropped");
    painter.drawn_.clear();
    width = painter.draw_text_utf8_fitted(Point{0.0, 0.0}, "abc", font, ink, 5.0, TextTrim::ellipsis);
    require(width == 0.0 && painter.drawn_.empty(), "nothing is drawn when the ellipsis does not fit");
}

void test_runs() {
    FixedWidthPainter painter;
    const FontSpec font{};
    const TextRun runs[3]{{Point{0.0, 0.0}, "a", Color::rgba(255, 0, 0)},
                          {Point{0.0, 10.0}, "b", Color::rgba(0, 255, 0)},
                          {Point{0.0, 20.0}, "c", Color::rgba(0, 0, 255)}};
    painter.draw_text_runs(font, std::span<const TextRun>(runs));
    require(painter.drawn_.size() == 3U, "one draw per run");
    require(painter.drawn_[1].text == "b" && painter.drawn_[1].origin.y == 10.0 &&
                painter.drawn_[1].color == Color::rgba(0, 255, 0),
            "each run keeps its origin, text and colour, in order");
}

} // namespace

int main() {
    test_fit();
    test_fitted_draw();
    test_runs();
    std::cout << "painter text fitting tests passed\n";
    return EXIT_SUCCESS;
}
