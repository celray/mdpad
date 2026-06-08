#include "markdown_parser.h"

#include "html_parser.h"

#include <md4c.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace mdpad {

struct ParseState {
    Document* doc = nullptr;

    // Block nesting stack (so we know when a P is inside a LI/QUOTE).
    std::vector<MD_BLOCKTYPE> stack;

    // List context — parallel stacks for list depth tracking.
    std::vector<bool> list_ordered;
    std::vector<int> list_indices;
    int list_depth = 0;

    // The block we're currently accumulating text into.
    Block current;
    bool has_current = false;

    // Inline formatting state (bold/italic/code/strike + underline, links,
    // colours from inline HTML).  Shared with the HTML parser.
    InlineState inl;

    // Raw block-level HTML accumulates here between MD_BLOCK_HTML
    // enter/leave, then gets handed to the HTML parser.
    bool in_html_block = false;
    std::string html_buffer;

    // Image capture — while inside a Markdown image span, the alt text is
    // collected here instead of being added as ordinary spans.
    bool in_image = false;
    std::string image_alt;
    std::string image_src;

    // Table state — while inside a TH/TD, text should be routed into the
    // current cell instead of the block's top-level spans.  We store a
    // row/cell index (not a pointer) so vector reallocations don't
    // dangle between cells.
    bool in_table = false;
    bool in_table_header = false;
    int active_row = -1;
    int active_cell = -1;
};

static void flush_current(ParseState* s) {
    if (s->has_current) {
        s->doc->blocks.push_back(std::move(s->current));
        s->current = Block{};
        s->has_current = false;
    }
}

static std::vector<TextSpan>* target_spans(ParseState* s) {
    if (s->in_table && s->has_current && s->active_row >= 0 &&
        s->active_cell >= 0 &&
        s->active_row < static_cast<int>(s->current.rows.size()) &&
        s->active_cell <
            static_cast<int>(s->current.rows[s->active_row].cells.size())) {
        return &s->current.rows[s->active_row].cells[s->active_cell].spans;
    }

    if (!s->has_current) {
        s->current = Block{};
        s->current.type = BlockType::Paragraph;
        s->has_current = true;
    }
    return &s->current.spans;
}

static bool same_style(const TextSpan& a, const TextSpan& b) {
    return a.bold == b.bold && a.italic == b.italic && a.code == b.code &&
           a.strikethrough == b.strikethrough && a.underline == b.underline &&
           a.mark == b.mark && a.is_image == b.is_image && !a.is_image &&
           a.link == b.link && a.has_color == b.has_color && a.cr == b.cr &&
           a.cg == b.cg && a.cb == b.cb;
}

static void append_text(ParseState* s, const char* text, size_t size) {
    // Inside a Markdown image span, the text is the alt; capture it.
    if (s->in_image) {
        s->image_alt.append(text, size);
        return;
    }

    auto* spans = target_spans(s);
    if (!spans) return;

    TextSpan span;
    s->inl.stamp(span);
    span.text.assign(text, size);

    if (!spans->empty() && same_style(spans->back(), span)) {
        spans->back().text.append(text, size);
        return;
    }
    spans->push_back(std::move(span));
}

static std::string attr_to_string(const MD_ATTRIBUTE& a) {
    if (!a.text || a.size == 0) return std::string();
    return html_decode_entities(std::string(a.text, a.size));
}

// Push an image placeholder span (Markdown or inline-HTML images render the
// same way: the alt text, styled, with the source kept for later).
static void push_image(ParseState* s, const std::string& alt,
                       const std::string& src) {
    auto* spans = target_spans(s);
    if (!spans) return;
    TextSpan span;
    s->inl.stamp(span);
    span.is_image = true;
    span.src = src;
    std::string label = alt;
    if (label.empty()) {
        size_t slash = src.find_last_of('/');
        label = (slash == std::string::npos) ? src : src.substr(slash + 1);
        size_t q = label.find('?');
        if (q != std::string::npos) label = label.substr(0, q);
    }
    if (label.empty()) label = "image";
    span.text = label;
    spans->push_back(std::move(span));
}

