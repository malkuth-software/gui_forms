# d15_rewrite: the typed D15 rewriter

A Clang 22.1.8 LibTooling tool that carries out the D15 vocabulary stages over
a `compile_commands.json`, with full type information, instead of hand or
regex passes:

| stage | from | to |
|---|---|---|
| V1 | `std::optional<T>` | `stxlib::Optional<T>` |
| V2 | `std::string_view` and `const std::string&` parameters | `stxlib::TextView` |
| V3 | `std::string` | `stxlib::Utf8String` (allocating returns optionally `OperationResult`, Q1) |
| V4 | `std::vector<T>`, `std::span<T>` | `stxlib::Vector<T>`, `stxlib::View<T>` / `MutableView<T>` |
| V5 | `std::shared_ptr`, `weak_ptr`, `unique_ptr`, the pointer casts | `stxlib::Shared`, `Weak`, `Unique`, `static_shared_cast`, `dynamic_shared_cast` |

It rewrites the declarations and every use site in `include src tests examples
tools demo lab benchmarks fuzz file_manager_demoboard` (with `src/host`), and
writes a residue report: every site it would not or could not rewrite, with a
reason.

## How it works

1. **Plan.** Each translation unit records *entities* (fields, variables,
   locals, parameters, function returns, type aliases, V5 shared bases),
   keyed by USR (locals by file and offset), with their eligibility, and the
   *edges* between them:
   - strict edges join entities that must change together (overrides, a
     declaration and its redeclarations, aliases, `auto` locals, V5 flows);
   - soft edges are value flows (assignment, initialisation, argument to
     parameter, return), followed only where the stage turns them on.
2. **Closure.** The plans of every unit (and every configuration) are merged
   and closed with union-find. A component converts when it holds a seeded,
   eligible entity (seeds: the public surface, `include/gui_forms`, minus the
   C ABI `include/gui_forms/c`). An ineligible member blocks its whole strict
   component and is reported.
3. **Rewrite.** Each unit is analysed again with the converted set: type
   spellings are replaced, values flowing into converted entities are wrapped
   (`::of`, `::none()`, `take_or_fail_fast(...)`), member calls are renamed
   (`has_value` to `present`/`absent`, `size` to `count`, `push_back` to
   `check_or_fail_fast(push(...))`, `->` to `(*x).`), and values flowing out to
   unconverted standard code go through `src/platform/stxlib_bridge.hpp`
   (never from `include/`, where a boundary is residue instead).

Eligibility, by rule (each refusal is a residue line):
- the value type's copy must be nothrow (the STXlib types are recognised by
  name: `Utf8String`, `Vector<nothrow T>`, `Buffer`, `Path`, `PriorityQueue`
  and `Optional<nothrow T>` copy; `Unique`, `OperationResult` and the frozen maps do
  not), since a converted record must keep its copies; a deleted copy
  (declared, or implied by a declared move or a member) counts as throwing,
  though Clang calls a copy deleted on its first declaration trivial;
- for `Optional` and `Vector`, the value type's move and destructor must be
  nothrow too (`stxlib::Optional` static_asserts both);
- a vocabulary bridge stays as written: a function that takes a standard
  type and returns its stxlib counterpart of the same value type, or the
  reverse (`connected_topology_from_standard`), or one marked
  `[[clang::annotate("gui_forms::d15_bridge")]]`; the private bridge header
  is excluded by path;
- a declaration whose declared type the rewrite cannot write (spelled
  through a macro, a `decltype`, a qualifier other than `std::`) is not
  converted, so its uses are not rewritten without it;
- vectors need the standard allocator, spans a dynamic extent, `unique_ptr`
  the default deleter and a non-array type;
