#pragma once

// The gui_forms plugin's effect pass (design note EFFECT_PROGRAMS_DESIGN.md
// §2 and §3; gui_forms/engine/effect/effect.hpp). It finds every effect
// registered in the translation unit (each instantiation of
// gui_forms::make_effect_entry<E>), walks the call graph from E::run and
// E::setup, rejects every construct the effect rules exclude, checks E's
// state, and bounds the operations of one run and one setup against the
// effect's declared budgets. Signal programs (gui_forms/plots/signal_program)
// are verified the same way: each instantiation of
// gui_forms::plots::make_signal_entry<P>, P::run(frame, sink) walked under
// the same rules and bounded by P's declared operations per sample times
// gui_forms::plots::signal_sink_capacity. Each failure is a compiler error
// at the offending construct, followed by a note naming the effect or
// program.

#include "clang/AST/ASTContext.h"
#include "clang/Basic/Diagnostic.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gui_forms_plugin {

// What the pass proved for one effect or signal program: its registered
// name ("signal:<name>" for a signal program), the static
// bound of one run and of one setup (0 without setup), in operations, and
// sizeof the effect, in bytes. Written to the object file as the effect's
// verification record.
struct EffectRecord final {
    std::string name;
    std::uint32_t bound{};
    std::uint32_t setup_bound{};
    std::uint32_t state_size{};
};

// Runs the pass over the translation unit in `context`, reporting through
// `diagnostics`. Returns the records of the effects that passed every check;
// an effect with any error has none.
std::vector<EffectRecord> verify_effects(clang::ASTContext& context, clang::DiagnosticsEngine& diagnostics);

} // namespace gui_forms_plugin
