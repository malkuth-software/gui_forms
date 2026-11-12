#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/application/application.hpp"
#include "gui_forms/window/update_scope/update_scope.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace gui_forms {

struct GUI_FORMS_API WorkspaceTransactionResult final {
    // The first refusal met while committing; the commit still carries out
    // every later step.
    HostServiceStatus status{};
    std::size_t windows_laid_out{};
    std::size_t windows_shown{};
    std::size_t windows_destroyed{};
    std::size_t notifications{};
};

// One coherent change across several windows of a run (stage DK decision 9,
// docking study 5.3): a move that empties one window and fills another lays
// out each window once, never shows a half-moved state, and tells observers
// once, at the end.
//
// While the transaction is open, every included window holds an update
// scope, so its layout waits; windows opened through the transaction are
// created hidden and included; shows, destructions and notifications are
// queued. commit() then runs, in this order:
//
//   1. close the update scopes, in inclusion order: each window lays out once;
//   2. show the windows queued for showing (opened visible, or show_window),
//      without activating them;
//   3. destroy the windows queued for destruction, so a panel moving between
//      two windows is never invisible;
//   4. run the queued notifications once each, in the order queued. A
//      notification that changes windows starts a transaction of its own.
//
// The destructor commits a transaction left open. Every member runs on the
// run's UI thread. The transaction borrows the included Window models; a
// window destroyed through the transaction is destroyed only after its
// scope closed, and the hosts release a model no earlier than the end of
// the turn in which its window closed.
class GUI_FORMS_API WorkspaceTransaction final {
public:
    explicit WorkspaceTransaction(ApplicationWindows windows);
    ~WorkspaceTransaction();
    WorkspaceTransaction(const WorkspaceTransaction&) = delete;
    WorkspaceTransaction& operator=(const WorkspaceTransaction&) = delete;

    [[nodiscard]] bool open() const noexcept { return open_; }
    // Defers `window`'s layout to the commit. Including a window twice
    // changes nothing. std::logic_error after commit.
    void include(Window& window);
    // Opens a window, hidden, and includes its model. It is shown at the
    // commit when `entry.options.initially_visible` was true. The refusals
    // are ApplicationWindows::open_window's; on a refusal the model is no
    // longer included (its scope is closed when the entry is still the
    // caller's, abandoned when the host had taken it).
    [[nodiscard]] ApplicationWindowOpenResult open_window(ApplicationWindow&& entry);
    // Shows `window` at the commit, after every window has laid out.
    [[nodiscard]] HostServiceStatus show_window(const ApplicationWindowHandle& window);
    // Destroys `window` at the commit, after the windows to show are shown.
    // The refusals are ApplicationWindows::destroy_window's, met at commit.
    [[nodiscard]] HostServiceStatus destroy_window(const ApplicationWindowHandle& window);
    // Runs `notification` once at the commit, after the windows are shown
    // and destroyed. std::invalid_argument for an empty callable.
    void defer_notification(std::function<void()> notification);
    // Carries out the transaction. A second commit returns an empty result.
    WorkspaceTransactionResult commit();

private:
    void require_open(const char* operation) const;

    ApplicationWindows windows_;
    bool open_{true};
    // Included windows, in inclusion order, with the scope held on each.
    std::vector<Window*> included_;
    std::vector<UpdateScope> scopes_;
    std::vector<ApplicationWindowHandle> to_show_;
    std::vector<ApplicationWindowHandle> to_destroy_;
    std::vector<std::function<void()>> notifications_;
};

} // namespace gui_forms
