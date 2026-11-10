# SDRSharp reflection system

This directory contains the reproducible compatibility harness for running a
separately obtained SDRSharp distribution through a Windows Forms facade over
the sealed GUI.Forms C ABI (`gui_forms_abi1.dll`, through
`bindings/csharp/GuiForms.Interop`) and the GUI.Drawing facade under Wine. The
facade itself is not in this repository: the swap-in launcher's facade is the
current one, and any facade over `GuiForms.Interop` with the same assembly
names will do. (The generated 0.x facade over `gui_forms_abi0.dll` was retired
with that library before 1.0.)

The repository does not contain SDRSharp binaries, extracted bundle entries,
configuration, layouts, runtime logs, decompiled or disassembled output, or
private member inventories. The user supplies an unchanged SDRSharp
distribution. Runtime extraction and writable application state remain outside
the repository.

The reflection runner performs four bounded operations:

1. verify and extract the user-supplied .NET single-file bundle into a private
   runtime directory;
2. substitute the facade's GUI.Forms and GUI.Drawing compatibility assemblies;
3. resolve the required Windows .NET and native GUI.Forms libraries; and
4. invoke the application's ordinary managed entry point.

It does not inspect application methods or fields.

## Supported replication model

The build and run may happen on the same computer. No remote build host,
machine-specific directory, screen-sharing service, or particular computer
model is required.

The recorded workflow uses a Unix-like host capable of:

- building the GUI.Forms Windows x64 target and a managed facade over it;
- running Windows x64 .NET applications through Wine;
- supplying a native .NET 10 SDK for the build;
- supplying the Windows x64 .NET 10.0.5 runtime and Windows Desktop reference
  pack; and
- providing `wine` and `winepath`.

Equivalent tool locations are supported through environment variables. The
commands below use descriptive placeholders; replace every `/absolute/path`
with a path on the replication machine.

## Required inputs

Prepare these inputs before launching:

- a GUI.Forms source checkout;
- a legitimately obtained SDRSharp directory containing `SDRSharp.exe` and its
  adjacent native/plugin files;
- a completed GUI.Forms Windows x64 build containing:
  - `gui_forms_abi1.dll`
  - `gui_drawing_abi0.dll`
  - `gui_drawing_raster0.dll`
  - `fonts/PortsmouthRapids.ttf`
- Release builds of the facade (with its `GuiForms.Interop.dll` beside them)
  and the runner:
  - `System.Windows.Forms.dll`
  - `System.Windows.Forms.Primitives.dll`
  - `System.Drawing.Common.dll`
  - `GuiForms.SdrSharpReflectionRunner.dll`
- a Windows x64 .NET runtime containing both `Microsoft.NETCore.App/10.0.5`
  and `Microsoft.WindowsDesktop.App/10.0.5`.

Do not copy the SDRSharp distribution, profile, extracted files, or logs into
the GUI.Forms checkout.

## Private runtime layout

Create a writable directory outside Git:

```text
reflection-runtime/
├── specimen/   user-supplied SDRSharp distribution
├── profile/    writable settings and layout
├── extract/    generated bundle extraction
└── logs/       optional local launch logs
```

For example:

```sh
export GUI_FORMS_ROOT=/absolute/path/to/file_manager/gui_forms
export GUI_FORMS_SDRSHARP_RUNTIME=/absolute/path/to/reflection-runtime
export GUI_FORMS_SDRSHARP_DIR="$GUI_FORMS_SDRSHARP_RUNTIME/specimen"
export GUI_FORMS_SDRSHARP_PROFILE="$GUI_FORMS_SDRSHARP_RUNTIME/profile"
export GUI_FORMS_SDRSHARP_EXTRACT_DIR="$GUI_FORMS_SDRSHARP_RUNTIME/extract"
export GUI_FORMS_SDRSHARP_NATIVE_DIR="$GUI_FORMS_ROOT/.build/windows-x64"
export GUI_FORMS_MANAGED_OUTPUT="$GUI_FORMS_ROOT/.build/managed"
```

The exact output directory names are not significant. The environment
variables are authoritative.

## Build GUI.Forms

Build the native Windows x64 GUI.Forms target using the repository's current
build instructions for the host toolchain. Stage the four native artifacts
listed above into one directory and point
`GUI_FORMS_SDRSHARP_NATIVE_DIR` at it.

