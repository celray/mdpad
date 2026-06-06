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
};

struct TableCell {
    std::vector<TextSpan> spans;
};

struct TableRow {
    std::vector<TableCell> cells;
    bool is_header = false;
};

struct Block {
    BlockType type = BlockType::Paragraph;
    std::vector<TextSpan> spans;

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
