#include "gui_forms/controls/expandable_sections.hpp"
#include "gui_forms/text.hpp"
#include "../panel/panel_anatomy.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/animation/animation_timeline/animation_timeline.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/surface_root/surface_root.hpp"

#include <array>
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {
class DisclosureButton final : public CheckBox {
public:
    explicit DisclosureButton(StableId id) : CheckBox(std::move(id)) {
        appearance = CheckBoxAppearance::button;
    }
    SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor = ButtonBase::semantic_descriptor();
        descriptor.role = SemanticRole::button;
        return descriptor;
    }
};
} // namespace

namespace expandable_sections_detail {

// A section body's clipping host: a child of the sections holding the body
// at its full height from its top, whatever its own height, so a host
// lower than the body clips it (a parent clips its children to its client
// rectangle) and the body is never laid out again for the reveal. It takes
// no focus, no pointer of its own and publishes no semantic node (the
// default descriptor is unexposed and includes its descendants).
class SectionHost final : public Control {
public:
    explicit SectionHost(StableId id) : Control(std::move(id)) {
        focusable = false;
        tab_stop = false;
    }

    // Moves `content` into the host (out of any parent it has), replacing
    // the body it held; empty leaves the host empty.
    void set_content(const Control::Ptr& content) {
        if (content_ == content) return;
        if (content_ && (*content_).parent().get() == this) static_cast<void>(remove_child((*content_).runtime_id()));
        content_ = content;
        if (!content_) return;
        const Control::Ptr parent = (*content_).parent();
        if (parent) static_cast<void>((*parent).remove_child((*content_).runtime_id()));
        add_child(content_);
    }

    // The body's full height, from the sections' arrange.
    void set_content_height(double height) noexcept { content_height_ = height; }

    void arrange(Rect bounds) override {
        arrange_self(bounds);
        if (content_) set_child_layout(content_, {0.0, 0.0, bounds.width, content_height_});
    }

    [[nodiscard]] bool hit_test_local(Point) const override { return false; }

private:
    Control::Ptr content_{};
    double content_height_{};
};

} // namespace expandable_sections_detail

struct ExpandableSections::Entry final {
    int id{};
    std::shared_ptr<CheckBox> heading{};
    Control::Ptr content{};
    std::shared_ptr<expandable_sections_detail::SectionHost> host{};
    SubscriptionToken change{};
    // How far the host is open (0 to 1), the levels the reveal runs
    // between, and its timeline.
    double reveal{};
    double reveal_from{};
    double reveal_to{};
    AnimationTimeline timeline{};
};
struct ExpandableSections::Revision final {
    std::vector<SectionItem> items{};
    std::vector<std::shared_ptr<Entry>> entries{};
    // Stamps items(): replacing the revision ends the views lent from it.
    detail::BorrowLender lender{};
};
struct ExpandableSections::HeadingChange final {
    ExpandableSections& sections;
    int id;
    void operator()(const bool expanded) const { sections.on_toggled(id, expanded); }
};

ExpandableSections::ExpandableSections(StableId id) : Panel(std::move(id)) {
    focusable = false;
    tab_stop = false;
}
ExpandableSections::~ExpandableSections() = default;
BorrowedSpan<SectionItem> ExpandableSections::items() const noexcept {
    if (!revision_) return BorrowedSpan<SectionItem>();
    const std::vector<SectionItem>& current = (*revision_).items;
    BorrowedSpan<SectionItem> view = detail::lend_view(current.data(), current.size(), (*revision_).lender);
    return view;
}
ExpandableSections::Entry& ExpandableSections::entry(const int index) const {
    if (!revision_ || index < 0 || static_cast<std::size_t>(index) >= (*revision_).entries.size()) {
        throw std::out_of_range("ExpandableSections item index");
    }
    return *(*revision_).entries[static_cast<std::size_t>(index)];
}
bool ExpandableSections::expanded(const int index) const {
    const Entry& item = entry(index);
    const bool value = (*item.heading).checked.read();
    return value;
}
void ExpandableSections::set_expanded(const int index, const bool expanded) {
    require_mutable();
    Entry& item = entry(index);
    (*item.heading).checked = expanded;
}
Control* ExpandableSections::part(const std::string_view name, const int index) const {
    const Entry& item = entry(index);
    Control* control = nullptr;
    if (name == "header") control = item.heading.get();
    else if (name == "body") control = item.content.get();
    return control;
}
void ExpandableSections::prepare_write(const decltype(single_open)&, bool&) {
    require_mutable();
}

