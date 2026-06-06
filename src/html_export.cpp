#include "html_export.h"

#include "syntax_highlight.h"

#include <cstdio>
#include <sstream>

namespace mdpad {

namespace {

std::string html_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string escape_with_br(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\n') {
            out += "<br>\n";
            continue;
        }
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string color_to_css(const SDL_Color& c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
    return std::string(buf);
}

std::string spans_to_html(const std::vector<TextSpan>& spans) {
    std::string out;
    for (const auto& s : spans) {
        std::string t = escape_with_br(s.text);
        if (s.code) t = "<code>" + t + "</code>";
        if (s.strikethrough) t = "<del>" + t + "</del>";
        if (s.italic) t = "<em>" + t + "</em>";
        if (s.bold) t = "<strong>" + t + "</strong>";
        out += t;
    }
    return out;
}

const char* heading_tag(BlockType type) {
    switch (type) {
    case BlockType::Heading1: return "h1";
    case BlockType::Heading2: return "h2";
    case BlockType::Heading3: return "h3";
    case BlockType::Heading4: return "h4";
    case BlockType::Heading5: return "h5";
    case BlockType::Heading6: return "h6";
    default: return "p";
    }
}

std::string render_code_block(const Block& block, const Theme& theme) {
    std::string source;
    for (const auto& s : block.spans) source += s.text;
    if (!source.empty() && source.back() == '\n') source.pop_back();

    std::ostringstream ss;
    ss << "<pre";
    if (!block.lang.empty()) {
        ss << " data-lang=\"" << html_escape(block.lang) << "\"";
    }
    ss << "><code>";

    size_t line_start = 0;
    bool first_line = true;
    while (true) {
        size_t nl = source.find('\n', line_start);
        bool last = (nl == std::string::npos);
        std::string line = source.substr(
            line_start, last ? std::string::npos : nl - line_start);

        if (!first_line) ss << "\n";
        first_line = false;

        auto tokens = highlight_line(line, block.lang, theme);
        if (tokens.empty() && !line.empty()) {
            tokens.push_back({line, theme.code_text});
        }
        for (const auto& tok : tokens) {
            ss << "<span style=\"color:" << color_to_css(tok.color) << "\">"
               << html_escape(tok.text) << "</span>";
        }

        if (last) break;
        line_start = nl + 1;
    }
    ss << "</code></pre>\n";
    return ss.str();
}

std::string render_table(const Block& block) {
    std::ostringstream ss;
    ss << "<table>\n";
    bool in_thead = false;
    bool in_tbody = false;
    for (const auto& row : block.rows) {
        if (row.is_header) {
            if (in_tbody) { ss << "</tbody>\n"; in_tbody = false; }
            if (!in_thead) { ss << "<thead>\n"; in_thead = true; }
        } else {
            if (in_thead) { ss << "</thead>\n"; in_thead = false; }
            if (!in_tbody) { ss << "<tbody>\n"; in_tbody = true; }
        }
        ss << "<tr>";
        const char* tag = row.is_header ? "th" : "td";
        for (const auto& cell : row.cells) {
            ss << "<" << tag << ">" << spans_to_html(cell.spans) << "</"
               << tag << ">";
        }
        ss << "</tr>\n";
    }
    if (in_thead) ss << "</thead>\n";
    if (in_tbody) ss << "</tbody>\n";
    ss << "</table>\n";
    return ss.str();
}

}  // namespace

std::string document_to_html(const Document& doc, const Theme& theme,
                              const std::string& title) {
    std::ostringstream ss;
    ss << "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n";
    ss << "<title>" << html_escape(title) << "</title>\n";
    ss << "<style>\n";
    ss << "html,body { margin: 0; padding: 0; }\n";
    ss << "body { font-family: -apple-system, 'Segoe UI', Cantarell, "
          "'Helvetica Neue', Arial, sans-serif; ";
    ss << "background: " << color_to_css(theme.background) << "; ";
    ss << "color: " << color_to_css(theme.text) << "; ";
    ss << "max-width: 48em; margin: 2em auto; padding: 0 1.5em; "
          "line-height: 1.55; }\n";
    ss << "h1 { color: " << color_to_css(theme.h1) << "; }\n";
    ss << "h2 { color: " << color_to_css(theme.h2) << "; }\n";
    ss << "h3 { color: " << color_to_css(theme.h3) << "; }\n";
    ss << "h4 { color: " << color_to_css(theme.h4) << "; }\n";
    ss << "h5 { color: " << color_to_css(theme.h5) << "; }\n";
    ss << "h6 { color: " << color_to_css(theme.h6) << "; }\n";
    ss << "pre { background: " << color_to_css(theme.code_bg) << "; ";
    ss << "border: 1px solid " << color_to_css(theme.code_border) << "; ";
    ss << "padding: 0.75em 1em; overflow-x: auto; border-radius: 4px; ";
    ss << "font-family: 'JetBrains Mono', ui-monospace, Menlo, Consolas, "
          "monospace; font-size: 0.92em; line-height: 1.45; "
          "white-space: pre; }\n";
    ss << "code { background: " << color_to_css(theme.inline_code_bg) << "; ";
    ss << "color: " << color_to_css(theme.inline_code_text) << "; ";
    ss << "padding: 1px 4px; border-radius: 3px; ";
    ss << "font-family: 'JetBrains Mono', ui-monospace, Menlo, Consolas, "
          "monospace; font-size: 0.92em; }\n";
    ss << "pre code { background: none; color: inherit; padding: 0; "
          "border-radius: 0; font-size: 1em; }\n";
    ss << "blockquote { border-left: 4px solid "
       << color_to_css(theme.quote_border) << "; ";
    ss << "color: " << color_to_css(theme.quote_text)
       << "; padding: 0.2em 1em; margin: 1em 0; }\n";
    ss << "hr { border: none; border-top: 1px solid "
       << color_to_css(theme.hr) << "; margin: 2em 0; }\n";
    ss << "table { border-collapse: collapse; margin: 1em 0; width: 100%; "
          "font-size: 0.95em; }\n";
    ss << "th, td { border: 1px solid " << color_to_css(theme.code_border)
       << "; padding: 6px 10px; text-align: left; vertical-align: top; }\n";
    ss << "thead { background: " << color_to_css(theme.code_bg) << "; }\n";
    ss << "ul, ol { padding-left: 1.6em; }\n";
    ss << "li { margin: 0.2em 0; }\n";
    ss << "@media print {\n"
          "  body { max-width: none; margin: 0; padding: 0; "
          "background: white; }\n"
          "  pre, blockquote, table { page-break-inside: avoid; }\n"
          "  h1, h2, h3 { page-break-after: avoid; }\n"
          "}\n";
    ss << "</style>\n";
    // Fire the print dialog shortly after load so the page has had a
    // chance to lay out.  Small delay avoids blank previews in Firefox.
    ss << "<script>window.addEventListener('load',function(){"
          "setTimeout(function(){window.print();},250);});</script>\n";
    ss << "</head><body>\n";

    size_t i = 0;
    while (i < doc.blocks.size()) {
        const auto& b = doc.blocks[i];
        switch (b.type) {
        case BlockType::Paragraph:
            ss << "<p>" << spans_to_html(b.spans) << "</p>\n";
            i++;
            break;
        case BlockType::Heading1:
        case BlockType::Heading2:
        case BlockType::Heading3:
        case BlockType::Heading4:
        case BlockType::Heading5:
        case BlockType::Heading6: {
            const char* t = heading_tag(b.type);
            ss << "<" << t << ">" << spans_to_html(b.spans) << "</" << t
               << ">\n";
            i++;
            break;
        }
        case BlockType::CodeBlock:
            ss << render_code_block(b, theme);
            i++;
            break;
        case BlockType::Quote:
            ss << "<blockquote>" << spans_to_html(b.spans) << "</blockquote>\n";
            i++;
            break;
        case BlockType::ListItem: {
            // Group runs of consecutive list items with matching
            // ordered/depth into a single <ul>/<ol>.
            bool ordered = b.ordered;
            int depth = b.list_depth;
            ss << (ordered ? "<ol>\n" : "<ul>\n");
            while (i < doc.blocks.size() &&
                   doc.blocks[i].type == BlockType::ListItem &&
                   doc.blocks[i].ordered == ordered &&
                   doc.blocks[i].list_depth == depth) {
                ss << "  <li>" << spans_to_html(doc.blocks[i].spans)
                   << "</li>\n";
                i++;
            }
            ss << (ordered ? "</ol>\n" : "</ul>\n");
            break;
        }
        case BlockType::HorizontalRule:
            ss << "<hr>\n";
            i++;
            break;
        case BlockType::Table:
            ss << render_table(b);
            i++;
            break;
        }
    }

    ss << "</body></html>\n";
    return ss.str();
}

}  // namespace mdpad
