#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/host/types/host_types.hpp"

#include <optional>

namespace gui_forms {

class Window;

// The stock icon a message dialog shows for `icon` (exhibit gap E10: the
// MessageBox and window alerts use the set): information-circle,
// exclamation-triangle, x-circle or question-mark-circle, solid, 32 logical
// units square, baked at `window`'s scale in its root control's theme: the
// panel recipe's glyph colour, in its invalid state for an error and its
// pending state for a warning or a question, so dark schemes and high
// contrast follow. The pixels are HostImage's premultiplied BGRA, top row
// first. Empty for HostMessageIcon::none, a value outside the enumeration, a
// window without a root, or a scale out of a part raster's range.
// HostMessageDialogRequest::icon_image carries it to the host.
[[nodiscard]] GUI_FORMS_API std::optional<HostImage> stock_alert_icon_image(const Window& window, HostMessageIcon icon);

} // namespace gui_forms
