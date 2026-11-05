#include "sub_surface_tool_tips.hpp"

#include "sub_surface_channel.hpp"
#include "sub_surface_request_posts.hpp"

#include <utility>
#include <vector>

namespace gui_forms::detail {

namespace {

// The node among `nodes` and their children whose stable ID is `stable_id`,
// or null. The pointer is borrowed from `nodes`.
[[nodiscard]] const SemanticNode* node_with_id(const std::vector<SemanticNode>& nodes, std::string_view stable_id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) return &node;
        const SemanticNode* deeper = node_with_id(node.children, stable_id);
        if (deeper != nullptr) return deeper;
    }
    return nullptr;
}
const SemanticNode* node_with_id(std::vector<SemanticNode>&& nodes, std::string_view stable_id) = delete;

// The semantic description of the thing `subject` names: its part's, or
// the control's own.
[[nodiscard]] std::string description_of(const ToolTipSubject& subject) {
    const Control::Ptr control = subject.control.lock();
    if (!control) return {};
    if (subject.item_id.empty()) {
        const SemanticDescriptor descriptor = (*control).semantic_descriptor();
        return descriptor.description;
    }
    const std::vector<SemanticNode> items = (*control).tool_tip_items();
    const SemanticNode* item = node_with_id(items, subject.item_id);
    if (item == nullptr) return {};
    return (*item).description;
}

// The same thing, text and anchor: nothing to ask again.
[[nodiscard]] bool unchanged(const ToolTipSubject& left, const ToolTipSubject& right) noexcept {
    const bool same = left.control.lock() == right.control.lock() && left.item_id == right.item_id &&
                      left.text == right.text && left.anchor == right.anchor;
    return same;
}

} // namespace

SubSurfaceToolTipRequester::SubSurfaceToolTipRequester(SubSurface& root) noexcept : root_(root) {}

SubSurfaceToolTipRequester::~SubSurfaceToolTipRequester() {
    shutdown();
}

// --- Posting ----------------------------------------------------------------

void SubSurfaceToolTipRequester::revoke(std::optional<OpenRequest>& slot) {
    if (!slot) return;
    const std::uint64_t id = (*slot).id;
    slot.reset();
    if (shown_id_ == id) shown_id_ = 0U;
    if (!root_.channel_) return;
    WindowDispatcher parent;
    std::weak_ptr<SubSurfaceRequestTarget> target;
    {
        const std::lock_guard<std::mutex> lock((*root_.channel_).mutex);
        parent = (*root_.channel_).request_dispatcher;
        target = (*root_.channel_).request_target;
        ++(*root_.channel_).statistics.requests_revoked;
    }
    if (target.expired()) return;
    // Posted outside the channel's lock (lock order: the parent's
    // dispatcher is never taken under it). A closed parent drops it.
    static_cast<void>(parent.begin_invoke(DeliverSubSurfaceRevoke(std::move(target), id)));
}

void SubSurfaceToolTipRequester::post(SubSurfaceToolTipOrigin origin, ToolTipSubject subject, std::string description,
                                      std::optional<std::chrono::milliseconds> duration,
                                      std::optional<OpenRequest>& slot) {
    if (!root_.channel_) return;
    SubSurfaceRequest request;
    request.kind = SubSurfaceRequestKind::tool_tip;
    request.content_epoch = root_.content_epoch_;
    request.input_sequence = root_.input_sequence_;
    request.anchor = subject.anchor;
    request.text = subject.text;
    request.description = description;
    request.origin = origin;
    request.duration = duration;
    WindowDispatcher parent;
    std::weak_ptr<SubSurfaceRequestTarget> target;
    {
        const std::lock_guard<std::mutex> lock((*root_.channel_).mutex);
        detail::SubSurfaceChannel& channel = *root_.channel_;
        if (channel.request_target.expired()) return;
        parent = channel.request_dispatcher;
        target = channel.request_target;
        request.id = channel.next_request_id;
        ++channel.next_request_id;
        ++channel.statistics.requests_posted;
    }
    OpenRequest open;
    open.id = request.id;
    open.subject = std::move(subject);
    open.description = std::move(description);
    open.content_epoch = request.content_epoch;
    slot = std::move(open);
    static_cast<void>(parent.begin_invoke(DeliverSubSurfaceRequest(std::move(target), std::move(request))));
}

// --- Finding the things -----------------------------------------------------

void SubSurfaceToolTipRequester::request_pointer(bool force) {
    const Control::Ptr hovered = hovered_.lock();
    ToolTipSubject subject =
        resolve_pointer_subject(hovered, pointer_position_, root_.tool_tip_settings(), covers_);
    pointer_region_ = subject.anchor;
    if (!force && pointer_ && unchanged((*pointer_).subject, subject)) return;
    if (!force && !pointer_ && subject.text.empty()) return;
    revoke(pointer_);
    if (subject.text.empty()) return;
    std::string description = description_of(subject);
    post(SubSurfaceToolTipOrigin::pointer, std::move(subject), std::move(description), std::nullopt, pointer_);
}

