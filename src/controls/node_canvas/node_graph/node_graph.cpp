#include "gui_forms/controls/node_graph.hpp"

#include "node_graph_room.hpp"
#include "node_graph_text.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

// The named operations and the undo log. Every recorded operation runs in
// three phases: validate (a refusal changes nothing, log included), prepare
// (make room in every array the operation and its record will touch; this is
// the only phase that allocates), then change and record, which cannot fail.
// undo and redo replay records over storage the records already own, so they
// allocate nothing and cannot fail either; the free lists, slot counts and
// generations they restore are exactly those before the operation, because
// steps are undone strictly newest first and redone oldest first. That
// argument is checked where it is used: every push or insert that relies on
// reserved capacity checks the capacity first and fails fast (require_room)
// rather than reallocate inside a noexcept path. Documents are in
// node_graph_document.cpp.

namespace gui_forms {
namespace {

using node_graph_room::capacity_fault;
using node_graph_room::require_room;

bool finite_rect(const WorldRect& bounds) noexcept {
    const bool finite = std::isfinite(bounds.x) && std::isfinite(bounds.y) && std::isfinite(bounds.width) &&
                        std::isfinite(bounds.height) && bounds.width >= 0.0 && bounds.height >= 0.0;
    return finite;
}

bool valid_port(const PortRecord& port) noexcept {
    const bool offset_valid = std::isfinite(port.offset) && port.offset >= 0.0 && port.offset <= 1.0;
    const bool side_valid = port.side == PortSide::left || port.side == PortSide::right ||
                            port.side == PortSide::top || port.side == PortSide::bottom ||
                            port.side == PortSide::automatic;
    const bool direction_valid = port.direction == PortDirection::input || port.direction == PortDirection::output;
    const bool name_valid = port.name.size() <= maximum_graph_text_bytes;
    return offset_valid && side_valid && direction_valid && name_valid;
}

// Makes room for `needed` entries in a slot-indexed list, growing
// geometrically, so a later push within the slot count never allocates.
void reserve_slot_list(std::vector<std::uint32_t>& list, std::size_t needed) {
    if (list.capacity() >= needed) return;
    const std::size_t grown = std::max(needed, list.capacity() * 2U);
    list.reserve(grown);
}

// Makes room for `extra` more entries in a log array, growing geometrically.
// A count past max_size reaches vector::reserve, which reports it as
// std::length_error.
template <typename Entry> void reserve_more(std::vector<Entry>& list, std::size_t extra) {
    const std::size_t size = list.size();
    if (extra > list.max_size() - size) {
        list.reserve(list.max_size());
        return;
    }
    const std::size_t needed = size + extra;
    if (list.capacity() >= needed) return;
    const std::size_t grown = std::max(needed, list.capacity() * 2U);
    list.reserve(grown);
}

// The next generation of a freed slot; zero is skipped so that a
// zero-initialised identity is never live.
std::uint32_t next_generation(std::uint32_t generation) noexcept {
    std::uint32_t next = generation + 1U;
    if (next == 0U) next = 1U;
    return next;
}

// The undo log's byte estimate for one node record kept in it.
std::size_t node_bytes(const NodeRecord& node) noexcept {
    std::size_t bytes = sizeof(NodeRecord) + node.key.size() + node.kind.size() + node.title.size();
    for (const PortRecord& port : node.ports) bytes += sizeof(PortRecord) + port.name.size();
    for (const NodeProperty& property : node.properties) {
        bytes += sizeof(NodeProperty) + property.name.size() + property.value.size();
    }
    return bytes;
}

// The position of the named property in `properties`, or its size.
std::size_t property_position(const std::vector<NodeProperty>& properties, std::string_view name) noexcept {
    for (std::size_t index = 0U; index < properties.size(); ++index) {
        if (std::string_view(properties[index].name) == name) return index;
    }
    return properties.size();
}

// The checks of add_node that need no graph: every field within its bounds,
// every name well formed, no property named twice.
GraphStatus check_spec(const NodeSpec& spec) noexcept {
    if (!finite_rect(spec.bounds)) return GraphStatus::invalid_argument;
    if (spec.ports.size() > maximum_node_ports) return GraphStatus::capacity;
    if (spec.properties.size() > maximum_node_properties) return GraphStatus::capacity;
    for (const PortRecord& port : spec.ports) {
        if (!valid_port(port)) return GraphStatus::invalid_argument;
    }
    if (!node_graph_text::valid_key(spec.key)) return GraphStatus::invalid_argument;
    if (spec.kind.size() > maximum_graph_text_bytes || spec.title.size() > maximum_graph_text_bytes) {
        return GraphStatus::invalid_argument;
    }
    for (std::size_t index = 0U; index < spec.properties.size(); ++index) {
        const NodeProperty& property = spec.properties[index];
        if (!node_graph_text::valid_property_name(property.name)) return GraphStatus::invalid_argument;
        if (property.value.size() > maximum_graph_text_bytes) return GraphStatus::invalid_argument;
        // At most 256 properties, so the pairwise check is bounded.
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (spec.properties[earlier].name == property.name) return GraphStatus::invalid_argument;
        }
    }
    return GraphStatus::ok;
}

// Orders a key order's entries (slots of `records`) by their record's key.
template <typename Record> struct SlotKeyBefore final {
    const std::vector<Record>* records{};
    bool operator()(std::uint32_t slot, std::string_view key) const noexcept {
        const bool before = std::string_view((*records)[slot].key) < key;
        return before;
    }
};

// Where `key` is or would be in `order` (slots of `records` sorted by key).
template <typename Record>
std::size_t ordered_position(const std::vector<std::uint32_t>& order, const std::vector<Record>& records,
                             std::string_view key) noexcept {
    const SlotKeyBefore<Record> before{&records};
    const std::vector<std::uint32_t>::const_iterator found =
        std::lower_bound(order.begin(), order.end(), key, before);
    const std::size_t position = static_cast<std::size_t>(found - order.begin());
    return position;
}

// Adds slot `index` to `order`. The order's capacity is at least the slot
// count (reserved with the slots) and the slot is not in it, so there is
// room; require_room checks.
template <typename Record>
void ordered_insert(std::vector<std::uint32_t>& order, const std::vector<Record>& records,
                    std::uint32_t index) noexcept {
    require_room(order, 1U, "node_graph.key_order");
    const std::size_t position = ordered_position(order, records, records[index].key);
    const std::vector<std::uint32_t>::iterator at = order.begin() + static_cast<std::ptrdiff_t>(position);
    order.insert(at, index);
}

template <typename Record>
void ordered_erase(std::vector<std::uint32_t>& order, const std::vector<Record>& records,
                   std::uint32_t index) noexcept {
    const std::size_t position = ordered_position(order, records, records[index].key);
    if (position >= order.size() || order[position] != index) return;
    const std::vector<std::uint32_t>::iterator at = order.begin() + static_cast<std::ptrdiff_t>(position);
    order.erase(at);
}

} // namespace

