#include "syntax_highlight.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace mdpad {

namespace {

struct LangSpec {
    std::unordered_set<std::string> keywords;
    std::unordered_set<std::string> builtins;
    std::unordered_set<std::string> types;
    std::string line_comment;   // e.g. "#" or "//"
    bool supports_hash_comment = false;
    bool supports_slash_comment = false;
    bool supports_dash_comment = false;   // -- (sql, lua)
    bool supports_triple_string = false;  // python """ / '''
    // For languages that use back-tick templates etc., can extend later.
};

static std::string lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

static const LangSpec& spec_for(const std::string& lang) {
    static const LangSpec python{
        {"def","class","if","elif","else","for","while","return","import",
         "from","as","with","try","except","finally","not","and","or","in",
         "is","pass","lambda","yield","global","nonlocal","raise","break",
         "continue","assert","del","async","await"},
        {"print","len","range","int","str","float","list","dict","set",
         "tuple","bool","bytes","open","input","type","isinstance","super",
         "self","cls","None","True","False","__init__","__main__","map",
         "filter","zip","enumerate","any","all","min","max","sum","abs",
         "round","sorted","reversed"},
        {},
        "#", true, false, false, true
    };
    static const LangSpec cpp{
        {"alignas","alignof","and","asm","auto","break","case","catch",
         "class","const","constexpr","const_cast","continue","default",
         "delete","do","dynamic_cast","else","enum","explicit","export",
         "extern","for","friend","goto","if","inline","mutable","namespace",
         "new","noexcept","not","operator","or","private","protected",
         "public","register","reinterpret_cast","return","sizeof","static",
         "static_cast","struct","switch","template","this","thread_local",
         "throw","try","typedef","typeid","typename","union","using",
         "virtual","volatile","while","nullptr","true","false","static_assert"},
        {"std","cout","cin","cerr","endl","printf","scanf","malloc","free",
         "memcpy","memset","strlen","strcpy","strcmp","fopen","fclose",
         "fprintf","NULL"},
        {"int","long","short","char","float","double","void","bool","size_t",
         "int8_t","int16_t","int32_t","int64_t","uint8_t","uint16_t",
         "uint32_t","uint64_t","string","vector","map","unordered_map",
         "unordered_set","set","pair","tuple","auto"},
        "//", false, true, false, false
    };
    static const LangSpec js{
        {"var","let","const","function","return","if","else","for","while",
         "do","break","continue","switch","case","default","new","delete",
         "typeof","instanceof","in","of","class","extends","super","this",
         "import","export","from","as","async","await","yield","try","catch",
         "finally","throw","null","undefined","true","false","void"},
        {"console","log","Math","Object","Array","String","Number","Boolean",
         "JSON","Promise","Map","Set","Symbol","Date","RegExp","Error",
         "window","document","globalThis","require","module","process"},
        {},
        "//", false, true, false, false
    };
    static const LangSpec rust{
        {"as","break","const","continue","crate","else","enum","extern",
         "false","fn","for","if","impl","in","let","loop","match","mod",
         "move","mut","pub","ref","return","self","Self","static","struct",
         "super","trait","true","type","unsafe","use","where","while",
         "async","await","dyn"},
        {"println","print","vec","panic","assert","assert_eq","format",
         "eprintln","eprint","Some","None","Ok","Err","Box","Rc","Arc",
         "String","Vec","HashMap","HashSet","Option","Result"},
        {"i8","i16","i32","i64","i128","isize","u8","u16","u32","u64",
         "u128","usize","f32","f64","bool","char","str"},
        "//", false, true, false, false
    };
    static const LangSpec go{
        {"break","case","chan","const","continue","default","defer","else",
         "fallthrough","for","func","go","goto","if","import","interface",
         "map","package","range","return","select","struct","switch","type",
         "var","true","false","nil","iota"},
        {"fmt","Println","Printf","Sprintf","Errorf","make","new","len",
         "cap","append","copy","delete","panic","recover","close"},
        {"int","int8","int16","int32","int64","uint","uint8","uint16",
         "uint32","uint64","uintptr","float32","float64","complex64",
         "complex128","string","bool","byte","rune","error"},
        "//", false, true, false, false
    };
    static const LangSpec shell{
        {"if","then","elif","else","fi","for","while","do","done","case",
         "esac","in","function","return","local","export","readonly",
         "declare","unset","break","continue","exit","source"},
        {"echo","printf","cd","ls","cat","grep","sed","awk","find","sort",
         "uniq","head","tail","wc","cut","tr","which","test","read"},
        {},
        "#", true, false, false, false
    };
    static const LangSpec sql{
        {"select","from","where","and","or","not","in","is","null","as",
         "join","inner","outer","left","right","full","on","group","by",
         "order","having","limit","offset","union","all","distinct",
         "insert","into","values","update","set","delete","create","drop",
         "alter","table","index","view","primary","key","foreign",
         "references","unique","default","constraint","case","when","then",
         "else","end","with","begin","commit","rollback","transaction"},
        {"count","sum","avg","min","max","coalesce","cast","substring",
         "length","upper","lower","trim","now","current_date",
         "current_timestamp"},
        {"int","integer","bigint","smallint","decimal","numeric","real",
         "double","precision","float","char","varchar","text","date","time",
         "timestamp","boolean","blob"},
        "--", false, false, true, false
    };
    static const LangSpec json_lang{
        {"true","false","null"}, {}, {}, "", false, false, false, false
    };
    static const LangSpec generic{
        {}, {}, {}, "", false, false, false, false
    };

    std::string k = lower(lang);
    if (k == "py" || k == "python") return python;
    if (k == "cpp" || k == "c++" || k == "cxx" || k == "c" || k == "cc" ||
        k == "h" || k == "hpp") return cpp;
    if (k == "js" || k == "javascript" || k == "ts" || k == "typescript" ||
        k == "jsx" || k == "tsx") return js;
    if (k == "rs" || k == "rust") return rust;
    if (k == "go" || k == "golang") return go;
    if (k == "sh" || k == "bash" || k == "shell" || k == "zsh") return shell;
    if (k == "sql") return sql;
    if (k == "json") return json_lang;
    return generic;
}

static bool is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

static bool is_ident_cont(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

}  // namespace

std::vector<SyntaxToken> highlight_line(const std::string& line,
                                        const std::string& lang,
                                        const Theme& theme) {
    std::vector<SyntaxToken> out;
    if (line.empty()) return out;

    const LangSpec& spec = spec_for(lang);

    auto push_token = [&](const std::string& text, SDL_Color color) {
        if (text.empty()) return;
        if (!out.empty() && out.back().color.r == color.r &&
            out.back().color.g == color.g && out.back().color.b == color.b &&
            out.back().color.a == color.a) {
            out.back().text += text;
        } else {
            out.push_back({text, color});
        }
    };

    size_t i = 0;
    const size_t n = line.size();

    while (i < n) {
        char c = line[i];

        // Line comments
        if (spec.supports_hash_comment && c == '#') {
            push_token(line.substr(i), theme.syn_comment);
            return out;
        }
        if (spec.supports_slash_comment && c == '/' && i + 1 < n &&
            line[i + 1] == '/') {
            push_token(line.substr(i), theme.syn_comment);
            return out;
        }
        if (spec.supports_dash_comment && c == '-' && i + 1 < n &&
            line[i + 1] == '-') {
            push_token(line.substr(i), theme.syn_comment);
            return out;
        }

        // Strings
        if (c == '"' || c == '\'' || c == '`') {
            char quote = c;
            size_t start = i;
            i++;
            // Python triple-quoted: peek for two more matching
            bool triple = false;
            if (spec.supports_triple_string && i + 1 < n &&
                line[i] == quote && line[i + 1] == quote) {
                triple = true;
                i += 2;
            }
            while (i < n) {
                if (!triple && line[i] == '\\' && i + 1 < n) {
                    i += 2;
                    continue;
                }
                if (triple) {
                    if (i + 2 < n && line[i] == quote &&
                        line[i + 1] == quote && line[i + 2] == quote) {
                        i += 3;
                        break;
                    }
                    i++;
                } else {
                    if (line[i] == quote) {
                        i++;
                        break;
                    }
                    i++;
                }
            }
            push_token(line.substr(start, i - start), theme.syn_string);
            continue;
        }

        // Numbers
        if (is_digit(c) ||
            (c == '.' && i + 1 < n && is_digit(line[i + 1]))) {
            size_t start = i;
            // Hex / oct / bin prefix
            if (c == '0' && i + 1 < n &&
                (line[i + 1] == 'x' || line[i + 1] == 'X' ||
                 line[i + 1] == 'b' || line[i + 1] == 'B' ||
                 line[i + 1] == 'o' || line[i + 1] == 'O')) {
                i += 2;
                while (i < n && (std::isalnum(static_cast<unsigned char>(line[i])) ||
                                 line[i] == '_')) {
                    i++;
                }
            } else {
                while (i < n && (is_digit(line[i]) || line[i] == '.' ||
                                 line[i] == '_')) {
                    i++;
                }
                // exponent
                if (i < n && (line[i] == 'e' || line[i] == 'E')) {
                    i++;
                    if (i < n && (line[i] == '+' || line[i] == '-')) i++;
                    while (i < n && is_digit(line[i])) i++;
                }
                // suffix (f, L, u, etc.)
                while (i < n && std::isalpha(static_cast<unsigned char>(line[i]))) {
                    i++;
                }
            }
            push_token(line.substr(start, i - start), theme.syn_number);
            continue;
        }

        // Identifiers / keywords
        if (is_ident_start(c)) {
            size_t start = i;
            while (i < n && is_ident_cont(line[i])) i++;
            std::string word = line.substr(start, i - start);

            SDL_Color color = theme.code_text;
            if (spec.keywords.count(word)) {
                color = theme.syn_keyword;
            } else if (spec.types.count(word)) {
                color = theme.syn_type;
            } else if (spec.builtins.count(word)) {
                color = theme.syn_builtin;
            } else {
                // Function call detection: identifier followed by '('
                size_t j = i;
                while (j < n && line[j] == ' ') j++;
                if (j < n && line[j] == '(') {
                    color = theme.syn_function;
                }
            }
            push_token(word, color);
            continue;
        }

        // Operators / punctuation — cluster adjacent ones.
        if (std::ispunct(static_cast<unsigned char>(c))) {
            size_t start = i;
            while (i < n) {
                char ch = line[i];
                if (ch == '"' || ch == '\'' || ch == '`' || ch == '_' ||
                    std::isalnum(static_cast<unsigned char>(ch)) ||
                    ch == ' ' || ch == '\t') {
                    break;
                }
                // Don't merge hash / slash-slash / dash-dash that would
                // start a comment — already handled above, but be safe.
                if (spec.supports_hash_comment && ch == '#') break;
                if (spec.supports_slash_comment && ch == '/' && i + 1 < n &&
                    line[i + 1] == '/') break;
                if (spec.supports_dash_comment && ch == '-' && i + 1 < n &&
                    line[i + 1] == '-') break;
                i++;
            }
            push_token(line.substr(start, i - start), theme.syn_operator);
            continue;
        }

        // Whitespace / fallback — accumulate as plain code text.
        size_t start = i;
        while (i < n) {
            char ch = line[i];
            if (is_ident_start(ch) || is_digit(ch) || ch == '"' ||
                ch == '\'' || ch == '`' ||
                std::ispunct(static_cast<unsigned char>(ch))) {
                break;
            }
            i++;
        }
        if (i == start) i++;  // safety: always advance
        push_token(line.substr(start, i - start), theme.code_text);
    }

    return out;
}

}  // namespace mdpad
