#pragma once

#include <string>

namespace castam {

    // HAM 3.0 的 token 种类，与文档 0x00-0x08 的语法对应
    // 只把有语法角色的词列为关键字，import 和内置集合名（Int 等）按普通标识符处理
    enum class TK {
        Eof,
        // 标识符与字面量
        Ident,      // [A-Za-z#][A-Za-z0-9_#]* | _[A-Za-z0-9_#]+
        Underscore, // 单独的 `_`（语法糖占位符）
        BacktickOp, // `+`、`<|` 等被当作名字引用的运算符
        IntLit,
        FloatLit,
        CharLit,
        StrLit,
        // 关键字
        KwIf,
        KwElse,
        KwIn,
        KwNotin,
        KwSubseteq,
        KwSubset,
        KwTrue,
        KwFalse,
        KwAs,
        KwLet,   // let {comb} in ...（临时组合，HAM 0x01）
        KwWhere, // ... where {comb}（同上）
        // 括号与分隔符
        LParen,
        RParen,
        LBrace,
        RBrace,
        LBracket,
        RBracket,
        Comma,
        Colon,
        // 点号族
        Dot,         // .
        Ellipsis,    // ...
        EllipsisAll, // ....
        // 声明与箭头
        Eq,        // =
        RecDecl,   // <-
        FatArrow,  // =>
        ThinArrow, // ->
        // 调用与扩展
        Dollar, // $
        Delta,  // <|
        Pipe,   // |>
        SetExt, // <~
        // 集合与逻辑
        Bar,    // |
        Amp,    // &
        Tilde,  // ~
        Bang,   // !
        AndAnd, // &&
        OrOr,   // ||
        // 算术与比较
        Plus,
        Minus,
        Star,
        Slash,
        Percent,
        EqEq,
        NotEq,
        Lt,
        Gt,
        Le,
        Ge,
    };

    struct Token {
        TK kind;
        std::string text; // 源码原文（字面量保留源形式，求值留给 parser）
        int line;         // 从 1 开始
        int col;          // 从 1 开始
    };

    const char *tokenKindName(TK kind);

} // namespace castam
