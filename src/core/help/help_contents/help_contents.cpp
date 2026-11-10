#include "gui_forms/help/help_contents/help_contents.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/surface_root/surface_root.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// The process's declared topics, in declaration order. Constant-initialized,
// so a topic declared during another translation unit's static
// initialization finds the list ready.
constinit HelpTopic* declared_first = nullptr;
constinit HelpTopic* declared_last = nullptr;

[[nodiscard]] std::string_view last_segment(std::string_view stable_id) noexcept {
    const std::size_t dot = stable_id.rfind('.');
    if (dot == std::string_view::npos) return stable_id;
    return stable_id.substr(dot + 1U);
}

// One step of the skeleton walk: a control and its depth below the window.
struct SkeletonVisit final {
    Control::Ptr control{};
    std::uint32_t depth{};
};

[[nodiscard]] std::string skeleton_name(const Control& control) {
    const std::string& accessible = control.accessible_name.read();
    if (!accessible.empty()) return accessible;
    const SemanticDescriptor descriptor = control.semantic_descriptor();
    return descriptor.name;
}

// The end of node `index`'s subtree: the first later node no deeper than it.
[[nodiscard]] std::size_t subtree_end(const std::vector<HelpContentsNode>& nodes, std::size_t index) noexcept {
    std::size_t end = index + 1U;
    while (end < nodes.size() && nodes[end].depth > nodes[index].depth) ++end;
    return end;
}

// Opens a slot at `position` in the preorder nodes, renumbering every parent
// and line page at or after it.
void open_slot(HelpContentsTree& tree, std::size_t position, HelpContentsNode node) {
    const std::uint32_t slot = static_cast<std::uint32_t>(position);
    for (HelpContentsNode& existing : tree.nodes) {
        if (existing.parent != help_no_parent && existing.parent >= slot) ++existing.parent;
    }
    for (HelpContentsLine& line : tree.lines) {
        if (line.page >= slot) ++line.page;
    }
    if (node.parent != help_no_parent && node.parent >= slot) ++node.parent;
    tree.nodes.insert(tree.nodes.begin() + static_cast<std::ptrdiff_t>(position), std::move(node));
}

} // namespace

HelpPlace HelpPlace::parse(std::string_view text) {
    if (text == "top") return HelpPlace(HelpPlaceKind::top, {});
    HelpPlaceKind kind = HelpPlaceKind::top;
    std::string_view anchor{};
    if (text.substr(0U, 6U) == "after:") {
        kind = HelpPlaceKind::after;
        anchor = text.substr(6U);
    } else if (text.substr(0U, 6U) == "under:") {
        kind = HelpPlaceKind::under;
        anchor = text.substr(6U);
    } else {
        throw std::invalid_argument("GUI.Forms help place must be top, after:<id> or under:<id>");
    }
    if (!valid_stable_id(anchor)) throw std::invalid_argument("GUI.Forms help place anchor must be a stable ID");
    return HelpPlace(kind, anchor);
}

HelpTopic::HelpTopic(StableId stable_id, std::string title, std::string body, HelpPlace place, std::string help_line,
                     std::string keywords)
    : stable_id_(std::move(stable_id)),
      title_(std::move(title)),
      body_(std::move(body)),
      place_(std::move(place)),
      help_line_(std::move(help_line)),
      keywords_(std::move(keywords)) {
    previous_ = declared_last;
    if (declared_last != nullptr) {
        (*declared_last).next_ = this;
    } else {
        declared_first = this;
    }
    declared_last = this;
}

HelpTopic::~HelpTopic() {
    if (previous_ != nullptr) {
        (*previous_).next_ = next_;
    } else {
        declared_first = next_;
    }
    if (next_ != nullptr) {
        (*next_).previous_ = previous_;
    } else {
        declared_last = previous_;
    }
}

const HelpTopic* HelpTopic::first() noexcept {
    return declared_first;
}

void collect_declared_topics(std::vector<HelpTopicPlacement>& placements) {
    placements.clear();
    for (const HelpTopic* topic = HelpTopic::first(); topic != nullptr; topic = (*topic).next()) {
        HelpTopicPlacement placement{};
        placement.stable_id = std::string((*topic).stable_id().value());
        placement.kind = (*topic).place().kind();
        placement.anchor = std::string((*topic).place().anchor());
        placements.push_back(std::move(placement));
    }
}

void put_declared_topics(HelpRecordLayer& layer) {
    for (const HelpTopic* topic = HelpTopic::first(); topic != nullptr; topic = (*topic).next()) {
        HelpRecordEntry entry{};
        entry.stable_id = std::string((*topic).stable_id().value());
        entry.title = (*topic).title();
        entry.help = (*topic).body();
        if (!(*topic).help_line().empty()) entry.help_line = (*topic).help_line();
        if (!(*topic).keywords().empty()) entry.keywords = (*topic).keywords();
        layer.put(std::move(entry));
    }
}