void SubSurfaceToolTipRequester::request_focus(bool force) {
    const Control::Ptr focused = root_.focused_control();
    if (!root_.active() || !focused) {
        revoke(keyboard_);
        return;
    }
    ToolTipSubject subject = resolve_focus_subject(focused, root_.tool_tip_settings(), covers_);
    if (!force && keyboard_ && unchanged((*keyboard_).subject, subject)) return;
    if (!force && !keyboard_ && subject.text.empty()) return;
    revoke(keyboard_);
    if (subject.text.empty()) return;
    std::string description = description_of(subject);
    post(SubSurfaceToolTipOrigin::keyboard, std::move(subject), std::move(description), std::nullopt, keyboard_);
}

void SubSurfaceToolTipRequester::request_program(bool force) {
    if (!program_) return;
    if (explicit_request_ != 0U) {
        // Its content is the caller's: only a new configuration asks again.
        if (!force) return;
        ToolTipSubject subject = (*program_).subject;
        std::string description = (*program_).description;
        revoke(program_);
        post(SubSurfaceToolTipOrigin::program, std::move(subject), std::move(description), program_duration_,
             program_);
        return;
    }
    const Control::Ptr target = (*program_).subject.control.lock();
    if (!target || !(*target).is_alive() || (*target).attached_surface_root() != &root_ ||
        (*target).tool_tip_text.read().empty()) {
        revoke(program_);
        return;
    }
    ToolTipSubject subject = tool_tip_subject_of(target, {}, (*target).tool_tip_text.read(), (*target).absolute_bounds());
    if (!force && unchanged((*program_).subject, subject)) return;
    revoke(program_);
    std::string description = description_of(subject);
    post(SubSurfaceToolTipOrigin::program, std::move(subject), std::move(description), program_duration_, program_);
}

// --- Root input -------------------------------------------------------------

void SubSurfaceToolTipRequester::pointer_moved(Point position, const Control::Ptr& hovered, FrameTime) {
    pointer_position_ = position;
    const bool same_control = hovered_.lock() == hovered;
    hovered_ = hovered;
    // Within the region of the thing found last, over the same control,
    // nothing changes (the parent tracks the rest).
    if (same_control && pointer_region_ && contains_point(*pointer_region_, position)) return;
    request_pointer(false);
}

void SubSurfaceToolTipRequester::pointer_pressed() {}

void SubSurfaceToolTipRequester::pointer_left() {
    hovered_.reset();
    pointer_region_.reset();
    revoke(pointer_);
}

void SubSurfaceToolTipRequester::focus_changed(const Control::Ptr&) {
    request_focus(false);
}

void SubSurfaceToolTipRequester::key_handled() {
    request_focus(false);
}

void SubSurfaceToolTipRequester::focus_left() {
    revoke(keyboard_);
}

void SubSurfaceToolTipRequester::root_arranged() {
    if (keyboard_) request_focus(false);
    if (program_) request_program(false);
}

void SubSurfaceToolTipRequester::content_epoch_changed() {
    // An open request measured in an older configuration is asked again; a
    // thing whose request the parent dropped as stale (possibly before this
    // configuration arrived) is found and asked for anew.
    const bool pointer_old = pointer_ && (*pointer_).content_epoch != root_.content_epoch_;
    if (pointer_old || (!pointer_ && !hovered_.expired())) request_pointer(pointer_old);
    const bool keyboard_old = keyboard_ && (*keyboard_).content_epoch != root_.content_epoch_;
    request_focus(keyboard_old);
    if (program_ && (*program_).content_epoch != root_.content_epoch_) request_program(true);
}

void SubSurfaceToolTipRequester::text_changed(const Control& control) {
    items_changed(control);
}

void SubSurfaceToolTipRequester::items_changed(const Control& control) {
    // The thing under the pointer is found again when the pointer is over
    // `control` (its text may have come or gone), not only when a request
    // for it is open.
    const Control::Ptr hovered = hovered_.lock();
    bool over = (pointer_ && (*pointer_).subject.control.lock().get() == &control);
    for (Control::Ptr current = hovered; current && !over; current = (*current).parent()) {
        if (current.get() == &control) over = true;
    }
    if (over) request_pointer(false);
    if (keyboard_ || root_.focused_control()) request_focus(false);
    if (program_ && (*program_).subject.control.lock().get() == &control) request_program(false);
}

void SubSurfaceToolTipRequester::settings_changed() {}

void SubSurfaceToolTipRequester::cover(RuntimeId control, std::string_view item_id, bool covered_now) {
    const std::pair<std::uint64_t, std::string> key{control.value, std::string(item_id)};
    if (covered_now) {
        ++covers_[key];
        return;
    }
    const ToolTipCovers::iterator found = covers_.find(key);
    if (found == covers_.end()) return;
    if ((*found).second <= 1U) {
        covers_.erase(found);
        return;
    }
    --(*found).second;
}

void SubSurfaceToolTipRequester::show_program(const Control::Ptr& target,
                                              std::optional<std::chrono::milliseconds> duration) {
    revoke(program_);
    explicit_request_ = 0U;
    explicit_item_.clear();
    program_duration_ = duration;
    ToolTipSubject subject = tool_tip_subject_of(target, {}, (*target).tool_tip_text.read(), (*target).absolute_bounds());
    std::string description = description_of(subject);
    post(SubSurfaceToolTipOrigin::program, std::move(subject), std::move(description), duration, program_);
}

