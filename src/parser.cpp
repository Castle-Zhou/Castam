#include "parser.h"

#include <functional>
#include <utility>

#include "op_table.h"

// HAM 3.0 的 Pratt parser
// 绑定力表来自 HAM 0x07 附录（14 级），特殊构造的解析决策标在各函数上

namespace castam {

    ParseError::ParseError(const std::string &msg, int line, int col)
        : std::runtime_error(msg), line(line), col(col) {}

    namespace {

        // 中缀二元运算符的绑定力：查 op_table.h 的 kBinOps（HAM 0x07 附录）
        // =>、->、$、where、as 不在此表，它们在 parseExpr 主循环里单独处理
        bool infixBinOp(TK k, BinOp &op, int &level) {
            for (const auto &info : kBinOps) {
                if (info.tk == k) {
                    op = info.op;
                    level = info.level;
                    return true;
                }
            }
            return false;
        }

        // `#` 可引用的符号运算符（HAM 0x07）：op_table.h 中 keyName 非空的行；
        // `$`、`.` 等调用族语法与 `=`、`<-`、`=>`、`->` 声明符不可引用。
        // 扩展自定义运算符前，白名单只放行这一张表
        bool isKnownOpName(const std::string &s) {
            for (const auto &info : kBinOps)
                if (info.keyName && s == info.keyName)
                    return true;
            for (const auto &info : kUnOps)
                if (info.keyName && s == info.keyName)
                    return true;
            return false;
        }

        // 病态输入护栏（B2）：递归下降/左偏树过深会爆栈（SIGSEGV），超限改为干净报错
        static constexpr int kMaxNestDepth = 128; // 语法嵌套上限
        static constexpr int kMaxChainLen = 128;  // 单个表达式的运算符链长上限

        // 护栏触发的错误：与候选试探的回溯信号区分开——反引号候选循环靠捕获
        // ParseError 逐候选重试，护栏错误是病态输入的终态，必须直接传播
        struct GuardError: ParseError {
            using ParseError::ParseError;
        };

        // parseExpr 入口的深度计数：括号/组合/数组/反引号/一元/-> 等所有嵌套来源
        // 都经由 parseExpr，一个计数器全覆盖
        struct ExprDepthGuard {
            int &depth;
            explicit ExprDepthGuard(int &d) : depth(d) { ++depth; }
            ~ExprDepthGuard() { --depth; }
        };

        // 通用子节点访问器（B2 的基础设施）：对 n 的每个直接子 Node 调用 fn(const Node&)
        // 覆盖 Node::kind 的全部 25 个分支；可空的 NodePtr 先判空；叶子无子节点
        template <typename F>
        void forEachChild(const Node &n, F &&fn) {
            auto each = [&](const NodePtr &p) {
                if (p)
                    fn(*p);
            };
            std::visit(
                [&](const auto &v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, CombLit>) {
                        for (const auto &d : v.items)
                            each(d.value);
                    } else if constexpr (std::is_same_v<T, CombSet>) {
                        for (const auto &f : v.fields)
                            each(f.type);
                    } else if constexpr (std::is_same_v<T, EnumSet> ||
                                         std::is_same_v<T, ArrayLit> ||
                                         std::is_same_v<T, StructLit>) {
                        for (const auto &p : v.elems)
                            each(p);
                    } else if constexpr (std::is_same_v<T, PredSet>) {
                        each(v.pred);
                    } else if constexpr (std::is_same_v<T, Lambda>) {
                        for (const auto &g : v.generics)
                            each(g.bound);
                        for (const auto &p : v.params)
                            each(p.type);
                        each(v.returnType);
                        each(v.body);
                    } else if constexpr (std::is_same_v<T, Binary>) {
                        each(v.lhs);
                        each(v.rhs);
                    } else if constexpr (std::is_same_v<T, As>) {
                        each(v.expr);
                        each(v.type);
                    } else if constexpr (std::is_same_v<T, Unary> ||
                                         std::is_same_v<T, Spread>) {
                        each(v.expr);
                    } else if constexpr (std::is_same_v<T, Proj>) {
                        each(v.obj);
                    } else if constexpr (std::is_same_v<T, ArrayType>) {
                        each(v.elem);
                    } else if constexpr (std::is_same_v<T, IfExpr>) {
                        each(v.cond);
                        each(v.thenBranch);
                        each(v.elseBranch);
                    } else if constexpr (std::is_same_v<T, TempComb>) {
                        each(v.comb);
                        each(v.body);
                    } else if constexpr (std::is_same_v<T, Index>) {
                        each(v.obj);
                        each(v.index);
                    } else if constexpr (std::is_same_v<T, Call>) {
                        each(v.callee);
                        for (const auto &a : v.args)
                            each(a);
                    } else if constexpr (std::is_same_v<T, Typed>) {
                        each(v.expr);
                        each(v.type);
                    }
                    // IntLit/FloatLit/CharLit/StrLit/BoolLit/Ident/OpRef/Placeholder/
                    // ContextProj 是叶子，无子节点
                },
                n.kind);
        }