std::string_view graph_status_name(GraphStatus status) noexcept {
    switch (status) {
    case GraphStatus::ok: return "ok";
    case GraphStatus::stale_identity: return "stale_identity";
    case GraphStatus::bad_port: return "bad_port";
    case GraphStatus::wrong_direction: return "wrong_direction";
    case GraphStatus::occupied: return "occupied";
    case GraphStatus::same_node: return "same_node";
    case GraphStatus::invalid_argument: return "invalid_argument";
    case GraphStatus::capacity: return "capacity";
    case GraphStatus::duplicate_key: return "duplicate_key";
    case GraphStatus::missing_property: return "missing_property";
    case GraphStatus::nothing_to_undo: return "nothing_to_undo";
    case GraphStatus::nothing_to_redo: return "nothing_to_redo";
    case GraphStatus::group_open: return "group_open";
    case GraphStatus::no_group: return "no_group";
    }
    return "none";
}

void NodeGraph::reserve(std::size_t nodes, std::size_t links) {
    nodes_.reserve(nodes);
    links_.reserve(links);
    free_nodes_.reserve(nodes);
    free_links_.reserve(links);
    move_marks_.reserve(nodes);
    key_order_.reserve(nodes);
    link_key_order_.reserve(links);
}

bool NodeGraph::live(NodeId node) const noexcept {
    if (node.index >= nodes_.size()) return false;
    const NodeRecord& record = nodes_[node.index];
    const bool alive = record.live && record.generation == node.generation;
    return alive;
}

bool NodeGraph::live(LinkId link) const noexcept {
    if (link.index >= links_.size()) return false;
    const LinkRecord& record = links_[link.index];
    const bool alive = record.live && record.generation == link.generation;
    return alive;
}

void NodeGraph::advance() noexcept {
    ++revision_;
    if (revision_ == 0U) revision_ = 1U;
}

// -- the key order ----------------------------------------------------------------

NodeId NodeGraph::find_node(std::string_view key) const noexcept {
    const std::size_t position = ordered_position(key_order_, nodes_, key);
    if (position >= key_order_.size()) return NodeId{};
    const std::uint32_t index = key_order_[position];
    const NodeRecord& record = nodes_[index];
    if (std::string_view(record.key) != key) return NodeId{};
    return NodeId{index, record.generation};
}

LinkId NodeGraph::find_link(std::string_view key) const noexcept {
    const std::size_t position = ordered_position(link_key_order_, links_, key);
    if (position >= link_key_order_.size()) return LinkId{};
    const std::uint32_t index = link_key_order_[position];
    const LinkRecord& record = links_[index];
    if (std::string_view(record.key) != key) return LinkId{};
    return LinkId{index, record.generation};
}

// -- the undo log -----------------------------------------------------------------

// Reserves one record, `nodes` node payloads, `links` link payloads, `moves`
// moved nodes and `texts` texts beyond the arrays' sizes, and one step when
// the record opens one: exactly what the caller then pushes.
void NodeGraph::prepare_record(std::size_t nodes, std::size_t links, std::size_t moves, std::size_t texts) {
    const bool opens_step = !group_step_open();
    if (opens_step) discard_redo_steps();
    reserve_more(records_, 1U);
    reserve_more(node_payload_, nodes);
    reserve_more(link_payload_, links);
    reserve_more(move_payload_, moves);
    reserve_more(text_payload_, texts);
    if (opens_step) reserve_more(steps_, 1U);
}

// The arrays were reserved by prepare_record, so the pushes never allocate.
void NodeGraph::commit_record(const UndoRecord& record, std::size_t bytes) noexcept {
    const std::size_t record_bytes = bytes + sizeof(UndoRecord);
    require_room(records_, 1U, "node_graph.commit_record");
    records_.push_back(record);
    if (group_step_open()) {
        UndoStep& step = steps_.back();
        ++step.record_count;
        step.bytes += record_bytes;
    } else {
        UndoStep step{};
        step.first_record = records_.size() - 1U;
        step.record_count = 1U;
        step.bytes = record_bytes;
        require_room(steps_, 1U, "node_graph.commit_record");
        steps_.push_back(step);
        ++applied_steps_;
        if (group_depth_ > 0U) group_step_open_ = true;
    }
    undo_bytes_ += record_bytes;
    trim_steps();
}