// --- inline-HTML sink, bridging to the shared interpreter ------------------
static void sink_text(void* ctx, const std::string& text) {
    auto* s = static_cast<ParseState*>(ctx);
    append_text(s, text.data(), text.size());
}
static void sink_image(void* ctx, const std::string& alt,
                       const std::string& src) {
    push_image(static_cast<ParseState*>(ctx), alt, src);
}
static void sink_break(void* ctx) {
    const char nl = '\n';
    append_text(static_cast<ParseState*>(ctx), &nl, 1);
}

// Minimal HTML entity decoder — handles the common named entities plus
// numeric &#NN; / &#xNN; forms.  md4c passes entities through verbatim via
// MD_TEXT_ENTITY, so without this they would render as literal "&amp;" etc.
static std::string decode_entity(const char* text, size_t size) {
    std::string s(text, size);
    if (s == "&amp;") return "&";
    if (s == "&lt;") return "<";
    if (s == "&gt;") return ">";
    if (s == "&quot;") return "\"";
    if (s == "&apos;") return "'";
    if (s == "&nbsp;") return "\xc2\xa0";
    if (s == "&copy;") return "\xc2\xa9";
    if (s == "&reg;") return "\xc2\xae";
    if (s == "&trade;") return "\xe2\x84\xa2";
    if (s == "&hellip;") return "\xe2\x80\xa6";
    if (s == "&mdash;") return "\xe2\x80\x94";
    if (s == "&ndash;") return "\xe2\x80\x93";

    if (s.size() > 3 && s[0] == '&' && s[1] == '#' && s.back() == ';') {
        long cp = 0;
        if (s[2] == 'x' || s[2] == 'X') {
            cp = std::strtol(s.c_str() + 3, nullptr, 16);
        } else {
            cp = std::strtol(s.c_str() + 2, nullptr, 10);
        }
        std::string out;
        if (cp <= 0) return s;
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        return out;
    }
    return s;
}

