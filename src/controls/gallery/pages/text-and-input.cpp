#include "pages.hpp"
#include "gui_forms/controls/auto_complete.hpp"
#include "gui_forms/controls/button_text_box.hpp"
#include "gui_forms/controls/combo_box.hpp"
#include "gui_forms/controls/combo_box_values.hpp"
#include "gui_forms/controls/command_bar.hpp"
#include "gui_forms/controls/content_text_editor.hpp"
#include "gui_forms/content/editing/content_text_layout.hpp"
#include "gui_forms/content/frame_sink/content_frame_sink.hpp"
#include "gui_forms/controls/masked_text_box.hpp"
#include "gui_forms/controls/masked_text_provider.hpp"
#include "gui_forms/controls/rich_text_box.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/token_box.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include "text-and-input_form.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::gallery::pages {
namespace {

// CommandBar item ids. The bar names its retained buttons
// "gallery.text.command.item.<id>"; the numbering is the retired page's.
constexpr int command_select_all = 0;
constexpr int command_replace = 1;
constexpr int command_undo = 2;
constexpr int command_redo = 3;
constexpr int command_copy = 4;
constexpr int command_cut = 5;
constexpr int command_paste = 6;

// ---------------------------------------------------------------------------
// The content editor's toy engine: what a consumer with its own text stack
// does, at its smallest. It lays the text out on a fixed grid (one line per
// line break, one cell per character) and draws each character from a 3 by
// 5 dot font of its own (capitals, digits and common punctuation; lowercase
// is drawn as capitals and anything else as a block). GUI.Forms never shapes
// or draws this text; it keeps the caret, selection, keys, undo and
// accessibility over the layout the engine submits.

// The grid, in the editor's logical units.
constexpr double toy_padding = 6.0;
constexpr double toy_advance = 8.0;
constexpr double toy_line_height = 16.0;
// One dot of the font, and the glyph's top inside its line.
constexpr double toy_dot = 2.0;
constexpr double toy_glyph_top = 3.0;
constexpr std::uint32_t toy_glyph_columns = 3U;
constexpr std::uint32_t toy_glyph_rows = 5U;
// A glyph with every dot set: a character the font does not have.
constexpr std::uint16_t toy_block = 0x7FFFU;

// One glyph's dots: five rows of three bits, top row in bits 14..12, each
// row's left dot in its highest bit. Zero draws nothing (space).
[[nodiscard]] std::uint16_t toy_glyph(char32_t code) noexcept {
    char32_t upper = code;
    if (code >= U'a' && code <= U'z') upper = code - U'a' + U'A';
    switch (upper) {
    case U' ': return 0x0000U;
    case U'A': return 0b010'101'111'101'101U;
    case U'B': return 0b110'101'110'101'110U;
    case U'C': return 0b011'100'100'100'011U;
    case U'D': return 0b110'101'101'101'110U;
    case U'E': return 0b111'100'110'100'111U;
    case U'F': return 0b111'100'110'100'100U;
    case U'G': return 0b011'100'101'101'011U;
    case U'H': return 0b101'101'111'101'101U;
    case U'I': return 0b111'010'010'010'111U;
    case U'J': return 0b001'001'001'101'010U;
    case U'K': return 0b101'101'110'101'101U;
    case U'L': return 0b100'100'100'100'111U;
    case U'M': return 0b101'111'111'101'101U;
    case U'N': return 0b110'101'101'101'101U;
    case U'O': return 0b010'101'101'101'010U;
    case U'P': return 0b110'101'110'100'100U;
    case U'Q': return 0b010'101'101'110'011U;
    case U'R': return 0b110'101'110'101'101U;
    case U'S': return 0b011'100'010'001'110U;
    case U'T': return 0b111'010'010'010'010U;
    case U'U': return 0b101'101'101'101'111U;
    case U'V': return 0b101'101'101'101'010U;
    case U'W': return 0b101'101'111'111'101U;
    case U'X': return 0b101'101'010'101'101U;
    case U'Y': return 0b101'101'010'010'010U;
    case U'Z': return 0b111'001'010'100'111U;
    case U'0': return 0b111'101'101'101'111U;
    case U'1': return 0b010'110'010'010'111U;
    case U'2': return 0b110'001'010'100'111U;
    case U'3': return 0b110'001'010'001'110U;
    case U'4': return 0b101'101'111'001'001U;
    case U'5': return 0b111'100'110'001'110U;
    case U'6': return 0b011'100'111'101'111U;
    case U'7': return 0b111'001'010'010'010U;
    case U'8': return 0b111'101'111'101'111U;
    case U'9': return 0b111'101'111'001'110U;
    case U'.': return 0b000'000'000'000'010U;
    case U',': return 0b000'000'000'010'100U;
    case U'!': return 0b010'010'010'000'010U;
    case U'?': return 0b110'001'010'000'010U;
    case U'-': return 0b000'000'111'000'000U;
    case U'\'': return 0b010'010'000'000'000U;
    case U'"': return 0b101'101'000'000'000U;
    case U':': return 0b000'010'000'010'000U;
    case U'(': return 0b001'010'010'010'001U;
    case U')': return 0b100'010'010'010'100U;
    default: return toy_block;
    }
}

// The length of the UTF-8 sequence `lead` begins. The editor's text is
// valid UTF-8, so a continuation byte here does not occur; it counts one.
[[nodiscard]] std::size_t toy_sequence_length(unsigned char lead) noexcept {
    if (lead < 0x80U) return 1U;
    if (lead >= 0xF0U) return 4U;
    if (lead >= 0xE0U) return 3U;
    if (lead >= 0xC0U) return 2U;
    return 1U;
}

// The code point of the `length` bytes at `at`.
[[nodiscard]] char32_t toy_decode(std::string_view text, std::size_t at, std::size_t length) noexcept {
    const unsigned char lead = static_cast<unsigned char>(text[at]);
    char32_t code = lead;
    if (length == 2U) code = lead & 0x1FU;
    if (length == 3U) code = lead & 0x0FU;
    if (length == 4U) code = lead & 0x07U;
    for (std::size_t index = 1U; index < length; ++index) {
        code = (code << 6U) | (static_cast<unsigned char>(text[at + index]) & 0x3FU);
    }
    return code;
}

// Whether `code` belongs to the character before it in the engine's simple
// clustering: combining marks, variation selectors, skin tones and the
// joiner. A sequence the engine splits where GUI.Forms finds no grapheme
// boundary is refused (refused_boundary), and the editor goes on without
// a layout until the next revision.
[[nodiscard]] bool toy_joins_previous(char32_t code) noexcept {
    const bool combining = code >= 0x0300U && code <= 0x036FU;
    const bool selector = code >= 0xFE00U && code <= 0xFE0FU;
    const bool tone = code >= 0x1F3FBU && code <= 0x1F3FFU;
    return combining || selector || tone || code == 0x200DU;
}

struct ToyColour final {
    std::byte blue{};
    std::byte green{};
    std::byte red{};
};

// The engine: lays out and draws. One instance per editor, on the UI thread;
// its pixel buffer is reused frame after frame.
class ToyTextEngine final {
public:
    // Replaces `layout` with the grid layout of `text` for `revision`.
    void lay_out(std::string_view text, std::uint64_t revision, ContentTextLayout& layout) const {
        layout.text_revision = revision;
        layout.lines.clear();
        layout.clusters.clear();
        layout.carets.clear();
        std::size_t line_start = 0U;
        std::size_t at = 0U;
        std::uint32_t column = 0U;
        bool after_joiner = false;
        while (at < text.size()) {
            const std::size_t length = std::min(toy_sequence_length(static_cast<unsigned char>(text[at])),
                                                text.size() - at);
            const char32_t code = toy_decode(text, at, length);
            if (code == U'\n') {
                // A CR before the break belongs to the break, as CRLF is one
                // grapheme; neither is in the line.
                std::size_t line_end = at;
                if (line_end > line_start && text[line_end - 1U] == '\r') {
                    line_end -= 1U;
                    if (!layout.clusters.empty() && layout.clusters.back().range.start.value() == line_end) {
                        layout.clusters.pop_back();
                        column -= 1U;
                    }
                }
                close_line(layout, line_start, line_end, column);
                line_start = at + length;
                at = line_start;
                column = 0U;
                after_joiner = false;
                continue;
            }
            const bool joins = !layout.clusters.empty() && column != 0U && (toy_joins_previous(code) || after_joiner);
            if (joins) {
                layout.clusters.back().range.end = Utf8Offset(at + length);
            } else {
                ContentTextCluster cluster{};
                cluster.range = Utf8Range{Utf8Offset(at), Utf8Offset(at + length)};
                cluster.line = static_cast<std::uint32_t>(layout.lines.size());
                cluster.bounds = Rect{toy_padding + toy_advance * column, line_top(layout.lines.size()),
                                      toy_advance, toy_line_height};
                layout.clusters.push_back(cluster);
                column += 1U;
            }
            after_joiner = code == 0x200DU;
            at += length;
        }
        close_line(layout, line_start, text.size(), column);
    }

