#!/bin/sh
set -eu

repo=${1:-"$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"}
windows_ref=${2:-"$HOME/.wine/drive_c/Program Files/dotnet/packs/Microsoft.WindowsDesktop.App.Ref/10.0.5/ref/net10.0"}
windows_runtime=${3:-"$HOME/.wine/drive_c/Program Files/dotnet/shared/Microsoft.WindowsDesktop.App/10.0.5"}
wine_binary=${WINE_BINARY:-/opt/homebrew/bin/wine}
catalogue=
for candidate in "$repo"/compatibility/*/*.facade-catalogue-v1.json; do
  [ -f "$candidate" ] || continue
  [ -z "$catalogue" ] || {
    echo "multiple private compatibility catalogues found" >&2
    exit 1
  }
  catalogue=$candidate
done
[ -n "$catalogue" ] || {
  echo "private compatibility catalogue not found" >&2
  exit 1
}
generated="$repo/generated/facade-v1"
mac_build="$repo/build-abi-surface"
# The Windows half runs Windows binaries under Wine. They are built natively
# with MSYS2 CLANG64 (the GCC cross toolchain this script used is retired):
# configure with -DGUI_FORMS_ENABLE_SKIA=ON -DGUI_FORMS_ENABLE_WINDOWS_HOST=ON,
# build gui_forms_c_api gui_drawing_c_api gui_drawing_raster_c_api
# gui_forms_win32_compat gui_forms_windows_probe, copy the build directory
# here and name it in GUI_FORMS_WINDOWS_BUILD. Every Windows binary links its
# runtimes statically, so no runtime DLL is staged beside them.
windows_build=${GUI_FORMS_WINDOWS_BUILD:-"$repo/build-abi-windows-skia-smoke"}
smoke_output="$repo/tools/facade_smoke/bin/Release/net10.0"
behavior_output="$repo/tools/facade_behavior_smoke/bin/Release/net10.0"

dotnet run --project "$repo/tools/facade_generator/GuiForms.FacadeGenerator.csproj" -c Release -- \
  "$catalogue" "$windows_ref" "$generated"
dotnet build "$generated/System.Windows.Forms/System.Windows.Forms.csproj" -c Release
dotnet run --project "$repo/tools/facade_verifier/GuiForms.FacadeVerifier.csproj" -c Release -- \
  "$catalogue" "$windows_ref" \
  "$generated/System.Windows.Forms/bin/Release/net10.0/System.Windows.Forms.dll" \
  "$generated/System.Windows.Forms.Primitives/bin/Release/net10.0/System.Windows.Forms.Primitives.dll" \
  "$generated/System.Drawing.Common/bin/Release/net10.0/System.Drawing.Common.dll"

cmake -S "$repo" -B "$mac_build" -DGUI_FORMS_BUILD_GALLERY=OFF \
  -DGUI_FORMS_ENABLE_SKIA=OFF -DGUI_FORMS_ENABLE_MACOS_HOST=OFF \
  -DGUI_FORMS_BUILD_TESTS=ON
cmake --build "$mac_build" --target gui_forms_c_api_c11_tests gui_forms_c_api_cpp_tests --parallel
ctest --test-dir "$mac_build" -R 'gui_forms_c_api_(c11|cpp)_tests' --output-on-failure
dotnet build "$repo/tools/facade_smoke/GuiForms.FacadeSmoke.csproj" -c Release
dotnet build "$repo/tools/facade_behavior_smoke/GuiForms.FacadeBehaviorSmoke.csproj" -c Release
GUI_FORMS_FORCE_HEADLESS=1 GUI_FORMS_AUTOMATION_ACTIVATE=1 \
  DYLD_LIBRARY_PATH="$mac_build" \
  dotnet "$smoke_output/GuiForms.FacadeSmoke.dll"
behavior_host_log=$(mktemp "${TMPDIR:-/tmp}/gui-forms-behavior-host.XXXXXX")
for behavior_mode in form-semantics secondary-form cursor dock-padding control-geometry layout-transactions scroll-panel split-container property-grid lifecycle-order; do
  GUI_FORMS_FORCE_HEADLESS=1 DYLD_LIBRARY_PATH="$mac_build" \
    dotnet "$behavior_output/GuiForms.FacadeBehaviorSmoke.dll" "$behavior_mode" \
    >>"$behavior_host_log"
done
cat "$behavior_host_log"
rg -q 'form-semantics=tab:nested\|accept:ordered\|cancel:ordered\|active-control:scoped\|owner:acyclic\|modal:focus-restored\|label-focus:rejected' "$behavior_host_log"
rg -q 'secondary-form=attached:true\|owned:true\|clamped:true\|reopened:true\|cancelled:true\|detached:true\|focus-restored:true\|loads:1\|closed:1' "$behavior_host_log"
rg -q 'cursor=identity:stable\|projection:roundtrip\|inherit:restored' "$behavior_host_log"
rg -q 'dock-padding=projection:owned\|fill:inset\|relayout:updated' "$behavior_host_log"
rg -q 'control-geometry=zorder:coherent\|lookup:filtered\|bounds:masked\|coordinates:roundtrip\|tab-order:nested\|autosize:shrink\|dock:zorder' "$behavior_host_log"
rg -q 'layout-transactions=nested:coalesced\|committed:stable\|args:exact\|reentry:bounded\|resume-false:deferred\|unmatched:no-op\|fault:recoverable' "$behavior_host_log"
rg -q 'scroll-panel=retained:true\|paint-composed:true\|step:48\|reached:true\|reverse:true\|into-view:true\|manual-axis:true\|event-args:true\|event-delivery:true' "$behavior_host_log"
rg -q 'split-container=geometry:constrained\|drag:live\|collapse:focus-transferred\|orientation:horizontal\|fixed:enforced' "$behavior_host_log"
rg -q 'property-grid=native:true\|multi:true\|clone:true\|sort:true\|selection-events:3\|sort-events:1\|type-descriptor:true\|proxy-schema-clean:true\|dropdown-editor:true\|modal-editor:true\|converted-commit:true\|nullable-commit:true\|reset:true\|atomic-rollback:true\|arbitrary-rejected:true\|retained-after-failure:true\|cleared:true' "$behavior_host_log"
rg -q 'lifecycle-order=load>input>closing>closed' "$behavior_host_log"
unlink "$behavior_host_log"

for windows_binary in gui_forms_abi0.dll gui_forms_win32_compat.dll gui_drawing_abi0.dll gui_drawing_raster0.dll gui_forms_windows_probe.exe; do
  [ -f "$windows_build/$windows_binary" ] || {
    echo "$windows_build/$windows_binary missing: build the Windows binaries with CLANG64 (see GUI_FORMS_WINDOWS_BUILD above)" >&2
    exit 1
  }
done
cp "$windows_build/gui_forms_abi0.dll" "$smoke_output/"
cp "$windows_build/gui_forms_abi0.dll" "$behavior_output/"
cp "$windows_build/gui_forms_win32_compat.dll" "$smoke_output/"
cp "$windows_build/gui_forms_win32_compat.dll" "$behavior_output/"
cp "$windows_build/gui_drawing_abi0.dll" "$smoke_output/"
cp "$windows_build/gui_drawing_abi0.dll" "$behavior_output/"
cp "$windows_build/gui_drawing_raster0.dll" "$smoke_output/"
cp "$windows_build/gui_drawing_raster0.dll" "$behavior_output/"
mkdir -p "$smoke_output/fonts" "$behavior_output/fonts"
cp "$windows_build/fonts/PortsmouthRapids.ttf" "$smoke_output/fonts/"
cp "$windows_build/fonts/PortsmouthRapids-Bold.ttf" "$smoke_output/fonts/"
cp "$windows_build/fonts/PortsmouthRapids.ttf" "$behavior_output/fonts/"
cp "$windows_build/fonts/PortsmouthRapids-Bold.ttf" "$behavior_output/fonts/"
for dependency in Microsoft.Win32.SystemEvents.dll System.Formats.Nrbf.dll System.Private.Windows.Core.dll System.Private.Windows.GdiPlus.dll; do
  cp "$windows_runtime/$dependency" "$smoke_output/"
  cp "$windows_runtime/$dependency" "$behavior_output/"
done
wine_log=$(mktemp "${TMPDIR:-/tmp}/gui-forms-facade-wine.XXXXXX")
(cd "$smoke_output" && GUI_FORMS_AUTOMATION=1 WINEDEBUG=-all \
  "$wine_binary" 'C:\Program Files\dotnet\dotnet.exe' GuiForms.FacadeSmoke.dll \
  >"$wine_log" 2>&1) &
wine_pid=$!
attempt=0
until GUI_FORMS_AUTOMATION_TITLE='GUI.Forms Radio Console' WINEDEBUG=-all \
  "$wine_binary" "$windows_build/gui_forms_windows_probe.exe" snapshot \
    >/dev/null 2>&1; do
  attempt=$((attempt + 1))
  if [ "$attempt" -ge 80 ]; then
    kill "$wine_pid" 2>/dev/null || true
    wait "$wine_pid" 2>/dev/null || true
    echo "managed Wine surface did not publish its automation window" >&2
    exit 1
  fi
  sleep 0.25
done
GUI_FORMS_AUTOMATION_TITLE='GUI.Forms Radio Console' WINEDEBUG=-all \
  "$wine_binary" "$windows_build/gui_forms_windows_probe.exe" \
  click startButton >/dev/null
wait "$wine_pid"
cat "$wine_log"
rg -q 'callbacks=click:1\|dispatch:1\|closing:1\|closed:1\|faults:1' "$wine_log"
unlink "$wine_log"

behavior_wine_log=$(mktemp "${TMPDIR:-/tmp}/gui-forms-behavior-wine.XXXXXX")
for behavior_mode in form-semantics secondary-form cursor dock-padding control-geometry layout-transactions scroll-panel split-container property-grid lifecycle-order; do
  (cd "$behavior_output" && GUI_FORMS_FORCE_HEADLESS=1 WINEDEBUG=-all \
    "$wine_binary" 'C:\Program Files\dotnet\dotnet.exe' \
    GuiForms.FacadeBehaviorSmoke.dll "$behavior_mode") >>"$behavior_wine_log" 2>&1
done
cat "$behavior_wine_log"
rg -q 'form-semantics=tab:nested\|accept:ordered\|cancel:ordered\|active-control:scoped\|owner:acyclic\|modal:focus-restored\|label-focus:rejected' "$behavior_wine_log"
rg -q 'secondary-form=attached:true\|owned:true\|clamped:true\|reopened:true\|cancelled:true\|detached:true\|focus-restored:true\|loads:1\|closed:1' "$behavior_wine_log"
rg -q 'cursor=identity:stable\|projection:roundtrip\|inherit:restored' "$behavior_wine_log"
rg -q 'dock-padding=projection:owned\|fill:inset\|relayout:updated' "$behavior_wine_log"
rg -q 'control-geometry=zorder:coherent\|lookup:filtered\|bounds:masked\|coordinates:roundtrip\|tab-order:nested\|autosize:shrink\|dock:zorder' "$behavior_wine_log"
rg -q 'layout-transactions=nested:coalesced\|committed:stable\|args:exact\|reentry:bounded\|resume-false:deferred\|unmatched:no-op\|fault:recoverable' "$behavior_wine_log"
rg -q 'scroll-panel=retained:true\|paint-composed:true\|step:48\|reached:true\|reverse:true\|into-view:true\|manual-axis:true\|event-args:true\|event-delivery:true' "$behavior_wine_log"
rg -q 'split-container=geometry:constrained\|drag:live\|collapse:focus-transferred\|orientation:horizontal\|fixed:enforced' "$behavior_wine_log"
rg -q 'property-grid=native:true\|multi:true\|clone:true\|sort:true\|selection-events:3\|sort-events:1\|type-descriptor:true\|proxy-schema-clean:true\|dropdown-editor:true\|modal-editor:true\|converted-commit:true\|nullable-commit:true\|reset:true\|atomic-rollback:true\|arbitrary-rejected:true\|retained-after-failure:true\|cleared:true' "$behavior_wine_log"
rg -q 'lifecycle-order=load>input>closing>closed\|handle-created:1\|handle-destroyed:1\|early-close:suppressed' "$behavior_wine_log"
unlink "$behavior_wine_log"

scroll_live_log=$(mktemp "${TMPDIR:-/tmp}/gui-forms-scroll-live.XXXXXX")
(cd "$behavior_output" && GUI_FORMS_AUTOMATION=1 MVK_CONFIG_LOG_LEVEL=0 WINEDEBUG=-all \
  "$wine_binary" 'C:\Program Files\dotnet\dotnet.exe' \
  GuiForms.FacadeBehaviorSmoke.dll scroll-panel-live >"$scroll_live_log" 2>&1) &
scroll_live_pid=$!
attempt=0
until GUI_FORMS_AUTOMATION_TITLE='GUI.Forms Scroll Event' WINEDEBUG=-all \
  "$wine_binary" "$windows_build/gui_forms_windows_probe.exe" snapshot \
    >/dev/null 2>&1; do
  attempt=$((attempt + 1))
  if [ "$attempt" -ge 80 ]; then
    kill "$scroll_live_pid" 2>/dev/null || true
    wait "$scroll_live_pid" 2>/dev/null || true
    echo "managed scroll event surface did not publish its automation window" >&2
    exit 1
  fi
  sleep 0.25
done
GUI_FORMS_AUTOMATION_TITLE='GUI.Forms Scroll Event' WINEDEBUG=-all \
  "$wine_binary" "$windows_build/gui_forms_windows_probe.exe" \
  click-at scrollViewport 232 112 >/dev/null
GUI_FORMS_AUTOMATION_TITLE='GUI.Forms Scroll Event' WINEDEBUG=-all \
  "$wine_binary" "$windows_build/gui_forms_windows_probe.exe" close >/dev/null
wait "$scroll_live_pid"
cat "$scroll_live_log"
rg -q 'scroll-panel-live=events:1\|type:SmallIncrement\|orientation:vertical\|position:12' "$scroll_live_log"
unlink "$scroll_live_log"

paint_lease_wine_log=$(mktemp "${TMPDIR:-/tmp}/gui-forms-paint-lease-wine.XXXXXX")
for behavior_mode in native-surface native-surface-lifecycle native-surface-fallback paint-reentry paint-input-deferral managed-double-buffer managed-damage theme-inherited-paint theme-restore-transaction; do
  (cd "$behavior_output" && MVK_CONFIG_LOG_LEVEL=0 WINEDEBUG=-all \
    "$wine_binary" 'C:\Program Files\dotnet\dotnet.exe' \
    GuiForms.FacadeBehaviorSmoke.dll "$behavior_mode") >>"$paint_lease_wine_log" 2>&1
done
cat "$paint_lease_wine_log"
rg -q 'native-surface=handle:virtual\|input:retained-host\|size:96x48\|gdi:true\|producer:uncoupled\|newest-frame:true\|display-clock:true\|disposed:true' "$paint_lease_wine_log"
rg -q 'native-surface-lifecycle=construction-hdc:retained\|writer:background\|producer:uncoupled\|compositor:continuous\|resize:durable\|hide-show:latest\|occlusion:latest\|echo:none\|reentry:none\|secondary-window:none\|disposed:true' "$paint_lease_wine_log"
rg -q 'native-surface-fallback=raw-gdi:true\|shim-boundary:true\|newest-frame:true\|disposed:true' "$paint_lease_wine_log"
rg -q 'theme-inherited-paint=plugin:true\|callback:atomic\|frame:one\|colors:current\|faults:zero' "$paint_lease_wine_log"
rg -q 'theme-restore-transaction=restore:queued\|descendants:coalesced\|callback:returned\|faults:zero' "$paint_lease_wine_log"
rg -q 'paint-reentry=recursive:false\|follow-up:one\|callback-boundary:true' "$paint_lease_wine_log"
rg -q 'paint-input-deferral=order:pointer>key\|during-paint:false\|follow-up:one\|disposed:abandoned\|queue:zero' "$paint_lease_wine_log"
rg -q 'managed-double-buffer=reflected:true\|phases:shared\|surface:reused\|burst:coalesced\|resize:stale-abandoned\|follow-up:one\|disabled:ephemeral\|fault:no-self-retry' "$paint_lease_wine_log"
rg -q 'managed-damage=rect:merged\|notify:signal-only\|event:clipped\|region:bounded\|children:translated\|fault:restored\|unbuffered:full' "$paint_lease_wine_log"
unlink "$paint_lease_wine_log"

echo "Generated facade surface and current experimental-ABI managed-loop smoke passed on host .NET and Wine."
