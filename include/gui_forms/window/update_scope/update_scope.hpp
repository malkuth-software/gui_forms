#pragma once

#include "gui_forms/export/application_export.hpp"

namespace gui_forms {

class SurfaceRoot;

// Move-only ownership token for one Window update-depth level. Closing the
// token balances the transaction and may trigger the bounded retained flush.
class GUI_FORMS_API UpdateScope final {
public:
    explicit UpdateScope(SurfaceRoot& root) noexcept : window_(&root) {}
    ~UpdateScope();
    UpdateScope(UpdateScope&& other) noexcept;
    UpdateScope& operator=(UpdateScope&& other) noexcept;
    UpdateScope(const UpdateScope&) = delete;
    UpdateScope& operator=(const UpdateScope&) = delete;

    void perform_layout();
    void close();
    // Ends this token's hold without leaving the update level it entered:
    // the window is not touched, so this is the operation for a window that
    // may already be destroyed (its model handed to a host that then
    // refused it). What it leaves behind: a window that still lives stays
    // one update level deeper for the rest of its life, since nothing can
    // leave that level any more. The token is empty afterwards; close and
    // the destructor do nothing.
    void abandon() noexcept;

private:
    SurfaceRoot* window_{};
};

} // namespace gui_forms
