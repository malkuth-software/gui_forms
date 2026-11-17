# GUI.Forms Linux validation VM

A Lima definition for building and graphically testing GUI.Forms on Linux:
Alpine 3.24.2 aarch64 (musl libc), Apple Virtualization framework, 4 CPUs,
4 GiB memory, 24 GiB sparse disk, headless X11 (Xvfb) and Wayland (Weston with
XWayland).

## Provenance

The previous VMs were lost: the Lima instance `gui-forms-linux` (Ubuntu 24.04,
4 CPUs, 4 GiB, read-only project mount; its directory was deleted before
2026-10-01) and the QEMU Alpine 3.24 VM `paint-builder` from
`~/Developer/Projects/rainstar-linux-lab` on the M4 (disk image gone). This
definition keeps `gui-forms-linux`'s name and size and `paint-builder`'s
Alpine/musl guest and package set, recovered from the session records that
created them. It is kept in the repository so it cannot be lost again.

## Use (on the M4)

```sh
limactl create --name=gui-forms-linux gui_forms/tools/linux_vm/gui-forms-linux.yaml
limactl start gui-forms-linux
limactl shell gui-forms-linux
```

`~/Developer/CodexBuilds` is mounted read-only at `/mnt/codexbuilds`. Copy or
build from there into the guest's home directory; never write the mount.

Clang 22 lives in `/usr/lib/llvm22/bin`:

```sh
export PATH=/usr/lib/llvm22/bin:$PATH CC=clang CXX=clang++
```

The guest builds against libc++ 22, as every Linux build does since
2026-11-15, and links libc++, libc++abi and libunwind statically from the
harness's own runtime, not Alpine's archives (Alpine's `libc++abi.a` is not
position independent, so `gui_forms_abi` could not carry it). Build the
runtime once in the guest with Alpine's clang, the bootstrap compiler, and
export its prefix before configuring:

```sh
runtime="$PWD/.build/toolchain/$(python3 template_harness/harness/linux_runtime_manifest.py --directory-name)"
python3 template_harness/harness/build_linux_runtimes.py --work "$PWD/.build/llvm-runtimes" --prefix "$runtime" --jobs 4
export GUI_FORMS_LLVM_RUNTIME="$runtime"
```

`cmake/llvm22.cmake` then compiles against the runtime's headers, adds
`-stdlib=libc++ -nostdlib++`, the three archives at the end of each C++ link
and, on musl, `-rtlib=compiler-rt -unwindlib=none`. A build tree configured
against another C++ library keeps working: the toolchain file adds the flags
at its next configure, and everything recompiles.

Graphical tests run against a headless server, for example:

```sh
Xvfb :2 -screen 0 1280x800x24 -nolisten tcp &
DISPLAY=:2 dwm &
DISPLAY=:2 ctest --test-dir ~/build/gui-forms --output-on-failure
DISPLAY=:2 xwd -root -silent > ~/results/screen.xwd
```

Weston provides Wayland with XWayland when a test needs it:

```sh
XDG_RUNTIME_DIR=/tmp/gf-wayland weston --backend=headless --renderer=pixman \
  --width=1280 --height=800 --xwayland --no-config --socket=gf-wayland &
```

This guest tests aarch64 musl only. It does not establish glibc or x86-64
behaviour; CI's Ubuntu jobs cover those.