void NodeGraph::discard_redo_steps() noexcept {
    if (applied_steps_ >= steps_.size()) return;
    const std::size_t cut_record = steps_[applied_steps_].first_record;
    const UndoRecord& cut = records_[cut_record];
    const std::size_t node_cut = cut.node_first;
    const std::size_t link_cut = cut.link_first;
    const std::size_t move_cut = cut.move_first;
    const std::size_t text_cut = cut.text_first;
    for (std::size_t index = applied_steps_; index < steps_.size(); ++index) undo_bytes_ -= steps_[index].bytes;
    steps_.erase(steps_.begin() + static_cast<std::ptrdiff_t>(applied_steps_), steps_.end());
    records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(cut_record), records_.end());
    node_payload_.erase(node_payload_.begin() + static_cast<std::ptrdiff_t>(node_cut), node_payload_.end());
    link_payload_.erase(link_payload_.begin() + static_cast<std::ptrdiff_t>(link_cut), link_payload_.end());
    move_payload_.erase(move_payload_.begin() + static_cast<std::ptrdiff_t>(move_cut), move_payload_.end());
    text_payload_.erase(text_payload_.begin() + static_cast<std::ptrdiff_t>(text_cut), text_payload_.end());
}

// Drops the oldest steps while the log is over its bound. Only called with no
// redo steps (after a record, or by set_undo_limits after discarding them).
// The newest step stays unless maximum_steps is zero.
void NodeGraph::trim_steps() noexcept {
    while (applied_steps_ > first_step_) {
        const std::size_t kept = applied_steps_ - first_step_;
        const bool over_steps = kept > undo_limits_.maximum_steps;
        const bool over_bytes = undo_bytes_ > undo_limits_.maximum_bytes && kept > 1U;
        if (!over_steps && !over_bytes) break;
        undo_bytes_ -= steps_[first_step_].bytes;
        ++first_step_;
    }
    if (first_step_ == applied_steps_) group_step_open_ = false;
    const std::size_t remaining = steps_.size() - first_step_;
    if (first_step_ > 0U && first_step_ >= remaining) compact_steps();
}

// Erases the dropped steps and their records and payloads, rebasing the rest.
// Amortised: it runs once the dropped prefix is at least half the table.
void NodeGraph::compact_steps() noexcept {
    if (first_step_ >= steps_.size()) {
        clear_log();
        return;
    }
    const std::size_t cut_record = steps_[first_step_].first_record;
    const UndoRecord& cut = records_[cut_record];
    const std::size_t node_cut = cut.node_first;
    const std::size_t link_cut = cut.link_first;
    const std::size_t move_cut = cut.move_first;
    const std::size_t text_cut = cut.text_first;
    steps_.erase(steps_.begin(), steps_.begin() + static_cast<std::ptrdiff_t>(first_step_));
    records_.erase(records_.begin(), records_.begin() + static_cast<std::ptrdiff_t>(cut_record));
    node_payload_.erase(node_payload_.begin(), node_payload_.begin() + static_cast<std::ptrdiff_t>(node_cut));
    link_payload_.erase(link_payload_.begin(), link_payload_.begin() + static_cast<std::ptrdiff_t>(link_cut));
    move_payload_.erase(move_payload_.begin(), move_payload_.begin() + static_cast<std::ptrdiff_t>(move_cut));
    text_payload_.erase(text_payload_.begin(), text_payload_.begin() + static_cast<std::ptrdiff_t>(text_cut));
    for (UndoStep& step : steps_) step.first_record -= cut_record;
    for (UndoRecord& record : records_) {
        record.node_first -= node_cut;
        record.link_first -= link_cut;
        record.move_first -= move_cut;
        record.text_first -= text_cut;
    }
    applied_steps_ -= first_step_;
    first_step_ = 0U;
}

void NodeGraph::clear_log() noexcept {
    steps_.clear();
    records_.clear();
    node_payload_.clear();
    link_payload_.clear();
    move_payload_.clear();
    text_payload_.clear();
    first_step_ = 0U;
    applied_steps_ = 0U;
    undo_bytes_ = 0U;
    group_step_open_ = false;
}

void NodeGraph::begin_undo_group() noexcept { ++group_depth_; }

GraphResult NodeGraph::end_undo_group() noexcept {
    GraphResult result{};
    if (group_depth_ == 0U) {
        result.status = GraphStatus::no_group;
        return result;
    }
    --group_depth_;
    if (group_depth_ == 0U) group_step_open_ = false;
    return result;
}

GraphResult NodeGraph::clear_undo() noexcept {
    GraphResult result{};
    if (group_depth_ > 0U) {
        result.status = GraphStatus::group_open;
        return result;
    }
    clear_log();
    return result;
}

void NodeGraph::set_undo_limits(UndoLimits limits) {
    // Reserve before changing anything, so a failed reservation leaves the
    // log and its limits as they were.
    if (limits.maximum_steps > 0U && limits.maximum_steps <= (steps_.max_size() - 1U) / 2U) {
        steps_.reserve(limits.maximum_steps * 2U + 1U);
    }
    undo_limits_ = limits;
    discard_redo_steps();
    trim_steps();
}

// Moves the node out of its slot into `keep` and frees the slot (the
// generation stays, so identities of the node are stale).
void NodeGraph::detach_node(std::uint32_t index, NodeRecord& keep) noexcept {
    ordered_erase(key_order_, nodes_, index);
    NodeRecord& slot = nodes_[index];
    const std::uint32_t generation = slot.generation;
    keep = std::move(slot);
    slot = NodeRecord{};
    slot.generation = generation;
    require_room(free_nodes_, 1U, "node_graph.detach_node");
    free_nodes_.push_back(index);
    --live_nodes_;
}

// The inverse of detach_node: the slot is the free list's newest entry.
void NodeGraph::attach_node(std::uint32_t index, NodeRecord& kept) noexcept {
    if (free_nodes_.empty() || free_nodes_.back() != index) capacity_fault("node_graph.attach_node");
    free_nodes_.pop_back();
    nodes_[index] = std::move(kept);
    kept = NodeRecord{};
    ordered_insert(key_order_, nodes_, index);
    ++live_nodes_;
}

