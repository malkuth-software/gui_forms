#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component.hpp"

#include <memory>

namespace gui_forms {

class Window;

class GUI_FORMS_API FrameRequestToken final {
public:
    FrameRequestToken() = default;
    ~FrameRequestToken();
    FrameRequestToken(FrameRequestToken&& other) noexcept;
    FrameRequestToken& operator=(FrameRequestToken&& other) noexcept;
    FrameRequestToken(const FrameRequestToken&) = delete;
    FrameRequestToken& operator=(const FrameRequestToken&) = delete;

    void disconnect() noexcept;
    [[nodiscard]] bool connected() const noexcept;

private:
    friend class Window;
    friend class SurfaceRoot;
    explicit FrameRequestToken(std::shared_ptr<detail::Revocable> revocable);

    std::shared_ptr<detail::Revocable> revocable_;
};

} // namespace gui_forms