        // 子树是否含自由 `_`（HAM 0x01）：迭代版（显式栈，不递归，B2）
        // 糖 lambda 的 `_` 已被绑定，不下潜其 body；非糖 lambda 只看 body
        bool hasFreePlaceholder(const Node &root) {
            std::vector<const Node *> stack{&root};
            while (!stack.empty()) {
                const Node *n = stack.back();
                stack.pop_back();
                if (std::holds_alternative<Placeholder>(n->kind))
                    return true;
                if (auto *lam = std::get_if<Lambda>(&n->kind)) {
                    if (!lam->sugar)
                        stack.push_back(lam->body.get());
                    continue;
                }
                forEachChild(*n, [&](const Node &c) { stack.push_back(&c); });
            }
            return false;
        }

        // 收尾校验（A5）：任何 Placeholder 都必须由反引号定界（HAM 0x01）
        // 迭代遍历（显式栈，B2）：糖 lambda 的 body 下潜时 sugarDepth+1，
        // sugarDepth == 0 的 Placeholder 即未定界；
        // `` `x => _` `` 的 `_` 在反引号作用域内，绑定外层糖参数，不报错
        void checkPlaceholdersDelimited(const Node &root) {
            std::vector<std::pair<const Node *, int>> stack{{&root, 0}};
            while (!stack.empty()) {
                auto [n, sugarDepth] = stack.back();
                stack.pop_back();
                if (std::holds_alternative<Placeholder>(n->kind)) {
                    if (sugarDepth == 0)
                        throw ParseError("占位符 _ 必须由反引号定界", n->loc.line,
                                         n->loc.col);
                    continue;
                }
                int childDepth = sugarDepth;
                if (auto *lam = std::get_if<Lambda>(&n->kind); lam && lam->sugar)
                    ++childDepth;
                forEachChild(*n,
                             [&](const Node &c) { stack.emplace_back(&c, childDepth); });
            }
        }

        class Parser {
        public:
            explicit Parser(const std::vector<Token> &toks) : toks_(toks) {}

            NodePtr run() {
                // 文件即隐式组合（HAM 0x00）：声明项之间的逗号可选
                CombLit root;
                while (!at(TK::Eof)) {
                    Item item = parseItem();
                    if (item.kind != Item::Kind::Decl) {
                        error("顶层只能包含声明", item.loc);
                    }
                    root.items.push_back(std::move(item.decl));
                    eat(TK::Comma);
                }
                return makeNode(std::move(root), SrcLoc{1, 1});
            }

        private:
            const std::vector<Token> &toks_;
            size_t i_ = 0;
            int parenDepth_ = 0;  // 括号嵌套深度，用于判断 x: T 标注是否合法
            int exprDepth_ = 0;   // parseExpr 递归深度（B2 护栏，ExprDepthGuard 维护）
            bool stopGt_ = false; // 解析泛型约束时把 > 视为终止符
            // 解析 -> 类型链时把 => 视为外层参数列表的分隔符（HAM 0x02），不成 λ
            bool stopFatArrow_ = false;

            // 组合/集合内的一项：声明、组合集合字段或表达式（枚举集合元素）
            struct Item {
                enum class Kind { Decl,
                                  Field,
                                  Expr } kind;
                SrcLoc loc;
                Decl decl;
                CombSetField field;
                NodePtr expr;
            };

