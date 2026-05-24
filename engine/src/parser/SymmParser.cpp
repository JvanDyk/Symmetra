#include "SymmParser.h"
#include <cctype>
#include <stdexcept>
#include <cassert>
#include <set>

namespace symm {

static const std::set<std::string> KEYWORDS = {
    "import","from","export","const","let","var","async","await",
    "return","if","else","new","true","false","null","undefined",
    "function","class","extends","super","this",
    "for","while","do","of","in","break","continue",
    "try","catch","finally","throw","switch","case","default",
    "typeof","void","instanceof","yield","static","get","set",
};

// ── Tokenizer ─────────────────────────────────────────────────────────────────

void SymmParser::tokenize() {
    _tokens.clear();
    size_t i = 0;
    int line = 1;
    auto src = _src;

    auto peek = [&](int off = 1) -> char {
        return (i + off < src.size()) ? src[i + off] : '\0';
    };

    while (i < src.size()) {
        char c = src[i];

        if (c == '\n') { ++line; ++i; continue; }
        if (std::isspace(c)) { ++i; continue; }

        // Single-line comment
        if (c == '/' && peek() == '/') {
            while (i < src.size() && src[i] != '\n') ++i;
            continue;
        }
        // Multi-line comment
        if (c == '/' && peek() == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i+1] == '/')) {
                if (src[i] == '\n') ++line;
                ++i;
            }
            i += 2;
            continue;
        }

        // String literal
        if (c == '"' || c == '\'') {
            char q = c; ++i;
            std::string s;
            while (i < src.size() && src[i] != q) {
                if (src[i] == '\\') { ++i; s += src[i]; }
                else                  s += src[i];
                ++i;
            }
            ++i;
            _tokens.push_back({Token::String, s, line});
            continue;
        }

        // Template literal (simplified — no nested templates)
        if (c == '`') {
            ++i;
            std::string s;
            while (i < src.size() && src[i] != '`') {
                if (src[i] == '\n') ++line;
                s += src[i++];
            }
            ++i;
            _tokens.push_back({Token::Template, s, line});
            continue;
        }

        // Number
        if (std::isdigit(c) || (c == '.' && std::isdigit(peek()))) {
            std::string n;
            while (i < src.size() && (std::isdigit(src[i]) || src[i] == '.' || src[i] == 'e' ||
                                      src[i] == 'E' || src[i] == '-' || src[i] == '+' ||
                                      src[i] == 'x' || src[i] == 'b' || src[i] == 'o' ||
                                      std::isxdigit(src[i]))) {
                n += src[i++];
            }
            _tokens.push_back({Token::Number, n, line});
            continue;
        }

        // Identifier or keyword
        if (std::isalpha(c) || c == '_' || c == '$') {
            std::string id;
            while (i < src.size() && (std::isalnum(src[i]) || src[i] == '_' || src[i] == '$')) {
                id += src[i++];
            }
            Token::Kind k = KEYWORDS.count(id) ? Token::Keyword : Token::Ident;
            _tokens.push_back({k, id, line});
            continue;
        }

        // Regex literal: /pattern/flags — detected by context (not after value tokens)
        if (c == '/' && peek() != '/' && peek() != '*') {
            bool isRegex = false;
            if (_tokens.empty()) {
                isRegex = true;
            } else {
                auto& last = _tokens.back();
                if (last.kind == Token::Punct) {
                    // After ), ], } or postfix ++/-- → division; everything else → regex
                    isRegex = (last.value != ")" && last.value != "]" && last.value != "}"
                               && last.value != "++" && last.value != "--");
                } else if (last.kind == Token::Number || last.kind == Token::String
                           || last.kind == Token::Template) {
                    isRegex = false;
                } else if (last.kind == Token::Ident) {
                    isRegex = false;
                } else if (last.kind == Token::Keyword) {
                    static const std::set<std::string> regexAfter = {
                        "return","typeof","instanceof","in","of","delete","void",
                        "throw","new","case","else","yield"
                    };
                    isRegex = regexAfter.count(last.value) > 0;
                } else {
                    isRegex = true;
                }
            }
            if (isRegex) {
                ++i;
                std::string pat;
                bool inCls = false;
                while (i < src.size() && src[i] != '\n') {
                    char rc = src[i];
                    if (!inCls && rc == '/') break;
                    if (rc == '[') inCls = true;
                    else if (rc == ']') inCls = false;
                    if (rc == '\\' && i + 1 < src.size()) { pat += rc; pat += src[++i]; ++i; continue; }
                    pat += rc; ++i;
                }
                if (i < src.size() && src[i] == '/') ++i; // closing /
                std::string flags;
                while (i < src.size() && std::isalpha(src[i])) flags += src[i++];
                _tokens.push_back({Token::String, "/" + pat + "/" + flags, line});
                continue;
            }
        }

        // Multi-char punctuation — 3-char first, then 2-char, then 1-char
        if (i + 2 < src.size()) {
            char c1 = src[i+1], c2 = src[i+2];
            if (c == '.' && c1 == '.' && c2 == '.') { _tokens.push_back({Token::Punct, "...", line}); i += 3; continue; }
            if (c == '=' && c1 == '=' && c2 == '=') { _tokens.push_back({Token::Punct, "===", line}); i += 3; continue; }
            if (c == '!' && c1 == '=' && c2 == '=') { _tokens.push_back({Token::Punct, "!==", line}); i += 3; continue; }
            if (c == '>' && c1 == '>' && c2 == '>') { _tokens.push_back({Token::Punct, ">>>", line}); i += 3; continue; }
            if (c == '*' && c1 == '*' && c2 == '=') { _tokens.push_back({Token::Punct, "**=", line}); i += 3; continue; }
        }
        if (i + 1 < src.size()) {
            std::string two = std::string(1, c) + src[i+1];
            if (two == "=>" || two == "==" || two == "!=" || two == "<=" ||
                two == ">=" || two == "&&" || two == "||" || two == "++" ||
                two == "--" || two == "**" || two == "??" || two == "::" ||
                two == "+=" || two == "-=" || two == "*=" || two == "/=" ||
                two == "%=" || two == "<<" || two == ">>") {
                _tokens.push_back({Token::Punct, two, line});
                i += 2; continue;
            }
        }
        _tokens.push_back({Token::Punct, std::string(1, c), line});
        ++i;
    }
    _tokens.push_back({Token::Eof, "", line});
}

SymmParser::Token& SymmParser::cur() { return _tokens[_tok]; }
SymmParser::Token& SymmParser::peek(int off) {
    size_t idx = _tok + off;
    return idx < _tokens.size() ? _tokens[idx] : _tokens.back();
}
SymmParser::Token SymmParser::consume() {
    Token t = _tokens[_tok];
    if (_tok + 1 < _tokens.size()) ++_tok;
    return t;
}
bool SymmParser::match(const std::string& val) {
    if (cur().value == val) { consume(); return true; }
    return false;
}
bool SymmParser::expect(const std::string& val) {
    if (cur().value != val) {
        error("expected '" + val + "', got '" + cur().value + "'", cur().line);
        return false;
    }
    consume();
    return true;
}
void SymmParser::error(const std::string& msg, int line) {
    _errors.push_back(_file + ":" + std::to_string(line == -1 ? cur().line : line) + ": " + msg);
}

