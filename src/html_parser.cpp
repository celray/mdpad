#include "html_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace mdpad {

namespace {

// --- small string helpers --------------------------------------------------

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
    return s;
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && is_space(s[a])) a++;
    while (b > a && is_space(s[b - 1])) b--;
    return s.substr(a, b - a);
}

bool all_space(const std::string& s) {
    for (char c : s)
        if (!is_space(c)) return false;
    return true;
}

void append_utf8(std::string& out, long cp) {
    if (cp <= 0) return;
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
}

struct NamedEntity {
    const char* name;
    const char* utf8;
};

// The named entities that actually turn up in Markdown-embedded HTML.
const NamedEntity kEntities[] = {
    {"amp", "&"},       {"lt", "<"},        {"gt", ">"},
    {"quot", "\""},     {"apos", "'"},      {"nbsp", "\xc2\xa0"},
    {"copy", "\xc2\xa9"}, {"reg", "\xc2\xae"}, {"trade", "\xe2\x84\xa2"},
    {"hellip", "\xe2\x80\xa6"}, {"mdash", "\xe2\x80\x94"},
    {"ndash", "\xe2\x80\x93"}, {"lsquo", "\xe2\x80\x98"},
    {"rsquo", "\xe2\x80\x99"}, {"ldquo", "\xe2\x80\x9c"},
    {"rdquo", "\xe2\x80\x9d"}, {"bull", "\xe2\x80\xa2"},
    {"middot", "\xc2\xb7"}, {"times", "\xc3\x97"}, {"divide", "\xc3\xb7"},
    {"deg", "\xc2\xb0"}, {"plusmn", "\xc2\xb1"}, {"larr", "\xe2\x86\x90"},
    {"rarr", "\xe2\x86\x92"}, {"uarr", "\xe2\x86\x91"},
    {"darr", "\xe2\x86\x93"}, {"harr", "\xe2\x86\x94"},
    {"check", "\xe2\x9c\x93"}, {"cross", "\xe2\x9c\x97"},
    {"laquo", "\xc2\xab"}, {"raquo", "\xc2\xbb"}, {"sect", "\xc2\xa7"},
    {"para", "\xc2\xb6"}, {"dagger", "\xe2\x80\xa0"},
    {"Dagger", "\xe2\x80\xa1"}, {"euro", "\xe2\x82\xac"},
    {"pound", "\xc2\xa3"}, {"cent", "\xc2\xa2"}, {"yen", "\xc2\xa5"},
};

}  // namespace

std::string html_decode_entities(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] != '&') {
            out += s[i++];
            continue;
        }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 12) {
            out += s[i++];
            continue;
        }
        std::string body = s.substr(i + 1, semi - i - 1);
        bool handled = false;
        if (!body.empty() && body[0] == '#') {
            long cp = 0;
            if (body.size() > 1 && (body[1] == 'x' || body[1] == 'X')) {
                cp = std::strtol(body.c_str() + 2, nullptr, 16);
            } else {
                cp = std::strtol(body.c_str() + 1, nullptr, 10);
            }
            if (cp > 0) {
                append_utf8(out, cp);
                handled = true;
            }
        } else {
            for (const auto& e : kEntities) {
                if (body == e.name) {
                    out += e.utf8;
                    handled = true;
                    break;
                }
            }
        }
        if (handled) {
            i = semi + 1;
        } else {
            out += s[i++];
        }
    }
    return out;
}

namespace {

// --- tag parsing -----------------------------------------------------------

struct Tag {
    std::string name;                                    // lowercased
    bool is_end = false;
    bool self_closing = false;
    std::vector<std::pair<std::string, std::string>> attrs;  // name lowercased