            const Token &peek(size_t ahead = 0) const {
                size_t idx = i_ + ahead;
                if (idx >= toks_.size())
                    idx = toks_.size() - 1;
                return toks_[idx];
            }
            const Token &advance() { return toks_[i_++]; }
            bool at(TK k) const { return peek().kind == k; }
            bool eat(TK k) {
                if (!at(k))
                    return false;
                advance();
                return true;
            }
            const Token &expect(TK k, const char *what) {
                if (!at(k)) {
                    error(std::string("此处应为 ") + what, peek().line, peek().col);
                }
                return advance();
            }
            [[noreturn]] void error(const std::string &msg, int line, int col) const {
                throw ParseError(msg, line, col);
            }
            [[noreturn]] void error(const std::string &msg, SrcLoc loc) const {
                throw ParseError(msg, loc.line, loc.col);
            }
            static SrcLoc loc(const Token &t) { return SrcLoc{t.line, t.col}; }

            // ---------------------------------------------------- 声明项与 pattern

            Item parseItem() {
                size_t save = i_;
                SrcLoc itemLoc = loc(peek());
                // `_` 不能作为键名（HAM 0x00），它是单参函数的语法糖占位符（HAM 0x01）
                if (at(TK::Underscore) &&
                    (peek(1).kind == TK::Eq || peek(1).kind == TK::RecDecl ||
                     peek(1).kind == TK::Colon))
                    error("_ 不能作为键名（单参函数的语法糖占位符）", itemLoc);
                Pattern pat;
                if (tryParsePattern(pat)) {
                    if (at(TK::Eq) || at(TK::RecDecl)) {
                        bool recursive = advance().kind == TK::RecDecl;
                        Item item;
                        item.kind = Item::Kind::Decl;
                        item.loc = itemLoc;
                        item.decl = Decl{std::move(pat), parseExpr(1), recursive};
                        return item;
                    }
                    if (at(TK::Colon)) {
                        // 组合集合的字段只允许裸键名（HAM 0x02），路径不行
                        if (!std::holds_alternative<PatIdent>(pat))
                            error("组合集合的字段名必须是键名，不能是路径", itemLoc);
                        advance();
                        Item item;
                        item.kind = Item::Kind::Field;
                        item.loc = itemLoc;
                        item.field = CombSetField{std::get<PatIdent>(pat).name, parseType()};
                        return item;
                    }
                }
                i_ = save;
                Item item;
                item.kind = Item::Kind::Expr;
                item.loc = itemLoc;
                item.expr = parseExpr(1);
                return item;
            }

            // 试探解析声明左侧的 pattern（HAM 0x00）
            // 失败时不消费任何 token；调用方若发现后面没有 =/<-/: 需自行回退
            bool tryParsePattern(Pattern &out) {
                size_t save = i_;
                if (at(TK::OpName)) {
                    out = PatOp{opName(advance())};
                    return true;
                }
                if (at(TK::Ident)) {
                    std::string base = advance().text;
                    PatPath path{base, {}};
                    bool extended = false;
                    while (true) {
                        if (at(TK::Dot) && peek(1).kind == TK::Ident) {
                            // .x 键段
                            advance();
                            path.segs.push_back(
                                PatSeg{PatSeg::Kind::Key, advance().text, nullptr, {}});
                            extended = true;
                            continue;
                        }
                        if (at(TK::Dot) && peek(1).kind == TK::LBrace) {
                            // .{a, b} 扩展尾段，必须是最后一段
                            advance();
                            advance();
                            if (!at(TK::Ident)) {
                                i_ = save;
                                return false;
                            }
                            PatSeg seg{PatSeg::Kind::ExtKeys, "", nullptr, {}};
                            while (true) {
                                seg.keys.push_back(advance().text);
                                if (eat(TK::Comma)) {
                                    if (at(TK::RBrace))
                                        break;
                                    if (!at(TK::Ident)) {
                                        i_ = save;
                                        return false;
                                    }
                                    continue;
                                }
                                break;
                            }
                            if (!eat(TK::RBrace)) {
                                i_ = save;
                                return false;
                            }
                            path.segs.push_back(std::move(seg));
                            extended = true;
                            break;
                        }
                        if (at(TK::LBracket)) {
                            // [i] 下标段（HAM 0x06：覆写由中括号拿到的引用），
                            // 与键段任意混合（a[0].b = 1）
                            advance();
                            NodePtr idx = parseExpr(1);
                            if (!eat(TK::RBracket)) {
                                i_ = save;
                                return false;
                            }
                            path.segs.push_back(
                                PatSeg{PatSeg::Kind::Index, "", std::move(idx), {}});
                            extended = true;
                            continue;
                        }
                        break;
                    }
                    if (!extended) {
                        out = PatIdent{base};
                    } else {
                        out = std::move(path);
                    }
                    return true;
                }
                if (at(TK::LBrace)) {
                    // 解构 {x, y}：只有纯键列表才是，否则回退给花括号表达式
                    advance();
                    std::vector<PatKeyField> keys;
                    bool ok = at(TK::Ident);
                    while (ok) {
                        keys.push_back(PatKeyField{advance().text, nullptr});
                        if (eat(TK::Comma)) {
                            if (at(TK::RBrace))
                                break;
                            ok = at(TK::Ident);
                            continue;
                        }
                        break;
                    }
                    if (ok && eat(TK::RBrace)) {
                        out = PatDestructure{std::move(keys)};
                        return true;
                    }
                    i_ = save;
                    return false;
                }
                return false;
            }

