#pragma once

// The check must still find a token in code after the comments are removed.
struct CGContext;
inline int in_code_value(CGContext* context) { return context == nullptr ? 0 : 1; }
