#pragma once

#include "gui_forms/commands.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/controls/menu_strip/menu_strip_capabilities/menu_strip_capabilities.hpp"

namespace gui_forms {

namespace detail {
struct MenuStripMergeHistory;
class StripMergeAccess;
}

struct MenuStripItemSpec final {
    std::string stable_id{};
    GUI_FORMS_LOCALIZABLE(text) std::string text{};
    std::vector<MenuItemSpec> items{};
    bool enabled{true};
    bool visible{true};
    // How the menu joins another menu strip when its strip merges
    // (MergeAction, merge_strips), and its position there; none matches by
    // text only.
    MergeAction merge_action{MergeAction::append};
    std::optional<std::size_t> merge_index{};
};

struct MenuStripInvocation final {
    std::string menu_strip_id{};
    std::string top_level_id{};
    MenuItemInvocation item{};
};

// Retained top-level application menu. The bar stays in ordinary layout; its
// active menu is presented by ContextMenu as a separate window overlay root.
// Pointer, keyboard, and semantic activation converge on the same shared
// Command instances contained by MenuItemSpec.
class MenuStrip final : public Control, public MenuStripCapabilities<MenuStrip> {
public:
    explicit MenuStrip(StableId stable_id);
    ~MenuStrip() override;

    [[nodiscard]] const std::vector<MenuStripItemSpec>& items() const noexcept {
        return items_;
    }
    void set_items(std::vector<MenuStripItemSpec> items);
    [[nodiscard]] std::optional<std::size_t> active_index() const noexcept {
        return active_index_;
    }
    // Unknown modes throw invalid_argument before reusing or replacing an overlay.
    bool open(const std::size_t index, const MenuOpenMode mode = MenuOpenMode::keyboard);
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;

    [[nodiscard]] Event<const MenuStripInvocation&>& item_invoked() noexcept {
        return item_invoked_;
    }
    [[nodiscard]] Event<std::optional<std::size_t>>& open_changed() noexcept {
        return open_changed_;
    }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
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
    // Stage T1 anatomy, as dot members; its states are Control's.
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry divider;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;
    struct ItemParts final {
        PartGeometry menu_item;
        PartGeometry menu_label;
        PartGeometry menu_focus;
    };
    struct ItemStates final {
        bool hot{};
        bool open{};
        bool selected{};
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
    void on_dispose() noexcept override;
    [[nodiscard]] bool mnemonic_matches(
        char32_t character) const noexcept override;
    bool process_mnemonic_self(char32_t character) override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void publish_realized_item_geometry(std::size_t item,
                                        std::span<PartGeometry> geometry) const override;
    // The model position of a realized (visible) top-level item.
    [[nodiscard]] std::size_t realized_item_index(std::size_t item) const noexcept;

private:
    friend class detail::StripMergeAccess;
    // set_items' work; a merge may leave a menu empty.
    void apply_items(std::vector<MenuStripItemSpec> items, bool merged);
    [[nodiscard]] std::vector<Rect> item_bounds() const;
    [[nodiscard]] double preferred_height() const;
    [[nodiscard]] std::optional<std::size_t> index_at(Point window_point) const;
    [[nodiscard]] std::optional<std::size_t> next_enabled(
        std::size_t start, int direction) const;
    bool navigate_root(int direction);
    bool handle_overlay_pointer(const PointerEvent& event);
    void on_overlay_item_invoked(const MenuItemInvocation& invocation);
    void on_overlay_open_changed(bool open_state);
    void set_hot(std::optional<std::size_t> index);

    std::vector<MenuStripItemSpec> items_{};
    std::unique_ptr<ContextMenu> overlay_{};
    std::optional<std::size_t> active_index_{};
    std::optional<std::size_t> hot_index_{};
    std::string selected_item_id_{};
    bool focused_{};
    bool switching_{};
    // The merges into this strip, oldest first; none until the first.
    std::unique_ptr<detail::MenuStripMergeHistory> merge_history_{};
    SubscriptionToken overlay_invoked_{};
    SubscriptionToken overlay_changed_{};
    Event<const MenuStripInvocation&> item_invoked_{};
    Event<std::optional<std::size_t>> open_changed_{};
    // property_codemod begin MenuStrip (tools/property_families/menu_strip.json; generated, do not edit)
private:
    friend struct MenuStripUseMnemonicCapability<MenuStrip>;
    friend struct MenuStripItemPaddingCapability<MenuStrip>;
    friend struct FontCapability<MenuStrip>;
    friend struct MenuStripAllowMergeCapability<MenuStrip>;
    friend struct MenuStripSelectedItemIdCapability<MenuStrip>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(use_mnemonic)& cell, bool& incoming);
    void written(const decltype(use_mnemonic)& cell);
    void prepare_write(const decltype(item_padding)& cell, double& incoming);
    void written(const decltype(item_padding)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    void prepare_write(const decltype(allow_merge)& cell, bool& incoming);
    void written(const decltype(allow_merge)& cell);
    void write_view(const decltype(selected_item_id)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(selected_item_id)& cell) const;
    // property_codemod end MenuStrip
};

} // namespace gui_forms
