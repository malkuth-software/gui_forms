#include "windows_semantic_source.hpp"

#include <climits>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms::host {

namespace {

// Which nodes a search passes through: control-backed nodes only (looking
// for a scope; a control-backed node never lies below a virtual one), or
// virtual nodes only (looking for a virtual child below its scope; a
// control-backed node there has its own scope).
enum class SearchKind : std::uint8_t { controls, virtual_children };

bool search_children(const SemanticLocation& at, std::uint64_t runtime_id, SearchKind kind,
                     SemanticLocation& found) noexcept {
    const std::vector<SemanticNode>& children = (*at.node).children;
    for (std::size_t index = 0; index < children.size(); ++index) {
        const SemanticNode& child = children[index];
        const bool backed = control_backed(child);
        if (kind == SearchKind::controls && !backed) continue;
        if (kind == SearchKind::virtual_children && backed) continue;
        const SemanticLocation child_at = child_location(at, index);
        if (child.runtime_id == runtime_id) {
            found = child_at;
            return true;
        }
        if (search_children(child_at, runtime_id, kind, found)) return true;
    }
    return false;
}

// The window title as UTF-8, or empty.
std::string window_title(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(length) + 1U, L'\0');
    const int copied = GetWindowTextW(hwnd, wide.data(), length + 1);
    if (copied <= 0) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), copied, nullptr, 0,
                                          nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string title(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), copied, title.data(), bytes, nullptr, nullptr);
    return title;
}

} // namespace

bool operator==(SemanticElementId left, SemanticElementId right) noexcept {
    const bool equal = left.scope == right.scope && left.node == right.node;
    return equal;
}

bool operator!=(SemanticElementId left, SemanticElementId right) noexcept {
    const bool different = !(left == right);
    return different;
}

bool control_backed(const SemanticNode& node) noexcept {
    const bool backed = node.runtime_id != 0U && !is_virtual_semantic_runtime_id(node.runtime_id);
    return backed;
}

SemanticLocation client_location(const SemanticNode& client) noexcept {
    SemanticLocation location;
    location.node = &client;
    return location;
}

SemanticLocation child_location(const SemanticLocation& parent, std::size_t index) noexcept {
    const SemanticNode& child = (*parent.node).children[index];
    SemanticLocation location;
    location.node = &child;
    location.parent = parent.node;
    location.index = index;
    if (control_backed(child)) {
        location.scope = &child;
        location.id = SemanticElementId{child.runtime_id, child.runtime_id};
    } else {
        location.scope = parent.scope;
        const std::uint64_t scope = parent.scope == nullptr ? 0U : (*parent.scope).runtime_id;
        location.id = SemanticElementId{scope, child.runtime_id};
    }
    return location;
}

bool locate_semantic_element(const SemanticNode& client, SemanticElementId id,
                             SemanticLocation& location) noexcept {
    const SemanticLocation client_at = client_location(client);
    if (id.node == 0U) {
        if (id.scope != 0U) return false;
        location = client_at;
        return true;
    }
    SemanticLocation scope_at = client_at;
    if (id.scope != 0U && !search_children(client_at, id.scope, SearchKind::controls, scope_at)) {
        return false;
    }
    if (id.node == id.scope) {
        location = scope_at;
        return true;
    }
    SemanticLocation found;
    if (!search_children(scope_at, id.node, SearchKind::virtual_children, found)) return false;
    location = found;
    return true;
}

bool parent_semantic_location(const SemanticNode& client, const SemanticLocation& location,
                              SemanticLocation& parent) noexcept {
    if (location.parent == nullptr) return false;
    const SemanticNode& node = *location.parent;
    SemanticElementId id;
    if (location.parent == &client) {
        id = SemanticElementId{};
    } else if (control_backed(node)) {
        id = SemanticElementId{node.runtime_id, node.runtime_id};
    } else {
        // A virtual parent shares its child's scope: no control-backed node
        // lies between them.
        const std::uint64_t scope = location.scope == nullptr ? 0U : (*location.scope).runtime_id;
        id = SemanticElementId{scope, node.runtime_id};
    }
    const bool found = locate_semantic_element(client, id, parent);
    return found;
}

namespace {

bool contains_point(const SemanticNode& node, double x, double y) noexcept {
    const Rect bounds = node.bounds;
    const bool inside = x >= bounds.x && y >= bounds.y && x < bounds.x + bounds.width &&
                        y < bounds.y + bounds.height;
    return inside;
}

} // namespace

bool hit_semantic_location(const SemanticLocation& at, double x, double y,
                           SemanticLocation& hit) noexcept {
    const std::size_t count = (*at.node).children.size();
    for (std::size_t index = count; index > 0U; --index) {
        const SemanticLocation child = child_location(at, index - 1U);
        if (hit_semantic_location(child, x, y, hit)) return true;
    }
    const bool visible = has_semantic_state((*at.node).states, SemanticState::visible);
    if (visible && contains_point(*at.node, x, y)) {
        hit = at;
        return true;
    }
    return false;
}

bool find_state_location(const SemanticLocation& at, SemanticState state,
                         SemanticLocation& found) noexcept {
    if (has_semantic_state((*at.node).states, state)) {
        found = at;
        return true;
    }
    const std::size_t count = (*at.node).children.size();
    for (std::size_t index = 0; index < count; ++index) {
        const SemanticLocation child = child_location(at, index);
        if (find_state_location(child, state, found)) return true;
    }
    return false;
}

bool find_control_location(const SemanticLocation& at, std::string_view stable_id,
                           SemanticLocation& found) noexcept {
    const std::size_t count = (*at.node).children.size();
    for (std::size_t index = 0; index < count; ++index) {
        const SemanticLocation child = child_location(at, index);
        // A control-backed node never lies below a virtual one.
        if (!control_backed(*child.node)) continue;
        if ((*child.node).stable_id == stable_id) {
            found = child;
            return true;
        }
        if (find_control_location(child, stable_id, found)) return true;
    }
    return false;
}