    // Draws `layout` of `text` at the request's device size into the
    // engine's buffer and returns the frame, which borrows that buffer
    // until the next render. An empty frame (width zero) while the editor
    // has no size yet; it is not published.
    [[nodiscard]] ContentFrame render(const ContentTextLayout& layout, std::string_view text,
                                      const ContentRequest& request) {
        ContentFrame frame{};
        const std::uint32_t width = request.size.width;
        const std::uint32_t height = request.size.height;
        if (width == 0U || height == 0U) return frame;
        const std::uint64_t row_bytes = static_cast<std::uint64_t>(width) * 4U;
        // The request is bounded by the surface's frame limits, so the size
        // fits a std::size_t on every supported target.
        pixels_.resize(static_cast<std::size_t>(row_bytes * height));
        fill(width, height, 0U, 0U, width, height, paper_);
        const double scale = request.scale;
        const std::uint32_t dot = static_cast<std::uint32_t>(std::max(1.0, std::round(toy_dot * scale)));
        for (const ContentTextCluster& cluster : layout.clusters) {
            const std::size_t start = cluster.range.start.value();
            const std::size_t length = std::min(toy_sequence_length(static_cast<unsigned char>(text[start])),
                                                text.size() - start);
            const std::uint16_t glyph = toy_glyph(toy_decode(text, start, length));
            const ToyColour& colour = glyph == toy_block ? muted_ink_ : ink_;
            const std::uint32_t left = static_cast<std::uint32_t>(std::round(cluster.bounds.x * scale));
            const std::uint32_t top =
                static_cast<std::uint32_t>(std::round((cluster.bounds.y + toy_glyph_top) * scale));
            draw_glyph(width, height, glyph, left, top, dot, colour);
        }
        frame.width = width;
        frame.height = height;
        frame.row_bytes = row_bytes;
        frame.opaque = true;
        frame.pixels = std::span<const std::byte>(pixels_.data(), pixels_.size());
        return frame;
    }

private:
    [[nodiscard]] static double line_top(std::size_t line) noexcept {
        return toy_padding + toy_line_height * static_cast<double>(line);
    }

