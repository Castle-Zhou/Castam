// 无第三方依赖的 lexer 测试
// CHECK 宏记录失败数，main 返回失败计数，ctest 据此判定

#include <iostream>
#include <string>
#include <vector>

#include "lexer.h"

using castam::TK;
using castam::Token;

static int g_failures = 0;

#define CHECK(cond)                                     \
    do {                                                \
        if (!(cond)) {                                  \
            std::cerr << __FILE__ << ':' << __LINE__    \
                      << ": CHECK failed: " #cond "\n"; \
            ++g_failures;                               \
        }                                               \
    } while (0)

// 对 src 做词法分析，返回 token 种类序列（含末尾的 Eof）
static std::vector<TK> kinds(const std::string &src) {
    std::vector<TK> out;
    for (const auto &t : castam::lex(src))
        out.push_back(t.kind);
    return out;
}

static bool sameKinds(const std::vector<TK> &got,
                      const std::vector<TK> &want) {
    if (got == want)
        return true;
    std::cerr << "kind sequence mismatch:\n  got:  ";
    for (TK k : got)
        std::cerr << castam::tokenKindName(k) << ' ';
    std::cerr << "\n  want: ";
    for (TK k : want)
        std::cerr << castam::tokenKindName(k) << ' ';
    std::cerr << '\n';
    return false;
}

#define CHECK_KINDS(src, ...) \
    CHECK(sameKinds(kinds(src), std::vector<TK>{__VA_ARGS__, TK::Eof}))

static bool lexThrows(const std::string &src, int wantLine, int wantCol) {
    try {
        castam::lex(src);
    } catch (const castam::LexError &e) {
        if (e.line != wantLine || e.col != wantCol) {
            std::cerr << "LexError at " << e.line << ':' << e.col << ", wanted "
                      << wantLine << ':' << wantCol << '\n';
            return false;
        }
        return true;
    }
    std::cerr << "expected LexError for: " << src << '\n';
    return false;
}

// 基础用例

static void testIdents() {
    CHECK_KINDS("abc x1 _private #next #curArgc a#b_c",
                TK::Ident, TK::Ident, TK::Ident, TK::Ident, TK::Ident,
                TK::Ident);
    CHECK_KINDS("_", TK::Underscore);
    CHECK_KINDS("_ + 1", TK::Underscore, TK::Plus, TK::IntLit);
    // 关键字（in 是 let in 的分隔符，集合判定为 is/isnt）
    CHECK_KINDS("if else in is isnt subseteq subset true false", TK::KwIf,
                TK::KwElse, TK::KwIn, TK::KwIs, TK::KwIsnt, TK::KwSubseteq,
                TK::KwSubset, TK::KwTrue, TK::KwFalse);
    // import 和内置集合名是普通标识符
    CHECK_KINDS("import Int Float Bool Anything Something Nothing Array",
                TK::Ident, TK::Ident, TK::Ident, TK::Ident, TK::Ident, TK::Ident,
                TK::Ident, TK::Ident);
    // 包含 in/is 的标识符不能被拆出关键字
    CHECK_KINDS("inc input inside issue island isnta", TK::Ident, TK::Ident,
                TK::Ident, TK::Ident, TK::Ident, TK::Ident);
}

static void testNumbers() {
    auto toks = castam::lex("0 42 3.14 1.0");
    CHECK(toks[0].kind == TK::IntLit && toks[0].text == "0");
    CHECK(toks[1].kind == TK::IntLit && toks[1].text == "42");
    CHECK(toks[2].kind == TK::FloatLit && toks[2].text == "3.14");
    CHECK(toks[3].kind == TK::FloatLit && toks[3].text == "1.0");
    // `1.x`：点后不是数字，按投影的 Dot 处理
    CHECK_KINDS("1.x", TK::IntLit, TK::Dot, TK::Ident);
    CHECK_KINDS("1 + 2.5", TK::IntLit, TK::Plus, TK::FloatLit);
}

static void testStrings() {
    auto toks = castam::lex(R"('a' "abc" "hi\n" '\'')");
    CHECK(toks[0].kind == TK::CharLit && toks[0].text == "a");
    CHECK(toks[1].kind == TK::StrLit && toks[1].text == "abc");
    CHECK(toks[2].kind == TK::StrLit && toks[2].text == "hi\n");
    CHECK(toks[3].kind == TK::CharLit && toks[3].text == "'");
    CHECK(toks[4].kind == TK::Eof);
}

// 运算符最长匹配

