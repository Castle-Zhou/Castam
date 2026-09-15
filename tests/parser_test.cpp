// parser 的快照测试：HAM 代码片段 → 期望的 S 表达式
// 覆盖优先级表、{} 四形态、pattern、lambda 全形态、`_` 糖与反引号范围、as、
// let/where、if、调用族、参数包，以及若干语法错误

#include <iostream>
#include <string>

#include "ast_dump.h"
#include "lexer.h"
#include "op_table.h"
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

// parseExpression 入口（HAM 0x08）：裸表达式，无 comb 包装
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
    // 裸 `_` 必须由反引号定界（HAM 0x01；反引号形式的正例见 testBacktickScope）
    checkError("e = {...|_ % 2 == 1 }");
    P("f = {...| x: Int -> Bool => x % 2 == 0 }",
      "(decl f (predset (lambda ((typed x (-> (ident Int) (ident Bool))))"
      " (== (% (ident x) (int 2)) (int 0)))))");

    P("comb.x = 2", "(decl (path comb x) (int 2))");
    P("comb.{a, b} = c", "(decl (path comb {a b}) (ident c))");
    P("{x, y} = comb", "(decl (destr x y) (ident comb))");
    // 下标路径（HAM 0x06：覆写由中括号拿到的引用），键/下标可任意混合
    P("arr3 = [1, 2, 3]\narr3[1] = 2",
      "(decl arr3 (array (int 1) (int 2) (int 3)))"
      " (decl (path arr3 [(int 1)]) (int 2))");
    P("a[0].b = 1", "(decl (path a [(int 0)] b) (int 1))");
    P("a.b[0] = 1", "(decl (path a b [(int 0)]) (int 1))");
    P("arr3[i + 1] = 2", "(decl (path arr3 [(+ (ident i) (int 1))]) (int 2))");

    // 逗号可选（HAM 0x00）
    P("x = 1 y = 2", "(decl x (int 1)) (decl y (int 2))");
    P("z = { a = 1 b = 2 }", "(decl z (comb (decl a (int 1)) (decl b (int 2))))");
    // 组合的集合表达式（字段）同样可省逗号；列举集合元素之间必须逗号
    P("t = { x: Int y: Int }",
      "(decl t (combset (field x (ident Int)) (field y (ident Int))))");
    P("x = { 0, 1, 2, }", "(decl x (enum (int 0) (int 1) (int 2)))");
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
    // 组合解构参数（HAM 0x01：函数的参数可以匹配组合；HAM 0x08 的神谕签名）
    P("addxy = ({ x, y }) => x + y",
      "(decl addxy (lambda ((destr x y)) (+ (ident x) (ident y))))");
    P("play = ({ output, value }) => output",
      "(decl play (lambda ((destr output value)) (ident output)))");
    // 键的取值约束写成组合集合形式：({ x: { 1 } })
    P("f = ({ x: { 1 } }) => x",
      "(decl f (lambda ((destr (x (enum (int 1))))) (ident x)))");
    P("f = ({x, y}, z) => x", "(decl f (lambda ((destr x y) z) (ident x)))");
    // λ 的参数列表优先于中缀运算符解析（HAM 0x06 的过滤器写法；HAM 0x07 的 5/8 档）
    P("a = arr2 | x => x > 1",
      "(decl a (| (ident arr2) (lambda (x) (> (ident x) (int 1)))))");
    // 副作用（预期）：比 => 紧的算符右侧可直接接裸 λ
    P("f = a && x => x", "(decl f (&& (ident a) (lambda (x) (ident x))))");
}

// `_` 糖与 as（HAM 0x01/0x02）