            // ---------------------------------------------------- 表达式

            // Pratt 主循环（中缀绑定力查 op_table.h，特例构造见下方各分支）
            NodePtr parseExpr(int minBp) {
                // 深度护栏（B2）：嵌套超限抛 ParseError 而不是递归爆栈
                ExprDepthGuard depthGuard{exprDepth_};
                if (exprDepth_ > kMaxNestDepth)
                    throw GuardError("表达式嵌套过深（上限 128）", peek().line,
                                     peek().col);
                NodePtr e = parsePrefix();
                // 参数列表是受限文法：后面紧跟 => 时优先成 λ（HAM 0x06 的
                // arr2 | x => x > 1；优先于任何中缀判定。LHS 含 -> 返回标注的
                // 形态不由这里处理，仍走主循环的 => 分支）
                // 例外：-> 类型链内（stopFatArrow_），=> 是外层参数列表的分隔符
                if (!stopFatArrow_ && at(TK::FatArrow))
                    e = makeLambda(std::move(e), advance());
                int chainLen = 0;
                while (true) {
                    // 链长护栏（B2）：扁平长链会造出过深的左偏树，析构/dump 时爆栈
                    if (++chainLen > kMaxChainLen)
                        throw GuardError("单个表达式的运算符链过长（上限 128）",
                                         peek().line, peek().col);
                    // 后缀（14 级，最紧）：.x、[i]、[]、(...)
                    if (at(TK::Dot)) {
                        Token dot = advance();
                        const Token &key = expect(TK::Ident, "投影键名");
                        e = makeNode(Proj{std::move(e), key.text}, loc(dot));
                        continue;
                    }
                    if (at(TK::LBracket)) {
                        Token br = advance();
                        if (eat(TK::RBracket)) {
                            // T[] 数组集合（HAM 0x06）
                            e = makeNode(ArrayType{std::move(e)}, loc(br));
                        } else {
                            NodePtr idx = parseExpr(1);
                            expect(TK::RBracket, "下标的 ]");
                            e = makeNode(Index{std::move(e), std::move(idx)}, loc(br));
                        }
                        continue;
                    }
                    if (at(TK::LParen)) {
                        Token open = advance();
                        e = makeNode(Call{std::move(e), parseElemList(TK::RParen).first,
                                          false},
                                     loc(open));
                        continue;
                    }
                    // =>（6 级）：函数声明
                    if (at(TK::FatArrow)) {
                        if (6 < minBp)
                            break;
                        Token arrow = advance();
                        e = makeLambda(std::move(e), arrow);
                        continue;
                    }
                    // ->（6 级，右结合）：函数集合类型（HAM 0x02）
                    // 右侧经 parseArrowType 递归吃 ->，lambda 返回值标注在 makeLambda 里拆出；
                    // 右侧不吃 as，保证 `A -> B as C` 按优先级解析为 `(A -> B) as C`
                    if (at(TK::ThinArrow)) {
                        if (6 < minBp)
                            break;
                        Token arrow = advance();
                        e = makeNode(Binary{BinOp::Arrow, std::move(e), parseArrowType()},
                                     loc(arrow));
                        continue;
                    }
                    // where（3 级，左结合）：expr where comb
                    if (at(TK::KwWhere)) {
                        if (3 < minBp)
                            break;
                        Token w = advance();
                        e = makeNode(TempComb{parseExpr(4), std::move(e)}, loc(w));
                        continue;
                    }
                    // as（2 级，左结合）
                    if (at(TK::KwAs)) {
                        if (2 < minBp)
                            break;
                        Token a = advance();
                        e = makeNode(As{std::move(e), parseType()}, loc(a));
                        continue;
                    }
                    // $（14 级，左结合）：f $ (a)，rhs 的结构展开为参数
                    if (at(TK::Dollar)) {
                        if (14 < minBp)
                            break;
                        Token d = advance();
                        NodePtr rhs = parseExpr(14);
                        std::vector<NodePtr> args;
                        if (auto *st = std::get_if<StructLit>(&rhs->kind)) {
                            args = std::move(st->elems);
                        } else {
                            args.push_back(std::move(rhs));
                        }
                        e = makeNode(Call{std::move(e), std::move(args), true}, loc(d));
                        continue;
                    }
                    BinOp op;
                    int level;
                    // 泛型约束解析期间 > 不作为比较运算符（<T: U> 的右尖括号）
                    if (stopGt_ && peek().kind == TK::Gt)
                        break;
                    if (!infixBinOp(peek().kind, op, level) || level < minBp)
                        break;
                    Token tok = advance();
                    e = makeNode(Binary{op, std::move(e), parseExpr(level + 1)}, loc(tok));
                }
                return e;
            }