    static void close_line(ContentTextLayout& layout, std::size_t start, std::size_t end, std::uint32_t columns) {
        ContentTextLine line{};
        line.range = Utf8Range{Utf8Offset(start), Utf8Offset(end)};
        line.bounds = Rect{toy_padding, line_top(layout.lines.size()), toy_advance * columns, toy_line_height};
        layout.lines.push_back(line);
    }

    void draw_glyph(std::uint32_t width, std::uint32_t height, std::uint16_t glyph, std::uint32_t left,
                    std::uint32_t top, std::uint32_t dot, const ToyColour& colour) {
        for (std::uint32_t row = 0U; row < toy_glyph_rows; ++row) {
            for (std::uint32_t column = 0U; column < toy_glyph_columns; ++column) {
                const std::uint32_t bit = (toy_glyph_rows - 1U - row) * toy_glyph_columns + (2U - column);
                if (((glyph >> bit) & 1U) == 0U) continue;
                fill(width, height, left + column * dot, top + row * dot, dot, dot, colour);
            }
        }
    }

    // Fills the rectangle, clipped to the frame, with an opaque colour.
    void fill(std::uint32_t width, std::uint32_t height, std::uint32_t x, std::uint32_t y, std::uint32_t w,
              std::uint32_t h, const ToyColour& colour) {
        if (x >= width || y >= height) return;
        const std::uint32_t right = std::min(width, x + w);
        const std::uint32_t bottom = std::min(height, y + h);
        const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
        for (std::uint32_t row = y; row < bottom; ++row) {
            std::byte* const line = pixels_.data() + static_cast<std::size_t>(row) * row_bytes;
            for (std::uint32_t column = x; column < right; ++column) {
                std::byte* const pixel = line + static_cast<std::size_t>(column) * 4U;
                pixel[0] = colour.blue;
                pixel[1] = colour.green;
                pixel[2] = colour.red;
                pixel[3] = std::byte{255U};
            }
        }
    }