static void testSugarAndAs() {
    // 裸 `_` 必须由反引号定界（HAM 0x01；反引号形式的正例见 testBacktickScope）
    checkError("inc = _ + 1");
    checkError("r = arr | _ > 1");
    checkError("fs = [_, -_]");
    P("x = 1 as Int", "(decl x (as (int 1) (ident Int)))");
    P("y = { x = 1 } as { x: Int }",
      "(decl y (as (comb (decl x (int 1))) (combset (field x (ident Int)))))");
    // as 不救裸 `_`：反引号定界后 as 才作用于整个函数（正例见 testBacktickScope）
    checkError("inc = _ + 1 as Int -> Int");
    // 类型位置同样允许 as（HAM 0x03）：as Set 吃到组合面、留下集合值面
    P("Env = { q: Heap(Pair(Int, Int)) as Set }",
      "(decl Env (combset (field q (as (call (ident Heap) (call (ident Pair) (ident Int)"
      " (ident Int))) (ident Set)))))");
    // as 松于 ->（HAM 0x07）：`A -> B as C` = `(A -> B) as C`，类型位置与表达式层一致
    P("Env = { q: Int -> Int as Set }",
      "(decl Env (combset (field q (as (-> (ident Int) (ident Int)) (ident Set)))))");
    P("x = Int -> Int as Set",
      "(decl x (as (-> (ident Int) (ident Int)) (ident Set)))");
}

// 反引号 `_` 范围（HAM 0x01）：显式定界的单参糖 lambda

static void testBacktickScope() {
    P("inc = `_ + 1`", "(decl inc (lambda* (_) (+ (placeholder) (int 1))))");
    // 嵌套：内层 `_` 绑定内层（HAM 0x01 的 xAndInc 示例）
    P("xAndInc = `_ <| `_ + 1``",
      "(decl xAndInc (lambda* (_) (<| (placeholder) (lambda* (_) (+ (placeholder) (int 1))))))");
    // 对内多个 `_` 绑同一个参数（与裸写法的逐元素包装不同）
    P("r = 2 |> `legs(_, _)`",
      "(decl r (|> (int 2) (lambda* (_) (call (ident legs) (placeholder) (placeholder)))))");
    P("f = Array.flatMap(arr2, `[_, -_]`)",
      "(decl f (call (. (ident Array) flatMap) (ident arr2)"
      " (lambda* (_) (array (placeholder) (neg (placeholder))))))");
    P("r = arr2 |> `map(_, `_ * 2`)`",
      "(decl r (|> (ident arr2)"
      " (lambda* (_) (call (ident map) (placeholder) (lambda* (_) (* (placeholder) (int 2)))))))");
    P("Odd = {...| `_ % 2 == 1` }",
      "(decl Odd (predset (lambda* (_) (== (% (placeholder) (int 2)) (int 1)))))");
    // scope 是 primary，可接后缀；as 作用于整个函数（HAM 0x03）
    P("y = `_+1` $ (3)", "(decl y (call$ (lambda* (_) (+ (placeholder) (int 1))) (int 3)))");
    P("inc = `_ + 1` as Int -> Int",
      "(decl inc (as (lambda* (_) (+ (placeholder) (int 1))) (-> (ident Int) (ident Int))))");
}

// B1：反引号单遍配对——嵌套未闭合反引号必须瞬时抛 ParseError（挂住即回归）

static void testBacktickRobustness() {
    // 文档 4.2 的指数爆炸形态：n 个开反引号 + `_`，全部未闭合
    checkError("x = " + std::string(8, '`') + " _");
    checkError("x = " + std::string(9, '`') + " _");
    checkError("x = " + std::string(10, '`') + " _");
    checkError("x = " + std::string(200, '`') + " _");
    // scope 内的真语法错误原样抛出（不吞成"未闭合"）
    checkError("x = `_ + * 2`");
    // `_` 在反引号作用域内绑定外层糖参数（HAM 0x01），不是裸 `_`
    P("f = `x => _`", "(decl f (lambda* (_) (lambda (x) (placeholder))))");
}

// `#` 运算符名（HAM 0x07）：白名单内的符号转成算子引用/声明目标

