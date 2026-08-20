#include "ast_dump.h"

namespace castam {

    namespace {

        const char *binOpSymbol(BinOp op) {
            switch (op) {
            case BinOp::In:
                return "in";
            case BinOp::NotIn:
                return "notin";
            case BinOp::Subseteq:
                return "subseteq";
            case BinOp::Subset:
                return "subset";
            case BinOp::Delta:
                return "<|";
            case BinOp::Pipe:
                return "|>";
            case BinOp::SetExt:
                return "<~";
            case BinOp::Arrow:
                return "->";
            case BinOp::Or:
                return "||";
            case BinOp::And:
                return "&&";
            case BinOp::Bar:
                return "|";
            case BinOp::Amp:
                return "&";
            case BinOp::Eq:
                return "==";
            case BinOp::NotEq:
                return "!=";
            case BinOp::Lt:
                return "<";
            case BinOp::Gt:
                return ">";
            case BinOp::Le:
                return "<=";
            case BinOp::Ge:
                return ">=";
            case BinOp::Add:
                return "+";
            case BinOp::Sub:
                return "-";
            case BinOp::Mul:
                return "*";
            case BinOp::Div:
                return "/";
            case BinOp::Mod:
                return "%";
            }
            return "?";
        }

        void dumpNode(const Node &n, std::string &out);

        void dumpChild(const NodePtr &p, std::string &out) { dumpNode(*p, out); }

        // 字符串/字符内容按 dump 需要转义
        void dumpEscaped(const std::string &s, std::string &out) {
            for (char c : s) {
                switch (c) {
                case '\n':
                    out += "\\n";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\'':
                    out += "\\'";
                    break;
                case '"':
                    out += "\\\"";
                    break;
                default:
                    out += c;
                }
            }
        }

        void dumpPattern(const Pattern &p, std::string &out) {
            std::visit(
                [&](const auto &pat) {
                    using T = std::decay_t<decltype(pat)>;
                    if constexpr (std::is_same_v<T, PatIdent>) {
                        out += pat.name;
                    } else if constexpr (std::is_same_v<T, PatPath>) {
                        out += "(path " + pat.base;
                        for (const auto &s : pat.segs)
                            out += " " + s;
                        if (!pat.extKeys.empty()) {
                            out += " {";
                            for (size_t i = 0; i < pat.extKeys.size(); ++i) {
                                if (i)
                                    out += " ";
                                out += pat.extKeys[i];
                            }
                            out += "}";
                        }
                        out += ")";
                    } else if constexpr (std::is_same_v<T, PatDestructure>) {
                        out += "(destr";
                        for (const auto &k : pat.keys)
                            out += " " + k;
                        out += ")";
                    } else {
                        out += "(op " + pat.name + ")";
                    }
                },
                p);
        }

        void dumpDecl(const Decl &d, std::string &out) {
            out += d.recursive ? "(decl<- " : "(decl ";
            dumpPattern(d.pattern, out);
            out += " ";
            dumpChild(d.value, out);
            out += ")";
        }

        void dumpParam(const Param &p, std::string &out) {
            switch (p.pack) {
            case PackKind::Rest:
                out += "(rest ";
                break;
            case PackKind::All:
                out += "(restall ";
                break;
            case PackKind::None:
                break;
            }
            if (p.type) {
                out += "(typed " + p.name + " ";
                dumpChild(p.type, out);
                out += ")";
            } else {
                out += p.name;
            }
            if (p.pack != PackKind::None)
                out += ")";
        }

        struct Dumper {
            std::string &out;

            void operator()(const IntLit &v) { out += "(int " + v.text + ")"; }
            void operator()(const FloatLit &v) { out += "(float " + v.text + ")"; }
            void operator()(const CharLit &v) {
                out += "(char '";
                dumpEscaped(v.value, out);
                out += "')";
            }
            void operator()(const StrLit &v) {
                out += "(str \"";
                dumpEscaped(v.value, out);
                out += "\")";
            }
            void operator()(const BoolLit &v) {
                out += v.value ? "(bool true)" : "(bool false)";
            }
            void operator()(const Ident &v) { out += "(ident " + v.name + ")"; }
            void operator()(const Placeholder &) { out += "(placeholder)"; }
            void operator()(const BacktickOp &v) { out += "(op " + v.name + ")"; }

