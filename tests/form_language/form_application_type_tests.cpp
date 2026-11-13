// Form documents in a CMake build (stage FD, phase FD6.1). Gate: a header
// gui_forms_add_form generated at build time compiles and makes the
// document's application type, as the application's own class, with its
// base type's properties and its bindings; add_application_type refuses
// malformed descriptions and leaves the schema unchanged.

#include "../../tools/form_compiler/form_catalog.hpp"

#include "generated/note_card.hpp"
#include "note_dialog_form.hpp"

#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/commands/types/command_types.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {

using gui_forms::form_language::FormSchema;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// The owner of note_dialog.form: the source its binding names, and a count
// of the command's invocations.
class NoteOwner final {
public:
    NoteOwner() {
        using Invoked = gui_forms::Delegate<const gui_forms::CommandInvocation&>;
        closed_ = close_.invoked().subscribe(Invoked::bind<NoteOwner, &NoteOwner::count_close>(*this));
    }

    void count_close(const gui_forms::CommandInvocation& invocation) {
        static_cast<void>(invocation);
        ++close_calls;
    }

    gui_forms::Command close_{};
    int close_calls{};

private:
    gui_forms::SubscriptionToken closed_{};
};

void test_generated_application_type() {
    NoteOwner owner{};
    form_generated::NoteDialogForm<NoteOwner> form{};
    form.initialize_component(owner);
    const std::shared_ptr<form_generated::NoteCard> card = form.notes_card;
    require(card != nullptr && (*card).padding.read() == gui_forms::Insets{3.0, 3.0, 3.0, 3.0},
            "generated: the application type, with its base type's property");
    require(form.root_control() != nullptr && (*form.root_control()).children().size() == 1U &&
                (*form.root_control()).children()[0] == card,
            "generated: the application type in the tree");
    gui_forms::Window window(form.root_control(), gui_forms::Size{200.0, 100.0});
    static_cast<void>((*form.notes_card_close).perform_click());
    require(owner.close_calls == 1, "generated: the binding in the application type's subtree");
}

// Whether adding `description` throws std::invalid_argument and leaves
// `schema` as it was.
bool refused(FormSchema& schema, std::string_view description) {
    const std::size_t before = schema.types.size();
    bool thrown = false;
    try {
        gui_forms::form_compiler::add_application_type(schema, description);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    return thrown && schema.types.size() == before;
}

void test_application_type_descriptions() {
    FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    const std::size_t before = schema.types.size();
    gui_forms::form_compiler::add_application_type(schema, "note_card,app::NoteCard,user_control,app/note_card.hpp");
    require(schema.types.size() == before + 1U && schema.types.back().name == "note_card" &&
                schema.types.back().cpp_type == "app::NoteCard" && schema.types.back().header == "app/note_card.hpp" &&
                schema.types.back().create == nullptr && schema.types.back().holds_children,
            "descriptions: added, with the base's description and no creator");
    require(refused(schema, "note_card,app::Other,user_control,app/other.hpp"), "descriptions: a repeated name");
    require(refused(schema, "pin,app::Pin,no_such_type,app/pin.hpp"), "descriptions: an unknown base");
    require(refused(schema, "pin,app::Pin,form,app/pin.hpp"), "descriptions: the form root as a base");
    require(refused(schema, "pin,app::Pin,user_control"), "descriptions: three fields");
    require(refused(schema, "pin,app::Pin,user_control,app/pin.hpp,extra"), "descriptions: five fields");
    require(refused(schema, "Pin,app::Pin,user_control,app/pin.hpp"), "descriptions: an upper-case name");
    require(refused(schema, "pin,::app::Pin,user_control,app/pin.hpp"), "descriptions: a leading ::");
    require(refused(schema, "pin,app::Pin,user_control,"), "descriptions: an empty header");
}

// The installed compiler's schema (form_core_catalog.cpp) names no lab or
// demonstration control (audit F9): those types left the registry, and only
// the Gallery's compiler adds them (form_lab_catalog.cpp).
void test_installed_schema_has_no_lab_types() {
    const FormSchema schema = gui_forms::form_compiler::registry_form_schema();
    bool lab_type = false;
    for (const gui_forms::form_language::FormSchemaType& type : schema.types) {
        const bool lab_name = type.name == "review_card" || type.name == "correspondence_view" ||
                              type.name == "instrument_rack" || type.name == "easing_preview" ||
                              type.name == "typography_specimen" || type.name == "visual_inspector_view" ||
                              type.name == "visual_inspector_overlay";
        if (lab_name) lab_type = true;
    }
    require(!lab_type, "installed schema: no lab or demonstration control");
}

} // namespace

int main() {
    test_generated_application_type();
    test_application_type_descriptions();
    test_installed_schema_has_no_lab_types();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "form application type tests passed\n";
    return 0;
}
