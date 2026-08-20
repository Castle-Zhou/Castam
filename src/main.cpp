#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "lexer.h"

namespace {

    std::string readFile(const char *path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error(std::string("cannot open file: ") + path);
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    int cmdLex(const char *path) {
        std::string src;
        try {
            src = readFile(path);
        } catch (const std::exception &e) {
            std::cerr << "castam: " << e.what() << '\n';
            return 1;
        }
        try {
            for (const auto &tok : castam::lex(src)) {
                std::cout << tok.line << ':' << tok.col << '\t'
                          << castam::tokenKindName(tok.kind);
                if (!tok.text.empty())
                    std::cout << '\t' << tok.text;
                std::cout << '\n';
            }
        } catch (const castam::LexError &e) {
            std::cerr << path << ':' << e.line << ':' << e.col << ": error: "
                      << e.what() << '\n';
            return 1;
        }
        return 0;
    }

    void usage(const char *argv0) {
        std::cerr << "usage: " << argv0 << " lex <file.ham>\n";
    }

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    std::string cmd = argv[1];
    if (cmd == "lex") {
        if (argc != 3) {
            usage(argv[0]);
            return 1;
        }
        return cmdLex(argv[2]);
    }
    std::cerr << "castam: unknown command '" << cmd << "'\n";
    usage(argv[0]);
    return 1;
}
