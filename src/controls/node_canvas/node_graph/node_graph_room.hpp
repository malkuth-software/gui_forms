#pragma once
// The node graph's capacity checks. Its no-fail paths (undo, redo, the end
// of each operation, a load's install) push and insert into storage reserved
// earlier; each push checks that the reservation is there and fails fast if
// it is not, so a broken reservation argument is reported where it breaks
// instead of becoming a reallocation (or an exception) inside a noexcept
// path.

#include "../../../core/failure/fail_fast.hpp"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>

namespace gui_forms::node_graph_room {

// A reservation the node graph relies on is missing: an internal defect,
// never an expected outcome. Ends the process through the fail-fast report;
// `site` names the push.
[[noreturn]] inline void capacity_fault(const char* site) noexcept {
    detail::fail_fast(site, std::make_exception_ptr(std::logic_error("node graph: reserved capacity missing")));
}

// Checks that `extra` more entries fit `list` without reallocating.
template <typename Entry>
void require_room(const std::vector<Entry>& list, std::size_t extra, const char* site) noexcept {
    if (list.capacity() - list.size() >= extra) return;
    capacity_fault(site);
}

} // namespace gui_forms::node_graph_room
