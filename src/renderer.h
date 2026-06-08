#pragma once

#include "markdown_parser.h"
#include "theme.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <string>
#include <vector>

namespace mdpad {

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(SDL_Renderer* sdl_renderer, const std::string& font_dir,
              float base_font_size = 18.0f);

    void set_document(const Document& doc);
    void scroll(float delta_y);
    float get_scroll_y() const;
    void set_scroll_y(float y);
    void render(int viewport_width, int viewport_height);

    const Theme& theme() const { return theme_; }
    void set_theme(const Theme& t);

    // Selection — screen coordinates are relative to the renderer's
    // viewport (tab bar excluded).  Use begin/update/end to drive a
    // drag selection from the event loop.
    void begin_selection(float screen_x, float screen_y);
    void update_selection(float screen_x, float screen_y);
    void end_selection();
    void clear_selection();
    void select_all();
    void select_word_at(float screen_x, float screen_y);   // double-click
    void select_line_at(float screen_x, float screen_y);   // triple-click
    void extend_selection_to(float screen_x, float screen_y);  // shift+click
    bool has_selection() const;
    bool is_selecting() const { return selecting_; }
    std::string selection_text() const;

    // Keyboard-driven caret movement.  `extend` = true means shift is
    // held (the anchor stays put, only the caret moves).
    void caret_move_char(bool forward, bool extend);
    void caret_move_word(bool forward, bool extend);
    void caret_move_line(bool down, bool extend);
    void caret_move_line_edge(bool end, bool extend);
    void caret_move_doc_edge(bool end, bool extend);

private:
    // A positioned, pre-rendered run of text.  x/y are in document
    // coordinates (pre-scroll).  We keep the source text + the font used
    // to render it so we can (a) measure sub-ranges for character-precise
    // selection with TTF_MeasureString, and (b) reconstruct plain text to
    // place on the clipboard.
    struct TextPiece {
        SDL_Texture* texture = nullptr;
        float x = 0.0f;
        float y = 0.0f;
        int w = 0;
        int h = 0;
        std::string text;
        TTF_Font* font = nullptr;
        int line_id = 0;
    };

public:
    struct DocPos {
        int piece_index = -1;
        int byte_offset = 0;
        bool valid() const { return piece_index >= 0; }
    };

private:

    struct BgBox {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        SDL_Color fill{};
        SDL_Color border{};
        bool has_fill = true;
        bool has_border = false;
    };

    struct HorizontalRuleBox {
        float x = 0.0f, y = 0.0f, w = 0.0f;
        SDL_Color color{};
    };

    struct LeftBorder {
        float x = 0.0f, y = 0.0f, h = 0.0f, thickness = 0.0f;
        SDL_Color color{};
    };

    void layout(int viewport_width);
    void clear_layout();

    TTF_Font* font_for_span(const Block& block, const TextSpan& span) const;
    SDL_Color color_for_block_text(BlockType type) const;
    float spacing_before(BlockType type) const;
    float spacing_after(BlockType type) const;

    // Layout helpers that append to the private piece/box vectors.
    // Return the vertical extent used (not including spacing_before).
    float layout_text_block(const Block& block, float x, float y,
                            float max_width);
    float layout_code_block(const Block& block, float x, float y,
                            float max_width);
    float layout_table(const Block& block, float x, float y,
                       float max_width);

    // Inline layout primitive — lays out a sequence of (text, font, color,
    // optional background) runs with word-wrapping within `max_width`.
    // Returns final y cursor (top of next line).
    struct InlineRun {
        std::string text;
        TTF_Font* font;
        SDL_Color color;
        bool has_bg = false;
        SDL_Color bg{};
        bool underline = false;
        bool strikethrough = false;
    };
    float layout_inline_runs(const std::vector<InlineRun>& runs, float x,
                             float y, float max_width,
                             Align align = Align::Left);

    // Build an inline run from a span, resolving colour/background/decoration
    // from the span's HTML attributes against the active theme.
    InlineRun run_from_span(const Block& block, const TextSpan& span,
                            SDL_Color default_color) const;

    SDL_Renderer* sdl_renderer_ = nullptr;

    TTF_Font* font_regular_ = nullptr;
    TTF_Font* font_bold_ = nullptr;
    TTF_Font* font_italic_ = nullptr;
    TTF_Font* font_bold_italic_ = nullptr;
    TTF_Font* font_h1_ = nullptr;
    TTF_Font* font_h2_ = nullptr;
    TTF_Font* font_h3_ = nullptr;
    TTF_Font* font_h4_ = nullptr;
    TTF_Font* font_code_ = nullptr;

    Theme theme_{};

    Document document_;
    float scroll_y_ = 0.0f;
    float total_height_ = 0.0f;
    int last_layout_width_ = 0;

    // Render data
    std::vector<BgBox> bgs_;
    std::vector<LeftBorder> left_borders_;
    std::vector<HorizontalRuleBox> hrs_;
    std::vector<TextPiece> pieces_;

    // Line bookkeeping — every commit_line during layout increments this
    // counter, and each piece records the id of its visual line.  Used for
    // hit-testing (snap click to nearest piece on the same line) and for
    // copy-paste text reconstruction (insert '\n' at line breaks).
    int next_line_id_ = 0;

    // Selection — anchor is the fixed end, caret is the moving end.
    // No selection means anchor == caret.  Both invalid = no caret yet.
    DocPos sel_anchor_{};
    DocPos sel_caret_{};
    bool selecting_ = false;

    // Remember the viewport height so keyboard caret movement can scroll
    // the caret into view without the render() loop being involved.
    float last_viewport_height_ = 0.0f;

    DocPos hit_test(float doc_x, float doc_y) const;
    static bool pos_less(const DocPos& a, const DocPos& b);
    void normalized_selection(DocPos& out_start, DocPos& out_end) const;
    void ensure_caret();
    DocPos step_char(DocPos p, bool forward) const;
    void set_caret(const DocPos& p, bool extend);
    void scroll_to_caret();
    int caret_line_id() const;
    float caret_x() const;
    int find_first_selectable() const;
    int find_last_selectable() const;
    int find_line_first_piece(int line_id) const;
    int find_line_last_piece(int line_id) const;
};

}  // namespace mdpad