    std::string attr(const std::string& key) const {
        for (const auto& a : attrs)
            if (a.first == key) return a.second;
        return std::string();
    }
    bool has_attr(const std::string& key) const {
        for (const auto& a : attrs)
            if (a.first == key) return true;
        return false;
    }
};

// Parse a raw "<...>" token into a Tag.  Forgiving: tolerates unquoted values,
// extra whitespace, and missing closing details.
Tag parse_tag(const std::string& token) {
    Tag t;
    size_t n = token.size();
    if (n < 2 || token[0] != '<') return t;
    size_t i = 1;
    size_t end = (token.back() == '>') ? n - 1 : n;
    if (i < end && token[i] == '/') {
        t.is_end = true;
        i++;
    }
    size_t name_start = i;
    while (i < end && !is_space(token[i]) && token[i] != '/' &&
           token[i] != '>') {
        i++;
    }
    t.name = to_lower(token.substr(name_start, i - name_start));

    // Attributes.
    while (i < end) {
        while (i < end && is_space(token[i])) i++;
        if (i < end && token[i] == '/') {
            t.self_closing = true;
            i++;
            continue;
        }
        if (i >= end) break;
        size_t key_start = i;
        while (i < end && !is_space(token[i]) && token[i] != '=' &&
               token[i] != '/' && token[i] != '>') {
            i++;
        }
        std::string key = to_lower(token.substr(key_start, i - key_start));
        std::string val;
        while (i < end && is_space(token[i])) i++;
        if (i < end && token[i] == '=') {
            i++;
            while (i < end && is_space(token[i])) i++;
            if (i < end && (token[i] == '"' || token[i] == '\'')) {
                char q = token[i++];
                size_t v_start = i;
                while (i < end && token[i] != q) i++;
                val = token.substr(v_start, i - v_start);
                if (i < end) i++;  // closing quote
            } else {
                size_t v_start = i;
                while (i < end && !is_space(token[i]) && token[i] != '>') i++;
                val = token.substr(v_start, i - v_start);
            }
        }
        if (!key.empty()) t.attrs.emplace_back(key, html_decode_entities(val));
    }
    return t;
}

// --- colour parsing --------------------------------------------------------

struct NamedColor {
    const char* name;
    unsigned char r, g, b;
};

const NamedColor kColors[] = {
    {"black", 0, 0, 0},        {"white", 255, 255, 255},
    {"red", 220, 38, 38},      {"green", 22, 126, 75},
    {"blue", 37, 99, 235},     {"yellow", 202, 138, 4},
    {"orange", 234, 88, 12},   {"purple", 147, 51, 234},
    {"gray", 107, 114, 128},   {"grey", 107, 114, 128},
    {"silver", 192, 192, 192}, {"maroon", 128, 0, 0},
    {"olive", 128, 128, 0},    {"lime", 50, 205, 50},
    {"aqua", 6, 182, 212},     {"cyan", 6, 182, 212},
    {"teal", 15, 118, 110},    {"navy", 30, 58, 138},
    {"fuchsia", 217, 70, 239}, {"magenta", 217, 70, 239},
    {"pink", 236, 72, 153},    {"brown", 120, 72, 40},
    {"gold", 202, 138, 4},     {"indigo", 79, 70, 229},
    {"violet", 139, 92, 246},  {"crimson", 185, 28, 28},
};

bool parse_hex(const std::string& h, InlineColor& out) {
    auto hexval = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        c = static_cast<char>(std::tolower((unsigned char)c));
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        return -1;
    };
    if (h.size() == 3) {
        int r = hexval(h[0]), g = hexval(h[1]), b = hexval(h[2]);
        if (r < 0 || g < 0 || b < 0) return false;
        out.r = static_cast<unsigned char>(r * 17);
        out.g = static_cast<unsigned char>(g * 17);
        out.b = static_cast<unsigned char>(b * 17);
        return true;
    }
    if (h.size() == 6) {
        int v[6];
        for (int k = 0; k < 6; k++) {
            v[k] = hexval(h[k]);
            if (v[k] < 0) return false;
        }
        out.r = static_cast<unsigned char>(v[0] * 16 + v[1]);
        out.g = static_cast<unsigned char>(v[2] * 16 + v[3]);
        out.b = static_cast<unsigned char>(v[4] * 16 + v[5]);
        return true;
    }
    return false;
}

