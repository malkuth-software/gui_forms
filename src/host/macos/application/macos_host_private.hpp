#pragma once

// The AppKit host's shared private prelude, included first by every source
// under src/host/macos/application, view and accessibility: the headers the
// host was written against and the GUI.Forms names its sources use
// unqualified. Objective-C++ only; never included by portable code.

#include "display_link_trace.hpp"
#include "../../offscreen_font_lookup.hpp"
#import <AppKit/AppKit.h>
// kAEQuitReason and the session-end reasons (AERegistry.h); constants only.
#import <CoreServices/CoreServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/CABase.h>
#import <QuartzCore/CADisplayLink.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "macos_host.hpp"
#include "../../../core/failure/fail_fast.hpp"
#if defined(GUI_FORMS_PREPARED_TEXT)
#include "../../../core/text/prepared/prepared_storage.hpp"
#endif
#include "application_menu.hpp"
#include "front_reuse.hpp"
#include "live_surface_damage.hpp"
#include "gui_forms/diagnostics/invalidation_overlay/invalidation_overlay.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "../../../core/damage/device_damage/device_damage.hpp"
#include "../../../render/skia/raster/skia_raster.hpp"
#include "../../../render/skia/raster/skia_splash_bake.hpp"
#include "../../../core/host/frame_capture/frame_capture.hpp"
#include "../../../core/application/splashscreen_surface.hpp"
#include "../compositor/macos_compositor.hpp"
#include "../../posix/fonts/sealed_pages_posix.hpp"
#include "../../../core/resources/fonts/bundled_font_set.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <time.h>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

using gui_forms::DamageRegion;
using gui_forms::DragAction;
using gui_forms::DragBinaryData;
using gui_forms::DragDataItem;
using gui_forms::DragEffect;
using gui_forms::DragEvent;
using gui_forms::DragFileListData;
using gui_forms::DragLimits;
using gui_forms::DragTextData;
using gui_forms::HostActivationEvent;
using gui_forms::HostAttachEvent;
using gui_forms::HostCapability;
using gui_forms::HostCloseReason;
using gui_forms::HostCloseRequest;
using gui_forms::HostClipboardTextResult;
using gui_forms::HostClosedEvent;
using gui_forms::HostContextRequestEvent;
using gui_forms::ContextRequestOrigin;
using gui_forms::HostDispatchResult;
using gui_forms::HostDialogChoice;
using gui_forms::HostDialogOutcome;
using gui_forms::HostDialogRequest;
using gui_forms::HostDialogResult;
using gui_forms::HostColorDialogRequest;
using gui_forms::HostColorDialogResult;
using gui_forms::HostDisplayEvent;
using gui_forms::HostFolderDialogRequest;
using gui_forms::HostMessageButtons;
using gui_forms::HostMessageDialogRequest;
using gui_forms::HostMessageDialogResult;
using gui_forms::HostMessageIcon;
using gui_forms::HostOpenFileDialogRequest;
using gui_forms::HostPathDialogResult;
using gui_forms::HostSaveFileDialogRequest;
using gui_forms::HostEvent;
using gui_forms::HostEventPayload;
using gui_forms::HostOcclusionEvent;
using gui_forms::HostResizeEvent;
using gui_forms::HostScaleEvent;
using gui_forms::HostMonitor;
using gui_forms::HostMonitorResult;
using gui_forms::HostServiceError;
using gui_forms::HostServiceStatus;
using gui_forms::HostSoundCue;
using gui_forms::HostSoundCueRequest;
using gui_forms::HostServices;
using gui_forms::HostSession;
using gui_forms::HostShutdownEvent;
using gui_forms::KeyAction;
using gui_forms::KeyEvent;
using gui_forms::LiveSurfacePresentation;
using gui_forms::Modifier;
using gui_forms::PaintReceipt;
using GFPoint = gui_forms::Point;
using gui_forms::PointerAction;
using gui_forms::PointerButton;
using gui_forms::PointerEvent;
using gui_forms::WheelDeltaUnit;
using gui_forms::SemanticAction;
using gui_forms::SemanticNode;
using gui_forms::SemanticOrientation;
using gui_forms::SemanticRole;
using gui_forms::SemanticState;
using gui_forms::SemanticTextGrapheme;
using gui_forms::SemanticTextLayout;
using gui_forms::SemanticTextLine;
using gui_forms::SemanticTextSelection;
using GFRect = gui_forms::Rect;
using GFSize = gui_forms::Size;
using gui_forms::TextInputEvent;
using gui_forms::Window;
using gui_forms::CursorKind;
using gui_forms::render::SkiaRaster;
