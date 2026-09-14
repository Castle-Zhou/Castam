#include "lexer.h"

#include <array>
#include <cctype>

namespace castam {

    LexError::LexError(const std::string &msg, int line, int col)
        : std::runtime_error(msg), line(line), col(col) {}

    const char *tokenKindName(TK kind) {
        switch (kind) {
        case TK::Eof:
            return "Eof";
        case TK::Ident:
            return "Ident";
        case TK::Underscore:
            return "Underscore";
        case TK::Backtick:
            return "Backtick";
        case TK::IntLit:
            return "IntLit";
        case TK::FloatLit:
            return "FloatLit";
        case TK::CharLit:
            return "CharLit";
        case TK::StrLit:
            return "StrLit";
        case TK::KwIf:
            return "KwIf";
        case TK::KwElse:
            return "KwElse";
        case TK::KwIn:
            return "KwIn";
        case TK::KwIs:
            return "KwIs";
        case TK::KwIsnt:
            return "KwIsnt";
        case TK::KwSubseteq:
            return "KwSubseteq";
        case TK::KwSubset:
            return "KwSubset";
        case TK::KwTrue:
            return "KwTrue";
        case TK::KwFalse:
            return "KwFalse";
        case TK::KwAs:
            return "KwAs";
        case TK::KwLet:
            return "KwLet";
        case TK::KwWhere:
            return "KwWhere";
        case TK::LParen:
            return "LParen";
        case TK::RParen:
            return "RParen";
        case TK::LBrace:
            return "LBrace";
        case TK::RBrace:
            return "RBrace";
        case TK::LBracket:
            return "LBracket";
        case TK::RBracket:
            return "RBracket";
        case TK::Comma:
            return "Comma";
        case TK::Colon:
            return "Colon";
        case TK::Dot:
            return "Dot";
        case TK::Ellipsis:
            return "Ellipsis";
        case TK::EllipsisAll:
            return "EllipsisAll";
        case TK::Eq:
            return "Eq";
        case TK::RecDecl:
            return "RecDecl";
        case TK::FatArrow:
            return "FatArrow";
        case TK::ThinArrow:
            return "ThinArrow";
        case TK::Dollar:
            return "Dollar";
        case TK::Delta:
            return "Delta";
        case TK::Pipe:
            return "Pipe";
        case TK::SetExt:
            return "SetExt";
        case TK::Bar:
            return "Bar";
        case TK::Amp:
            return "Amp";
        case TK::Tilde:
            return "Tilde";
        case TK::Bang:
            return "Bang";
        case TK::AndAnd:
            return "AndAnd";
        case TK::OrOr:
            return "OrOr";
        case TK::Plus:
            return "Plus";
        case TK::Minus:
            return "Minus";
        case TK::Star:
            return "Star";
        case TK::Slash:
            return "Slash";
        case TK::Percent:
            return "Percent";
        case TK::EqEq:
            return "EqEq";
        case TK::NotEq:
            return "NotEq";
        case TK::Lt:
            return "Lt";
        case TK::Gt:
            return "Gt";
        case TK::Le:
            return "Le";
        case TK::Ge:
            return "Ge";
        }
        return "?";
    }

    namespace {

        // HAM 0x00 的键名首字符：字母、# 或 _
        bool isIdentStart(char c) {
            return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') || c == '#' || c == '_';
        }

        // 键名的后续字符：首字符集加上数字
        bool isIdentCont(char c) { return isIdentStart(c) || (c >= '0' && c <= '9'); }

        // 只把有语法角色的词列为关键字（实现决策：文档里 import 和内置集合名
        // 没有特殊语法地位，按普通标识符处理）
        TK keywordKind(const std::string &s) {
            static const std::array<std::pair<const char *, TK>, 12> kKeywords{{
                {"if", TK::KwIf},
                {"else", TK::KwElse},
                {"in", TK::KwIn},
                {"is", TK::KwIs},
                {"isnt", TK::KwIsnt},
                {"subseteq", TK::KwSubseteq},
                {"subset", TK::KwSubset},
                {"true", TK::KwTrue},
                {"false", TK::KwFalse},
                {"as", TK::KwAs},
                {"let", TK::KwLet},
                {"where", TK::KwWhere},
            }};
            for (const auto &[word, kind] : kKeywords) {
                if (s == word)
                    return kind;
            }
            return TK::Ident;
        }

        class Lexer {
        public:
            explicit Lexer(const std::string &src) : src_(src) {}

