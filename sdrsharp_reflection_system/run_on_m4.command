#!/bin/sh
set -eu

runtime=${GUI_FORMS_SDRSHARP_RUNTIME:-"$HOME/Developer/CodexRuns/gui-forms-sdrsharp"}
gui_forms_root=${GUI_FORMS_ROOT:-"$HOME/Developer/work/codex-builds/file_manager-2d80cdb86b7d/gui_forms"}

GUI_FORMS_ROOT="$gui_forms_root" \
GUI_FORMS_SDRSHARP_RUNTIME="$runtime" \
exec /bin/sh "$gui_forms_root/sdrsharp_reflection_system/run_reflected.sh" \
  >"$runtime/reflection-launch.log" 2>&1