static void testOperatorRefs() {
    P("#+ = (a, b) => a * b",
      "(decl (op +) (lambda (a b) (* (ident a) (ident b))))");
    P("#+ <- (a, b) => a + b", "(decl<- (op +) (lambda (a b) (+ (ident a) (ident b))))");
    P("x = #+(a, b)", "(decl x (call (op +) (ident a) (ident b)))");
    P("#+ = #+ <| (a, b) => a",
      "(decl (op +) (<| (op +) (lambda (a b) (ident a))))");
    // `<=>`（HAM 0x07 附录第 9 档）同样可声明与引用
    P("#<=> = (a, b) => a", "(decl (op <=>) (lambda (a b) (ident a)))");
    // 其余有符号运算符同样可引用
    P("r = [#<|, #|>, #<~, #<=, #!]",
      "(decl r (array (op <|) (op |>) (op <~) (op <=) (op !)))");
}

// 算子表（op_table.h，HAM 0x07 附录）：表中算子的中缀与 `#` 引用形式都要可用

static void testOpTable() {
    // 每个可 `#` 引用的二元算子：中缀与 # 形式指向同一键名
    for (const auto &info : kBinOps) {
        if (!info.keyName)
            continue;
        std::string sym = info.symbol;
        std::string key = info.keyName;
        P("x = a " + sym + " b", "(decl x (" + sym + " (ident a) (ident b)))");
        P("x = #" + key + "(a, b)",
          "(decl x (call (op " + key + ") (ident a) (ident b)))");
    }
    // 每个可 `#` 引用的一元算子：`#` 形式可调用
    for (const auto &info : kUnOps) {
        if (!info.keyName)
            continue;
        std::string key = info.keyName;
        P("x = #" + key + "(a)", "(decl x (call (op " + key + ") (ident a)))");
    }
    // 一元前缀形式
    P("x = !a", "(decl x (not (ident a)))");
    P("x = ~a", "(decl x (compl (ident a)))");
    // `-` 一键两面：二元减号与一元负号共键 `#-`（HAM 0x07）
    P("x = a - b", "(decl x (- (ident a) (ident b)))");
    P("x = -a", "(decl x (neg (ident a)))");
    P("x = #-(a, b)", "(decl x (call (op -) (ident a) (ident b)))");
    P("x = #-(a)", "(decl x (call (op -) (ident a)))");
    // `<=>` 与其它比较同级、左结合（HAM 0x07 附录第 9 档）
    P("x = a <=> b <=> c", "(decl x (<=> (<=> (ident a) (ident b)) (ident c)))");
}

// let in / where / if（HAM 0x01/0x02）