// Turning single_open on keeps the first open section and closes the rest;
// a closing heading may replace the items, which ends the walk.
void ExpandableSections::written(const decltype(single_open)&) {
    if (!single_open.read() || !revision_) return;
    const Control::Ptr retained = shared_from_this();
    const std::shared_ptr<Revision> revision = revision_;
    bool found = false;
    for (const std::shared_ptr<Entry>& item : (*revision).entries) {
        if (!(*(*item).heading).checked.read()) continue;
        if (found) (*(*item).heading).checked = false;
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
        bool ours = !parent || parent.get() == this;
        if (!ours && revision_) {
            for (const std::shared_ptr<Entry>& held : (*revision_).entries) {
                ours = ours || parent == (*held).host;
            }
        }
        if (!ours) throw std::invalid_argument("section content already has a parent");
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
            std::string body(stable_id().value());
            body += ".body.";
            body += std::to_string(value.id);
            (*item).host = make_control<expandable_sections_detail::SectionHost>(StableId(body));
        }
        (*next).entries.push_back(std::move(item));
    }
    replacing_ = true;
    try {
        const std::shared_ptr<Revision> previous = std::exchange(revision_, next);
        // The items lent from the previous revision end here, even while a
        // callback still holds that revision.
        if (previous) (*previous).lender.invalidate();
        if (previous) {
            for (const std::shared_ptr<Entry>& item : (*previous).entries) {
                (*item).change.disconnect();
                (*(*item).heading).unbind();
                const std::vector<std::shared_ptr<Entry>>::const_iterator found =
                    std::find((*next).entries.begin(), (*next).entries.end(), item);
                if (found == (*next).entries.end()) {
                    static_cast<void>(remove_child((*(*item).heading).runtime_id()));
                    // The host leaves with its body; a body reused by a
                    // section that stays moves to that section's host below.
                    (*(*item).host).set_content(nullptr);
                    static_cast<void>(remove_child((*(*item).host).runtime_id()));
                }
            }
        }
        bool opened = false;
        for (std::size_t index = 0U; index < (*next).items.size(); ++index) {
            const SectionItem& value = (*next).items[index];
            Entry& item = *(*next).entries[index];
            item.content = value.content;
            (*item.host).set_content(item.content);
            CheckBox& heading = *item.heading;
            heading.text = value.label;
            heading.tab_index = static_cast<std::uint32_t>(index * 2U);
            if (value.expanded != nullptr) heading.bind(*value.expanded);
            if (single_open.read() && opened && heading.checked.read()) heading.checked = false;
            opened = opened || heading.checked.read();
            heading.expanded_state = heading.checked.read();
            (*item.content).visible = heading.checked.read();
            // A replacement shows each section as it stands, with no reveal.
            item.reveal = heading.checked.read() ? 1.0 : 0.0;
            item.reveal_from = item.reveal;
            item.reveal_to = item.reveal;
            item.timeline.stop();
            (*item.host).visible = heading.checked.read();
            if (!heading.parent()) add_child(item.heading);
            if (!(*item.host).parent()) add_child(item.host);
            item.change = heading.checked_changed().subscribe(*this, HeadingChange{*this, value.id});
        }
        const std::size_t child_count = (*next).entries.size() * 2U;
        for (std::size_t index = 0U; index < (*next).entries.size(); ++index) {
            const Entry& item = *(*next).entries[index];
            static_cast<void>(set_child_index_from_top((*item.heading).runtime_id(), child_count - index * 2U - 1U));
            static_cast<void>(set_child_index_from_top((*item.host).runtime_id(), child_count - index * 2U - 2U));
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
        (*item.heading).expanded_state = expanded;
        if (!is_alive() || revision_ != revision) return;
        (*item.content).visible = expanded;
        if (!is_alive() || revision_ != revision) return;
        start_reveal(item, expanded);
        if (expanded && single_open.read()) {
            for (const std::shared_ptr<Entry>& other : (*revision).entries) {
                if ((*other).id != id) (*(*other).heading).checked = false;
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
        // The host takes the opened share of the body's height; the body
        // keeps its full height inside it.
        if ((*(*item).host).visible.read()) {
            const double height = (*(*item).content).requested_bounds().height;
            const double shown = height * std::clamp((*item).reveal, 0.0, 1.0);
            (*(*item).host).set_content_height(height);
            set_child_layout((*item).host, {0.0, top, bounds.width, shown});
            top += shown;
        }
    }
}

double ExpandableSections::section_reveal(const int index) const {
    const Entry& item = entry(index);
    return item.reveal;
}

void ExpandableSections::start_reveal(Entry& item, const bool open) {
    const double target = open ? 1.0 : 0.0;
    item.reveal_from = item.reveal;
    item.reveal_to = target;
    (*item.host).visible = true;
    const SurfaceRoot* const root = attached_surface_root();
    const bool suppressed = root == nullptr || (*root).motion_clock().suppressed();
    if (suppressed || item.reveal == target) {
        item.timeline.stop();
        item.reveal = target;
        item.reveal_from = target;
        (*item.host).visible = open;
        return;
    }
    AnimationSpec specification{};
    specification.duration = open ? section_reveal_open : section_reveal_close;
    specification.easing = EasingCurve::ease_out;
    item.timeline.set_specification(specification);
    item.timeline.start(FrameClock::now());
    if (reveal_frames_.started()) return;
    const std::shared_ptr<ExpandableSections> self =
        std::static_pointer_cast<ExpandableSections>(shared_from_this());
    reveal_frames_.start(*this, display_frame_interval, RevealTick{self});
}

AnimationTickResult ExpandableSections::RevealTick::operator()(FrameTime now) const {
    const std::shared_ptr<ExpandableSections> live = target.lock();
    const AnimationTickResult result = live ? (*live).on_reveal_tick(now) : AnimationTickResult::unchanged;
    return result;
}

AnimationTickResult ExpandableSections::on_reveal_tick(FrameTime now) {
    const SurfaceRoot* const root = attached_surface_root();
    const bool suppressed = root == nullptr || (*root).motion_clock().suppressed();
    if (suppressed || !revision_) {
        complete_reveals();
        invalidate(Dirty::layout | Dirty::paint);
        return AnimationTickResult::damaged;
    }
    bool moving = false;
    // Held, and checked after each visibility write: the write raises the
    // host's events and may move focus, and a handler reached from there
    // may call set_items, replacing (and freeing) the revision
    // (A-gf-controls-01-F8). A replaced revision ends the walk; the new
    // one starts its own reveals.
    const std::shared_ptr<Revision> revision = revision_;
    for (const std::shared_ptr<Entry>& held : (*revision).entries) {
        Entry& item = *held;
        if (!item.timeline.running()) continue;
        const AnimationSample sample = item.timeline.sample(now);
        if (sample.finished) {
            item.timeline.stop();
            item.reveal = item.reveal_to;
            item.reveal_from = item.reveal_to;
            (*item.host).visible = item.reveal_to > 0.0;
            if (!is_alive() || revision_ != revision) return AnimationTickResult::damaged;
            continue;
        }
        const double eased = std::clamp(sample.eased_progress, 0.0, 1.0);
        item.reveal = item.reveal_from + (item.reveal_to - item.reveal_from) * eased;
        moving = true;
    }
    if (!moving) reveal_frames_.stop();
    invalidate(Dirty::layout | Dirty::paint);
    return AnimationTickResult::damaged;
}

void ExpandableSections::complete_reveals() {
    reveal_frames_.stop();
    // Held and checked as in on_reveal_tick (A-gf-controls-01-F8).
    const std::shared_ptr<Revision> revision = revision_;
    if (!revision) return;
    for (const std::shared_ptr<Entry>& held : (*revision).entries) {
        Entry& item = *held;
        item.timeline.stop();
        item.reveal = item.reveal_to;
        item.reveal_from = item.reveal_to;
        (*item.host).visible = item.reveal_to > 0.0;
        if (!is_alive() || revision_ != revision) return;
    }
}

void ExpandableSections::on_detached_from_window() noexcept {
    // Leaving the window ends every reveal at its target. Writing a host's
    // visibility may allocate; if it fails the timer is already stopped and
    // the next arrange shows the hosts as they stand.
    reveal_frames_.stop();
    try {
        complete_reveals();
    } catch (...) {
    }
    Panel::on_detached_from_window();
}

void ExpandableSections::on_paint(Painter& painter, Rect local_damage) {
    reveal_frames_.note_painted();
    Panel::on_paint(painter, local_damage);
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
    .item_systems = {},
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

// property_codemod begin ExpandableSections (tools/property_families/expandable_sections.json; generated, do not edit)

// property_codemod end ExpandableSections

} // namespace gui_forms
