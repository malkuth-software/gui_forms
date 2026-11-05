# GUI.Forms source and atlas migration frontier

Status: **OBSERVED inventory with a CANDIDATE execution order**.

The grand-architect direction to isolate controls, core mechanisms, and state
machines is **GIVEN**. The bundle sequence below is not an architecture
decision; it is a reversible work order chosen to keep each change buildable,
testable, and visually reviewable. Accepted ADRs and contract registries remain
authoritative throughout the migration.

## Bundle status

| Bundle | Status | Cohesive scope |
| --- | --- | --- |
| 001 | **OBSERVED complete** | `Card`, `ReviewCard`, and `MasterDetailView`; hierarchical headers/sources, compatibility umbrella, activation-selection policy, focused tests, M4 capture crops, and reviewed atlas pages |
| 002 | **OBSERVED complete** | Basic retained primitives: `Panel`, `Label`, `ButtonBase`, `Button`, `CheckBox`, `RadioButton`, `LinkLabel`, `GroupBox`, and `PictureBox`; nine isolated translation units, shared private rendering helpers, bounded customization, focused M4 tests, native showcase crops, and reviewed atlas pages |
| 003 | **OBSERVED complete** | Containers and scrolling: `ScrollProperties`, `ScrollableControl`, `ContainerControl`, `UserControl`, scaled containers, flow/table layout panels, tab controls, `SplitterPanel`, `SplitContainer`, and the source-private `SplitterGrip`; mirrored hierarchical declarations/definitions, compatibility umbrellas, explicit bounded flow item spacing, focused M4 tests, Screen Sharing captures, and reviewed all-method atlas pages |
| 004 | **OBSERVED complete** | Input, range, and date controls: `TextBox`, `ListBox`, `CheckedListBox`, `ComboBox`, `NumericUpDown`, `RangeControl`, `TrackBar`, `ProgressBar`, `ScrollBar` leaves, `DateTimePicker`, their value types, and source-private overlay/spin/calendar state machines; mirrored hierarchical declarations/definitions, compatibility umbrellas, bounded maximum-length/indicator/overlay/button/alignment customization, focused M4 tests, Screen Sharing captures, and reviewed all-method atlas pages |
| 005 | **OBSERVED complete** | Collections and transient surfaces: `TreeView`, `ObjectView`, `CorrespondenceView`, `ContextMenu`, `MenuStrip`, `AnchoredOverlayLayer`, `ToolTip`, `ErrorProvider`, `HelpProvider`, their policy/event/snapshot values, and source-private tooltip/error visual state machines; mirrored hierarchical declarations/definitions, compatibility umbrellas, bounded expander/secondary-text/status-rail/menu-width/menu-padding/tooltip-width/error-icon customization, focused M4 tests, Screen Sharing captures, and reviewed all-method atlas pages |
| 006 | **OBSERVED complete** | Inspection and specialized visual surfaces: `PropertyGrid`, `PropertyList`, converter/editor registries and source-private editor layers, `InstrumentRack` and source-private rack modules, `DrawingSurface`, `MetricsView`, `EasingPreview`, `RasterCanvas`, and `MaterialPanel`; mirrored hierarchical declarations/definitions, compatibility umbrellas, bounded overlay/swatch/raster/metric/easing customization, focused M4 tests, Screen Sharing captures, and reviewed all-method atlas pages |
| 007 | **OBSERVED complete** | Window/host lifecycle state machines: hierarchical `Window` declaration and lifecycle/dispatcher/token sources, isolated dispatcher operation/queue/Window/Control paths, split host value/service/session seams, split deterministic headless capabilities/services/session, isolated AppKit service implementation and native application loop, isolated Win32 service state owner and native application loop, bounded semantic-sound coalescing customization, focused M4 macOS tests, complete M4 MinGW cross-build, Screen Sharing native-window capture, and reviewed all-method atlas pages including private `Window` machinery |
| 008 | **OBSERVED complete** | Presentation and high-throughput buffers: hierarchical public `LiveSurface`, scheduler, display, and Window presentation contracts; isolated configurable buffer/state/allocation, immutable frame, exclusive write lease, revocable wake, display command/chunk/recorder/replay, scheduled request/token, device-damage, Window presentation, and Window scheduler paths; bounded two-to-eight-buffer customization with triple-buffer default; focused M4 macOS tests, complete M4 MinGW cross-build including the Windows live-surface endpoint, refreshed Screen Sharing native-showcase pass, and reviewed all-method atlas pages |
| 009 | **OBSERVED complete** | Drawing, text shaping, resources, themes, materials, and semantic projection mechanisms: thin compatibility umbrellas over mirrored per-type public headers; isolated drawing value/object/brush/pen/font/path/region/image/bitmap/recorder implementations with private shared support; isolated text store, Unicode, shaping validation, image registry, theme, semantic snapshot, Skia executor/raster, CoreGraphics raster, and HarfBuzz engine paths; repaired split-aware boundary/fuzzer build contracts and digit-separator-aware atlas discovery; 453-type/144-enum reviewed atlas validation, focused native M4 tests, complete M4 MinGW/Skia cross-build, passing renderer boundary audit, and refreshed Screen Sharing Images and Drawing board |
| 010 | **OBSERVED complete** | Binding, command, component, timer, metrics, feedback, and C ABI isolation: mirrored public hierarchies and isolated definitions; command enabled-state synchronization policy; split control adapters, generational registry, error translation, and Windows paint endpoint; the atlas then reported 471 types/147 enums; complete M4 MinGW/Skia build; all 64 native M4 tests; and refreshed Screen Sharing Controls and States boards |
| 011 | **OBSERVED complete** | Foundational retained-control, revocable event, normalized input/drag, animation timeline, image-list, static-tree/control-factory, geometry, Painter, Rect, and bounded DamageRegion contracts moved behind stable compatibility umbrellas into hierarchy-mirrored declarations/definitions; C function-table discovery now names entries correctly; atlas validation requires a reviewed narrative for every discovered type and publishes all enum vocabularies with no missing visual-capture marker; the new command enabled-state option preserves legacy aggregate ordering with a regression test; all 64 native M4 tests, the complete M4 MinGW/Skia build, and a Screen Sharing animation pause/zero-wakeup transition pass |
| 012 | **OBSERVED complete** | Completion-audit closure: fixed the declaration parser's false `friend class detail::…` type, corrected the atlas to 470 real types/147 enums/617 HTML and Markdown pages, isolated the remaining production definitions (`StableId`, `UpdateScope`, host capability/service/session snapshots, binding object/collection handles, property rows, drawing geometry/image snapshots, material fills, and the Win32 compatibility-paint state machine), hierarchy-named the Skia executor, isolated demo declarations while identifying their single composition unit, and made zero grouped migrations plus visual-capture presence generator-enforced; fresh native and complete MinGW/Skia M4 builds pass, all 64 native M4 tests pass, and the bundle-correct Screen Sharing run renders Controls, Images and Drawing, and States and Diagnostics while the posted dispatcher reports ordered `ABCD`, three callbacks, and zero faults |
| 013 | **OBSERVED complete** | Final visual-isolation audit: split the private C ABI `InputTransparentControl`, `FormControl`, Unicode `FieldControl`, live-surface `RasterControl`, and nonvisual `AbiPropertyObjectControl` state owner out of the 1,492-line adapter header into exact per-type directories, headers, and translation units; made `CalendarOverlay`'s existing exact source boundary visible to the atlas; retained `abi_control_adapters.hpp` as a thin compatibility umbrella; the atlas now reports 146 isolated types, 321 intentionally header-only/distributed contracts, three demo-composition types, and only the foundational multi-state-machine `Control` plus demo-only `GalleryControl` as non-isolated visuals; fresh native and complete MinGW/Skia M4 builds pass, all 64 native M4 tests pass, and the final bundle-correct Screen Sharing run renders fully labeled Controls and Unicode Text and Input boards |

## Bundle completion law

A bundle is complete only when all applicable checks have evidence:

1. Each migrated reusable type has a mirrored hierarchical declaration and
   definition path. A former family header remains only as a thin compatibility
   umbrella while downstream code migrates.
2. Public behavior and ownership laws are preserved. New customization is
   admitted as explicit validated state with ordering, invalidation, semantic,
   and lifetime tests; unmeasured performance claims are not added.
3. The final Neo source is synchronized and built on the M4 Mini. The quiet
   verifier emits one success line or a bounded diagnostic tail.
4. Every visual type is exercised in a real retained demoboard surface through
   Screen Sharing. The atlas crop is taken from that observation, not from a
   mockup.
5. `manual.json` supplies reviewed method explanations and evidence notes. The
   generated HTML and Markdown mirrors pass structural validation.
6. The generator reports zero `grouped migration pending` records. Source-private
   generated/demo composition is named separately and may not masquerade as a
   reusable library definition.

The generator now rejects missing manual type narratives and publishes every
discovered method and enum value. Atlas presence remains evidence of inventory
and documentation only; source isolation, visual verification, and accepted
architecture retain their separate evidence labels.