    std::vector<std::byte> pixels_{};
    ToyColour paper_{std::byte{236U}, std::byte{246U}, std::byte{250U}};
    ToyColour ink_{std::byte{72U}, std::byte{46U}, std::byte{30U}};
    ToyColour muted_ink_{std::byte{170U}, std::byte{160U}, std::byte{150U}};
};

// Owns the text-and-input page: the controls demo/pages/text-and-input.form
// describes, and their behaviour.
class TextAndInputPage final : public Component {
public:
    TextAndInputPage(const std::shared_ptr<Surface>& body,
                     const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        document_.initialize_component(*this);
        mount_document(body);
        build_fields();
        build_commands();
        build_masked();
        build_auto_complete();
        build_tokens();
        build_rich_text();
        build_search();
        build_buttons();
        build_content_editor();
        refresh_state();
    }

private:
    void mount_document(const std::shared_ptr<Surface>& body) {
        // The document's root is a declared boundary only: its children move
        // into the shell's page body, where each is placed by its bounds in
        // design coordinates. The copy of the child list keeps the iteration
        // valid while the root shrinks.
        Control& root = *document_.gallery_text_document;
        const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
        for (const Control::Ptr& child : children) {
            Control::Ptr moved = root.remove_child((*child).runtime_id());
            (*body).add_child(std::move(moved));
        }
        primary_ = document_.gallery_text_primary;
        state_ = document_.gallery_text_state;
        bar_ = document_.gallery_text_command;
        clipboard_status_ = document_.gallery_text_clipboard_status;
        phone_ = document_.gallery_text_masked_phone;
        masked_status_ = document_.gallery_text_masked_status;
        city_ = document_.gallery_text_complete_city;
        band_ = document_.gallery_text_complete_band;
        complete_status_ = document_.gallery_text_complete_status;
        recipients_ = document_.gallery_text_tokens_recipients;
        tokens_status_ = document_.gallery_text_tokens_status;
        note_ = document_.gallery_text_rich_note;
        rich_status_ = document_.gallery_text_rich_status;
    }

    void build_fields() {
        (*document_.gallery_text_password).use_system_password_character = true;
        // Both are state events; the handler reads the field, so it takes no
        // event argument and serves either publisher.
        on((*primary_).text_changed(), *this, &TextAndInputPage::refresh_state);
        on((*primary_).selection_changed(), *this, &TextAndInputPage::refresh_state);
    }

