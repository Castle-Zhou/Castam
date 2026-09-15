#include "ast_dump.h"

#include "op_table.h"

namespace castam {

    namespace {

        // 二元算子的 dump 符号：查 op_table.h 的 kBinOps（HAM 0x07 附录）
        const char *binOpSymbol(BinOp op) {
            for (const auto &info : kBinOps)
                if (info.op == op)
                    return info.symbol;
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
                        for (const auto &s : pat.segs) {
                            switch (s.kind) {
                            case PatSeg::Kind::Key:
                                out += " " + s.key;
                                break;
                            case PatSeg::Kind::Index:
                                out += " [";
                                dumpChild(s.index, out);
                                out += "]";
                                break;
                            case PatSeg::Kind::ExtKeys:
                                out += " {";
                                for (size_t i = 0; i < s.keys.size(); ++i) {
                                    if (i)
                                        out += " ";
                                    out += s.keys[i];
                                }
                                out += "}";
                                break;
                            }
                        }
                        out += ")";
                    } else if constexpr (std::is_same_v<T, PatDestructure>) {
                        out += "(destr";
                        for (const auto &k : pat.keys) {
                            out += " ";
                            if (k.type) {
                                // 键的取值约束（HAM 0x01 的 { x: { 1 } }）
                                out += "(" + k.name + " ";
                                dumpChild(k.type, out);
                                out += ")";
                            } else {
                                out += k.name;
                            }
                        }
                        out += ")";
                    } else if constexpr (std::is_same_v<T, PatOp>) {
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
                out += "(typed ";
                dumpPattern(p.pattern, out);
                out += " ";
                dumpChild(p.type, out);
                out += ")";
            } else {
                dumpPattern(p.pattern, out);
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
            void operator()(const OpRef &v) { out += "(op " + v.name + ")"; }
            void operator()(const Placeholder &) { out += "(placeholder)"; }

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