// Parse a CSS colour value ("#abc", "#aabbcc", "rgb(...)", or a name).
bool parse_color(const std::string& raw, InlineColor& out) {
    std::string v = trim(raw);
    if (v.empty()) return false;
    if (v[0] == '#') return parse_hex(v.substr(1), out);
    std::string low = to_lower(v);
    if (low.rfind("rgb", 0) == 0) {
        size_t lp = v.find('(');
        size_t rp = v.find(')', lp == std::string::npos ? 0 : lp);
        if (lp != std::string::npos && rp != std::string::npos) {
            int comp[3] = {0, 0, 0};
            int idx = 0;
            std::string cur;
            for (size_t i = lp + 1; i < rp && idx < 3; i++) {
                if (v[i] == ',') {
                    comp[idx++] = std::atoi(trim(cur).c_str());
                    cur.clear();
                } else {
                    cur += v[i];
                }
            }
            if (idx < 3 && !trim(cur).empty()) comp[idx++] = std::atoi(trim(cur).c_str());
            out.r = static_cast<unsigned char>(std::clamp(comp[0], 0, 255));
            out.g = static_cast<unsigned char>(std::clamp(comp[1], 0, 255));
            out.b = static_cast<unsigned char>(std::clamp(comp[2], 0, 255));
            return true;
        }
        return false;
    }
    for (const auto& c : kColors) {
        if (low == c.name) {
            out.r = c.r;
            out.g = c.g;
            out.b = c.b;
            return true;
        }
    }
    return false;
}

// Pull "color: VALUE" out of a style="..." attribute.
bool color_from_style(const std::string& style, InlineColor& out) {
    std::string low = to_lower(style);
    size_t p = 0;
    while ((p = low.find("color", p)) != std::string::npos) {
        // Skip "background-color" and other *-color properties.
        bool boundary = (p == 0) || low[p - 1] == ';' || is_space(low[p - 1]) ||
                        low[p - 1] == '{';
        size_t colon = low.find(':', p);
        if (boundary && colon != std::string::npos) {
            size_t semi = low.find(';', colon);
            std::string val = style.substr(
                colon + 1,
                (semi == std::string::npos ? style.size() : semi) - colon - 1);
            if (parse_color(val, out)) return true;
        }
        p += 5;
    }
    return false;
}

InlineColor color_for_span_tag(const Tag& t) {
    InlineColor c;
    c.none = true;
    std::string style = t.attr("style");
    if (!style.empty() && color_from_style(style, c)) {
        c.none = false;
        return c;
    }
    std::string col = t.attr("color");  // <font color>
    if (!col.empty() && parse_color(col, c)) {
        c.none = false;
        return c;
    }
    return c;  // none == true (inherits)
}

