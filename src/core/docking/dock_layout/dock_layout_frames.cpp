#include "dock_layout_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// The frames presentation's bookkeeping (DK decisions 60 to 63): the
// documents of the main site's document area, their default frames, their
// icon slots and their stacking order.

namespace gui_forms::detail {

namespace {

// One area document's frame, with its tree position, for numbering the
// orders without changing the stacking.
struct StackedFrame final {
    DockFrameMemory* frame{};
    std::size_t position{};
};

struct StackedBelow final {
    [[nodiscard]] bool operator()(const StackedFrame& left, const StackedFrame& right) const noexcept {
        if ((*left.frame).order != (*right.frame).order) return (*left.frame).order < (*right.frame).order;
        const bool earlier = left.position < right.position;
        return earlier;
    }
};

[[nodiscard]] bool slot_taken(const std::vector<std::uint32_t>& taken, std::uint32_t slot) noexcept {
    const bool found = std::find(taken.begin(), taken.end(), slot) != taken.end();
    return found;
}

[[nodiscard]] std::uint32_t first_slot_not_in(const std::vector<std::uint32_t>& taken) noexcept {
    std::uint32_t slot = 0U;
    while (slot_taken(taken, slot)) ++slot;
    return slot;
}

} // namespace

void collect_area_documents(const DockLayoutState& state, std::vector<DockPanelId>& out) {
    DockNodeId area{};
    for (const DockNode& node : state.nodes) {
        if (node.kind == DockNodeKind::document_area) area = node.id;
    }
    const DockNode* const area_node = find_dock_node(state, area);
    if (area_node == nullptr || (*area_node).first == DockNodeId{}) return;
    std::vector<DockNodeId> order{};
    collect_dock_subtree(state, (*area_node).first, order);
    for (const DockNodeId id : order) {
        const DockNode* const node = find_dock_node(state, id);
        if (node == nullptr || !is_dock_stack((*node).kind)) continue;
        out.insert(out.end(), (*node).panels.begin(), (*node).panels.end());
    }
}

std::uint32_t top_frame_order(const DockLayoutState& state) noexcept {
    std::uint32_t top = 0U;
    for (const DockPanelRecord& record : state.panels) {
        const DockFrameMemory* const frame = record.frame.value();
        if (frame != nullptr) top = std::max(top, (*frame).order);
    }
    return top;
}

DockFrameMemory default_frame_memory(const DockLayoutState& state) {
    std::vector<DockPanelId> documents{};
    collect_area_documents(state, documents);
    std::uint32_t framed = 0U;
    for (const DockPanelId& document : documents) {
        const DockPanelRecord* const record = find_dock_record(state, document);
        if (record != nullptr && (*record).frame.present()) ++framed;
    }
    const double offset = dock_frame_cascade_step * static_cast<double>(framed % dock_frame_cascade_wrap);
    DockFrameMemory frame{};
    frame.bounds = Rect{offset, offset, dock_frame_default_width, dock_frame_default_height};
    frame.state = DockFrameState::normal;
    frame.order = top_frame_order(state) + 1U;
    return frame;
}

std::uint32_t first_free_icon_slot(const DockLayoutState& state) {
    std::vector<DockPanelId> documents{};
    collect_area_documents(state, documents);
    std::vector<std::uint32_t> taken{};
    for (const DockPanelId& document : documents) {
        const DockPanelRecord* const record = find_dock_record(state, document);
        if (record == nullptr) continue;
        const DockFrameMemory* const frame = (*record).frame.value();
        if (frame != nullptr && (*frame).state == DockFrameState::minimized) taken.push_back((*frame).icon_slot);
    }
    const std::uint32_t slot = first_slot_not_in(taken);
    return slot;
}

void sync_document_frames(DockLayoutState& state) {
    if (state.document_presentation != DockDocumentPresentation::frames) return;
    std::vector<DockPanelId> documents{};
    collect_area_documents(state, documents);
    // Defaults first, one at a time: each cascades past the ones before.
    for (const DockPanelId& document : documents) {
        DockPanelRecord* const record = find_dock_record(state, document);
        if (record == nullptr || (*record).frame.present()) continue;
        const DockFrameMemory frame = default_frame_memory(state);
        (*record).frame.set(frame);
    }
    std::vector<StackedFrame> stacked{};
    std::vector<std::uint32_t> taken{};
    stacked.reserve(documents.size());
    for (std::size_t position = 0; position < documents.size(); ++position) {
        DockPanelRecord* const record = find_dock_record(state, documents[position]);
        if (record == nullptr) continue;
        DockFrameMemory* const frame = (*record).frame.value();
        if (frame == nullptr) continue;
        if ((*frame).state == DockFrameState::minimized) {
            if (slot_taken(taken, (*frame).icon_slot)) (*frame).icon_slot = first_slot_not_in(taken);
            taken.push_back((*frame).icon_slot);
        }
        StackedFrame entry{};
        entry.frame = frame;
        entry.position = position;
        stacked.push_back(entry);
    }
    std::stable_sort(stacked.begin(), stacked.end(), StackedBelow{});
    for (std::size_t index = 0; index < stacked.size(); ++index) {
        (*stacked[index].frame).order = static_cast<std::uint32_t>(index + 1U);
    }
}

} // namespace gui_forms::detail
