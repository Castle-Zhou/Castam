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
            int parenDepth_ = 0;         // 括号嵌套深度，用于判断 x: T 标注是否合法
            bool stopGt_ = false;        // 解析泛型约束时把 > 视为终止符
            bool suppressSugar_ = false; // 反引号对内抑制隐式 `_` 包装（HAM 0x01）

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
                    if (at(TK::Colon) && std::holds_alternative<PatIdent>(pat)) {
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
                    PatPath path{base, {}, {}};
                    bool extended = false;
                    while (at(TK::Dot)) {
                        if (peek(1).kind == TK::Ident) {
                            advance();
                            path.segs.push_back(advance().text);
                            extended = true;
                        } else if (peek(1).kind == TK::LBrace) {
                            // .{a, b} 扩展尾段，必须是最后一段
                            advance();
                            advance();
                            if (!at(TK::Ident)) {
                                i_ = save;
                                return false;
                            }
                            while (true) {
                                path.extKeys.push_back(advance().text);
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
                            extended = true;
                            break;
                        } else {
                            break;
                        }
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
                    std::vector<std::string> keys;
                    bool ok = at(TK::Ident);
                    while (ok) {
                        keys.push_back(advance().text);
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

            // ------------------------------------------------------------ 表达式

            // Pratt 主循环
            // minBp ≤ 10 返回时，若子树含自由 `_` 则包成糖 lambda（HAM 0x01）
            // 这让 `|`/`&`（9 级）的 rhs 恰好在边界上（arr | _ > 1），
            // 而比较/算术/前缀运算的操作数不提前包裹
            // 反引号对内由 suppressSugar_ 抑制，改在闭反引号处统一包裹
            NodePtr parseExpr(int minBp) {
                NodePtr e = parsePrefix();
                while (true) {
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
                    // as（2 级，左结合）：左侧含自由 `_` 时先包成糖 lambda
                    // 使 _ + 1 as Int -> Int 语义为给整个函数标注类型
                    if (at(TK::KwAs)) {
                        if (2 < minBp)
                            break;
                        Token a = advance();
                        e = makeNode(As{wrapIfPlaceholder(std::move(e)), parseType()}, loc(a));
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
                if (minBp <= 10)
                    e = wrapIfPlaceholder(std::move(e));
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
            // 闭反引号的位置由试探解析决定：逐个候选试，要求内容恰好解析为一个
            // 完整表达式且含顶层自由 `_`（如 `` `_ <| `_ + 1`` `` 中第一个 `` ` ``
            // 不能与第二个配对，因为 `_ <|` 不是完整表达式）
            NodePtr parseBacktickScope() {
                Token open = advance(); // '`'
                bool sawNoPlaceholder = false;
                for (size_t j = i_; j < toks_.size(); ++j) {
                    if (toks_[j].kind != TK::Backtick)
                        continue;
                    size_t save = i_;
                    int saveParenDepth = parenDepth_;
                    bool saveStopGt = stopGt_;
                    bool saveSuppress = suppressSugar_;
                    suppressSugar_ = true;
                    NodePtr content;
                    try {
                        content = parseExpr(1);
                    } catch (const ParseError &) {
                        content = nullptr;
                    }
                    size_t stop = i_;
                    i_ = save;
                    parenDepth_ = saveParenDepth;
                    stopGt_ = saveStopGt;
                    suppressSugar_ = saveSuppress;
                    // stop 越过 j 说明候选被嵌套反引号消耗，提前停下同理，都拒绝
                    if (!content || stop != j)
                        continue;
                    if (!hasFreePlaceholder(*content)) {
                        sawNoPlaceholder = true;
                        continue;
                    }
                    i_ = j + 1; // 消费闭反引号
                    return wrapSugar(std::move(content));
                }
                if (sawNoPlaceholder)
                    error("反引号对内必须至少有一个 `_`", loc(open));
                error("未闭合的反引号", loc(open));
            }

            // 类型/集合表达式（HAM 0x02/0x03）：允许 -> 右结合，停于 => , ) } >
            // as（2 级）与表达式层一致，松于 ->（6 级）：`A -> B as C` = `(A -> B) as C`
            NodePtr parseType() {
                NodePtr t = parseArrowType();
                if (at(TK::KwAs)) {
                    Token a = advance();
                    t = makeNode(As{wrapIfPlaceholder(std::move(t)), parseType()}, loc(a));
                }
                return t;
            }

            // -> 链（6 级，右结合）：类型位置的 as 由 parseType 在其外统一处理
            NodePtr parseArrowType() {
                NodePtr t = parseExpr(7);
                if (at(TK::ThinArrow)) {
                    Token arrow = advance();
                    t = makeNode(Binary{BinOp::Arrow, std::move(t), parseArrowType()},
                                 loc(arrow));
                }
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
                    eat(TK::Comma);
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
                NodePtr thenB = parseExpr(1);
                NodePtr elseB;
                if (eat(TK::KwElse))
                    elseB = parseExpr(1);
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
            // 合法项：x、x: T、...x、....x（HAM 0x01/0x02/0x05）
            std::vector<Param> toParams(NodePtr &node, const Token &at) {
                std::vector<Param> out;
                std::function<Param(NodePtr &)> convOne = [&](NodePtr &n) -> Param {
                    if (auto *id = std::get_if<Ident>(&n->kind))
                        return Param{id->name, nullptr};
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
                lam.params.push_back(Param{"_", nullptr});
                lam.body = std::move(e);
                lam.sugar = true;
                return makeNode(std::move(lam), l);
            }

            // `_` 糖：子树含自由 Placeholder 时包成单参 lambda（HAM 0x01）
            // 反引号对内（suppressSugar_）抑制，统一在闭反引号处包装
            NodePtr wrapIfPlaceholder(NodePtr e) {
                if (suppressSugar_ || !hasFreePlaceholder(*e))
                    return e;
                return wrapSugar(std::move(e));
            }

            static bool hasFreePlaceholder(const Node &n) {
                return std::visit(
                    [](const auto &v) {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, Placeholder>) {
                            return true;
                        } else if constexpr (std::is_same_v<T, Lambda>) {
                            // 糖 lambda 的 `_` 已被绑定，不再向外泄露
                            if (v.sugar)
                                return false;
                            return hasFreePlaceholder(*v.body);
                        } else {
                            return anyChildPlaceholder(v);
                        }
                    },
                    n.kind);
            }

            // 检查各节点种类的直接子节点（Lambda/Placeholder 已在上面特判）
            static bool anyOf(const std::vector<NodePtr> &v) {
                for (const auto &p : v)
                    if (hasFreePlaceholder(*p))
                        return true;
                return false;
            }
            static bool anyChildPlaceholder(const IntLit &) { return false; }
            static bool anyChildPlaceholder(const FloatLit &) { return false; }
            static bool anyChildPlaceholder(const CharLit &) { return false; }
            static bool anyChildPlaceholder(const StrLit &) { return false; }
            static bool anyChildPlaceholder(const BoolLit &) { return false; }
            static bool anyChildPlaceholder(const Ident &) { return false; }
            static bool anyChildPlaceholder(const OpRef &) { return false; }
            static bool anyChildPlaceholder(const Placeholder &) { return true; }
            static bool anyChildPlaceholder(const CombLit &v) {
                for (const auto &d : v.items)
                    if (hasFreePlaceholder(*d.value))
                        return true;
                return false;
            }
            static bool anyChildPlaceholder(const CombSet &v) {
                for (const auto &f : v.fields)
                    if (hasFreePlaceholder(*f.type))
                        return true;
                return false;
            }
            static bool anyChildPlaceholder(const EnumSet &v) { return anyOf(v.elems); }
            static bool anyChildPlaceholder(const PredSet &v) {
                return hasFreePlaceholder(*v.pred);
            }
            static bool anyChildPlaceholder(const Lambda &v) {
                return hasFreePlaceholder(*v.body);
            }
            static bool anyChildPlaceholder(const Binary &v) {
                return hasFreePlaceholder(*v.lhs) || hasFreePlaceholder(*v.rhs);
            }
            static bool anyChildPlaceholder(const As &v) {
                return hasFreePlaceholder(*v.expr) || hasFreePlaceholder(*v.type);
            }
            static bool anyChildPlaceholder(const Unary &v) {
                return hasFreePlaceholder(*v.expr);
            }
            static bool anyChildPlaceholder(const IfExpr &v) {
                return hasFreePlaceholder(*v.cond) || hasFreePlaceholder(*v.thenBranch) ||
                       (v.elseBranch && hasFreePlaceholder(*v.elseBranch));
            }
            static bool anyChildPlaceholder(const TempComb &v) {
                return hasFreePlaceholder(*v.comb) || hasFreePlaceholder(*v.body);
            }
            static bool anyChildPlaceholder(const Proj &v) {
                return hasFreePlaceholder(*v.obj);
            }
            static bool anyChildPlaceholder(const ContextProj &) { return false; }
            static bool anyChildPlaceholder(const Index &v) {
                return hasFreePlaceholder(*v.obj) || hasFreePlaceholder(*v.index);
            }
            static bool anyChildPlaceholder(const ArrayType &v) {
                return hasFreePlaceholder(*v.elem);
            }
            static bool anyChildPlaceholder(const Call &v) {
                return hasFreePlaceholder(*v.callee) || anyOf(v.args);
            }
            static bool anyChildPlaceholder(const ArrayLit &v) { return anyOf(v.elems); }
            static bool anyChildPlaceholder(const StructLit &v) { return anyOf(v.elems); }
            static bool anyChildPlaceholder(const Spread &v) {
                return hasFreePlaceholder(*v.expr);
            }
            static bool anyChildPlaceholder(const Typed &v) {
                return hasFreePlaceholder(*v.expr) || hasFreePlaceholder(*v.type);
            }
        };

    } // namespace

    NodePtr parse(const std::vector<Token> &tokens) { return Parser(tokens).run(); }

} // namespace castam
