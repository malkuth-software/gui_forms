#pragma once

#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/controls/panel/list_box/list_box.hpp"
#include "gui_forms/controls/panel/text_box/auto_complete/auto_complete.hpp"
#include "gui_forms/controls/range_control/scroll_bar/scroll_bar.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace gui_forms {

// The list a ComboBox or a TextBox's suggestions drop down. Besides
// ListBox's row parts, each row publishes a `match` part: the matched text,
// highlighted under the label (see match_bounds).
class DropDownList final : public ListBox {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit DropDownList(StableId stable_id);
    void initialize_control_tree();
    void arrange(Rect bounds) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    bool on_semantic_child_action(std::string_view id, SemanticAction action,
                                  std::string_view value) override;
    // Matches the scroll bar to the rows after the owner moves the selection.
    void synchronize_scroll();
    // One match per row, by row; missing rows match nothing. A match that
    // runs past its row's text is clamped to it.
    void set_matches(std::vector<TextMatchRange> matches);
    [[nodiscard]] std::span<const TextMatchRange> matches() const& noexcept { return matches_; }
    std::span<const TextMatchRange> matches() const&& = delete;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    struct ItemParts final {
        PartGeometry row;
        PartGeometry label;
        PartGeometry row_focus;
        PartGeometry match;
    };
    // A realized row's parts, as ListBox::realized_items reads its own.
    [[nodiscard]] ItemParts drop_down_item_parts(std::size_t item) const;

protected:
    void paint_row_adornment(Painter& painter, std::size_t index, Rect row_bounds,
                             bool selected, bool focused) const override;
    void publish_realized_item_geometry(std::size_t item,
                                        std::span<PartGeometry> geometry) const override;

private:
    // The matched text of row `index`, whose row rectangle is `row`, in the
    // list's coordinates: the paint and the published part both use it.
    // Empty when the row matches nothing or is selected (the selected row's
    // own plate already marks it).
    [[nodiscard]] Rect match_bounds(std::size_t index, Rect row) const;
    std::vector<TextMatchRange> matches_;
    void scroll_changed(double value);
    std::shared_ptr<ScrollBar> scrollbar_;
    SubscriptionToken scroll_subscription_;
    bool synchronizing_ = false;
};

class DropDownLayer final : public Panel {
public:
    explicit DropDownLayer(StableId stable_id);
    [[nodiscard]] Event<>& dismissed() noexcept { return dismissed_; }
    void on_pointer(PointerEvent& event) override;
    void on_key_preview(KeyEvent& event) override;

private:
    Event<> dismissed_;
};

} // namespace gui_forms
