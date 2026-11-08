#include "multiline_scrollbars_form.hpp"   // generated from forms/multiline_scrollbars.form

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

void document_viewport() {
    reference::ViewerWindow window{};   // binds nothing, so it takes no model
    gui_forms::TextBox& editor = *window.viewer_document;
    std::string source{};
    for (unsigned int row = 0; row < 80; ++row) {
        source.append(140, 'x');
        source.append("\n");
    }
    editor.text = source;
    editor.select(gui_forms::Utf8Offset(0), gui_forms::Utf8Offset(4));
    window.perform_layout();
    require(editor.hscroll() && editor.vscroll(), "document extent must expose both bars");
    const gui_forms::TextSelection selection = editor.selection();
    const bool scrolled = editor.scroll_to({120.0, 300.0});
    require(scrolled && editor.scroll_offset() == editor.scroll_position(), "text and bars must agree");
    require(editor.text.read() == source && editor.selection() == selection, "scrolling must preserve source and selection");
    editor.word_wrap = true;
    window.perform_layout();
    require(!editor.hscroll() && editor.vscroll() && editor.scroll_offset().x == 0.0, "wrap must use vertical scrolling");
    editor.word_wrap = false;
    window.resize({2400.0, 2400.0});
    window.perform_layout();
    require(!editor.hscroll() && !editor.vscroll() && editor.scroll_offset() == gui_forms::Point{},
            "larger viewport must clamp ranges");
    window.resize({360.0, 240.0});
    window.perform_layout();
    editor.text = "short again";
    window.perform_layout();
    require(!editor.hscroll() && !editor.vscroll(), "short replacement must hide document bars");
}
} // namespace

int main() {
    try {
        document_viewport();
        std::cout << "installed multiline scrollbar contract passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
