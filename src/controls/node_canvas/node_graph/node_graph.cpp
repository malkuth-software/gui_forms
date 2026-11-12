#include "gui_forms/controls/node_graph.hpp"

#include "node_graph_text.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

// The named operations and the undo log. Every recorded operation runs in
// three phases: validate (a refusal changes nothing, log included), prepare
// (make room in every array the operation and its record will touch; this is
// the only phase that allocates), then change and record, which cannot fail.
// undo and redo replay records over storage the records already own, so they
// allocate nothing and cannot fail either; the free lists, slot counts and
// generations they restore are exactly those before the operation, because
// steps are undone strictly newest first and redone oldest first. Documents
// are in node_graph_document.cpp.

namespace gui_forms {
namespace {

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

// Orders key_order_ entries (node slots) by their node's key.
struct SlotKeyBefore final {
    const std::vector<NodeRecord>* nodes{};
    bool operator()(std::uint32_t slot, std::string_view key) const noexcept {
        const bool before = std::string_view((*nodes)[slot].key) < key;
        return before;
    }
};

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

std::size_t NodeGraph::key_position(std::string_view key) const noexcept {
    const SlotKeyBefore before{&nodes_};
    const std::vector<std::uint32_t>::const_iterator found =
        std::lower_bound(key_order_.begin(), key_order_.end(), key, before);
    const std::size_t position = static_cast<std::size_t>(found - key_order_.begin());
    return position;
}

// key_order_'s capacity is at least the slot count (add_node and load reserve
// it with the slots) and a slot being attached is not in it, so this insert
// never allocates.
void NodeGraph::insert_key(std::uint32_t index) noexcept {
    const std::size_t position = key_position(nodes_[index].key);
    const std::vector<std::uint32_t>::iterator at = key_order_.begin() + static_cast<std::ptrdiff_t>(position);
    key_order_.insert(at, index);
}

void NodeGraph::erase_key(std::uint32_t index) noexcept {
    const std::size_t position = key_position(nodes_[index].key);
    if (position >= key_order_.size() || key_order_[position] != index) return;
    const std::vector<std::uint32_t>::iterator at = key_order_.begin() + static_cast<std::ptrdiff_t>(position);
    key_order_.erase(at);
}

NodeId NodeGraph::find_node(std::string_view key) const noexcept {
    const std::size_t position = key_position(key);
    if (position >= key_order_.size()) return NodeId{};
    const std::uint32_t index = key_order_[position];
    const NodeRecord& record = nodes_[index];
    if (std::string_view(record.key) != key) return NodeId{};
    return NodeId{index, record.generation};
}

// -- the undo log -----------------------------------------------------------------

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
    erase_key(index);
    NodeRecord& slot = nodes_[index];
    const std::uint32_t generation = slot.generation;
    keep = std::move(slot);
    slot = NodeRecord{};
    slot.generation = generation;
    free_nodes_.push_back(index);
    --live_nodes_;
}

// The inverse of detach_node: the slot is the free list's newest entry.
void NodeGraph::attach_node(std::uint32_t index, NodeRecord& kept) noexcept {
    free_nodes_.pop_back();
    nodes_[index] = std::move(kept);
    kept = NodeRecord{};
    insert_key(index);
    ++live_nodes_;
}

void NodeGraph::revert(const UndoRecord& record) noexcept {
    switch (record.operation) {
    case UndoOperation::add_node: {
        const std::uint32_t index = record.node.index;
        erase_key(index);
        node_payload_[record.node_first] = std::move(nodes_[index]);
        --live_nodes_;
        if (record.appended) {
            // The newest slot: every slot appended after it was undone first.
            nodes_.pop_back();
            move_marks_.pop_back();
        } else {
            nodes_[index] = NodeRecord{};
            nodes_[index].generation = record.generation_before;
            free_nodes_.push_back(index);
        }
        break;
    }
    case UndoOperation::remove_node: {
        attach_node(record.node.index, node_payload_[record.node_first]);
        for (std::size_t offset = record.link_count; offset > 0U; --offset) {
            const RemovedLink& removed = link_payload_[record.link_first + offset - 1U];
            free_links_.pop_back();
            links_[removed.index] = removed.record;
            ++live_links_;
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
        // The erase kept the vector's capacity, so this insert does not
        // allocate.
        std::vector<NodeProperty>& properties = nodes_[record.node.index].properties;
        const std::vector<NodeProperty>::iterator at =
            properties.begin() + static_cast<std::ptrdiff_t>(record.position);
        properties.insert(at, std::move(text_payload_[record.text_first]));
        text_payload_[record.text_first] = NodeProperty{};
        break;
    }
    case UndoOperation::connect: {
        const std::uint32_t index = record.link.index;
        --live_links_;
        if (record.appended) {
            links_.pop_back();
        } else {
            LinkRecord& slot = links_[index];
            slot.live = false;
            slot.generation = record.generation_before;
            free_links_.push_back(index);
        }
        break;
    }
    case UndoOperation::disconnect: {
        free_links_.pop_back();
        LinkRecord& slot = links_[record.link.index];
        slot.from = record.from;
        slot.to = record.to;
        slot.generation = record.link.generation;
        slot.live = true;
        ++live_links_;
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
            // pop_back kept the capacity, so these pushes do not allocate.
            nodes_.push_back(std::move(kept));
            move_marks_.push_back(0U);
        } else {
            free_nodes_.pop_back();
            nodes_[index] = std::move(kept);
        }
        kept = NodeRecord{};
        insert_key(index);
        ++live_nodes_;
        break;
    }
    case UndoOperation::remove_node: {
        for (std::size_t offset = 0U; offset < record.link_count; ++offset) {
            const RemovedLink& removed = link_payload_[record.link_first + offset];
            free_link(removed.index);
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
        LinkRecord link{};
        link.from = record.from;
        link.to = record.to;
        link.generation = record.link.generation;
        link.live = true;
        if (record.appended) {
            links_.push_back(link);
        } else {
            free_links_.pop_back();
            links_[record.link.index] = link;
        }
        ++live_links_;
        break;
    }
    case UndoOperation::disconnect: free_link(record.link.index); break;
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
        // The slot-indexed lists grow with the slots, so removing, moving,
        // undoing and redoing never allocate.
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
    insert_key(index);
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
        node_payload_.emplace_back();
        commit_record(undo, bytes);
    }
    advance();
    return result;
}

void NodeGraph::free_link(std::uint32_t index) noexcept {
    LinkRecord& record = links_[index];
    record.live = false;
    // The free list was reserved to the link slots' count, so this never
    // allocates (see connect).
    free_links_.push_back(index);
    --live_links_;
}

GraphResult NodeGraph::remove_node(NodeId node) {
    GraphResult result{};
    if (!live(node)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    std::size_t taken = 0U;
    for (const LinkRecord& record : links_) {
        if (!record.live) continue;
        if (record.from.node == node || record.to.node == node) ++taken;
    }
    const std::size_t bytes = node_bytes(nodes_[node.index]) + taken * sizeof(RemovedLink);
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
    // Links in ascending slot order; undo restores them in reverse, popping
    // the free list entries this pushes.
    for (std::uint32_t index = 0U; index < links_.size(); ++index) {
        const LinkRecord& record = links_[index];
        if (!record.live) continue;
        if (!(record.from.node == node) && !(record.to.node == node)) continue;
        if (records) {
            RemovedLink removed{};
            removed.index = index;
            removed.record = record;
            link_payload_.push_back(removed);
        }
        free_link(index);
    }
    if (records) {
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
        if (records) move_payload_.push_back(moved);
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
    if (replaces) {
        properties[position].value.swap(kept.value);
        if (records) text_payload_.push_back(std::move(kept));
    } else {
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
    if (records) text_payload_.push_back(std::move(*at));
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

GraphResult NodeGraph::connect(PortRef from, PortRef to) {
    GraphResult result{};
    if (!live(from.node) || !live(to.node)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    const NodeRecord& source = nodes_[from.node.index];
    const NodeRecord& target = nodes_[to.node.index];
    if (from.port.value >= source.ports.size() || to.port.value >= target.ports.size()) {
        result.status = GraphStatus::bad_port;
        return result;
    }
    if (from.node == to.node) {
        result.status = GraphStatus::same_node;
        return result;
    }
    if (source.ports[from.port.value].direction != PortDirection::output ||
        target.ports[to.port.value].direction != PortDirection::input) {
        result.status = GraphStatus::wrong_direction;
        return result;
    }
    if (live(link_into(to))) {
        result.status = GraphStatus::occupied;
        return result;
    }
    const bool reuses = !free_links_.empty();
    if (!reuses && links_.size() >= std::numeric_limits<std::uint32_t>::max()) {
        result.status = GraphStatus::capacity;
        return result;
    }
    const bool records = recording();
    if (records) prepare_record(0U, 0U, 0U, 0U);
    if (!reuses) {
        // Grow the free list with the slots, so freeing a link never
        // allocates (free_link runs inside remove_node, after validation).
        reserve_slot_list(free_links_, links_.size() + 1U);
        links_.reserve(links_.size() + 1U);
    }
    LinkRecord record{};
    record.from = from;
    record.to = to;
    record.live = true;
    std::uint32_t index = 0U;
    std::uint32_t generation_before = 0U;
    if (reuses) {
        index = free_links_.back();
        generation_before = links_[index].generation;
        record.generation = next_generation(generation_before);
        links_[index] = record;
        free_links_.pop_back();
    } else {
        index = static_cast<std::uint32_t>(links_.size());
        record.generation = 1U;
        links_.push_back(record);
    }
    ++live_links_;
    result.link = LinkId{index, links_[index].generation};
    if (records) {
        UndoRecord undo{};
        undo.operation = UndoOperation::connect;
        undo.link = result.link;
        undo.from = from;
        undo.to = to;
        undo.generation_before = generation_before;
        undo.appended = !reuses;
        undo.node_first = node_payload_.size();
        undo.link_first = link_payload_.size();
        undo.move_first = move_payload_.size();
        undo.text_first = text_payload_.size();
        commit_record(undo, 0U);
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
    if (records) prepare_record(0U, 0U, 0U, 0U);
    const LinkRecord& record = links_[link.index];
    UndoRecord undo{};
    undo.operation = UndoOperation::disconnect;
    undo.link = link;
    undo.from = record.from;
    undo.to = record.to;
    undo.node_first = node_payload_.size();
    undo.link_first = link_payload_.size();
    undo.move_first = move_payload_.size();
    undo.text_first = text_payload_.size();
    free_link(link.index);
    if (records) commit_record(undo, 0U);
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
