#pragma once

// AST 的 S 表达式 dump，供调试与 M3 parser 的快照测试
// 输出为单行，格式约定：
//   字面量     (int 1) (float 3.14) (char 'a') (str "abc") (bool true)
//   名字       (ident x) (placeholder) (op +)
//   pattern    x | (path comb x) | (path comb {a b}) | (destr x y) | (op +)
//   声明       (decl P V) | (decl<- P V)
//   组合与集合 (comb D...) | (combset (field x T)...) | (enum E...) | (predset P)
//   函数       (lambda PARAMS BODY)，泛型 (lambda (gen (T) (U B)) PARAMS BODY)，
//              返回值标注插入 (ret T)，`_` 糖生成的头为 lambda*
//              参数项：x | (typed x T) | (rest x) | (restall x)
//   运算       (SYM L R)（SYM 为运算符原文）| (as E T) | (not/compl/neg E)
//   其余       (if C T [E]) | (tempcomb C E) | (. E key) | (. key) | (index E I)
//              (arrty E) | (call f A...) | (call$ f A...) | (array E...)
//              (struct E...) | (spread E) | (spreadall E) | (typed E T)
// SrcLoc 不参与 dump

#include <string>

#include "ast.h"

namespace castam {

    std::string dumpAst(const Node &node);

} // namespace castam
