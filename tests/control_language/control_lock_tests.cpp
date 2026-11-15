// A composite type's lock file (cp34, phase 1; control_lock.hpp). Gate:
// the committed locks of the worked examples are what the build writes;
// additions within a major version keep the lock; a removed or renamed
// name, a changed kind, a lower version, another type's lock and a
// malformed lock each violate it (E923) at the lock's line; a new major
// version is not held to the old lock.

#include "../../src/core/control_language/control_lock/control_lock.hpp"
#include "../../src/core/control_language/control_model/control_model.hpp"
#include "../../src/core/form_language/form_model/form_model.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"

#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/flex_panel.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/user_control.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#ifndef GUI_FORMS_COMPOSITES_DIR
#error "GUI_FORMS_COMPOSITES_DIR names demo/composites"
#endif

namespace {

using gui_forms::control_language::ControlLockOutcome;
using gui_forms::control_language::ControlModel;
using gui_forms::control_language::ControlModelOutcome;
using gui_forms::control_language::ControlPropertyKind;
using gui_forms::control_language::ControlSyntax;
using gui_forms::control_language::check_control_lock;
using gui_forms::control_language::read_control_model;
using gui_forms::control_language::write_control_lock;
using gui_forms::form_language::FormSchema;
using gui_forms::form_language::form_control_type;
using gui_forms::theme_language::DiagnosticCode;
using gui_forms::theme_language::ThemeDiagnostic;
using gui_forms::theme_language::ThemeDiagnosticList;
using gui_forms::theme_language::ThemeSource;

int failures = 0;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

FormSchema composite_schema() {
    FormSchema schema{};
    schema.types.push_back(form_control_type<gui_forms::UserControl>("user_control"));
    schema.types.push_back(form_control_type<gui_forms::TextBox>("text_box"));
    schema.types.push_back(form_control_type<gui_forms::FlexPanel>("flex_panel"));
    schema.types.push_back(form_control_type<gui_forms::Label>("label"));
    schema.types.push_back(form_control_type<gui_forms::Button>("button"));
    return schema;
}

std::string read_file(const std::string_view name) {
    const std::string path = std::string(GUI_FORMS_COMPOSITES_DIR) + "/" + std::string(name);
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents{};
    contents << stream.rdbuf();
    return contents.str();
}

// Reads an example's model; false (with a failure) when it does not build.
bool example_model(const std::string_view name, ControlModel& model) {
    const FormSchema schema = composite_schema();
    const std::string text = read_file(name);
    ControlSyntax syntax{};
    ThemeDiagnosticList diagnostics{};
    ThemeSource source{};
    source.file = name;
    source.text = text;
    const ControlModelOutcome outcome = read_control_model(source, syntax, schema, model, diagnostics);
    require(outcome == ControlModelOutcome::model_built, std::string(name) + " builds");
    return outcome == ControlModelOutcome::model_built;
}

ControlLockOutcome check(const ControlModel& model, const std::string_view lock, ThemeDiagnosticList& diagnostics) {
    diagnostics.clear();
    const ControlLockOutcome outcome = check_control_lock(model, lock, "test.control.lock", diagnostics);
    return outcome;
}

void test_committed_locks() {
    ControlModel toggle{};
    if (example_model("toggle-pill.control", toggle)) {
        const std::string committed = read_file("toggle-pill.control.lock");
        require(write_control_lock(toggle) == committed, "toggle-pill.control.lock is what the build writes");
        ThemeDiagnosticList diagnostics{};
        require(check(toggle, committed, diagnostics) == ControlLockOutcome::kept, "the toggle keeps its lock");
    }
    ControlModel card{};
    if (example_model("card-with-action.control", card)) {
        const std::string committed = read_file("card-with-action.control.lock");
        require(write_control_lock(card) == committed, "card-with-action.control.lock is what the build writes");
        ThemeDiagnosticList diagnostics{};
        require(check(card, committed, diagnostics) == ControlLockOutcome::kept, "the card keeps its lock");
    }
}

void test_violations() {
    ControlModel model{};
    if (!example_model("toggle-pill.control", model)) return;
    const std::string lock = write_control_lock(model);
    ThemeDiagnosticList diagnostics{};

    // Additions within the major version keep the lock.
    ControlModel added = model;
    added.version_minor = 1U;
    added.commands.push_back(added.commands[0]);
    added.commands.back().name = "flip";
    require(check(added, lock, diagnostics) == ControlLockOutcome::kept, "an added command keeps the lock");

    // A removed part.
    ControlModel removed = model;
    removed.parts.pop_back();
    require(check(removed, lock, diagnostics) == ControlLockOutcome::violated, "a removed part violates the lock");
    require(diagnostics.records().size() == 1U && diagnostics.records()[0].code == DiagnosticCode::control_lock_violation &&
                diagnostics.records()[0].position.line.value == 11U && diagnostics.records()[0].file == "test.control.lock",
            "at the part's line of the lock");

    // A renamed event.
    ControlModel renamed = model;
    renamed.events[0].name = "toggled";
    require(check(renamed, lock, diagnostics) == ControlLockOutcome::violated, "a renamed event violates the lock");

    // A property's changed kind.
    ControlModel changed = model;
    changed.properties[2].kind = ControlPropertyKind::signed_integer;
    require(check(changed, lock, diagnostics) == ControlLockOutcome::violated, "a changed kind violates the lock");
    require(diagnostics.records().size() == 1U && diagnostics.records()[0].rule == "drag-position",
            "the changed property is named");

    // A published state turned custom is a different entry.
    ControlModel republished = model;
    republished.states[0].custom = true;
    require(check(republished, lock, diagnostics) == ControlLockOutcome::violated,
            "a published state may not become a custom one");

    // A lower version, another type's lock, a malformed lock, an empty one.
    ControlModel lower = model;
    lower.version_major = 0U;
    lower.version_minor = 9U;
    require(check(lower, lock, diagnostics) == ControlLockOutcome::violated, "a lower version violates the lock");
    require(check(model, "control card-with-action 1.0\n", diagnostics) == ControlLockOutcome::violated,
            "another type's lock");
    require(check(model, "control toggle-pill 1.0\nproperty checked\n", diagnostics) == ControlLockOutcome::violated,
            "a property entry without its kind");
    require(check(model, "control toggle-pill 1.0\nwidget thing\n", diagnostics) == ControlLockOutcome::violated,
            "an unknown category");
    require(check(model, "toggle-pill 1.0\n", diagnostics) == ControlLockOutcome::violated, "no header");
    require(check(model, "\n# nothing\n", diagnostics) == ControlLockOutcome::violated, "an empty lock");

    // Comments and blank lines are ignored.
    require(check(model, "# the toggle\n\n" + lock, diagnostics) == ControlLockOutcome::kept,
            "comments and blank lines are ignored");

    // A new major version is not held to the old lock.
    ControlModel next = removed;
    next.version_major = 2U;
    require(check(next, lock, diagnostics) == ControlLockOutcome::kept, "a new major version may remove");

    // Every violation at once.
    ControlModel several = model;
    several.parts.clear();
    require(check(several, lock, diagnostics) == ControlLockOutcome::violated &&
                diagnostics.records().size() == 4U,
            "four removed parts, four diagnostics");
}

} // namespace

int main() {
    test_committed_locks();
    test_violations();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "control lock tests passed\n";
    return 0;
}
