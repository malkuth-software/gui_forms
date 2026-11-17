#!/bin/sh
# Configures (once) and builds the lab inside the build lock. Run from
# PowerShell through msys2.cmd with MSYSTEM=CLANG64 (see the Windows worker
# brief); the log goes to D:/gw_present_lab/build.log.
set -eu
cd /d/gw_present_lab
if [ ! -f build/build.ninja ]; then
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang++
fi
cmake --build build