            std::vector<Token> run() {
                while (!atEnd()) {
                    skipWhitespaceAndComments();
                    if (atEnd())
                        break;
                    char c = peek();
                    if (isIdentStart(c)) {
                        lexIdent();
                    } else if (c >= '0' && c <= '9') {
                        lexNumber();
                    } else if (c == '\'' || c == '"') {
                        lexQuoted(c);
                    } else if (c == '`') {
                        lexBacktick();
                    } else {
                        lexOperator();
                    }
                }
                push(TK::Eof, "", line_, col_);
                return std::move(tokens_);
            }

        private:
            const std::string &src_;
            size_t pos_ = 0;
            int line_ = 1;
            int col_ = 1;
            std::vector<Token> tokens_;

            bool atEnd() const { return pos_ >= src_.size(); }
            char peek(size_t ahead = 0) const {
                return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0';
            }

            char advance() {
                char c = src_[pos_++];
                if (c == '\n') {
                    ++line_;
                    col_ = 1;
                } else {
                    ++col_;
                }
                return c;
            }

            void push(TK kind, std::string text, int line, int col) {
                tokens_.push_back(Token{kind, std::move(text), line, col});
            }

            [[noreturn]] void error(const std::string &msg, int line, int col) const {
                throw LexError(msg, line, col);
            }

            void skipWhitespaceAndComments() {
                while (!atEnd()) {
                    char c = peek();
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                        advance();
                    } else if (c == '/' && peek(1) == '/') {
                        while (!atEnd() && peek() != '\n')
                            advance();
                    } else {
                        break;
                    }
                }
            }

            // 字符类规则见 isIdentStart/isIdentCont（HAM 0x00）
            // 单独的 `_` 是函数语法糖占位符（HAM 0x01），不是标识符
            void lexIdent() {
                int startLine = line_, startCol = col_;
                size_t start = pos_;
                while (!atEnd() && isIdentCont(peek()))
                    advance();
                std::string text = src_.substr(start, pos_ - start);
                if (text == "_") {
                    push(TK::Underscore, text, startLine, startCol);
                } else {
                    push(keywordKind(text), text, startLine, startCol);
                }
            }

            // 数字字面量：整数与小数
            // 负号不在这里处理，它是一元运算符（HAM 0x07），归 parser
            void lexNumber() {
                int startLine = line_, startCol = col_;
                size_t start = pos_;
                while (!atEnd() && peek() >= '0' && peek() <= '9')
                    advance();
                bool isFloat = false;
                // 只有小数点后跟数字时才按 float 处理
                // 这样 `1.x` 会切成 IntLit + Dot + Ident（投影，HAM 0x00）
                if (peek() == '.' && peek(1) >= '0' && peek(1) <= '9') {
                    isFloat = true;
                    advance(); // '.'
                    while (!atEnd() && peek() >= '0' && peek() <= '9')
                        advance();
                }
                push(isFloat ? TK::FloatLit : TK::IntLit, src_.substr(start, pos_ - start),
                     startLine, startCol);
            }

            // 消费反斜杠后的一个字符，返回转义结果
            char lexEscape() {
                int escLine = line_, escCol = col_;
                char c = advance();
                switch (c) {
                case 'n':
                    return '\n';
                case 't':
                    return '\t';
                case 'r':
                    return '\r';
                case '0':
                    return '\0';
                case '\\':
                    return '\\';
                case '\'':
                    return '\'';
                case '"':
                    return '"';
                default:
                    error(std::string("unknown escape sequence '\\") + c + "'", escLine,
                          escCol);
                }
            }

            void lexQuoted(char quote) {
                int startLine = line_, startCol = col_;
                advance(); // 开引号
                std::string value;
                while (true) {
                    if (atEnd() || peek() == '\n') {
                        error("unterminated string literal", startLine, startCol);
                    }
                    char c = peek();
                    if (c == quote) {
                        advance();
                        break;
                    }
                    if (c == '\\') {
                        advance();
                        value += lexEscape();
                    } else {
                        value += advance();
                    }
                }
                // HAM 0x03：'a' 是字符，"abc" 是字符串
                // 单引号字面量必须恰好一个字符，否则报词法错误
                if (quote == '\'') {
                    if (value.size() != 1) {
                        error("character literal must contain exactly one character",
                              startLine, startCol);
                    }
                    push(TK::CharLit, value, startLine, startCol);
                } else {
                    push(TK::StrLit, value, startLine, startCol);
                }
            }

            // 反引号是 `_` 语法糖的范围定界符（HAM 0x01/0x07），只发裸 token；
            // 成对嵌套与"对内至少一个 `_`"的判定需要试探解析，归 parser
            void lexBacktick() {
                int startLine = line_, startCol = col_;
                advance(); // '`'
                push(TK::Backtick, "`", startLine, startCol);
            }