// ── Parser entry ──────────────────────────────────────────────────────────────

Module SymmParser::parse(const std::string& js, const std::string& filename) {
    _src  = js;
    _file = filename;
    _pos  = 0;
    _tok  = 0;
    _errors.clear();
    _tokens.clear();

    tokenize();
    Module mod = parseModule();

    // Extract routes and serve config from top-level statements
    for (auto& stmt : mod.stmts) {
        extractRoutes(stmt, mod);
        extractServe(stmt, mod);
    }

    return mod;
}

Module SymmParser::parseModule() {
    Module mod;
    while (cur().kind != Token::Eof) {
        if (cur().value == "import") {
            mod.imports.push_back(parseImport());
        } else {
            NodePtr s = parseStmt();
            if (s) mod.stmts.push_back(s);
        }
    }
    return mod;
}

NodePtr SymmParser::parseStmt() {
    auto tok = cur();

    // ── TypeScript / ES module keywords ────────────────────────────────────────
    // export [default] <decl> — strip the 'export' (and optional 'default') and
    // parse the underlying declaration normally.
    if (tok.value == "export") {
        consume();
        if (cur().value == "default") consume();
        // export type ... / export interface ... — skip
        if ((cur().kind == Token::Ident && (cur().value == "type" || cur().value == "interface"))
            && peek().kind == Token::Ident) {
            // handled by the type/interface branches below — just parse next stmt
        }
        return parseStmt();
    }

    // interface Foo { ... } — TypeScript interface declaration: skip entirely.
    if (tok.kind == Token::Ident && tok.value == "interface") {
        consume(); // 'interface'
        if (cur().kind == Token::Ident || cur().kind == Token::Keyword) consume(); // name
        // optional extends clause
        if (cur().kind == Token::Ident && cur().value == "extends") {
            consume();
            // consume list of base types separated by commas
            while (cur().value != "{" && cur().kind != Token::Eof) consume();
        }
        // skip generic type params
        if (cur().value == "<") {
            int d = 1; consume();
            while (d > 0 && cur().kind != Token::Eof) {
                if (cur().value == "<") d++; else if (cur().value == ">") d--;
                consume();
            }
        }
        // skip body { ... }
        if (cur().value == "{") {
            int d = 1; consume();
            while (d > 0 && cur().kind != Token::Eof) {
                if (cur().value == "{") d++; else if (cur().value == "}") d--;
                consume();
            }
        }
        return nullptr;
    }

    // type Alias = ... — TypeScript type alias: skip.
    // Only treat 'type' as a keyword when the next token is an identifier and then '=' or '<'
    if (tok.kind == Token::Ident && tok.value == "type") {
        auto& nxt = peek();
        if (nxt.kind == Token::Ident || nxt.value == "{") {
            consume(); // 'type'
            // consume name
            if (cur().kind == Token::Ident || cur().kind == Token::Keyword) consume();
            // skip generic params
            if (cur().value == "<") {
                int d = 1; consume();
                while (d > 0 && cur().kind != Token::Eof) {
                    if (cur().value == "<") d++; else if (cur().value == ">") d--;
                    consume();
                }
            }
            // skip = and type body (stop at semicolon or next keyword)
            if (cur().value == "=") {
                consume();
                // consume tokens until semicolon or newline-guarded keyword
                // For block types { ... } handle braces; otherwise scan to ';'
                if (cur().value == "{") {
                    int d = 1; consume();
                    while (d > 0 && cur().kind != Token::Eof) {
                        if (cur().value == "{") d++; else if (cur().value == "}") d--;
                        consume();
                    }
                } else {
                    while (cur().value != ";" && cur().value != "}" &&
                           cur().kind != Token::Eof) consume();
                }
            }
            match(";");
            return nullptr;
        }
    }

    // enum Name { A = val, B, C } — TypeScript/JS enum: emit as __device__ constexpr ints.
    // We convert to a Block of ConstDecl nodes so existing codegen handles them.
    if (tok.kind == Token::Ident && tok.value == "enum") {
        consume(); // 'enum'
        std::string enumName;
        if (cur().kind == Token::Ident || cur().kind == Token::Keyword)
            enumName = consume().value;
        expect("{");
        // Build a flat block of const int declarations
        Block blk; blk.flat = true;
        int autoVal = 0;
        while (cur().value != "}" && cur().kind != Token::Eof) {
            // member name
            std::string memberName;
            if (cur().kind == Token::Ident || cur().kind == Token::Keyword || cur().kind == Token::String)
                memberName = consume().value;
            else { consume(); match(","); continue; }
            // optional initializer
            NodePtr initExpr;
            if (cur().value == "=") {
                consume();
                initExpr = parseExpr();
                // try to get the integer value for autoVal tracking
                if (initExpr && initExpr->kind == NodeKind::Literal) {
                    auto& l = std::get<Literal>(initExpr->data);
                    if (l.kind == Literal::Int)   try { autoVal = std::stoi(l.raw) + 1; } catch(...) {}
                    if (l.kind == Literal::Float) try { autoVal = (int)std::stof(l.raw) + 1; } catch(...) {}
                } else {
                    autoVal++; // unknown expression — just increment
                }
            } else {
                // auto-increment: build integer literal node
                auto lit = std::make_shared<Node>(); lit->kind = NodeKind::Literal;
                lit->data = Literal{Literal::Int, std::to_string(autoVal)};
                initExpr = lit;
                autoVal++;
            }
            // Emit as: const EnumName_MemberName = value;
            // Using ConstDecl with name = "EnumName_MemberName", isMut = false
            auto cd = std::make_shared<Node>(); cd->kind = NodeKind::ConstDecl;
            cd->data = ConstDecl{enumName + "_" + memberName, initExpr, false};
            blk.stmts.push_back(cd);
            match(",");
        }
        expect("}");
        match(";");
        auto n = std::make_shared<Node>(); n->kind = NodeKind::Block;
        n->data = blk; n->line = tok.line;
        return n;
    }

    // declare keyword — TypeScript ambient declaration: skip.
    if (tok.kind == Token::Ident && tok.value == "declare") {
        consume();
        // skip until end of declaration
        while (cur().value != ";" && cur().value != "}" &&
               cur().kind != Token::Eof) {
            if (cur().value == "{") {
                int d = 1; consume();
                while (d > 0 && cur().kind != Token::Eof) {
                    if (cur().value == "{") d++; else if (cur().value == "}") d--;
                    consume();
                }
            } else {
                consume();
            }
        }
        match(";");
        return nullptr;
    }

    // abstract class — strip 'abstract' and parse normally
    if (tok.kind == Token::Ident && tok.value == "abstract") {
        consume();
        return parseStmt();
    }

    if (tok.value == "const" || tok.value == "let" || tok.value == "var") {
        bool mut = tok.value != "const";
        consume();
        return parseConst(mut);
    }
    if (tok.value == "return") {
        consume();
        NodePtr expr;
        if (cur().value != ";" && cur().value != "}" && cur().kind != Token::Eof)
            expr = parseExpr();
        match(";");
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::ReturnStmt;
        n->data = ReturnStmt{expr};
        n->line = tok.line;
        return n;
    }
    if (tok.value == "if") {
        consume();
        expect("(");
        auto cond = parseExpr();
        expect(")");
        auto then = (cur().value == "{") ? parseBlock() : parseStmt();
        NodePtr else_;
        if (cur().value == "else") {
            consume();
            else_ = (cur().value == "if") ? parseStmt() : (cur().value == "{") ? parseBlock() : parseStmt();
        }
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::IfStmt;
        n->data = IfStmt{cond, then, else_};
        n->line = tok.line;
        return n;
    }
    if (tok.value == "for")      return parseFor();
    if (tok.value == "while")    return parseWhile();
    if (tok.value == "do")       return parseDoWhile();
    if (tok.value == "try")      return parseTryCatch();
    if (tok.value == "switch")   return parseSwitch();
    if (tok.value == "throw") {
        consume();
        auto expr = parseExpr();
        match(";");
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::ThrowStmt;
        n->data = ThrowStmt{expr};
        n->line = tok.line;
        return n;
    }
    if (tok.value == "break") {
        consume(); match(";");
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::BreakStmt;
        n->data = BreakStmt{};
        n->line = tok.line;
        return n;
    }
    if (tok.value == "continue") {
        consume(); match(";");
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::ContinueStmt;
        n->data = ContinueStmt{};
        n->line = tok.line;
        return n;
    }
    if (tok.value == "class")    return parseClass();
    if (tok.value == "function") return parseFunctionDecl(false);
    if (tok.value == "async" && peek().value == "function") {
        consume();
        return parseFunctionDecl(true);
    }
    if (tok.value == "{")        return parseBlock();
    if (tok.value == ";")        { consume(); return nullptr; }

    // Expression statement (may include assignment)
    auto expr = parseExpr();
    match(";");
    if (!expr) return nullptr;
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::ExprStmt;
    n->data = ExprStmt{expr};
    n->line = tok.line;
    return n;
}

