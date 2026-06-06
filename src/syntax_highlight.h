#pragma once

#include "theme.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

namespace mdpad {

struct SyntaxToken {
    std::string text;
    SDL_Color color;
};

// Tokenize a single line of source code according to `lang`.
// Returns a sequence of (text, color) runs that, concatenated, reproduce
// the original line.  `theme` is used to map token categories to colors.
// `lang` is normalized (case-insensitive); unknown languages get a generic
// tokenizer that highlights strings / numbers / common comments.
std::vector<SyntaxToken> highlight_line(const std::string& line,
                                        const std::string& lang,
                                        const Theme& theme);

}  // namespace mdpad
