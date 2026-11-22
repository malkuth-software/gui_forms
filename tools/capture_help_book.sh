#!/bin/sh
# The help-book page captures (GAPS H5): the Gallery's headless capture
# (`--page help-book --capture <png> --headless`, demo/gallery_headless_capture.hpp)
# in the light and dark schemes at scales 1 and 2, written to
# <output>/help_book_<scheme>_<scale>x.png (default .build/help-book-captures, outside the documentation).
#
#   tools/capture_help_book.sh <gallery executable> [output directory]
#
# Run it on the build host after a build (on macOS the executable inside
# the app bundle, Contents/MacOS/). No host window opens. It stops at the
# first capture that fails; the Gallery prints the reason to stderr.
set -eu

gallery=${1:?usage: tools/capture_help_book.sh <gallery executable> [output directory]}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output=${2:-"$root/.build/help-book-captures"}
mkdir -p "$output"
test -x "$gallery"

for scheme in light dark; do
    for scale in 1 2; do
        "$gallery" --page help-book --headless --size 900x620 --scale "$scale" \
            --scheme "$scheme" --capture "$output/help_book_${scheme}_${scale}x.png"
    done
done