    void build_commands() {
        // Undo and Redo are Commands so their availability (the field's
        // history) projects onto the bar's retained buttons; the other five
        // items dispatch through the bar's single group notification.
        (*bar_).set_items({
            {.id = command_select_all, .label = "Select all"},
            {.id = command_copy, .label = "Copy"},
            {.id = command_cut, .label = "Cut"},
            {.id = command_paste, .label = "Paste"},
            {.id = command_replace, .label = "Replace"},
            {.id = command_undo, .label = "Undo", .command = &undo_},
            {.id = command_redo, .label = "Redo", .command = &redo_},
        });
        (*bar_).item(4).visual_style = ButtonVisualStyle::accent;
        on((*bar_).invoked(), *this, &TextAndInputPage::on_command);
        on(undo_.invoked(), *this, &TextAndInputPage::on_undo);
        on(redo_.invoked(), *this, &TextAndInputPage::on_redo);
    }

    // MaskedTextBox specimens: a telephone mask, and a part code whose
    // letters are shifted to upper and then lower case. The telephone
    // field's refusals and completion report to the status label.
    void build_masked() {
        (*phone_).mask = "(000) 000-0000";
        (*document_.gallery_text_masked_code).mask = ">LL-0000-<aa";
        on((*phone_).mask_input_rejected(), *this, &TextAndInputPage::masked_rejected);
        on((*phone_).text_changed(), *this, &TextAndInputPage::masked_changed);
    }

    // Auto-complete specimens: a TextBox that suggests and appends from its
    // own collection, and an editable ComboBox that completes from its
    // items. Accepted completions report to the status label.
    void build_auto_complete() {
        (*(*city_).auto_complete_custom_source.read()).add_range(
            {"Calgary", "Cambridge", "Canberra", "Cardiff", "Edinburgh", "Edmonton", "London", "Los Angeles"});
        (*city_).auto_complete_source = AutoCompleteSource::custom_source;
        (*city_).auto_complete_mode = AutoCompleteMode::suggest_append;
        (*band_).items = {"Long wave", "Medium wave", "Short wave", "VHF FM", "UHF", "Microwave"};
        (*band_).drop_down_style = ComboBoxStyle::drop_down;
        (*band_).auto_complete_source = AutoCompleteSource::list_items;
        (*band_).auto_complete_mode = AutoCompleteMode::suggest;
        on((*city_).auto_complete_accepted(), *this, &TextAndInputPage::completion_accepted);
        on((*(*band_).editor()).auto_complete_accepted(), *this, &TextAndInputPage::completion_accepted);
    }

    // TokenBox specimen: recipients entered by Enter, a comma or a
    // suggestion, each a removable token.
    void build_tokens() {
        (*(*recipients_).auto_complete_custom_source.read()).add_range(
            {"Ada Lovelace", "Alan Turing", "Grace Hopper", "Edsger Dijkstra", "Barbara Liskov"});
        (*recipients_).auto_complete_source = AutoCompleteSource::custom_source;
        (*recipients_).auto_complete_mode = AutoCompleteMode::suggest;
        static_cast<void>((*recipients_).add_token("Grace Hopper"));
        on((*recipients_).token_added(), *this, &TextAndInputPage::token_added);
        on((*recipients_).token_removed(), *this, &TextAndInputPage::token_removed);
    }

    // A RichTextBox over markdown: a heading, emphasis, a link, a list and a
    // code block, shown styled with their markup dimmed. Ctrl+click on the
    // link reports its target.
    void build_rich_text() {
        (*note_).set_markdown("## Release notes\n\n"
                              "Text may be **bold**, *italic* or `code`. "
                              "See [the guide](https://example.org/guide).\n\n"
                              "- Ctrl+B and Ctrl+I format the selection\n"
                              "- Ctrl+K makes a link\n\n"
                              "```\nsave_rtf(document);\n```\n");
        on((*note_).link_clicked(), *this, &TextAndInputPage::rich_link_clicked);
    }

