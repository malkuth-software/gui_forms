#pragma once

// LinkPage: the example control of docs/user/custom-controls.md. A page of
// wrapped paragraphs with links, painted by the control itself and scrolled
// by ScrollableControl, written the retained way:
//
// - Inputs: the paragraphs (content_revision_), the width the text flows
//   at, the text font, the window's text engine and its scale.
// - Kept layout: every line and piece in content coordinates, rebuilt by
//   ensure_layout only when one of those inputs changed (LayoutKey).
// - Readers: paint, hit testing, hover and clicks read the kept layout.
//   They never flow text and never ask for layout; hover and press changes
//   repaint the link they touch and nothing else.
//
// The page states the size of what it paints (painted_content_extent), so
// ScrollableControl's arrange keeps the scroll position across arranges and
// the control never moves it from its own arrange.
//
// Holder: the page owns its paragraphs and its laid-out lines; its link
// activations are reported through link_activated().

#include <gui_forms/control/stable_id/stable_id.hpp>
#include <gui_forms/controls/scrollable_control.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/events/mouse_events/mouse_events.hpp>
#include <gui_forms/text/paragraph_layout/paragraph_layout.hpp>
#include <gui_forms/types/geometry/geometry.hpp>
#include <gui_forms/types/paint_types/paint_types.hpp>

#include <stxlib/optional.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {
class Painter;
class TextMetricsProvider;
} // namespace gui_forms

namespace link_page_example {

// A link inside a paragraph: the bytes [begin, end) of its text, and what
// it points at (the application's own name for the destination).
struct LinkSpan final {
    std::size_t begin{};
    std::size_t end{};
    std::string target;
};

// One paragraph: UTF-8 text, a line feed inside it ends a line, and its
// links in text order, neither empty nor overlapping.
struct Paragraph final {
    std::string text;
    std::vector<LinkSpan> links;
};

// What link_activated() reports: the link's index over the whole page (in
// paragraph order, then text order) and its target, borrowed from the page
// for the handler's call.
struct LinkActivation final {
    std::size_t link{};
    std::string_view target;
};

class LinkPage final : public gui_forms::ScrollableControl {
public:
    explicit LinkPage(gui_forms::StableId id);

    // Replaces the content. Refuses, with std::invalid_argument and nothing
    // changed, a link that is empty, out of order, overlapping another or
    // outside its paragraph's text. The next arrange flows the new text.
    void set_paragraphs(std::vector<Paragraph> paragraphs);

    [[nodiscard]] std::size_t paragraph_count() const noexcept { return paragraphs_.size(); }
    [[nodiscard]] std::size_t link_count() const noexcept { return link_addresses_.size(); }
    // The link under the pointer, none when the pointer is over no link.
    [[nodiscard]] stxlib::Optional<std::size_t> hovered_link() const noexcept { return hovered_link_; }
    // The bounds of the first line a link is on, in control coordinates at
    // the current scroll position; none for an unknown link or before the
    // first layout. Reads the kept layout only.
    [[nodiscard]] stxlib::Optional<gui_forms::Rect> link_bounds(std::size_t link) const noexcept;
    // How many times the page flowed its text (ensure_layout passed its
    // early return): what a test checks to prove scrolling, hovering and
    // re-arranging at the same width flow nothing.
    [[nodiscard]] std::uint64_t flow_count() const noexcept { return flow_count_; }
    // The height of the laid-out content, in logical pixels.
    [[nodiscard]] double content_height() const noexcept { return content_height_; }

