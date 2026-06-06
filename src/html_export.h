#pragma once

#include "markdown_parser.h"
#include "theme.h"

#include <string>

namespace mdpad {

// Render `doc` to a standalone, styled HTML document suitable for
// printing via a web browser.  The returned HTML contains:
//   - inline CSS mirroring the current theme (heading colors, code
//     block bg, quote border, etc.),
//   - syntax-highlighted code blocks (per-token <span> with inline
//     colors, so the output is self-contained and prints faithfully
//     even offline),
//   - a tiny onload script that triggers the browser's print dialog.
std::string document_to_html(const Document& doc, const Theme& theme,
                              const std::string& title);

}  // namespace mdpad
