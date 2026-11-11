#!/bin/sh
# Builds a lab program against the built libraries of D:\gui_forms_h (read
# only), with that tree's headers. usage: link_h.sh <name>
set -e
export PATH=/d/a/_temp/msys64/clang64/bin:$PATH
H=D:/gui_forms_h/gui_forms
B=D:/gui_forms_h/.build/native-windows-x64/gui-forms
S=D:/gui_forms_w3/.build/be-sdk/gui-forms/stx-package
name=$1
shift
clang++ -O3 -DNDEBUG -std=c++20 -ffp-contract=off -pthread -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 \
    -I$H/include -I$H/src/render/skia/raster -I$H/tests -I$H/src -isystem $S/stx/include \
    $name.cpp "$@" -static -o $name.exe \
    $B/libgui_forms_controls.a $B/libgui_forms_skia.a $B/libgui_drawing_core.a $B/libgui_forms_plot_primitives.a \
    $S/build/lib/libstx_skia.a $B/libgui_forms_text_engine.a $B/libgui_forms_core.a \
    $S/build/lib/libstx_skia_swizzle.a $B/libgui_forms_threading.dll.a \
    $S/stx/lib/libstx_png.a $S/stx/lib/libstx_zlib.a $S/stx/lib/libstx_conv.a $S/stx/lib/libstx_rtf2md.a \
    -lwinpthread $S/build/lib/libharfbuzz.a $S/build/lib/libfreetype.a \
    $S/build/lib/libstx_text_sheenbidi.a $S/build/lib/libstx_text_linebreak.a \
    -lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -lbcrypt
