#include "renderer.h"

#include "syntax_highlight.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cstring>

namespace mdpad {

static constexpr float kMarginX = 48.0f;
static constexpr float kMarginTop = 32.0f;
static constexpr float kCodePadX = 14.0f;
static constexpr float kCodePadY = 10.0f;
static constexpr float kQuotePadLeft = 18.0f;
static constexpr float kQuotePadY = 4.0f;
static constexpr float kQuoteBorderW = 4.0f;
static constexpr float kInlineCodePadX = 3.0f;
static constexpr float kListIndent = 24.0f;
static constexpr float kBulletWidth = 18.0f;

Renderer::Renderer() {
    theme_ = kLightTheme;
}

Renderer::~Renderer() {
    clear_layout();
    if (font_regular_) TTF_CloseFont(font_regular_);
    if (font_bold_) TTF_CloseFont(font_bold_);
    if (font_italic_) TTF_CloseFont(font_italic_);
    if (font_bold_italic_) TTF_CloseFont(font_bold_italic_);
    if (font_h1_) TTF_CloseFont(font_h1_);
    if (font_h2_) TTF_CloseFont(font_h2_);
    if (font_h3_) TTF_CloseFont(font_h3_);
    if (font_h4_) TTF_CloseFont(font_h4_);
    if (font_code_) TTF_CloseFont(font_code_);
}

bool Renderer::init(SDL_Renderer* sdl_renderer, const std::string& font_dir,
                    float base_font_size) {
    sdl_renderer_ = sdl_renderer;

    std::string regular = font_dir + "/JetBrainsMonoNerdFont-Regular.ttf";
    std::string bold = font_dir + "/JetBrainsMonoNerdFont-Bold.ttf";
    std::string italic = font_dir + "/JetBrainsMonoNerdFont-Italic.ttf";
    std::string bold_italic = font_dir + "/JetBrainsMonoNerdFont-BoldItalic.ttf";

    font_regular_ = TTF_OpenFont(regular.c_str(), base_font_size);
    font_bold_ = TTF_OpenFont(bold.c_str(), base_font_size);
    font_italic_ = TTF_OpenFont(italic.c_str(), base_font_size);
    font_bold_italic_ = TTF_OpenFont(bold_italic.c_str(), base_font_size);
    font_h1_ = TTF_OpenFont(bold.c_str(), base_font_size * 2.0f);
    font_h2_ = TTF_OpenFont(bold.c_str(), base_font_size * 1.55f);
    font_h3_ = TTF_OpenFont(bold.c_str(), base_font_size * 1.28f);
    font_h4_ = TTF_OpenFont(bold.c_str(), base_font_size * 1.1f);
    font_code_ = TTF_OpenFont(regular.c_str(), base_font_size * 0.95f);

    if (!font_regular_) {
        SDL_Log("Failed to load font '%s': %s", regular.c_str(), SDL_GetError());
        return false;
    }

    return true;
}

void Renderer::set_document(const Document& doc) {
    document_ = doc;
    scroll_y_ = 0.0f;
    last_layout_width_ = 0;
    clear_selection();
}

void Renderer::scroll(float delta_y) {
    scroll_y_ -= delta_y * 40.0f;
    if (scroll_y_ < 0.0f) {
        scroll_y_ = 0.0f;
    }
}

float Renderer::get_scroll_y() const { return scroll_y_; }
void Renderer::set_scroll_y(float y) { scroll_y_ = y; }

void Renderer::set_theme(const Theme& t) {
    theme_ = t;
    last_layout_width_ = 0;  // force relayout for color changes
}

void Renderer::clear_layout() {
    for (auto& p : pieces_) {
        if (p.texture) SDL_DestroyTexture(p.texture);
    }
    pieces_.clear();
    bgs_.clear();
    left_borders_.clear();
    hrs_.clear();
    next_line_id_ = 0;
    // Selection refers to piece indices that are about to be invalidated.
    sel_anchor_ = {};
    sel_caret_ = {};
    selecting_ = false;
}

TTF_Font* Renderer::font_for_span(const Block& block,
                                  const TextSpan& span) const {
    // Inline code always uses the code font.
    if (span.code) return font_code_ ? font_code_ : font_regular_;

    switch (block.type) {
    case BlockType::Heading1: return font_h1_ ? font_h1_ : font_bold_;
    case BlockType::Heading2: return font_h2_ ? font_h2_ : font_bold_;
    case BlockType::Heading3: return font_h3_ ? font_h3_ : font_bold_;
    case BlockType::Heading4:
    case BlockType::Heading5:
    case BlockType::Heading6:
        return font_h4_ ? font_h4_ : font_bold_;
    default: break;
    }

    if (span.bold && span.italic) {
        return font_bold_italic_ ? font_bold_italic_
                                 : (font_bold_ ? font_bold_ : font_regular_);
    }
    if (span.bold) return font_bold_ ? font_bold_ : font_regular_;
    if (span.italic) return font_italic_ ? font_italic_ : font_regular_;
    return font_regular_;
}

SDL_Color Renderer::color_for_block_text(BlockType type) const {
    switch (type) {
    case BlockType::Heading1: return theme_.h1;
    case BlockType::Heading2: return theme_.h2;
    case BlockType::Heading3: return theme_.h3;
    case BlockType::Heading4: return theme_.h4;
    case BlockType::Heading5: return theme_.h5;
    case BlockType::Heading6: return theme_.h6;
    case BlockType::Quote: return theme_.quote_text;
    case BlockType::CodeBlock: return theme_.code_text;
    default: return theme_.text;
    }
}

float Renderer::spacing_before(BlockType type) const {
    switch (type) {
    case BlockType::Heading1: return 28.0f;
    case BlockType::Heading2: return 24.0f;
    case BlockType::Heading3: return 20.0f;
    case BlockType::Heading4: return 16.0f;
    case BlockType::CodeBlock: return 10.0f;
    case BlockType::Quote: return 10.0f;
    case BlockType::Table: return 12.0f;
    case BlockType::HorizontalRule: return 16.0f;
    case BlockType::ListItem: return 2.0f;
    default: return 8.0f;
    }
}

float Renderer::spacing_after(BlockType type) const {
    switch (type) {
    case BlockType::Heading1: return 12.0f;
    case BlockType::Heading2: return 10.0f;
    case BlockType::Heading3: return 8.0f;
    case BlockType::Heading4: return 6.0f;
    case BlockType::CodeBlock: return 10.0f;
    case BlockType::Quote: return 10.0f;
    case BlockType::Table: return 12.0f;
    case BlockType::HorizontalRule: return 16.0f;
    case BlockType::ListItem: return 2.0f;
    default: return 6.0f;
    }
}

// ---------------------------------------------------------------------------
// Inline layout
// ---------------------------------------------------------------------------

// Split text into tokens suitable for word-wrapping.  Each token is one of:
// - "\n"  (hard break)
// - " "   (run of spaces collapsed to one)
// - a word (any non-space non-newline sequence)
// We preserve UTF-8 byte sequences; we only split on ASCII whitespace.
// Measure pixel width of the first `bytes` bytes of `text` in `font`.
static int measure_prefix(TTF_Font* font, const std::string& text, int bytes) {
    if (!font || bytes <= 0 || text.empty()) return 0;
    int n = static_cast<int>(text.size());
    if (bytes > n) bytes = n;
    int w = 0, h = 0;
    if (!TTF_GetStringSize(font, text.c_str(), static_cast<size_t>(bytes),
                           &w, &h)) {
        return 0;
    }
    return w;
}

static std::vector<std::string> tokenize_for_wrap(const std::string& text) {
    std::vector<std::string> tokens;
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        char c = text[i];
        if (c == '\n') {
            tokens.push_back("\n");
            i++;
        } else if (c == ' ' || c == '\t') {
            // Collapse consecutive spaces/tabs to a single space token.
            while (i < n && (text[i] == ' ' || text[i] == '\t')) i++;
            tokens.push_back(" ");
        } else {
            size_t start = i;
            while (i < n && text[i] != ' ' && text[i] != '\t' &&
                   text[i] != '\n') {
                i++;
            }
            tokens.push_back(text.substr(start, i - start));
        }
    }
    return tokens;
}

