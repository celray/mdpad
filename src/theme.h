#pragma once

#include <SDL3/SDL.h>

namespace mdpad {

struct Theme {
    SDL_Color background;

    // Body text
    SDL_Color text;

    // Headings — each level gets its own shade.
    SDL_Color h1;
    SDL_Color h2;
    SDL_Color h3;
    SDL_Color h4;
    SDL_Color h5;
    SDL_Color h6;

    // Block code
    SDL_Color code_text;
    SDL_Color code_bg;
    SDL_Color code_border;

    // Inline code
    SDL_Color inline_code_text;
    SDL_Color inline_code_bg;

    // Quotes
    SDL_Color quote_text;
    SDL_Color quote_border;

    // Misc
    SDL_Color hr;
    SDL_Color list_marker;

    // Syntax highlighting
    SDL_Color syn_keyword;
    SDL_Color syn_string;
    SDL_Color syn_number;
    SDL_Color syn_comment;
    SDL_Color syn_function;
    SDL_Color syn_type;
    SDL_Color syn_builtin;
    SDL_Color syn_operator;
    SDL_Color syn_punct;
};

static constexpr Theme kLightTheme = {
    /* background     */ {252, 252, 250, 255},
    /* text           */ { 45,  45,  50, 255},

    /* h1             */ { 30,  58, 138, 255},   // deep blue
    /* h2             */ {  6, 101, 166, 255},   // sky blue
    /* h3             */ { 15, 118, 110, 255},   // teal
    /* h4             */ {101,  67,  33, 255},   // warm brown
    /* h5             */ { 80,  80,  90, 255},
    /* h6             */ {110, 110, 120, 255},

    /* code_text      */ { 39,  39,  42, 255},
    /* code_bg        */ {244, 244, 245, 255},
    /* code_border    */ {228, 228, 231, 255},

    /* inline_code_t  */ {185,  28,  28, 255},   // crimson
    /* inline_code_bg */ {250, 240, 238, 255},

    /* quote_text     */ { 82,  82,  91, 255},
    /* quote_border   */ {148, 163, 184, 255},   // slate

    /* hr             */ {212, 212, 216, 255},
    /* list_marker    */ { 90, 110, 140, 255},

    /* syn_keyword    */ {139,  34, 170, 255},   // purple
    /* syn_string     */ { 22, 126,  75, 255},   // green
    /* syn_number     */ {200,  80,  20, 255},   // orange
    /* syn_comment    */ {130, 130, 140, 255},
    /* syn_function   */ { 30,  90, 200, 255},   // blue
    /* syn_type       */ {  6, 120, 140, 255},   // cyan
    /* syn_builtin    */ {180,  50, 130, 255},   // magenta
    /* syn_operator   */ { 80,  80,  90, 255},
    /* syn_punct      */ { 80,  80,  90, 255},
};

}  // namespace mdpad