- template-dependent code and macro bodies are not rewritten (macro
  *arguments* are, at their spelling, when the range lies within one
  argument written at the macro's use). A template-dependent value flowing
  into a converted (or already stxlib) declaration is judged by the
  instantiations the unit made: nothing changes when every one already
  gives the stxlib type, a plain value in every one is wrapped, anything
  else is residue, and a template never instantiated in the unit is left
  to the units that do;
- a converted variable declared in a condition becomes an init-statement
  and a test, `if (stxlib::Optional<T> x = f(); x.present())`; a `while`,
  `switch` or `for` condition, or an `if` with an init-statement already,
  is residue;
- a declaration with a deprecated D15 compatibility overload (Q3), or one
  whose rewrite would collide with a sibling overload, stays as it is;
- ordering comparisons (`<` on an optional) are reported, not guessed;
- V2 text boundaries (below): a converted parameter whose use needs the
  private bridge where the bridge cannot be written (a public header, a
  macro body) stays standard.

### V2 text boundaries

V2 converts parameters only and has no flow edges, so a converted
parameter meets standard text wherever its body or its callers still use
it. Outside `include/` each such site goes through
`src/platform/stxlib_bridge.hpp`:

| use of a converted parameter `name` | rewritten as |
|---|---|
| `std::string(name)` (one argument, parenthesised) | `gui_forms::platform::standard_text_copy(name)` |
| `std::string moved(name)` (direct initialisation, one argument) | `std::string moved(gui_forms::platform::standard_text_copy(name))`, once: the initialiser's flow does not bridge it again |
| any other standard text made from it, a cast to standard text | `standard_text(name)` inside the construction or cast |
| an ordering comparison, a range-based for, a subscript, `std::hash<std::string_view>{}(name)`, any other standard operator | `standard_text(name)` (`standard_text_copy(name)` where the operator takes `std::string`, as `operator+`) |
| `==` / `!=` with a literal, a TextView or converted text | unchanged |
| `==` / `!=` with other standard text | `standard_text(name) == other` |
| `size()`, `length()` | `count()` |
| `empty()`, `data()` | unchanged |
| a `std::string_view` method; a `std::string` method returning `std::string` (`substr`) | `standard_text(name).method(...)`; `standard_text_copy(name).method(...)` |
| a `const std::string&` passed where a view is taken | the view conversion denotes `name`: converted text needs nothing, standard text gets `standard_text(name)` |
| an argument, initialiser, member initialiser or return into standard text (`std::logic_error(name)`, `map.find(name)`) | `standard_text_copy(name)` for a `std::string` parameter, else `standard_text(name)` |
| an empty default spelled with its type (`= std::string_view()`) | `= stxlib::TextView()`; `= {}` stays as written |
| an assignment through a standard `operator=` (`binding_value = name`) | the bridge the operator's deduced parameter takes: `standard_text_copy` for a variant's or optional's converting `operator=`, `standard_text` for a view's |
| `static_cast<void>(name)`, a lambda capture | unchanged |

A public header cannot include the bridge. There the plan phase keeps the
parameter standard instead of leaving a site that does not compile: a use
or flow that would need the bridge *refuses* the parameter, and the
refusal becomes its ineligibility, which the closure reports as residue
(`parameter X not converted: V2: ... (path:line)`). A header flow or `==`
between two candidate parameters ties them strictly, so they change
together or not at all. Standard text passed to a header candidate
(`valid_stable_id(value_)` from `StableIdLiteral`'s consteval constructor,
`HelpPlace(kind, anchor.value())`) refuses that candidate, so a consteval
or constexpr caller stays a constant expression. These also refuse, in
any file: `c_str()` and the `std::string` methods a view lacks, an
assignment to the parameter, its address, a forwarding factory
(`std::make_unique<T>(name)`), an unresolved or variadic callee, a
template-dependent value passed to a candidate, and any use with no
rule above. Also refused, because code the rewrite cannot see fixes the
parameter's type:
- a function whose address is taken (a member bound as an event
  handler: `Delegate<const std::string&>::bind<T, &T::handler>`,
  `on(event, *this, &T::handler)`, a `WeakMemberCallback`): every
  candidate parameter of it;
- a call operator's parameters (a callback object an event calls);
- a candidate given a literal that is not well-formed UTF-8 (a TextView
  cannot hold it, and the consteval literal constructor rejects it), or
  compared with one;
- every overload's candidate parameter at a position where a template
  passes anything but a literal or a TextView through a call it leaves
  unresolved (`bind_form_value(target, property, source, direction)`). A conditional's branch is judged by the conditional's use;
- a candidate given anything but a well-formed literal or a TextView by
  evidence code (below).

Evidence: in V2 the plan also reads the units under `--evidence` (default
`tests`) that lie outside `--scope`. They are never rewritten, so they
record no entities and no edges; they only refuse, by rule E and because
what they pass to a candidate must compile unchanged against a TextView.
With the default scope, which includes `tests`, evidence adds nothing.

A first-party header clang marks a system header is still read as
first-party. CMake's prefix header (`cmake_pch.hxx`) opens with
`#pragma clang system_header`, and every header it includes inherits the
mark. The tests' shared prefix includes `gui_forms.hpp`, so in a test unit
every public header was a system header. The plan then recorded no entity
and no call into those headers from the test units, and the 2026-11-17 V2 run
converted eleven parameters that tests give malformed literals. A location is
foreign only when it is a system header outside the scope and the evidence.

Implicit copies of text and vectors need no rewriting (STXlib `claude/vocab-copy`
6e4f66b gives them ordinary noexcept copies); only `Unique` and `OperationResult` sites
do.

## Determinism and idempotence

Edits are byte ranges, sorted and deduplicated across units (a header seen by
every unit yields its edits once). Each edit names its site (`group`: the
rule and the anchor's offset), and a site's edits are applied together or
not at all: identical edits from two sites merge and join the sites; a
replacement overlapping another, an insertion strictly inside a
replacement, two wrapped or replaced ranges that cross, or one range
wrapped by two different openers that share a closer refuse every site
involved (except the bridge's view and copy of one range, which merge
into the copy), each reported once as `conflicting edits` residue, so a refusal
never leaves half a wrapper behind. Output depends only on the sources and the
arguments, not on `--jobs` or unit order. A second run of the same stage
finds nothing to change (the fixtures check this). `--mode check` writes
nothing and exits 1 when any file would change.

Every unit is parsed without the build tree's precompiled header: an apply
makes it stale, and a check through it reports the PCH's errors, not the
code's (919 such units after V1's apply). The tool drops `-include-pch FILE`
(plain or CMake's `-Xclang` pairs) from each compile command and keeps the
prefix header (`-Xclang -include -Xclang cmake_pch.hxx`) as a textual
include; a plain `-include HEADER` with a `.pch` or `.gch` beside it is
turned into the cc1 form, so the driver does not pick that up either.

## Options

- `--stage V1..V5` (required); `--mode check|plan|apply` (default `check`);
  `--source-root` (required).
- `--seed DIR`, `--seed-exclude DIR`, `--scope DIR`, `--exclude DIR`
  (repeatable; defaults as above, excluding `third_party`, `build`, `.build`,
  `tools/d15_rewrite` and the bridge header).
- `--evidence DIR` (repeatable; default `tests`; `none` for none): V2
  plan-only units outside the scope (above). `--units` limits them too.
- `--units DIR` (repeatable): analyse only units under these directories
  (per-directory runs); `--only REGEX`: rewrite only matching files.
- `--kinds field,variable,parameter,function_return,...`: seed only these.
- `--surface-headers FILE`, `--surface-exports FILE` (repeatable): the
  consumer surface (below).
- `--propagate-locals on|off|default`, `--flow-edges on|off|default`. Stage
  defaults: V1 both on; V2 parameters only, no propagation; V3 and V4 off;
  V5 adds shared bases.
- `--allocating-returns plain|result` (Q1; default `plain`).
- `--drop-standard-includes`: remove a standard include the file no longer
  uses.
- `--jobs N`, `--report FILE.json`, `--residue FILE`, `--quiet`.
- `--plan-output`, `--plan-input` (repeatable), `--edits-output`,
  `--edits-input` (repeatable): the several-configuration workflow below.

## Consumer surface

The owner limited D15 to the consumer surface. `--surface-exports
modules/gui_forms.cppm` reads the names the gui_forms module exports
(`using gui_forms::Name;`, written by `tools/module_exports.py` on
`claude/consumer-partition`); `--surface-headers FILE` reads a list of
headers or directories, one relative path per line (`#` comments). Either
or both: an entity is on the surface when it is declared in a listed
header, or inside an exported declaration (a member of an exported class,
a parameter or return of an exported function). Locals are always on it.

With a surface, only entities on it are seeded, and a value flow does not
carry a conversion to a non-local declaration off it: that declaration
keeps its standard type, and the site becomes a bridged boundary
(`gui_forms::platform::standard_optional(...)` outside `include/`,
residue inside). Structural ties still convert together: overrides,
redeclarations, aliases and `auto` locals. The edits therefore land in the
surface's declarations and in the code that uses them (callers' locals
and use sites, anywhere in `--scope`).

## On the M4

All commands run from the integration checkout, through `m4ws integration`
(`bin/m4ws`), never on the laptop.

### Build the tool

```sh
m4ws integration cmake -S tools/d15_rewrite -B .build/d15-rewrite -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm@22/bin/clang++ \
    -DLLVM_DIR=/opt/homebrew/opt/llvm@22/lib/cmake/llvm \
    -DClang_DIR=/opt/homebrew/opt/llvm@22/lib/cmake/clang
m4ws integration cmake --build .build/d15-rewrite
```

### Run the fixtures

STXlib must be at or after `claude/vocab-copy` 6e4f66b (`stxlib/fail_fast.hpp`).
Its include directory is the one beside the installed `stxlibConfig.cmake` of
the build tree's stx package (`find .build -path '*/stx/include/stxlib' -type d`).

```sh
m4ws integration sh -c 'python3 -B tools/d15_rewrite/tests/run_fixtures.py \
    --tool .build/d15-rewrite/gui_forms_d15_rewrite \
    --stxlib-include "$(dirname "$(find .build/native-macos-arm64 -path "*/stx/include/stxlib" -type d | head -n 1)")" \
    --extra-arg=-resource-dir=$(/opt/homebrew/opt/llvm@22/bin/clang -print-resource-dir) \
    --extra-arg=-isysroot$(xcrun --show-sdk-path) --compile-results'
```

Each fixture prints `ok` or `FAIL` with a diff; `--keep` keeps the rewritten
trees, `--only NAME` runs one.

### compile_commands.json per configuration

`ci_build.py` does not export the database. After an ordinary build of each
tree, reconfigure it once with the export on (no rebuild needed):

```sh
m4ws integration python3 -B tools/ci_build.py
m4ws integration cmake -S . -B .build/native-macos-arm64/gui-forms -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
m4ws integration python3 -B tools/ci_build.py --high-integrity
m4ws integration cmake -S . -B .build/native-macos-arm64/gui-forms-high-integrity -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Linux, in the Lima guest `gui-forms-linux` (tools/linux_vm/README.md), over
the same checkout mounted in the guest:

```sh
limactl shell gui-forms-linux -- sh -c 'cd <checkout> && python3 -B tools/ci_build.py && \
    cmake -S . -B .build/native-linux-arm64/gui-forms -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && \
    cmake -S tools/d15_rewrite -B .build/d15-rewrite-linux -G Ninja -DLLVM_DIR=/usr/lib/llvm22/lib/cmake/llvm \
        -DClang_DIR=/usr/lib/llvm22/lib/cmake/clang && cmake --build .build/d15-rewrite-linux'
```

(in the guest, pass `GUI_FORMS_D15_TOOL=.build/d15-rewrite-linux/gui_forms_d15_rewrite`
and `GUI_FORMS_D15_LLVM=/usr/lib/llvm22` to `run_stage.sh`).

Includes the rewrite adds go in the file's leading include block (the
lines before the first that is neither blank, a comment nor a
preprocessor directive), at `#if` depth zero (an include guard does not
count): never after an include further down, such as a generated table
included inside an array, or inside a conditional block.

