# Audio

`GUIForms::Audio` is an optional component for playing short sounds and music
in a GUI.Forms application. It loads stereo clips, mixes up to 64 live
voices and writes the mix to the platform's audio device. Ogg Vorbis files
are decoded by stx_vorbis, an Ogg Vorbis I decoder that lives in the
stx repository (`malkuth-software/stx`) under
`stx_vorbis/`, at the commit `stx.lock` pins. This page describes what the component
offers, why it has its own decoder, and how far that decoder has been
checked.

## What the component offers

Build it with `-DGUI_FORMS_BUILD_AUDIO=ON` and link the installed component:

```cmake
find_package(GUIForms CONFIG REQUIRED COMPONENTS Audio)
target_link_libraries(my_app PRIVATE GUIForms::Audio)
```

`GUIForms::Audio` is a static library that links `GUIForms::Threading`,
`GUIForms::Memory` and nothing from the retained engine, so an adapter library can use it beside
`GUIForms::Application` (see [Consuming the SDK](consume-the-sdk.md)). The
public header is `<gui_forms/audio/audio.hpp>`; every type is listed in the
[Audio reference](../api/library.audio.rst).

`AudioClip`
: An immutable buffer of interleaved stereo float samples at 48 kHz, at most
  600 seconds long. All live clips together are limited to 512 MiB.
  `load_ogg` reads a single-stream stereo 48 kHz Ogg Vorbis file of up to
  64 MiB, `load_wav` reads 16-bit or float stereo 48 kHz RIFF, and `copy`
  wraps samples you already hold. A failed load publishes no partial clip
  and reports an `AudioStatus`.

`AudioEngine` and `AudioVoice`
: The engine opens the device (or renders offline, for tests) and creates
  voices from clips. A voice plays, pauses, stops, loops, takes a gain in
  [0, 1] and, when created with pitch enabled, a rate in [0.25, 4] using
  linear interpolation. Fixed-rate voices keep exact sample positions.

`AudioGenerator`
: A live source the mixer pulls from on the device callback, for synthesized
  sound. Its `render` must fill the span with finite samples and must not
  block, allocate, lock or throw.

The device backend is miniaudio, pinned by commit and checked by hash, and
compiled for Core Audio on macOS, WASAPI on Windows and ALSA on Linux. Files
are decoded into memory, not streamed.

## Threads

All engine and voice calls belong to one control thread; the device callback
is private. `load_ogg` is synchronous file I/O and decoding, so call it on a
worker, never on the UI thread or the device callback. It accepts a
`CancellationFlag` and checks it between reads, pages and decode blocks of at
most 4,096 frames. [Threading](threading.md) shows the worker pattern, and
`examples/reference/audio_worker.cpp` is a complete adapter that decodes on a
worker and joins before reading the clip.

## Choosing the decoder

| Option | Values | Default |
|---|---|---|
| `GUI_FORMS_AUDIO_VORBIS_BACKEND` | `stx`, `stb` | `stx` |

`stx` links the stx_vorbis decoder through its stb-compatible C adapter.
`stb` compiles the stb_vorbis 1.22 file shipped with the pinned miniaudio
source instead. Only the selected decoder is linked, the public API is the
same for both, and the SDK that CI builds and installs uses `stx`. An existing
build directory keeps the value it was configured with: pass the option
explicitly when changing an older cache, then rebuild and reinstall. Other
build options are listed in [Building from source](../building/index.md).

## Why GUI.Forms has its own decoder

The component first decoded Ogg with stb_vorbis. Comparing stb_vorbis sample
by sample against libvorbis, the Xiph reference decoder, recorded these
problems:

- **Wrong six-channel output.** On a generated ten-second six-channel file
  (480,017 frames at 48 kHz, encoded by libvorbis), stb returns the right
  number of samples, but 2,880,065 of its 2,880,102 values differ from
  libvorbis by more than the acceptance bound, by up to 27.0. The same
  failure appears on both an Apple M4 Mac mini and a Windows x64 machine.
- **Large errors on single-entry codebooks.** Generated stress files built
  around single-entry codebooks show discrepancies of up to about 27.1.
- **No floor 0.** stb rejects floor type 0, which Vorbis I defines and some
  historical files use.
- **Undefined pointer arithmetic when seeking.** `set_file_offset` forms
  `stream_start + loc` before checking `loc`, then tests for wraparound with
  a comparison the compiler is allowed to delete. An `stb` build compiles a
  one-line corrected copy, shown in `cmake/Audio.cmake`.
- **Allocation outside the supplied arena.** stb sorts with the C library's
  `qsort`, which some C libraries implement with a temporary heap buffer. An
  `stb` build replaces it with an in-place heap sort.

The causes of the first two have not been diagnosed. `load_ogg` accepts only
stereo files, and the stereo test cases agree with libvorbis, so the
multichannel errors do not reach it; they showed that matching sample counts
are not evidence of correct output. stx_vorbis was written so the decoder
follows the specification and is checked against the reference in CI.

## How close and how fast

stx_vorbis is an independent C++20 implementation of the Vorbis I and Ogg
framing specifications. It does not call or include stb_vorbis, libvorbis or
Tremor; those are used only as test oracles.

Accuracy
: Every sample is compared with libvorbis's float output against the bound
  `2e-6 + 2e-6 * |reference|`, with exact sample counts. On the two ten-second
  benchmark files the largest difference is 2.4e-7 (stereo) and 2.7e-7
  (six-channel). Over generated files covering 8 to 96 kHz, 1, 2, 6 and 8
  channels, every floor and residue combination, start trimming and a
  14-link chain with format changes, the largest is 2.4e-7 for ordinary
  audio and 1.9e-6 for synthetic high-amplitude stress input.

Speed
: Its inverse MDCT uses the BODFT transform from the BFFT library, compiled
  privately into the decoder. On a Windows 11 x64 virtual machine (AMD EPYC
  9354 guest, Clang 22.1.8), decoding each ten-second file from memory, 21
  runs after three warm-ups, the median total time is 14.2 ms stereo and
  37.6 ms six-channel, against 17.0 ms and 43.4 ms for libvorbis on the same
  machine. That is one machine and two synthetic files, not a general speed
  result. No timing of this transform on ARM has been recorded; an earlier
  arm64 measurement, before it was adopted, had stx 24 % (stereo) and 35 %
  (six-channel) slower than libvorbis. stb_vorbis is faster than stx on the
  stereo file: 6.0 ms against 13.4 ms for stx on the Apple M4 Mac mini
  before the transform change, and 10.5 ms on the Windows machine in an
  earlier session.

The decoder bounds its memory (128 MiB by default) and per-packet work,
reports its exact allocations, and returns status values instead of throwing
once constructed.

## Status

stx_vorbis is a new implementation undergoing validation. It is not a
security certification, and no formal proof or independent security audit has
been done. Its evidence so far: sanitizer builds, six libFuzzer targets run in
Linux CI, an allocation-failure sweep through a full decode, libvorbis
differential tests on four CI platforms, and a downstream application's 424
Ogg assets decoding with exact loop lengths. A finite corpus is not exhaustive,
and some disagreements with the reference oracles remain open.

The detailed records are in the stx repository:

- `stx_vorbis/README.md`: capabilities, streaming protocol, limits.
- `stx_vorbis/docs/VALIDATION.md`: test coverage, fuzzing, reference
  disagreements, remaining assurance work.
- `stx_vorbis/docs/OPTIMIZATION.md` and `stx_vorbis/docs/PRODUCTION.md`:
  timing method, results and the transform integration.
- `stx_vorbis/docs/INVARIANTS.md`: parsing, allocation and ownership review.