NodePtr SymmParser::parseImport() {
    int line = cur().line;
    expect("import");
    ImportDecl decl;

    if (cur().value == "{") {
        consume();
        while (cur().value != "}") {
            if (cur().kind == Token::Ident) decl.names.push_back(consume().value);
            match(",");
        }
        expect("}");
    } else if (cur().kind == Token::Ident) {
        decl.names.push_back(consume().value);
    }

    if (cur().value == "from") {
        consume();
        if (cur().kind == Token::String) decl.from = consume().value;
    }
    match(";");

    auto n = std::make_shared<Node>();
    n->kind = NodeKind::ImportDecl;
    n->data = decl;
    n->line = line;
    return n;
}

NodePtr SymmParser::parseConst(bool isMut) {
    int line = cur().line;

    // ── Array destructuring: const [a, b, ...rest] = expr ──────────────────────
    if (cur().value == "[") {
        consume();
        std::vector<std::string> names;
        std::string restName;
        while (cur().value != "]" && cur().kind != Token::Eof) {
            if (cur().value == "...") {
                consume();
                if (cur().kind == Token::Ident) restName = consume().value;
            } else if (cur().kind == Token::Ident) {
                names.push_back(consume().value);
                if (cur().value == "=") { consume(); parseExpr(); } // skip default
            } else if (cur().value == "{" || cur().value == "[") {
                std::string open = cur().value, close = (open == "{") ? "}" : "]";
                int d = 1; consume();
                while (cur().kind != Token::Eof && d > 0) {
                    if (cur().value == open) d++;
                    else if (cur().value == close) d--;
                    consume();
                }
                names.push_back("_nested" + std::to_string(names.size()));
            } else { consume(); }
            match(",");
        }
        expect("]");
        if (cur().value == ":") { consume(); parseTypeAnnotation(); }
        expect("=");
        auto init = parseExpr();
        match(";");

        Block blk; blk.flat = true;
        // If RHS is an array literal, bind directly to elements (avoids temp)
        if (init && init->kind == NodeKind::ArrayExpr) {
            auto& ae = std::get<ArrayExpr>(init->data);
            for (size_t k = 0; k < names.size(); k++) {
                if (names[k].empty() || names[k].substr(0,7) == "_nested") continue;
                NodePtr val = (k < ae.elements.size()) ? ae.elements[k] : nullptr;
                auto decl = std::make_shared<Node>(); decl->kind = NodeKind::ConstDecl;
                decl->data = ConstDecl{names[k], val, isMut}; decl->line = line;
                blk.stmts.push_back(decl);
            }
        } else {
            std::string tmp = "_da" + std::to_string(_tmpCnt++);
            auto tmpDecl = std::make_shared<Node>(); tmpDecl->kind = NodeKind::ConstDecl;
            tmpDecl->data = ConstDecl{tmp, init, false}; tmpDecl->line = line;
            blk.stmts.push_back(tmpDecl);
            for (size_t k = 0; k < names.size(); k++) {
                if (names[k].empty() || names[k].substr(0,7) == "_nested") continue;
                auto idx = std::make_shared<Node>(); idx->kind = NodeKind::Literal;
                idx->data = Literal{Literal::Int, std::to_string(k)};
                auto ref = std::make_shared<Node>(); ref->kind = NodeKind::Ident;
                ref->data = Ident{tmp};
                auto sub = std::make_shared<Node>(); sub->kind = NodeKind::BinaryExpr;
                sub->data = BinaryExpr{"[]", ref, idx};
                auto decl = std::make_shared<Node>(); decl->kind = NodeKind::ConstDecl;
                decl->data = ConstDecl{names[k], sub, isMut}; decl->line = line;
                blk.stmts.push_back(decl);
            }
        }
        if (!restName.empty()) {
            auto ref = std::make_shared<Node>(); ref->kind = NodeKind::Ident;
            ref->data = Ident{"nullptr"};
            auto decl = std::make_shared<Node>(); decl->kind = NodeKind::ConstDecl;
            decl->data = ConstDecl{restName, ref, isMut}; decl->line = line;
            blk.stmts.push_back(decl);
        }
        auto n = std::make_shared<Node>(); n->kind = NodeKind::Block;
        n->data = blk; n->line = line; return n;
    }

    // ── Object destructuring: const { a, b, ...rest } = expr ───────────────────
    if (cur().value == "{") {
        consume();
        struct Bind { std::string key, alias; };
        std::vector<Bind> bindings;
        std::string restName;
        while (cur().value != "}" && cur().kind != Token::Eof) {
            if (cur().value == "...") {
                consume();
                if (cur().kind == Token::Ident) restName = consume().value;
            } else if (cur().kind == Token::Ident || cur().kind == Token::String || cur().kind == Token::Keyword) {
                std::string key = consume().value;
                std::string alias = key;
                if (match(":")) {
                    if (cur().kind == Token::Ident) { alias = consume().value; }
                    else if (cur().value == "{" || cur().value == "[") {
                        // Nested pattern — skip
                        std::string open = cur().value, close = (open == "{") ? "}" : "]";
                        int d = 1; consume();
                        while (cur().kind != Token::Eof && d > 0) {
                            if (cur().value == open) d++;
                            else if (cur().value == close) d--;
                            consume();
                        }
                        alias = "_skip";
                    }
                }
                if (cur().value == "=") { consume(); parseExpr(); } // skip default
                if (alias != "_skip") bindings.push_back({key, alias});
            } else { consume(); }
            match(",");
        }
        expect("}");
        if (cur().value == ":") { consume(); parseTypeAnnotation(); }
        expect("=");
        auto init = parseExpr();
        match(";");

        std::string tmp = "_do" + std::to_string(_tmpCnt++);
        Block blk; blk.flat = true;
        auto tmpDecl = std::make_shared<Node>(); tmpDecl->kind = NodeKind::ConstDecl;
        tmpDecl->data = ConstDecl{tmp, init, false}; tmpDecl->line = line;
        blk.stmts.push_back(tmpDecl);
        for (auto& b : bindings) {
            auto ref = std::make_shared<Node>(); ref->kind = NodeKind::Ident;
            ref->data = Ident{tmp};
            auto mem = std::make_shared<Node>(); mem->kind = NodeKind::MemberExpr;
            mem->data = MemberExpr{ref, b.key, false};
            auto decl = std::make_shared<Node>(); decl->kind = NodeKind::ConstDecl;
            decl->data = ConstDecl{b.alias, mem, isMut}; decl->line = line;
            blk.stmts.push_back(decl);
        }
        if (!restName.empty()) {
            auto ref = std::make_shared<Node>(); ref->kind = NodeKind::Ident;
            ref->data = Ident{tmp};
            auto decl = std::make_shared<Node>(); decl->kind = NodeKind::ConstDecl;
            decl->data = ConstDecl{restName, ref, isMut}; decl->line = line;
            blk.stmts.push_back(decl);
        }
        auto n = std::make_shared<Node>(); n->kind = NodeKind::Block;
        n->data = blk; n->line = line; return n;
    }

    // ── Simple: const name = expr ───────────────────────────────────────────────
    std::string name;
    if (cur().kind == Token::Ident) name = consume().value;
    if (cur().value == ":") { consume(); while (cur().value != "=" && cur().kind != Token::Eof) consume(); }
    expect("=");
    auto init = parseExpr();
    match(";");
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::ConstDecl;
    n->data = ConstDecl{name, init, isMut};
    n->line = line;
    return n;
}

