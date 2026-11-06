#include "gui_forms/controls/collections/choice_group.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "../panel/panel_anatomy.hpp"

#include <array>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {
class ChoiceButton final : public Button {
public:
    explicit ChoiceButton(StableId id) : Button(std::move(id)) {}
    SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor = Button::semantic_descriptor();
        descriptor.role = SemanticRole::radio_button;
        if (selected()) descriptor.states |= SemanticState::checked;
        return descriptor;
    }
};
} // namespace

struct ChoiceGroup::Entry final {
    int id{};
    std::shared_ptr<Button> button{};
    SubscriptionToken click{};
    SubscriptionToken enabled{};
    SubscriptionToken visible{};
    SubscriptionToken focus{};
};
struct ChoiceGroup::Revision final {
    std::vector<ChoiceItem> items{};
    std::vector<std::shared_ptr<Entry>> entries{};
};
struct ChoiceGroup::ItemClick final {
    ChoiceGroup& group;
    int id;
    void operator()(ButtonBase&) const { group.activate_item(id); }
};

struct ChoiceGroup::ItemAvailability final {
    ChoiceGroup& group;
    void operator()(bool) const { group.refresh_tab_stop(); }
};

void ChoiceGroup::refresh_tab_stop() {
    if (!revision_ || replacing_) return;
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> revision = revision_;
    const Control::Ptr focused = window() == nullptr ? Control::Ptr{} : (*window()).focused_control();
    int selected = -1;
    for (std::size_t index = 0U; index < (*revision).entries.size(); ++index) {
        const std::shared_ptr<Button> button = (*(*revision).entries[index]).button;
        if (!(*button).enabled.read() || !(*button).visible.read()) continue;
        if (selected < 0 || (*button).tab_stop.read()) selected = static_cast<int>(index);
        if (button == focused) { selected = static_cast<int>(index); break; }
    }
    for (std::size_t index = 0U; index < (*revision).entries.size(); ++index) {
        (*(*(*revision).entries[index]).button).tab_stop = static_cast<int>(index) == selected;
    }
}

ChoiceGroup::ChoiceGroup(StableId id) : Panel(std::move(id)) {
    set_focusable(false);
    tab_stop = false;
    on(selection_.validating(), *this, &ChoiceGroup::validate_local_selection);
    on(selection_.changed(), *this, &ChoiceGroup::apply_selection);
}
ChoiceGroup::~ChoiceGroup() = default;

std::span<const ChoiceItem> ChoiceGroup::items() const noexcept {
    if (!revision_) return {};
    return (*revision_).items;
}
Button& ChoiceGroup::item(const int index) const {
    if (!revision_ || index < 0 || static_cast<std::size_t>(index) >= (*revision_).entries.size()) {
        throw std::out_of_range("ChoiceGroup item index");
    }
    const Entry& entry = *(*revision_).entries[static_cast<std::size_t>(index)];
    return *entry.button;
}
Control* ChoiceGroup::part(const std::string_view name, const int index) const {
    if (name != "item") return nullptr;
    Control* const result = &item(index);
    return result;
}
void ChoiceGroup::set_selected_index(const int index) {
    require_mutable();
    selection_.set(index);
}
void ChoiceGroup::validate_selection(const int index) const {
    const std::size_t count = revision_ ? (*revision_).items.size() : 0U;
    if (index < -1 || (index >= 0 && static_cast<std::size_t>(index) >= count)) {
        throw std::out_of_range("ChoiceGroup selection is outside its items");
    }
}
void ChoiceGroup::validate_local_selection(const int index) const {
    validate_selection(index);
    if (binding_) (*binding_).validate_update(index);
}
void ChoiceGroup::bind(Value<int>& selection) {
    require_mutable();
    const Control::Ptr retained = shared_from_this();
    std::unique_ptr<detail::ScalarBinding<ChoiceGroup, int>> binding =
        std::make_unique<detail::ScalarBinding<ChoiceGroup, int>>(
            *this, selection, changed(), &ChoiceGroup::selected_index,
            &ChoiceGroup::set_selected_index, &ChoiceGroup::validate_selection);
    binding_ = std::move(binding);
    (*binding_).synchronize();
}
void ChoiceGroup::unbind() noexcept { binding_.reset(); }

void ChoiceGroup::apply_selection(const int selected) {
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> revision = revision_;
    if (!revision) return;
    if (selected < -1 || (selected >= 0 && static_cast<std::size_t>(selected) >= (*revision).entries.size())) {
        throw std::out_of_range("ChoiceGroup selection is outside its items");
    }
    // With nothing selected the first item is the tab stop.
    const int tab_stop = selected < 0 ? 0 : selected;
    for (std::size_t index = 0U; index < (*revision).entries.size(); ++index) {
        Button& button = *(*(*revision).entries[index]).button;
        button.set_selected(static_cast<int>(index) == selected);
        button.tab_stop = static_cast<int>(index) == tab_stop;
        if (!is_alive() || revision_ != revision) return;
    }
    refresh_tab_stop();
    invalidate(Dirty::paint | Dirty::semantics);
}

