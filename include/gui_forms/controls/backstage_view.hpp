#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/commands.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/controls/backstage_view_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// A Backstage column entry (stage RB, phase 6; owner decisions 3, 26): a fast
// command, which runs its Command (and, when `definitive`, closes Backstage
// afterwards), or a topic, which shows its `page` beside the column. `text`
// (with an optional mnemonic) is the label; a command takes its Command's
// text when it is empty.
enum class BackstageItemKind : std::uint8_t { command, topic };
struct GUI_FORMS_API BackstageItemSpec final {
    std::string stable_id{};
    BackstageItemKind kind{BackstageItemKind::command};
    std::shared_ptr<Command> command{};
    GUI_FORMS_LOCALIZABLE(text) std::string text{};
    ImageId image{};
    bool definitive{};
    Control::Ptr page{};
};

// The selected topic after it changed.
struct GUI_FORMS_API BackstageTopicChange final {
    std::string backstage_id{};
    std::size_t item{};
    std::string topic_id{};
};

// The File tab's full page (stage RB, phase 6; owner decisions 3, 26, after
// Office 2010): a column of fast commands and topics down the left, and the
// selected topic's page beside it, a child control the application made.
// A Ribbon given a BackstageView opens it from the File tab over everything
// below its tab row; Escape, the File tab, another tab, or a definitive
// command returns to the document. The view only asks to close
// (close_requested); its owner closes it.
//
// The column and page are told apart by their surfaces (decision 26: the
// contrast between them helps), the column on the card family and the page
// on the window family until the default theme (T4) gives the column its own.
class GUI_FORMS_API BackstageView final : public Control, public BackstageViewCapabilities<BackstageView> {
public:
    // Development bounds, not measured capacity.
    static constexpr std::size_t maximum_items = 64U;

    explicit BackstageView(StableId stable_id);

    [[nodiscard]] BorrowedSpan<BackstageItemSpec> items() const& noexcept { return detail::lend_view(items_); }
    BorrowedSpan<BackstageItemSpec> items() const&& = delete;
    // Replaces the column. The selection stays on a topic with the same ID,
    // else the first topic. Throws std::invalid_argument, keeping the old
    // items, for an empty or repeated ID, invalid UTF-8, a command without a
    // Command, a topic without a page or text, a page given twice or owned by
    // another parent, a page or definitive flag on a command or topic that
    // takes none, or more than maximum_items.
    void set_items(std::vector<BackstageItemSpec> items);

    // The one path every activation takes (pointer, keyboard, mnemonic and
    // accessibility): runs a command, or selects a topic. False for an
    // unknown ID or a disabled command.
    bool activate(std::string_view item_id);

    [[nodiscard]] Event<const BackstageTopicChange&>& topic_changed() noexcept { return topic_changed_; }
    // Raised for Escape and after a definitive command ran.
    [[nodiscard]] Event<>& close_requested() noexcept { return close_requested_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(std::size_t item) const override;

    // Stage T1 anatomy, as dot members; its states are Control's.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry column;
        PartGeometry page;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

    // The "backstage_item" item system: one column entry.
    struct GUI_FORMS_API ItemParts final {
        PartGeometry item_face;
        PartGeometry item_icon;
        PartGeometry item_label;
        PartGeometry item_focus;
    };
    struct GUI_FORMS_API ItemStates final {
        bool hot{};
        bool pressed{};
        bool selected{};
        bool disabled{};
        bool focus_visible{};
    };
    struct GUI_FORMS_API Item final {
        std::size_t index{};
        std::string stable_id;
        ItemParts parts;
        ItemStates states;
    };
    [[nodiscard]] std::vector<Item> realized_items() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    [[nodiscard]] bool mnemonic_matches(char32_t character) const noexcept override;
    bool process_mnemonic_self(char32_t character) override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] ItemSystemIndex publish_realized_item_system(std::size_t item) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;

private:
    struct Geometry final {
        Rect column{};
        Rect page{};
        std::vector<Rect> items;
    };
    struct CommandWatch final {
        BackstageView* view{};
        void operator()(const CommandState& state) const;
    };

    [[nodiscard]] Geometry geometry() const;
    [[nodiscard]] ItemParts item_parts(const Geometry& placed, std::size_t item) const;
    [[nodiscard]] AnatomyStateSet item_states(std::size_t item) const;
    // An item's text as authored, mnemonic marker included: its own, or its
    // Command's when it has none. A borrow of the argument (or of its
    // Command's state): temporaries are refused (house style §1).
    [[nodiscard]] static const std::string& authored_text(const BackstageItemSpec& item) noexcept;
    static const std::string& authored_text(BackstageItemSpec&& item) = delete;
    [[nodiscard]] std::string item_text(const BackstageItemSpec& item) const;
    [[nodiscard]] bool item_enabled(const BackstageItemSpec& item) const;
    [[nodiscard]] std::optional<std::size_t> item_at(Point local) const;
    void activate_item(std::size_t item);
    void select_topic(std::size_t item);
    void move_focus(bool forward);

    detail::LendingVector<BackstageItemSpec> items_{};
    std::vector<SubscriptionToken> watches_{};
    std::optional<std::size_t> selected_{};
    std::optional<std::size_t> hot_{};
    std::optional<std::size_t> pressed_{};
    std::optional<std::size_t> focus_item_{};
    bool focused_{};
    FontSpec font_{FontRole::control, 10.5, 400, false, 0.12};
    Event<const BackstageTopicChange&> topic_changed_{};
    Event<> close_requested_{};
    // property_codemod begin BackstageView (tools/property_families/backstage_view.json; generated, do not edit)
private:
    friend struct BackstageViewColumnWidthCapability<BackstageView>;
    friend struct BackstageViewSelectedTopicCapability<BackstageView>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(column_width)& cell, double& incoming);
    void written(const decltype(column_width)& cell);
    void write_view(const decltype(selected_topic)& cell, std::optional<std::size_t> incoming);
    [[nodiscard]] std::optional<std::size_t> read_view(const decltype(selected_topic)& cell) const;
    // property_codemod end BackstageView
};

} // namespace gui_forms
