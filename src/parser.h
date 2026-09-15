#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "ast.h"
#include "token.h"

namespace castam {

    struct ParseError: std::runtime_error {
        int line;
        int col;
        ParseError(const std::string &msg, int line, int col);
    };

    // 把 token 流解析为 AST，根节点恒为 CombLit（文件即隐式组合，HAM 0x00）
    // 遇到第一个语法错误时抛 ParseError，不做错误恢复
    NodePtr parse(const std::vector<Token> &tokens);

    // 解析单个表达式并要求结尾是 Eof；不做根 CombLit 包装。
    // 供 REPL / `--entry` 使用（HAM 0x08）：--entry 的值按表达式解析，在该文件
    // 顶层组合的作用域里求值（`--entry='mainCl'` 与 `--entry='cl(game(os))'` 均成立）；
    // 同样执行占位符收尾校验。CLI 接线属于求值层，暂不提供。
    NodePtr parseExpression(const std::vector<Token> &tokens);

} // namespace castam