    // The search field reports each query and each clearing.
    void build_search() {
        on((*document_.gallery_text_search_field).search_requested(), *this, &TextAndInputPage::search_requested);
        on((*document_.gallery_text_search_field).cleared(), *this, &TextAndInputPage::search_cleared);
    }

    // The ContentTextEditor: the page's toy engine lays out and draws its
    // text; each change of the text or the editor's size asks for a layout.
    void build_content_editor() {
        content_editor_ = document_.gallery_text_content_editor_field;
        content_status_ = document_.gallery_text_content_editor_status;
        (*content_editor_).set_multiline(true);
        content_frames_ = (*content_editor_).open_frames(true);
        on((*content_editor_).layout_requested(), *this, &TextAndInputPage::content_layout_requested);
        (*content_editor_).set_text("Typed here, laid out and drawn by the page's own engine.\n"
                                    "GUI.Forms keeps the caret, selection, keys and undo.");
    }

    // Lays out the editor's text for `revision`, submits the layout, then
    // publishes its frame (ContentTextEditor's protocol), and reports both.
    void content_layout_requested(std::uint64_t revision) {
        const std::string text = (*content_editor_).text();
        content_engine_.lay_out(text, revision, content_layout_);
        const std::size_t line_count = content_layout_.lines.size();
        const std::size_t cluster_count = content_layout_.clusters.size();
        const ContentFrame frame = content_engine_.render(content_layout_, text, content_frames_.request());
        const ContentLayoutResult submitted = (*content_editor_).submit_layout(std::move(content_layout_));
        content_layout_ = ContentTextLayout{};
        std::string report = "Layout " + std::string(content_layout_result_name(submitted)) + " for revision " +
                             std::to_string(revision) + " · " + std::to_string(line_count) + " lines, " +
                             std::to_string(cluster_count) + " characters";
        if (frame.width != 0U) {
            const ContentPublishResult published = content_frames_.publish(frame);
            report += " · frame " + std::string(content_publish_result_name(published));
        }
        (*content_status_).text = report;
    }

    // The folder field has a browse button and a clear button at its
    // trailing end; each click reports to the status line, and browse
    // fills in a folder as a chooser would.
    void build_buttons() {
        ButtonTextBox& field = *document_.gallery_text_buttons_field;
        EditButton browse;
        browse.stable_id = "browse";
        browse.kind = EditButtonKind::ellipsis;
        browse.caption = "Browse";
        browse.description = "Choose a folder";
        EditButton clear;
        clear.stable_id = "clear";
        clear.kind = EditButtonKind::close;
        clear.caption = "Clear";
        field.set_buttons({browse, clear});
        on(field.button_clicked(), *this, &TextAndInputPage::folder_button_clicked);
    }

    void folder_button_clicked(const EditButtonClick& click) {
        ButtonTextBox& field = *document_.gallery_text_buttons_field;
        if (click.button_id == "browse") {
            field.text = "Documents/Harbour";
            (*document_.gallery_text_buttons_status).text = "Browse clicked · the folder is Documents/Harbour";
            return;
        }
        field.text = std::string{};
        (*document_.gallery_text_buttons_status).text = "Clear clicked · the folder is empty";
    }

    void search_requested(const std::string& query) {
        if (query.empty()) {
            (*document_.gallery_text_search_status).text = "Showing everything";
            return;
        }
        (*document_.gallery_text_search_status).text = "Searching for \"" + query + "\"";
    }

    void search_cleared() {
        (*document_.gallery_text_search_status).text = "Cleared";
    }

    void rich_link_clicked(const std::string& target) {
        (*rich_status_).text = "Link clicked: " + target;
    }

    void token_added(const Token& token) {
        (*tokens_status_).text = "Added \"" + token.text + "\" · recipients: " +
                                   std::to_string((*recipients_).tokens().size());
    }