// Moves the link (with its ID) out of its slot into `keep` and frees the
// slot; the generation stays, so its identities are stale.
void NodeGraph::detach_link(std::uint32_t index, LinkRecord& keep) noexcept {
    ordered_erase(link_key_order_, links_, index);
    LinkRecord& slot = links_[index];
    const std::uint32_t generation = slot.generation;
    keep = std::move(slot);
    slot = LinkRecord{};
    slot.generation = generation;
    require_room(free_links_, 1U, "node_graph.detach_link");
    free_links_.push_back(index);
    --live_links_;
}

// The inverse of detach_link: the slot is the free list's newest entry.
void NodeGraph::attach_link(std::uint32_t index, LinkRecord& kept) noexcept {
    if (free_links_.empty() || free_links_.back() != index) capacity_fault("node_graph.attach_link");
    free_links_.pop_back();
    links_[index] = std::move(kept);
    kept = LinkRecord{};
    ordered_insert(link_key_order_, links_, index);
    ++live_links_;
}

void NodeGraph::revert(const UndoRecord& record) noexcept {
    switch (record.operation) {
    case UndoOperation::add_node: {
        const std::uint32_t index = record.node.index;
        ordered_erase(key_order_, nodes_, index);
        node_payload_[record.node_first] = std::move(nodes_[index]);
        --live_nodes_;
        if (record.appended) {
            // The newest slot: every slot appended after it was undone first.
            if (index + 1U != nodes_.size()) capacity_fault("node_graph.revert_add_node");
            nodes_.pop_back();
            move_marks_.pop_back();
        } else {
            nodes_[index] = NodeRecord{};
            nodes_[index].generation = record.generation_before;
            require_room(free_nodes_, 1U, "node_graph.revert_add_node");
            free_nodes_.push_back(index);
        }
        break;
    }
    case UndoOperation::remove_node: {
        attach_node(record.node.index, node_payload_[record.node_first]);
        for (std::size_t offset = record.link_count; offset > 0U; --offset) {
            RemovedLink& removed = link_payload_[record.link_first + offset - 1U];
            attach_link(removed.index, removed.record);
        }
        break;
    }
    case UndoOperation::move_nodes: {
        for (std::size_t offset = 0U; offset < record.move_count; ++offset) {
            const MovedNode& moved = move_payload_[record.move_first + offset];
            NodeRecord& node = nodes_[moved.node.index];
            node.bounds.x = moved.before.x;
            node.bounds.y = moved.before.y;
        }
        break;
    }
    case UndoOperation::set_node_bounds: nodes_[record.node.index].bounds = record.bounds_before; break;
    case UndoOperation::set_title:
        nodes_[record.node.index].title.swap(text_payload_[record.text_first].value);
        break;
    case UndoOperation::set_property: {
        std::vector<NodeProperty>& properties = nodes_[record.node.index].properties;
        if (record.replaced) {
            properties[record.position].value.swap(text_payload_[record.text_first].value);
        } else {
            // The newest property: later appends were undone first.
            text_payload_[record.text_first] = std::move(properties.back());
            properties.pop_back();
        }
        break;
    }
    case UndoOperation::remove_property: {
        // The erase kept the vector's capacity, so there is room.
        std::vector<NodeProperty>& properties = nodes_[record.node.index].properties;
        require_room(properties, 1U, "node_graph.revert_remove_property");
        const std::vector<NodeProperty>::iterator at =
            properties.begin() + static_cast<std::ptrdiff_t>(record.position);
        properties.insert(at, std::move(text_payload_[record.text_first]));
        text_payload_[record.text_first] = NodeProperty{};
        break;
    }
    case UndoOperation::connect: {
        const std::uint32_t index = record.link.index;
        RemovedLink& kept = link_payload_[record.link_first];
        ordered_erase(link_key_order_, links_, index);
        kept.record = std::move(links_[index]);
        --live_links_;
        if (record.appended) {
            if (index + 1U != links_.size()) capacity_fault("node_graph.revert_connect");
            links_.pop_back();
        } else {
            links_[index] = LinkRecord{};
            links_[index].generation = record.generation_before;
            require_room(free_links_, 1U, "node_graph.revert_connect");
            free_links_.push_back(index);
        }
        break;
    }
    case UndoOperation::disconnect: {
        RemovedLink& removed = link_payload_[record.link_first];
        attach_link(removed.index, removed.record);
        break;
    }
    default: break;
    }
}

