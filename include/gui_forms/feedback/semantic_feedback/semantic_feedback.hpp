#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component/component/component.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/feedback/types/feedback_types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

class Window;

class GUI_FORMS_API SemanticFeedback final : public Component {
public:
    using Clock = std::function<std::uint64_t()>;

    explicit SemanticFeedback(Window& window, Clock clock = {});

    [[nodiscard]] SemanticFeedbackRecord emit(SemanticFeedbackKind kind);
    [[nodiscard]] std::span<const SemanticFeedbackRecord> records() const& noexcept {
        return records_;
    }
    std::span<const SemanticFeedbackRecord> records() const&& = delete;
    [[nodiscard]] std::uint64_t dropped_record_count() const noexcept {
        return dropped_record_count_;
    }
    [[nodiscard]] std::size_t maximum_records() const noexcept {
        return maximum_records_;
    }
    void set_maximum_records(std::size_t maximum);
    void clear();
    [[nodiscard]] std::string trace() const;
    [[nodiscard]] Event<const SemanticFeedbackRecord&>& emitted() noexcept {
        return emitted_;
    }

    [[nodiscard]] static HostSoundCue cue_for(
        SemanticFeedbackKind kind) noexcept;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    Window* window_{};
    Clock clock_;
    std::vector<SemanticFeedbackRecord> records_;
    std::uint64_t next_sequence_{1U};
    std::uint64_t last_timestamp_{};
    std::uint64_t dropped_record_count_{};
    std::size_t maximum_records_{1024U};
    Event<const SemanticFeedbackRecord&> emitted_;
};

} // namespace gui_forms
