// 文档一致性测试：HAM 3.0 文档（ham-docs/0x00-0x08）写明的语法与解析器补全的
// 关键正反例集中固化，防止"文档写了、实现没跟上"再次退化
// 病态输入（深度/链长护栏、反引号回溯）也在此留档；与既有套件有意重复，作为基准

#include <iostream>
#include <string>

#include "ast_dump.h"
#include "lexer.h"
#include "parser.h"

using namespace castam;

static int g_failures = 0;

static void checkParse(const std::string &src, const std::string &want) {
    std::string got;
    try {
        got = dumpAst(*parse(lex(src)));
    } catch (const std::exception &e) {
        std::cerr << "parse threw for: " << src << "\n  " << e.what() << '\n';
        ++g_failures;
        return;
    }
    if (got != want) {
        std::cerr << "parse mismatch for: " << src << "\n  got:  " << got
                  << "\n  want: " << want << '\n';
        ++g_failures;
    }
}

static void checkError(const std::string &src) {
    try {
        parse(lex(src));
    } catch (const ParseError &) {
        return;
    }
    std::cerr << "expected ParseError for: " << src << '\n';
    ++g_failures;
}

static void P(const std::string &body, const std::string &inner) {
    checkParse(body, "(comb " + inner + ")");
}

static void checkExprParse(const std::string &src, const std::string &want) {
    std::string got;
    try {
        got = dumpAst(*parseExpression(lex(src)));
    } catch (const std::exception &e) {
        std::cerr << "parseExpression threw for: " << src << "\n  " << e.what() << '\n';
        ++g_failures;
        return;
    }
    if (got != want) {
        std::cerr << "parseExpression mismatch for: " << src << "\n  got:  " << got
                  << "\n  want: " << want << '\n';
        ++g_failures;
    }
}

static void checkExprError(const std::string &src) {
    try {
        parseExpression(lex(src));
    } catch (const ParseError &) {
        return;
    }
    std::cerr << "expected ParseError for: " << src << '\n';
    ++g_failures;
}

static std::string rep(const std::string &s, int n) {
    std::string r;
    for (int i = 0; i < n; ++i)
        r += s;
    return r;
}

// HAM 0x07：运算符是语法糖，a + b 与 #+(a, b) 等价；算子表是唯一权威

static void testOpTableConformance() {
    P("x = a + b", "(decl x (+ (ident a) (ident b)))");
    P("x = #+(a, b)", "(decl x (call (op +) (ident a) (ident b)))");
    // `-` 一键两面：二元减号与一元负号共键 `#-`
    P("x = a - b", "(decl x (- (ident a) (ident b)))");
    P("x = -a", "(decl x (neg (ident a)))");
    P("x = #-(a, b)", "(decl x (call (op -) (ident a) (ident b)))");
    P("x = #-(a)", "(decl x (call (op -) (ident a)))");
}

// HAM 0x07 附录第 9 档：`<=>` 三向比较，可中缀、可 `#` 引用（dijkstra.ham 用到）

static void testSpaceshipConformance() {
    P("x = a <=> b", "(decl x (<=> (ident a) (ident b)))");
    P("x = a <=> b <=> c", "(decl x (<=> (<=> (ident a) (ident b)) (ident c)))");
    P("x = #<=>(a, b)", "(decl x (call (op <=>) (ident a) (ident b)))");
    P("#<=> = #<=> <| (a, b) => a",
      "(decl (op <=>) (<| (op <=>) (lambda (a b) (ident a))))");
}

// HAM 0x01：用 `_` 代替参数、用反引号划定范围；裸 `_` 是语法错误

static void testBacktickConformance() {
    P("inc = `_ + 1`", "(decl inc (lambda* (_) (+ (placeholder) (int 1))))");
    // `_` 在反引号作用域内绑定外层糖参数，不是裸 `_`
    P("f = `x => _`", "(decl f (lambda* (_) (lambda (x) (placeholder))))");
    checkError("inc = _ + 1");
    checkError("fs = [_, -_]");
    checkError("e = {...|_ % 2 == 1 }");
    checkError("inc = _ + 1 as Int -> Int");
}

// HAM 0x01：函数的参数可以匹配组合；约束键的取值写 { x: { 1 } }

static void testDestructureParamConformance() {
    P("addxy = ({ x, y }) => x + y",
      "(decl addxy (lambda ((destr x y)) (+ (ident x) (ident y))))");
    P("play = ({ output, value }) => output",
      "(decl play (lambda ((destr output value)) (ident output)))");
    P("f = ({ x: { 1 } }) => x",
      "(decl f (lambda ((destr (x (enum (int 1))))) (ident x)))");
    checkError("f = ({x = 1}) => x"); // 组合模式参数不能写声明
}

// HAM 0x06：覆写由中括号拿到的引用；键/下标可混合（0x00 提取扩展的推广）

