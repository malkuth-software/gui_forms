#!/bin/sh
set -eu

gui_forms_root=${1:-"$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"}
dotnet_root=${GUI_FORMS_DOTNET_ROOT:-"$HOME/.local/share/dotnet-sdk-10.0.105"}
windows_sdk_root=${GUI_FORMS_WINDOWS_SDK_ROOT:-"$HOME/.local/share/dotnet-win-x64-sdk-10.0.105"}
windows_ref=${GUI_FORMS_WINDOWS_DESKTOP_REF:-"$windows_sdk_root/packs/Microsoft.WindowsDesktop.App.Ref/10.0.5/ref/net10.0"}
output_root=${GUI_FORMS_MANAGED_OUTPUT:-"$gui_forms_root/.build/managed"}
# The Windows Forms facade over the sealed C ABI (GuiForms.Interop); it is not
# in this repository (see README.md).
facade_project=${GUI_FORMS_FACADE_PROJECT:-}

dotnet="$dotnet_root/dotnet"
[ -x "$dotnet" ] || { echo "missing native .NET SDK host: $dotnet" >&2; exit 1; }
[ -d "$windows_ref" ] || { echo "missing Windows Desktop reference pack: $windows_ref" >&2; exit 1; }
[ -f "$facade_project" ] || { echo "set GUI_FORMS_FACADE_PROJECT to the facade's System.Windows.Forms.csproj" >&2; exit 1; }

mkdir -p "$output_root/facade" "$output_root/reflection-runner"
GUI_FORMS_WINDOWS_DESKTOP_REF="$windows_ref" \
  "$dotnet" build \
  "$facade_project" \
  -c Release -o "$output_root/facade"
GUI_FORMS_WINDOWS_DESKTOP_REF="$windows_ref" \
  "$dotnet" build \
  "$gui_forms_root/sdrsharp_reflection_system/runner/GuiForms.SdrSharpReflectionRunner.csproj" \
  -c Release -o "$output_root/reflection-runner"