    [[nodiscard]] gui_forms::Event<LinkActivation&>& link_activated() noexcept { return link_activated_; }

    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    void arrange(gui_forms::Rect final_bounds) override;

protected:
    [[nodiscard]] bool paints_scrolled_content() const noexcept override { return true; }
    [[nodiscard]] gui_forms::Size painted_content_extent() const noexcept override;
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect local_damage) override;
    void on_mouse_down(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_up(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_move(gui_forms::MouseEventArgs& arguments) override;
    void on_mouse_leave() override;

private:
    // Everything the laid-out text depends on. Two equal keys flow the same
    // lines, so ensure_layout returns early when the key is unchanged.
    struct LayoutKey final {
        std::uint64_t content_revision{};
        double flow_width{-1.0};
        gui_forms::FontSpec font{};
        const gui_forms::TextMetricsProvider* provider{};
        double scale{};

        friend bool operator==(const LayoutKey& left, const LayoutKey& right) noexcept {
            return left.content_revision == right.content_revision && left.flow_width == right.flow_width &&
                   left.font == right.font && left.provider == right.provider && left.scale == right.scale;
        }
    };

    // A run of one line drawn in one style: plain text, or part of a link.
    // bounds are in content coordinates (the page's top left at the origin,
    // unscrolled), baseline from the content's top; [begin, end) are bytes
    // of paragraph `paragraph`'s text.
    struct LaidPiece final {
        gui_forms::Rect bounds{};
        double baseline{};
        std::size_t paragraph{};
        std::size_t begin{};
        std::size_t end{};
        std::size_t link{no_link};
    };

    // One line: its top and height in content coordinates and its pieces.
    struct LaidLine final {
        double top{};
        double height{};
        std::size_t first_piece{};
        std::size_t piece_count{};
    };

    // A link's pieces, which are consecutive (a link's text is), in order.
    struct LaidLink final {
        std::size_t first_piece{};
        std::size_t piece_count{};
    };

    static constexpr std::size_t no_link = static_cast<std::size_t>(-1);

    // The key the window and the control give now for text flowed at
    // `flow_width`.
    [[nodiscard]] LayoutKey current_key(double flow_width) const;
    // Flows every paragraph at `flow_width` unless the kept layout already
    // has the same key. The only place text is flowed.
    void ensure_layout(double flow_width);
    void flow_paragraph(std::size_t paragraph, const gui_forms::ParagraphStyle& style,
                        gui_forms::TextMetricsProvider* provider, double& top, std::size_t& next_link);
    // The width of `text` in `font`, from `provider` or, without one, the
    // renderer-free estimate.
    [[nodiscard]] static double text_width(gui_forms::TextMetricsProvider* provider, std::string_view text,
                                           gui_forms::FontSpec font);
    // Where the content's origin is in control coordinates at the current
    // scroll position.
    [[nodiscard]] gui_forms::Point content_origin() const noexcept;
    // The first line whose bottom is below content y `y`.
    [[nodiscard]] std::size_t first_line_below(double y) const noexcept;
    // The link under `local`, a point in control coordinates (MouseEventArgs::location).
    [[nodiscard]] stxlib::Optional<std::size_t> link_at(gui_forms::Point local) const noexcept;
    // Repaints a link's pieces only.
    void damage_link(const stxlib::Optional<std::size_t>& link);
    void set_hovered_link(const stxlib::Optional<std::size_t>& link);

    // Inputs.
    std::vector<Paragraph> paragraphs_{};
    // Where each link is, by page-wide link index: its paragraph and its
    // index in that paragraph's links.
    struct LinkAddress final {
        std::size_t paragraph{};
        std::size_t span{};
    };
    std::vector<LinkAddress> link_addresses_{};
    std::uint64_t content_revision_{1U};
    gui_forms::FontSpec body_font_{gui_forms::FontRole::control, 14.0, 400, false};

    // Kept layout, valid for layout_key_ while laid_out_ is true.
    bool laid_out_{};
    LayoutKey layout_key_{};
    std::vector<LaidLine> lines_{};
    std::vector<LaidPiece> pieces_{};
    std::vector<LaidLink> links_{};
    double content_height_{};
    // The control width the last arrange was given and the width the text
    // flowed at then: their difference is the room the vertical bar took.
    double arranged_width_{-1.0};
    // Reused storage for paragraph layout, sized for the longest paragraph.
    gui_forms::ParagraphBuffers buffers_{};

    // Interaction state: which link the pointer is over and which one a
    // primary press started on. Changing either repaints, never re-lays out.
    stxlib::Optional<std::size_t> hovered_link_{};
    stxlib::Optional<std::size_t> pressed_link_{};

    std::uint64_t flow_count_{};
    gui_forms::Event<LinkActivation&> link_activated_;
};

} // namespace link_page_example