            // `#op`（TK::OpName）：词法已贪婪成词，这里只做白名单校验（HAM 0x07），
            // 暂不支持自定义新运算符
            std::string opName(const Token &t) const {
                std::string name = t.text.substr(1); // 去掉 '#'
                if (!isKnownOpName(name))
                    error("不支持的运算符 `" + t.text + "`", t.line, t.col);
                return name;
            }

            NodePtr parsePrefix() {
                Token t = peek();
                switch (t.kind) {
                case TK::IntLit:
                    advance();
                    return makeNode(IntLit{t.text}, loc(t));
                case TK::FloatLit:
                    advance();
                    return makeNode(FloatLit{t.text}, loc(t));
                case TK::CharLit:
                    advance();
                    return makeNode(CharLit{t.text}, loc(t));
                case TK::StrLit:
                    advance();
                    return makeNode(StrLit{t.text}, loc(t));
                case TK::KwTrue:
                    advance();
                    return makeNode(BoolLit{true}, loc(t));
                case TK::KwFalse:
                    advance();
                    return makeNode(BoolLit{false}, loc(t));
                case TK::Ident: {
                    advance();
                    NodePtr e = makeNode(Ident{t.text}, loc(t));
                    // x: T 参数标注（HAM 0x02）
                    // 只在括号内或 => 之前合法，其余位置回退后由外层报错
                    if (at(TK::Colon)) {
                        if (parenDepth_ > 0) {
                            advance();
                            e = makeNode(Typed{std::move(e), parseType()}, loc(t));
                        } else {
                            size_t save = i_;
                            advance();
                            NodePtr type = parseType();
                            if (at(TK::FatArrow)) {
                                e = makeNode(Typed{std::move(e), std::move(type)}, loc(t));
                            } else {
                                i_ = save;
                            }
                        }
                    }
                    return e;
                }
                case TK::Underscore:
                    advance();
                    return makeNode(Placeholder{}, loc(t));
                case TK::Backtick:
                    return parseBacktickScope();
                case TK::OpName: {
                    Token op = advance();
                    return makeNode(OpRef{opName(op)}, loc(op));
                }
                case TK::KwIf:
                    return parseIf();
                case TK::KwLet:
                    return parseLet();
                case TK::LParen:
                    return parseParen();
                case TK::LBracket: {
                    Token open = advance();
                    return makeNode(ArrayLit{parseElemList(TK::RBracket).first}, loc(open));
                }
                case TK::LBrace: {
                    Token open = advance();
                    return parseBraced(open);
                }
                case TK::Dot: {
                    // 前导 .x：在 <| 链中投影累积合并（HAM 0x01/0x04）
                    advance();
                    const Token &key = expect(TK::Ident, "投影键名");
                    return makeNode(ContextProj{key.text}, loc(t));
                }
                case TK::Minus: {
                    advance();
                    return makeNode(Unary{UnaryOp::Neg, parseExpr(13)}, loc(t));
                }
                case TK::Bang: {
                    advance();
                    return makeNode(Unary{UnaryOp::Not, parseExpr(13)}, loc(t));
                }
                case TK::Tilde: {
                    advance();
                    return makeNode(Unary{UnaryOp::Complement, parseExpr(13)}, loc(t));
                }
                case TK::Ellipsis:
                case TK::EllipsisAll: {
                    // ...x / ....x 参数包（HAM 0x05），暂存为 Spread，转参数时校验
                    advance();
                    const Token &name = expect(TK::Ident, "参数包名称");
                    NodePtr id = makeNode(Ident{name.text}, loc(name));
                    return makeNode(Spread{std::move(id), t.kind == TK::EllipsisAll}, loc(t));
                }
                case TK::Lt:
                    return parseGenerics();
                default:
                    error("此处应为表达式", t.line, t.col);
                }
            }