void NodeGraph::reapply(const UndoRecord& record) noexcept {
    switch (record.operation) {
    case UndoOperation::add_node: {
        const std::uint32_t index = record.node.index;
        NodeRecord& kept = node_payload_[record.node_first];
        if (record.appended) {
            // pop_back kept the capacity, so there is room.
            require_room(nodes_, 1U, "node_graph.reapply_add_node");
            require_room(move_marks_, 1U, "node_graph.reapply_add_node");
            nodes_.push_back(std::move(kept));
            move_marks_.push_back(0U);
        } else {
            if (free_nodes_.empty() || free_nodes_.back() != index) capacity_fault("node_graph.reapply_add_node");
            free_nodes_.pop_back();
            nodes_[index] = std::move(kept);
        }
        kept = NodeRecord{};
        ordered_insert(key_order_, nodes_, index);
        ++live_nodes_;
        break;
    }
    case UndoOperation::remove_node: {
        for (std::size_t offset = 0U; offset < record.link_count; ++offset) {
            RemovedLink& removed = link_payload_[record.link_first + offset];
            detach_link(removed.index, removed.record);
        }
        detach_node(record.node.index, node_payload_[record.node_first]);
        break;
    }
    case UndoOperation::move_nodes: {
        for (std::size_t offset = 0U; offset < record.move_count; ++offset) {
            const MovedNode& moved = move_payload_[record.move_first + offset];
            NodeRecord& node = nodes_[moved.node.index];
            node.bounds.x = moved.after.x;
            node.bounds.y = moved.after.y;
        }
        break;
    }
    case UndoOperation::set_node_bounds: nodes_[record.node.index].bounds = record.bounds_after; break;
    case UndoOperation::set_title:
        nodes_[record.node.index].title.swap(text_payload_[record.text_first].value);
        break;
    case UndoOperation::set_property: {
        std::vector<NodeProperty>& properties = nodes_[record.node.index].properties;
        if (record.replaced) {
            properties[record.position].value.swap(text_payload_[record.text_first].value);
        } else {
            require_room(properties, 1U, "node_graph.reapply_set_property");
            properties.push_back(std::move(text_payload_[record.text_first]));
            text_payload_[record.text_first] = NodeProperty{};
        }
        break;
    }
    case UndoOperation::remove_property: {
        std::vector<NodeProperty>& properties = nodes_[record.node.index].properties;
        const std::vector<NodeProperty>::iterator at =
            properties.begin() + static_cast<std::ptrdiff_t>(record.position);
        text_payload_[record.text_first] = std::move(*at);
        properties.erase(at);
        break;
    }
    case UndoOperation::connect: {
        const std::uint32_t index = record.link.index;
        RemovedLink& kept = link_payload_[record.link_first];
        if (record.appended) {
            require_room(links_, 1U, "node_graph.reapply_connect");
            links_.push_back(std::move(kept.record));
        } else {
            if (free_links_.empty() || free_links_.back() != index) capacity_fault("node_graph.reapply_connect");
            free_links_.pop_back();
            links_[index] = std::move(kept.record);
        }
        kept.record = LinkRecord{};
        ordered_insert(link_key_order_, links_, index);
        ++live_links_;
        break;
    }
    case UndoOperation::disconnect: {
        RemovedLink& removed = link_payload_[record.link_first];
        detach_link(removed.index, removed.record);
        break;
    }
    default: break;
    }
}

GraphResult NodeGraph::undo() {
    GraphResult result{};
    if (group_depth_ > 0U) {
        result.status = GraphStatus::group_open;
        return result;
    }
    if (applied_steps_ == first_step_) {
        result.status = GraphStatus::nothing_to_undo;
        return result;
    }
    const UndoStep& step = steps_[applied_steps_ - 1U];
    // Newest record first: each inverse meets the state its record left.
    for (std::size_t offset = step.record_count; offset > 0U; --offset) {
        revert(records_[step.first_record + offset - 1U]);
    }
    --applied_steps_;
    advance();
    return result;
}

GraphResult NodeGraph::redo() {
    GraphResult result{};
    if (group_depth_ > 0U) {
        result.status = GraphStatus::group_open;
        return result;
    }
    if (applied_steps_ == steps_.size()) {
        result.status = GraphStatus::nothing_to_redo;
        return result;
    }
    const UndoStep& step = steps_[applied_steps_];
    for (std::size_t offset = 0U; offset < step.record_count; ++offset) {
        reapply(records_[step.first_record + offset]);
    }
    ++applied_steps_;
    advance();
    return result;
}

// -- the named operations ---------------------------------------------------------

GraphResult NodeGraph::add_node(const NodeSpec& spec) {
    GraphResult result{};
    const GraphStatus checked = check_spec(spec);
    if (checked != GraphStatus::ok) {
        result.status = checked;
        return result;
    }
    if (live(find_node(spec.key))) {
        result.status = GraphStatus::duplicate_key;
        return result;
    }
    const bool reuses = !free_nodes_.empty();
    if (!reuses && nodes_.size() >= std::numeric_limits<std::uint32_t>::max()) {
        result.status = GraphStatus::capacity;
        return result;
    }
    // Copies and reservations first, so a failed allocation leaves the graph
    // unchanged.
    NodeRecord record{};
    record.key = spec.key;
    record.kind = spec.kind;
    record.title = spec.title;
    record.bounds = spec.bounds;
    record.ports = spec.ports;
    record.properties = spec.properties;
    record.live = true;
    const std::size_t bytes = node_bytes(record);
    const bool records = recording();
    if (records) prepare_record(1U, 0U, 0U, 0U);
    if (!reuses) {
        // Reserve sizes: the slots, their move marks, the free list and the
        // key order each to one more than the slot count (the lists by
        // geometric growth), so removing, moving, undoing and redoing never
        // allocate.
        reserve_slot_list(free_nodes_, nodes_.size() + 1U);
        reserve_slot_list(key_order_, nodes_.size() + 1U);
        move_marks_.reserve(nodes_.size() + 1U);
        nodes_.reserve(nodes_.size() + 1U);
    }
    std::uint32_t index = 0U;
    std::uint32_t generation_before = 0U;
    if (reuses) {
        index = free_nodes_.back();
        generation_before = nodes_[index].generation;
        record.generation = next_generation(generation_before);
        nodes_[index] = std::move(record);
        free_nodes_.pop_back();
    } else {
        index = static_cast<std::uint32_t>(nodes_.size());
        record.generation = 1U;
        nodes_.push_back(std::move(record));
        move_marks_.push_back(0U);
    }
    ordered_insert(key_order_, nodes_, index);
    ++live_nodes_;
    result.node = NodeId{index, nodes_[index].generation};
    if (records) {
        UndoRecord undo{};
        undo.operation = UndoOperation::add_node;
        undo.node = result.node;
        undo.generation_before = generation_before;
        undo.appended = !reuses;
        undo.node_first = node_payload_.size();
        undo.link_first = link_payload_.size();
        undo.move_first = move_payload_.size();
        undo.text_first = text_payload_.size();
        // An empty placeholder: undo moves the node here.
        require_room(node_payload_, 1U, "node_graph.add_node");
        node_payload_.emplace_back();
        commit_record(undo, bytes);
    }
    advance();
    return result;
}

