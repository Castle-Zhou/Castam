// parser 的快照测试：HAM 代码片段 → 期望的 S 表达式
// 覆盖优先级表、{} 四形态、pattern、lambda 全形态、`_` 糖、as、
// let/where、if、调用族、参数包，以及若干语法错误

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

// 包装：片段默认是完整的声明列表
static void P(const std::string &body, const std::string &inner) {
    checkParse(body, "(comb " + inner + ")");
}

// 优先级（HAM 0x07 附录）

static void testPrecedence() {
    P("x = a + b * c", "(decl x (+ (ident a) (* (ident b) (ident c))))");
    P("x = a <| b |> f()", "(decl x (|> (<| (ident a) (ident b)) (call (ident f))))");
    P("x = a || b && c", "(decl x (|| (ident a) (&& (ident b) (ident c))))");
    P("x = !a == b", "(decl x (== (not (ident a)) (ident b)))");
    // is 松于 <|（HAM 0x03）
    P("x = a is B <| c", "(decl x (is (ident a) (<| (ident B) (ident c))))");
    P("x = -a * b", "(decl x (* (neg (ident a)) (ident b)))");
    P("x = a <| b <| c", "(decl x (<| (<| (ident a) (ident b)) (ident c)))");
    P("x = a |> f() |> g()",
      "(decl x (|> (|> (ident a) (call (ident f))) (call (ident g))))");
    P("x = a is B isnt C", "(decl x (isnt (is (ident a) (ident B)) (ident C)))");
    // where 松于 <|，右侧吃掉整条 <| 链（HAM 0x01）
    P("r = x + y where comb <| { x = 3 }",
      "(decl r (tempcomb (<| (ident comb) (comb (decl x (int 3))))"
      " (+ (ident x) (ident y))))");
}

// {} 的四形态（HAM 0x02）与声明 pattern（HAM 0x00）

static void testBracesAndPatterns() {
    P("x = { a = 1, b = 2 }", "(decl x (comb (decl a (int 1)) (decl b (int 2))))");
    P("x = {}", "(decl x (comb))");
    P("s = { 0, 1, 2 }", "(decl s (enum (int 0) (int 1) (int 2)))");
    P("p = (e: {x: Num, y: Num}) => e.x",
      "(decl p (lambda ((typed e (combset (field x (ident Num)) (field y (ident Num)))))"
      " (. (ident e) x)))");
    P("e = {...|_ % 2 == 1 }",
      "(decl e (predset (lambda* (_) (== (% (placeholder) (int 2)) (int 1)))))");
    P("f = {...| x: Int -> Bool => x % 2 == 0 }",
      "(decl f (predset (lambda ((typed x (-> (ident Int) (ident Bool))))"
      " (== (% (ident x) (int 2)) (int 0)))))");

    P("comb.x = 2", "(decl (path comb x) (int 2))");
    P("comb.{a, b} = c", "(decl (path comb {a b}) (ident c))");
    P("{x, y} = comb", "(decl (destr x y) (ident comb))");
    P("`+` = (a, b) => a * b", "(decl (op +) (lambda (a b) (* (ident a) (ident b))))");
    P("r = `+`(a, b)", "(decl r (call (op +) (ident a) (ident b)))");

    // 逗号可选（HAM 0x00）
    P("x = 1 y = 2", "(decl x (int 1)) (decl y (int 2))");
    P("z = { a = 1 b = 2 }", "(decl z (comb (decl a (int 1)) (decl b (int 2))))");
}

// lambda 全形态（HAM 0x01/0x02/0x05/0x06）

static void testLambdas() {
    P("one = () => 1", "(decl one (lambda () (int 1)))");
    P("inc = x => x + 1", "(decl inc (lambda (x) (+ (ident x) (int 1))))");
    P("curriedAdd = (x) => (y) => x + y",
      "(decl curriedAdd (lambda (x) (lambda (y) (+ (ident x) (ident y)))))");
    P("inc = (x: Int) -> Int => x + 1",
      "(decl inc (lambda ((typed x (ident Int))) (ret (ident Int)) (+ (ident x) (int 1))))");
    P("inc = x: Int => x + 1",
      "(decl inc (lambda ((typed x (ident Int))) (+ (ident x) (int 1))))");
    P("muladd = <T: Num>(mul: T, x: T, y: T) -> T => (x + y) * mul",
      "(decl muladd (lambda (gen (T (ident Num)))"
      " ((typed mul (ident T)) (typed x (ident T)) (typed y (ident T))) (ret (ident T))"
      " (* (+ (ident x) (ident y)) (ident mul))))");
    P("getArgc = ...Args => Args", "(decl getArgc (lambda ((rest Args)) (ident Args)))");
    P("allSum = (....args) => sum(args....)",
      "(decl allSum (lambda ((restall args)) (call (ident sum) (spreadall (ident args)))))");
    P("sum2 = (head, ...rest) => head",
      "(decl sum2 (lambda (head (rest rest)) (ident head)))");
}

