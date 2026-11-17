#!/bin/sh
# Runs one D15 stage over one configuration's compile_commands.json
# (README.md, "Running a stage"). Every argument after the first two goes to
# the tool, for example --mode plan --plan-output build/d15/v1-macos.plan.
set -eu

if [ "$#" -lt 2 ]; then
    echo "usage: run_stage.sh <build-directory-with-compile_commands.json> <V1|V2|V3|V4|V5> [tool arguments...]" >&2
    exit 2
fi

tool_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
gui_forms_root=$(CDPATH= cd -- "$tool_directory/../.." && pwd)
build_directory=$1
stage=$2
shift 2

llvm_prefix=${GUI_FORMS_D15_LLVM:-/opt/homebrew/opt/llvm@22}
tool=${GUI_FORMS_D15_TOOL:-$gui_forms_root/.build/d15-rewrite/gui_forms_d15_rewrite}
resource_directory=$("$llvm_prefix/bin/clang" -print-resource-dir)

if [ ! -x "$tool" ]; then
    echo "run_stage.sh: build the tool first ($tool)" >&2
    exit 2
fi
if [ ! -f "$build_directory/compile_commands.json" ]; then
    echo "run_stage.sh: $build_directory has no compile_commands.json" >&2
    exit 2
fi

case $(uname -s) in
    Darwin)
        sdk_root=$(/usr/bin/xcrun --show-sdk-path)
        exec "$tool" -p "$build_directory" --source-root "$gui_forms_root" --stage "$stage" \
            "--extra-arg=-resource-dir=$resource_directory" "--extra-arg=-isysroot$sdk_root" "$@"
        ;;
    *)
        exec "$tool" -p "$build_directory" --source-root "$gui_forms_root" --stage "$stage" \
            "--extra-arg=-resource-dir=$resource_directory" "$@"
        ;;
esac