The Windows host is not compiled anywhere; after the apply, a textual scan
names every converted member found in a scope file no unit saw (for example
`src/surface/windows`) as residue, to be fixed by hand.

### Running a stage (several configurations)

The plans of every configuration are merged before the closure, and the
edits of every configuration before anything is written, so no run reads a
half-rewritten tree. With `S=V1` and `D=build/d15/$S`:

```sh
m4ws integration mkdir -p build/d15/V1
# 1. plan, per configuration
m4ws integration tools/d15_rewrite/run_stage.sh .build/native-macos-arm64/gui-forms V1 \
    --mode plan --plan-output build/d15/V1/macos.plan
m4ws integration tools/d15_rewrite/run_stage.sh .build/native-macos-arm64/gui-forms-high-integrity V1 \
    --mode plan --plan-output build/d15/V1/high-integrity.plan
#    (Linux guest: same with .build/native-linux-arm64/gui-forms, output build/d15/V1/linux.plan)
# 2. edits, per configuration, from the merged plans
for configuration in macos:gui-forms high-integrity:gui-forms-high-integrity; do
    name=${configuration%%:*}; tree=${configuration#*:}
    m4ws integration tools/d15_rewrite/run_stage.sh .build/native-macos-arm64/$tree V1 --mode check \
        --plan-input build/d15/V1/macos.plan --plan-input build/d15/V1/high-integrity.plan \
        --plan-input build/d15/V1/linux.plan --edits-output build/d15/V1/$name.edits
done
#    (Linux guest: same, --edits-output build/d15/V1/linux.edits)
# 3. merge and apply once
m4ws integration tools/d15_rewrite/run_stage.sh .build/native-macos-arm64/gui-forms V1 --mode apply \
    --edits-input build/d15/V1/macos.edits --edits-input build/d15/V1/high-integrity.edits \
    --edits-input build/d15/V1/linux.edits \
    --residue build/d15/V1/residue.txt --report build/d15/V1/report.json
# 4. idempotence
m4ws integration tools/d15_rewrite/run_stage.sh .build/native-macos-arm64/gui-forms V1 --mode check
```

