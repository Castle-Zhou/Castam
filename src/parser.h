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

} // namespace castam