bool SymmParser::isAssignOp(const std::string& op) {
    return op == "=" || op == "+=" || op == "-=" || op == "*=" ||
           op == "/=" || op == "%=" || op == "**=" || op == "&&=" ||
           op == "||=" || op == "??=";
}

int SymmParser::precedence(const std::string& op) {
    if (op == "||" || op == "??")           return 1;
    if (op == "&&")                         return 2;
    if (op == "|")                          return 3;
    if (op == "^")                          return 4;
    if (op == "&")                          return 5;
    if (op == "==" || op == "!=" ||
        op == "===" || op == "!==")         return 6;
    if (op == "<" || op == ">" ||
        op == "<=" || op == ">=")           return 7;
    if (op == "instanceof" || op == "in")   return 7;
    if (op == "<<" || op == ">>" ||
        op == ">>>")                        return 8;  // JS: shifts above comparisons
    if (op == "+" || op == "-")             return 9;
    if (op == "*" || op == "/" ||
        op == "%")                          return 10;
    if (op == "**")                         return 11;
    return 0;
}

NodePtr SymmParser::parseExpr(int prec) {
    auto left = parseUnary();
    left = parsePostfix(left);

    // TypeScript 'as' type assertion: value as Type  →  value  (strip type annotation)
    // TypeScript 'satisfies' operator: value satisfies Type  →  value
    while (cur().kind == Token::Ident &&
           (cur().value == "as" || cur().value == "satisfies")) {
        consume(); // 'as' or 'satisfies'
        parseTypeAnnotation(); // skip type
        // Re-apply postfix in case the cast was inside an expression:
        // (x as string).length — after stripping 'as string', we want .length
        left = parsePostfix(left);
    }

    // Right-associative assignment (only at top level, prec==0)
    if (prec == 0 && isAssignOp(cur().value)) {
        auto op = consume().value;
        auto right = parseExpr(0);
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::AssignExpr;
        n->data = AssignExpr{op, left, right};
        return n;
    }

    // Ternary operator: cond ? a : b
    if (prec == 0 && cur().value == "?") {
        consume();  // ?
        auto thenE = parseExpr(0);
        expect(":");
        auto elseE = parseExpr(0);
        // Emit as IfStmt-like (binary with "?" op)
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::BinaryExpr;
        n->data = BinaryExpr{"?:", left, thenE};
        // Chain the else: store as the "right" of a second node
        auto ternary = std::make_shared<Node>();
        ternary->kind = NodeKind::BinaryExpr;
        ternary->data = BinaryExpr{"?:else", n, elseE};
        return ternary;
    }

    while (true) {
        int p = precedence(cur().value);
        if (p <= prec) break;
        auto op = consume().value;
        auto right = parseExpr(p);
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::BinaryExpr;
        n->data = BinaryExpr{op, left, right};
        left = n;
    }
    return left;
}

NodePtr SymmParser::parseUnary() {
    auto tok = cur();

    if (tok.value == "!" || tok.value == "-" || tok.value == "~" || tok.value == "+") {
        consume();
        // In JS, unary prefix operators have lower precedence than member access,
        // so `-a.b` means `-(a.b)`, not `(-a).b`. Apply postfix after inner unary.
        auto expr = parsePostfix(parseUnary());
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::UnaryExpr;
        n->data = UnaryExpr{tok.value, expr};
        n->line = tok.line;
        return n;
    }
    if (tok.value == "++" || tok.value == "--") {
        consume();
        auto expr = parsePostfix(parseUnary());
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::UnaryExpr;
        n->data = UnaryExpr{tok.value, expr, false};
        n->line = tok.line;
        return n;
    }
    if (tok.value == "typeof") {
        consume();
        // typeof a.b.c — member access has higher precedence than typeof
        auto expr = parsePostfix(parseUnary());
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::TypeofExpr;
        n->data = TypeofExpr{expr};
        n->line = tok.line;
        return n;
    }
    if (tok.value == "void") {
        consume();
        auto expr = parsePostfix(parseUnary());
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::VoidExpr;
        n->data = VoidExpr{expr};
        n->line = tok.line;
        return n;
    }
    if (tok.value == "await") {
        consume();
        // await fn() — call expression must be picked up via postfix
        auto expr = parsePostfix(parseUnary());
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::AwaitExpr;
        n->data = AwaitExpr{expr};
        n->line = tok.line;
        return n;
    }
    if (tok.value == "async") {
        consume();
        if (cur().value == "(") {
            consume();
            auto params = parseParamList();
            expect(")");
            if (cur().value == ":") { consume(); parseTypeAnnotation(); }
            expect("=>");
            return parseArrow(params, true);
        }
        if (cur().value == "function") return parseFunctionDecl(true);
        // async ident => body
        if (cur().kind == Token::Ident) {
            std::string param = consume().value;
            if (match("=>")) return parseArrow({param}, true);
        }
    }
    if (tok.value == "yield") {
        consume();
        NodePtr expr;
        if (cur().value != ";" && cur().value != "}" && cur().kind != Token::Eof)
            expr = parseExpr();
        // Emit as a call-like node (generators desugar to state machines later)
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::AwaitExpr;  // treat yield same as await for now
        n->data = AwaitExpr{expr};
        n->line = tok.line;
        return n;
    }
    return parsePrimary();
}