void append_help_skeleton(const SurfaceRoot& window, std::vector<HelpSkeletonNode>& nodes) {
    const Control::Ptr root = window.root();
    if (!root) return;
    // Depth first with an explicit stack, children pushed in reverse so they
    // come off in order; the tree's depth is bounded by the window's own
    // nesting limit, not by the call stack.
    std::vector<SkeletonVisit> pending;
    pending.push_back(SkeletonVisit{root, 0U});
    while (!pending.empty()) {
        const SkeletonVisit visit = std::move(pending.back());
        pending.pop_back();
        const Control& control = *visit.control;
        const std::span<const Control::Ptr> children = control.children();
        HelpSkeletonNode node{};
        node.stable_id = std::string(control.stable_id().value());
        node.name = skeleton_name(control);
        node.depth = visit.depth;
        if (visit.depth == 0U) {
            node.kind = HelpSkeletonKind::window;
        } else if (!children.empty()) {
            node.kind = HelpSkeletonKind::group;
        } else {
            node.kind = HelpSkeletonKind::control;
        }
        nodes.push_back(std::move(node));
        for (std::size_t index = children.size(); index > 0U; --index) {
            const Control::Ptr& child = children[index - 1U];
            if (child) pending.push_back(SkeletonVisit{child, visit.depth + 1U});
        }
    }
}

std::uint32_t HelpContentsTree::find(std::string_view stable_id) const noexcept {
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (nodes[index].stable_id == stable_id) return static_cast<std::uint32_t>(index);
    }
    return help_no_parent;
}

void HelpContentsTree::clear() noexcept {
    nodes.clear();
    lines.clear();
}

void build_help_contents(std::span<const HelpSkeletonNode> skeleton, std::span<const HelpTopicPlacement> topics,
                         const HelpCatalog& catalog, HelpContentsTree& tree) {
    tree.clear();
    // The nearest page above each skeleton depth: page_at[d] is the page
    // holding the skeleton node at depth d on the current path, or none.
    std::vector<std::uint32_t> page_at;
    for (const HelpSkeletonNode& node : skeleton) {
        if (page_at.size() > node.depth) page_at.resize(node.depth);
        while (page_at.size() < node.depth) page_at.push_back(help_no_parent);
        std::uint32_t parent = help_no_parent;
        for (std::size_t depth = page_at.size(); depth > 0U; --depth) {
            if (page_at[depth - 1U] != help_no_parent) {
                parent = page_at[depth - 1U];
                break;
            }
        }
        if (tree.find(node.stable_id) != help_no_parent) {
            page_at.push_back(help_no_parent);
            continue;
        }
        const HelpResolvedRecord record = catalog.resolve(node.stable_id);
        std::string title = record.title.text;
        if (title.empty()) title = node.name;
        if (title.empty()) title = std::string(last_segment(node.stable_id));
        if (!record.help.text.empty()) {
            HelpContentsNode page{};
            page.stable_id = node.stable_id;
            page.title = std::move(title);
            page.parent = parent;
            page.depth = parent == help_no_parent ? 0U : tree.nodes[parent].depth + 1U;
            tree.nodes.push_back(std::move(page));
            page_at.push_back(static_cast<std::uint32_t>(tree.nodes.size() - 1U));
            continue;
        }
        page_at.push_back(help_no_parent);
        if (!record.help_line.text.empty() && parent != help_no_parent) {
            HelpContentsLine line{};
            line.page = parent;
            line.stable_id = node.stable_id;
            line.title = std::move(title);
            line.help_line = record.help_line.text;
            tree.lines.push_back(std::move(line));
        }
    }
    std::size_t top_inserted = 0U;
    for (const HelpTopicPlacement& placement : topics) {
        if (tree.find(placement.stable_id) != help_no_parent) continue;
        const HelpResolvedRecord record = catalog.resolve(placement.stable_id);
        if (record.help.text.empty()) continue;
        HelpContentsNode page{};
        page.stable_id = placement.stable_id;
        page.title = record.title.text.empty() ? std::string(last_segment(placement.stable_id)) : record.title.text;
        page.topic = true;
        const std::uint32_t anchor =
            placement.kind == HelpPlaceKind::top ? help_no_parent : tree.find(placement.anchor);
        if (placement.kind == HelpPlaceKind::top) {
            open_slot(tree, top_inserted, std::move(page));
            ++top_inserted;
            continue;
        }
        if (anchor == help_no_parent) {
            tree.nodes.push_back(std::move(page));
            continue;
        }
        const std::size_t end = subtree_end(tree.nodes, anchor);
        if (placement.kind == HelpPlaceKind::under) {
            page.parent = anchor;
            page.depth = tree.nodes[anchor].depth + 1U;
        } else {
            page.parent = tree.nodes[anchor].parent;
            page.depth = tree.nodes[anchor].depth;
        }
        open_slot(tree, end, std::move(page));
    }
}

} // namespace gui_forms
