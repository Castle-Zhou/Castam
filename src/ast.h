#pragma once

// HAM 3.0 的 AST 定义
// 节点与文档语法的对应关系标在各结构上（0x00-0x08），新语法标 ham 提交号
// 表示法：std::variant + std::unique_ptr，语义阶段靠 variant 访问，不用继承体系

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace castam {

    // 源码位置（行列均从 1 开始）
    struct SrcLoc {
        int line = 0;
        int col = 0;
    };

    struct Node;
    using NodePtr = std::unique_ptr<Node>;

    // 二元运算符，优先级见 HAM 0x07 附录
    // `as` 不在其中，它是专用节点 As（右侧必须是集合/类型表达式，HAM 0x02）
    enum class BinOp {
        In,       // in（HAM 0x03）
        NotIn,    // notin
        Subseteq, // subseteq
        Subset,   // subset
        Delta,    // <|（HAM 0x00/0x01）
        Pipe,     // |>（HAM 0x01）
        SetExt,   // <~（HAM 0x03）
        Arrow,    // ->（函数集合，HAM 0x02）
        Or,       // ||
        And,      // &&
        Bar,      // |（集合并、数组过滤，HAM 0x03/0x06）
        Amp,      // &
        Eq,       // ==
        NotEq,    // !=
        Lt,       // <
        Gt,       // >
        Le,       // <=
        Ge,       // >=
        Add,      // +
        Sub,      // -
        Mul,      // *
        Div,      // /
        Mod,      // %
    };

    // 一元运算符（HAM 0x07，11 级）
    enum class UnaryOp {
        Not,        // !
        Complement, // ~（集合补集，HAM 0x03）
        Neg,        // 一元 -
    };

    // 声明左侧的 pattern（HAM 0x00）
    struct PatIdent {
        std::string name;
    };
    struct PatPath {
        std::string base;                 // comb.x / comb.{a, b} 的 comb
        std::vector<std::string> segs;    // .x.y 的键链
        std::vector<std::string> extKeys; // .{a, b} 的键列表，为空表示无此尾段
    };
    struct PatDestructure {
        std::vector<std::string> keys; // {x, y} = comb
    };
    struct PatOp {
        std::string name; // `+` = ...（HAM 0x07 的运算符声明）
    };
    using Pattern = std::variant<PatIdent, PatPath, PatDestructure, PatOp>;

    // 组合中的一项声明：pattern = value 或 pattern <- value（HAM 0x01）
    struct Decl {
        Pattern pattern;
        NodePtr value;
        bool recursive = false; // <- 声明，可在值中递归引用正在被定义的键
    };

    // 参数包的种类（HAM 0x05）
    enum class PackKind {
        None, // 普通参数
        Rest, // ...x：捕获当前结构内的剩余参数
        All,  // ....x：捕获将来所有结构内的所有参数
    };

    struct Param {
        std::string name;
        NodePtr type; // 可空：x: Int 的标注
        PackKind pack = PackKind::None;
    };

    // 泛型参数（HAM 0x06）
    struct GenericParam {
        std::string name;
        NodePtr bound; // 可空：<T: U> 的 U
    };

    // 字面量：数值保留源码原文，字符/字符串存转义后的内容
    struct IntLit {
        std::string text;
    };
    struct FloatLit {
        std::string text;
    };
    struct CharLit {
        std::string value;
    };
    struct StrLit {
        std::string value;
    };
    struct BoolLit {
        bool value;
    };

    struct Ident {
        std::string name;
    };
    // `_` 语法糖占位符（HAM 0x01），parser 在表达式边界把它包成 Lambda
    struct Placeholder {};
    // 反引号引用的运算符名（HAM 0x07）
    struct BacktickOp {
        std::string name;
    };

    // 组合字面量 { x = 1, f <- ... }（HAM 0x00）
    // 源文件本身就是一个隐式的 CombLit
    struct CombLit {
        std::vector<Decl> items;
    };

    // HAM 0x02 的 {} 四形态之二三（另两个是 CombLit 与声明左侧的 PatDestructure）
    struct CombSetField {
        std::string name;
        NodePtr type;
    };
    struct CombSet {
        std::vector<CombSetField> fields; // { x: Int, y: Int }
    };
    struct EnumSet {
        std::vector<NodePtr> elems; // { 0, 1, 2 }
    };
    struct PredSet {
        NodePtr pred; // {...| f}（... 与 | 是两个 token，parser 前瞻识别）
    };

    // 函数声明式（HAM 0x01）
    // 含 HAM 0x02 的返回值标注（(x: Int) -> Int => ...）与 0x06 的泛型（<T: U> ...）
    struct Lambda {
        std::vector<GenericParam> generics;
        std::vector<Param> params;
        NodePtr returnType; // 可空：=> 前的 -> Type
        NodePtr body;
        bool sugar = false; // 由 `_` 语法糖生成，参数为单个 `_`
    };

    struct Binary {
        BinOp op;
        NodePtr lhs;
        NodePtr rhs;
    };
    // as 类型标记（HAM 0x02）：x = 1 as Int
    struct As {
        NodePtr expr;
        NodePtr type;
    };
    struct Unary {
        UnaryOp op;
        NodePtr expr;
    };
    // if 表达式（HAM 0x02），elseBranch 可空
    struct IfExpr {
        NodePtr cond;
        NodePtr thenBranch;
        NodePtr elseBranch;
    };

    // a.x 投影（HAM 0x00）
    struct Proj {
        NodePtr obj;
        std::string key;
    };
    // 前导 .x：在 <| 链中投影左侧的累积合并（HAM 0x01/0x04）
    struct ContextProj {
        std::string key;
    };
    // a[i] 下标（HAM 0x06）；定长数组集合 T[n] 也用此节点，语义阶段再解释
    struct Index {
        NodePtr obj;
        NodePtr index;
    };
    // T[] 数组集合（HAM 0x06）
    struct ArrayType {
        NodePtr elem;
    };

    // 函数调用（HAM 0x01）：f(...) 或 f $ (...)（dollar 标记后者）
    struct Call {
        NodePtr callee;
        std::vector<NodePtr> args;
        bool dollar = false;
    };

    struct ArrayLit {
        std::vector<NodePtr> elems; // [1, 2, 3]（HAM 0x06）
    };
    // (a, b) 结构：函数真正的参数（HAM 0x01），也是 lambda LHS 的载体
    struct StructLit {
        std::vector<NodePtr> elems;
    };
    // 实参展开 args... / args....（HAM 0x05）
    struct Spread {
        NodePtr expr;
        bool all = false;
    };
    // x: Int 参数标注的临时载体，仅允许出现在参数位置，parser 负责校验
    struct Typed {
        NodePtr expr;
        NodePtr type;
    };

    struct Node {
        SrcLoc loc;
        std::variant<IntLit, FloatLit, CharLit, StrLit, BoolLit, Ident, Placeholder,
                     BacktickOp, CombLit, CombSet, EnumSet, PredSet, Lambda, Binary, As,
                     Unary, IfExpr, Proj, ContextProj, Index, ArrayType, Call, ArrayLit,
                     StructLit, Spread, Typed>
            kind;
    };

    // 构造节点：makeNode(IntLit{"1"}, {1, 5})
    template <typename T>
    NodePtr makeNode(T kind, SrcLoc loc = {}) {
        auto n = std::make_unique<Node>();
        n->loc = loc;
        n->kind = std::move(kind);
        return n;
    }

} // namespace castam
