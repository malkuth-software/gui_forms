// The named host-service targets for one window (macos_window_requests.hpp).

#include "macos_window_requests.hpp"
#include "macos_main_work.hpp"
#include "macos_values.hpp"
#include "../view/macos_view.hpp"

namespace gui_forms::host {

namespace {
// The pending-dispatch callback and the view it collects damage on, both
// kept until the work runs, as the replaced block captured them.
struct DispatchAndCollectWork final {
    GUIFormsView* view;
    std::function<void()> dispatch_pending;
};

void dispatch_and_collect_on_main(void* context) {
    const std::unique_ptr<DispatchAndCollectWork> work(static_cast<DispatchAndCollectWork*>(context));
    if ((*work).dispatch_pending) (*work).dispatch_pending();
    [(*work).view collectDamage];
}

void make_key_and_activate_on_main(void* context) {
    const std::unique_ptr<WindowWork> work(static_cast<WindowWork*>(context));
    [(*work).window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

void toggle_full_screen_on_main(void* context) {
    const std::unique_ptr<WindowWork> work(static_cast<WindowWork*>(context));
    [(*work).window toggleFullScreen:nil];
}

void order_out_on_main(void* context) {
    const std::unique_ptr<WindowWork> work(static_cast<WindowWork*>(context));
    [(*work).window orderOut:nil];
}

} // namespace

void DispatchAndCollectWake::operator()() const
{
    DispatchAndCollectWork* const work = new DispatchAndCollectWork{view_, dispatch_pending_};
    post_main_work(work, &dispatch_and_collect_on_main);
}

void RequestNativeClose::operator()() const
{
    post_perform_close(window_);
}

void RequestNativeShow::operator()() const
{
    WindowWork* const work = new WindowWork{window_};
    post_main_work(work, &make_key_and_activate_on_main);
}

HostServiceStatus RequestNativeTitle::operator()(const std::string_view title) const {
    if (![NSThread isMainThread]) return {HostServiceError::wrong_thread};
    try {
        @try {
            NSWindow* const window = window_;
            GUIFormsView* const view = view_;
            if (window == nil || view == nil || ![view acceptsWindowRequest])
                return {HostServiceError::after_shutdown};
            if (title.size() > 65536U || title.find('\0') != std::string_view::npos ||
                !validate_utf8(title).valid()) return {HostServiceError::invalid_argument};
            NSString* const text = native_string(title);
            if (text == nil) return {HostServiceError::backend_failure};
            [window setTitle:text];
            return {};
        } @catch (NSException* failure) {
            static_cast<void>(failure);
            return {HostServiceError::backend_failure};
        }
    } catch (...) { return {HostServiceError::backend_failure}; }
}

HostServiceStatus RequestNativeClientSize::operator()(const GFSize clientSize) const {
    if (![NSThread isMainThread]) return {HostServiceError::wrong_thread};
    try {
        @try {
            NSWindow* const window = window_;
            GUIFormsView* const view = view_;
            if (window == nil || view == nil || ![view acceptsWindowRequest])
                return {HostServiceError::after_shutdown};
            if (!std::isfinite(clientSize.width) || !std::isfinite(clientSize.height) ||
                clientSize.width > 32768.0 || clientSize.height > 32768.0)
                return {HostServiceError::invalid_argument};
            const NSSize minimum = [window contentMinSize];
            if (clientSize.width < std::max(1.0, static_cast<double>(minimum.width)) ||
                clientSize.height < std::max(1.0, static_cast<double>(minimum.height)))
                return {HostServiceError::invalid_argument};
            const bool fullScreen = ([window styleMask] & NSWindowStyleMaskFullScreen) != 0;
            if (fullScreen || [window isMiniaturized] || [window isZoomed])
                return {HostServiceError::unsupported};
            [window setContentSize:NSMakeSize(clientSize.width, clientSize.height)];
            return {};
        } @catch (NSException* failure) {
            static_cast<void>(failure);
            return {HostServiceError::backend_failure};
        }
    } catch (...) { return {HostServiceError::backend_failure}; }
}

void RequestNativeFullScreen::operator()() const {
    WindowWork* const work = new WindowWork{window_};
    post_main_work(work, &toggle_full_screen_on_main);
}

void RequestNativeHide::operator()() const
{
    WindowWork* const work = new WindowWork{window_};
    post_main_work(work, &order_out_on_main);
}

HostDialogResult ShowNativeDialog::operator()(const HostDialogRequest& request) const
{
    return [view_ showHostDialog:request];
}

HostClipboardTextResult ReadNativeClipboard::operator()() const
{
    return [view_ readHostClipboard];
}

HostServiceStatus WriteNativeClipboard::operator()(std::string_view text) const
{
    return [view_ writeHostClipboard:text];
}

} // namespace gui_forms::host
