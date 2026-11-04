#include "gui_forms/controls/collections/command_bar.hpp"
#include "gui_forms/text.hpp"
#include "../panel/panel_anatomy.hpp"

#include <array>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms {

struct CommandBar::Entry final {
    int id{};
    std::shared_ptr<Button> button{};
    SubscriptionToken click{};
    SubscriptionToken enabled{};
    SubscriptionToken visible{};
    SubscriptionToken focus{};
    AcceleratorToken shortcut{};
};

struct CommandBar::Revision final {
    std::vector<CommandItem> items{};
    std::vector<std::shared_ptr<Entry>> entries{};
};

struct CommandBar::ItemClick final {
    CommandBar& bar;
    int id;
    void operator()(ButtonBase&) const { bar.invoke_item(id); }
};

struct CommandBar::ItemAvailability final {
    CommandBar& group;
    void operator()(bool) const { group.refresh_tab_stop(); }
};

void CommandBar::refresh_tab_stop() {
    if (!revision_ || replacing_) return;
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> revision = revision_;
    const Control::Ptr focused = window() == nullptr ? Control::Ptr{} : (*window()).focused_control();
    int selected = -1;
    for (std::size_t index = 0U; index < (*revision).entries.size(); ++index) {
        const std::shared_ptr<Button> button = (*(*revision).entries[index]).button;
        if (!(*button).enabled() || !(*button).visible()) continue;
        if (selected < 0 || (*button).tab_stop()) selected = static_cast<int>(index);
        if (button == focused) { selected = static_cast<int>(index); break; }
    }
    for (std::size_t index = 0U; index < (*revision).entries.size(); ++index) {
        (*(*(*revision).entries[index]).button).set_tab_stop(static_cast<int>(index) == selected);
    }
}

CommandBar::CommandBar(StableId id) : Panel(std::move(id)) {
    set_focusable(false);
    set_tab_stop(false);
}
CommandBar::~CommandBar() = default;

std::span<const CommandItem> CommandBar::items() const noexcept {
    if (!revision_) return {};
    return (*revision_).items;
}

Button& CommandBar::item(const int index) const {
    if (!revision_ || index < 0 ||
        static_cast<std::size_t>(index) >= (*revision_).entries.size()) {
        throw std::out_of_range("CommandBar item index");
    }
    const Entry& entry = *(*revision_).entries[static_cast<std::size_t>(index)];
    return *entry.button;
}

Control* CommandBar::part(const std::string_view name, const int index) const {
    if (name != "item") return nullptr;
    Control* const control = &item(index);
    return control;
}

void CommandBar::set_items(std::vector<CommandItem> items) {
    require_mutable();
    if (replacing_) throw std::logic_error("CommandBar cannot replace items recursively");
    if (items.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("too many CommandBar items");
    }
    for (std::size_t index = 0U; index < items.size(); ++index) {
        const CommandItem& value = items[index];
        if (!validate_utf8(value.label).valid() || !validate_utf8(value.glyph).valid() ||
            !validate_utf8(value.accessible_name).valid()) {
            throw std::invalid_argument("CommandBar item text must be UTF-8");
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (items[earlier].id == value.id) throw std::invalid_argument("duplicate CommandBar item id");
        }
        if (value.shortcut && value.command == nullptr) {
            throw std::invalid_argument("CommandBar shortcuts require a Command");
        }
    }
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> next = std::make_shared<Revision>();
    (*next).items = std::move(items);
    (*next).entries.reserve((*next).items.size());
    for (const CommandItem& value : (*next).items) {
        std::shared_ptr<Entry> entry{};
        if (revision_) {
            for (const std::shared_ptr<Entry>& candidate : (*revision_).entries) {
                if ((*candidate).id == value.id) { entry = candidate; break; }
            }
        }
        if (!entry) {
            entry = std::make_shared<Entry>();
            (*entry).id = value.id;
            std::string id(stable_id().value());
            id += ".item.";
            id += std::to_string(value.id);
            (*entry).button = make_control<Button>(StableId(id));
        }
        (*next).entries.push_back(std::move(entry));
    }
    replacing_ = true;
    try {
        clear_shortcuts();
        const std::shared_ptr<Revision> previous = std::exchange(revision_, next);
        if (previous) {
            for (const std::shared_ptr<Entry>& entry : (*previous).entries) {
                const std::vector<std::shared_ptr<Entry>>::const_iterator found =
                    std::find((*next).entries.begin(), (*next).entries.end(), entry);
                if (found == (*next).entries.end()) {
                    (*entry).click.disconnect();
                    (*entry).enabled.disconnect();
                    (*entry).visible.disconnect();
                    (*entry).focus.disconnect();
                    (*(*entry).button).unbind_command();
                    static_cast<void>(remove_child((*(*entry).button).runtime_id()));
                }
            }
        }
        for (std::size_t index = 0U; index < (*next).items.size(); ++index) {
            const CommandItem& value = (*next).items[index];
            Button& button = *(*(*next).entries[index]).button;
            (*(*next).entries[index]).click.disconnect();
            button.unbind_command();
            const std::string text = value.glyph.empty() ? value.label : value.glyph + " " + value.label;
            button.set_text(text);
            button.set_accessible_name(value.accessible_name.empty() ? value.label : value.accessible_name);
            button.set_tab_stop(index == 0U);
            button.set_tab_index(static_cast<std::uint32_t>(index));
            if (value.command != nullptr) button.bind(*value.command);
            (*(*next).entries[index]).click = button.clicked().subscribe(
                *this, ItemClick{*this, value.id});
            if (!button.parent()) add_child((*(*next).entries[index]).button);
        }
        for (std::size_t index = 0U; index < (*next).entries.size(); ++index) {
            const Button& button = *(*(*next).entries[index]).button;
            static_cast<void>(set_child_index(button.runtime_id(), (*next).entries.size() - index - 1U));
        }
        install_shortcuts();
        invalidate(Dirty::measure | Dirty::layout | Dirty::paint | Dirty::semantics);
        for (const std::shared_ptr<Entry>& entry : (*next).entries) {
            Button& button = *(*entry).button;
            (*entry).enabled = button.enabled_changed().subscribe(*this, ItemAvailability{*this});
            (*entry).visible = button.visible_changed().subscribe(*this, ItemAvailability{*this});
            (*entry).focus = button.focus_observed().subscribe(*this, ItemAvailability{*this});
        }
        replacing_ = false;
        refresh_tab_stop();
    } catch (...) {
        replacing_ = false;
        throw;
    }
}