namespace {
struct PendingPiece {
    SDL_Texture* tex = nullptr;
    float x_in_line = 0.0f;
    int w = 0;
    int h = 0;
    int ascent = 0;
    // Optional background (for inline code)
    bool has_bg = false;
    SDL_Color bg{};
    float bg_pad_x = 0.0f;
    // Source text + font for selection / copy.
    std::string text;
    TTF_Font* font = nullptr;
};
}  // namespace

float Renderer::layout_inline_runs(const std::vector<InlineRun>& runs,
                                   float x_start, float y_start,
                                   float max_width) {
    if (runs.empty()) return y_start;

    std::vector<PendingPiece> line;
    float cursor_x = 0.0f;        // relative to x_start
    float line_y = y_start;
    int default_line_h = TTF_GetFontHeight(font_regular_);
    if (default_line_h <= 0) default_line_h = 20;

    auto commit_line = [&](bool force_min_height) {
        if (line.empty() && !force_min_height) {
            return;
        }
        int max_ascent = 0;
        int max_below = 0;
        int line_h_fallback = default_line_h;
        for (const auto& p : line) {
            max_ascent = std::max(max_ascent, p.ascent);
            max_below = std::max(max_below, p.h - p.ascent);
        }
        if (line.empty()) {
            max_ascent = line_h_fallback;
            max_below = 0;
        }
        float line_h = static_cast<float>(max_ascent + max_below);
        if (line_h < 4.0f) line_h = static_cast<float>(line_h_fallback);

        int this_line_id = next_line_id_++;

        for (const auto& p : line) {
            float piece_top = line_y + static_cast<float>(max_ascent - p.ascent);

            if (p.has_bg) {
                BgBox bg;
                bg.x = x_start + p.x_in_line - p.bg_pad_x;
                bg.y = piece_top;
                bg.w = static_cast<float>(p.w) + p.bg_pad_x * 2.0f;
                bg.h = static_cast<float>(p.h);
                bg.fill = p.bg;
                bg.has_border = false;
                bgs_.push_back(bg);
            }

            TextPiece tp;
            tp.texture = p.tex;
            tp.x = x_start + p.x_in_line;
            tp.y = piece_top;
            tp.w = p.w;
            tp.h = p.h;
            tp.text = p.text;
            tp.font = p.font;
            tp.line_id = this_line_id;
            pieces_.push_back(std::move(tp));
        }

        line.clear();
        line_y += line_h;
        cursor_x = 0.0f;
    };

    for (const auto& run : runs) {
        if (!run.font) continue;
        auto tokens = tokenize_for_wrap(run.text);
        int run_ascent = TTF_GetFontAscent(run.font);
        if (run_ascent <= 0) run_ascent = TTF_GetFontHeight(run.font) * 4 / 5;

        for (const auto& tok : tokens) {
            if (tok == "\n") {
                commit_line(true);
                continue;
            }

            bool is_space = (tok == " ");
            if (is_space && line.empty()) {
                // Skip leading whitespace on a line.
                continue;
            }

            // Measure.
            int tw = 0, th = 0;
            TTF_GetStringSize(run.font, tok.c_str(), tok.size(), &tw, &th);

            float effective_w = static_cast<float>(tw);
            float bg_pad = run.has_bg ? kInlineCodePadX : 0.0f;
            float needed_w = effective_w + bg_pad * 2.0f;

            if (!line.empty() && cursor_x + needed_w > max_width &&
                !is_space) {
                commit_line(false);
            }

            if (is_space && line.empty()) continue;

            // Render.
            SDL_Surface* surf = TTF_RenderText_Blended(
                run.font, tok.c_str(), tok.size(), run.color);
            if (!surf) continue;
            SDL_Texture* tex = SDL_CreateTextureFromSurface(sdl_renderer_,
                                                             surf);
            int sw = surf->w;
            int sh = surf->h;
            SDL_DestroySurface(surf);
            if (!tex) continue;

            PendingPiece p;
            p.tex = tex;
            p.x_in_line = cursor_x + bg_pad;
            p.w = sw;
            p.h = sh;
            p.ascent = run_ascent;
            if (run.has_bg && !is_space) {
                p.has_bg = true;
                p.bg = run.bg;
                p.bg_pad_x = bg_pad;
            }
            p.text = tok;
            p.font = run.font;
            line.push_back(std::move(p));
            cursor_x += static_cast<float>(sw) + bg_pad * 2.0f;
        }
    }

    commit_line(false);
    return line_y;
}