static int on_enter_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto* s = static_cast<ParseState*>(userdata);
    s->stack.push_back(type);

    switch (type) {
    case MD_BLOCK_DOC:
        break;

    case MD_BLOCK_HTML:
        // Begin accumulating a block-level HTML run; its raw text arrives
        // through on_text and is parsed when the block closes.
        flush_current(s);
        s->in_html_block = true;
        s->html_buffer.clear();
        break;

    case MD_BLOCK_UL:
        s->list_depth++;
        s->list_ordered.push_back(false);
        s->list_indices.push_back(0);
        break;

    case MD_BLOCK_OL: {
        s->list_depth++;
        s->list_ordered.push_back(true);
        auto* d = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
        // We'll ++ before assigning, so start is (start - 1).
        s->list_indices.push_back(static_cast<int>(d->start) - 1);
        break;
    }

    case MD_BLOCK_LI:
        flush_current(s);
        s->current.type = BlockType::ListItem;
        s->current.list_depth = s->list_depth;
        if (!s->list_ordered.empty()) {
            s->current.ordered = s->list_ordered.back();
            if (s->current.ordered) {
                s->list_indices.back()++;
                s->current.list_index = s->list_indices.back();
            }
        }
        s->has_current = true;
        break;

    case MD_BLOCK_QUOTE:
        flush_current(s);
        s->current.type = BlockType::Quote;
        s->has_current = true;
        break;

    case MD_BLOCK_H: {
        flush_current(s);
        auto* d = static_cast<MD_BLOCK_H_DETAIL*>(detail);
        switch (d->level) {
        case 1: s->current.type = BlockType::Heading1; break;
        case 2: s->current.type = BlockType::Heading2; break;
        case 3: s->current.type = BlockType::Heading3; break;
        case 4: s->current.type = BlockType::Heading4; break;
        case 5: s->current.type = BlockType::Heading5; break;
        default: s->current.type = BlockType::Heading6; break;
        }
        s->has_current = true;
        break;
    }

    case MD_BLOCK_CODE: {
        flush_current(s);
        s->current.type = BlockType::CodeBlock;
        auto* d = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
        if (d->lang.text && d->lang.size > 0) {
            s->current.lang.assign(d->lang.text, d->lang.size);
        }
        s->has_current = true;
        break;
    }

    case MD_BLOCK_HR:
        flush_current(s);
        s->current.type = BlockType::HorizontalRule;
        s->doc->blocks.push_back(std::move(s->current));
        s->current = Block{};
        s->has_current = false;
        break;

    case MD_BLOCK_TABLE:
        flush_current(s);
        s->current.type = BlockType::Table;
        s->has_current = true;
        s->in_table = true;
        s->in_table_header = false;
        s->active_row = -1;
        s->active_cell = -1;
        break;

    case MD_BLOCK_THEAD:
        s->in_table_header = true;
        break;

    case MD_BLOCK_TBODY:
        s->in_table_header = false;
        break;

    case MD_BLOCK_TR:
        if (s->in_table && s->has_current &&
            s->current.type == BlockType::Table) {
            s->current.rows.emplace_back();
            s->current.rows.back().is_header = s->in_table_header;
            s->active_row = static_cast<int>(s->current.rows.size()) - 1;
            s->active_cell = -1;
        }
        break;

    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        if (s->in_table && s->has_current &&
            s->current.type == BlockType::Table && s->active_row >= 0 &&
            s->active_row < static_cast<int>(s->current.rows.size())) {
            s->current.rows[s->active_row].cells.emplace_back();
            s->active_cell =
                static_cast<int>(s->current.rows[s->active_row].cells.size()) -
                1;
        }
        break;

    case MD_BLOCK_P:
        // If we're nested inside a container (LI / QUOTE), the P's text
        // belongs to the container block.  Don't start a new block — but
        // insert a paragraph break if the container already has content.
        if (s->in_table) {
            // Paragraphs inside table cells don't start new blocks either.
            break;
        }
        if (s->has_current && (s->current.type == BlockType::ListItem ||
                               s->current.type == BlockType::Quote)) {
            if (!s->current.spans.empty()) {
                TextSpan br;
                br.text = "\n\n";
                s->current.spans.push_back(std::move(br));
            }
        } else {
            flush_current(s);
            s->current.type = BlockType::Paragraph;
            s->has_current = true;
        }
        break;

    default:
        break;
    }
    return 0;
}

static int on_leave_block(MD_BLOCKTYPE type, void* /*detail*/, void* userdata) {
    auto* s = static_cast<ParseState*>(userdata);
    if (!s->stack.empty()) s->stack.pop_back();

    switch (type) {
    case MD_BLOCK_HTML:
        parse_html_block(s->html_buffer, s->doc->blocks);
        s->in_html_block = false;
        s->html_buffer.clear();
        break;

    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        s->list_depth--;
        if (!s->list_ordered.empty()) s->list_ordered.pop_back();
        if (!s->list_indices.empty()) s->list_indices.pop_back();
        break;

    case MD_BLOCK_LI:
    case MD_BLOCK_QUOTE:
    case MD_BLOCK_H:
    case MD_BLOCK_CODE:
        flush_current(s);
        break;

    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        s->active_cell = -1;
        break;

    case MD_BLOCK_TR:
        s->active_row = -1;
        s->active_cell = -1;
        break;

    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
        s->in_table_header = false;
        break;

    case MD_BLOCK_TABLE:
        s->in_table = false;
        s->active_row = -1;
        s->active_cell = -1;
        flush_current(s);
        break;

    case MD_BLOCK_P: {
        // Only flush a paragraph block if it was created at the top level
        // (not absorbed into a LI / QUOTE / table cell).
        bool in_container = false;
        for (auto t : s->stack) {
            if (t == MD_BLOCK_LI || t == MD_BLOCK_QUOTE ||
                t == MD_BLOCK_TH || t == MD_BLOCK_TD) {
                in_container = true;
                break;
            }
        }
        if (!in_container) flush_current(s);
        break;
    }

    default:
        break;
    }
    return 0;
}

