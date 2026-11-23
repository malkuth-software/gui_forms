#pragma once

// The platform types GUI.Forms' public headers name (STXlib swap-in A2, T1):
// owned aliases, so the headers spell GUI.Forms' names, not the standard
// library's, and the move to STXlib's own types (T2: stxlib::Path,
// IntervalTime, Thread, Mutex and ThreadIdentity) changes this header, not
// every site. Thread, Mutex and ThreadId are STXlib's since the shield
// (2026-11-22: every route to the host's thread library passes through
// STXlib); FilePath and IntervalPoint are still the standard types.

#include <chrono>
#include <filesystem>
#include <stxlib/threads.hpp>

namespace gui_forms {

// A file system path (a font, a light-plane or a resource directory, an
// executable to host, a sound file).
using FilePath = std::filesystem::path;

// A point on the monotonic clock: intervals between two of them are
// meaningful, the points themselves are not times of day.
using IntervalPoint = std::chrono::steady_clock::time_point;

// A thread the library starts and joins (a sub-surface's).
using Thread = stxlib::Thread;

// A mutual-exclusion lock.
using Mutex = stxlib::Mutex;

// A thread's identity (an owner thread, a UI thread); default-constructed,
// no thread. Never reused, even after its thread ends.
using ThreadId = stxlib::ThreadIdentity;

} // namespace gui_forms
