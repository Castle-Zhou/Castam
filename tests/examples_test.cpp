// 示例文件的端到端快照测试：完整解析 tests/examples/*.ham 并比对全文 dump
// 示例原样复制自 ~/Code/ham/examples/，HAM 文档变更时需重新复制

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "ast_dump.h"
#include "lexer.h"
#include "parser.h"

using namespace castam;

static int g_failures = 0;

static std::string readFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "cannot open: " << path << '\n';
        ++g_failures;
        return "";
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void checkExample(const char *name, const std::string &want) {
    std::string src = readFile(std::string(EXAMPLES_DIR) + "/" + name);
    if (src.empty())
        return;
    std::string got;
    try {
        got = dumpAst(*parse(lex(src)));
    } catch (const std::exception &e) {
        std::cerr << name << ": parse threw: " << e.what() << '\n';
        ++g_failures;
        return;
    }
    if (got != want) {
        std::cerr << name << ": dump mismatch:\n  got:  " << got << "\n  want: " << want
                  << '\n';
        ++g_failures;
    }
}

// 只断言示例能完整解析（用于全文 dump 过长、快照过脆的文件）
static void checkParses(const char *name) {
    std::string src = readFile(std::string(EXAMPLES_DIR) + "/" + name);
    if (src.empty())
        return;
    try {
        parse(lex(src));
    } catch (const std::exception &e) {
        std::cerr << name << ": parse threw: " << e.what() << '\n';
        ++g_failures;
    }
}

int main() {
    checkExample("sum.ham",
                 "(comb (decl<- sum (<| (int 0) (lambda (first (rest rest))"
                 " (+ (ident first) (call (ident sum) (spread (ident rest))))))))");

    checkExample("sort.ham",
                 "(comb (decl arr (array (int 1) (int 2) (int 3) (int 4) (int 5)))"
                 " (decl<- sort (lambda (arr)"
                 " (if (> (call (. (ident Array) length) (ident arr)) (int 0))"
                 " (+ (+ (call (ident sort) (| (ident arr) (lambda* (_)"
                 " (< (placeholder) (index (ident arr) (int 0))))))"
                 " (| (ident arr) (lambda* (_) (== (placeholder) (index (ident arr) (int 0))))))"
                 " (call (ident sort) (| (ident arr) (lambda* (_)"
                 " (> (placeholder) (index (ident arr) (int 0)))))))"
                 " (ident arr)))))");

    checkExample("counter.ham",
                 "(comb (decl<- #next (lambda ()"
                 " (<| (<| (comb (decl count (+ (. count) (int 1)))) (. count)) (ident #next))))"
                 " (decl createCounter (lambda (initCount)"
                 " (<| (<| (comb (decl count (ident initCount))) (. count)) (ident #next))))"
                 " (decl counter (call (ident createCounter) (int 0)))"
                 " (decl counter (call (ident counter)))"
                 " (decl counter (call (ident counter))))");

    checkExample("guess.ham",
                 "(comb (decl (destr os) (call (ident import) (str \"os\")))"
                 " (decl (destr cl) (call (ident import) (str \"cl\")))"
                 " (decl game (<| (lambda (os)"
                 " (comb (decl os (. (ident os) random)) (decl ans (. (ident os) value))"
                 " (decl<- guess (<| (lambda (os)"
                 " (comb (decl os (. (ident os) input)) (decl val (. (ident os) value))))"
                 " (if (== (. val) (ident ans))"
                 " (call (. (. os) output) (str \"You win!\\n\"))"
                 " (if (< (. val) (ident ans))"
                 " (call (ident guess) (call (. (. os) output) (str \"Too small.\\n\")))"
                 " (call (ident guess) (call (. (. os) output) (str \"Too Big.\\n\")))))))))"
                 " (call (. guess) (. os))))"
                 " (decl mainCl (call (ident cl) (call (ident game) (ident os)))))");

    checkParses("hexagons.ham");

    if (g_failures == 0) {
        std::cout << "examples_test: all tests passed\n";
    } else {
        std::cerr << "examples_test: " << g_failures << " failure(s)\n";
    }
    return g_failures == 0 ? 0 : 1;
}