NodePtr SymmParser::parsePostfix(NodePtr left) {
    while (true) {
        // Optional chaining: obj?.prop or obj?.(args) or obj?.[key]
        if (cur().value == "?" && peek().value == ".") {
            consume(); consume();  // skip ?  .
            std::string prop = cur().kind != Token::Eof ? consume().value : "";
            auto n = std::make_shared<Node>();
            n->kind = NodeKind::OptChainExpr;
            n->data = OptChainExpr{left, prop};
            left = n;
            if (cur().value == "(") left = parseCall(left);
            continue;
        }
        if (cur().value == ".") {
            consume();
            std::string prop = cur().value;
            consume();

            // Special GPU array methods: .map, .filter, .reduce
            if ((prop == "map" || prop == "filter" || prop == "reduce") && cur().value == "(") {
                consume();
                auto fn = parseExpr();
                NodePtr init;
                if (prop == "reduce" && match(",")) init = parseExpr();
                expect(")");
                auto n = std::make_shared<Node>();
                if (prop == "map")    { n->kind = NodeKind::MapCall;    n->data = MapCall{left, fn, nullptr}; }
                if (prop == "filter") { n->kind = NodeKind::FilterCall; n->data = FilterCall{left, fn}; }
                if (prop == "reduce") { n->kind = NodeKind::ReduceCall; n->data = ReduceCall{left, fn, init}; }
                left = n;
            } else {
                auto n = std::make_shared<Node>();
                n->kind = NodeKind::MemberExpr;
                n->data = MemberExpr{left, prop};
                left = n;
                if (cur().value == "(") left = parseCall(left);
            }
        } else if (cur().value == "[") {
            // Subscript access: arr[i] — emit as MemberExpr with computed flag
            consume();
            auto idx = parseExpr();
            expect("]");
            auto n = std::make_shared<Node>();
            n->kind = NodeKind::MemberExpr;
            n->data = MemberExpr{left, ""/*prop unused*/, true};
            // Store subscript in a wrapper CallExpr for now
            // We'll encode it as a binary "[]" op
            auto sub = std::make_shared<Node>();
            sub->kind = NodeKind::BinaryExpr;
            sub->data = BinaryExpr{"[]", left, idx};
            left = sub;
            if (cur().value == "(") left = parseCall(left);
        } else if (cur().value == "(") {
            left = parseCall(left);
        } else if (cur().value == "++" || cur().value == "--") {
            // Postfix increment/decrement
            auto op = consume().value;
            auto n = std::make_shared<Node>();
            n->kind = NodeKind::UnaryExpr;
            n->data = UnaryExpr{op + "_post", left, true};
            left = n;
        } else {
            break;
        }
    }
    return left;
}

std::string SymmParser::parseTypeAnnotation() {
    // Skip a TypeScript type annotation up to the next meaningful token.
    // We count angle-bracket depth to handle generic types like Array<Foo<Bar>>.
    int depth = 0;
    while (cur().kind != Token::Eof) {
        std::string v = cur().value;
        if (v == "<")             { depth++; consume(); continue; }
        if (v == ">" && depth>0)  { depth--; consume(); continue; }
        if (depth > 0)            { consume(); continue; }
        // At depth 0, stop at these delimiters
        if (v == "=" || v == "," || v == ")" || v == "{" || v == ";" ||
            v == "=>" || v == "|" /* union */ )
            break;
        consume();
    }
    return {};
}

NodePtr SymmParser::parsePrimary() {
    auto tok = cur();

    // 'this' and 'super' — emit as Ident
    if (tok.value == "this" || tok.value == "super") {
        consume();
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::Ident;
        n->data = Ident{tok.value};
        n->line = tok.line;
        return n;
    }

    if (tok.value == "new") {
        consume();
        // Handle new.target (esbuild may produce this)
        if (cur().value == "." && peek().value == "target") {
            consume(); consume();
            auto n = std::make_shared<Node>();
            n->kind = NodeKind::Literal;
            n->data = Literal{Literal::Null, "null"};
            return n;
        }
        // Generic type params: new Map<K,V>() — skip type args
        std::string cls;
        if (cur().kind == Token::Ident) cls = consume().value;
        if (cur().value == "<") {
            int d = 1; consume();
            while (cur().kind != Token::Eof && d > 0) {
                if (cur().value == "<") d++;
                else if (cur().value == ">") d--;
                consume();
            }
        }
        std::vector<NodePtr> args;
        if (cur().value == "(") {
            consume();
            args = parseArgList();
            expect(")");
        }
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::NewExpr;
        n->data = NewExpr{cls, args};
        n->line = tok.line;
        return n;
    }

    if (tok.value == "(") {
        consume();
        // Check for arrow: (params) => body
        if (cur().value == ")" || (cur().kind == Token::Ident && peek().value == "," )
            || (cur().kind == Token::Ident && peek().value == ")")) {
            // might be param list
            size_t save = _tok;
            auto params = parseParamList();
            if (cur().value == ")") {
                consume();
                if (cur().value == "=>" || cur().value == ":") {
                    if (cur().value == ":") { consume(); while (cur().value != "=>" && cur().kind != Token::Eof) consume(); }
                    if (match("=>")) return parseArrow(params, false);
                }
            }
            _tok = save;
        }
        auto expr = parseExpr();
        expect(")");
        return expr;
    }

    if (tok.kind == Token::Ident) {
        consume();
        // Single param arrow: x => expr
        if (cur().value == "=>") {
            consume();
            return parseArrow({tok.value}, false);
        }
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::Ident;
        n->data = Ident{tok.value};
        n->line = tok.line;
        return n;
    }

    if (tok.kind == Token::Number) {
        consume();
        bool isFloat = tok.value.find('.') != std::string::npos
                    || tok.value.find('e') != std::string::npos;
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::Literal;
        n->data = Literal{isFloat ? Literal::Float : Literal::Int, tok.value};
        n->line = tok.line;
        return n;
    }

    if (tok.kind == Token::String) {
        consume();
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::Literal;
        n->data = Literal{Literal::Str, tok.value};
        n->line = tok.line;
        return n;
    }

    if (tok.value == "true" || tok.value == "false") {
        consume();
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::Literal;
        n->data = Literal{Literal::Bool, tok.value};
        n->line = tok.line;
        return n;
    }

    if (tok.value == "null" || tok.value == "undefined") {
        consume();
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::Literal;
        n->data = Literal{Literal::Null, tok.value};
        n->line = tok.line;
        return n;
    }

    if (tok.kind == Token::Template) {
        return parseTemplate();
    }

    if (tok.value == "[") return parseArray();
    if (tok.value == "{") return parseObject();

    // Unknown — skip and return null literal
    consume();
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::Literal;
    n->data = Literal{Literal::Null, "null"};
    return n;
}