GraphResult NodeGraph::remove_node(NodeId node) {
    GraphResult result{};
    if (!live(node)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    std::size_t taken = 0U;
    std::size_t bytes = node_bytes(nodes_[node.index]);
    for (const LinkRecord& record : links_) {
        if (!record.live) continue;
        if (!(record.from.node == node) && !(record.to.node == node)) continue;
        ++taken;
        bytes += sizeof(RemovedLink) + record.key.size();
    }
    const bool records = recording();
    if (records) prepare_record(1U, taken, 0U, 0U);
    UndoRecord undo{};
    undo.operation = UndoOperation::remove_node;
    undo.node = node;
    undo.node_first = node_payload_.size();
    undo.link_first = link_payload_.size();
    undo.link_count = records ? taken : 0U;
    undo.move_first = move_payload_.size();
    undo.text_first = text_payload_.size();
    // Links in ascending slot order, each moved with its ID into the log;
    // undo restores them in reverse, popping the free list entries this
    // pushes.
    for (std::uint32_t index = 0U; index < links_.size(); ++index) {
        const LinkRecord& record = links_[index];
        if (!record.live) continue;
        if (!(record.from.node == node) && !(record.to.node == node)) continue;
        if (records) {
            require_room(link_payload_, 1U, "node_graph.remove_node");
            link_payload_.emplace_back();
            link_payload_.back().index = index;
            detach_link(index, link_payload_.back().record);
        } else {
            LinkRecord discarded{};
            detach_link(index, discarded);
        }
    }
    if (records) {
        require_room(node_payload_, 1U, "node_graph.remove_node");
        node_payload_.emplace_back();
        detach_node(node.index, node_payload_.back());
        commit_record(undo, bytes);
    } else {
        NodeRecord discarded{};
        detach_node(node.index, discarded);
    }
    advance();
    return result;
}

GraphResult NodeGraph::move_nodes(std::span<const NodeId> nodes, WorldPoint delta) {
    GraphResult result{};
    if (nodes.empty() || !std::isfinite(delta.x) || !std::isfinite(delta.y)) {
        result.status = GraphStatus::invalid_argument;
        return result;
    }
    for (const NodeId node : nodes) {
        if (live(node)) continue;
        result.status = GraphStatus::stale_identity;
        return result;
    }
    // A drag's next frame joins the group's previous move record when it
    // names the same identities in the same order: the record keeps the
    // first frame's start and takes this frame's end.
    bool joins = false;
    if (recording() && group_step_open() && !records_.empty()) {
        const UndoRecord& last = records_.back();
        joins = last.operation == UndoOperation::move_nodes && last.move_count == nodes.size();
        for (std::size_t offset = 0U; joins && offset < nodes.size(); ++offset) {
            joins = move_payload_[last.move_first + offset].node == nodes[offset];
        }
    }
    const bool records = recording() && !joins;
    if (records) prepare_record(0U, 0U, nodes.size(), 0U);
    UndoRecord undo{};
    undo.operation = UndoOperation::move_nodes;
    undo.node_first = node_payload_.size();
    undo.link_first = link_payload_.size();
    undo.move_first = move_payload_.size();
    undo.text_first = text_payload_.size();
    // A node listed twice moves once: each moved slot is marked with the
    // revision this move makes, in storage kept with the slots, so a move
    // allocates nothing (a drag moves nodes on every frame).
    const std::uint64_t mark = revision_ + 1U;
    for (const NodeId node : nodes) {
        if (move_marks_[node.index] == mark) continue;
        move_marks_[node.index] = mark;
        NodeRecord& record = nodes_[node.index];
        MovedNode moved{};
        moved.node = node;
        moved.before = WorldPoint{record.bounds.x, record.bounds.y};
        record.bounds.x += delta.x;
        record.bounds.y += delta.y;
        moved.after = WorldPoint{record.bounds.x, record.bounds.y};
        if (records) {
            require_room(move_payload_, 1U, "node_graph.move_nodes");
            move_payload_.push_back(moved);
        }
    }
    if (joins) {
        const UndoRecord& last = records_.back();
        for (std::size_t offset = 0U; offset < last.move_count; ++offset) {
            MovedNode& moved = move_payload_[last.move_first + offset];
            const NodeRecord& record = nodes_[moved.node.index];
            moved.after = WorldPoint{record.bounds.x, record.bounds.y};
        }
    }
    if (records) {
        undo.move_count = move_payload_.size() - undo.move_first;
        commit_record(undo, undo.move_count * sizeof(MovedNode));
    }
    advance();
    return result;
}

GraphResult NodeGraph::set_node_bounds(NodeId node, WorldRect bounds) {
    GraphResult result{};
    if (!live(node)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    if (!finite_rect(bounds)) {
        result.status = GraphStatus::invalid_argument;
        return result;
    }
    const bool records = recording();
    if (records) prepare_record(0U, 0U, 0U, 0U);
    NodeRecord& record = nodes_[node.index];
    UndoRecord undo{};
    undo.operation = UndoOperation::set_node_bounds;
    undo.node = node;
    undo.bounds_before = record.bounds;
    undo.bounds_after = bounds;
    undo.node_first = node_payload_.size();
    undo.link_first = link_payload_.size();
    undo.move_first = move_payload_.size();
    undo.text_first = text_payload_.size();
    record.bounds = bounds;
    if (records) commit_record(undo, 0U);
    advance();
    return result;
}

GraphResult NodeGraph::set_title(NodeId node, std::string_view title) {
    GraphResult result{};
    if (!live(node)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    if (title.size() > maximum_graph_text_bytes) {
        result.status = GraphStatus::invalid_argument;
        return result;
    }
    // The new title is copied before anything changes (it may view the old
    // one); the swap then leaves the old title in the payload, where undo
    // swaps it back.
    NodeProperty kept{};
    kept.value.assign(title);
    NodeRecord& record = nodes_[node.index];
    const bool records = recording();
    const std::size_t bytes = sizeof(NodeProperty) + std::max(title.size(), record.title.size());
    if (records) prepare_record(0U, 0U, 0U, 1U);
    UndoRecord undo{};
    undo.operation = UndoOperation::set_title;
    undo.node = node;
    undo.node_first = node_payload_.size();
    undo.link_first = link_payload_.size();
    undo.move_first = move_payload_.size();
    undo.text_first = text_payload_.size();
    record.title.swap(kept.value);
    if (records) {
        require_room(text_payload_, 1U, "node_graph.set_title");
        text_payload_.push_back(std::move(kept));
        commit_record(undo, bytes);
    }
    advance();
    return result;
}

GraphResult NodeGraph::set_property(NodeId node, std::string_view name, std::string_view value) {
    GraphResult result{};
    if (!live(node)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    if (!node_graph_text::valid_property_name(name) || value.size() > maximum_graph_text_bytes) {
        result.status = GraphStatus::invalid_argument;
        return result;
    }
    std::vector<NodeProperty>& properties = nodes_[node.index].properties;
    const std::size_t position = property_position(properties, name);
    const bool replaces = position < properties.size();
    if (!replaces && properties.size() >= maximum_node_properties) {
        result.status = GraphStatus::capacity;
        return result;
    }
    // Copies first (name and value may view this node's own texts), then
    // reservations; only sizes are read from the views after this.
    NodeProperty kept{};
    if (!replaces) kept.name.assign(name);
    kept.value.assign(value);
    std::size_t bytes = sizeof(NodeProperty) + name.size() + value.size();
    if (replaces) bytes = sizeof(NodeProperty) + std::max(value.size(), properties[position].value.size());
    if (!replaces) properties.reserve(properties.size() + 1U);
    const bool records = recording();
    if (records) prepare_record(0U, 0U, 0U, 1U);
    UndoRecord undo{};
    undo.operation = UndoOperation::set_property;
    undo.node = node;
    undo.replaced = replaces;
    undo.position = static_cast<std::uint32_t>(position);
    undo.node_first = node_payload_.size();
    undo.link_first = link_payload_.size();
    undo.move_first = move_payload_.size();
    undo.text_first = text_payload_.size();
    if (records) require_room(text_payload_, 1U, "node_graph.set_property");
    if (replaces) {
        properties[position].value.swap(kept.value);
        if (records) text_payload_.push_back(std::move(kept));
    } else {
        require_room(properties, 1U, "node_graph.set_property");
        properties.push_back(std::move(kept));
        // An empty placeholder: undo moves the property here.
        if (records) text_payload_.emplace_back();
    }
    if (records) commit_record(undo, bytes);
    advance();
    return result;
}

GraphResult NodeGraph::remove_property(NodeId node, std::string_view name) {
    GraphResult result{};
    if (!live(node)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    std::vector<NodeProperty>& properties = nodes_[node.index].properties;
    const std::size_t position = property_position(properties, name);
    if (position >= properties.size()) {
        result.status = GraphStatus::missing_property;
        return result;
    }
    const bool records = recording();
    const std::size_t bytes =
        sizeof(NodeProperty) + properties[position].name.size() + properties[position].value.size();
    if (records) prepare_record(0U, 0U, 0U, 1U);
    UndoRecord undo{};
    undo.operation = UndoOperation::remove_property;
    undo.node = node;
    undo.position = static_cast<std::uint32_t>(position);
    undo.node_first = node_payload_.size();
    undo.link_first = link_payload_.size();
    undo.move_first = move_payload_.size();
    undo.text_first = text_payload_.size();
    const std::vector<NodeProperty>::iterator at = properties.begin() + static_cast<std::ptrdiff_t>(position);
    if (records) {
        require_room(text_payload_, 1U, "node_graph.remove_property");
        text_payload_.push_back(std::move(*at));
    }
    properties.erase(at);
    if (records) commit_record(undo, bytes);
    advance();
    return result;
}

GraphResult NodeGraph::set_port_lamp(PortRef port, PortLamp lamp) {
    GraphResult result{};
    if (!live(port.node)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    NodeRecord& record = nodes_[port.node.index];
    if (port.port.value >= record.ports.size()) {
        result.status = GraphStatus::bad_port;
        return result;
    }
    if (lamp != PortLamp::off && lamp != PortLamp::on && lamp != PortLamp::error) {
        result.status = GraphStatus::invalid_argument;
        return result;
    }
    record.ports[port.port.value].lamp = lamp;
    advance();
    return result;
}

GraphStatus NodeGraph::check_link(PortRef from, PortRef to) const noexcept {
    if (!live(from.node) || !live(to.node)) return GraphStatus::stale_identity;
    const NodeRecord& source = nodes_[from.node.index];
    const NodeRecord& target = nodes_[to.node.index];
    if (from.port.value >= source.ports.size() || to.port.value >= target.ports.size()) {
        return GraphStatus::bad_port;
    }
    if (from.node == to.node) return GraphStatus::same_node;
    if (source.ports[from.port.value].direction != PortDirection::output ||
        target.ports[to.port.value].direction != PortDirection::input) {
        return GraphStatus::wrong_direction;
    }
    if (live(link_into(to))) return GraphStatus::occupied;
    const bool reuses = !free_links_.empty();
    if (!reuses && links_.size() >= std::numeric_limits<std::uint32_t>::max()) return GraphStatus::capacity;
    return GraphStatus::ok;
}

GraphResult NodeGraph::connect(PortRef from, PortRef to) {
    GraphResult result{};
    const GraphStatus checked = check_link(from, to);
    if (checked != GraphStatus::ok) {
        result.status = checked;
        return result;
    }
    // The next unused "l.<n>"; the counter only increases, so the loop runs
    // once unless a caller chose IDs of this shape.
    std::string key{};
    while (true) {
        key.assign("l.");
        std::array<char, 24> digits{};
        const std::to_chars_result written =
            std::to_chars(digits.data(), digits.data() + digits.size(), next_link_number_);
        key.append(digits.data(), static_cast<std::size_t>(written.ptr - digits.data()));
        ++next_link_number_;
        if (!live(find_link(key))) break;
    }
    result = install_link(from, to, key);
    return result;
}

GraphResult NodeGraph::connect(PortRef from, PortRef to, std::string_view key) {
    GraphResult result{};
    const GraphStatus checked = check_link(from, to);
    if (checked != GraphStatus::ok) {
        result.status = checked;
        return result;
    }
    if (!node_graph_text::valid_key(key)) {
        result.status = GraphStatus::invalid_argument;
        return result;
    }
    if (live(find_link(key))) {
        result.status = GraphStatus::duplicate_key;
        return result;
    }
    std::string owned(key);
    result = install_link(from, to, owned);
    return result;
}

// Validated by check_link; `key` is a fresh, unused ID, moved into the link.
GraphResult NodeGraph::install_link(PortRef from, PortRef to, std::string& key) {
    GraphResult result{};
    const bool reuses = !free_links_.empty();
    const std::size_t bytes = sizeof(RemovedLink) + key.size();
    const bool records = recording();
    if (records) prepare_record(0U, 1U, 0U, 0U);
    if (!reuses) {
        // Reserve sizes: the link slots, their free list and their ID order
        // each to one more than the slot count, so disconnecting, removing,
        // undoing and redoing never allocate.
        reserve_slot_list(free_links_, links_.size() + 1U);
        reserve_slot_list(link_key_order_, links_.size() + 1U);
        links_.reserve(links_.size() + 1U);
    }
    LinkRecord record{};
    record.key = std::move(key);
    record.from = from;
    record.to = to;
    record.live = true;
    std::uint32_t index = 0U;
    std::uint32_t generation_before = 0U;
    if (reuses) {
        index = free_links_.back();
        generation_before = links_[index].generation;
        record.generation = next_generation(generation_before);
        links_[index] = std::move(record);
        free_links_.pop_back();
    } else {
        index = static_cast<std::uint32_t>(links_.size());
        record.generation = 1U;
        require_room(links_, 1U, "node_graph.connect");
        links_.push_back(std::move(record));
    }
    ordered_insert(link_key_order_, links_, index);
    ++live_links_;
    result.link = LinkId{index, links_[index].generation};
    if (records) {
        UndoRecord undo{};
        undo.operation = UndoOperation::connect;
        undo.link = result.link;
        undo.generation_before = generation_before;
        undo.appended = !reuses;
        undo.node_first = node_payload_.size();
        undo.link_first = link_payload_.size();
        undo.link_count = 1U;
        undo.move_first = move_payload_.size();
        undo.text_first = text_payload_.size();
        // A placeholder: undo moves the link (with its ID) here.
        require_room(link_payload_, 1U, "node_graph.connect");
        link_payload_.emplace_back();
        link_payload_.back().index = index;
        commit_record(undo, bytes);
    }
    advance();
    return result;
}

GraphResult NodeGraph::disconnect(LinkId link) {
    GraphResult result{};
    if (!live(link)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    const bool records = recording();
    const std::size_t bytes = sizeof(RemovedLink) + links_[link.index].key.size();
    if (records) prepare_record(0U, 1U, 0U, 0U);
    UndoRecord undo{};
    undo.operation = UndoOperation::disconnect;
    undo.link = link;
    undo.node_first = node_payload_.size();
    undo.link_first = link_payload_.size();
    undo.link_count = 1U;
    undo.move_first = move_payload_.size();
    undo.text_first = text_payload_.size();
    if (records) {
        require_room(link_payload_, 1U, "node_graph.disconnect");
        link_payload_.emplace_back();
        link_payload_.back().index = link.index;
        detach_link(link.index, link_payload_.back().record);
        commit_record(undo, bytes);
    } else {
        LinkRecord discarded{};
        detach_link(link.index, discarded);
    }
    advance();
    return result;
}

// -- reading ----------------------------------------------------------------------

const NodeRecord* NodeGraph::node(NodeId node) const noexcept {
    if (!live(node)) return nullptr;
    return &nodes_[node.index];
}

const NodeProperty* NodeGraph::property(NodeId node, std::string_view name) const noexcept {
    if (!live(node)) return nullptr;
    const std::vector<NodeProperty>& properties = nodes_[node.index].properties;
    const std::size_t position = property_position(properties, name);
    if (position >= properties.size()) return nullptr;
    return &properties[position];
}

const LinkRecord* NodeGraph::link(LinkId link) const noexcept {
    if (!live(link)) return nullptr;
    return &links_[link.index];
}

const PortRecord* NodeGraph::port(PortRef port) const noexcept {
    if (!live(port.node)) return nullptr;
    const NodeRecord& record = nodes_[port.node.index];
    if (port.port.value >= record.ports.size()) return nullptr;
    return &record.ports[port.port.value];
}

LinkId NodeGraph::link_into(PortRef input) const noexcept {
    for (std::uint32_t index = 0U; index < links_.size(); ++index) {
        const LinkRecord& record = links_[index];
        if (record.live && record.to == input) return LinkId{index, record.generation};
    }
    return LinkId{};
}

WorldPoint port_anchor(const NodeRecord& node, PortSide side, double offset) noexcept {
    const WorldRect& bounds = node.bounds;
    WorldPoint anchor{};
    switch (side) {
    case PortSide::left: anchor = WorldPoint{bounds.x, bounds.y + bounds.height * offset}; break;
    case PortSide::right: anchor = WorldPoint{bounds.right(), bounds.y + bounds.height * offset}; break;
    case PortSide::top: anchor = WorldPoint{bounds.x + bounds.width * offset, bounds.y}; break;
    case PortSide::bottom: anchor = WorldPoint{bounds.x + bounds.width * offset, bounds.bottom()}; break;
    case PortSide::automatic:
    default:
        // Automatic is resolved by the caller; the node's centre stands in.
        anchor = WorldPoint{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
        break;
    }
    return anchor;
}

} // namespace gui_forms
