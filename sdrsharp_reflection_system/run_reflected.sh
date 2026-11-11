#!/bin/sh
set -eu

system_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
gui_forms_root=${GUI_FORMS_ROOT:-"$(CDPATH= cd -- "$system_root/.." && pwd)"}
runtime=${GUI_FORMS_SDRSHARP_RUNTIME:-"$HOME/Developer/CodexRuns/gui-forms-sdrsharp"}
application_dir=${GUI_FORMS_SDRSHARP_DIR:-"$runtime/specimen"}
profile_dir=${GUI_FORMS_SDRSHARP_PROFILE:-"$runtime/profile"}
extract_dir=${GUI_FORMS_SDRSHARP_EXTRACT_DIR:-"$runtime/extract"}
native_dir=${GUI_FORMS_SDRSHARP_NATIVE_DIR:-"$gui_forms_root/.build/windows-x64-skia-make"}
windows_sdk=${GUI_FORMS_WINDOWS_SDK_ROOT:-"$HOME/.local/share/dotnet-win-x64-sdk-10.0.105"}
wine_binary=${WINE_BINARY:-"/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine"}
winepath_binary=${WINEPATH_BINARY:-"/Applications/Wine Devel.app/Contents/Resources/wine/bin/winepath"}
windows_dotnet=${GUI_FORMS_WINDOWS_DOTNET:-"$windows_sdk/dotnet.exe"}
core_runtime=${GUI_FORMS_WINDOWS_CORE_RUNTIME:-"$windows_sdk/shared/Microsoft.NETCore.App/10.0.5"}
desktop_runtime=${GUI_FORMS_WINDOWS_DESKTOP_RUNTIME:-"$windows_sdk/shared/Microsoft.WindowsDesktop.App/10.0.5"}
managed=${GUI_FORMS_MANAGED_OUTPUT:-"$gui_forms_root/.build/managed"}
runner=${GUI_FORMS_RUNNER:-"$managed/reflection-runner/GuiForms.SdrSharpReflectionRunner.dll"}
forms=${GUI_FORMS_FORMS_FACADE:-"$managed/facade/System.Windows.Forms.dll"}
primitives=${GUI_FORMS_PRIMITIVES_FACADE:-"$managed/facade/System.Windows.Forms.Primitives.dll"}
drawing=${GUI_FORMS_DRAWING_FACADE:-"$managed/facade/System.Drawing.Common.dll"}
bundle="$application_dir/SDRSharp.exe"

require_file() { [ -f "$1" ] || { echo "missing required file: $1" >&2; exit 1; }; }
require_dir() { [ -d "$1" ] || { echo "missing required directory: $1" >&2; exit 1; }; }
to_windows_path() { "$winepath_binary" -w "$1"; }

for file in "$bundle" "$wine_binary" "$winepath_binary" "$windows_dotnet" \
            "$runner" "$forms" "$primitives" "$drawing" \
            "$native_dir/gui_forms_abi1.dll" "$native_dir/gui_drawing_abi0.dll" \
            "$native_dir/gui_drawing_raster0.dll" "$native_dir/fonts/PortsmouthRapids.ttf"; do
    require_file "$file"
done
require_dir "$core_runtime"
require_dir "$desktop_runtime"

mkdir -p "$profile_dir" "$extract_dir"
for profile_file in SDRSharp.config SDRSharp.Layout.xml BandPlan.xml notches.xml spybrowser.history; do
    if [ ! -f "$profile_dir/$profile_file" ] && [ -f "$application_dir/$profile_file" ]; then
        cp "$application_dir/$profile_file" "$profile_dir/$profile_file"
    fi
done

GUI_FORMS_RUN_WORKING_DIRECTORY="$(to_windows_path "$profile_dir")" \
GUI_FORMS_REFLECTION_EXPECTED_SHA256="${GUI_FORMS_REFLECTION_EXPECTED_SHA256:-6859b1658245acd0416d47b1ebb2bfb72fb24bf34f7674f73502c1a9093a53b5}" \
GUI_FORMS_DIRECT_HWND_TYPES="${GUI_FORMS_DIRECT_HWND_TYPES-SDRSharp.PanView.SpectrumAnalyzer;SDRSharp.PanView.Waterfall}" \
WINEDEBUG="${WINEDEBUG:--all}" \
exec "$wine_binary" "$windows_dotnet" \
    "$(to_windows_path "$runner")" \
    "$(to_windows_path "$bundle")" \
    "$(to_windows_path "$extract_dir")" \
    "$(to_windows_path "$forms")" \
    "$(to_windows_path "$primitives")" \
    "$(to_windows_path "$drawing")" \
    "$(to_windows_path "$core_runtime")" \
    "$(to_windows_path "$desktop_runtime")" \
    "$(to_windows_path "$native_dir")" \
    "$(to_windows_path "$application_dir")"
