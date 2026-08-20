// ast_dump 的快照测试：手工构造 AST，比对 S 表达式字符串
// 目标是每个节点种类至少出现一次，dump 格式即 M3 parser 快照测试的基准

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "ast_dump.h"

using namespace castam;

static int g_failures = 0;

static void checkDump(const NodePtr &node, const std::string &want) {
    std::string got = dumpAst(*node);
    if (got != want) {
        std::cerr << "dump mismatch:\n  got:  " << got << "\n  want: " << want << '\n';
        ++g_failures;
    }
}

// 构造辅助

static NodePtr num(const char *t) { return makeNode(IntLit{t}); }
static NodePtr id(const char *n) { return makeNode(Ident{n}); }
static NodePtr bin(BinOp op, NodePtr l, NodePtr r) {
    return makeNode(Binary{op, std::move(l), std::move(r)});
}
static Decl decl(Pattern p, NodePtr v, bool recursive = false) {
    return Decl{std::move(p), std::move(v), recursive};
}

// 字面量与名字

static void testLiterals() {
    checkDump(num("42"), "(int 42)");
    checkDump(makeNode(FloatLit{"3.14"}), "(float 3.14)");
    checkDump(makeNode(CharLit{"a"}), "(char 'a')");
    checkDump(makeNode(CharLit{"\n"}), R"((char '\n'))");
    checkDump(makeNode(StrLit{"abc"}), R"((str "abc"))");
    checkDump(makeNode(StrLit{"say \"hi\"\n"}), R"((str "say \"hi\"\n"))");
    checkDump(makeNode(BoolLit{true}), "(bool true)");
    checkDump(makeNode(BoolLit{false}), "(bool false)");
    checkDump(id("x"), "(ident x)");
    checkDump(makeNode(Placeholder{}), "(placeholder)");
    checkDump(makeNode(BacktickOp{"+"}), "(op +)");
}

// 组合与四种声明 pattern

static void testCombAndPatterns() {
    CombLit comb;
    comb.items.push_back(decl(PatIdent{"x"}, num("1")));
    comb.items.push_back(decl(PatPath{"comb", {"x"}, {}}, num("2")));
    comb.items.push_back(decl(PatPath{"comb", {}, {"a", "b"}}, id("comb1")));
    comb.items.push_back(decl(PatPath{"comb", {"x"}, {"a"}}, num("3")));
    comb.items.push_back(decl(PatDestructure{{"x", "y"}}, id("comb")));
    comb.items.push_back(decl(PatOp{"+"}, id("mul"), true));
    checkDump(makeNode(std::move(comb)),
              "(comb (decl x (int 1)) (decl (path comb x) (int 2))"
              " (decl (path comb {a b}) (ident comb1))"
              " (decl (path comb x {a}) (int 3))"
              " (decl (destr x y) (ident comb)) (decl<- (op +) (ident mul)))");
    checkDump(makeNode(CombLit{}), "(comb)");
}

// 集合的三种表达式形态

static void testSets() {
    CombSet cs;
    cs.fields.push_back(CombSetField{"x", id("Int")});
    cs.fields.push_back(CombSetField{"y", id("Int")});
    checkDump(makeNode(std::move(cs)),
              "(combset (field x (ident Int)) (field y (ident Int)))");

    EnumSet es;
    es.elems.push_back(num("0"));
    es.elems.push_back(num("1"));
    es.elems.push_back(num("2"));
    checkDump(makeNode(std::move(es)), "(enum (int 0) (int 1) (int 2))");

    // {...|_ % 2 == 1 }（parser 会把 _ 糖包成 lambda*）
    auto pred = bin(BinOp::Eq, bin(BinOp::Mod, makeNode(Placeholder{}), num("2")), num("1"));
    Lambda sugar;
    sugar.params.push_back(Param{"_", nullptr});
    sugar.body = std::move(pred);
    sugar.sugar = true;
    checkDump(makeNode(PredSet{makeNode(std::move(sugar))}),
              "(predset (lambda* (_) (== (% (placeholder) (int 2)) (int 1))))");
}

// 函数：泛型、返回值标注、参数包

static void testLambdas() {
    // <T: U>(mul: T, x: T) -> T => (x + y) * mul
    Lambda lam;
    lam.generics.push_back(GenericParam{"T", id("U")});
    lam.generics.push_back(GenericParam{"V", nullptr});
    Param mul;
    mul.name = "mul";
    mul.type = id("T");
    lam.params.push_back(std::move(mul));
    lam.params.push_back(Param{"x", id("T")});
    Param rest;
    rest.name = "rest";
    rest.pack = PackKind::Rest;
    lam.params.push_back(std::move(rest));
    Param all;
    all.name = "args";
    all.type = id("Int");
    all.pack = PackKind::All;
    lam.params.push_back(std::move(all));
    lam.returnType = id("T");
    lam.body = bin(BinOp::Mul, bin(BinOp::Add, id("x"), id("y")), id("mul"));
    checkDump(makeNode(std::move(lam)),
              "(lambda (gen (T (ident U)) (V)) ((typed mul (ident T)) (typed x (ident T))"
              " (rest rest) (restall (typed args (ident Int)))) (ret (ident T))"
              " (* (+ (ident x) (ident y)) (ident mul)))");

    // 无参：() => 1
    Lambda noArg;
    noArg.body = num("1");
    checkDump(makeNode(std::move(noArg)), "(lambda () (int 1))");
}

// 运算符：每个 BinOp 的 dump 符号