bool is_inline_tag(const std::string& name) {
    static const char* kInline[] = {
        "b",   "strong", "i",    "em",  "u",     "ins",  "s",    "strike",
        "del", "code",   "kbd",  "samp", "tt",   "var",  "mark", "sub",
        "sup", "small",  "big",  "a",   "span",  "font", "q",    "cite",
        "abbr", "dfn",   "img",  "wbr", "time",  "bdi",  "bdo",
    };
    for (const char* n : kInline)
        if (name == n) return true;
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Inline tag interpreter (shared with the Markdown inline path)
// ---------------------------------------------------------------------------

void html_handle_inline_tag(const std::string& token, InlineSink& sink) {
    Tag t = parse_tag(token);
    if (t.name.empty()) return;
    InlineState& st = *sink.state;
    int d = t.is_end ? -1 : 1;
    auto bump = [&](int& counter) {
        counter += d;
        if (counter < 0) counter = 0;
    };

    const std::string& n = t.name;
    if (n == "b" || n == "strong") {
        bump(st.bold);
    } else if (n == "i" || n == "em" || n == "cite" || n == "var" ||
               n == "dfn") {
        bump(st.italic);
    } else if (n == "u" || n == "ins") {
        bump(st.underline);
    } else if (n == "s" || n == "strike" || n == "del") {
        bump(st.strike);
    } else if (n == "code" || n == "kbd" || n == "samp" || n == "tt") {
        bump(st.code);
    } else if (n == "mark") {
        bump(st.mark);
    } else if (n == "br") {
        if (sink.emit_break) sink.emit_break(sink.ctx);
    } else if (n == "wbr" || n == "small" || n == "big" || n == "sub" ||
               n == "sup" || n == "abbr" || n == "time" || n == "bdi" ||
               n == "bdo") {
        // No distinct styling; render the contents inline.
    } else if (n == "q") {
        if (sink.emit_text)
            sink.emit_text(sink.ctx, t.is_end ? "\xe2\x80\x9d" : "\xe2\x80\x9c");
    } else if (n == "a") {
        if (t.is_end) {
            if (!st.links.empty()) st.links.pop_back();
        } else {
            st.links.push_back(t.attr("href"));
        }
    } else if (n == "span" || n == "font") {
        if (t.is_end) {
            if (!st.colors.empty()) st.colors.pop_back();
        } else {
            st.colors.push_back(color_for_span_tag(t));
        }
    } else if (n == "img") {
        if (sink.emit_image) {
            std::string alt = t.attr("alt");
            if (alt.empty()) alt = t.attr("title");
            sink.emit_image(sink.ctx, alt, t.attr("src"));
        }
    }
    // Unknown inline tags: ignored (their text content still flows through).
}

// ---------------------------------------------------------------------------
// Block-level HTML parser
// ---------------------------------------------------------------------------

namespace {

struct HtmlBuilder {
    std::vector<Block>& out;
    InlineState inl;

    Block cur;
    bool has_cur = false;
    BlockType cur_type = BlockType::Paragraph;

    std::vector<Align> align_stack;
    int quote_depth = 0;

    std::vector<bool> list_ordered;
    std::vector<int> list_index;
    int list_depth = 0;

    bool in_table = false;
    bool in_thead = false;
    Block table;
    bool has_table = false;
    int active_row = -1;
    int active_cell = -1;

    explicit HtmlBuilder(std::vector<Block>& o) : out(o) {}

    Align cur_align() const {
        return align_stack.empty() ? Align::Left : align_stack.back();
    }

    void flush() {
        if (has_cur) {
            // Drop trailing whitespace-only spans.
            while (!cur.spans.empty() && all_space(cur.spans.back().text) &&
                   !cur.spans.back().is_image) {
                cur.spans.pop_back();
            }
            if (!cur.spans.empty()) out.push_back(std::move(cur));
            cur = Block{};
            has_cur = false;
        }
    }

    std::vector<TextSpan>* table_cell_spans() {
        if (in_table && has_table && active_row >= 0 && active_cell >= 0 &&
            active_row < static_cast<int>(table.rows.size()) &&
            active_cell <
                static_cast<int>(table.rows[active_row].cells.size())) {
            return &table.rows[active_row].cells[active_cell].spans;
        }
        return nullptr;
    }

    std::vector<TextSpan>* target() {
        if (auto* cell = table_cell_spans()) return cell;
        if (!has_cur) {
            cur = Block{};
            cur.type = quote_depth > 0 ? BlockType::Quote : BlockType::Paragraph;
            cur.align = cur_align();
            cur_type = cur.type;
            has_cur = true;
        }
        return &cur.spans;
    }

    void push_span(TextSpan span) {
        auto* spans = target();
        if (!spans) return;
        if (!spans->empty()) {
            TextSpan& back = spans->back();
            if (back.bold == span.bold && back.italic == span.italic &&
                back.code == span.code &&
                back.strikethrough == span.strikethrough &&
                back.underline == span.underline && back.mark == span.mark &&
                back.is_image == span.is_image && !span.is_image &&
                back.link == span.link && back.has_color == span.has_color &&
                back.cr == span.cr && back.cg == span.cg &&
                back.cb == span.cb) {
                back.text += span.text;
                return;
            }
        }
        spans->push_back(std::move(span));
    }

    // Append text, collapsing runs of whitespace (we are not inside <pre>).
    // Leading / trailing whitespace is significant between inline elements,
    // so it is preserved as a single separating space when adjacent content
    // exists, and dropped at block edges.
    void add_text(const std::string& raw) {
        std::string decoded = html_decode_entities(raw);
        if (decoded.empty()) return;
        bool leading = is_space(decoded.front());
        bool trailing = is_space(decoded.back());

        std::string core;
        core.reserve(decoded.size());
        bool prev_space = false;
        for (char c : decoded) {
            if (is_space(c)) {
                prev_space = true;
            } else {
                if (prev_space && !core.empty()) core += ' ';
                prev_space = false;
                core += c;
            }
        }

        // Is there preceding visible content on this line that does not
        // already end in a space?  If so, a leading space is meaningful.
        const std::vector<TextSpan>* sp_vec = table_cell_spans();
        if (!sp_vec && has_cur) sp_vec = &cur.spans;
        bool needs_sep = false;
        if (sp_vec && !sp_vec->empty()) {
            const std::string& last = sp_vec->back().text;
            needs_sep = !last.empty() && !is_space(last.back());
        }

        std::string result;
        if (core.empty()) {
            if (!needs_sep) return;  // whitespace between block edges: drop
            result = " ";
        } else {
            if (leading && needs_sep) result = " ";
            result += core;
            if (trailing) result += " ";
        }

        TextSpan sp;
        inl.stamp(sp);
        sp.text = std::move(result);
        push_span(std::move(sp));
    }

    void add_break() {
        TextSpan sp;
        inl.stamp(sp);
        sp.text = "\n";
        push_span(std::move(sp));
    }

    void add_image(const std::string& alt, const std::string& src) {
        TextSpan sp;
        inl.stamp(sp);
        sp.is_image = true;
        sp.src = src;
        std::string label = alt;
        if (label.empty()) {
            // Fall back to the file name.
            size_t slash = src.find_last_of('/');
            label = (slash == std::string::npos) ? src : src.substr(slash + 1);
            size_t q = label.find('?');
            if (q != std::string::npos) label = label.substr(0, q);
        }
        if (label.empty()) label = "image";
        sp.text = label;
        push_span(std::move(sp));
    }

    void start_block(BlockType type, Align align) {
        flush();
        cur = Block{};
        cur.type = type;
        cur.align = align;
        cur_type = type;
        has_cur = true;
    }

    void emit_hr() {
        flush();
        Block hr;
        hr.type = BlockType::HorizontalRule;
        out.push_back(std::move(hr));
    }
};

// Thunks bridging HtmlBuilder to the shared inline-tag interpreter.
void sink_text(void* ctx, const std::string& text) {
    static_cast<HtmlBuilder*>(ctx)->add_text(text);
}
void sink_image(void* ctx, const std::string& alt, const std::string& src) {
    static_cast<HtmlBuilder*>(ctx)->add_image(alt, src);
}
void sink_break(void* ctx) {
    static_cast<HtmlBuilder*>(ctx)->add_break();
}

Align align_from_tag(const Tag& t, Align inherited) {
    std::string a = to_lower(trim(t.attr("align")));
    if (a == "center") return Align::Center;
    if (a == "right") return Align::Right;
    if (a == "left") return Align::Left;
    std::string style = to_lower(t.attr("style"));
    if (style.find("text-align") != std::string::npos) {
        if (style.find("center") != std::string::npos) return Align::Center;
        if (style.find("right") != std::string::npos) return Align::Right;
        if (style.find("left") != std::string::npos) return Align::Left;
    }
    return inherited;
}

int heading_level(const std::string& name) {
    if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6')
        return name[1] - '0';
    return 0;
}

BlockType heading_block(int level) {
    switch (level) {
    case 1: return BlockType::Heading1;
    case 2: return BlockType::Heading2;
    case 3: return BlockType::Heading3;
    case 4: return BlockType::Heading4;
    case 5: return BlockType::Heading5;
    default: return BlockType::Heading6;
    }
}

// Find the matching close of `name` starting at `from` (index just past the
// opening tag).  Returns npos if not found.  Used for <pre>/<script>/<style>
// where inner content should not be tokenised.
size_t find_close(const std::string& html, const std::string& name,
                  size_t from) {
    std::string needle = "</" + name;
    std::string low = to_lower(html);
    return low.find(needle, from);
}

// Capture a <pre> block's verbatim text, stripping an optional wrapping
// <code> and reading a language hint from a class="language-xxx".
void handle_pre(const std::string& html, size_t inner_start, size_t inner_end,
                const Tag& pre_tag, HtmlBuilder& b) {
    std::string inner = html.substr(inner_start, inner_end - inner_start);
    std::string lang;

    // class on <pre ...>
    auto lang_from_class = [](const std::string& cls) -> std::string {
        std::string low = to_lower(cls);
        size_t p = low.find("language-");
        if (p == std::string::npos) p = low.find("lang-");
        if (p == std::string::npos) return std::string();
        p = low.find('-', p);
        size_t q = p + 1;
        while (q < cls.size() && !is_space(cls[q])) q++;
        return cls.substr(p + 1, q - p - 1);
    };
    lang = lang_from_class(pre_tag.attr("class"));

    // Strip a leading <code ...> and trailing </code>.
    std::string trimmed = inner;
    {
        std::string low = to_lower(trimmed);
        size_t lt = low.find("<code");
        size_t first_non_space = 0;
        while (first_non_space < trimmed.size() &&
               is_space(trimmed[first_non_space]))
            first_non_space++;
        if (lt == first_non_space) {
            size_t gt = trimmed.find('>', lt);
            if (gt != std::string::npos) {
                Tag code_tag = parse_tag(trimmed.substr(lt, gt - lt + 1));
                if (lang.empty())
                    lang = lang_from_class(code_tag.attr("class"));
                trimmed = trimmed.substr(gt + 1);
                std::string low2 = to_lower(trimmed);
                size_t cc = low2.rfind("</code>");
                if (cc != std::string::npos) trimmed = trimmed.substr(0, cc);
            }
        }
    }

    // Drop a single leading newline left by "<pre>\n".
    if (!trimmed.empty() && trimmed[0] == '\n') trimmed.erase(0, 1);
    else if (trimmed.size() > 1 && trimmed[0] == '\r' && trimmed[1] == '\n')
        trimmed.erase(0, 2);

    b.flush();
    Block code;
    code.type = BlockType::CodeBlock;
    code.lang = trim(lang);
    TextSpan sp;
    sp.text = html_decode_entities(trimmed);
    code.spans.push_back(std::move(sp));
    b.out.push_back(std::move(code));
}

}  // namespace

void parse_html_block(const std::string& html, std::vector<Block>& out) {
    HtmlBuilder b(out);
    InlineSink sink;
    sink.state = &b.inl;
    sink.ctx = &b;
    sink.emit_text = sink_text;
    sink.emit_image = sink_image;
    sink.emit_break = sink_break;

    size_t i = 0;
    const size_t n = html.size();
    while (i < n) {
        if (html[i] != '<') {
            size_t start = i;
            while (i < n && html[i] != '<') i++;
            b.add_text(html.substr(start, i - start));
            continue;
        }

        // Comment / declaration.
        if (html.compare(i, 4, "<!--") == 0) {
            size_t e = html.find("-->", i + 4);
            i = (e == std::string::npos) ? n : e + 3;
            continue;
        }
        if (i + 1 < n && html[i + 1] == '!') {
            size_t e = html.find('>', i);
            i = (e == std::string::npos) ? n : e + 1;
            continue;
        }
        // Not a tag start (a stray '<').
        if (i + 1 >= n ||
            (!std::isalpha((unsigned char)html[i + 1]) && html[i + 1] != '/')) {
            b.add_text(html.substr(i, 1));
            i++;
            continue;
        }

        // Read the tag up to the matching '>', respecting quotes.
        size_t j = i + 1;
        char quote = 0;
        while (j < n) {
            char c = html[j];
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '>') {
                break;
            }
            j++;
        }
        std::string token = html.substr(i, (j < n ? j + 1 : n) - i);
        size_t after = (j < n) ? j + 1 : n;
        Tag t = parse_tag(token);
        i = after;

        if (t.name.empty()) continue;

        // Tags whose contents we skip wholesale.
        if (!t.is_end && (t.name == "script" || t.name == "style" ||
                          t.name == "head" || t.name == "title" ||
                          t.name == "template" || t.name == "noscript")) {
            size_t e = find_close(html, t.name, i);
            if (e == std::string::npos) {
                i = n;
            } else {
                size_t gt = html.find('>', e);
                i = (gt == std::string::npos) ? n : gt + 1;
            }
            continue;
        }

        // <pre>: capture verbatim.
        if (!t.is_end && t.name == "pre") {
            size_t e = find_close(html, "pre", i);
            size_t inner_end = (e == std::string::npos) ? n : e;
            handle_pre(html, i, inner_end, t, b);
            if (e == std::string::npos) {
                i = n;
            } else {
                size_t gt = html.find('>', e);
                i = (gt == std::string::npos) ? n : gt + 1;
            }
            continue;
        }

        if (is_inline_tag(t.name)) {
            html_handle_inline_tag(token, sink);
            continue;
        }

        // --- block-level tags ---
        int hl = heading_level(t.name);
        if (hl > 0) {
            if (t.is_end) {
                b.flush();
            } else {
                b.start_block(heading_block(hl), align_from_tag(t, b.cur_align()));
            }
            continue;
        }

        if (t.name == "p") {
            if (t.is_end) b.flush();
            else b.start_block(BlockType::Paragraph,
                               align_from_tag(t, b.cur_align()));
            continue;
        }

        if (t.name == "br") {
            b.add_break();
            continue;
        }

        if (t.name == "hr") {
            b.emit_hr();
            continue;
        }

        if (t.name == "center") {
            if (t.is_end) {
                b.flush();
                if (!b.align_stack.empty()) b.align_stack.pop_back();
            } else {
                b.flush();
                b.align_stack.push_back(Align::Center);
            }
            continue;
        }

        if (t.name == "div" || t.name == "section" || t.name == "article" ||
            t.name == "header" || t.name == "footer" || t.name == "main" ||
            t.name == "nav" || t.name == "aside" || t.name == "figure" ||
            t.name == "figcaption" || t.name == "details" ||
            t.name == "summary" || t.name == "dl" || t.name == "dt" ||
            t.name == "dd" || t.name == "address") {
            if (t.is_end) {
                b.flush();
                if ((t.name == "div" || t.name == "figure") &&
                    !b.align_stack.empty())
                    b.align_stack.pop_back();
                if (t.name == "summary") b.inl.bold = std::max(0, b.inl.bold - 1);
            } else {
                b.flush();
                if (t.name == "div" || t.name == "figure")
                    b.align_stack.push_back(align_from_tag(t, b.cur_align()));
                if (t.name == "summary") b.inl.bold++;
            }
            continue;
        }

        if (t.name == "blockquote") {
            if (t.is_end) {
                b.flush();
                if (b.quote_depth > 0) b.quote_depth--;
            } else {
                b.flush();
                b.quote_depth++;
            }
            continue;
        }

        if (t.name == "ul" || t.name == "ol") {
            if (t.is_end) {
                b.flush();
                if (b.list_depth > 0) {
                    b.list_depth--;
                    b.list_ordered.pop_back();
                    b.list_index.pop_back();
                }
            } else {
                b.flush();
                b.list_depth++;
                b.list_ordered.push_back(t.name == "ol");
                int start = 1;
                if (t.has_attr("start")) start = std::atoi(t.attr("start").c_str());
                b.list_index.push_back(start - 1);
            }
            continue;
        }

        if (t.name == "li") {
            if (t.is_end) {
                b.flush();
            } else {
                b.flush();
                b.cur = Block{};
                b.cur.type = BlockType::ListItem;
                b.cur.align = b.cur_align();
                b.cur.list_depth = b.list_depth > 0 ? b.list_depth : 1;
                if (!b.list_ordered.empty()) {
                    b.cur.ordered = b.list_ordered.back();
                    if (b.cur.ordered) {
                        b.list_index.back()++;
                        b.cur.list_index = b.list_index.back();
                    }
                }
                b.cur_type = BlockType::ListItem;
                b.has_cur = true;
            }
            continue;
        }

        // --- tables ---
        if (t.name == "table") {
            if (t.is_end) {
                if (b.in_table && b.has_table) {
                    b.out.push_back(std::move(b.table));
                }
                b.in_table = false;
                b.has_table = false;
                b.table = Block{};
                b.active_row = -1;
                b.active_cell = -1;
                b.in_thead = false;
            } else {
                b.flush();
                b.in_table = true;
                b.has_table = true;
                b.table = Block{};
                b.table.type = BlockType::Table;
                b.active_row = -1;
                b.active_cell = -1;
                b.in_thead = false;
            }
            continue;
        }
        if (t.name == "thead") {
            b.in_thead = !t.is_end;
            continue;
        }
        if (t.name == "tbody" || t.name == "tfoot") {
            b.in_thead = false;
            continue;
        }
        if (t.name == "tr") {
            if (!t.is_end && b.in_table && b.has_table) {
                b.table.rows.emplace_back();
                b.table.rows.back().is_header = b.in_thead;
                b.active_row = static_cast<int>(b.table.rows.size()) - 1;
                b.active_cell = -1;
            } else if (t.is_end) {
                b.active_cell = -1;
            }
            continue;
        }
        if (t.name == "th" || t.name == "td") {
            if (!t.is_end && b.in_table && b.has_table && b.active_row >= 0 &&
                b.active_row < static_cast<int>(b.table.rows.size())) {
                if (t.name == "th") b.table.rows[b.active_row].is_header = true;
                b.table.rows[b.active_row].cells.emplace_back();
                b.active_cell =
                    static_cast<int>(b.table.rows[b.active_row].cells.size()) - 1;
            } else if (t.is_end) {
                b.active_cell = -1;
            }
            continue;
        }

        // Any other block-ish tag: treat as a soft boundary.
        b.flush();
    }

    // Close any dangling table / block.
    if (b.in_table && b.has_table) b.out.push_back(std::move(b.table));
    b.flush();
}

}  // namespace mdpad
