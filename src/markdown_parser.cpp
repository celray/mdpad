#include "markdown_parser.h"

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

    // Inline formatting flags.
    bool in_bold = false;
    bool in_italic = false;
    bool in_code = false;
    bool in_strikethrough = false;

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

static void append_text(ParseState* s, const char* text, size_t size) {
    auto* spans = target_spans(s);
    if (!spans) return;

    if (!spans->empty()) {
        auto& back = spans->back();
        if (back.bold == s->in_bold && back.italic == s->in_italic &&
            back.code == s->in_code && back.strikethrough == s->in_strikethrough) {
            back.text.append(text, size);
            return;
        }
    }

    TextSpan span;
    span.text.assign(text, size);
    span.bold = s->in_bold;
    span.italic = s->in_italic;
    span.code = s->in_code;
    span.strikethrough = s->in_strikethrough;
    spans->push_back(std::move(span));
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

static int on_enter_span(MD_SPANTYPE type, void* /*detail*/, void* userdata) {
    auto* s = static_cast<ParseState*>(userdata);
    switch (type) {
    case MD_SPAN_STRONG: s->in_bold = true; break;
    case MD_SPAN_EM: s->in_italic = true; break;
    case MD_SPAN_CODE: s->in_code = true; break;
    case MD_SPAN_DEL: s->in_strikethrough = true; break;
    default: break;
    }
    return 0;
}

static int on_leave_span(MD_SPANTYPE type, void* /*detail*/, void* userdata) {
    auto* s = static_cast<ParseState*>(userdata);
    switch (type) {
    case MD_SPAN_STRONG: s->in_bold = false; break;
    case MD_SPAN_EM: s->in_italic = false; break;
    case MD_SPAN_CODE: s->in_code = false; break;
    case MD_SPAN_DEL: s->in_strikethrough = false; break;
    default: break;
    }
    return 0;
}

static int on_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size,
                   void* userdata) {
    auto* s = static_cast<ParseState*>(userdata);

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
    case MD_TEXT_HTML:
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
