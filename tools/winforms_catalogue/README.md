# WinForms catalogue generator

This tool turns pinned reference surfaces into the conservative GUI.Forms
implementation ledger:

- the official Windows Forms shipped API lists, `PublicAPI.Shipped.txt` of
  `System.Windows.Forms` and `System.Windows.Forms.Primitives` in
  `dotnet/winforms`, on `release/8.0`, `release/9.0` and `release/10.0` (the
  commits are recorded in `generate_catalogue.py` and the summary);
- the `[Obsolete]` markers of the matching `Microsoft.WindowsDesktop.App.Ref`
  reference packs, read by `read_obsolete.py`, which parses the assemblies'
  metadata as data and executes nothing from the package;
- the `System.Drawing` rows of the earlier catalogue (.NET 10.0.3
  `System.Drawing.Common.xml` and `System.Drawing.Primitives.xml`), carried
  unchanged.

Until 2026-10-14 the Forms rows came from the LibreWinForms fork's
`PublicAPI.Shipped.txt` (commit `1457ed5beef24a7d7e3688a4725c7487da09791e`).
Stage W replaced them with the official lists under the owner's scope rule:
a row is in scope when some version of .NET 8.0 or later lists it without
`[Obsolete]`. The same API spelled with and without nullability annotations
in different versions is one row.

It records every published type/member row and applies only the exclusions in
the accepted GUI.Forms boundary. It does not infer support from name similarity.
Rows associated with a native retained type remain `partial_native` until exact
behavioral evidence promotes them.

M12-P4 promotes the measured ErrorProvider/HelpProvider visual and guidance
centers. M12-P5 promotes retained binding/currency. M12-P6 promotes the exact
validation enums/events/container calls and binding-aware ErrorProvider calls.
M12-P7 promotes renderer-free mnemonic parsing/routing, programmatic button
commands, and Window/Form accept/cancel dialog-key behavior.
M12-P8 promotes stable duplicate arbitration, MenuStrip/overlay mnemonic
behavior, exact DialogResult/IButtonControl centers, and Click-before-result
propagation while retaining native modal closure as open host work.
M12-P9 promotes the base Control geometry/order family: exact geometry enums,
masked bounds, preferred/min/max/AutoSize sizing, client/window transforms,
direct-child filtering, nested tab traversal, and topmost-first collection
semantics. True portable screen origin/multi-monitor conversion remains host
contract work rather than being inferred from AppKit or Win32.
M12-P10 promotes the bounded scrolling family: `ScrollableControl`,
`ScrollProperties`, horizontal/vertical properties, exact scroll enums/events,
`Control.AutoScrollOffset`, automatic/manual axes, viewport/display geometry,
and control reveal. RTL/scaling and exhaustive independent WinForms ordering
remain explicitly open.
M12-P11 promotes per-control layout transactions: nested `SuspendLayout`, both
`ResumeLayout` forms, both `PerformLayout` forms, exact `LayoutEventArgs`
payloads, committed geometry while suspended, retained ABI 0.21 state, bounded
re-entry, and fault recovery. Exhaustive independent WinForms event ordering
and designer-scale convergence remain explicitly open.
The generator deliberately leaves arbitrary managed IDataErrorInfo/reflection,
true nested object traversal, Help TopicId/raw enum projection, protected
disposal/site shapes, and unproved constructors partial or missing rather than
inferring closure from the public type name.

M12-P20 adds native instance-owned converter/editor registries and retained
custom-editor ownership, but deliberately promotes no managed `PropertyGrid`,
`GridItem`, `TypeConverter`, or component-editor row. Those identities remain
missing until their exact generated facade behavior is projected and measured.

```sh
# Inputs (ask the owner before downloading): for each VERSION in 8.0 9.0 10.0,
#   https://raw.githubusercontent.com/dotnet/winforms/release/VERSION/src/System.Windows.Forms/src/PublicAPI.Shipped.txt
#   (10.0: src/System.Windows.Forms/PublicAPI.Shipped.txt), the same for
#   System.Windows.Forms.Primitives, and the Microsoft.WindowsDesktop.App.Ref
#   .nupkg of that version from nuget.org.
python3 -I tools/winforms_catalogue/read_obsolete.py   --package ref-8.0.32.nupkg --package ref-9.0.21.nupkg --package ref-10.0.13.nupkg   --output obsolete.tsv
# NOTES is a checkout of the gui_forms branch of malkuth-software/notes.
cp "$NOTES/planning/generated/WINFORMS_API_CATALOGUE.tsv" previous.tsv
python3 -I tools/winforms_catalogue/generate_catalogue.py   --forms-api 8.0=api-8.0.txt --forms-api 8.0=primitives-8.0.txt   --forms-api 9.0=api-9.0.txt --forms-api 9.0=primitives-9.0.txt   --forms-api 10.0=api-10.0.txt --forms-api 10.0=primitives-10.0.txt   --obsolete obsolete.tsv --drawing-rows previous.tsv   --output "$NOTES/planning/generated/WINFORMS_API_CATALOGUE.tsv"   --summary "$NOTES/planning/WINFORMS_API_CATALOGUE.md"
```

The full TSV is intentionally generated and checked in to the notes
repository's `gui_forms` branch: it is the inspectable
member-level backlog. The summary contains counts and the policy boundary.
