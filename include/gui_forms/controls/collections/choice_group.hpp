#pragma once

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/controls/collections/choice_group_values/choice_group_values.hpp"
#include "gui_forms/controls/collections/choice_group_capabilities/choice_group_capabilities.hpp"
#include "gui_forms/detail/scalar_binding.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <span>
#include <vector>

namespace gui_forms {

struct ChoiceItem final {
    int id{};
    GUI_FORMS_LOCALIZABLE(text) std::string label{};
    std::string glyph{};
    ImageId image{};
    GUI_FORMS_LOCALIZABLE(accessible_name) std::string accessible_name{};
};

// A set of mutually exclusive choices, one button per item, with
// radio-group semantics and arrow-key selection. `presentation` draws them
// as a row of buttons or as a segmented control (owner decision D6): one
// track, the items as segments over it, and a selection indicator behind
// the selected segment. The segmented presentation adds the custom state
// `segmented` and publishes the `track` and `selection` parts; the
// indicator is a keyed part, so the theme's engine may move it between
// segments, and does not when motion is reduced.
class ChoiceGroup final : public Panel, public ChoiceGroupCapabilities<ChoiceGroup> {
public:
    explicit ChoiceGroup(StableId id);
    ~ChoiceGroup() override;
    void set_items(std::vector<ChoiceItem> items);
    [[nodiscard]] std::span<const ChoiceItem> items() const noexcept;
    void bind(Value<int>& selection);
    void unbind() noexcept;
    // Raised after a change of selected_index, with the new index.
    [[nodiscard]] Event<int>& changed() noexcept { return selected_index_changed_; }
    [[nodiscard]] Event<int>& activated() noexcept { return activated_; }
    [[nodiscard]] Button& item(int index) const;
    [[nodiscard]] Control* part(std::string_view name, int index) const;
    void arrange(Rect bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_key(KeyEvent& event) override;
    void on_key_bubble(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members: Panel's parts, then the segmented
    // presentation's track (the whole strip) and selection (the indicator
    // behind the selected segment). Both are absent in the buttons
    // presentation, and the selection while nothing is selected.
    struct Parts final {
        Panel::Parts panel;
        PartGeometry track;
        PartGeometry selection;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    struct Entry;
    struct Revision;
    struct ItemClick;
    struct ItemAvailability;
    void refresh_tab_stop();
    void activate_item(int id);
    void apply_selection(int index);
    void validate_selection(int index) const;
    void validate_local_selection(int index) const;
    // The bounds of the segment at `index` of `count` in a segmented group
    // of `size`, in local coordinates: shares of the track inside a small
    // inset whose edges are rounded to device pixels at the window's scale
    // (1 outside a window), so the segments tile with no gap or overlap and
    // their widths differ by at most one device pixel.
    [[nodiscard]] Rect segment_bounds(std::size_t index, std::size_t count, Size size) const noexcept;
    [[nodiscard]] Rect selection_bounds() const;
    // The selection binding's getter and setter (ScalarBinding names members).
    [[nodiscard]] int bound_selected_index() const noexcept { return selected_index.read(); }
    void write_bound_selected_index(int index);
    Event<int> selected_index_changed_;
    std::unique_ptr<detail::ScalarBinding<ChoiceGroup, int>> binding_{};
    std::shared_ptr<Revision> revision_{};
    Event<int> activated_{};
    bool replacing_{};
    // property_codemod begin ChoiceGroup (tools/property_families/choice_group.json; generated, do not edit)
public:
    // ChoiceGroup's property table: Panel's entries, then its own.
    static constexpr std::size_t choice_group_property_count = Panel::panel_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, choice_group_property_count>& choice_group_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct ChoiceGroupPresentationCapability<ChoiceGroup>;
    friend struct ChoiceGroupSelectedIndexCapability<ChoiceGroup>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(presentation)& cell, ChoiceGroupPresentation& incoming);
    void written(const decltype(presentation)& cell);
    void prepare_write(const decltype(selected_index)& cell, int& incoming);
    void written(const decltype(selected_index)& cell);
    // property_codemod end ChoiceGroup
};

} // namespace gui_forms
