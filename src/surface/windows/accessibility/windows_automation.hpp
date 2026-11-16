#pragma once

// UI Automation publication (stage A): one provider object per element a
// client asks for, serving a fragment root for the window's client area and
// fragments for every semantic node below it. Win32, COM and UIA types stay
// inside src/surface/windows/.
//
// Threading (house style 8). The semantic tree has one owner, the window's
// UI thread, and only that thread touches it. Every provider reports
// ProviderOptions_UseComThreading: UI Automation then calls a provider
// through COM in the apartment that created it. Providers are created on
// the UI thread, which is a single-threaded apartment (the host calls
// CoInitializeEx with COINIT_APARTMENTTHREADED), and they do not aggregate
// the free-threaded marshaler, so COM marshals every call from a client or
// from UI Automation's worker threads to the UI thread, where it runs
// between messages. Without the option UI Automation would call providers
// on its own threads and each call would need its own hop to the UI thread.
// A provider never blocks waiting for another thread.
//
// IInvokeProvider::Invoke must return before the action runs (the action
// may open a modal dialog), so it posts the press through
// Window::begin_invoke; the posted work runs on the UI thread on a later
// dispatcher turn, looks the element up again, and does nothing when the
// element's control has gone meanwhile. Every other pattern method acts
// synchronously.
//
// Stale elements (house style 9, interview GF017): a provider names its
// node by runtime identity (SemanticElementId) and resolves it in a fresh
// snapshot on every call. When the window has gone or the node has left the
// tree, every method answers UIA_E_ELEMENTNOTAVAILABLE; a disposed or
// replaced control's identity never reaches another control.

#include "windows_semantic_source.hpp"

#include <uiautomation.h>

#include <memory>

namespace gui_forms::host {

// A new provider for the element `id` of `source`'s window, with one
// reference the caller owns, or null when memory runs out. The provider
// observes the source; once the window has gone, or the element's node has
// left the tree, every call answers UIA_E_ELEMENTNOTAVAILABLE.
[[nodiscard]] IRawElementProviderSimple* make_automation_element(
    const std::shared_ptr<SemanticSource>& source, SemanticElementId id) noexcept;

// Raises the UI Automation events for the change from `previous` to
// `current` (two client trees of the same window, from
// SemanticSource::snapshot): focus moved, children changed, and toggle,
// expand-collapse, selection, value, range value, name and enabled
// changes of nodes present in both. UI thread only.
void raise_automation_events(const std::shared_ptr<SemanticSource>& source,
                             const SemanticNode& previous, const SemanticNode& current) noexcept;

} // namespace gui_forms::host