// ---------------------------------------------------------------------------
// Block layout
// ---------------------------------------------------------------------------

float Renderer::layout_text_block(const Block& block, float x, float y,
                                  float max_width) {
    std::vector<InlineRun> runs;
    SDL_Color default_color = color_for_block_text(block.type);

    for (const auto& span : block.spans) {
        InlineRun run;
        run.text = span.text;
        run.font = font_for_span(block, span);
        if (span.code) {
            run.color = theme_.inline_code_text;
            run.has_bg = true;
            run.bg = theme_.inline_code_bg;
        } else {
            run.color = default_color;
        }
        runs.push_back(std::move(run));
    }

    return layout_inline_runs(runs, x, y, max_width) - y;
}

static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n') {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start <= s.size()) {
        out.push_back(s.substr(start));
    }
    return out;
}

float Renderer::layout_code_block(const Block& block, float x, float y,
                                  float max_width) {
    // Collect the full code text from the spans.
    std::string source;
    for (const auto& span : block.spans) {
        source += span.text;
    }

    // Strip one trailing newline, if present, so we don't render an
    // extra blank line at the end of every code block.
    if (!source.empty() && source.back() == '\n') {
        source.pop_back();
    }

    auto code_lines = split_lines(source);
    if (code_lines.empty()) code_lines.push_back("");

    TTF_Font* cf = font_code_ ? font_code_ : font_regular_;
    int line_h = TTF_GetFontHeight(cf);
    if (line_h <= 0) line_h = 20;
    int ascent = TTF_GetFontAscent(cf);
    if (ascent <= 0) ascent = line_h * 4 / 5;

    float content_x = x + kCodePadX;
    float content_y = y + kCodePadY;
    float content_max_w = max_width - kCodePadX * 2.0f;
    if (content_max_w < 50.0f) content_max_w = 50.0f;

    float widest_line_px = 0.0f;

    for (size_t idx = 0; idx < code_lines.size(); ++idx) {
        const auto& line = code_lines[idx];
        float cursor_x = 0.0f;
        float piece_y = content_y + static_cast<float>(idx) * line_h;

        auto tokens = highlight_line(line, block.lang, theme_);
        if (tokens.empty() && !line.empty()) {
            tokens.push_back({line, theme_.code_text});
        }

        int line_id = next_line_id_++;

        for (const auto& tok : tokens) {
            if (tok.text.empty()) continue;
            // Replace tabs for display.
            std::string display;
            display.reserve(tok.text.size());
            for (char ch : tok.text) {
                if (ch == '\t') {
                    display.append("    ");
                } else {
                    display += ch;
                }
            }

            SDL_Surface* surf = TTF_RenderText_Blended(
                cf, display.c_str(), display.size(), tok.color);
            if (!surf) continue;
            SDL_Texture* tex = SDL_CreateTextureFromSurface(sdl_renderer_,
                                                             surf);
            int sw = surf->w;
            int sh = surf->h;
            SDL_DestroySurface(surf);
            if (!tex) continue;

            TextPiece tp;
            tp.texture = tex;
            tp.x = content_x + cursor_x;
            tp.y = piece_y;
            tp.w = sw;
            tp.h = sh;
            tp.text = display;
            tp.font = cf;
            tp.line_id = line_id;
            pieces_.push_back(std::move(tp));

            cursor_x += static_cast<float>(sw);
        }

        widest_line_px = std::max(widest_line_px, cursor_x);
    }

    float total_h = static_cast<float>(code_lines.size()) *
                    static_cast<float>(line_h);
    float bg_h = total_h + kCodePadY * 2.0f;

    // Background box (clamped to content area width).
    BgBox bg;
    bg.x = x;
    bg.y = y;
    bg.w = max_width;
    bg.h = bg_h;
    bg.fill = theme_.code_bg;
    bg.border = theme_.code_border;
    bg.has_border = true;
    // Insert at the front so it draws first.  We'll actually draw bgs
    // before pieces in the main render loop, so order within bgs_ doesn't
    // matter relative to pieces.  But we do want this box _behind_ the
    // highlighted text, which is guaranteed by draw order (bgs first).
    bgs_.push_back(bg);

    (void)widest_line_px;  // reserved for future horizontal overflow handling

    return bg_h;
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

float Renderer::layout_table(const Block& block, float x, float y,
                             float max_width) {
    if (block.rows.empty()) return 0.0f;

    // Column count = max cell count across rows.
    size_t n_cols = 0;
    for (const auto& row : block.rows) {
        n_cols = std::max(n_cols, row.cells.size());
    }
    if (n_cols == 0) return 0.0f;

    const float cell_pad_x = 10.0f;
    const float cell_pad_y = 6.0f;
    float col_w = max_width / static_cast<float>(n_cols);
    float cell_text_w = col_w - cell_pad_x * 2.0f;
    if (cell_text_w < 40.0f) cell_text_w = 40.0f;

    float row_y = y;

    for (const auto& row : block.rows) {
        // Lay out each cell to measure its height.  Because we don't know
        // the final row height yet we append pieces first, then fix up
        // their vertical positions only if we decide to center-align.  For
        // now we top-align: row height is just the max cell height.
        float max_cell_h = 0.0f;
        size_t row_pieces_start = pieces_.size();
        (void)row_pieces_start;

        for (size_t ci = 0; ci < n_cols; ++ci) {
            if (ci >= row.cells.size()) continue;
            const auto& cell = row.cells[ci];
            if (cell.spans.empty()) continue;

            std::vector<InlineRun> runs;
            runs.reserve(cell.spans.size());
            for (const auto& span : cell.spans) {
                InlineRun r;
                r.text = span.text;

                TTF_Font* f = nullptr;
                bool want_bold = span.bold || row.is_header;
                if (span.code) {
                    f = font_code_;
                } else if (want_bold && span.italic) {
                    f = font_bold_italic_;
                } else if (want_bold) {
                    f = font_bold_;
                } else if (span.italic) {
                    f = font_italic_;
                } else {
                    f = font_regular_;
                }
                if (!f) f = font_regular_;
                r.font = f;

                if (span.code) {
                    r.color = theme_.inline_code_text;
                    r.has_bg = true;
                    r.bg = theme_.inline_code_bg;
                } else {
                    r.color = theme_.text;
                }
                runs.push_back(std::move(r));
            }

            float cell_x = x + static_cast<float>(ci) * col_w + cell_pad_x;
            float cell_top = row_y + cell_pad_y;
            float end_y = layout_inline_runs(runs, cell_x, cell_top,
                                             cell_text_w);
            float h = end_y - cell_top;
            if (h > max_cell_h) max_cell_h = h;
        }

        if (max_cell_h <= 0.0f) {
            max_cell_h = static_cast<float>(TTF_GetFontHeight(font_regular_));
        }
        float row_h = max_cell_h + cell_pad_y * 2.0f;

        // Header row background — note: bgs drawn in order, but header
        // bg is added _after_ cells' inline-code bgs and will paint over
        // them.  To avoid that, insert at the start of bgs_ so it paints
        // first.  Simpler fix: record bg index range per row and reorder.
        // For now, header bg only covers the header row and we accept
        // that inline-code pills would be occluded.  Markdown tables
        // rarely have inline code in headers.
        if (row.is_header) {
            BgBox bg;
            bg.x = x;
            bg.y = row_y;
            bg.w = max_width;
            bg.h = row_h;
            bg.fill = theme_.code_bg;
            bg.has_fill = true;
            bg.has_border = false;
            // Insert before this row's piece-related bgs so it draws first.
            bgs_.insert(bgs_.begin(), bg);
        }

        row_y += row_h;

        // Divider under each row.
        HorizontalRuleBox hr;
        hr.x = x;
        hr.y = row_y - 1.0f;
        hr.w = max_width;
        hr.color = row.is_header ? theme_.code_border : theme_.code_bg;
        hrs_.push_back(hr);
    }

    // Outer box — border only, no fill.
    BgBox outer;
    outer.x = x;
    outer.y = y;
    outer.w = max_width;
    outer.h = row_y - y;
    outer.has_fill = false;
    outer.has_border = true;
    outer.border = theme_.code_border;
    bgs_.push_back(outer);

    // Vertical column dividers.
    for (size_t ci = 1; ci < n_cols; ++ci) {
        LeftBorder lb;
        lb.x = x + static_cast<float>(ci) * col_w;
        lb.y = y;
        lb.h = row_y - y;
        lb.thickness = 1.0f;
        lb.color = theme_.code_border;
        left_borders_.push_back(lb);
    }

    return row_y - y;
}

// ---------------------------------------------------------------------------
// Main layout
// ---------------------------------------------------------------------------

void Renderer::layout(int viewport_width) {
    if (viewport_width == last_layout_width_ &&
        (!pieces_.empty() || !bgs_.empty() || !hrs_.empty() ||
         document_.blocks.empty())) {
        return;
    }
    clear_layout();
    last_layout_width_ = viewport_width;

    float max_text_width = static_cast<float>(viewport_width) - kMarginX * 2.0f;
    if (max_text_width < 100.0f) max_text_width = 100.0f;

    float y = kMarginTop;

    for (const auto& block : document_.blocks) {
        y += spacing_before(block.type);

        switch (block.type) {
        case BlockType::HorizontalRule: {
            HorizontalRuleBox hr;
            hr.x = kMarginX;
            hr.y = y;
            hr.w = max_text_width;
            hr.color = theme_.hr;
            hrs_.push_back(hr);
            y += 2.0f;
            break;
        }

        case BlockType::CodeBlock:
            y += layout_code_block(block, kMarginX, y, max_text_width);
            break;

        case BlockType::Table:
            y += layout_table(block, kMarginX, y, max_text_width);
            break;

        case BlockType::Quote: {
            float content_x = kMarginX + kQuotePadLeft;
            float content_max = max_text_width - kQuotePadLeft;
            float block_top = y;
            y += kQuotePadY;
            float text_top = y;
            float text_bottom = layout_text_block(
                block, content_x, text_top,
                content_max > 50.0f ? content_max : 50.0f);
            y = text_top + text_bottom + kQuotePadY;

            LeftBorder lb;
            lb.x = kMarginX;
            lb.y = block_top;
            lb.h = y - block_top;
            lb.thickness = kQuoteBorderW;
            lb.color = theme_.quote_border;
            left_borders_.push_back(lb);
            break;
        }

        case BlockType::ListItem: {
            int depth = block.list_depth > 0 ? block.list_depth : 1;
            float indent = kListIndent * static_cast<float>(depth);
            float bullet_x = kMarginX + indent - kBulletWidth;
            float text_x = kMarginX + indent;
            float text_max = max_text_width - indent;
            if (text_max < 50.0f) text_max = 50.0f;

            // Render the bullet as its own piece on the first line.
            std::string marker;
            if (block.ordered) {
                marker = std::to_string(block.list_index) + ".";
            } else {
                marker = "\xe2\x80\xa2";  // •
            }

            SDL_Surface* msurf = TTF_RenderText_Blended(
                font_regular_, marker.c_str(), marker.size(),
                theme_.list_marker);
            if (msurf) {
                SDL_Texture* mtex = SDL_CreateTextureFromSurface(
                    sdl_renderer_, msurf);
                int mw = msurf->w;
                int mh = msurf->h;
                SDL_DestroySurface(msurf);
                if (mtex) {
                    TextPiece tp;
                    tp.texture = mtex;
                    tp.x = bullet_x;
                    tp.y = y;
                    tp.w = mw;
                    tp.h = mh;
                    // Bullet isn't part of selectable body text — leave
                    // .text empty so it's skipped when copying.
                    tp.font = font_regular_;
                    tp.line_id = next_line_id_;  // share first text line
                    pieces_.push_back(std::move(tp));
                }
            }

            float used = layout_text_block(block, text_x, y, text_max);
            y += used;
            break;
        }

        default:
            y += layout_text_block(block, kMarginX, y, max_text_width);
            break;
        }

        y += spacing_after(block.type);
    }

    total_height_ = y + kMarginTop;
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

bool Renderer::pos_less(const DocPos& a, const DocPos& b) {
    if (a.piece_index != b.piece_index) return a.piece_index < b.piece_index;
    return a.byte_offset < b.byte_offset;
}

void Renderer::normalized_selection(DocPos& s, DocPos& e) const {
    s = sel_anchor_;
    e = sel_caret_;
    if (!s.valid() || !e.valid()) return;
    if (pos_less(e, s)) std::swap(s, e);
}

int Renderer::find_first_selectable() const {
    for (int i = 0; i < static_cast<int>(pieces_.size()); ++i) {
        if (!pieces_[i].text.empty()) return i;
    }
    return -1;
}

int Renderer::find_last_selectable() const {
    for (int i = static_cast<int>(pieces_.size()) - 1; i >= 0; --i) {
        if (!pieces_[i].text.empty()) return i;
    }
    return -1;
}

int Renderer::find_line_first_piece(int line_id) const {
    for (int i = 0; i < static_cast<int>(pieces_.size()); ++i) {
        if (pieces_[i].line_id == line_id && !pieces_[i].text.empty()) return i;
    }
    return -1;
}

int Renderer::find_line_last_piece(int line_id) const {
    int last = -1;
    for (int i = 0; i < static_cast<int>(pieces_.size()); ++i) {
        if (pieces_[i].line_id == line_id && !pieces_[i].text.empty()) last = i;
    }
    return last;
}

void Renderer::ensure_caret() {
    if (sel_caret_.valid() &&
        sel_caret_.piece_index < static_cast<int>(pieces_.size())) {
        return;
    }
    int first = find_first_selectable();
    if (first < 0) return;
    sel_caret_ = {first, 0};
    sel_anchor_ = sel_caret_;
}

void Renderer::set_caret(const DocPos& p, bool extend) {
    if (!p.valid()) return;
    if (extend) {
        if (!sel_anchor_.valid()) sel_anchor_ = p;
        sel_caret_ = p;
    } else {
        sel_anchor_ = p;
        sel_caret_ = p;
    }
    scroll_to_caret();
}

void Renderer::scroll_to_caret() {
    if (!sel_caret_.valid()) return;
    int pi = sel_caret_.piece_index;
    if (pi < 0 || pi >= static_cast<int>(pieces_.size())) return;
    const auto& p = pieces_[pi];
    float vh = last_viewport_height_ > 0.0f ? last_viewport_height_ : 500.0f;
    float top = p.y;
    float bot = p.y + static_cast<float>(p.h);
    const float pad = 24.0f;
    if (top < scroll_y_ + pad) {
        scroll_y_ = top - pad;
        if (scroll_y_ < 0.0f) scroll_y_ = 0.0f;
    } else if (bot > scroll_y_ + vh - pad) {
        scroll_y_ = bot - vh + pad;
        if (scroll_y_ < 0.0f) scroll_y_ = 0.0f;
    }
}

int Renderer::caret_line_id() const {
    if (!sel_caret_.valid() ||
        sel_caret_.piece_index >= static_cast<int>(pieces_.size())) {
        return -1;
    }
    return pieces_[sel_caret_.piece_index].line_id;
}

float Renderer::caret_x() const {
    if (!sel_caret_.valid() ||
        sel_caret_.piece_index >= static_cast<int>(pieces_.size())) {
        return 0.0f;
    }
    const auto& p = pieces_[sel_caret_.piece_index];
    int pw = measure_prefix(p.font, p.text, sel_caret_.byte_offset);
    return p.x + static_cast<float>(pw);
}

Renderer::DocPos Renderer::step_char(DocPos p, bool forward) const {
    if (!p.valid() || pieces_.empty()) return p;
    int pi = p.piece_index;
    if (pi < 0 || pi >= static_cast<int>(pieces_.size())) return p;
    const auto& piece = pieces_[pi];

    if (forward) {
        if (p.byte_offset < static_cast<int>(piece.text.size())) {
            int off = p.byte_offset + 1;
            while (off < static_cast<int>(piece.text.size()) &&
                   (static_cast<unsigned char>(piece.text[off]) & 0xC0) == 0x80) {
                off++;
            }
            return {pi, off};
        }
        // At end of this piece — step into the next selectable piece.
        int next = pi + 1;
        while (next < static_cast<int>(pieces_.size()) &&
               pieces_[next].text.empty()) {
            next++;
        }
        if (next < static_cast<int>(pieces_.size())) {
            return {next, 0};
        }
        return p;  // at end of document
    } else {
        if (p.byte_offset > 0) {
            int off = p.byte_offset - 1;
            while (off > 0 &&
                   (static_cast<unsigned char>(piece.text[off]) & 0xC0) == 0x80) {
                off--;
            }
            return {pi, off};
        }
        // At start of this piece — step to the end of the previous piece.
        int prev = pi - 1;
        while (prev >= 0 && pieces_[prev].text.empty()) {
            prev--;
        }
        if (prev >= 0) {
            return {prev, static_cast<int>(pieces_[prev].text.size())};
        }
        return p;  // at start of document
    }
}

Renderer::DocPos Renderer::hit_test(float doc_x, float doc_y) const {
    DocPos out{};
    if (pieces_.empty()) return out;

    // Collect piece indices whose vertical range contains doc_y.
    std::vector<int> online;
    online.reserve(16);
    for (int i = 0; i < static_cast<int>(pieces_.size()); ++i) {
        const auto& p = pieces_[i];
        if (p.text.empty()) continue;  // skip non-selectable pieces (bullets)
        if (doc_y >= p.y && doc_y < p.y + static_cast<float>(p.h)) {
            online.push_back(i);
        }
    }

    // If no line under cursor, find the closest piece by vertical distance.
    if (online.empty()) {
        int best = -1;
        float best_d = 1e30f;
        for (int i = 0; i < static_cast<int>(pieces_.size()); ++i) {
            const auto& p = pieces_[i];
            if (p.text.empty()) continue;
            float mid = p.y + p.h / 2.0f;
            float d = std::abs(doc_y - mid);
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        }
        if (best < 0) return out;
        // Snap to start or end of that piece depending on doc_x.
        const auto& p = pieces_[best];
        out.piece_index = best;
        out.byte_offset =
            doc_x < p.x + p.w / 2.0f ? 0 : static_cast<int>(p.text.size());
        return out;
    }

    // Among pieces on the line, find one whose x-range contains doc_x.
    for (int idx : online) {
        const auto& p = pieces_[idx];
        if (doc_x >= p.x && doc_x < p.x + static_cast<float>(p.w)) {
            int offset_px = static_cast<int>(doc_x - p.x);
            int measured_w = 0;
            size_t measured_len = 0;
            if (!TTF_MeasureString(p.font, p.text.c_str(), p.text.size(),
                                    offset_px, &measured_w, &measured_len)) {
                measured_len = 0;
            }
            // Snap to the nearer char boundary.
            int before_w = measure_prefix(p.font, p.text,
                                           static_cast<int>(measured_len));
            int after_len = static_cast<int>(measured_len);
            // Advance to the following byte boundary (UTF-8-aware: step over
            // any trailing continuation bytes).
            if (after_len < static_cast<int>(p.text.size())) {
                do {
                    after_len++;
                } while (after_len < static_cast<int>(p.text.size()) &&
                         (static_cast<unsigned char>(p.text[after_len]) &
                          0xC0) == 0x80);
            }
            int after_w = measure_prefix(p.font, p.text, after_len);
            int target = (offset_px - before_w) < (after_w - offset_px)
                             ? static_cast<int>(measured_len)
                             : after_len;
            out.piece_index = idx;
            out.byte_offset = target;
            return out;
        }
    }

    // Click outside any piece on this line — snap to the nearest piece.
    int best = online[0];
    float best_d = 1e30f;
    for (int idx : online) {
        const auto& p = pieces_[idx];
        float cx = p.x + p.w / 2.0f;
        float d = std::abs(doc_x - cx);
        if (d < best_d) {
            best_d = d;
            best = idx;
        }
    }
    const auto& p = pieces_[best];
    out.piece_index = best;
    out.byte_offset =
        doc_x < p.x + p.w / 2.0f ? 0 : static_cast<int>(p.text.size());
    return out;
}

void Renderer::begin_selection(float screen_x, float screen_y) {
    float doc_x = screen_x;
    float doc_y = screen_y + scroll_y_;
    DocPos p = hit_test(doc_x, doc_y);
    if (!p.valid()) return;
    sel_anchor_ = p;
    sel_caret_ = p;
    selecting_ = true;
}

void Renderer::update_selection(float screen_x, float screen_y) {
    if (!selecting_) return;
    float doc_x = screen_x;
    float doc_y = screen_y + scroll_y_;
    DocPos p = hit_test(doc_x, doc_y);
    if (!p.valid()) return;
    sel_caret_ = p;
}

void Renderer::end_selection() {
    selecting_ = false;
}

void Renderer::clear_selection() {
    sel_anchor_ = {};
    sel_caret_ = {};
    selecting_ = false;
}

void Renderer::select_all() {
    int first = find_first_selectable();
    int last = find_last_selectable();
    if (first < 0 || last < 0) {
        clear_selection();
        return;
    }
    sel_anchor_ = {first, 0};
    sel_caret_ = {last, static_cast<int>(pieces_[last].text.size())};
    selecting_ = false;
}

void Renderer::select_word_at(float screen_x, float screen_y) {
    float doc_x = screen_x;
    float doc_y = screen_y + scroll_y_;
    DocPos p = hit_test(doc_x, doc_y);
    if (!p.valid()) return;
    const auto& piece = pieces_[p.piece_index];
    if (piece.text.empty()) return;
    sel_anchor_ = {p.piece_index, 0};
    sel_caret_ = {p.piece_index, static_cast<int>(piece.text.size())};
    selecting_ = false;
}

void Renderer::select_line_at(float screen_x, float screen_y) {
    float doc_x = screen_x;
    float doc_y = screen_y + scroll_y_;
    DocPos p = hit_test(doc_x, doc_y);
    if (!p.valid()) return;
    int line_id = pieces_[p.piece_index].line_id;
    int first = find_line_first_piece(line_id);
    int last = find_line_last_piece(line_id);
    if (first < 0 || last < 0) return;
    sel_anchor_ = {first, 0};
    sel_caret_ = {last, static_cast<int>(pieces_[last].text.size())};
    selecting_ = false;
}

void Renderer::extend_selection_to(float screen_x, float screen_y) {
    float doc_x = screen_x;
    float doc_y = screen_y + scroll_y_;
    DocPos p = hit_test(doc_x, doc_y);
    if (!p.valid()) return;
    if (!sel_anchor_.valid()) sel_anchor_ = p;
    sel_caret_ = p;
    selecting_ = true;
}

// ---------------------------------------------------------------------------
// Keyboard caret movement
// ---------------------------------------------------------------------------

void Renderer::caret_move_char(bool forward, bool extend) {
    if (!extend && has_selection()) {
        // Collapse selection in the direction of travel.
        DocPos s, e;
        normalized_selection(s, e);
        set_caret(forward ? e : s, /*extend=*/false);
        return;
    }
    ensure_caret();
    if (!sel_caret_.valid()) return;
    set_caret(step_char(sel_caret_, forward), extend);
}

void Renderer::caret_move_word(bool forward, bool extend) {
    if (!extend && has_selection()) {
        DocPos s, e;
        normalized_selection(s, e);
        set_caret(forward ? e : s, /*extend=*/false);
        return;
    }
    ensure_caret();
    if (!sel_caret_.valid()) return;
    int pi = sel_caret_.piece_index;
    DocPos target = sel_caret_;
    if (forward) {
        int next = pi + 1;
        while (next < static_cast<int>(pieces_.size()) &&
               pieces_[next].text.empty()) {
            next++;
        }
        if (next < static_cast<int>(pieces_.size())) {
            target = {next, 0};
        } else {
            target = {pi, static_cast<int>(pieces_[pi].text.size())};
        }
    } else {
        if (sel_caret_.byte_offset > 0) {
            target = {pi, 0};
        } else {
            int prev = pi - 1;
            while (prev >= 0 && pieces_[prev].text.empty()) prev--;
            if (prev >= 0) {
                target = {prev, 0};
            } else {
                target = {pi, 0};
            }
        }
    }
    set_caret(target, extend);
}

void Renderer::caret_move_line(bool down, bool extend) {
    ensure_caret();
    int cur_line = caret_line_id();
    if (cur_line < 0) return;

    int target_line = -1;
    for (const auto& p : pieces_) {
        if (p.text.empty()) continue;
        if (down) {
            if (p.line_id > cur_line &&
                (target_line < 0 || p.line_id < target_line)) {
                target_line = p.line_id;
            }
        } else {
            if (p.line_id < cur_line && p.line_id > target_line) {
                target_line = p.line_id;
            }
        }
    }
    if (target_line < 0) {
        // At the extremity — jump to line edge instead.
        int edge_line = cur_line;
        int edge_piece = down ? find_line_last_piece(edge_line)
                              : find_line_first_piece(edge_line);
        if (edge_piece < 0) return;
        DocPos p = {edge_piece, down ? static_cast<int>(
                                          pieces_[edge_piece].text.size())
                                     : 0};
        set_caret(p, extend);
        return;
    }

    float target_x = caret_x();

    DocPos best{-1, 0};
    float best_d = 1e30f;
    for (int i = 0; i < static_cast<int>(pieces_.size()); ++i) {
        const auto& p = pieces_[i];
        if (p.line_id != target_line || p.text.empty()) continue;
        if (target_x >= p.x && target_x < p.x + static_cast<float>(p.w)) {
            int off_px = static_cast<int>(target_x - p.x);
            int mw = 0;
            size_t mlen = 0;
            if (!TTF_MeasureString(p.font, p.text.c_str(), p.text.size(),
                                    off_px, &mw, &mlen)) {
                mlen = 0;
            }
            best = {i, static_cast<int>(mlen)};
            best_d = 0.0f;
            break;
        }
        float cx = p.x + static_cast<float>(p.w) / 2.0f;
        float d = std::abs(target_x - cx);
        if (d < best_d) {
            best_d = d;
            best = {i, target_x < p.x
                           ? 0
                           : static_cast<int>(p.text.size())};
        }
    }
    if (best.piece_index < 0) return;
    set_caret(best, extend);
}

void Renderer::caret_move_line_edge(bool end, bool extend) {
    ensure_caret();
    int cur_line = caret_line_id();
    if (cur_line < 0) return;
    int piece = end ? find_line_last_piece(cur_line)
                    : find_line_first_piece(cur_line);
    if (piece < 0) return;
    DocPos p{piece, end ? static_cast<int>(pieces_[piece].text.size()) : 0};
    set_caret(p, extend);
}

void Renderer::caret_move_doc_edge(bool end, bool extend) {
    int piece = end ? find_last_selectable() : find_first_selectable();
    if (piece < 0) return;
    DocPos p{piece, end ? static_cast<int>(pieces_[piece].text.size()) : 0};
    set_caret(p, extend);
}

bool Renderer::has_selection() const {
    DocPos s, e;
    normalized_selection(s, e);
    if (!s.valid() || !e.valid()) return false;
    return pos_less(s, e);
}

std::string Renderer::selection_text() const {
    DocPos s, e;
    normalized_selection(s, e);
    if (!s.valid() || !e.valid()) return "";
    if (!pos_less(s, e)) return "";

    std::string out;
    for (int i = s.piece_index; i <= e.piece_index && i < (int)pieces_.size();
         ++i) {
        const auto& p = pieces_[i];
        if (p.text.empty()) continue;
        int start_off = (i == s.piece_index) ? s.byte_offset : 0;
        int end_off = (i == e.piece_index) ? e.byte_offset
                                           : static_cast<int>(p.text.size());
        if (end_off <= start_off) continue;

        if (!out.empty()) {
            // Look back for a piece we actually emitted from to decide the
            // separator.  Walk backward to find the previous selected
            // piece with non-empty text.
            int prev = i - 1;
            while (prev >= s.piece_index && pieces_[prev].text.empty()) {
                --prev;
            }
            if (prev >= s.piece_index) {
                const auto& pp = pieces_[prev];
                bool same_row =
                    (p.y < pp.y + static_cast<float>(pp.h) &&
                     pp.y < p.y + static_cast<float>(p.h));
                if (!same_row) {
                    out += '\n';
                } else if (p.x > pp.x + static_cast<float>(pp.w) + 2.0f) {
                    out += ' ';
                }
            }
        }

        out.append(p.text, start_off, end_off - start_off);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void Renderer::render(int viewport_width, int viewport_height) {
    last_viewport_height_ = static_cast<float>(viewport_height);
    layout(viewport_width);

    // Background fill.
    SDL_SetRenderDrawColor(sdl_renderer_, theme_.background.r,
                           theme_.background.g, theme_.background.b,
                           theme_.background.a);
    SDL_FRect bgrect = {0, 0, static_cast<float>(viewport_width),
                        static_cast<float>(viewport_height)};
    SDL_RenderFillRect(sdl_renderer_, &bgrect);

    if (document_.blocks.empty()) return;

    // Clamp scroll.
    float max_scroll = total_height_ - static_cast<float>(viewport_height);
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (scroll_y_ > max_scroll) scroll_y_ = max_scroll;

    float vp_top = scroll_y_;
    float vp_bottom = scroll_y_ + static_cast<float>(viewport_height);

    auto in_view = [&](float y, float h) {
        return !(y + h < vp_top || y > vp_bottom);
    };

    // Backgrounds first.
    for (const auto& b : bgs_) {
        if (!in_view(b.y, b.h)) continue;
        SDL_FRect r = {b.x, b.y - scroll_y_, b.w, b.h};
        if (b.has_fill) {
            SDL_SetRenderDrawColor(sdl_renderer_, b.fill.r, b.fill.g, b.fill.b,
                                   b.fill.a);
            SDL_RenderFillRect(sdl_renderer_, &r);
        }
        if (b.has_border) {
            SDL_SetRenderDrawColor(sdl_renderer_, b.border.r, b.border.g,
                                   b.border.b, b.border.a);
            SDL_RenderRect(sdl_renderer_, &r);
        }
    }

    // Left borders (quote bars, etc.)
    for (const auto& lb : left_borders_) {
        if (!in_view(lb.y, lb.h)) continue;
        SDL_FRect r = {lb.x, lb.y - scroll_y_, lb.thickness, lb.h};
        SDL_SetRenderDrawColor(sdl_renderer_, lb.color.r, lb.color.g,
                               lb.color.b, lb.color.a);
        SDL_RenderFillRect(sdl_renderer_, &r);
    }

    // Horizontal rules.
    for (const auto& hr : hrs_) {
        if (!in_view(hr.y, 2.0f)) continue;
        SDL_FRect r = {hr.x, hr.y - scroll_y_, hr.w, 2.0f};
        SDL_SetRenderDrawColor(sdl_renderer_, hr.color.r, hr.color.g,
                               hr.color.b, hr.color.a);
        SDL_RenderFillRect(sdl_renderer_, &r);
    }

    // Selection highlight — drawn behind the text so glyphs stay legible.
    {
        DocPos s, e;
        normalized_selection(s, e);
        if (s.valid() && e.valid() && pos_less(s, e)) {
            SDL_SetRenderDrawBlendMode(sdl_renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(sdl_renderer_, 150, 180, 240, 120);
            for (int i = s.piece_index;
                 i <= e.piece_index && i < (int)pieces_.size(); ++i) {
                const auto& p = pieces_[i];
                if (p.text.empty()) continue;
                if (!in_view(p.y, static_cast<float>(p.h))) continue;
                int start_off = (i == s.piece_index) ? s.byte_offset : 0;
                int end_off = (i == e.piece_index)
                                  ? e.byte_offset
                                  : static_cast<int>(p.text.size());
                if (end_off <= start_off) continue;

                int prefix_w = measure_prefix(p.font, p.text, start_off);
                int upto_w = measure_prefix(p.font, p.text, end_off);
                float hx = p.x + static_cast<float>(prefix_w);
                float hw = static_cast<float>(upto_w - prefix_w);
                if (hw <= 0.0f) continue;
                SDL_FRect r = {hx, p.y - scroll_y_, hw,
                               static_cast<float>(p.h)};
                SDL_RenderFillRect(sdl_renderer_, &r);
            }
        }
    }

    // Text pieces.
    for (const auto& p : pieces_) {
        if (!in_view(p.y, static_cast<float>(p.h))) continue;
        SDL_FRect dst = {p.x, p.y - scroll_y_, static_cast<float>(p.w),
                         static_cast<float>(p.h)};
        SDL_RenderTexture(sdl_renderer_, p.texture, nullptr, &dst);
    }
}

}  // namespace mdpad
