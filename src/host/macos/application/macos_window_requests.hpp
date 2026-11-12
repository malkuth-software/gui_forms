#pragma once

// The named host-service targets a run hands to the portable side for one
// window (macos_window_requests.mm): wakes, close, show and hide, title,
// client size, full screen, dialogs and the clipboard. Each holds the
// native window or view it acts on.

#include "macos_host_private.hpp"

@class GUIFormsView;

namespace gui_forms::host {

class DispatchAndCollectWake final {
public:
    DispatchAndCollectWake(
        GUIFormsView* view,
        std::function<void()> dispatch_pending) noexcept
        : view_(view), dispatch_pending_(std::move(dispatch_pending)) {}

    void operator()() const;

private:
    GUIFormsView* view_;
    std::function<void()> dispatch_pending_;
};

class RequestNativeClose final {
public:
    explicit RequestNativeClose(NSWindow* window) noexcept : window_(window) {}

    void operator()() const;

private:
    NSWindow* window_;
};

class RequestNativeShow final {
public:
    explicit RequestNativeShow(NSWindow* window) noexcept : window_(window) {}

    void operator()() const;

private:
    NSWindow* window_;
};

class RequestNativeTitle final {
public:
    RequestNativeTitle(NSWindow* const window, GUIFormsView* const view) noexcept
        : window_(window), view_(view) {}
    HostServiceStatus operator()(const std::string_view title) const;
private:
    __weak NSWindow* window_;
    __weak GUIFormsView* view_;
};
// Asks for a content size in points (GUI.Forms logical units). AppKit resizes
// the view before setContentSize: returns, and the model is resized through
// the ordinary resize event (deferred while a host callback is running).
class RequestNativeClientSize final {
public:
    RequestNativeClientSize(NSWindow* const window, GUIFormsView* const view) noexcept
        : window_(window), view_(view) {}
    HostServiceStatus operator()(const GFSize clientSize) const;
private:
    __weak NSWindow* window_;
    __weak GUIFormsView* view_;
};
class RequestNativeFullScreen final {
public:
    explicit RequestNativeFullScreen(NSWindow* window) noexcept : window_(window) {}
    void operator()() const;
private:
    NSWindow* window_;
};

class RequestNativeHide final {
public:
    explicit RequestNativeHide(NSWindow* window) noexcept : window_(window) {}

    void operator()() const;

private:
    NSWindow* window_;
};

class ShowNativeDialog final {
public:
    explicit ShowNativeDialog(GUIFormsView* view) noexcept : view_(view) {}

    HostDialogResult operator()(const HostDialogRequest& request) const;

private:
    GUIFormsView* view_;
};

class ReadNativeClipboard final {
public:
    explicit ReadNativeClipboard(GUIFormsView* view) noexcept : view_(view) {}

    HostClipboardTextResult operator()() const;

private:
    GUIFormsView* view_;
};

class WriteNativeClipboard final {
public:
    explicit WriteNativeClipboard(GUIFormsView* view) noexcept : view_(view) {}

    HostServiceStatus operator()(std::string_view text) const;

private:
    GUIFormsView* view_;
};

} // namespace gui_forms::host
