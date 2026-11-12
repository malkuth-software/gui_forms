#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/scheduler/frame_request_token/frame_request_token.hpp"
#include "gui_forms/controls/search_field_capabilities.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class TextBox;

// A text field for a query (W-4): a well holding the search mark, an inner
// TextBox (made after construction and owned as the only child), a clear
// mark while there is text, and an activity strip along the bottom while
// `busy`. The strip is an ambient effect on the window's motion clock: a
// segment runs along it once per busy_sweep_period while the field is
// drawn. Under reduced motion, with ambient motion off or in an occluded
// window it is a still strip the field's width.
//
// Each change of the text raises text_changed and, once typing has paused
// for search_delay (on the window's frame schedule; at once outside a
// window or with a zero delay), search_requested with the query. Enter
// raises search_requested at once. Escape, or the clear mark, empties a
// field that has text: that raises cleared, then search_requested with the
// empty query. Escape in an empty field goes on to the containers and the
// window's cancel button.
//
// To assistive technology it is one search_box node (hosts without a
// mapping publish a text box) whose value is the query, busy while `busy`:
// it carries the editor's selection and its text layout and selection
// requests reach the editor, which is not published separately (coordinator
// ruling). The editor takes the keyboard focus, which the node reports.
// One sweep of the busy strip's segment, and the segment's share of the
// strip's width.
inline constexpr FrameInterval busy_sweep_period = std::chrono::milliseconds(1200);
inline constexpr double busy_segment_share = 0.3;

class SearchField final : public Control, public SearchFieldCapabilities<SearchField> {
public:
    // The inner editor is made after construction (make_control calls
    // initialize_control_tree), since add_child needs a shared owner.
    static constexpr bool initialize_tree_after_construction = true;
    explicit SearchField(StableId stable_id);
    ~SearchField() override;
    // Makes the inner editor; make_control calls it once.
    void initialize_control_tree();

    // The inner editor, owned as a child.
    [[nodiscard]] std::shared_ptr<TextBox> editor() const noexcept { return editor_; }
    // Focus on the field goes to its edit part (null without one).
    // Defined in the source: TextBox is only declared here.
    [[nodiscard]] Ptr focus_target() const override;
    // Raises search_requested with the query now, dropping a pending one.
    void search_now();
    // Empties the field: raises cleared, then search_requested with the
    // empty query. False, raising nothing, when the field is already empty.
    bool clear();

    // Raised after the text changes, with the new text.
    [[nodiscard]] Event<const std::string&>& text_changed() noexcept { return text_changed_; }
    // Raised when a search is due, with the query.
    [[nodiscard]] Event<const std::string&>& search_requested() noexcept { return search_requested_; }
    // Raised after the field is emptied by clear, Escape or the clear mark.
    [[nodiscard]] Event<>& cleared() noexcept { return cleared_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] SemanticTextResult on_semantic_text_layout(SemanticTextLayout& layout) override;
    [[nodiscard]] SemanticTextResult on_set_semantic_text_selection(SemanticTextSelection selection) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `well` is the
    // field's ground, `icon` the search mark, `content` the inner editor's
    // slot, `actuator` the clear mark (absent while the field is empty; hot
    // and pressed as the pointer is), `activity` the busy strip (absent
    // unless busy) and `focus` the cue shown while the editor has the
    // keyboard focus.
    struct Parts final {
        PartGeometry region;
        PartGeometry well;
        PartGeometry icon;
        PartGeometry content;
        PartGeometry actuator;
        PartGeometry activity;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_detached_from_window() noexcept override;
    // One ambient frame of the busy strip: the segment advances to `now`.
    // False when the field was not drawn since the last frame.
    bool on_ambient_frame(FrameTime now) override;

private:
    // The search delay's target: runs on the UI thread from the window's
    // frame schedule once typing has paused.
    struct SearchDelayCallback;
    struct Placement final {
        Rect well{};
        Rect icon{};
        Rect content{};
        Rect clear{};
        Rect activity{};
    };
    [[nodiscard]] Placement placement(Size size) const;
    [[nodiscard]] bool focus_visible() const noexcept;
    // Raises search_requested with the current text, dropping the pending
    // delay.
    void raise_search();
    // Starts the search delay again (or searches at once without one).
    void schedule_search();
    void editor_text_changed(const std::string& value);
    void editor_committed(const std::string& value);
    void editor_cancelled();
    void editor_focus_changed(bool focused);
    // Whether the busy strip's segment sweeps now (busy, in a window whose
    // ambient motion is not suppressed).
    [[nodiscard]] bool busy_moves() const noexcept;
    // Enlists the field on its window's motion clock while the strip
    // sweeps (`painting`: from on_paint; otherwise only an enlisted field
    // is updated), and withdraws it otherwise.
    void update_busy_ambient(bool painting);

    std::shared_ptr<TextBox> editor_{};
    SubscriptionToken editor_text_;
    SubscriptionToken editor_commit_;
    SubscriptionToken editor_cancel_;
    SubscriptionToken editor_focus_;
    FrameRequestToken search_timer_{};
    Event<const std::string&> text_changed_;
    Event<const std::string&> search_requested_;
    Event<> cleared_;
    // The editor's natural height from the last measure.
    double editor_height_{};
    // The sweep's start on the motion clock, its phase from 0 to 1, and the
    // paints counted so that a field no longer drawn drops off the clock.
    FrameTime busy_start_{};
    double busy_phase_{};
    std::uint64_t busy_paints_{};
    std::uint64_t busy_paints_seen_{};
    bool editor_focused_{};
    bool hot_clear_{};
    bool pressed_clear_{};
    // property_codemod begin SearchField (tools/property_families/search_field.json; generated, do not edit)
public:
    // SearchField's property table: Control's entries, then its own.
    static constexpr std::size_t search_field_property_count = Control::control_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, search_field_property_count>& search_field_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_text_changed();

private:
    friend struct SearchFieldTextCapability<SearchField>;
    friend struct SearchFieldPlaceholderTextCapability<SearchField>;
    friend struct SearchFieldSearchDelayCapability<SearchField>;
    friend struct SearchFieldBusyCapability<SearchField>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(text)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(text)& cell) const;
    void write_view(const decltype(placeholder_text)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(placeholder_text)& cell) const;
    void prepare_write(const decltype(search_delay)& cell, std::chrono::milliseconds& incoming);
    void written(const decltype(search_delay)& cell);
    void prepare_write(const decltype(busy)& cell, bool& incoming);
    void written(const decltype(busy)& cell);
    // property_codemod end SearchField
};

} // namespace gui_forms