static int on_enter_span(MD_SPANTYPE type, void* detail, void* userdata) {
    auto* s = static_cast<ParseState*>(userdata);
    switch (type) {
    case MD_SPAN_STRONG: s->inl.bold++; break;
    case MD_SPAN_EM: s->inl.italic++; break;
    case MD_SPAN_CODE: s->inl.code++; break;
    case MD_SPAN_DEL: s->inl.strike++; break;
    case MD_SPAN_A: {
        auto* d = static_cast<MD_SPAN_A_DETAIL*>(detail);
        s->inl.links.push_back(attr_to_string(d->href));
        break;
    }
    case MD_SPAN_IMG: {
        auto* d = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
        s->in_image = true;
        s->image_alt.clear();
        s->image_src = attr_to_string(d->src);
        break;
    }
    default: break;
    }
    return 0;
}

static int on_leave_span(MD_SPANTYPE type, void* /*detail*/, void* userdata) {
    auto* s = static_cast<ParseState*>(userdata);
    switch (type) {
    case MD_SPAN_STRONG: if (s->inl.bold > 0) s->inl.bold--; break;
    case MD_SPAN_EM: if (s->inl.italic > 0) s->inl.italic--; break;
    case MD_SPAN_CODE: if (s->inl.code > 0) s->inl.code--; break;
    case MD_SPAN_DEL: if (s->inl.strike > 0) s->inl.strike--; break;
    case MD_SPAN_A:
        if (!s->inl.links.empty()) s->inl.links.pop_back();
        break;
    case MD_SPAN_IMG:
        s->in_image = false;
        push_image(s, s->image_alt, s->image_src);
        s->image_alt.clear();
        s->image_src.clear();
        break;
    default: break;
    }
    return 0;
}

static int on_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size,
                   void* userdata) {
    auto* s = static_cast<ParseState*>(userdata);

    // Inside a block-level HTML run, collect the raw bytes verbatim; they get
    // parsed as a unit when the block closes.
    if (s->in_html_block) {
        s->html_buffer.append(text, size);
        return 0;
    }

    switch (type) {
    case MD_TEXT_NULLCHAR: {
        // Substitute U+FFFD.
        const char replacement[] = "\xef\xbf\xbd";
        append_text(s, replacement, sizeof(replacement) - 1);
        break;
    }
    case MD_TEXT_BR: {
        const char nl = '\n';
        append_text(s, &nl, 1);
        break;
    }
    case MD_TEXT_SOFTBR: {
        // Soft break — allow wrap but don't force a new line.
        const char sp = ' ';
        append_text(s, &sp, 1);
        break;
    }
    case MD_TEXT_ENTITY: {
        std::string decoded = decode_entity(text, size);
        append_text(s, decoded.data(), decoded.size());
        break;
    }
    case MD_TEXT_HTML: {
        // Inline raw HTML (e.g. <kbd>, <br>, <span style=...>, <a>).
        InlineSink sink;
        sink.state = &s->inl;
        sink.ctx = s;
        sink.emit_text = sink_text;
        sink.emit_image = sink_image;
        sink.emit_break = sink_break;
        html_handle_inline_tag(std::string(text, size), sink);
        break;
    }
    case MD_TEXT_NORMAL:
    case MD_TEXT_CODE:
    case MD_TEXT_LATEXMATH:
    default:
        append_text(s, text, size);
        break;
    }
    return 0;
}

MarkdownParser::MarkdownParser() = default;
MarkdownParser::~MarkdownParser() = default;

bool MarkdownParser::parse(const std::string& markdown_text, Document& out_doc) {
    out_doc.blocks.clear();

    ParseState s;
    s.doc = &out_doc;

    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS;
    parser.enter_block = on_enter_block;
    parser.leave_block = on_leave_block;
    parser.enter_span = on_enter_span;
    parser.leave_span = on_leave_span;
    parser.text = on_text;

    int result = md_parse(markdown_text.c_str(),
                          static_cast<MD_SIZE>(markdown_text.size()), &parser, &s);

    // Safety: flush anything still pending.
    flush_current(&s);
    return result == 0;
}

bool MarkdownParser::parse_file(const std::string& file_path, Document& out_doc) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return parse(ss.str(), out_doc);
}

}  // namespace mdpad
