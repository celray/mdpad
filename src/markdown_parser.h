#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mdpad {

enum class BlockType {
    Paragraph,
    Heading1,
    Heading2,
    Heading3,
    Heading4,
    Heading5,
    Heading6,
    CodeBlock,
    Quote,
    ListItem,
    HorizontalRule,
    Table,
};

struct TextSpan {
    std::string text;
    bool bold = false;
    bool italic = false;
    bool code = false;
    bool strikethrough = false;
    bool underline = false;
    bool mark = false;          // highlighted background (<mark>)
    bool is_image = false;      // render as an image placeholder

    std::string link;           // hyperlink target (non-empty => link)
    std::string src;            // image source (when is_image)

    // Custom text colour (from <span style="color:..."> / <font color>).
    bool has_color = false;
    unsigned char cr = 0, cg = 0, cb = 0;
};

// Inline formatting state shared between the Markdown inline path and the
// HTML parser.  Counters (not bools) so nested identical tags balance.
struct InlineColor {
    unsigned char r = 0, g = 0, b = 0;
    bool none = false;      // a colour scope that sets no colour (inherits)
};

struct InlineState {
    int bold = 0, italic = 0, code = 0, strike = 0, underline = 0, mark = 0;
    std::vector<std::string> links;     // active <a href>, one per open <a>
    std::vector<InlineColor> colors;    // active colour scopes, one per <span>

    // Stamp the current formatting onto a span.
    void stamp(TextSpan& s) const {
        s.bold = bold > 0;
        s.italic = italic > 0;
        s.code = code > 0;
        s.strikethrough = strike > 0;
        s.underline = underline > 0;
        s.mark = mark > 0;
        for (auto it = links.rbegin(); it != links.rend(); ++it) {
            if (!it->empty()) {
                s.link = *it;
                s.underline = true;
                break;
            }
        }
        for (auto it = colors.rbegin(); it != colors.rend(); ++it) {
            if (!it->none) {
                s.has_color = true;
                s.cr = it->r;
                s.cg = it->g;
                s.cb = it->b;
                break;
            }
        }
    }
};

struct TableCell {
    std::vector<TextSpan> spans;
};

struct TableRow {
    std::vector<TableCell> cells;
    bool is_header = false;
};

enum class Align {
    Left,
    Center,
    Right,
};

struct Block {
    BlockType type = BlockType::Paragraph;
    std::vector<TextSpan> spans;

    // Horizontal alignment (from HTML align="..." / <center>).
    Align align = Align::Left;

    // Code block metadata
    std::string lang;

    // List item metadata
    int list_depth = 0;     // 1 for top-level list, 2 for nested, ...
    bool ordered = false;
    int list_index = 0;     // 1-based item number for ordered lists

    // Table metadata
    std::vector<TableRow> rows;
};

struct Document {
    std::vector<Block> blocks;
};

class MarkdownParser {
public:
    MarkdownParser();
    ~MarkdownParser();

    bool parse(const std::string& markdown_text, Document& out_doc);
    bool parse_file(const std::string& file_path, Document& out_doc);
};

}  // namespace mdpad