NodePtr SymmParser::parseArrow(std::vector<std::string> params, bool isAsync) {
    NodePtr body;
    if (cur().value == "{") {
        body = parseBlock();
    } else {
        body = parseExpr();
    }
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::FuncDecl;
    n->data = FuncDecl{"", params, body, isAsync, false};
    return n;
}

NodePtr SymmParser::parseBlock() {
    expect("{");
    Block blk;
    while (cur().value != "}" && cur().kind != Token::Eof) {
        auto s = parseStmt();
        if (s) blk.stmts.push_back(s);
    }
    expect("}");
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::Block;
    n->data = blk;
    return n;
}

NodePtr SymmParser::parseCall(NodePtr callee) {
    expect("(");
    auto args = parseArgList();
    expect(")");
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::CallExpr;
    n->data = CallExpr{callee, args};
    return n;
}

std::vector<NodePtr> SymmParser::parseArgList() {
    std::vector<NodePtr> args;
    while (cur().value != ")" && cur().kind != Token::Eof) {
        if (cur().value == "...") consume(); // spread — include expr
        args.push_back(parseExpr());
        if (!match(",")) break;
    }
    return args;
}

std::vector<std::string> SymmParser::parseParamList() {
    std::vector<std::string> params;
    while (cur().value != ")" && cur().kind != Token::Eof) {
        match("...");  // rest params
        // Skip access modifiers (public, private, protected, readonly)
        while (cur().value == "public" || cur().value == "private" ||
               cur().value == "protected" || cur().value == "readonly") consume();
        // Destructuring patterns — collect the full bracketed expression, use placeholder
        if (cur().value == "{" || cur().value == "[") {
            std::string delim = cur().value;
            std::string close = (delim == "{") ? "}" : "]";
            int d = 1; consume();
            std::string pat;
            while (cur().kind != Token::Eof && d > 0) {
                if (cur().value == delim) d++;
                else if (cur().value == close) { if (--d == 0) break; }
                pat += cur().value; consume();
            }
            expect(close);
            params.push_back("_destruct_" + std::to_string(params.size()));
        } else if (cur().kind == Token::Ident) {
            params.push_back(consume().value);
        } else {
            consume();
        }
        // Type annotation: name: Type
        if (cur().value == ":") { consume(); parseTypeAnnotation(); }
        // Default value: name = expr
        if (cur().value == "=") { consume(); parseExpr(); }
        if (!match(",")) break;
    }
    return params;
}

NodePtr SymmParser::parseArray() {
    expect("[");
    ArrayExpr arr;
    while (cur().value != "]" && cur().kind != Token::Eof) {
        if (cur().value == ",") { match(","); continue; } // sparse hole
        if (cur().value == "...") consume();              // spread — include expr
        arr.elements.push_back(parseExpr());
        match(",");
    }
    expect("]");
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::ArrayExpr;
    n->data = arr;
    return n;
}

NodePtr SymmParser::parseObject() {
    expect("{");
    ObjectExpr obj;
    while (cur().value != "}" && cur().kind != Token::Eof) {
        // Spread: { ...src }
        if (cur().value == "...") {
            consume();
            auto spreadVal = parseExpr();
            obj.props.push_back({"__spread__", spreadVal});
            match(","); continue;
        }
        // Computed key: { [expr]: val }
        if (cur().value == "[") {
            consume();
            while (cur().value != "]" && cur().kind != Token::Eof) consume();
            expect("]");
            if (cur().value == ":") { consume(); parseExpr(); }
            obj.props.push_back({"__computed__", nullptr});
            match(","); continue;
        }
        // Key
        std::string key;
        if (cur().kind == Token::String || cur().kind == Token::Number)
            key = consume().value;
        else if (cur().kind == Token::Ident || cur().kind == Token::Keyword)
            key = consume().value;
        else { consume(); match(","); continue; }

        // Skip generic type params on method names
        if (cur().value == "<") {
            int d = 1; consume();
            while (cur().kind != Token::Eof && d > 0) {
                if (cur().value == "<") d++;
                else if (cur().value == ">") d--;
                consume();
            }
        }

        NodePtr val;
        if (cur().value == "(") {
            // Method shorthand: { method(params) { body } }
            consume();
            auto params = parseParamList();
            expect(")");
            if (cur().value == ":") { consume(); parseTypeAnnotation(); }
            NodePtr body;
            if (cur().value == "{") body = parseBlock();
            else match(";");
            auto fn = std::make_shared<Node>(); fn->kind = NodeKind::FuncDecl;
            fn->data = FuncDecl{key, params, body, false, false};
            val = fn;
        } else if (cur().value == ":") {
            consume();
            val = parseExpr();
        } else {
            // Shorthand: { name } → { name: name }
            auto n = std::make_shared<Node>(); n->kind = NodeKind::Ident;
            n->data = Ident{key}; val = n;
        }
        obj.props.push_back({key, val});
        match(",");
    }
    expect("}");
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::ObjectExpr;
    n->data = obj;
    return n;
}

NodePtr SymmParser::parseTemplate() {
    auto tok = consume(); // Template token
    const std::string& raw = tok.value;

    // Fast path: no ${ } interpolation
    if (raw.find("${") == std::string::npos) {
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::TemplateLit;
        n->data = TemplateLit{{raw}, {}};
        n->line = tok.line;
        return n;
    }

    // Save parser state so we can re-tokenize sub-expressions
    auto savedTokens = _tokens;
    auto savedTok    = _tok;
    auto savedSrc    = _src;

    TemplateLit tmpl;
    size_t i = 0;

    while (true) {
        // Collect string part up to next ${
        std::string part;
        while (i < raw.size()) {
            if (raw[i] == '$' && i + 1 < raw.size() && raw[i+1] == '{') break;
            if (raw[i] == '\\' && i + 1 < raw.size()) {
                char esc = raw[++i];
                if      (esc == 'n')  part += '\n';
                else if (esc == 't')  part += '\t';
                else if (esc == 'r')  part += '\r';
                else                  part += esc;
                ++i;
            } else {
                part += raw[i++];
            }
        }
        tmpl.parts.push_back(part);

        if (i >= raw.size()) break;  // done
        i += 2;  // skip ${

        // Extract expression string up to matching }
        int depth = 1;
        std::string exprSrc;
        while (i < raw.size() && depth > 0) {
            if      (raw[i] == '{') depth++;
            else if (raw[i] == '}') { if (--depth == 0) { ++i; break; } }
            exprSrc += raw[i++];
        }

        // Re-tokenize the sub-expression and parse it
        _src = exprSrc;
        _tokens.clear();
        _tok = 0;
        tokenize();
        auto exprNode = parseExpr();
        tmpl.exprs.push_back(exprNode);
    }

    // Restore parser state
    _tokens = savedTokens;
    _tok    = savedTok;
    _src    = savedSrc;

    auto n = std::make_shared<Node>();
    n->kind = NodeKind::TemplateLit;
    n->data = tmpl;
    n->line = tok.line;
    return n;
}