void SubSurfaceToolTipRequester::request(const Control::Ptr& owner, std::uint64_t id, std::string item_id,
                                         const ToolTipRequest& content) {
    revoke(program_);
    explicit_request_ = id;
    explicit_item_ = std::move(item_id);
    program_duration_ = content.duration;
    ToolTipSubject subject = tool_tip_subject_of(owner, {}, content.text, content.anchor);
    post(SubSurfaceToolTipOrigin::program, std::move(subject), content.description, content.duration, program_);
    // Nowhere to ask (no parent target): the request cannot show.
    if (!program_) {
        explicit_request_ = 0U;
        explicit_item_.clear();
        root_.answer_tool_tip_request(id, SurfaceRequestResult::declined);
    }
}

void SubSurfaceToolTipRequester::withdraw(std::string_view item_id) {
    if (explicit_request_ == 0U || explicit_item_ != item_id) return;
    explicit_request_ = 0U;
    explicit_item_.clear();
    revoke(program_);
}

void SubSurfaceToolTipRequester::show_program_item(const Control::Ptr&, std::string, std::string, Rect,
                                                   std::optional<std::chrono::milliseconds>) {}

void SubSurfaceToolTipRequester::hide() {
    if (explicit_request_ != 0U) {
        const std::uint64_t id = explicit_request_;
        explicit_request_ = 0U;
        explicit_item_.clear();
        root_.answer_tool_tip_request(id, SurfaceRequestResult::closed);
    }
    revoke(program_);
}

void SubSurfaceToolTipRequester::shutdown() noexcept {
    pointer_.reset();
    keyboard_.reset();
    program_.reset();
    explicit_request_ = 0U;
    explicit_item_.clear();
    shown_id_ = 0U;
}

// --- Answers ----------------------------------------------------------------

bool SubSurfaceToolTipRequester::receive_outcome(const SurfaceRequestOutcome& outcome) {
    std::optional<OpenRequest>* slot = nullptr;
    if (pointer_ && (*pointer_).id == outcome.id) slot = &pointer_;
    if (keyboard_ && (*keyboard_).id == outcome.id) slot = &keyboard_;
    if (program_ && (*program_).id == outcome.id) slot = &program_;
    if (slot == nullptr) return false;
    // An answer to the root's request_tool_tip goes on to its target; a
    // stale one is asked again with the next configuration instead.
    const std::uint64_t root_request = slot == &program_ ? explicit_request_ : 0U;
    if (root_request != 0U && outcome.result != SurfaceRequestResult::stale) {
        root_.answer_tool_tip_request(root_request, outcome.result);
    }
    switch (outcome.result) {
    case SurfaceRequestResult::shown:
        shown_id_ = outcome.id;
        return true;
    case SurfaceRequestResult::closed:
        if (shown_id_ == outcome.id) shown_id_ = 0U;
        return true;
    case SurfaceRequestResult::stale:
        if (shown_id_ == outcome.id) shown_id_ = 0U;
        // A program request keeps its slot (only a configuration makes it
        // stale): the next configuration asks again (content_epoch_changed).
        if (slot == &program_) return true;
        if (slot == &pointer_) pointer_region_.reset();
        (*slot).reset();
        return true;
    case SurfaceRequestResult::declined:
    case SurfaceRequestResult::revoked:
        // The parent keeps nothing of it: the next change asks again.
        if (shown_id_ == outcome.id) shown_id_ = 0U;
        if (slot == &pointer_) pointer_region_.reset();
        if (root_request != 0U) {
            explicit_request_ = 0U;
            explicit_item_.clear();
        }
        (*slot).reset();
        return true;
    default:
        throw std::invalid_argument("GUI.Forms sub-surface request result is not a defined value");
    }
}

// --- Queries ----------------------------------------------------------------

const SubSurfaceToolTipRequester::OpenRequest* SubSurfaceToolTipRequester::shown() const noexcept {
    if (shown_id_ == 0U) return nullptr;
    if (pointer_ && (*pointer_).id == shown_id_) return &*pointer_;
    if (keyboard_ && (*keyboard_).id == shown_id_) return &*keyboard_;
    if (program_ && (*program_).id == shown_id_) return &*program_;
    return nullptr;
}

bool SubSurfaceToolTipRequester::visible() const noexcept {
    const bool showing = shown() != nullptr;
    return showing;
}

Control::Ptr SubSurfaceToolTipRequester::target() const noexcept {
    const OpenRequest* request = shown();
    if (request == nullptr) return {};
    const Control::Ptr control = (*request).subject.control.lock();
    return control;
}

std::string SubSurfaceToolTipRequester::item_id() const {
    const OpenRequest* request = shown();
    if (request == nullptr) return {};
    return (*request).subject.item_id;
}

std::string SubSurfaceToolTipRequester::text() const {
    const OpenRequest* request = shown();
    if (request == nullptr) return {};
    return (*request).subject.text;
}

} // namespace gui_forms::detail
