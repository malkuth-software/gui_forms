#include "gui_forms/controls/collections/expandable_sections.hpp"
#include "gui_forms/text.hpp"
#include "../panel/panel_anatomy.hpp"

#include <array>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {
bool contains_content(const std::vector<SectionItem>& items, const Control::Ptr& content) {
    for (const SectionItem& item : items) {
        if (item.content == content) return true;
    }
    return false;
}

class DisclosureButton final : public CheckBox {
public:
    explicit DisclosureButton(StableId id) : CheckBox(std::move(id)) {
        set_appearance(CheckBoxAppearance::button);
    }
    SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor = ButtonBase::semantic_descriptor();
        descriptor.role = SemanticRole::button;
        return descriptor;
    }
};
} // namespace

struct ExpandableSections::Entry final {
    int id{};
    std::shared_ptr<CheckBox> heading{};
    Control::Ptr content{};
    SubscriptionToken change{};
};
struct ExpandableSections::Revision final {
    std::vector<SectionItem> items{};
    std::vector<std::shared_ptr<Entry>> entries{};
};
struct ExpandableSections::HeadingChange final {
    ExpandableSections& sections;
    int id;
    void operator()(const bool expanded) const { sections.on_toggled(id, expanded); }
};

ExpandableSections::ExpandableSections(StableId id) : Panel(std::move(id)) {
    set_focusable(false);
    tab_stop = false;
}
ExpandableSections::~ExpandableSections() = default;
std::span<const SectionItem> ExpandableSections::items() const noexcept {
    if (!revision_) return {};
    return (*revision_).items;
}
ExpandableSections::Entry& ExpandableSections::entry(const int index) const {
    if (!revision_ || index < 0 || static_cast<std::size_t>(index) >= (*revision_).entries.size()) {
        throw std::out_of_range("ExpandableSections item index");
    }
    return *(*revision_).entries[static_cast<std::size_t>(index)];
}
bool ExpandableSections::expanded(const int index) const {
    const Entry& item = entry(index);
    const bool value = (*item.heading).checked();
    return value;
}
void ExpandableSections::set_expanded(const int index, const bool expanded) {
    require_mutable();
    Entry& item = entry(index);
    (*item.heading).set_checked(expanded);
}
Control* ExpandableSections::part(const std::string_view name, const int index) const {
    const Entry& item = entry(index);
    Control* control = nullptr;
    if (name == "header") control = item.heading.get();
    else if (name == "body") control = item.content.get();
    return control;
}
void ExpandableSections::set_single_open(const bool enabled) {
    require_mutable();
    if (single_open_ == enabled) return;
    single_open_ = enabled;
    if (!enabled || !revision_) return;
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> revision = revision_;
    bool found = false;
    for (const std::shared_ptr<Entry>& item : (*revision).entries) {
        if (!(*(*item).heading).checked()) continue;
        if (found) (*(*item).heading).set_checked(false);
        found = true;
        if (!is_alive() || revision_ != revision) return;
    }
}
void ExpandableSections::set_items(std::vector<SectionItem> items) {
    require_mutable();
    if (replacing_) throw std::logic_error("ExpandableSections cannot replace items recursively");
    if (items.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("too many sections");
    }
    for (std::size_t index = 0U; index < items.size(); ++index) {
        const SectionItem& value = items[index];
        if (!value.content || !validate_utf8(value.label).valid()) {
            throw std::invalid_argument("section requires content and a UTF-8 label");
        }
        const Control::Ptr parent = (*value.content).parent();
        if (parent && parent.get() != this) throw std::invalid_argument("section content already has a parent");
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (items[earlier].id == value.id || items[earlier].content == value.content ||
                (value.expanded != nullptr && items[earlier].expanded == value.expanded)) {
                throw std::invalid_argument("sections require distinct ids, content, and models");
            }
        }
    }
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> next = std::make_shared<Revision>();
    (*next).items = std::move(items);
    (*next).entries.reserve((*next).items.size());
    for (const SectionItem& value : (*next).items) {
        std::shared_ptr<Entry> item{};
        if (revision_) {
            for (const std::shared_ptr<Entry>& candidate : (*revision_).entries) {
                if ((*candidate).id == value.id) { item = candidate; break; }
            }
        }
        if (!item) {
            item = std::make_shared<Entry>();
            (*item).id = value.id;
            std::string id(stable_id().value());
            id += ".header.";
            id += std::to_string(value.id);
            (*item).heading = make_control<DisclosureButton>(StableId(id));
        }
        (*next).entries.push_back(std::move(item));
    }
    replacing_ = true;
    try {
        const std::shared_ptr<Revision> previous = std::exchange(revision_, next);
        if (previous) {
            for (const std::shared_ptr<Entry>& item : (*previous).entries) {
                (*item).change.disconnect();
                (*(*item).heading).unbind();
                const std::vector<std::shared_ptr<Entry>>::const_iterator found =
                    std::find((*next).entries.begin(), (*next).entries.end(), item);
                if (found == (*next).entries.end()) {
                    static_cast<void>(remove_child((*(*item).heading).runtime_id()));
                    if ((*item).content && !contains_content((*next).items, (*item).content)) {
                        static_cast<void>(remove_child((*(*item).content).runtime_id()));
                    }
                }
            }
        }
        bool opened = false;
        for (std::size_t index = 0U; index < (*next).items.size(); ++index) {
            const SectionItem& value = (*next).items[index];
            Entry& item = *(*next).entries[index];
            if (item.content && item.content != value.content &&
                !contains_content((*next).items, item.content)) {
                static_cast<void>(remove_child((*item.content).runtime_id()));
            }
            item.content = value.content;
            CheckBox& heading = *item.heading;
            heading.set_text(value.label);
            heading.tab_index = static_cast<std::uint32_t>(index * 2U);
            if (value.expanded != nullptr) heading.bind(*value.expanded);
            if (single_open_ && opened && heading.checked()) heading.set_checked(false);
            opened = opened || heading.checked();
            heading.set_expanded_state(heading.checked());
            (*item.content).visible = heading.checked();
            if (!heading.parent()) add_child(item.heading);
            if (!(*item.content).parent()) add_child(item.content);
            item.change = heading.checked_changed().subscribe(*this, HeadingChange{*this, value.id});
        }
        const std::size_t child_count = (*next).entries.size() * 2U;
        for (std::size_t index = 0U; index < (*next).entries.size(); ++index) {
            const Entry& item = *(*next).entries[index];
            static_cast<void>(set_child_index((*item.heading).runtime_id(), child_count - index * 2U - 1U));
            static_cast<void>(set_child_index((*item.content).runtime_id(), child_count - index * 2U - 2U));
        }
        invalidate(Dirty::layout | Dirty::paint | Dirty::semantics);
        replacing_ = false;
    } catch (...) {
        replacing_ = false;
        throw;
    }
}
void ExpandableSections::on_toggled(const int id, const bool expanded) {
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> revision = revision_;
    if (!revision || replacing_) return;
    for (std::size_t index = 0U; index < (*revision).entries.size(); ++index) {
        Entry& item = *(*revision).entries[index];
        if (item.id != id) continue;
        (*item.heading).set_expanded_state(expanded);
        if (!is_alive() || revision_ != revision) return;
        (*item.content).visible = expanded;
        if (!is_alive() || revision_ != revision) return;
        if (expanded && single_open_) {
            for (const std::shared_ptr<Entry>& other : (*revision).entries) {
                if ((*other).id != id) (*(*other).heading).set_checked(false);
                if (!is_alive() || revision_ != revision) return;
            }
        }
        invalidate(Dirty::layout | Dirty::paint | Dirty::semantics);
        toggled_.emit(static_cast<int>(index), expanded);
        return;
    }
}
void ExpandableSections::arrange(const Rect bounds) {
    arrange_self(bounds);
    const std::shared_ptr<Revision> revision = revision_;
    if (!revision) return;
    double top = 0.0;
    for (const std::shared_ptr<Entry>& item : (*revision).entries) {
        set_child_layout((*item).heading, {0.0, top, bounds.width, 28.0});
        top += 28.0;
        if ((*(*item).heading).checked()) {
            const double height = (*(*item).content).requested_bounds().height;
            set_child_layout((*item).content, {0.0, top, bounds.width, height});
            top += height;
        }
    }
}
SemanticDescriptor ExpandableSections::semantic_descriptor() const {
    SemanticDescriptor descriptor = Panel::semantic_descriptor();
    descriptor.role = SemanticRole::group;
    descriptor.exposed = true;
    return descriptor;
}


namespace {

// Its members are child controls, which publish their own anatomies.
constexpr ControlAnatomy expandable_sections_anatomy{
    .control_id = "expandable_sections",
    .parts = panel_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& ExpandableSections::anatomy() const noexcept {
    return expandable_sections_anatomy;
}

namespace {

// The events ExpandableSections adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 1> expandable_sections_interactions{{
    {.name = "toggled", .payload = "int, bool", .mutable_payload = false},
}};

} // namespace

void ExpandableSections::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), expandable_sections_interactions.begin(), expandable_sections_interactions.end());
}

} // namespace gui_forms