bool semantic_has_action(const SemanticNode& node, SemanticAction action) noexcept {
    for (const SemanticAction published : node.actions) {
        if (published == action) return true;
    }
    return false;
}

bool checkable_role(SemanticRole role) noexcept {
    const SemanticRole acting = semantic_role_fallback(role);
    const bool check = acting == SemanticRole::check_box || acting == SemanticRole::check_list_item;
    return check;
}

HRESULT utf8_to_bstr(std::string_view value, BSTR* result) noexcept {
    *result = nullptr;
    if (value.empty()) return S_FALSE;
    if (value.size() > static_cast<std::size_t>(INT_MAX)) return E_FAIL;
    const int size = static_cast<int>(value.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), size,
                                          nullptr, 0);
    if (count <= 0) return E_FAIL;
    BSTR text = SysAllocStringLen(nullptr, static_cast<UINT>(count));
    if (text == nullptr) return E_OUTOFMEMORY;
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), size, text, count);
    *result = text;
    return S_OK;
}

bool utf8_from_wide(std::wstring_view value, std::string& text) {
    text.clear();
    if (value.empty()) return true;
    if (value.size() > static_cast<std::size_t>(INT_MAX)) return false;
    const int units = static_cast<int>(value.size());
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), units,
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return false;
    text.resize(static_cast<std::size_t>(size));
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), units, text.data(), size,
                        nullptr, nullptr);
    return true;
}

bool operator==(const SemanticTreeKey& left, const SemanticTreeKey& right) noexcept {
    const bool equal = left.semantic_generation == right.semantic_generation &&
                       left.content_revision == right.content_revision &&
                       left.display_generation == right.display_generation &&
                       left.focused == right.focused && left.pressed == right.pressed &&
                       left.captured == right.captured && left.active == right.active &&
                       left.client_width == right.client_width &&
                       left.client_height == right.client_height &&
                       left.scale == right.scale && left.title == right.title;
    return equal;
}

bool operator!=(const SemanticTreeKey& left, const SemanticTreeKey& right) noexcept {
    const bool different = !(left == right);
    return different;
}

namespace {

std::uint64_t control_id(const Control::Ptr& control) noexcept {
    const std::uint64_t id = control ? (*control).runtime_id().value : 0U;
    return id;
}

// The key `window` and `hwnd` give now. Throws std::bad_alloc when memory
// runs out.
SemanticTreeKey read_tree_key(const Window& window, HWND hwnd, double scale) {
    SemanticTreeKey key;
    key.semantic_generation = window.semantic_generation();
    key.content_revision = window.paint_lease_snapshot().content_revision;
    key.display_generation = window.metrics_snapshot().display_generation;
    key.focused = control_id(window.focused_control());
    key.pressed = control_id(window.pressed_control());
    key.captured = control_id(window.captured_control());
    key.active = window.active();
    RECT bounds{};
    GetClientRect(hwnd, &bounds);
    key.client_width = bounds.right;
    key.client_height = bounds.bottom;
    key.scale = scale;
    key.title = window_title(hwnd);
    return key;
}

} // namespace

bool SemanticSource::snapshot(SemanticTreeRef& client) noexcept {
    client.reset();
    if (model == nullptr || hwnd == nullptr) return false;
    try {
        const SemanticTreeKey before = read_tree_key(*model, hwnd, scale);
        if (current && before == current_key) {
            ++trees_reused;
            client = current;
            return true;
        }
        SemanticSnapshot tree = (*model).semantic_snapshot();
        // The key is read again after the build, which may lay the window
        // out first and so move its revisions.
        SemanticTreeKey after = read_tree_key(*model, hwnd, scale);
        std::shared_ptr<SemanticNode> built = std::make_shared<SemanticNode>();
        SemanticNode& node = *built;
        node.runtime_id = 0U;
        node.stable_id.clear();
        node.role = SemanticRole::group;
        node.children = std::move(tree.roots);
        node.states = SemanticState::enabled | SemanticState::visible;
        node.name = after.title;
        node.bounds = Rect{0.0, 0.0, after.client_width / scale, after.client_height / scale};
        current = std::move(built);
        current_key = std::move(after);
        ++trees_built;
        client = current;
        return true;
    } catch (...) {
        return false;
    }
}

void SemanticSource::release_trees() noexcept {
    current.reset();
    current_key = SemanticTreeKey{};
    published.reset();
}

bool SemanticSource::perform(const SemanticLocation& location, SemanticAction action,
                             std::string_view value) noexcept {
    // The client node takes no action of its own.
    if (model == nullptr || (*location.node).runtime_id == 0U) return false;
    try {
        if (action == SemanticAction::focus) SetFocus(hwnd);
        const SemanticNode& node = *location.node;
        bool handled = false;
        if (location.scope == nullptr) {
            // A virtual child with no control-backed node above it: no
            // control names it, so the window looks for it by stable ID
            // under its root and overlays.
            handled = (*model).perform_semantic_action(node.stable_id, action, value);
        } else {
            // The window refuses a scope whose control was disposed or
            // replaced since the snapshot (interview GF017).
            const RuntimeId scope{(*location.scope).runtime_id};
            std::string_view virtual_child;
            if (location.scope != location.node) virtual_child = node.stable_id;
            handled = (*model).perform_semantic_action(scope, virtual_child, action, value);
        }
        if (handled) NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, hwnd, OBJID_CLIENT, CHILDID_SELF);
        return handled;
    } catch (...) {
        return false;
    }
}

} // namespace gui_forms::host
