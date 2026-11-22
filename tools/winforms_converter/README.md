# Windows Forms converter

Converts the forms of a Windows Forms project into GUI.Forms form documents
(stage MCP G7). Each form produces three files:

- **The form document** (`main-form.form`). It holds the form's structure:
  the objects, their stable IDs, their nesting, literal property values and
  `<bind>` elements.
- **The theme sheet** (`main-form.theme.css`). It holds the colours and
  fonts, under stable-ID selectors.
- **The C# binding** (`MainForm.GuiForms.g.cs`). This is a partial class of
  the form class. It finds each object by its stable ID, attaches the form
  class's handlers through `GuiForms.Interop.FormTree`, and lists as
  comments the settings a form document cannot hold.

Every setting that does not carry over exactly gets a diagnostic. Codes run
from `WF001` to `WF302`; see `conversion_diagnostics.hpp`.

## Run

The program is built along with the form compiler wherever the Application
library exists (`GUI_FORMS_BUILD_FORM_COMPILER=ON`).

```text
gui_forms_winforms_converter list path/to/Project
gui_forms_winforms_converter convert path/to/Project MainForm --out converted
gui_forms_winforms_converter validate converted/main-form.form
gui_forms_winforms_converter diff converted/main-form.form edited/main-form.form
gui_forms_winforms_converter preview converted/main-form.form --png main-form.png --original original.png
gui_forms_winforms_converter tools
```

Put `--json` before the subcommand to get the tool's structured reply
instead of its text.

## Model tools

`converter_tools.hpp` describes five tools and runs them without depending
on any transport. Each has a spec (name, description, JSON input schema and
hints), a request record and a reply record:

- `winforms.list_forms`
- `winforms.convert`
- `winforms.validate`
- `winforms.diff`
- `winforms.preview` (in `form_preview.hpp`, which needs the Application
  library)

The MCP server's dispatcher (track M2) binds them. Until then, the `tools`
subcommand prints the specs.

The tools support this loop:

1. Convert the form. `convert` also validates what it wrote.
2. Edit the document to fix what validation reports.
3. Run `diff` against the converter's output to review the edit.
4. Run `preview` to see the result beside the original.

## What it reads

The converter reads `*.Designer.cs` files, but only the subset that the
Windows Forms designer writes:

- field creations;
- property assignments;
- `+=` event subscriptions;
- `Controls.Add` and `AddRange`;
- `Items.AddRange`;
- `DataBindings.Add`;
- `SetToolTip`;
- the TableLayoutPanel cell calls;
- `resources.ApplyResources` and `GetString`.

It does not read any other statement. Each one it skips is reported as
WF103 or WF215 and listed in the binding.

It also reads:

- `.resx` files: only their `data` entries; binary entries are reported as
  WF210;
- the project's C# files: only their class heads and method signatures, to
  learn base classes and handler parameters.

How names map comes from the ledgers that every importer reads
(`manifests/controls.json`, `members.json` and `winforms-divergences.json`).
`emit_member_table.py` compiles those ledgers into `winforms_member_table.hpp`.
After changing a ledger, run `python3 tools/winforms_converter/emit_member_table.py`.
CI runs it with `--check`.

## Limits

The converter has limits of several kinds:

- **Size:** sources up to 8 MiB, 2M tokens, 100,000 statements, and
  expressions nested 64 deep.
- **Projects:** 20,000 C# files in a project.
- **Unmapped types:** a type with no GUI.Forms counterpart becomes a
  `panel` with the class `winforms-unmapped`, and is reported as WF201.
- **Scaling:** automatic scaling is left out (WF301).
- **Preview:** the preview leaves out bindings and draws in the stock theme.