    void token_removed(const Token& token) {
        (*tokens_status_).text = "Removed \"" + token.text + "\" · recipients: " +
                                   std::to_string((*recipients_).tokens().size());
    }

    void completion_accepted(const std::string& text) {
        (*complete_status_).text = "Accepted \"" + text + "\"";
    }

    void masked_rejected(const MaskInputRejection& rejection) {
        (*masked_status_).text = "Telephone refused \"" + rejection.text + "\" at position " +
                                   std::to_string(rejection.position.value()) + " · " +
                                   std::string(mask_result_hint_name(rejection.hint));
    }

    void masked_changed(const std::string& text) {
        (*masked_status_).text = "Telephone " + text +
                                   ((*phone_).mask_completed() ? " · complete" : " · incomplete");
    }

    void refresh_state() {
        const TextSelection selection = (*primary_).selection();
        (*state_).text = "Selection: " + std::to_string(selection.start().value()) + ".." +
                           std::to_string(selection.end().value()) + " bytes · Undo: " +
                           ((*primary_).can_undo() ? "ready" : "clean") + " · Redo: " +
                           ((*primary_).can_redo() ? "ready" : "clean");
        undo_.set_enabled((*primary_).can_undo());
        redo_.set_enabled((*primary_).can_redo());
    }

    void on_command(const CommandItem& item) {
        switch (item.id) {
        case command_select_all:
            (*primary_).select_all();
            return;
        case command_replace:
            static_cast<void>((*primary_).replace_selection("GUI.Forms"));
            return;
        case command_copy:
            publish_clipboard_status("copy", (*primary_).copy());
            return;
        case command_cut:
            publish_clipboard_status("cut", (*primary_).cut());
            return;
        case command_paste:
            publish_clipboard_status("paste", (*primary_).paste());
            return;
        default:
            return; // Undo and Redo run through their Commands.
        }
    }

    void on_undo() { static_cast<void>((*primary_).undo()); }
    void on_redo() { static_cast<void>((*primary_).redo()); }

    void publish_clipboard_status(const std::string_view command, const bool accepted) {
        (*clipboard_status_).text = std::string("Clipboard ") + std::string(command) +
            (accepted ? " · accepted" : " · unavailable or empty") + " · " +
            std::to_string((*primary_).text.read().size()) + " UTF-8 bytes retained";
    }

    std::shared_ptr<TextBox> primary_;
    std::shared_ptr<Label> state_;
    std::shared_ptr<Label> clipboard_status_;
    std::shared_ptr<CommandBar> bar_;
    std::shared_ptr<MaskedTextBox> phone_;
    std::shared_ptr<Label> masked_status_;
    std::shared_ptr<TextBox> city_;
    std::shared_ptr<ComboBox> band_;
    std::shared_ptr<Label> complete_status_;
    std::shared_ptr<TokenBox> recipients_;
    std::shared_ptr<Label> tokens_status_;
    std::shared_ptr<RichTextBox> note_;
    std::shared_ptr<Label> rich_status_;
    std::shared_ptr<ContentTextEditor> content_editor_;
    std::shared_ptr<Label> content_status_;
    // The editor's frame channel; frames borrow content_engine_'s buffer only
    // during publish, which copies them.
    ContentFrameSink content_frames_{};
    ToyTextEngine content_engine_{};
    // The layout being built; moved into submit_layout, then reset.
    ContentTextLayout content_layout_{};
    Command undo_{"gallery.text.command.undo", "Undo"};
    Command redo_{"gallery.text.command.redo", "Redo"};
    // Declared after the Commands its bar borrows, so it is released first.
    TextAndInputDocument<TextAndInputPage> document_{};
};

} // namespace

void add_text_input(const std::shared_ptr<Surface>& page,
                    const std::shared_ptr<PageContext>& context) {
    (*context).components.add(gui_forms::detail::make_library_shared<TextAndInputPage>(page, context));
}

} // namespace gui_forms::gallery::pages
