#pragma once

#include "gui_forms/event.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace gui_forms {

// Application-owned scalar state. Notifications are synchronous on the UI
// execution thread. Equal writes do nothing; a different recursive write is
// rejected rather than creating an unbounded observer feedback loop.
template <typename T>
class Value final : public Component {
    static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>,
                  "gui_forms::Value requires a scalar value; keep objects in the model");

    struct State final {
        explicit State(const T initial) : value(initial) {}
        T value;
        Event<T> changed{};
        Event<T> validating{};
        bool active{true};
        bool notifying{};
    };

    class NotificationScope final {
    public:
        explicit NotificationScope(std::shared_ptr<State> state)
            : state_(std::move(state)) { (*state_).notifying = true; }
        ~NotificationScope() { (*state_).notifying = false; }
        NotificationScope(const NotificationScope&) = delete;
        NotificationScope& operator=(const NotificationScope&) = delete;
    private:
        std::shared_ptr<State> state_;
    };

public:
    explicit Value(const T initial = T{}) : state_(gui_forms::detail::make_library_shared<State>(initial)) {
        validate(initial);
    }
    ~Value() override { on_dispose(); }
    [[nodiscard]] T get() const noexcept { return (*state_).value; }
    [[nodiscard]] Event<T>& changed() noexcept { return (*state_).changed; }
    [[nodiscard]] Event<T>& validating() noexcept { return (*state_).validating; }

    // Controls validate before committing local state, so a constraint from a
    // second bound control cannot leave the source changed after rejection.
    void validate_candidate(const T value) const {
        if (!is_alive()) throw std::logic_error("cannot validate a disposed Value");
        validate(value);
        const std::shared_ptr<State> state = state_;
        if ((*state).value == value) return;
        if ((*state).notifying) throw std::logic_error("Value observers may not recursively change the value");
        NotificationScope notification(state);
        (*state).validating.emit(value);
    }

    void set(const T value) {
        if (!is_alive()) throw std::logic_error("cannot change a disposed Value");
        validate(value);
        const std::shared_ptr<State> state = state_;
        if ((*state).value == value) return;
        if ((*state).notifying) {
            throw std::logic_error("Value observers may not recursively change the value");
        }
        NotificationScope notification(state);
        (*state).validating.emit(value);
        if (!(*state).active) return;
        (*state).value = value;
        (*state).changed.emit(value);
    }

private:
    static void validate(const T value) {
        if constexpr (std::is_floating_point_v<T>) {
            if (!std::isfinite(value)) {
                throw std::invalid_argument("Value requires a finite number");
            }
        }
    }
    void on_dispose() noexcept override {
        (*state_).active = false;
        (*state_).validating.disconnect_all();
        (*state_).changed.disconnect_all();
    }
    std::shared_ptr<State> state_;
};

} // namespace gui_forms