            // 反引号 `_` 范围（HAM 0x01）：`` `expr` `` 是单参糖 lambda 的显式定界，
            // 对内至少一个 `_`，多个 `_` 绑同一个参数，内层对的 `_` 绑定内层
            // 内容恰好解析为一个表达式且正好停在反引号才算闭合——解析结果与候选闭
            // 反引号的位置无关（B1：旧实现逐候选重试同一份输入，嵌套后指数爆炸），
            // 单遍解析即可，闭反引号处包成单参糖 lambda
            NodePtr parseBacktickScope() {
                Token open = advance(); // '`'
                NodePtr content;
                try {
                    content = parseExpr(1);
                } catch (const GuardError &) {
                    throw; // 护栏错误是终态，直接传播
                } catch (const ParseError &) {
                    // open 之后还有反引号 → 真语法错误发生在 scope 内
                    // （如 `_ + * 2`），原样抛出；其后没有反引号 → 按未闭合报错
                    for (size_t j = i_; j < toks_.size(); ++j)
                        if (toks_[j].kind == TK::Backtick)
                            throw;
                    error("未闭合的反引号", loc(open));
                }
                if (at(TK::Backtick)) {
                    if (!hasFreePlaceholder(*content))
                        error("反引号对内必须至少有一个 `_`", loc(open));
                    advance(); // 消费闭反引号
                    return wrapSugar(std::move(content));
                }
                error("未闭合的反引号", loc(open));
            }

            // 类型/集合表达式（HAM 0x02/0x03）：允许 -> 右结合，停于 => , ) } >
            // as（2 级）与表达式层一致，松于 ->（6 级）：`A -> B as C` = `(A -> B) as C`
            NodePtr parseType() {
                NodePtr t = parseArrowType();
                if (at(TK::KwAs)) {
                    Token a = advance();
                    t = makeNode(As{std::move(t), parseType()}, loc(a));
                }
                return t;
            }

            // -> 链（6 级，右结合）：类型位置的 as 由 parseType 在其外统一处理
            // -> 的右侧是类型片段：其中的 => 属于外层参数列表（HAM 0x02），
            // 解析期间置 stopFatArrow_，防止它被提前折成 λ
            NodePtr parseArrowType() {
                bool saveStopFatArrow = stopFatArrow_;
                stopFatArrow_ = true;
                NodePtr t = parseExpr(7);
                if (at(TK::ThinArrow)) {
                    Token arrow = advance();
                    t = makeNode(Binary{BinOp::Arrow, std::move(t), parseArrowType()},
                                 loc(arrow));
                }
                stopFatArrow_ = saveStopFatArrow;
                return t;
            }

            // 括号：(...) 单元素无逗号视为分组，否则为结构（HAM 0x01）
            NodePtr parseParen() {
                Token open = advance();
                auto [elems, sawComma] = parseElemList(TK::RParen);
                if (elems.size() == 1 && !sawComma)
                    return std::move(elems[0]);
                return makeNode(StructLit{std::move(elems)}, loc(open));
            }

