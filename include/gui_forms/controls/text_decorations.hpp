#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms {

// Styled text runs over a multiline TextBox's text: a decoration layer of
// byte ranges, each giving its text a look (a theme ink or a colour, a
// weight, italics, an underline or an error squiggle, a background). The
// text model never changes: decorations sit beside it, follow its edits and
// are composed into runs only to lay out and paint the visible rows.
//
// The shape follows SwiftEdit's highlighter (src/highlight/decorations.hpp):
// each producer (a syntax highlighter, a diagnostics pass, a search) owns
// one layer and replaces its contents whenever its answer changes, so
// producers never see each other; the box composes the layers.

// A theme ink for decorated text: the colour the effective theme gives text
// of that kind, so a theme (and high contrast) styles it. text and muted
// are the editor's content and secondary inks; link, visited_link and
// notice are those families' text.
enum class TextInkRole : std::uint8_t { text, muted, link, visited_link, notice };

// How decorated text is underlined: not, with a straight line, or with a
// wavy line (an error or warning marker).
enum class TextUnderline : std::uint8_t { none, single, squiggle };

// The look one decoration gives its text. Each field left unset leaves that
// aspect as the layers under it (or the box itself) draw it.
// - ink: a theme ink; foreground: an authored colour, which stands over the
//   ink except in high contrast, where the theme's colours stand (T4-1).
// - weight (1 to 1000) and italic change the font, so they change where
//   glyphs fall: the box measures decorated text in its own font, and the
//   caret, hit testing and the accessibility layout follow.
// - underline_color: the underline's colour; unset, or in high contrast, the
//   text's ink.
// - background: a flat ground under the text, drawn under the selection
//   band and left out in high contrast.
struct GUI_FORMS_API TextDecorationStyle final {
    std::optional<TextInkRole> ink{};
    std::optional<Color> foreground{};
    std::optional<std::uint16_t> weight{};
    std::optional<bool> italic{};
    std::optional<TextUnderline> underline{};
    std::optional<Color> underline_color{};
    std::optional<Color> background{};

    // Whether the style changes the font (and therefore the layout).
    [[nodiscard]] bool changes_font() const noexcept {
        const bool result = weight.has_value() || italic.has_value();
        return result;
    }
    [[nodiscard]] bool empty() const noexcept {
        const bool result = !ink.has_value() && !foreground.has_value() && !changes_font() &&
                            !underline.has_value() && !underline_color.has_value() &&
                            !background.has_value();
        return result;
    }
    friend bool operator==(const TextDecorationStyle& left, const TextDecorationStyle& right) noexcept {
        const bool result = left.ink == right.ink && left.foreground == right.foreground &&
                            left.weight == right.weight && left.italic == right.italic &&
                            left.underline == right.underline &&
                            left.underline_color == right.underline_color &&
                            left.background == right.background;
        return result;
    }
};

// One decoration: a byte range of the text (UTF-8 offsets; a bound inside a
// grapheme cluster applies from that cluster's next boundary) and its look.
// `tag` belongs to the producer: a diagnostic's index, a token's kind.
struct GUI_FORMS_API TextDecoration final {
    Utf8Range range{};
    TextDecorationStyle style{};
    std::uint32_t tag{};

    friend bool operator==(const TextDecoration& left, const TextDecoration& right) noexcept {
        const bool result = left.range == right.range && left.style == right.style && left.tag == right.tag;
        return result;
    }
};

// A producer's layer. Layers compose in ascending order: a later layer's
// fields stand over an earlier one's where both set them.
struct GUI_FORMS_API TextDecorationLayer final {
    std::uint32_t value{};

    friend constexpr bool operator==(TextDecorationLayer left, TextDecorationLayer right) noexcept {
        return left.value == right.value;
    }
    friend constexpr std::strong_ordering operator<=>(TextDecorationLayer left,
                                                      TextDecorationLayer right) noexcept {
        return left.value <=> right.value;
    }
};

// One change of the text: removed_bytes at start replaced by inserted_bytes.
struct GUI_FORMS_API TextChange final {
    Utf8Offset start{};
    std::size_t removed_bytes{};
    std::size_t inserted_bytes{};

    friend bool operator==(const TextChange& left, const TextChange& right) noexcept {
        const bool result = left.start == right.start && left.removed_bytes == right.removed_bytes &&
                            left.inserted_bytes == right.inserted_bytes;
        return result;
    }
};

// The smallest change turning `before` into `after`: the bytes between
// their common prefix and their common suffix. Equal texts give an empty
// change at the end. Reads both once; allocates nothing.
[[nodiscard]] GUI_FORMS_API TextChange text_change_between(std::string_view before, std::string_view after) noexcept;

// One run of uniformly styled text, as composition gives it: the fields
// every covering decoration set, the later layer's (and within a layer the
// later decoration's) standing. Undecorated text has an empty style.
struct GUI_FORMS_API StyledTextRun final {
    Utf8Range range{};
    TextDecorationStyle style{};