void ChoiceGroup::set_items(std::vector<ChoiceItem> items) {
    require_mutable();
    if (replacing_) throw std::logic_error("ChoiceGroup cannot replace items recursively");
    if (items.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("too many ChoiceGroup items");
    }
    for (std::size_t index = 0U; index < items.size(); ++index) {
        const ChoiceItem& value = items[index];
        if (!validate_utf8(value.label).valid() || !validate_utf8(value.glyph).valid() ||
            !validate_utf8(value.accessible_name).valid()) throw std::invalid_argument("ChoiceGroup text must be UTF-8");
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (items[earlier].id == value.id) throw std::invalid_argument("duplicate ChoiceGroup item id");
        }
    }
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> next = std::make_shared<Revision>();
    (*next).items = std::move(items);
    (*next).entries.reserve((*next).items.size());
    for (const ChoiceItem& value : (*next).items) {
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
            (*entry).button = make_control<ChoiceButton>(StableId(id));
            (*entry).click = (*(*entry).button).clicked().subscribe(*this, ItemClick{*this, value.id});
        }
        (*next).entries.push_back(std::move(entry));
    }
    replacing_ = true;
    try {
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
                    static_cast<void>(remove_child((*(*entry).button).runtime_id()));
                }
            }
        }
        for (std::size_t index = 0U; index < (*next).items.size(); ++index) {
            const ChoiceItem& value = (*next).items[index];
            Button& button = *(*(*next).entries[index]).button;
            const std::string text = value.glyph.empty() ? value.label : value.glyph + " " + value.label;
            button.text = text;
            button.set_image(value.image);
            button.accessible_name = value.accessible_name.empty() ? value.label : value.accessible_name;
            button.tab_index = static_cast<std::uint32_t>(index);
            if (!button.parent()) add_child((*(*next).entries[index]).button);
        }
        for (std::size_t index = 0U; index < (*next).entries.size(); ++index) {
            const Button& button = *(*(*next).entries[index]).button;
            static_cast<void>(set_child_index(button.runtime_id(), (*next).entries.size() - index - 1U));
        }
        const int current = selected_index();
        if (current >= static_cast<int>((*next).items.size())) selection_.set(-1);
        else apply_selection(current);
        invalidate(Dirty::layout | Dirty::paint | Dirty::semantics);
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

void ChoiceGroup::activate_item(const int id) {
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> revision = revision_;
    if (!revision || replacing_) return;
    for (std::size_t index = 0U; index < (*revision).items.size(); ++index) {
        if ((*revision).items[index].id != id) continue;
        const int selected = static_cast<int>(index);
        set_selected_index(selected);
        if (is_alive() && revision_ == revision) activated_.emit(selected);
        return;
    }
}
void ChoiceGroup::arrange(const Rect bounds) {
    arrange_self(bounds);
    const std::shared_ptr<Revision> revision = revision_;
    if (!revision || (*revision).entries.empty()) return;
    const double width = bounds.width / static_cast<double>((*revision).entries.size());
    for (std::size_t index = 0U; index < (*revision).entries.size(); ++index) {
        set_child_layout((*(*revision).entries[index]).button,
            {static_cast<double>(index) * width, 0.0, width, bounds.height});
    }
}
void ChoiceGroup::on_key_bubble(KeyEvent& event) { on_key(event); }
void ChoiceGroup::on_key(KeyEvent& event) {
    if (event.handled || event.action != KeyAction::down || !enabled.read() || !revision_) return;
    const bool backward = event.physical_key == PhysicalKey::left || event.physical_key == PhysicalKey::up;
    const bool forward = event.physical_key == PhysicalKey::right || event.physical_key == PhysicalKey::down;
    if (!backward && !forward) return;
    const int count = static_cast<int>((*revision_).items.size());
    if (count == 0) return;
    int next = std::max(0, selected_index());
    const std::shared_ptr<Revision> revision = revision_;
    for (int attempt = 0; attempt < count; ++attempt) {
        if (backward) next = next == 0 ? count - 1 : next - 1;
        else next = next + 1 == count ? 0 : next + 1;
        const std::shared_ptr<Button> button = (*(*revision).entries[static_cast<std::size_t>(next)]).button;
        if (!(*button).enabled.read() || !(*button).visible.read()) continue;
        const int id = (*revision).items[static_cast<std::size_t>(next)].id;
        event.handled = true;
        const Control::Ptr retained = shared_from_this();
        activate_item(id);
        if (is_alive() && revision_ == revision && window() != nullptr) {
            static_cast<void>((*window()).request_focus(button));
        }
        return;
    }
}
SemanticDescriptor ChoiceGroup::semantic_descriptor() const {
    SemanticDescriptor descriptor = Panel::semantic_descriptor();
    descriptor.role = SemanticRole::radio_group;
    descriptor.exposed = true;
    return descriptor;
}


namespace {

// Its members are child controls, which publish their own anatomies.
constexpr ControlAnatomy choice_group_anatomy{
    .control_id = "choice_group",
    .parts = panel_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& ChoiceGroup::anatomy() const noexcept {
    return choice_group_anatomy;
}

namespace {

// The events ChoiceGroup adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 2> choice_group_interactions{{
    {.name = "changed", .payload = "int", .mutable_payload = false},
    {.name = "activated", .payload = "int", .mutable_payload = false},
}};

} // namespace

void ChoiceGroup::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), choice_group_interactions.begin(), choice_group_interactions.end());
}

} // namespace gui_forms