static void testIndexPathConformance() {
    P("arr3 = [1, 2, 3]\narr3[1] = 2",
      "(decl arr3 (array (int 1) (int 2) (int 3)))"
      " (decl (path arr3 [(int 1)]) (int 2))");
    P("a[0].b = 1", "(decl (path a [(int 0)] b) (int 1))");
    P("arr3[i + 1] = 2", "(decl (path arr3 [(+ (ident i) (int 1))]) (int 2))");
}

// HAM 0x06 过滤器 / 0x07 第 5、8 档：参数列表是受限文法，优先于中缀解析

static void testLambdaParamConformance() {
    P("a = arr2 | x => x > 1",
      "(decl a (| (ident arr2) (lambda (x) (> (ident x) (int 1)))))");
    // 回归：带返回值标注的形态不变
    P("inc = (x: Int) -> Int => x + 1",
      "(decl inc (lambda ((typed x (ident Int))) (ret (ident Int)) (+ (ident x) (int 1))))");
}

// HAM 0x00：组合与组合的集合表达式可省逗号；集合的列举不能省

static void testCommaConformance() {
    checkError("x = { 1 2 3 }"); // 集合元素之间需要逗号
    P("s = { 0, 1, 2 }", "(decl s (enum (int 0) (int 1) (int 2)))");
    P("s = { 0, 1, 2, }", "(decl s (enum (int 0) (int 1) (int 2)))");
    P("t = { x: Int y: Int }",
      "(decl t (combset (field x (ident Int)) (field y (ident Int))))");
    P("z = { a = 1 b = 2 }", "(decl z (comb (decl a (int 1)) (decl b (int 2))))");
    checkError("x = { _ = 4 }");       // `_` 不能作为键名（HAM 0x00）
    checkError("x = { comb.x: Int }"); // 组合集合的字段名必须是键名，不能是路径
}

// HAM 0x02：else 分支只吸收第 6 级及更紧的算符，其余作用于整个 if

static void testIfBoundaryConformance() {
    P("a = if (c) 1 else 2 where { c = true }",
      "(decl a (tempcomb (comb (decl c (bool true))) (if (ident c) (int 1) (int 2))))");
    P("b = if (c) 1 else \"a\" is Int",
      "(decl b (is (if (ident c) (int 1) (str \"a\")) (ident Int)))");
    P("f = a <| if (c) 1 else 2 <| b",
      "(decl f (<| (<| (ident a) (if (ident c) (int 1) (int 2))) (ident b)))");
    P("x = if (c) a is Int else b",
      "(decl x (if (ident c) (is (ident a) (ident Int)) (ident b)))");
    P("x = if (a) 1 else if (b) 2 else 3",
      "(decl x (if (ident a) (int 1) (if (ident b) (int 2) (int 3))))");
}

// HAM 0x08：--entry 的值是一个表达式；REPL 复用同一入口

static void testExpressionEntryConformance() {
    checkExprParse("2 |> `legs(_, 3)`",
                   "(|> (int 2) (lambda* (_) (call (ident legs) (placeholder) (int 3))))");
    checkExprParse("arr2 | `_ > 1`",
                   "(| (ident arr2) (lambda* (_) (> (placeholder) (int 1))))");
    checkExprError("a b");
    checkExprError("_ + 1");
    checkError("2 |> `legs(_, 3)`"); // .ham 顶层仍只允许声明
}

// 病态输入：必须干净抛 ParseError，不能崩溃/挂死（B1/B2）

static void testPathologicalConformance() {
    // 反引号候选回溯已消除：n 个开反引号瞬时未闭合
    checkError("x = " + std::string(200, '`') + " _");
    // 深嵌套（上限 128）
    checkError("x = " + std::string(5000, '-') + "1");
    checkError("x = " + std::string(5000, '(') + "1" + std::string(5000, ')'));
    checkError("x = " + rep("{a=", 5000) + "1" + rep("}", 5000));
    checkError("x = " + std::string(5000, '[') + std::string(5000, ']'));
    checkError("x = " + std::string(5000, '`'));
    // 扁平长链（上限 128）
    checkError("x = a" + rep(" <| a", 7999));
    // 最坏组合：128 层嵌套 × 每层 128 项链
    checkError("x = " + std::string(128, '(') + ("a" + rep(" <| a", 127)) +
               std::string(128, ')'));
}

int main() {
    testOpTableConformance();
    testSpaceshipConformance();
    testBacktickConformance();
    testDestructureParamConformance();
    testIndexPathConformance();
    testLambdaParamConformance();
    testCommaConformance();
    testIfBoundaryConformance();
    testExpressionEntryConformance();
    testPathologicalConformance();

    if (g_failures == 0) {
        std::cout << "doc_conformance_test: all tests passed\n";
    } else {
        std::cerr << "doc_conformance_test: " << g_failures << " failure(s)\n";
    }
    return g_failures == 0 ? 0 : 1;
}