// `_` 糖与 as（HAM 0x01/0x02）

static void testSugarAndAs() {
    P("inc = _ + 1", "(decl inc (lambda* (_) (+ (placeholder) (int 1))))");
    P("r = arr | _ > 1", "(decl r (| (ident arr) (lambda* (_) (> (placeholder) (int 1)))))");
    P("fs = [_, -_]",
      "(decl fs (array (lambda* (_) (placeholder)) (lambda* (_) (neg (placeholder)))))");
    P("x = 1 as Int", "(decl x (as (int 1) (ident Int)))");
    P("y = { x = 1 } as { x: Int }",
      "(decl y (as (comb (decl x (int 1))) (combset (field x (ident Int)))))");
    // as 左侧的 `_` 先包成糖 lambda，类型标注作用于整个函数
    P("inc = _ + 1 as Int -> Int",
      "(decl inc (as (lambda* (_) (+ (placeholder) (int 1))) (-> (ident Int) (ident Int))))");
}

// let in / where / if（HAM 0x01/0x02）

static void testTempCombAndIf() {
    P("r = let { x = 1, y = 2 } in x + y",
      "(decl r (tempcomb (comb (decl x (int 1)) (decl y (int 2))) (+ (ident x) (ident y))))");
    P("a = if (c) 100", "(decl a (if (ident c) (int 100)))");
    P("b = if (c) 100 else 10", "(decl b (if (ident c) (int 100) (int 10)))");
    P("x = if (a) 1 else if (b) 2 else 3",
      "(decl x (if (ident a) (int 1) (if (ident b) (int 2) (int 3))))");
}

// 调用族与后缀（HAM 0x01/0x06）

static void testCallsAndPostfix() {
    P("c = add(a, b)", "(decl c (call (ident add) (ident a) (ident b)))");
    P("b = inc $ (a)", "(decl b (call$ (ident inc) (ident a)))");
    P("e = a |> inc() |> add(3)",
      "(decl e (|> (|> (ident a) (call (ident inc))) (call (ident add) (int 3))))");
    P("d = (x => x * x)(2)",
      "(decl d (call (lambda (x) (* (ident x) (ident x))) (int 2)))");
    P("i = arr[0]", "(decl i (index (ident arr) (int 0)))");
    P("l = arr.length()", "(decl l (call (. (ident arr) length)))");
    P("y = comb.x.y", "(decl y (. (. (ident comb) x) y))");
    P("t = Int[]", "(decl t (arrty (ident Int)))");
    P("t5 = Int[5]", "(decl t5 (index (ident Int) (int 5)))");
    P("s = sum(rest...)", "(decl s (call (ident sum) (spread (ident rest))))");
    P("x = (a)", "(decl x (ident a))");
    P("s = (a, b)", "(decl s (struct (ident a) (ident b)))");
    P("s = (a,)", "(decl s (struct (ident a)))");
}

// 语法错误

static void testErrors() {
    checkError("x: Int = 1");            // 顶层不是声明（旧类型标记已砍）
    checkError("x = { a = 1, b: Int }"); // {} 内混合声明与集合字段
    checkError("x = { 1, y = 2 }");      // {} 内混合表达式与声明
    checkError("x = <T> 1");             // 泛型后不是函数声明
    checkError("f = (1) => 2");          // 非法参数列表
    checkError("x = (1 + 2");            // 未闭合括号
    checkError("_ = 1");                 // _ 不能作为键名（HAM 0x00）
}

int main() {
    testPrecedence();
    testBracesAndPatterns();
    testLambdas();
    testSugarAndAs();
    testTempCombAndIf();
    testCallsAndPostfix();
    testErrors();

    if (g_failures == 0) {
        std::cout << "parser_test: all tests passed\n";
    } else {
        std::cerr << "parser_test: " << g_failures << " failure(s)\n";
    }
    return g_failures == 0 ? 0 : 1;
}
