#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "token.h"

namespace castam {

    struct LexError: std::runtime_error {
        int line;
        int col;
        LexError(const std::string &msg, int line, int col);
    };

    // 把整个源文件一次性 token 化为 vector，末尾带一个 Eof token
    // 遇到第一个词法错误时抛 LexError
    std::vector<Token> lex(const std::string &source);

} // namespace castam
