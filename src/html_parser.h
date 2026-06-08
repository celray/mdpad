#pragma once

#include "markdown_parser.h"

#include <string>
#include <vector>

namespace mdpad {

// Translate a block-level HTML fragment (the raw text md4c hands us for an
// HTML block) into renderable blocks, appended to `out`.  Handles the common
// structural tags (headings, paragraphs, lists, blockquotes, <pre>, tables,
// <hr>, <div>/<center> alignment) plus the inline tags below.
void parse_html_block(const std::string& html, std::vector<Block>& out);

// A sink the inline-tag interpreter writes through, so the same logic serves
// both the Markdown inline path (appending into the parser's current block)
// and the block HTML parser.  Plain function pointers + a ctx keep this free
// of <functional> and circular includes.
struct InlineSink {
    InlineState* state = nullptr;
    void* ctx = nullptr;
    void (*emit_text)(void* ctx, const std::string& text) = nullptr;
    void (*emit_image)(void* ctx, const std::string& alt,
                       const std::string& src) = nullptr;
    void (*emit_break)(void* ctx) = nullptr;
};

// Interpret one inline HTML tag token (e.g. "<b>", "</a>", "<br/>",
// "<img src=...>"), updating sink.state and emitting any content.  Block-level
// tokens that slip into an inline context are ignored.
void html_handle_inline_tag(const std::string& token, InlineSink& sink);

// Decode HTML entities (named + numeric &#NN; / &#xNN;) within `s`.
std::string html_decode_entities(const std::string& s);

}  // namespace mdpad
