#pragma once
#include <string>
#include <vector>
#include <memory>
#include <variant>

namespace symm {

// ── AST Node types ────────────────────────────────────────────────────────────

enum class NodeKind {
    Module,
    ImportDecl,       // import { x, y } from 'mod'
    ConstDecl,        // const x = expr
    LetDecl,          // let x = expr
    FuncDecl,         // async (params) => body
    CallExpr,         // f(args...)
    MemberExpr,       // obj.prop
    AwaitExpr,        // await expr
    ReturnStmt,       // return expr
    Block,            // { stmts... }
    BinaryExpr,       // a op b
    UnaryExpr,        // !x, -x
    Literal,          // 42, "str", true, null
    Ident,            // identifier
    NewExpr,          // new Foo(args...)
    ArrayExpr,        // [a, b, c]
    ObjectExpr,       // { key: val }
    TemplateLit,      // `str ${expr}`
    IfStmt,           // if (cond) { }
    MapCall,          // data.map(fn) — special-cased for GPU codegen
    FilterCall,       // data.filter(fn)
    ReduceCall,       // data.reduce(fn, init)
    ServeCall,        // serve(app, opts) — entry point
    RouteReg,         // app.get/post/put/delete(path, handler)
    GpuCall,          // gpu.fingerprint / gpu.rayk / gpu.run
    ExprStmt,         // expression as statement
};

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct ImportDecl {
    std::vector<std::string> names;   // imported identifiers
    std::string              from;    // module specifier
};

struct ConstDecl {
    std::string name;
    NodePtr     init;
    bool        isMut;   // false = const, true = let
};

struct FuncDecl {
    std::vector<std::string> params;
    NodePtr                  body;
    bool                     isAsync;
};

struct CallExpr {
    NodePtr              callee;
    std::vector<NodePtr> args;
};

struct MemberExpr {
    NodePtr     object;
    std::string prop;
};

struct AwaitExpr { NodePtr expr; };
struct ReturnStmt { NodePtr expr; };
struct Block { std::vector<NodePtr> stmts; };

struct BinaryExpr {
    std::string op;
    NodePtr     left;
    NodePtr     right;
};

struct UnaryExpr {
    std::string op;
    NodePtr     expr;
};

struct Literal {
    enum Kind { Int, Float, Str, Bool, Null } kind;
    std::string raw;
};

struct Ident { std::string name; };

struct NewExpr {
    std::string          className;
    std::vector<NodePtr> args;
};

struct ArrayExpr  { std::vector<NodePtr> elements; };
struct ObjectExpr { std::vector<std::pair<std::string, NodePtr>> props; };
struct TemplateLit { std::vector<std::string> parts; std::vector<NodePtr> exprs; };
struct IfStmt     { NodePtr cond; NodePtr then; NodePtr else_; };

struct MapCall    { NodePtr array; NodePtr fn;  NodePtr init; };
struct FilterCall { NodePtr array; NodePtr fn; };
struct ReduceCall { NodePtr array; NodePtr fn; NodePtr init; };

struct RouteReg {
    std::string method;   // "GET", "POST", "PUT", "DELETE"
    std::string path;
    NodePtr     handler;  // FuncDecl
};

struct ServeCall {
    NodePtr     app;
    int         port;
    int         streams;
    std::string engine;
};

struct GpuCall {
    std::string              method;   // "fingerprint", "rayk", "run"
    std::vector<NodePtr>     args;
};

struct ExprStmt { NodePtr expr; };

using NodeData = std::variant<
    ImportDecl, ConstDecl, FuncDecl, CallExpr, MemberExpr,
    AwaitExpr, ReturnStmt, Block, BinaryExpr, UnaryExpr,
    Literal, Ident, NewExpr, ArrayExpr, ObjectExpr, TemplateLit,
    IfStmt, MapCall, FilterCall, ReduceCall, RouteReg, ServeCall,
    GpuCall, ExprStmt
>;

struct Node {
    NodeKind kind;
    NodeData data;
    int      line = 0;
};

struct Module {
    std::vector<NodePtr> imports;
    std::vector<NodePtr> stmts;
    std::vector<RouteReg> routes;   // extracted route registrations
    int                  servePort = 8080;
    std::string          serveEngine = "OMEGA";
};

// ── Parser ────────────────────────────────────────────────────────────────────

class SymmParser {
public:
    Module parse(const std::string& js, const std::string& filename = "<input>");

    bool hasErrors() const { return !_errors.empty(); }
    const std::vector<std::string>& errors() const { return _errors; }

private:
    // Tokenizer
    struct Token {
        enum Kind {
            Ident, Number, String, Template,
            Punct, Keyword, Eof
        } kind;
        std::string value;
        int line = 1;
    };

    std::string _src;
    std::string _file;
    size_t      _pos = 0;
    int         _line = 1;
    std::vector<Token>  _tokens;
    size_t              _tok = 0;
    std::vector<std::string> _errors;

    void tokenize();
    Token& cur();
    Token& peek(int offset = 1);
    Token  consume();
    bool   expect(const std::string& val);
    bool   match(const std::string& val);

    Module       parseModule();
    NodePtr      parseStmt();
    NodePtr      parseImport();
    NodePtr      parseConst(bool isMut);
    NodePtr      parseExpr(int prec = 0);
    NodePtr      parseUnary();
    NodePtr      parsePostfix(NodePtr left);
    NodePtr      parsePrimary();
    NodePtr      parseArrow(std::vector<std::string> params, bool isAsync);
    NodePtr      parseBlock();
    NodePtr      parseCall(NodePtr callee);
    NodePtr      parseObject();
    NodePtr      parseArray();
    NodePtr      parseTemplate();
    std::vector<NodePtr> parseArgList();
    std::vector<std::string> parseParamList();

    // Semantic extraction
    void extractRoutes(const NodePtr& node, Module& mod);
    void extractServe(const NodePtr& node, Module& mod);

    void error(const std::string& msg, int line = -1);
    static int precedence(const std::string& op);
};

} // namespace symm