            // 逗号分隔的元素列表：元素可带 ... / .... 后缀展开（HAM 0x05），允许尾逗号
            std::pair<std::vector<NodePtr>, bool> parseElemList(TK closer) {
                ++parenDepth_;
                std::vector<NodePtr> elems;
                bool sawComma = false;
                if (!eat(closer)) {
                    while (true) {
                        NodePtr e = parseExpr(1);
                        if (at(TK::Ellipsis) || at(TK::EllipsisAll)) {
                            Token sp = advance();
                            e = makeNode(Spread{std::move(e), sp.kind == TK::EllipsisAll},
                                         loc(sp));
                        }
                        elems.push_back(std::move(e));
                        if (eat(TK::Comma)) {
                            sawComma = true;
                            if (eat(closer))
                                break;
                            continue;
                        }
                        expect(closer, "闭合括号");
                        break;
                    }
                }
                --parenDepth_;
                return {std::move(elems), sawComma};
            }

            // {} 的四形态（HAM 0x02）：统一解析项列表后按形态分类
            NodePtr parseBraced(const Token &open) {
                // 条件集合 {...| f}：... 与 | 是两个 token，前瞻识别
                if (at(TK::Ellipsis) && peek(1).kind == TK::Bar) {
                    advance();
                    advance();
                    NodePtr pred = parseExpr(1);
                    expect(TK::RBrace, "条件集合的 }");
                    return makeNode(PredSet{std::move(pred)}, loc(open));
                }
                // 项之间的逗号可选（HAM 0x00），与文件层级一致
                std::vector<Item> items;
                while (!at(TK::RBrace)) {
                    items.push_back(parseItem());
                    if (eat(TK::Comma))
                        continue;
                    // 组合（声明）与组合的集合表达式（字段）可省逗号（HAM 0x00）；
                    // 集合的列举（表达式元素）之间必须逗号
                    if (!at(TK::RBrace) && items.back().kind == Item::Kind::Expr)
                        error("集合元素之间需要逗号", loc(peek()));
                }
                expect(TK::RBrace, "}");
                if (items.empty())
                    return makeNode(CombLit{}, loc(open));
                bool anyDecl = false, allDecl = true, allField = true, allExpr = true;
                for (const auto &it : items) {
                    anyDecl |= it.kind == Item::Kind::Decl;
                    allDecl &= it.kind == Item::Kind::Decl;
                    allField &= it.kind == Item::Kind::Field;
                    allExpr &= it.kind == Item::Kind::Expr;
                }
                if (anyDecl) {
                    if (!allDecl)
                        error("组合字面量中混入了非声明项", loc(open));
                    CombLit comb;
                    for (auto &it : items)
                        comb.items.push_back(std::move(it.decl));
                    return makeNode(std::move(comb), loc(open));
                }
                if (allField) {
                    CombSet cs;
                    for (auto &it : items)
                        cs.fields.push_back(std::move(it.field));
                    return makeNode(std::move(cs), loc(open));
                }
                if (allExpr) {
                    EnumSet es;
                    for (auto &it : items)
                        es.elems.push_back(std::move(it.expr));
                    return makeNode(std::move(es), loc(open));
                }
                error("{} 内混合了声明、集合字段与表达式", loc(open));
            }

            NodePtr parseIf() {
                Token ifTok = advance();
                expect(TK::LParen, "if 条件的 (");
                NodePtr cond = parseExpr(1);
                expect(TK::RParen, "if 条件的 )");
                // then 延伸到 else 为止（else 天然界定）；
                // else 与 λ 同级（6 级）：is/as、let in/where、|>、<|、<~
                // 作用于整个 if，要作用于 else 分支本身请加括号（HAM 0x02）
                NodePtr thenB = parseExpr(1);
                NodePtr elseB;
                if (eat(TK::KwElse))
                    elseB = parseExpr(6);
                return makeNode(IfExpr{std::move(cond), std::move(thenB), std::move(elseB)},
                                loc(ifTok));
            }

            // let comb in body（3 级）：comb 部分自然停于 KwIn（KwIn 无中缀含义）
            NodePtr parseLet() {
                Token letTok = advance();
                NodePtr comb = parseExpr(4);
                expect(TK::KwIn, "let ... in 的 in");
                NodePtr body = parseExpr(3);
                return makeNode(TempComb{std::move(comb), std::move(body)}, loc(letTok));
            }

