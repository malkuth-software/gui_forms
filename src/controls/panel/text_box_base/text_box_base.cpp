#include "gui_forms/controls/text_box_base.hpp"

#include <utility>

namespace gui_forms {

TextBoxBase::TextBoxBase(StableId stable_id) : Panel(std::move(stable_id)) {}

TextBoxBase::~TextBoxBase() = default;

} // namespace gui_forms
