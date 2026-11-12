#include "sub_surface_alerts.hpp"
#include "../access/surface_request_access.hpp"

#include "sub_surface_channel.hpp"
#include "sub_surface_request_posts.hpp"

#include <mutex>
#include <utility>

namespace gui_forms::detail {

SubSurfaceAlertRequester::SubSurfaceAlertRequester(SubSurface& root) noexcept : root_(root) {}

SubSurfaceAlertRequester::~SubSurfaceAlertRequester() {}

void SubSurfaceAlertRequester::answer(std::uint64_t id, const std::weak_ptr<SurfaceRequestAnswer>& target,
                                      SurfaceRequestResult result, HostDialogChoice choice) {
    SurfaceRequestOutcome outcome;
    outcome.id = id;
    outcome.result = result;
    outcome.choice = choice;
    SurfaceRequestAccess::deliver_request_answer(root_, target, outcome);
}

void SubSurfaceAlertRequester::raise(AlertRecord record) {
    SubSurfaceRequest request;
    request.kind = SubSurfaceRequestKind::alert;
    request.content_epoch = root_.content_epoch_;
    request.input_sequence = root_.input_sequence_;
    request.text = std::move(record.content.text);
    request.caption = std::move(record.content.caption);
    request.buttons = record.content.buttons;
    request.icon = record.content.icon;
    request.default_choice = record.content.default_choice;
    WindowDispatcher parent;
    std::weak_ptr<SubSurfaceRequestTarget> target;
    if (root_.channel_) {
        const std::lock_guard<std::mutex> lock((*root_.channel_).mutex);
        SubSurfaceChannel& channel = *root_.channel_;
        if (!channel.request_target.expired()) {
            parent = channel.request_dispatcher;
            target = channel.request_target;
            request.id = channel.next_request_id;
            ++channel.next_request_id;
            ++channel.statistics.requests_posted;
        }
    }
    if (request.id == 0U) {
        answer(record.id, record.answer, SurfaceRequestResult::declined, HostDialogChoice::none);
        return;
    }
    Forwarded forwarded;
    forwarded.request = request.id;
    forwarded.id = record.id;
    forwarded.answer = std::move(record.answer);
    forwarded_.push_back(std::move(forwarded));
    // Posted outside the channel's lock (lock order: the parent's dispatcher
    // is never taken under it). A closed parent drops it; the host then
    // withdraws nothing and the alert stays unanswered until the thread stops.
    static_cast<void>(parent.begin_invoke(DeliverSubSurfaceRequest(std::move(target), std::move(request))));
}

void SubSurfaceAlertRequester::withdraw(std::uint64_t id) {
    std::vector<Forwarded>::iterator found = forwarded_.begin();
    while (found != forwarded_.end() && (*found).id != id) ++found;
    if (found == forwarded_.end()) return;
    const Forwarded withdrawn = std::move(*found);
    forwarded_.erase(found);
    answer(withdrawn.id, withdrawn.answer, SurfaceRequestResult::revoked, HostDialogChoice::none);
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
    static_cast<void>(parent.begin_invoke(DeliverSubSurfaceRevoke(std::move(target), withdrawn.request)));
}

void SubSurfaceAlertRequester::shutdown() noexcept {
    forwarded_.clear();
}

bool SubSurfaceAlertRequester::receive_outcome(const SurfaceRequestOutcome& outcome) {
    std::vector<Forwarded>::iterator found = forwarded_.begin();
    while (found != forwarded_.end() && (*found).request != outcome.id) ++found;
    if (found == forwarded_.end()) return false;
    // An alert is answered once: whatever the parent says ends it here.
    const Forwarded answered = std::move(*found);
    forwarded_.erase(found);
    answer(answered.id, answered.answer, outcome.result, outcome.choice);
    return true;
}

} // namespace gui_forms::detail
