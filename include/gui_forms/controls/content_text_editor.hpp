#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/content/editing/content_text_layout.hpp"
#include "gui_forms/content/frame_sink/content_frame_sink.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/text_box_base.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/scheduler/frame_request_token/frame_request_token.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// One user edit the editor accepted (ContentTextEditor::user_edit): its kind
// (typing, deletion or paste) and whether the input was an unfinished
// composition. Raised after the edit's text_changed, or alone when the edit
// replaced bytes by the same bytes; refused edits, undo, redo and set_text
// raise none.
struct GUI_FORMS_API ContentUserEdit final {
    TextEditKind kind{TextEditKind::typing};
    bool composing{false};
};

namespace detail {
struct ContentChannel;
class TextEditingEngine;
} // namespace detail

// The content editing mode (owner decisions 2026-11-13, "Content editing
// mode and the integrity boundary"): GUI.Forms' editing logic driving text a
// consumer lays out and renders with its own engine and fonts. The editor
// keeps the text and does everything the text controls do with it, through
// the same editing engine as TextBox: caret and selection, the editing keys
// and input methods, undo and redo, the clipboard, and the publication of
// the text, selection, caret and text layout to assistive technology. It
// never shapes or draws the text: the consumer supplies the layout
// (ContentTextLayout: lines, clusters and optional caret stops, validated
// against the text) and the pixels (finished frames through the sink
// open_frames returns, from any thread, as a ContentSurface's).
//
// The protocol, on the UI thread: each change of the text raises
// layout_requested with the new text_revision (and so does a change of the
// region's size); the consumer lays out text() and calls submit_layout with
// that revision, then publishes a frame. A layout for an older revision is
// refused as stale. Until a current layout arrives, keys that move by
// grapheme or word, editing, undo and the clipboard work as ever; the caret
// and selection are not drawn, Up and Down do nothing (Home and End go to
// the ends of the text), the pointer places the caret at the end, and the
// published text layout holds the graphemes with empty boxes. A layout made
// for an earlier text is never applied to a later one.
//
// Painting: the newest frame fills the client area; over it the editor
// draws the selection highlight and the blinking caret from the layout,
// unless set_draws_selection(false) leaves both to the consumer (who reads
// caret_rectangle and selection_rectangles). Without frames (open_frames
// never called, or no frame published yet) the editor paints nothing of its
// content: the empty surface it registers with the window's presentation
// has no frame for a host to show, and its own paint draws no fill, so a
// consumer that draws the text itself, under the editor (an ancestor's
// paint), shows through. Only the selection highlight and the caret are
// drawn over it while draws_selection() is true, and a back colour or an
// authored surface if the program sets one.
//
// Measuring outside paint: layout_requested is raised outside paint. A
// consumer measuring with GUI.Forms' own text engine measures through the
// window (Window::resolve_text_layout_utf8 and measure_text_extent_utf8),
// which read the same text metrics provider as a paint's
// Painter::measure_text_utf8. A consumer may also submit the layout later,
// after its next paint: the revision stays current until the text changes,
// and until then the editor works without a layout, as described above.
// Resubmitting a layout for the current revision (after the consumer
// scrolls its rendering, say) is accepted and replaces the layout; it
// raises no event (layout_requested, text_changed, selection_changed or
// user_edit) and only repaints the editor and republishes its text layout.
//
// Edits: set_edit_filter offers each user edit, with its range and
// replacement, before the editor applies it, exactly as TextBox's filter
// does; user_edit reports each accepted one.
//
// Integrity: everything the consumer hands over is validated before use,
// and a refused layout or frame changes nothing. Text the consumer cannot
// lay out is still edited, published and undone by GUI.Forms.
class GUI_FORMS_API ContentTextEditor final : public TextBoxBase {
public:
    explicit ContentTextEditor(StableId stable_id, std::string initial_text = {});
    ~ContentTextEditor() override;

    // A copy of the text (UTF-8).
    [[nodiscard]] std::string text() const;
    // Replaces the text without undo history; the caret goes to the end.
    // std::invalid_argument for text that is not valid UTF-8.
    void set_text(std::string_view text);
    // Grows with every change of the text; a layout names the revision it
    // was made for.
    [[nodiscard]] std::uint64_t text_revision() const noexcept;
    void set_read_only(bool read_only);
    [[nodiscard]] bool read_only() const noexcept;
    // Whether Enter inserts a line break and Up, Down, Home and End move by
    // the layout's lines (true), or Enter passes on (false, the default).
    void set_multiline(bool multiline);
    [[nodiscard]] bool multiline() const noexcept { return multiline_; }
    bool redo();
    [[nodiscard]] bool can_redo() const noexcept;

    // Validates and adopts a layout for the current revision (the class
    // comment). Returns why it was refused otherwise.
    [[nodiscard]] ContentLayoutResult submit_layout(ContentTextLayout layout);
    // Whether the adopted layout is for the current revision.
    [[nodiscard]] bool layout_current() const noexcept;
    // Opens the channel the consumer publishes the editor's frames through,
    // revoking any earlier one (ContentSurface::open_content).
    [[nodiscard]] ContentFrameSink open_frames(bool opaque);
    [[nodiscard]] std::shared_ptr<LiveSurface> live_surface() const noexcept { return surface_; }