    friend bool operator==(const StyledTextRun& left, const StyledTextRun& right) noexcept {
        const bool result = left.range == right.range && left.style == right.style;
        return result;
    }
};

// What a change to the decorations touched: the byte range whose look
// changed, whether any look changed at all, and whether a font changed
// (the text must be laid out again) or only colours, lines and grounds (the
// range's rows repaint).
struct GUI_FORMS_API TextDecorationChange final {
    Utf8Range range{};
    bool changed{};
    bool layout{};
};

// The decorations of one text, by layer. Each layer keeps its decorations
// in start order (equal starts in the order given); decorations of one
// layer may overlap. It knows nothing of the text: its owner checks ranges
// against the text and tells it of each edit (shift_for_edit). One
// execution context owns it.
class GUI_FORMS_API TextDecorations final {
public:
    // Replaces `layer`'s decorations with a copy of `decorations`. A range
    // with end before start, or a weight outside 1 to 1000, throws
    // std::invalid_argument before anything changes; empty ranges are
    // dropped. An empty result removes the layer.
    TextDecorationChange replace(TextDecorationLayer layer, std::span<const TextDecoration> decorations);
    // Replaces `layer`'s decorations within `range` only, as an incremental
    // highlighter does after an edit: decorations inside the range go,
    // decorations crossing its bounds keep their parts outside it, and
    // `decorations` (each inside the range, else std::invalid_argument
    // before anything changes) come in. The change is the range alone, and
    // nothing changes when the range already looks that way.
    TextDecorationChange replace_range(TextDecorationLayer layer, Utf8Range range,
                                       std::span<const TextDecoration> decorations);
    TextDecorationChange clear(TextDecorationLayer layer);
    TextDecorationChange clear_all();
    // Follows one edit of the text: ranges after the edit shift; ranges the
    // edit touches keep their outer bytes and take the replacement in (an
    // insertion at a range's end joins it, one at its start does not);
    // ranges the edit removes entirely are dropped.
    void shift_for_edit(TextChange change);

    // The layers in ascending order, and one layer's decorations (empty for
    // a layer without any). The spans are valid until the next change.
    [[nodiscard]] std::size_t layer_count() const noexcept { return layers_.size(); }
    [[nodiscard]] TextDecorationLayer layer_at(std::size_t index) const;
    // Borrowed: any change of the decorations (replace, clear, an edit's
    // shift) ends the views lent so far.
    [[nodiscard]] BorrowedSpan<TextDecoration> items(TextDecorationLayer layer) const noexcept;
    [[nodiscard]] bool empty() const noexcept { return layers_.empty(); }
    // Whether any decoration changes the font.
    [[nodiscard]] bool changes_font() const noexcept { return font_decorations_ != 0U; }
    // Counts the changes that changed a font (a layout cache key).
    [[nodiscard]] std::uint64_t layout_revision() const noexcept { return layout_revision_; }

    // Fills `runs` (cleared first) with runs covering `window` exactly, in
    // order and without gaps; adjacent runs with the same style are merged.
    // An empty window gives no runs. Reuses its own scratch storage, so
    // composing the visible rows on each paint does not allocate once the
    // storage has grown; `runs` keeps its capacity.
    void compose(Utf8Range window, std::vector<StyledTextRun>& runs);

private:
    struct Layer final {
        TextDecorationLayer id{};
        std::vector<TextDecoration> items{};
        // The longest decoration's length: a decoration reaching a window
        // starts at most this far before it.
        std::size_t longest{};
        // How many of the decorations change the font.
        std::size_t font_count{};
    };
    struct DecorationStartsBefore final {
        bool operator()(const TextDecoration& left, const TextDecoration& right) const noexcept;
    };
    struct LayerBefore final {
        bool operator()(const Layer& layer, TextDecorationLayer id) const noexcept;
    };

    [[nodiscard]] std::size_t find_layer(TextDecorationLayer id) const noexcept;
    // Recounts a layer's longest decoration and font decorations.
    void measure_layer(Layer& layer) noexcept;
    // Installs `items` (sorted) as layer `id`'s, creating or removing the
    // layer, and keeps the font count and layout revision.
    void install(TextDecorationLayer id, std::vector<TextDecoration> items, bool layout);
    // Appends the decorations of `layer` that overlap `window` to
    // overlapping_.
    void collect_overlapping(const Layer& layer, Utf8Range window);

    std::vector<Layer> layers_{};
    // Stamps items(); empty in the ordinary build.
    [[no_unique_address]] detail::BorrowLender items_lender_{};
    std::size_t font_decorations_{};
    std::uint64_t layout_revision_{};
    // compose's scratch: the overlapping decorations (borrowed from
    // layers_ for the call, in layer order) and the cut points.
    std::vector<const TextDecoration*> overlapping_{};
    std::vector<std::size_t> cuts_{};
};

} // namespace gui_forms