// ── New statement parsers ─────────────────────────────────────────────────────

NodePtr SymmParser::parseFunctionDecl(bool isAsync) {
    int line = cur().line;
    expect("function");
    bool isGenerator = match("*");
    std::string name;
    if (cur().kind == Token::Ident) name = consume().value;
    // Skip generic type params
    if (cur().value == "<") {
        int d = 1; consume();
        while (cur().kind != Token::Eof && d > 0) {
            if (cur().value == "<") d++;
            else if (cur().value == ">") d--;
            consume();
        }
    }
    expect("(");
    auto params = parseParamList();
    expect(")");
    // Skip return type annotation
    if (cur().value == ":") { consume(); parseTypeAnnotation(); }
    auto body = parseBlock();
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::FuncDecl;
    n->data = FuncDecl{name, params, body, isAsync, isGenerator};
    n->line = line;
    return n;
}

NodePtr SymmParser::parseFor() {
    int line = cur().line;
    expect("for");
    // `for await (const x of iter)` — consume the optional 'await' keyword
    bool isForAwait = false;
    if (cur().value == "await") { isForAwait = true; consume(); }
    expect("(");

    // Detect for...of and for...in
    // Look ahead: if first token after ( is const/let/var followed by ident followed by of/in
    bool isConst = cur().value == "const" || cur().value == "let" || cur().value == "var";
    if (isConst && (peek().kind == Token::Ident || peek().value == "{" || peek().value == "[")) {
        bool c = cur().value == "const";
        consume();  // const/let/var
        std::string varName;
        if (cur().kind == Token::Ident) varName = consume().value;
        // Skip destructuring patterns
        else if (cur().value == "{" || cur().value == "[") {
            int d = 1; consume();
            while (cur().kind != Token::Eof && d > 0) {
                if (cur().value == "{" || cur().value == "[") d++;
                else if (cur().value == "}" || cur().value == "]") d--;
                if (d > 0) varName += consume().value;
                else consume();
            }
        }
        // Skip type annotation
        if (cur().value == ":") { consume(); parseTypeAnnotation(); }

        if (cur().value == "of") {
            consume();
            auto iter = parseExpr();
            expect(")");
            auto body = (cur().value == "{") ? parseBlock() : parseStmt();
            auto n = std::make_shared<Node>();
            n->kind = NodeKind::ForOfStmt;
            n->data = ForOfStmt{varName.empty() ? "_it" : varName, c, isForAwait, iter, body};
            n->line = line;
            return n;
        }
        if (cur().value == "in") {
            consume();
            auto obj = parseExpr();
            expect(")");
            auto body = (cur().value == "{") ? parseBlock() : parseStmt();
            auto n = std::make_shared<Node>();
            n->kind = NodeKind::ForInStmt;
            n->data = ForInStmt{varName.empty() ? "_k" : varName, c, obj, body};
            n->line = line;
            return n;
        }
        // Fall through to traditional for with initialiser already parsed
        // Reconstruct the init as a ConstDecl
        NodePtr init;
        NodePtr initVal;
        if (cur().value == "=") { consume(); initVal = parseExpr(); }
        auto initNode = std::make_shared<Node>();
        initNode->kind = NodeKind::ConstDecl;
        initNode->data = ConstDecl{varName, initVal, !c};
        init = initNode;

        expect(";");
        NodePtr cond, update;
        if (cur().value != ";") cond = parseExpr();
        expect(";");
        if (cur().value != ")") update = parseExpr();
        expect(")");
        auto body = (cur().value == "{") ? parseBlock() : parseStmt();
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::ForStmt;
        n->data = ForStmt{init, cond, update, body};
        n->line = line;
        return n;
    }

    // Traditional for (init; cond; update)
    NodePtr init;
    if (cur().value != ";") {
        auto e = parseExpr();
        auto ni = std::make_shared<Node>();
        ni->kind = NodeKind::ExprStmt;
        ni->data = ExprStmt{e};
        init = ni;
    }
    expect(";");
    NodePtr cond, update;
    if (cur().value != ";") cond = parseExpr();
    expect(";");
    if (cur().value != ")") update = parseExpr();
    expect(")");
    auto body = (cur().value == "{") ? parseBlock() : parseStmt();
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::ForStmt;
    n->data = ForStmt{init, cond, update, body};
    n->line = line;
    return n;
}

NodePtr SymmParser::parseWhile() {
    int line = cur().line;
    expect("while");
    expect("(");
    auto cond = parseExpr();
    expect(")");
    auto body = (cur().value == "{") ? parseBlock() : parseStmt();
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::WhileStmt;
    n->data = WhileStmt{cond, body};
    n->line = line;
    return n;
}

NodePtr SymmParser::parseDoWhile() {
    int line = cur().line;
    expect("do");
    auto body = parseBlock();
    expect("while");
    expect("(");
    auto cond = parseExpr();
    expect(")");
    match(";");
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::DoWhileStmt;
    n->data = DoWhileStmt{body, cond};
    n->line = line;
    return n;
}

NodePtr SymmParser::parseTryCatch() {
    int line = cur().line;
    expect("try");
    auto tryBody = parseBlock();

    std::string catchParam;
    NodePtr catchBody, finallyBody;

    if (cur().value == "catch") {
        consume();
        if (cur().value == "(") {
            consume();
            if (cur().kind == Token::Ident) catchParam = consume().value;
            // Skip type annotation
            if (cur().value == ":") { consume(); parseTypeAnnotation(); }
            expect(")");
        }
        catchBody = parseBlock();
    }
    if (cur().value == "finally") {
        consume();
        finallyBody = parseBlock();
    }

    auto n = std::make_shared<Node>();
    n->kind = NodeKind::TryCatchStmt;
    n->data = TryCatchStmt{tryBody, catchParam, catchBody, finallyBody};
    n->line = line;
    return n;
}

