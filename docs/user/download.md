# Download

GUI.Forms ships three demonstration programs so you can see the toolkit
before you build it. Every push to `main` that passes the build and a launch
of each packaged program is published as a release on the project's GitHub
Releases page, tagged `vX.Y.Z`, where X.Y is the package version (now
{{ gf_version }}). If no `v` release is listed yet, build the programs
yourself; see [Build it yourself instead](#build-it-yourself-instead).

The `build-<sha>` pre-releases on the same page are compiler-cache archives
used by CI and by downstream projects. They are not application downloads.

## The three programs

GUI.Forms Gallery
: A catalogue of the stock controls on pages grouped by function, from
  basic controls and value editors to collections, layout panels, dialogs,
  live surfaces and typography. Each page uses only public controls, and a
  status line reports what the last interaction did.

GUI.Forms Explore (preview)
: A file-explorer interface built from `file_manager_demoboard/` over
  deterministic in-memory fixtures. It never reads the real filesystem. It
  exercises panels, trees, details views, breadcrumbs, menus and dialogs in
  one composed window. The preview label is literal: the demoboard's own
  status file lists which interaction gates are complete.

GUI.Forms Correspondence
: A mail-client mockup over in-memory fixtures: a folder tree, a message
  list, a reading pane, a menu strip, a command bar, tooltips, context menus
  and host dialogs. It composes `SplitContainer`, `TreeView`,
  `CorrespondenceView`, `MenuStrip` and `CommandBar` into a three-pane
  layout. It is a demonstration, not a mail client.

All three are built from `demo/` and `file_manager_demoboard/` using only the
public headers, so anything they do is available to your application.

## What a release contains

Each program is published for four platforms:

| Platform | Archive | Installer |
|---|---|---|
| macOS arm64 | `.zip` holding the `.app` bundle | `.pkg` |
| Windows x64 | `.zip` holding the program folder | `-setup.exe`, a per-user installer |
| Linux x64, Linux arm64 | `.tar.gz` holding the program folder | `.deb` |

File names follow `gui-forms-<program>-<version>-<platform>`, for example
`gui-forms-gallery-<version>-windows-x64.zip`. Each release also carries
`SHA256SUMS.txt`, the packaging and smoke receipts the pipeline produced, and
the source SDK, `gui-forms-sdk-<version>-source.tar.gz`, from which you build
GUI.Forms for your own platform (see
[The source SDK](install.md#the-source-sdk)).

## Platform notes

macOS
: Apple silicon (arm64) only, macOS 14.0 or newer. Builds are **ad hoc
  signed** (`codesign --sign -`) and **not notarized**. Gatekeeper refuses the
  first launch; open the program from the Finder context menu with "Open", or
  remove the quarantine attribute with
  `xattr -dr com.apple.quarantine "GUI.Forms Gallery.app"`.

Windows
: Windows 10 or newer, x64. Builds are **unsigned**. SmartScreen warns that
  the publisher is unknown; choose "More info" and then "Run anyway" if you
  decide to trust the download. The `fonts/` folder holds the fonts'
  licences; the faces themselves are compiled into the program.

Linux
: Ubuntu 24.04 on x64 and arm64 is the tested target. The programs need an
  **X11** display; Wayland sessions run them through XWayland. The `.deb`
  declares its library dependencies; the `.tar.gz` needs the X11, Xcursor and
  ATK runtime libraries installed. No Flatpak, Snap or AppImage is provided.

Every program can prove itself from a terminal. `--smoke-test` presents one
frame, closes itself, prints the close receipt and the line
`GUI.Forms window smoke passed`, and exits with status 0. This is the check
the release pipeline runs on every package. If no frame is presented within
ten seconds, for example because the window stayed hidden behind other
windows or on a Space that is not shown, the program still closes but prints
`GUI.Forms window smoke failed: no frame was presented` and exits with status 1.

## Verify a download

Check the archive against `SHA256SUMS.txt` before extracting it:

```sh
shasum -a 256 -c SHA256SUMS.txt --ignore-missing
```

On Windows:

```bat
certutil -hashfile gui-forms-gallery-<version>-windows-x64.zip SHA256
```

## Build it yourself instead

[Building from source](../building/index) produces the Gallery and Correspondence
on a machine with the LLVM 22 toolchain;
`-DGUI_FORMS_BUILD_FILE_MANAGER_DEMOBOARD=ON` adds Explore.
