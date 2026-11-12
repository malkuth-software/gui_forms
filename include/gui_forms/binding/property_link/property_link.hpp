#pragma once

#include "gui_forms/export/application_export.hpp"

// A link of one property of a control to a source outside it (stage FD,
// FD-6): what a form binding made. The control it links keeps it
// (Control::adopt_property_link) and ends it when the control is destroyed
// (owner ruling FD-6 R4, 2026-11-07), so no link outlives its control.
// Kinds of link differ at run time (a scalar Value, later others), hence
// the virtual destructor; a link has no other interface.

namespace gui_forms {

class GUI_FORMS_API PropertyLink {
public:
    PropertyLink() = default;
    virtual ~PropertyLink();
    PropertyLink(const PropertyLink&) = delete;
    PropertyLink& operator=(const PropertyLink&) = delete;
};

} // namespace gui_forms
