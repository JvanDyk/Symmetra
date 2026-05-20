#include "SymmParser.h"
#include <cctype>
#include <stdexcept>
#include <cassert>
#include <set>

namespace symm {

static const std::set<std::string> KEYWORDS = {
    "import","from","export","const","let","var","async","await",
    "return","if","else","new","true","false","null","undefined",
    "function","class","extends","for","while","of","in","break","continue",
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

        // Multi-char punctuation
        std::string p(1, c);
        if (i+1 < src.size()) {
            std::string two = p + src[i+1];
            if (two == "=>" || two == "==" || two == "!=" || two == "<=" ||
                two == ">=" || two == "&&" || two == "||" || two == "++" ||
                two == "--" || two == "**" || two == "??" || two == "::" ||
                two == "+=" || two == "-=" || two == "*=" || two == "/=") {
                _tokens.push_back({Token::Punct, two, line});
                i += 2; continue;
            }
            if (two == "===" || two == "!==") {
                if (i+2 < src.size() && (src[i+2] == '=' || src[i+2] == '=')) {
                    _tokens.push_back({Token::Punct, two + src[i+2], line});
                    i += 3; continue;
                }
            }
        }
        _tokens.push_back({Token::Punct, p, line});
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

    if (tok.value == "const" || tok.value == "let") {
        bool mut = tok.value == "let";
        consume();
        return parseConst(mut);
    }
    if (tok.value == "return") {
        consume();
        auto expr = parseExpr();
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
        auto then = parseBlock();
        NodePtr else_;
        if (cur().value == "else") {
            consume();
            else_ = (cur().value == "if") ? parseStmt() : parseBlock();
        }
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::IfStmt;
        n->data = IfStmt{cond, then, else_};
        n->line = tok.line;
        return n;
    }
    if (tok.value == "{") {
        return parseBlock();
    }

    // Expression statement
    auto expr = parseExpr();
    match(";");
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
    std::string name;
    if (cur().kind == Token::Ident) name = consume().value;
    // Ignore type annotation: `: Type`
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

int SymmParser::precedence(const std::string& op) {
    if (op == "||" || op == "??")       return 1;
    if (op == "&&")                     return 2;
    if (op == "==" || op == "!=" ||
        op == "===" || op == "!==")     return 3;
    if (op == "<" || op == ">" ||
        op == "<=" || op == ">=")       return 4;
    if (op == "+" || op == "-")         return 5;
    if (op == "*" || op == "/" ||
        op == "%")                      return 6;
    if (op == "**")                     return 7;
    return 0;
}

NodePtr SymmParser::parseExpr(int prec) {
    auto left = parseUnary();
    left = parsePostfix(left);

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
    if (tok.value == "!" || tok.value == "-" || tok.value == "~") {
        consume();
        auto expr = parseUnary();
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::UnaryExpr;
        n->data = UnaryExpr{tok.value, expr};
        n->line = tok.line;
        return n;
    }
    if (tok.value == "await") {
        consume();
        auto expr = parseUnary();
        auto n = std::make_shared<Node>();
        n->kind = NodeKind::AwaitExpr;
        n->data = AwaitExpr{expr};
        n->line = tok.line;
        return n;
    }
    if (tok.value == "async") {
        // async (params) => body
        consume();
        if (cur().value == "(") {
            consume();
            auto params = parseParamList();
            expect(")");
            // skip type annotation on return type
            if (cur().value == ":") { consume(); while (cur().value != "=>" && cur().kind != Token::Eof) consume(); }
            expect("=>");
            return parseArrow(params, true);
        }
    }
    return parsePrimary();
}

NodePtr SymmParser::parsePostfix(NodePtr left) {
    while (true) {
        if (cur().value == ".") {
            consume();
            std::string prop = cur().value;
            consume();

            // Check for special GPU array methods: .map, .filter, .reduce
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
        } else if (cur().value == "(") {
            left = parseCall(left);
        } else {
            break;
        }
    }
    return left;
}

NodePtr SymmParser::parsePrimary() {
    auto tok = cur();

    if (tok.value == "new") {
        consume();
        std::string cls = cur().kind == Token::Ident ? consume().value : "";
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
    n->data = FuncDecl{params, body, isAsync};
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
        args.push_back(parseExpr());
        if (!match(",")) break;
    }
    return args;
}

std::vector<std::string> SymmParser::parseParamList() {
    std::vector<std::string> params;
    while (cur().value != ")" && cur().kind != Token::Eof) {
        if (cur().kind == Token::Ident) params.push_back(consume().value);
        // Skip type annotation
        if (cur().value == ":") { consume(); while (cur().value != "," && cur().value != ")" && cur().kind != Token::Eof) consume(); }
        if (!match(",")) break;
    }
    return params;
}

NodePtr SymmParser::parseArray() {
    expect("[");
    ArrayExpr arr;
    while (cur().value != "]" && cur().kind != Token::Eof) {
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
        std::string key;
        if (cur().kind == Token::String) key = consume().value;
        else if (cur().kind == Token::Ident || cur().kind == Token::Keyword) key = consume().value;
        expect(":");
        auto val = parseExpr();
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
    // Simple: treat the whole template as a string literal for now
    auto n = std::make_shared<Node>();
    n->kind = NodeKind::Literal;
    n->data = Literal{Literal::Str, tok.value};
    n->line = tok.line;
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
                        if (k == "engine" && v->kind == NodeKind::Literal)
                            mod.serveEngine = std::get<Literal>(v->data).raw;
                    }
                }
            }
        }
    }
}

} // namespace symm