Build the facade and the reflection runner with a native .NET SDK while using
the Windows Desktop reference pack. `GUI_FORMS_FACADE_PROJECT` names the
facade's `System.Windows.Forms.csproj`, which must reference
`GuiForms.Interop` (the sealed ABI's binding):

```sh
export NATIVE_DOTNET=/absolute/path/to/native-dotnet-sdk/dotnet
export GUI_FORMS_WINDOWS_SDK_ROOT=/absolute/path/to/windows-x64-dotnet-sdk
export GUI_FORMS_WINDOWS_DESKTOP_REF=\
"$GUI_FORMS_WINDOWS_SDK_ROOT/packs/Microsoft.WindowsDesktop.App.Ref/10.0.5/ref/net10.0"

mkdir -p \
  "$GUI_FORMS_MANAGED_OUTPUT/facade" \
  "$GUI_FORMS_MANAGED_OUTPUT/reflection-runner"

GUI_FORMS_WINDOWS_DESKTOP_REF="$GUI_FORMS_WINDOWS_DESKTOP_REF" \
  "$NATIVE_DOTNET" build \
  "$GUI_FORMS_FACADE_PROJECT" \
  -c Release -o "$GUI_FORMS_MANAGED_OUTPUT/facade"

GUI_FORMS_WINDOWS_DESKTOP_REF="$GUI_FORMS_WINDOWS_DESKTOP_REF" \
  "$NATIVE_DOTNET" build \
  "$GUI_FORMS_ROOT/sdrsharp_reflection_system/runner/GuiForms.SdrSharpReflectionRunner.csproj" \
  -c Release -o "$GUI_FORMS_MANAGED_OUTPUT/reflection-runner"
```

The first project builds its referenced primitives, drawing and
`GuiForms.Interop` projects into the same output directory; the runner
resolves the facade's own assemblies from that directory.

## Configure Wine and the Windows runtime

Point the launcher at the local Wine installation and Windows x64 .NET runtime:

```sh
export WINE_BINARY=/absolute/path/to/wine
export WINEPATH_BINARY=/absolute/path/to/winepath
export GUI_FORMS_WINDOWS_DOTNET="$GUI_FORMS_WINDOWS_SDK_ROOT/dotnet.exe"
export GUI_FORMS_WINDOWS_CORE_RUNTIME=\
"$GUI_FORMS_WINDOWS_SDK_ROOT/shared/Microsoft.NETCore.App/10.0.5"
export GUI_FORMS_WINDOWS_DESKTOP_RUNTIME=\
"$GUI_FORMS_WINDOWS_SDK_ROOT/shared/Microsoft.WindowsDesktop.App/10.0.5"
```

Calculate the SHA-256 of the supplied `SDRSharp.exe` and explicitly admit that
exact file:

```sh
shasum -a 256 "$GUI_FORMS_SDRSHARP_DIR/SDRSharp.exe"
export GUI_FORMS_REFLECTION_EXPECTED_SHA256=<the-reported-sha256>
```

This pin prevents the runner from silently launching a different application
build.

## Run

Launch from a terminal belonging to the host's interactive graphical session:

```sh
mkdir -p \
  "$GUI_FORMS_SDRSHARP_PROFILE" \
  "$GUI_FORMS_SDRSHARP_EXTRACT_DIR"

/bin/sh "$GUI_FORMS_ROOT/sdrsharp_reflection_system/run_reflected.sh"
```

`run_reflected.sh` validates all required files before starting Wine. It copies
initial profile files from the supplied distribution only when the corresponding
writable profile file does not already exist.

The launcher routes the spectrum and waterfall controls through the GUI.Forms
direct HWND surface lease and leaves ordinary controls on the retained facade
path. Override `GUI_FORMS_DIRECT_HWND_TYPES` only when deliberately testing a
different surface-routing configuration.

Close SDRSharp normally to stop the run. If Wine leaves only the reflection
runner alive after the application window closes, identify that exact runner
process and terminate it; avoid resetting a shared Wine server when unrelated
Wine applications are active.

## Audio

Audio device names are host-specific. Enumerate the devices exposed by the
local Wine installation and pin the intended WASAPI input and output devices in
the private profile. MME was nonfunctional in the reference Wine run, so WASAPI
is the verified configuration; this is an observed compatibility constraint,
not a claim about every Wine installation.

## Expected result

- The main UI, spectrum, waterfall, menus, dialogs, docking panels, and plugin
  controls render through GUI.Forms.
- Plugin windows can be opened, dragged, and closed with their title-bar close
  control.
- The Zoom/Contrast/Range/Offset container and plugin content use the same dark
  application surface as the top toolbar.

The images in `screenshots/` are visual records of a successful reflected run.
They contain no extracted application data and are not runtime dependencies.