A single-configuration run is `run_stage.sh <tree> V1 --mode apply`. A
per-directory run adds `--units src/controls --only '^src/controls/'` (the
closure still sees every unit's plan when `--plan-input` is given).

Residue lines read `path:line:column: reason | source snippet`.

### Census and checks after a stage

```sh
m4ws integration python3 -B tools/std_surface_census.py scan
m4ws integration python3 -B tools/std_surface_census.py check
m4ws integration python3 -B tools/std_surface_census.py update   # once the stage is accepted
m4ws integration python3 -B tools/check_house_style.py include src demo tests examples tools file_manager_demoboard
m4ws integration python3 -B tools/check_lifetime_rules.py include src demo tests examples tools file_manager_demoboard
m4ws integration python3 -B tools/check_std_rules.py
m4ws integration python3 -B tools/generate_library_docs.py --check
m4ws integration python3 -B tools/abi_event_catalog.py --check
m4ws integration python3 -B tools/ci_build.py --sanitize
```

## Fixtures

`tests/fixtures/<name>/{input,expected}/` hold small gui_forms-shaped trees
as `.in` files (kept out of the house-style scan), with `args` (the tool
arguments), an optional `residue` (substrings the report must contain), an
optional `skip-compile` (the reason the expected result keeps residue that
cannot compile; `--compile-results` skips that fixture), an optional
`database-args` (extra compiler arguments in every compile command) and an
optional `skip-idempotence` (the reason a second run cannot be checked: the
arguments carry edits). `{root}` in `args` and `database-args` names the
fixture's scratch root.
They cover the D15 V1 queue entry's miss list:

| miss pattern | fixture |
|---|---|
| bare `T` written to / compared with an Optional (`::of`) | v1_basics, v1_flows |
| `has_value` to `present` / `absent`; bool tests | v1_basics, v1_flows |
| `nullopt` to `::none()`, `== nullopt` to `absent()`, member-initialiser `nullopt` | v1_basics, v1_flows |
| rvalue `value()` / `*` to `take()`; lvalue `value()` / `->` | v1_basics |
| plain-value and ternary returns | v1_flows |
| setters: deprecated Q3 overload, deleted plain overload, `{}` argument | v1_basics, v1_guards |
| virtual overrides change together | v1_flows |
| test helpers taking the optional | v1_basics |
| default arguments, `make_optional`, `emplace`, aggregate initialisation | v1_flows |
| macro-argument conditions (`REQUIRE(...)`), rewritten at their spelling | v1_flows |
| ordering comparisons reported, left as written | v1_ordering |
| throwing value copies and templates left alone | v1_guards |
| text parameters and the validated bridge | v2_text_parameters |
| owned text: assignment, comparison, size, copies out | v3_owned_text |
| vectors and spans: push, reserve, view arguments | v4_sequences |
| shared/weak: make, casts, `->`, `nullptr` | v5_ownership |
| templates reading an already stxlib value through a dependent call (V1 run defect 1) | v1_template_reads |
| a value type whose copy is deleted or whose move can throw (defect 2, MotionValue) | v1_motion_move |
| a vocabulary bridge's own parameter (defect 3) | v1_bridge |
| condition declarations (defect 4) | v1_condition, v1_condition_residue |
| a dependent parenthesised initialiser (defect 5, linear_light_tests) | v1_paren_list |
| overlapping, crossing and duplicated edits from merged edits files (defect 5) | v1_overlaps |
| a stale precompiled header in the compile commands (defect 6) | v1_precompiled |
| the consumer surface, by exports and by headers | v1_surface, v1_surface_headers |
| V2 bridges: `std::string(text)`, ordering, range-for, subscript, `std::hash`, methods, view conversions, standard parameters (V2 run patterns 1, 2, 3, 5) | v2_text_uses |
| V2: `= {}` defaults kept, spelled empty defaults (V2 run 2, A) | v2_empty_default |
| V2: one bridge for `std::string moved(id)` (B) | v2_string_from_text |
| V2: two bridges of one range merged into the copy, other doubled wrappers refused (B) | v2_merged_bridges |
| V2: the bridge include goes in the leading include block, outside `#if`, not after a table included in an array (C) | v2_include_region |
| V2: handlers bound by address and call operators stay standard (D, H) | v2_handlers |
| V2: literals that are not UTF-8 keep their parameter standard (E) | v2_invalid_literal |
| V2: rule E through a prefix header marked `#pragma clang system_header`: member, `const std::string&`, second argument (E, 2026-11-17) | v2_prefix_system_header |
| V2: evidence units outside `--scope` refuse (malformed literal, standard text) but are not rewritten | v2_evidence_tests |
| V2: overloads a template calls unresolved stay standard (F) | v2_template_overloads |
| V2: assignments pick the bridge from the operator's parameter (G) | v2_assignment_target |
| V2 public-header boundaries: consteval literal check, standard exceptions, members and maps, hash, tied candidates, spelled empty default (V2 run patterns 3 to 7) | v2_header_boundaries |
| sources the M4 does not compile (Windows host) | the textual scan, run in the real tree |
| benchmarks and fuzz | the default `--scope` |

## Known limits

- Macro bodies are residue; template-dependent code is judged by the
  instantiations each unit makes, and a template no unit instantiates is not
  rewritten.
- The Windows host is a textual scan, not a typed rewrite.
- A rewrite can change overload resolution where a plain and an Optional
  overload both exist; those declarations are refused (`would collide`), but a
  call through a template or a function pointer is not seen.
- The tool is written against Clang 22 (no `ElaboratedType`); it refuses to
  configure with another major version.
