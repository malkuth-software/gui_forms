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
  not), since a converted record must keep its copies;
- vectors need the standard allocator, spans a dynamic extent, `unique_ptr`
  the default deleter and a non-array type;
- template-dependent code and macro bodies are not rewritten (macro
  *arguments* are, at their spelling, when the range lies within one
  argument written at the macro's use);
- a declaration with a deprecated D15 compatibility overload (Q3), or one
  whose rewrite would collide with a sibling overload, stays as it is;
- ordering comparisons (`<` on an optional) are reported, not guessed.

Implicit copies of text and vectors need no rewriting (STXlib `claude/vocab-copy`
6e4f66b gives them ordinary noexcept copies); only `Unique` and `OperationResult` sites
do.

## Determinism and idempotence

Edits are byte ranges, sorted and deduplicated across units (a header seen by
every unit yields its edits once); overlapping edits become `conflict`
residue and neither is applied. Output depends only on the sources and the
arguments, not on `--jobs` or unit order. A second run of the same stage
finds nothing to change (the fixtures check this). `--mode check` writes
nothing and exits 1 when any file would change.

## Options

- `--stage V1..V5` (required); `--mode check|plan|apply` (default `check`);
  `--source-root` (required).
- `--seed DIR`, `--seed-exclude DIR`, `--scope DIR`, `--exclude DIR`
  (repeatable; defaults as above, excluding `third_party`, `build`, `.build`,
  `tools/d15_rewrite` and the bridge header).
- `--units DIR` (repeatable): analyse only units under these directories
  (per-directory runs); `--only REGEX`: rewrite only matching files.
- `--kinds field,variable,parameter,function_return,...`: seed only these.
- `--propagate-locals on|off|default`, `--flow-edges on|off|default`. Stage
  defaults: V1 both on; V2 parameters only, no propagation; V3 and V4 off;
  V5 adds shared bases.
- `--allocating-returns plain|result` (Q1; default `plain`).
- `--drop-standard-includes`: remove a standard include the file no longer
  uses.
- `--jobs N`, `--report FILE.json`, `--residue FILE`, `--quiet`.
- `--plan-output`, `--plan-input` (repeatable), `--edits-output`,
  `--edits-input` (repeatable): the several-configuration workflow below.

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

The Windows host is not compiled anywhere; after the apply, a textual scan
names every converted member found in a scope file no unit saw (for example
`src/host/windows`) as residue, to be fixed by hand.

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
arguments), an optional `residue` (substrings the report must contain) and
an optional `skip-compile` (the reason the expected result keeps residue that
cannot compile; `--compile-results` skips that fixture).
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
| sources the M4 does not compile (Windows host) | the textual scan, run in the real tree |
| benchmarks and fuzz | the default `--scope` |

## Known limits

- Template-dependent code and macro bodies are residue.
- The Windows host is a textual scan, not a typed rewrite.
- A rewrite can change overload resolution where a plain and an Optional
  overload both exist; those declarations are refused (`would collide`), but a
  call through a template or a function pointer is not seen.
- The tool is written against Clang 22 (no `ElaboratedType`); it refuses to
  configure with another major version.
