#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstdint>
#include <thread>

namespace gui_drawing {

enum class ObjectState : std::uint8_t { alive, disposing, disposed };

class GUI_FORMS_API DrawingObject {
public:
    DrawingObject();
    virtual ~DrawingObject();
    DrawingObject(const DrawingObject&) = delete;
    DrawingObject& operator=(const DrawingObject&) = delete;

    void dispose();
    [[nodiscard]] ObjectState state() const;
    [[nodiscard]] bool is_disposed() const;
    [[nodiscard]] std::thread::id owner_thread() const noexcept { return owner_thread_; }
    void verify_access() const;
    // ABI/frontends may transfer a drawing value between serialized calls.
    // This is never an authorization for concurrent access.
    void handoff_to_current_thread();

protected:
    void require_alive() const;
    virtual void on_dispose() noexcept;

private:
    std::thread::id owner_thread_;
    ObjectState state_{ObjectState::alive};
};

} // namespace gui_drawing