static void testOperators() {
    CHECK_KINDS("....", TK::EllipsisAll);
    CHECK_KINDS("...", TK::Ellipsis);
    CHECK_KINDS(".", TK::Dot);
    CHECK_KINDS("....x ...y .z", TK::EllipsisAll, TK::Ident, TK::Ellipsis,
                TK::Ident, TK::Dot, TK::Ident);
    CHECK_KINDS("<|", TK::Delta);
    CHECK_KINDS("<-", TK::RecDecl);
    CHECK_KINDS("<~", TK::SetExt);
    CHECK_KINDS("<=", TK::Le);
    CHECK_KINDS("<", TK::Lt);
    CHECK_KINDS("a<|b<-c<~d<=e<f<g", TK::Ident, TK::Delta, TK::Ident,
                TK::RecDecl, TK::Ident, TK::SetExt, TK::Ident, TK::Le, TK::Ident,
                TK::Lt, TK::Ident, TK::Lt, TK::Ident);
    CHECK_KINDS("|>", TK::Pipe);
    CHECK_KINDS("|", TK::Bar);
    CHECK_KINDS("||", TK::OrOr);
    CHECK_KINDS("a|>b|c||d", TK::Ident, TK::Pipe, TK::Ident, TK::Bar, TK::Ident,
                TK::OrOr, TK::Ident);
    CHECK_KINDS("->", TK::ThinArrow);
    CHECK_KINDS("=>", TK::FatArrow);
    CHECK_KINDS("-", TK::Minus);
    CHECK_KINDS("a->b=>c-d", TK::Ident, TK::ThinArrow, TK::Ident, TK::FatArrow,
                TK::Ident, TK::Minus, TK::Ident);
    CHECK_KINDS("==", TK::EqEq);
    CHECK_KINDS("=", TK::Eq);
    CHECK_KINDS("!= ! && & ~ $ , :", TK::NotEq, TK::Bang, TK::AndAnd, TK::Amp,
                TK::Tilde, TK::Dollar, TK::Comma, TK::Colon);
    CHECK_KINDS("( ) { } [ ]", TK::LParen, TK::RParen, TK::LBrace, TK::RBrace,
                TK::LBracket, TK::RBracket);
    CHECK_KINDS("+ - * / % > >=", TK::Plus, TK::Minus, TK::Star, TK::Slash,
                TK::Percent, TK::Gt, TK::Ge);
}

static void testBacktick() {
    // 反引号是裸定界 token（HAM 0x01），成对嵌套与 `_` 检查归 parser
    CHECK_KINDS("`_ + 1`", TK::Backtick, TK::Underscore, TK::Plus, TK::IntLit,
                TK::Backtick);
    CHECK_KINDS("`_ <| `_ + 1``", TK::Backtick, TK::Underscore, TK::Delta,
                TK::Backtick, TK::Underscore, TK::Plus, TK::IntLit, TK::Backtick,
                TK::Backtick);
    // 字符串内的反引号不参与配对
    CHECK_KINDS("\"`\"", TK::StrLit);
}

// 注释与行列号

static void testCommentsAndPositions() {
    std::string src =
        "a = 1, // first\n"
        "// whole line comment\n"
        "  b = 2,\n"
        "c = a + b // trailing";
    auto toks = castam::lex(src);
    CHECK(sameKinds(kinds(src), {TK::Ident, TK::Eq, TK::IntLit, TK::Comma,
                                 TK::Ident, TK::Eq, TK::IntLit, TK::Comma,
                                 TK::Ident, TK::Eq, TK::Ident, TK::Plus,
                                 TK::Ident, TK::Eof}));
    CHECK(toks[0].line == 1 && toks[0].col == 1);   // a
    CHECK(toks[4].line == 3 && toks[4].col == 3);   // b
    CHECK(toks[8].line == 4 && toks[8].col == 1);   // c
    CHECK(toks[12].line == 4 && toks[12].col == 9); // 行尾的 b
}

// 真实源码片段

static void testSumHamSnippet() {
    // sum.ham 第 1 行
    CHECK_KINDS("sum <- 0 <| (first, ...rest) => first + sum(rest...),",
                TK::Ident, TK::RecDecl, TK::IntLit, TK::Delta, TK::LParen,
                TK::Ident, TK::Comma, TK::Ellipsis, TK::Ident, TK::RParen,
                TK::FatArrow, TK::Ident, TK::Plus, TK::Ident, TK::LParen,
                TK::Ident, TK::Ellipsis, TK::RParen, TK::Comma);
}

static void testCounterHamSnippet() {
    // counter.ham 第 1 行
    CHECK_KINDS(
        "#next <- () => ({ count = .count + 1 } <| .count <| #next),",
        TK::Ident, TK::RecDecl, TK::LParen, TK::RParen, TK::FatArrow, TK::LParen,
        TK::LBrace, TK::Ident, TK::Eq, TK::Dot, TK::Ident, TK::Plus, TK::IntLit,
        TK::RBrace, TK::Delta, TK::Dot, TK::Ident, TK::Delta, TK::Ident,
        TK::RParen, TK::Comma);
}

// 新语法（as、泛型、返回值标注，HAM 0x02/0x06）

