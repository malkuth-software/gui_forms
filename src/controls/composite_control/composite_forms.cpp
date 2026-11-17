// Composite controls in form documents (cp34). See composite_forms.hpp.

#include "composite_forms.hpp"

#include "../../core/control_language/composite_definition/composite_definition.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace gui_forms {

namespace {

// The stable ID of the prototype a composite's form type is described from.
constexpr std::string_view prototype_stable_id = "prototype";

} // namespace

Component::Ptr create_composite_form_object(const std::shared_ptr<const CompositeDefinition>& definition,
                                            StableId stable_id) {
    const std::shared_ptr<CompositeControl> made = make_control<CompositeControl>(std::move(stable_id), definition);
    return made;
}

Control::Ptr find_composite_slot(Control& owner) {
    CompositeControl* const composite = dynamic_cast<CompositeControl*>(&owner);
    if (composite == nullptr) return nullptr;
    Control::Ptr found = (*composite).slot();
    return found;
}

form_language::FormSchemaType composite_form_type(const std::shared_ptr<const CompositeDefinition>& definition) {
    const std::shared_ptr<CompositeControl> prototype =
        make_control<CompositeControl>(StableId(std::string(prototype_stable_id)), definition);
    form_language::FormSchemaType type =
        form_language::describe_form_type((*definition).registered, *prototype);
    type.composite = definition;
    type.create_composite = &create_composite_form_object;
    type.final_type = false;
    const bool slotted = !(*definition).slot.empty();
    type.child_placement =
        slotted ? form_language::FormChildPlacement::slot : form_language::FormChildPlacement::refused;
    type.find_slot = &find_composite_slot;
    return type;
}

} // namespace gui_forms
