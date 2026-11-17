#pragma once

// Composite controls in form documents (cp34): the schema type a form
// names a composite by. Internal: the run-time loader (load_form) and the
// form compiler build their schemas with it.
//
// A form writes a composite as any control, `<toggle-pill id="..."
// checked="true"/>`, setting and binding its registered properties; a
// composite with a slot takes the element's children into it in document
// order (`<card-with-action ...><label .../></card-with-action>`), one
// without refuses them (E809). The composite's own children stay private.

#include "gui_forms/composites/composite_control/composite_control.hpp"

#include "../../core/form_language/form_model/form_model.hpp"

#include <memory>

namespace gui_forms {

// The type of `definition` for a run-time load: described from a prototype
// made from it and released, named by the definition's registered name,
// made by the loader from the definition, its children going into its
// slot. No C++ class or header: the form compiler names those of the
// generated class (add_composite_type). Throws what making the prototype
// throws.
[[nodiscard]] form_language::FormSchemaType composite_form_type(
    const std::shared_ptr<const CompositeDefinition>& definition);

// The loader's creator and slot finder for composites (FormSchemaType::
// create_composite, find_slot).
[[nodiscard]] Component::Ptr create_composite_form_object(const std::shared_ptr<const CompositeDefinition>& definition,
                                                          StableId stable_id);
[[nodiscard]] Control::Ptr find_composite_slot(Control& owner);

} // namespace gui_forms
