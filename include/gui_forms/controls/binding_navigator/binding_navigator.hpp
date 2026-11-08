#pragma once

#include "gui_forms/binding/binding_source/binding_source.hpp"
#include "gui_forms/controls/tool_strip/tool_strip.hpp"
#include "gui_forms/controls/binding_navigator/binding_navigator_capabilities/binding_navigator_capabilities.hpp"

#include <memory>
#include <optional>
#include <string>

namespace gui_forms {

// Raised by a BindingNavigator's add command. A handler supplies the record
// to add; without one nothing is added. The navigator adds it to the end of
// the binding source and moves to it.
struct BindingNavigatorAddNew final {
    std::string navigator_id{};
    std::optional<BindingRecord> record{};
};

// A ToolStrip that moves through a BindingSource (Windows Forms'
// BindingNavigator): first, previous, a position text box, the count, next,
// last, add and delete. Each action is a shared Command, so the same action
// can sit in a menu; its enabled state follows the source's position, count
// and permissions. Typing a position and pressing Enter moves there; Escape
// restores the shown position.
//
// The standard items have the IDs move_first, move_previous, position,
// count, move_next, move_last, add_new and delete, with separators between
// the groups. Replacing the items with set_items drops the ones left out.
class BindingNavigator final : public ToolStrip, public BindingNavigatorCapabilities<BindingNavigator> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit BindingNavigator(StableId stable_id);
    void initialize_control_tree();

    [[nodiscard]] const std::shared_ptr<Command>& move_first_command() const noexcept { return move_first_; }
    [[nodiscard]] const std::shared_ptr<Command>& move_previous_command() const noexcept { return move_previous_; }
    [[nodiscard]] const std::shared_ptr<Command>& move_next_command() const noexcept { return move_next_; }
    [[nodiscard]] const std::shared_ptr<Command>& move_last_command() const noexcept { return move_last_; }
    [[nodiscard]] const std::shared_ptr<Command>& add_new_command() const noexcept { return add_new_; }
    [[nodiscard]] const std::shared_ptr<Command>& delete_command() const noexcept { return delete_; }

    // Raised before the add command adds a record; see BindingNavigatorAddNew.
    [[nodiscard]] Event<BindingNavigatorAddNew&>& adding_new() noexcept { return adding_new_; }

    void on_key_preview(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    // Subscribes `target` to one action command's invocations; the token
    // joins action_tokens_, so the subscription lives as long as the
    // navigator.
    void connect_action(Command& command, Delegate<const CommandInvocation&> target);
    void refresh();
    void commit_position();
    void on_move_first(const CommandInvocation& invocation);
    void on_move_previous(const CommandInvocation& invocation);
    void on_move_next(const CommandInvocation& invocation);
    void on_move_last(const CommandInvocation& invocation);
    void on_add_new(const CommandInvocation& invocation);
    void on_delete(const CommandInvocation& invocation);
    void on_list_changed(const BindingListChange& change);
    void on_position_changed(std::ptrdiff_t position);
    void on_source_disposed();

    std::shared_ptr<BindingSource> source_{};
    std::shared_ptr<Command> move_first_{};
    std::shared_ptr<Command> move_previous_{};
    std::shared_ptr<Command> move_next_{};
    std::shared_ptr<Command> move_last_{};
    std::shared_ptr<Command> add_new_{};
    std::shared_ptr<Command> delete_{};
    std::vector<SubscriptionToken> action_tokens_{};
    SubscriptionToken list_changed_{};
    SubscriptionToken position_changed_{};
    SubscriptionToken source_disposed_{};
    Event<BindingNavigatorAddNew&> adding_new_{};
    // property_codemod begin BindingNavigator (tools/property_families/binding_navigator.json; generated, do not edit)
private:
    friend struct BindingNavigatorBindingSourceCapability<BindingNavigator>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(binding_source)& cell, std::shared_ptr<BindingSource> incoming);
    [[nodiscard]] std::shared_ptr<BindingSource> read_view(const decltype(binding_source)& cell) const;
    // property_codemod end BindingNavigator
};

} // namespace gui_forms
