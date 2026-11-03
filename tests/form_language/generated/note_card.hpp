#pragma once

// An application type for the form compiler's tests (stage FD, phase
// FD6.1): a control the application derives from a registry type, named in
// note_dialog.form as `note-card` through gui_forms_add_form's TYPES.

#include "gui_forms/gui_forms.hpp"

#include <utility>

namespace form_generated {

class NoteCard final : public gui_forms::UserControl {
public:
    explicit NoteCard(gui_forms::StableId stable_id) : gui_forms::UserControl(std::move(stable_id)) {}
};

} // namespace form_generated