            // 尝试在当前位置匹配 s；成功则消费它、压入 token 并返回 true
            bool matchOp(TK kind, const char *s) {
                size_t len = std::char_traits<char>::length(s);
                if (src_.compare(pos_, len, s) != 0)
                    return false;
                int startLine = line_, startCol = col_;
                for (size_t i = 0; i < len; ++i)
                    advance();
                push(kind, s, startLine, startCol);
                return true;
            }

            // 运算符表来自 HAM 0x07
            // 最长匹配优先（4、3、2、1 字符），保证 `<|` 不会被拆成 `<` 和 `|`
            void lexOperator() {
                int startLine = line_, startCol = col_;
                if (matchOp(TK::EllipsisAll, "...."))
                    return;
                // `..` 在 HAM 中没有含义：`.` 是投影（0x00），`...` 和 `....` 是
                // 参数包（0x05），直接报错并给出提示，而不是默默切成两个 Dot
                if (peek() == '.' && peek(1) == '.' && peek(2) != '.') {
                    error("`..` is not a valid token (did you mean `...`?)", startLine,
                          startCol);
                }
                if (matchOp(TK::Ellipsis, "..."))
                    return;
                if (matchOp(TK::FatArrow, "=>"))
                    return;
                if (matchOp(TK::ThinArrow, "->"))
                    return;
                if (matchOp(TK::RecDecl, "<-"))
                    return;
                if (matchOp(TK::Delta, "<|"))
                    return;
                if (matchOp(TK::Pipe, "|>"))
                    return;
                if (matchOp(TK::SetExt, "<~"))
                    return;
                if (matchOp(TK::EqEq, "=="))
                    return;
                if (matchOp(TK::NotEq, "!="))
                    return;
                if (matchOp(TK::Le, "<="))
                    return;
                if (matchOp(TK::Ge, ">="))
                    return;
                if (matchOp(TK::AndAnd, "&&"))
                    return;
                if (matchOp(TK::OrOr, "||"))
                    return;
                switch (peek()) {
                case '(':
                    advance();
                    push(TK::LParen, "(", startLine, startCol);
                    return;
                case ')':
                    advance();
                    push(TK::RParen, ")", startLine, startCol);
                    return;
                case '{':
                    advance();
                    push(TK::LBrace, "{", startLine, startCol);
                    return;
                case '}':
                    advance();
                    push(TK::RBrace, "}", startLine, startCol);
                    return;
                case '[':
                    advance();
                    push(TK::LBracket, "[", startLine, startCol);
                    return;
                case ']':
                    advance();
                    push(TK::RBracket, "]", startLine, startCol);
                    return;
                case ',':
                    advance();
                    push(TK::Comma, ",", startLine, startCol);
                    return;
                case ':':
                    advance();
                    push(TK::Colon, ":", startLine, startCol);
                    return;
                case '.':
                    advance();
                    push(TK::Dot, ".", startLine, startCol);
                    return;
                case '=':
                    advance();
                    push(TK::Eq, "=", startLine, startCol);
                    return;
                case '$':
                    advance();
                    push(TK::Dollar, "$", startLine, startCol);
                    return;
                case '|':
                    advance();
                    push(TK::Bar, "|", startLine, startCol);
                    return;
                case '&':
                    advance();
                    push(TK::Amp, "&", startLine, startCol);
                    return;
                case '~':
                    advance();
                    push(TK::Tilde, "~", startLine, startCol);
                    return;
                case '!':
                    advance();
                    push(TK::Bang, "!", startLine, startCol);
                    return;
                case '+':
                    advance();
                    push(TK::Plus, "+", startLine, startCol);
                    return;
                case '-':
                    advance();
                    push(TK::Minus, "-", startLine, startCol);
                    return;
                case '*':
                    advance();
                    push(TK::Star, "*", startLine, startCol);
                    return;
                case '/':
                    advance();
                    push(TK::Slash, "/", startLine, startCol);
                    return;
                case '%':
                    advance();
                    push(TK::Percent, "%", startLine, startCol);
                    return;
                case '<':
                    advance();
                    push(TK::Lt, "<", startLine, startCol);
                    return;
                case '>':
                    advance();
                    push(TK::Gt, ">", startLine, startCol);
                    return;
                default:
                    error(std::string("unexpected character '") + peek() + "'", startLine,
                          startCol);
                }
            }
        };

    } // namespace

    std::vector<Token> lex(const std::string &source) { return Lexer(source).run(); }

} // namespace castam
