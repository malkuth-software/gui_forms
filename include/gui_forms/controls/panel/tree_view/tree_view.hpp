#pragma once

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/scheduler.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

struct TreeViewItem final {
    std::string stable_id;
    GUI_FORMS_LOCALIZABLE(text) std::string text;
    std::size_t depth{};
    bool expandable{};
    bool expanded{};
    bool enabled{true};
    std::string image_key;
};

struct TreeSelectionChange final {
    std::string previous_id;
    std::string current_id;
};

struct TreeExpansionChange final {
    std::string stable_id;
    bool expanded{};
};

// A retained, flat-model tree. Only visible rows are painted and published as
// semantic children; item identity and expansion state remain stable while
// descendants are unrealized.
class TreeView final : public Panel {
public:
    explicit TreeView(StableId stable_id);

    [[nodiscard]] std::span<const TreeViewItem> items() const noexcept {
        return items_;
    }
    void set_items(std::vector<TreeViewItem> items);
    [[nodiscard]] std::string_view selected_id() const noexcept {
        return selected_id_;
    }
    void set_selected_id(std::string_view stable_id);
    [[nodiscard]] std::string_view active_id() const noexcept {
        return active_id_;
    }
    [[nodiscard]] bool expanded(std::string_view stable_id) const;
    void set_expanded(std::string_view stable_id, bool expanded);
    [[nodiscard]] double item_height() const noexcept { return item_height_; }
    void set_item_height(double height);
    [[nodiscard]] double indentation() const noexcept { return indentation_; }
    void set_indentation(double indentation);
    [[nodiscard]] bool show_expanders() const noexcept { return show_expanders_; }
    void set_show_expanders(bool show);
    [[nodiscard]] std::size_t top_row() const noexcept { return top_row_; }
    void set_top_row(std::size_t row);
    [[nodiscard]] FontSpec font() const noexcept { return font_; }
    void set_font(FontSpec font);
    [[nodiscard]] std::shared_ptr<ImageList> image_list() const noexcept {
        return image_list_;
    }
    void set_image_list(std::shared_ptr<ImageList> image_list);

    [[nodiscard]] Event<const TreeSelectionChange&>& selection_changed() noexcept {
        return selection_changed_;
    }
    [[nodiscard]] Event<const TreeExpansionChange&>& expansion_changed() noexcept {
        return expansion_changed_;
    }
    [[nodiscard]] Event<const std::string&>& item_activated() noexcept {
        return item_activated_;
    }

    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_text_input(TextInputEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id,
                                  SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(
        std::size_t item) const override;
    // Stage T1 anatomy, as dot members.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry content;
    };
    [[nodiscard]] Parts parts() const;
    struct ItemParts final {
        PartGeometry row;
        PartGeometry expander;
        PartGeometry icon;
        PartGeometry label;
        PartGeometry row_focus;
    };
    struct ItemStates final {
        bool selected{};
        bool hot{};
        bool current{};
        bool open{};
        bool closed{};
        bool disabled{};
    };
    struct Item final {
        std::size_t index{};
        std::string stable_id;
        ItemParts parts;
        ItemStates states;
    };
    // The items currently realized, in paint order (owner decision 5).
    [[nodiscard]] std::vector<Item> realized_items() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_attached_to_window() override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void publish_realized_item_geometry(std::size_t item,
                                        std::span<PartGeometry> geometry) const override;

private:
    void on_image_list_changed(const ImageListChange& change);
    void rebuild_visible();
    [[nodiscard]] std::optional<std::size_t> item_index(
        std::string_view stable_id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> visible_row_at(
        Point absolute) const noexcept;
    [[nodiscard]] std::size_t visible_row_count() const noexcept;
    void ensure_visible(std::size_t visible_row);
    void select_visible_row(std::size_t visible_row, bool activate);
    void type_select(std::string_view text);

    std::vector<TreeViewItem> items_;
    std::vector<std::size_t> visible_;
    std::string selected_id_;
    std::string active_id_;
    std::optional<std::size_t> hovered_row_;
    std::optional<std::size_t> pressed_row_;
    std::uint32_t pressed_click_count_{1U};
    bool pressed_expander_{};
    std::size_t top_row_{};
    double item_height_{24.0};
    double indentation_{18.0};
    bool show_expanders_{true};
    FontSpec font_{FontRole::content, 12.0, 400, false};
    std::string type_prefix_;
    std::chrono::steady_clock::time_point last_type_time_{};
    bool focused_{};
    std::shared_ptr<ImageList> image_list_;
    SubscriptionToken image_list_changed_;
    Event<const TreeSelectionChange&> selection_changed_;
    Event<const TreeExpansionChange&> expansion_changed_;
    Event<const std::string&> item_activated_;
};


} // namespace gui_forms