static void testTempCombAndIf() {
    P("r = let { x = 1, y = 2 } in x + y",
      "(decl r (tempcomb (comb (decl x (int 1)) (decl y (int 2))) (+ (ident x) (ident y))))");
    P("a = if (c) 100", "(decl a (if (ident c) (int 100)))");
    P("b = if (c) 100 else 10", "(decl b (if (ident c) (int 100) (int 10)))");
    P("x = if (a) 1 else if (b) 2 else 3",
      "(decl x (if (ident a) (int 1) (if (ident b) (int 2) (int 3))))");
    // else 分支停在管道级之前（HAM 0x02）：is/as/where/|> / <| 作用于整个 if
    P("a = if (c) 1 else 2 where { c = true }",
      "(decl a (tempcomb (comb (decl c (bool true))) (if (ident c) (int 1) (int 2))))");
    P("b = if (c) 1 else \"a\" is Int",
      "(decl b (is (if (ident c) (int 1) (str \"a\")) (ident Int)))");
    P("f = a <| if (c) 1 else 2 <| b",
      "(decl f (<| (<| (ident a) (if (ident c) (int 1) (int 2))) (ident b)))");
    // then 分支里的 is 属于该分支
    P("x = if (c) a is Int else b",
      "(decl x (if (ident c) (is (ident a) (ident Int)) (ident b)))");
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

// 单表达式入口 parseExpression（HAM 0x08 的 --entry / REPL）

static void testExpressionEntry() {
    checkExprParse("2 |> `legs(_, 3)`",
                   "(|> (int 2) (lambda* (_) (call (ident legs) (placeholder) (int 3))))");
    checkExprParse("arr2 | `_ > 1`",
                   "(| (ident arr2) (lambda* (_) (> (placeholder) (int 1))))");
    // 结尾必须是 Eof
    checkExprError("a b");
    // 裸 `_` 同样执行收尾校验
    checkExprError("_ + 1");
    // 同一串走 parse() 仍只允许声明
    checkError("2 |> `legs(_, 3)`");
}

// 语法错误

static void testErrors() {
    checkError("x: Int = 1");            // 顶层不是声明（旧类型标记已砍）
    checkError("x = { a = 1, b: Int }"); // {} 内混合声明与集合字段
    checkError("x = { 1, y = 2 }");      // {} 内混合表达式与声明
    checkError("x = { 1 2 3 }");         // 集合元素之间需要逗号
    checkError("x = { _ = 4 }");         // `_` 不能作为键名（HAM 0x00）
    checkError("x = { comb.x: Int }");   // 组合集合的字段名必须是键名，不能是路径
    checkError("x = <T> 1");             // 泛型后不是函数声明
    checkError("f = (1) => 2");          // 非法参数列表
    checkError("f = ({x = 1}) => x");    // 组合模式参数不能写声明（HAM 0x01）
    checkError("x = (1 + 2");            // 未闭合括号
    checkError("_ = 1");                 // _ 不能作为键名（HAM 0x00）
    checkError("x = `_ + 1");            // 未闭合的反引号
    checkError("x = `f(x)`");            // 反引号对内没有 `_`
    checkError("x = #$");                // `$` 是调用语法，不可引用
    checkError("#=> = (a, b) => a");     // 箭头/声明符不可引用
}

// 病态输入护栏（B2）：深嵌套/扁平长链必须干净抛 ParseError，不能崩溃/挂死

static std::string rep(const std::string &s, int n) {
    std::string r;
    for (int i = 0; i < n; ++i)
        r += s;
    return r;
}

static void testGuards() {
    // 文档 4.3 的 6 个崩溃输入（原 SIGSEGV）
    checkError("x = " + std::string(5000, '-') + "1"); // 一元负号 5000 层
    checkError("x = " + std::string(5000, '(') + "1" + std::string(5000, ')'));
    checkError("x = " + rep("{a=", 5000) + "1" + rep("}", 5000)); // 组合 5000 层
    checkError("x = " + std::string(5000, '[') + std::string(5000, ']'));
    checkError("x = " + std::string(5000, '`')); // 反引号 5000 个
    checkError("x = a" + rep(" <| a", 7999));    // 扁平 <| 链 8000 项
    // 最坏组合：128 层嵌套 × 每层 128 项链 → 干净报错、不崩
    checkError("x = " + std::string(128, '(') + ("a" + rep(" <| a", 127)) +
               std::string(128, ')'));
    // 回归：5 万条顶层声明（不是长链）必须线性通过、不被护栏误伤
    {
        std::string src;
        for (int i = 0; i < 50000; ++i)
            src += "x" + std::to_string(i) + " = 1\n";
        try {
            parse(lex(src));
        } catch (const std::exception &e) {
            std::cerr << "many_items_50k threw: " << e.what() << '\n';
            ++g_failures;
        }
    }
}

int main() {
    testPrecedence();
    testBracesAndPatterns();
    testLambdas();
    testSugarAndAs();
    testBacktickScope();
    testBacktickRobustness();
    testOperatorRefs();
    testOpTable();
    testTempCombAndIf();
    testExpressionEntry();
    testCallsAndPostfix();
    testErrors();
    testGuards();

    if (g_failures == 0) {
        std::cout << "parser_test: all tests passed\n";
    } else {
        std::cerr << "parser_test: " << g_failures << " failure(s)\n";
    }
    return g_failures == 0 ? 0 : 1;
}
