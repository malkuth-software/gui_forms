#pragma once

// The private state of TextBox auto-complete, shared by text_box.cpp (which
// owns it through a std::unique_ptr) and text_box_auto_complete.cpp.

#include "gui_forms/controls/panel/text_box/text_box.hpp"

#include "../combo_box/drop_down_layer.hpp"
#include "gui_forms/controls/panel/anchored_overlay_layer/anchored_overlay_layer.hpp"
#include "gui_forms/window.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gui_forms::detail {

// The state of one box's completion. `typed` is what the user's last edit
// left, restored by Escape; `shown_revision` is the text store's revision
// right after the box last showed completion text (an appended completion or
// a highlighted suggestion), so a later edit of the user's is never undone
// by Escape. The overlay members are set while the list is open. The
// revocation subscription is declared after the overlay token so that it is
// disconnected first when the session is destroyed.
struct TextBoxAutoCompleteSession final {
    std::string typed{};
    TextSelection typed_selection{};
    std::vector<std::string> suggestions{};
    std::optional<std::size_t> highlighted{};
    std::uint64_t shown_revision{};
    // The box shows completion text (shown_revision is valid).
    bool shown{};
    // The shown text is an appended completion, selected as
    // completion_selection, that Enter or Tab accepts.
    bool appended{};
    TextSelection completion_selection{};
    bool open{};
    // Set while the box moves the list's selection itself, so the list's
    // selection event is not taken as the user's.
    bool synchronizing{};
    // Set while the box closes the list, so the revocation the close raises
    // is not taken as the window closing it.
    bool closing{};
    std::shared_ptr<AnchoredOverlayLayer> layer{};
    std::shared_ptr<DropDownList> list{};
    OverlayToken token{};
    SubscriptionToken list_selection{};
    SubscriptionToken list_activation{};
    SubscriptionToken dismissal{};
    SubscriptionToken revocation{};
    SubscriptionToken collection_change{};
};

} // namespace gui_forms::detail