static void testBinOpSymbols() {
    const std::pair<BinOp, const char *> kCases[] = {
        {BinOp::Is, "is"},
        {BinOp::Isnt, "isnt"},
        {BinOp::Subseteq, "subseteq"},
        {BinOp::Subset, "subset"},
        {BinOp::Delta, "<|"},
        {BinOp::Pipe, "|>"},
        {BinOp::SetExt, "<~"},
        {BinOp::Arrow, "->"},
        {BinOp::Or, "||"},
        {BinOp::And, "&&"},
        {BinOp::Bar, "|"},
        {BinOp::Amp, "&"},
        {BinOp::Eq, "=="},
        {BinOp::NotEq, "!="},
        {BinOp::Lt, "<"},
        {BinOp::Gt, ">"},
        {BinOp::Le, "<="},
        {BinOp::Ge, ">="},
        {BinOp::Add, "+"},
        {BinOp::Sub, "-"},
        {BinOp::Mul, "*"},
        {BinOp::Div, "/"},
        {BinOp::Mod, "%"},
    };
    for (const auto &[op, sym] : kCases) {
        std::string want = std::string("(") + sym + " (int 1) (int 2))";
        checkDump(bin(op, num("1"), num("2")), want);
    }
}

// 一元、as、if

static void testUnaryAsIf() {
    checkDump(makeNode(Unary{UnaryOp::Not, id("x")}), "(not (ident x))");
    checkDump(makeNode(Unary{UnaryOp::Complement, id("LessThan3")}),
              "(compl (ident LessThan3))");
    checkDump(makeNode(Unary{UnaryOp::Neg, num("1")}), "(neg (int 1))");

    checkDump(makeNode(As{num("1"), id("Int")}), "(as (int 1) (ident Int))");

    IfExpr noElse{id("c"), num("100"), nullptr};
    checkDump(makeNode(std::move(noElse)), "(if (ident c) (int 100))");
    IfExpr withElse{id("c"), num("100"), num("10")};
    checkDump(makeNode(std::move(withElse)), "(if (ident c) (int 100) (int 10))");

    // 临时组合：let { x = 1 } in (y <| 2)
    CombLit tc;
    tc.items.push_back(decl(PatIdent{"x"}, num("1")));
    checkDump(makeNode(TempComb{makeNode(std::move(tc)), bin(BinOp::Delta, id("y"), num("2"))}),
              "(tempcomb (comb (decl x (int 1))) (<| (ident y) (int 2)))");
}

// 投影、下标、调用与容器

static void testPostfixAndContainers() {
    checkDump(makeNode(Proj{id("comb"), "x"}), "(. (ident comb) x)");
    checkDump(makeNode(ContextProj{"count"}), "(. count)");
    checkDump(makeNode(Index{id("arr"), num("0")}), "(index (ident arr) (int 0))");
    checkDump(makeNode(ArrayType{id("Int")}), "(arrty (ident Int))");

    Call call;
    call.callee = id("add");
    call.args.push_back(num("1"));
    call.args.push_back(num("2"));
    checkDump(makeNode(std::move(call)), "(call (ident add) (int 1) (int 2))");

    Call dollar;
    dollar.callee = id("inc");
    dollar.args.push_back(id("a"));
    dollar.dollar = true;
    checkDump(makeNode(std::move(dollar)), "(call$ (ident inc) (ident a))");

    ArrayLit arr;
    arr.elems.push_back(num("1"));
    arr.elems.push_back(num("2"));
    checkDump(makeNode(std::move(arr)), "(array (int 1) (int 2))");
    checkDump(makeNode(ArrayLit{}), "(array)");

    StructLit st;
    st.elems.push_back(id("a"));
    st.elems.push_back(makeNode(Spread{id("rest"), false}));
    st.elems.push_back(makeNode(Spread{id("args"), true}));
    checkDump(makeNode(std::move(st)),
              "(struct (ident a) (spread (ident rest)) (spreadall (ident args)))");
    checkDump(makeNode(StructLit{}), "(struct)");

    checkDump(makeNode(Typed{id("x"), id("Int")}), "(typed (ident x) (ident Int))");
}

// 综合：sum.ham 第 1 行的 AST 形态

static void testSumHamShape() {
    // sum <- 0 <| (first, ...rest) => first + sum(rest...)
    Lambda lam;
    lam.params.push_back(Param{"first", nullptr});
    Param rest;
    rest.name = "rest";
    rest.pack = PackKind::Rest;
    lam.params.push_back(std::move(rest));
    Call sumCall;
    sumCall.callee = id("sum");
    sumCall.args.push_back(makeNode(Spread{id("rest"), false}));
    lam.body = bin(BinOp::Add, id("first"), makeNode(std::move(sumCall)));

    CombLit file;
    file.items.push_back(
        decl(PatIdent{"sum"}, bin(BinOp::Delta, num("0"), makeNode(std::move(lam))), true));

    checkDump(makeNode(std::move(file)),
              "(comb (decl<- sum (<| (int 0) (lambda (first (rest rest))"
              " (+ (ident first) (call (ident sum) (spread (ident rest))))))))");
}

int main() {
    testLiterals();
    testCombAndPatterns();
    testSets();
    testLambdas();
    testBinOpSymbols();
    testUnaryAsIf();
    testPostfixAndContainers();
    testSumHamShape();

    if (g_failures == 0) {
        std::cout << "ast_dump_test: all tests passed\n";
    } else {
        std::cerr << "ast_dump_test: " << g_failures << " failure(s)\n";
    }
    return g_failures == 0 ? 0 : 1;
}