void CommandBar::invoke_item(const int id) {
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> revision = revision_;
    if (!revision || replacing_) return;
    for (const CommandItem& value : (*revision).items) {
        if (value.id == id) {
            // The revision keeps this reference valid if a handler replaces items.
            invoked_.emit(value);
            return;
        }
    }
}

void CommandBar::arrange(const Rect bounds) {
    arrange_self(bounds);
    if (!revision_) return;
    const std::shared_ptr<Revision> revision = revision_;
    const double count = static_cast<double>((*revision).entries.size());
    if (count == 0.0) return;
    const double width = bounds.width / count;
    for (std::size_t index = 0U; index < (*revision).entries.size(); ++index) {
        set_child_layout((*(*revision).entries[index]).button,
            {static_cast<double>(index) * width, 0.0, width, bounds.height});
    }
}

void CommandBar::on_key_bubble(KeyEvent& event) { on_key(event); }
void CommandBar::on_key(KeyEvent& event) {
    if (event.handled || event.action != KeyAction::down || !enabled() || !revision_) return;
    const bool backward = event.physical_key == PhysicalKey::left || event.physical_key == PhysicalKey::up;
    const bool forward = event.physical_key == PhysicalKey::right || event.physical_key == PhysicalKey::down;
    if (!backward && !forward) return; // Tab stays with the Window focus traversal.
    const std::shared_ptr<Revision> revision = revision_;
    const int count = static_cast<int>((*revision).entries.size());
    if (count == 0 || window() == nullptr) return;
    const Control::Ptr focused = (*window()).focused_control();
    int current = 0;
    for (int index = 0; index < count; ++index) {
        if ((*(*revision).entries[static_cast<std::size_t>(index)]).button == focused) current = index;
    }
    for (int attempt = 0; attempt < count; ++attempt) {
        if (backward) current = current == 0 ? count - 1 : current - 1;
        else current = current + 1 == count ? 0 : current + 1;
        const std::shared_ptr<Button> button = (*(*revision).entries[static_cast<std::size_t>(current)]).button;
        if (!(*button).enabled() || !(*button).visible()) continue;
        for (int index = 0; index < count; ++index) {
            (*(*(*revision).entries[static_cast<std::size_t>(index)]).button).set_tab_stop(index == current);
        }
        event.handled = (*window()).request_focus(button);
        return;
    }
}

SemanticDescriptor CommandBar::semantic_descriptor() const {
    SemanticDescriptor descriptor = Panel::semantic_descriptor();
    descriptor.role = SemanticRole::toolbar;
    descriptor.exposed = true;
    return descriptor;
}

void CommandBar::clear_shortcuts() noexcept {
    if (!revision_) return;
    for (const std::shared_ptr<Entry>& entry : (*revision_).entries) (*entry).shortcut.disconnect();
}
void CommandBar::install_shortcuts() {
    if (window() == nullptr || !revision_) return;
    for (std::size_t index = 0U; index < (*revision_).items.size(); ++index) {
        const CommandItem& value = (*revision_).items[index];
        Entry& entry = *(*revision_).entries[index];
        if (value.shortcut && (*entry.button).command_connected()) {
            entry.shortcut = (*value.command).bind_shortcut(*window(), *value.shortcut);
        }
    }
}
void CommandBar::on_attached_to_window() {
    Panel::on_attached_to_window();
    install_shortcuts();
}
void CommandBar::on_detaching_from_window(SurfaceRoot& former_window) noexcept {
    clear_shortcuts();
    Panel::on_detaching_from_window(former_window);
}


namespace {

// Its members are child controls, which publish their own anatomies.
constexpr ControlAnatomy command_bar_anatomy{
    .control_id = "command_bar",
    .parts = panel_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& CommandBar::anatomy() const noexcept {
    return command_bar_anatomy;
}

namespace {

// The events CommandBar adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 1> command_bar_interactions{{
    {.name = "invoked", .payload = "CommandItem", .mutable_payload = false},
}};

} // namespace

void CommandBar::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), command_bar_interactions.begin(), command_bar_interactions.end());
}

} // namespace gui_forms