            // 泛型（HAM 0x06）：<T: U, V> 后必须跟一个函数声明
            NodePtr parseGenerics() {
                Token lt = advance();
                std::vector<GenericParam> gens;
                while (true) {
                    const Token &name = expect(TK::Ident, "泛型参数名");
                    NodePtr bound;
                    if (eat(TK::Colon)) {
                        // 约束解析期间把 > 视为终止符，否则 U> 的 > 会被当成比较
                        stopGt_ = true;
                        bound = parseType();
                        stopGt_ = false;
                    }
                    gens.push_back(GenericParam{name.text, std::move(bound)});
                    if (eat(TK::Comma))
                        continue;
                    break;
                }
                expect(TK::Gt, "泛型参数列表的 >");
                NodePtr e = parseExpr(6);
                auto *lam = std::get_if<Lambda>(&e->kind);
                if (!lam)
                    error("泛型参数列表后应为函数声明", loc(lt));
                lam->generics = std::move(gens);
                return e;
            }

            // => 的 led：左侧转换为参数列表；若左侧是 -> 则拆出返回值标注（HAM 0x02）
            NodePtr makeLambda(NodePtr lhs, const Token &arrow) {
                NodePtr ret;
                NodePtr paramsPart = std::move(lhs);
                if (auto *b = std::get_if<Binary>(&paramsPart->kind);
                    b && b->op == BinOp::Arrow) {
                    ret = std::move(b->rhs);
                    paramsPart = std::move(b->lhs);
                }
                Lambda lam;
                lam.params = toParams(paramsPart, arrow);
                lam.returnType = std::move(ret);
                lam.body = parseExpr(6);
                return makeNode(std::move(lam), loc(arrow));
            }

            // 把 => 左侧的节点转换为参数列表：Ident / 结构逐项
            // 合法项：x、x: T、...x、....x（HAM 0x01/0x02/0x05）、组合解构（HAM 0x01）
            std::vector<Param> toParams(NodePtr &node, const Token &at) {
                std::vector<Param> out;
                std::function<Param(NodePtr &)> convOne = [&](NodePtr &n) -> Param {
                    if (auto *id = std::get_if<Ident>(&n->kind))
                        return Param{PatIdent{id->name}, nullptr};
                    if (auto *t = std::get_if<Typed>(&n->kind)) {
                        Param p = convOne(t->expr);
                        p.type = std::move(t->type);
                        return p;
                    }
                    if (auto *s = std::get_if<Spread>(&n->kind)) {
                        Param p = convOne(s->expr);
                        p.pack = s->all ? PackKind::All : PackKind::Rest;
                        return p;
                    }
                    // 组合解构参数（HAM 0x01：函数的参数可以匹配组合）
                    // ({ x, y }) 归组后是 EnumSet；({ x: { 1 } }) 是 CombSet
                    if (auto *es = std::get_if<EnumSet>(&n->kind)) {
                        PatDestructure pat;
                        for (auto &el : es->elems) {
                            auto *k = std::get_if<Ident>(&el->kind);
                            if (!k)
                                error("非法的参数列表", loc(at));
                            pat.keys.push_back(PatKeyField{k->name, nullptr});
                        }
                        return Param{std::move(pat), nullptr};
                    }
                    if (auto *cs = std::get_if<CombSet>(&n->kind)) {
                        PatDestructure pat;
                        for (auto &f : cs->fields)
                            pat.keys.push_back(PatKeyField{f.name, std::move(f.type)});
                        return Param{std::move(pat), nullptr};
                    }
                    if (std::holds_alternative<CombLit>(n->kind))
                        error("组合模式参数不能写声明；要约束键的取值请写成 { x: { 1 } }",
                              loc(at));
                    error("非法的参数列表", loc(at));
                };
                if (auto *st = std::get_if<StructLit>(&node->kind)) {
                    for (auto &el : st->elems)
                        out.push_back(convOne(el));
                } else {
                    out.push_back(convOne(node));
                }
                return out;
            }

            // 无条件包成单参糖 lambda（调用方已确认子树含自由 `_`）
            NodePtr wrapSugar(NodePtr e) {
                SrcLoc l = e->loc;
                Lambda lam;
                lam.params.push_back(Param{PatIdent{"_"}, nullptr});
                lam.body = std::move(e);
                lam.sugar = true;
                return makeNode(std::move(lam), l);
            }
        };

    } // namespace

    NodePtr parse(const std::vector<Token> &tokens) {
        NodePtr root = Parser(tokens).run();
        checkPlaceholdersDelimited(*root);
        return root;
    }

} // namespace castam
