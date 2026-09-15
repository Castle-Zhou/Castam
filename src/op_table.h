#pragma once

// HAM 0x07 附录的算子表：优先级、`#` 引用键名与 dump 符号的唯一权威
// parser 的 infixBinOp / isKnownOpName 与 ast_dump 的 binOpSymbol 全部从此派生；
// 新增算子时只改这张表（词法侧的最长匹配顺序仍需同步，lexer_test 的交叉校验会兜底）
//
// 特例不进表：as、let in、where、=、<-、=>、$、.、[]、f(...)、反引号
// （as/where/$/后缀在 parseExpr 里单独处理，=/<- 是声明符，=> 走 makeLambda）

#include "ast.h"   // BinOp / UnaryOp
#include "token.h" // TK

namespace castam {

    // 一个算子键（`#` 后的名字）可以同时有一元和二元两个函数面（如 `-`，HAM 0x07）
    struct BinOpInfo {
        TK tk;               // 中缀 token
        BinOp op;            // AST 二元算子
        const char *symbol;  // 源码里的写法，dump 与报错用
        const char *keyName; // `#` 后的键名；nullptr = 不可通过 `#` 引用
        int level;           // parser 内部优先级编号（越大越紧）
    };

    struct UnOpInfo {
        TK tk;               // 前缀 token
        UnaryOp op;          // AST 一元算子
        const char *symbol;  // 源码里的写法，dump 与报错用
        const char *keyName; // `#` 后的键名；nullptr = 不可通过 `#` 引用
        int level;           // parser 内部优先级编号（一元操作数按 parseExpr(13)）
    };

    // 二元（含仅用于 dump 的 ->）
    // 不变式：ThinArrow 行对 infixBinOp 不可达——parseExpr 主循环在查表之前
    // 已单独处理（或按 minBp 拦截）TK::ThinArrow
    inline constexpr BinOpInfo kBinOps[] = {
        {TK::KwIs, BinOp::Is, "is", nullptr, 2},
        {TK::KwIsnt, BinOp::Isnt, "isnt", nullptr, 2},
        {TK::KwSubseteq, BinOp::Subseteq, "subseteq", nullptr, 2},
        {TK::KwSubset, BinOp::Subset, "subset", nullptr, 2},
        {TK::Pipe, BinOp::Pipe, "|>", "|>", 4},
        {TK::Delta, BinOp::Delta, "<|", "<|", 5},
        {TK::SetExt, BinOp::SetExt, "<~", "<~", 5},
        {TK::ThinArrow, BinOp::Arrow, "->", nullptr, 6},
        {TK::OrOr, BinOp::Or, "||", "||", 7},
        {TK::AndAnd, BinOp::And, "&&", "&&", 8},
        {TK::Bar, BinOp::Bar, "|", "|", 9},
        {TK::Amp, BinOp::Amp, "&", "&", 9},
        {TK::EqEq, BinOp::Eq, "==", "==", 10},
        {TK::NotEq, BinOp::NotEq, "!=", "!=", 10},
        {TK::Lt, BinOp::Lt, "<", "<", 10},
        {TK::Gt, BinOp::Gt, ">", ">", 10},
        {TK::Le, BinOp::Le, "<=", "<=", 10},
        {TK::Ge, BinOp::Ge, ">=", ">=", 10},
        {TK::Spaceship, BinOp::Spaceship, "<=>", "<=>", 10},
        {TK::Plus, BinOp::Add, "+", "+", 11},
        {TK::Minus, BinOp::Sub, "-", "-", 11},
        {TK::Star, BinOp::Mul, "*", "*", 12},
        {TK::Slash, BinOp::Div, "/", "/", 12},
        {TK::Percent, BinOp::Mod, "%", "%", 12},
    };

    // 一元：`-` 与二元减号共键 `#-`——一个键、两个函数面（HAM 0x07），可以 `<|` 起来
    inline constexpr UnOpInfo kUnOps[] = {
        {TK::Bang, UnaryOp::Not, "!", "!", 13},
        {TK::Tilde, UnaryOp::Complement, "~", "~", 13},
        {TK::Minus, UnaryOp::Neg, "-", "-", 13},
    };

} // namespace castam
