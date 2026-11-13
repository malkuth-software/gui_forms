#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/value.hpp"
#include "gui_forms/controls/expandable_sections_capabilities.hpp"
#include "gui_forms/animation/animation_timer/animation_timer.hpp"

#include <chrono>
#include <memory>
#include <span>
#include <vector>

namespace gui_forms {

struct GUI_FORMS_API SectionItem final {
    int id{};
    GUI_FORMS_LOCALIZABLE(text) std::string label{};
    Control::Ptr content{};
    Value<bool>* expanded{}; // Borrowed; a missing model uses retained local state.
};

// How long a section takes to open (its body revealed from the top) and to
// close (the space it leaves closing up).
inline constexpr FrameInterval section_reveal_open = std::chrono::milliseconds(160);
inline constexpr FrameInterval section_reveal_close = std::chrono::milliseconds(120);

// Each section's body sits in a private clipping host (roadmap row 2.6, the
// Dear ImGui study's collapsing-header transition): the body keeps its full
// height and layout, and only the host's height moves. Opening shows the
// body at once (focus and semantics see it) and grows the host from 0 to
// the body's height; closing hides the body at once and closes the host's
// space. The motion runs on a paint-gated timer (ADR-016) and jumps to its
// end under reduced motion, in an occluded window or with no window. The
// host takes no focus and publishes no semantic node of its own.
class GUI_FORMS_API ExpandableSections final : public Panel, public ExpandableSectionsCapabilities<ExpandableSections> {
public:
    explicit ExpandableSections(StableId id);
    ~ExpandableSections() override;
    void set_items(std::vector<SectionItem> items);
    // Borrowed: the next set_items ends the views lent so far (the sanitizer
    // tree reports a later use).
    [[nodiscard]] BorrowedSpan<SectionItem> items() const noexcept;
    [[nodiscard]] bool expanded(int index) const;
    void set_expanded(int index, bool expanded);
    [[nodiscard]] Event<int, bool>& toggled() noexcept { return toggled_; }
    [[nodiscard]] Control* part(std::string_view name, int index) const;
    void arrange(Rect bounds) override;
    // How far section `index`'s host is open, 0 to 1 (std::out_of_range
    // for a bad index), and whether a reveal is moving.
    [[nodiscard]] double section_reveal(int index) const;
    [[nodiscard]] bool revealing() const noexcept { return reveal_frames_.started(); }
    void on_detached_from_window() noexcept override;
    // The panel's paint; the reveal timer waits for it after each tick.
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    struct Entry;
    struct Revision;
    struct HeadingChange;
    [[nodiscard]] Entry& entry(int index) const;
    void on_toggled(int id, bool expanded);
    // The reveal timer's target. Observes the control.
    struct RevealTick final {
        std::weak_ptr<ExpandableSections> target;
        AnimationTickResult operator()(FrameTime now) const;
    };
    // Moves `item`'s host toward open or closed; at once when motion is
    // suppressed. Starts the timer when it moves.
    void start_reveal(Entry& item, bool open);
    // One tick: every moving host advances; the timer stops when none does.
    AnimationTickResult on_reveal_tick(FrameTime now);
    // Ends every reveal at its target and stops the timer.
    void complete_reveals();
    std::shared_ptr<Revision> revision_{};
    Event<int, bool> toggled_{};
    bool replacing_{};
    AnimationTimer reveal_frames_{};
    // property_codemod begin ExpandableSections (tools/property_families/expandable_sections.json; generated, do not edit)
private:
    friend struct ExpandableSectionsSingleOpenCapability<ExpandableSections>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(single_open)& cell, bool& incoming);
    void written(const decltype(single_open)& cell);
    // property_codemod end ExpandableSections
};

} // namespace gui_forms
