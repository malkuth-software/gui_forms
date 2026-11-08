#include "toast_presenter.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace gui_forms::detail {

namespace {

// The space kept between the toast and the client area's edges, in
// logical units: below it, and on each side when the window is narrow.
constexpr double toast_margin = 16.0;

} // namespace

ToastLayer::ToastLayer(StableId stable_id) : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    set_focusable(false);
}

bool ToastLayer::hit_test_local(Point) const {
    return false;
}

ToastPresenter::Overlay::Overlay(std::shared_ptr<ToastLayer> layer_control, Control::Ptr content_control,
                                 OverlayToken overlay_token) noexcept
    : layer(std::move(layer_control)), content(std::move(content_control)), token(std::move(overlay_token)) {}

ToastPresenter::Overlay::~Overlay() {
    closed.disconnect();
    token.disconnect();
    // The caller owns the content: it leaves the layer detached, so that it
    // may be shown again.
    if (layer && content && (*content).parent() == layer) {
        static_cast<void>((*layer).remove_child((*content).runtime_id()));
    }
    content.reset();
    layer.reset();
}

void ToastPresenter::Wake::operator()(FrameTime) const {
    (*presenter).wake_.disconnect();
    (*presenter).raise((*presenter).close(ToastDismissReason::timed_out));
}

void ToastPresenter::OverlayClosed::operator()() const {
    (*presenter).raise((*presenter).close(ToastDismissReason::owner_gone));
}

ToastPresenter::ToastPresenter(Window& window) noexcept : window_(window) {}

ToastPresenter::~ToastPresenter() {
    shutdown();
}

void ToastPresenter::shutdown() noexcept {
    wake_.disconnect();
    // The overlay's destructor disconnects its overlay before the state goes.
    showing_.reset();
}

std::uint64_t ToastPresenter::show(const Control::Ptr& owner, const Control::Ptr& content,
                                   std::chrono::milliseconds duration) {
    // The toast that shows closes first; its dismissal is raised last, once
    // the new toast shows.
    const std::optional<ToastDismissal> replaced = close(ToastDismissReason::replaced);
    const std::uint64_t id = next_id_;
    ++next_id_;
    const std::string prefix = "toast." + std::to_string(id);
    std::shared_ptr<ToastLayer> layer = make_control<ToastLayer>(StableId(prefix + ".layer"));
    const Size client = window_.client_size();
    (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    (*layer).add_child(content);
    // A toast is passive, as a tooltip is: a disabled owner may show one.
    OverlayOptions options{};
    options.require_enabled_owner = false;
    OverlayToken token{};
    try {
        token = window_.open_overlay(owner, layer, options);
    } catch (...) {
        // The caller's content leaves the unopened layer, detached again.
        static_cast<void>((*layer).remove_child((*content).runtime_id()));
        raise(replaced);
        throw;
    }
    std::unique_ptr<Overlay> overlay = std::make_unique<Overlay>(std::move(layer), content, std::move(token));
    if (Event<>* closed = (*overlay).token.closed_event()) {
        (*overlay).closed = (*closed).subscribe(OverlayClosed{this});
    }
    Showing showing{};
    showing.id = id;
    showing.overlay = std::move(overlay);
    showing_ = std::move(showing);
    position();
    if (duration.count() > 0) {
        wake_ = window_.request_tool_tip_wake(FrameClock::now() + duration, Wake{this});
    }
    raise(replaced);
    return id;
}

bool ToastPresenter::dismiss(const Control& content, ToastDismissReason reason) {
    if (!showing_ || (*(*showing_).overlay).content.get() != &content) return false;
    raise(close(reason));
    return true;
}

void ToastPresenter::hide() {
    raise(close(ToastDismissReason::hidden));
}

void ToastPresenter::client_resized() {
    if (!showing_) return;
    const Size client = window_.client_size();
    (*(*(*showing_).overlay).layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    position();
}

std::optional<ToastDismissal> ToastPresenter::close(ToastDismissReason reason) {
    wake_.disconnect();
    if (!showing_) return std::nullopt;
    const ToastDismissal dismissal{(*showing_).id, reason};
    // Moving the state out first leaves the presenter consistent while the
    // overlay's destructor detaches the overlay.
    std::optional<Showing> previous = std::move(showing_);
    showing_.reset();
    previous.reset();
    return dismissal;
}

// Centred across the client area, its bottom toast_margin above the
// client area's; as wide as the content measures within the margins.
void ToastPresenter::position() {
    if (!showing_) return;
    const Control::Ptr& content = (*(*showing_).overlay).content;
    const Size client = window_.client_size();
    const Size room{std::max(0.0, client.width - 2.0 * toast_margin),
                    std::max(0.0, client.height - 2.0 * toast_margin)};
    const Size measured = (*content).measure(room);
    const double width = std::min(measured.width, room.width);
    const double height = std::min(measured.height, room.height);
    const double x = std::floor((client.width - width) * 0.5);
    const double y = std::max(toast_margin, client.height - toast_margin - height);
    (*content).set_requested_bounds({x, y, width, height});
}

void ToastPresenter::raise(const std::optional<ToastDismissal>& dismissal) {
    if (!dismissal) return;
    window_.raise_toast_dismissed(*dismissal);
}

bool ToastPresenter::visible() const noexcept {
    return showing_.has_value();
}

Control::Ptr ToastPresenter::content() const noexcept {
    if (!showing_) return Control::Ptr{};
    return (*(*showing_).overlay).content;
}

std::uint64_t ToastPresenter::id() const noexcept {
    if (!showing_) return 0U;
    return (*showing_).id;
}

} // namespace gui_forms::detail
