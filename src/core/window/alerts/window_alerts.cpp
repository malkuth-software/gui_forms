#include "window_alerts.hpp"
#include "../../surface_root/access/surface_request_access.hpp"

#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/window.hpp"

#include <utility>
#include <variant>

namespace gui_forms::detail {

namespace {

// Posted to the window's dispatcher: shows the next alert unless the route
// has gone.
class ShowNextAlert final {
public:
    explicit ShowNextAlert(std::weak_ptr<WindowAlerts::Liveness> liveness) noexcept
        : liveness_(std::move(liveness)) {}
    void operator()() const {
        const std::shared_ptr<WindowAlerts::Liveness> held = liveness_.lock();
        if (!held || (*held).alerts == nullptr) return;
        (*(*held).alerts).show_next();
    }

private:
    std::weak_ptr<WindowAlerts::Liveness> liveness_;
};

} // namespace

WindowAlerts::WindowAlerts(Window& window) : window_(window), liveness_(std::make_shared<Liveness>()) {
    (*liveness_).alerts = this;
}

WindowAlerts::~WindowAlerts() {
    (*liveness_).alerts = nullptr;
}

void WindowAlerts::raise(AlertRecord record) {
    waiting_.push_back(std::move(record));
    schedule();
}

void WindowAlerts::withdraw(std::uint64_t id) {
    for (std::deque<AlertRecord>::iterator current = waiting_.begin(); current != waiting_.end(); ++current) {
        if ((*current).id != id) continue;
        const AlertRecord withdrawn = std::move(*current);
        waiting_.erase(current);
        answer(withdrawn, SurfaceRequestResult::revoked, HostDialogChoice::none);
        return;
    }
}

void WindowAlerts::shutdown() noexcept {
    (*liveness_).alerts = nullptr;
    waiting_.clear();
}

void WindowAlerts::schedule() {
    if (scheduled_ || showing_ || waiting_.empty() || (*liveness_).alerts == nullptr) return;
    scheduled_ = true;
    static_cast<void>(window_.dispatcher().begin_invoke(ShowNextAlert(liveness_)));
}

void WindowAlerts::answer(const AlertRecord& record, SurfaceRequestResult result, HostDialogChoice choice) {
    SurfaceRequestOutcome outcome;
    outcome.id = record.id;
    outcome.result = result;
    outcome.choice = choice;
    SurfaceRequestAccess::deliver_request_answer(window_, record.answer, outcome);
}

void WindowAlerts::show_next() {
    scheduled_ = false;
    if (showing_ || waiting_.empty()) return;
    const AlertRecord record = std::move(waiting_.front());
    waiting_.pop_front();
    const Control::Ptr owner = record.owner.lock();
    if (!owner || !(*owner).is_alive() || (*owner).attached_surface_root() != &window_) {
        answer(record, SurfaceRequestResult::revoked, HostDialogChoice::none);
        schedule();
        return;
    }
    HostServices* const services = window_.host_services();
    if (services == nullptr) {
        answer(record, SurfaceRequestResult::declined, HostDialogChoice::none);
        schedule();
        return;
    }
    HostMessageDialogRequest payload;
    payload.title = record.content.caption;
    payload.message = record.content.text;
    payload.buttons = record.content.buttons;
    payload.icon = record.content.icon;
    payload.default_choice = record.content.default_choice == HostDialogChoice::none
                                 ? first_alert_choice(record.content.buttons)
                                 : record.content.default_choice;
    HostDialogRequest host_request;
    host_request.request_id = take_dialog_request_id();
    const Control::Ptr root = window_.root();
    if (root) host_request.owner_id.assign((*root).stable_id().value());
    host_request.payload = std::move(payload);
    // Held across the modal loop: a window destroyed inside it clears it.
    const std::shared_ptr<Liveness> liveness = liveness_;
    showing_ = true;
    HostDialogResult shown{};
    try {
        shown = (*services).show_dialog(host_request);
    } catch (...) {
        if ((*liveness).alerts != nullptr) {
            showing_ = false;
            answer(record, SurfaceRequestResult::declined, HostDialogChoice::none);
            schedule();
        }
        throw;
    }
    if ((*liveness).alerts == nullptr) return;
    showing_ = false;
    const HostMessageDialogResult* const message = std::get_if<HostMessageDialogResult>(&shown.payload);
    if (!shown.status.accepted() || message == nullptr) {
        answer(record, SurfaceRequestResult::declined, HostDialogChoice::none);
    } else {
        const HostDialogChoice choice = (*message).outcome == HostDialogOutcome::accepted
                                            ? (*message).choice
                                            : dismissed_alert_choice(record.content.buttons);
        answer(record, SurfaceRequestResult::closed, choice);
    }
    schedule();
}

} // namespace gui_forms::detail