NodePtr SymmParser::parseSwitch() {
    int line = cur().line;
    expect("switch");
    expect("(");
    auto test = parseExpr();
    expect(")");
    expect("{");

    std::vector<SwitchCase> cases;
    while (cur().value != "}" && cur().kind != Token::Eof) {
        SwitchCase sc;
        if (cur().value == "case") {
            consume();
            sc.test = parseExpr();
            expect(":");
        } else if (cur().value == "default") {
            consume();
            expect(":");
            sc.test = nullptr;  // default case
        } else {
            break;
        }
        while (cur().value != "case" && cur().value != "default" &&
               cur().value != "}" && cur().kind != Token::Eof) {
            auto s = parseStmt();
            if (s) sc.body.push_back(s);
        }
        cases.push_back(std::move(sc));
    }
    expect("}");

    auto n = std::make_shared<Node>();
    n->kind = NodeKind::SwitchStmt;
    n->data = SwitchStmt{test, cases};
    n->line = line;
    return n;
}

NodePtr SymmParser::parseClass() {
    int line = cur().line;
    expect("class");
    std::string name;
    if (cur().kind == Token::Ident) name = consume().value;

    // Skip generic type params: class Foo<T, K extends V>
    std::vector<std::string> typeParams;
    if (cur().value == "<") {
        consume();
        while (cur().value != ">" && cur().kind != Token::Eof) {
            if (cur().kind == Token::Ident) typeParams.push_back(consume().value);
            else consume();
        }
        expect(">");
    }

    std::string superName;
    if (cur().value == "extends") {
        consume();
        if (cur().kind == Token::Ident) superName = consume().value;
        // Skip generic params on super: extends Base<T>
        if (cur().value == "<") {
            int d = 1; consume();
            while (cur().kind != Token::Eof && d > 0) {
                if (cur().value == "<") d++;
                else if (cur().value == ">") d--;
                consume();
            }
        }
    }
    // implements clause (TS)
    if (cur().value == "implements") {
        consume();
        while (cur().value != "{" && cur().kind != Token::Eof) consume();
    }

    expect("{");

    ClassDecl cls;
    cls.name       = name;
    cls.superName  = superName;
    cls.typeParams = typeParams;

    while (cur().value != "}" && cur().kind != Token::Eof) {
        // Skip decorators
        if (cur().value == "@") {
            while (cur().value != "\n" && cur().kind != Token::Eof) consume();
            continue;
        }
        // Modifiers
        bool isStatic = match("static");
        bool isAsync  = match("async");
        bool isGetter = false, isSetter = false;
        if (cur().value == "get" && peek().kind == Token::Ident) { consume(); isGetter = true; }
        if (cur().value == "set" && peek().kind == Token::Ident) { consume(); isSetter = true; }
        bool isGenerator = match("*");
        bool isPrivate = (cur().value.size() > 0 && cur().value[0] == '#');

        // Field or method name
        std::string memberName;
        if (cur().kind == Token::Ident || cur().kind == Token::Keyword ||
            cur().kind == Token::String) {
            memberName = consume().value;
            if (memberName[0] == '#') { memberName = memberName.substr(1); isPrivate = true; }
        } else if (cur().value == "[") {
            // Computed member name — scan for known Symbol.* names
            consume();
            std::string inner;
            while (cur().value != "]" && cur().kind != Token::Eof) inner += consume().value;
            expect("]");
            // Map known Symbol.* → internal names that codegen recognises
            if (inner == "Symbol.asyncIterator") memberName = "__asyncIterator__";
            else if (inner == "Symbol.iterator")  memberName = "__iterator__";
            else if (inner == "Symbol.toPrimitive") memberName = "__toPrimitive__";
            else if (inner == "Symbol.hasInstance") memberName = "__hasInstance__";
            else memberName = "__computed__";
        } else {
            consume();  // skip unknown
            continue;
        }

        // Skip type params
        if (cur().value == "<") {
            int d = 1; consume();
            while (cur().kind != Token::Eof && d > 0) {
                if (cur().value == "<") d++;
                else if (cur().value == ">") d--;
                consume();
            }
        }

        if (cur().value == "(") {
            // Method
            consume();
            auto params = parseParamList();
            expect(")");
            // Skip return type
            if (cur().value == ":") { consume(); parseTypeAnnotation(); }
            NodePtr body;
            if (cur().value == "{") body = parseBlock();
            else { match(";"); }  // abstract method

            bool isCtor = (memberName == "constructor");
            ClassMethod m{ memberName, params, body, isStatic, isCtor, isGetter, isSetter, isAsync };
            cls.methods.push_back(m);
        } else {
            // Field declaration
            // Skip type annotation: name: Type
            if (cur().value == ":") { consume(); parseTypeAnnotation(); }
            NodePtr init;
            if (cur().value == "=") { consume(); init = parseExpr(); }
            match(";");
            ClassField f{ memberName, init, isPrivate, isStatic };
            cls.fields.push_back(f);
        }
    }
    expect("}");

    auto n = std::make_shared<Node>();
    n->kind = NodeKind::ClassDecl;
    n->data = cls;
    n->line = line;
    return n;
}

// ── Semantic extraction ───────────────────────────────────────────────────────

static std::string resolveStringLiteral(const NodePtr& n) {
    if (!n) return "";
    if (n->kind == NodeKind::Literal) {
        return std::get<Literal>(n->data).raw;
    }
    return "";
}

void SymmParser::extractRoutes(const NodePtr& node, Module& mod) {
    if (!node) return;

    // Look for: app.get('/path', handler) or app.post(...) etc.
    if (node->kind == NodeKind::ExprStmt) {
        auto& es = std::get<ExprStmt>(node->data);
        extractRoutes(es.expr, mod);
        return;
    }

    if (node->kind == NodeKind::CallExpr) {
        auto& call = std::get<CallExpr>(node->data);
        if (call.callee && call.callee->kind == NodeKind::MemberExpr) {
            auto& mem = std::get<MemberExpr>(call.callee->data);
            std::string method = mem.prop;
            if ((method == "get" || method == "post" || method == "put" ||
                 method == "delete" || method == "patch") && call.args.size() >= 2) {
                RouteReg rr;
                rr.method  = method;
                for (char& c : rr.method) c = std::toupper(c);
                rr.path    = resolveStringLiteral(call.args[0]);
                rr.handler = call.args[1];
                mod.routes.push_back(rr);
            }
        }
    }
}

void SymmParser::extractServe(const NodePtr& node, Module& mod) {
    if (!node) return;
    if (node->kind == NodeKind::ExprStmt) {
        auto& es = std::get<ExprStmt>(node->data);
        extractServe(es.expr, mod);
        return;
    }
    if (node->kind == NodeKind::CallExpr) {
        auto& call = std::get<CallExpr>(node->data);
        if (call.callee && call.callee->kind == NodeKind::Ident) {
            if (std::get<Ident>(call.callee->data).name == "serve" && call.args.size() >= 2) {
                // serve(app, { port: 8080, ... })
                if (call.args[1]->kind == NodeKind::ObjectExpr) {
                    auto& obj = std::get<ObjectExpr>(call.args[1]->data);
                    for (auto& [k, v] : obj.props) {
                        if (k == "port" && v->kind == NodeKind::Literal)
                            mod.servePort = std::stoi(std::get<Literal>(v->data).raw);
                    }
                }
            }
        }
    }
}

} // namespace symm