    // The caret's bar in local coordinates, from the current layout; none
    // without one.
    [[nodiscard]] std::optional<Rect> caret_rectangle() const;
    // The selection's boxes in local coordinates, one per cluster part it
    // covers, from the current layout.
    [[nodiscard]] std::vector<Rect> selection_rectangles() const;
    // The text offset nearest `local` by the current layout (the end of the
    // text without one).
    [[nodiscard]] Utf8Offset position_at(Point local) const;
    void set_draws_selection(bool draws);
    [[nodiscard]] bool draws_selection() const noexcept { return draws_selection_; }
    // The edit filter (TextBox::set_edit_filter, with the same rules): it
    // sees every user edit after the editor's own checks (read-only, UTF-8,
    // grapheme boundaries) and before it is applied, including a replacement
    // by the same bytes, and may refuse or rewrite it. It does not see
    // set_text, undo or redo. The delegate does not own its target; clear it
    // (an empty Delegate) before the target goes away.
    void set_edit_filter(Delegate<TextEdit&> filter) noexcept;

    // Raised with the new revision when the text changed or the region's
    // size did: lay out text() and submit the layout.
    [[nodiscard]] Event<std::uint64_t>& layout_requested() noexcept { return layout_requested_; }
    [[nodiscard]] Event<const std::string&>& text_changed() noexcept { return text_changed_; }
    [[nodiscard]] Event<const TextSelection&>& selection_changed() noexcept { return selection_changed_; }
    // Raised after each user edit the editor applied (ContentUserEdit).
    [[nodiscard]] Event<const ContentUserEdit&>& user_edit() noexcept { return user_edit_; }

    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // An editable text box named by the accessible name, with the text as
    // its value and the selection; multiline when multiline() is.
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;

    // Stage T1 anatomy, as dot members: the panel's scrollable parts, then
    // `content` (the consumer's frame), `caret` (the bar while it shows),
    // `selection` (the union of the highlight's boxes) and `focus`.
    struct GUI_FORMS_API Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry content;
        PartGeometry caret;
        PartGeometry selection;
        PartGeometry focus;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool read_only{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    [[nodiscard]] detail::TextEditingEngine* editing_engine() const noexcept override;
    void apply_selection(TextSelection selection) override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    void on_attached_to_window() override;
    void on_detaching_from_window(SurfaceRoot& former_window) noexcept override;
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_key_down(KeyEventArgs& arguments) override;
    void on_text_input(TextInputEvent& event) override;
    void on_focus_changed(bool focused) override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] SemanticTextResult on_semantic_text_layout(SemanticTextLayout& layout) override;
    [[nodiscard]] SemanticTextResult on_set_semantic_text_selection(SemanticTextSelection selection) override;
    [[nodiscard]] std::optional<Rect> text_input_caret() const override;

private:
    // The engine's host: forwards each engine reaction to the editor.
    class EditingHost;
    // The caret blink's timer callback; holds the editor weakly.
    class CaretTick;
    // The frame channel's notices, posted to the UI thread.
    class FrameNotices;

    // The engine changed the text (an edit, undo or redo).
    void text_edited();
    void set_selection(TextSelection selection);
    void handle_key(KeyEvent& event);
    // Up, Down, Home and End by the layout's lines; false when there is no
    // current layout or the key is not one of them.
    [[nodiscard]] bool move_by_line(KeyEvent& event);
    void handle_pointer(PointerEvent& event);
    // The index of the line holding `offset` in the current layout, or none.
    [[nodiscard]] std::optional<std::size_t> line_of(Utf8Offset offset) const;
    // The caret at `offset` from the current layout, or none.
    [[nodiscard]] std::optional<Rect> caret_at(Utf8Offset offset) const;
    // The caret's x at `offset` on line `line` of the current layout: a
    // caret stop's centre, else the edge of the line's cluster at `offset`
    // (a fraction of the cluster's width inside one), else the line's left.
    [[nodiscard]] double boundary_x_on_line(std::size_t line, Utf8Offset offset) const;
    // The offset nearest `x` on line `line` of the current layout.
    [[nodiscard]] Utf8Offset position_on_line(std::size_t line, double x) const;
    void reset_caret_blink();
    void toggle_caret();
    // Registers the newest frame (or an empty placeholder) with the window's
    // presentation, and sends the region's device size to the consumer.
    void register_presentation();
    void update_request();

    std::unique_ptr<EditingHost> editing_host_;
    std::unique_ptr<detail::TextEditingEngine> editing_;
    ContentTextLayout layout_{};
    bool has_layout_{};
    std::shared_ptr<detail::ContentChannel> channel_{};
    std::shared_ptr<LiveSurface> surface_{};
    std::shared_ptr<LiveSurface> placeholder_{};
    std::shared_ptr<FrameNotices> notices_{};
    FrameRequestToken caret_timer_{};
    // The x the caret keeps across Up and Down; negative when unset.
    double preferred_x_{-1.0};
    bool multiline_{};
    bool draws_selection_{true};
    bool focused_{};
    bool selecting_{};
    bool caret_visible_{};
    Size arranged_size_{};
    Event<std::uint64_t> layout_requested_;
    Event<const std::string&> text_changed_;
    Event<const TextSelection&> selection_changed_;
    Event<const ContentUserEdit&> user_edit_;
};

} // namespace gui_forms