static void testNewSyntax() {
    // as 关键字（键的类型标记）：x = 1 as Int
    CHECK_KINDS("as", TK::KwAs);
    CHECK_KINDS("asInt has as_1", TK::Ident, TK::Ident, TK::Ident);
    CHECK_KINDS("x = 1 as Int", TK::Ident, TK::Eq, TK::IntLit, TK::KwAs,
                TK::Ident);
    CHECK_KINDS("inc = _ + 1 as Int -> Int", TK::Ident, TK::Eq, TK::Underscore,
                TK::Plus, TK::IntLit, TK::KwAs, TK::Ident, TK::ThinArrow,
                TK::Ident);

    // lambda 返回值标注：inc = (x: Int) -> Int => x + 1
    CHECK_KINDS("inc = (x: Int) -> Int => x + 1", TK::Ident, TK::Eq, TK::LParen,
                TK::Ident, TK::Colon, TK::Ident, TK::RParen, TK::ThinArrow,
                TK::Ident, TK::FatArrow, TK::Ident, TK::Plus, TK::IntLit);

    // 泛型：尖括号按普通的 Lt/Gt 切分，消歧是 parser 的事
    CHECK_KINDS("<T: U>(mul: T, x: T, y: T) -> T => (x + y) * mul",
                TK::Lt, TK::Ident, TK::Colon, TK::Ident, TK::Gt, TK::LParen,
                TK::Ident, TK::Colon, TK::Ident, TK::Comma, TK::Ident,
                TK::Colon, TK::Ident, TK::Comma, TK::Ident, TK::Colon,
                TK::Ident, TK::RParen, TK::ThinArrow, TK::Ident, TK::FatArrow,
                TK::LParen, TK::Ident, TK::Plus, TK::Ident, TK::RParen,
                TK::Star, TK::Ident);
    CHECK_KINDS("<T, U>(T[], (T) -> U) -> U[]", TK::Lt, TK::Ident, TK::Comma,
                TK::Ident, TK::Gt, TK::LParen, TK::Ident, TK::LBracket,
                TK::RBracket, TK::Comma, TK::LParen, TK::Ident, TK::RParen,
                TK::ThinArrow, TK::Ident, TK::RParen, TK::ThinArrow, TK::Ident,
                TK::LBracket, TK::RBracket);
}

// 临时组合（let/where，HAM 0x01）

static void testTempCombSyntax() {
    CHECK_KINDS("let { x = 1, y = 2 } in x + y", TK::KwLet, TK::LBrace,
                TK::Ident, TK::Eq, TK::IntLit, TK::Comma, TK::Ident, TK::Eq,
                TK::IntLit, TK::RBrace, TK::KwIn, TK::Ident, TK::Plus,
                TK::Ident);
    CHECK_KINDS("a where { a = 2 }", TK::Ident, TK::KwWhere, TK::LBrace,
                TK::Ident, TK::Eq, TK::IntLit, TK::RBrace);
    // 非字面量用法：where 后可接任意表达式（HAM 0x01）
    CHECK_KINDS("x + y where comb <| { x = 3 }", TK::Ident, TK::Plus, TK::Ident,
                TK::KwWhere, TK::Ident, TK::Delta, TK::LBrace, TK::Ident, TK::Eq,
                TK::IntLit, TK::RBrace);
    // 整词匹配：letx、wherever 仍是普通标识符
    CHECK_KINDS("letx wherever inlet", TK::Ident, TK::Ident, TK::Ident);
}

// 错误用例

static void testErrors() {
    CHECK(lexThrows("a = \"abc", 1, 5));         // 未闭合字符串
    CHECK(lexThrows("a = 'ab", 1, 5));           // 未闭合字符
    CHECK(lexThrows("x @ y", 1, 3));             // 非法字符
    CHECK(lexThrows("a .. b", 1, 3));            // `..`
    CHECK(lexThrows("x = \"a\\q\"", 1, 8));      // 未知转义（位于 q）
    CHECK(lexThrows("x = 'os'", 1, 5));          // 多字符单引号
    CHECK(lexThrows("x = ''", 1, 5));            // 空字符字面量
}

#define RUN(t)                                                     \
    do {                                                           \
        try {                                                      \
            t();                                                   \
        } catch (const castam::LexError &e) {                      \
            std::cerr << #t " threw LexError at " << e.line << ':' \
                      << e.col << ": " << e.what() << '\n';        \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

int main() {
    RUN(testIdents);
    RUN(testNumbers);
    RUN(testStrings);
    RUN(testOperators);
    RUN(testBacktick);
    RUN(testCommentsAndPositions);
    RUN(testSumHamSnippet);
    RUN(testCounterHamSnippet);
    RUN(testNewSyntax);
    RUN(testTempCombSyntax);
    RUN(testErrors);

    if (g_failures == 0) {
        std::cout << "lexer_test: all tests passed\n";
    } else {
        std::cerr << "lexer_test: " << g_failures << " failure(s)\n";
    }
    return g_failures == 0 ? 0 : 1;
}