            void operator()(const CombLit &v) {
                out += "(comb";
                for (const auto &d : v.items) {
                    out += " ";
                    dumpDecl(d, out);
                }
                out += ")";
            }
            void operator()(const CombSet &v) {
                out += "(combset";
                for (const auto &f : v.fields) {
                    out += " (field " + f.name + " ";
                    dumpChild(f.type, out);
                    out += ")";
                }
                out += ")";
            }
            void operator()(const EnumSet &v) {
                out += "(enum";
                for (const auto &e : v.elems) {
                    out += " ";
                    dumpChild(e, out);
                }
                out += ")";
            }
            void operator()(const PredSet &v) {
                out += "(predset ";
                dumpChild(v.pred, out);
                out += ")";
            }

            void operator()(const Lambda &v) {
                out += v.sugar ? "(lambda*" : "(lambda";
                if (!v.generics.empty()) {
                    out += " (gen";
                    for (const auto &g : v.generics) {
                        out += " (" + g.name;
                        if (g.bound) {
                            out += " ";
                            dumpChild(g.bound, out);
                        }
                        out += ")";
                    }
                    out += ")";
                }
                out += " (";
                for (size_t i = 0; i < v.params.size(); ++i) {
                    if (i)
                        out += " ";
                    dumpParam(v.params[i], out);
                }
                out += ")";
                if (v.returnType) {
                    out += " (ret ";
                    dumpChild(v.returnType, out);
                    out += ")";
                }
                out += " ";
                dumpChild(v.body, out);
                out += ")";
            }

            void operator()(const Binary &v) {
                out += "(";
                out += binOpSymbol(v.op);
                out += " ";
                dumpChild(v.lhs, out);
                out += " ";
                dumpChild(v.rhs, out);
                out += ")";
            }
            void operator()(const As &v) {
                out += "(as ";
                dumpChild(v.expr, out);
                out += " ";
                dumpChild(v.type, out);
                out += ")";
            }
            void operator()(const Unary &v) {
                switch (v.op) {
                case UnaryOp::Not:
                    out += "(not ";
                    break;
                case UnaryOp::Complement:
                    out += "(compl ";
                    break;
                case UnaryOp::Neg:
                    out += "(neg ";
                    break;
                }
                dumpChild(v.expr, out);
                out += ")";
            }
            void operator()(const IfExpr &v) {
                out += "(if ";
                dumpChild(v.cond, out);
                out += " ";
                dumpChild(v.thenBranch, out);
                if (v.elseBranch) {
                    out += " ";
                    dumpChild(v.elseBranch, out);
                }
                out += ")";
            }
            void operator()(const TempComb &v) {
                out += "(tempcomb ";
                dumpChild(v.comb, out);
                out += " ";
                dumpChild(v.body, out);
                out += ")";
            }

            void operator()(const Proj &v) {
                out += "(. ";
                dumpChild(v.obj, out);
                out += " " + v.key + ")";
            }
            void operator()(const ContextProj &v) { out += "(. " + v.key + ")"; }
            void operator()(const Index &v) {
                out += "(index ";
                dumpChild(v.obj, out);
                out += " ";
                dumpChild(v.index, out);
                out += ")";
            }
            void operator()(const ArrayType &v) {
                out += "(arrty ";
                dumpChild(v.elem, out);
                out += ")";
            }

            void operator()(const Call &v) {
                out += v.dollar ? "(call$ " : "(call ";
                dumpChild(v.callee, out);
                for (const auto &a : v.args) {
                    out += " ";
                    dumpChild(a, out);
                }
                out += ")";
            }
            void operator()(const ArrayLit &v) {
                out += "(array";
                for (const auto &e : v.elems) {
                    out += " ";
                    dumpChild(e, out);
                }
                out += ")";
            }
            void operator()(const StructLit &v) {
                out += "(struct";
                for (const auto &e : v.elems) {
                    out += " ";
                    dumpChild(e, out);
                }
                out += ")";
            }
            void operator()(const Spread &v) {
                out += v.all ? "(spreadall " : "(spread ";
                dumpChild(v.expr, out);
                out += ")";
            }
            void operator()(const Typed &v) {
                out += "(typed ";
                dumpChild(v.expr, out);
                out += " ";
                dumpChild(v.type, out);
                out += ")";
            }
        };

        void dumpNode(const Node &n, std::string &out) { std::visit(Dumper{out}, n.kind); }

    } // namespace

    std::string dumpAst(const Node &node) {
        std::string out;
        dumpNode(node, out);
        return out;
    }

} // namespace castam
