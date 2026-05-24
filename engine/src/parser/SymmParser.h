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
    FuncDecl,         // async (params) => body  /  function foo(params) { body }
    CallExpr,         // f(args...)
    MemberExpr,       // obj.prop
    OptChainExpr,     // obj?.prop
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
    TemplateLit,      // `str ${expr}`  — parts[] interleaved with exprs[]
    IfStmt,           // if (cond) { }
    ForStmt,          // for (init; cond; update) { }
    ForOfStmt,        // for (const x of iter) { }
    ForInStmt,        // for (const k in obj) { }
    WhileStmt,        // while (cond) { }
    DoWhileStmt,      // do { } while (cond)
    TryCatchStmt,     // try { } catch (e) { } finally { }
    ThrowStmt,        // throw expr
    SwitchStmt,       // switch (expr) { case: ... }
    BreakStmt,        // break
    ContinueStmt,     // continue
    AssignExpr,       // x = expr, x += expr, etc.
    ClassDecl,        // class Foo extends Bar { constructor; methods }
    MapCall,          // data.map(fn) — special-cased for GPU codegen
    FilterCall,       // data.filter(fn)
    ReduceCall,       // data.reduce(fn, init)
    ServeCall,        // serve(app, opts) — entry point
    RouteReg,         // app.get/post/put/delete(path, handler)
    GpuCall,          // gpu.fingerprint / gpu.run
    ExprStmt,         // expression as statement
    TypeofExpr,       // typeof expr
    VoidExpr,         // void 0
};

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct ImportDecl {
    std::vector<std::string> names;
    std::string              from;
};

struct ConstDecl {
    std::string name;
    NodePtr     init;
    bool        isMut;
};

struct FuncDecl {
    std::string              name;    // empty for arrow/anonymous
    std::vector<std::string> params;
    NodePtr                  body;
    bool                     isAsync;
    bool                     isGenerator;
};

struct CallExpr {
    NodePtr              callee;
    std::vector<NodePtr> args;
};

struct MemberExpr {
    NodePtr     object;
    std::string prop;
    bool        computed = false;  // obj[expr] vs obj.prop
};

struct OptChainExpr {
    NodePtr     object;
    std::string prop;
};

struct AwaitExpr   { NodePtr expr; };
struct ReturnStmt  { NodePtr expr; };
struct Block       { std::vector<NodePtr> stmts; bool flat = false; };

struct BinaryExpr {
    std::string op;
    NodePtr     left;
    NodePtr     right;
};

struct UnaryExpr {
    std::string op;
    NodePtr     expr;
    bool        postfix = false;  // x++ vs ++x
};

struct TypeofExpr  { NodePtr expr; };
struct VoidExpr    { NodePtr expr; };

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

// Template literal: parts[i] interleaved with exprs[i].
//   `hello ${name}, you have ${count} msgs`
//   parts  = ["hello ", ", you have ", " msgs"]
//   exprs  = [name_node, count_node]
//   Invariant: parts.size() == exprs.size() + 1
struct TemplateLit {
    std::vector<std::string> parts;
    std::vector<NodePtr>     exprs;
};

struct IfStmt { NodePtr cond; NodePtr then; NodePtr else_; };

struct ForStmt {
    NodePtr init;    // const/let decl or expr stmt (may be null)
    NodePtr cond;    // condition (may be null)
    NodePtr update;  // update expression (may be null)
    NodePtr body;
};

struct ForOfStmt {
    std::string varName;
    bool        isConst;
    bool        isAwait = false;  // true for `for await (const x of asyncIter)`
    NodePtr     iter;
    NodePtr     body;
};

struct ForInStmt {
    std::string varName;
    bool        isConst;
    NodePtr     obj;
    NodePtr     body;
};

struct WhileStmt   { NodePtr cond; NodePtr body; };
struct DoWhileStmt { NodePtr body; NodePtr cond; };

struct TryCatchStmt {
    NodePtr     tryBody;
    std::string catchParam;   // variable name in catch(e)
    NodePtr     catchBody;    // null if no catch
    NodePtr     finallyBody;  // null if no finally
};

struct ThrowStmt   { NodePtr expr; };

struct SwitchCase {
    NodePtr              test;   // null = default
    std::vector<NodePtr> body;
};
struct SwitchStmt {
    NodePtr                 test;
    std::vector<SwitchCase> cases;
};

struct BreakStmt    {};
struct ContinueStmt {};

struct AssignExpr {
    std::string op;    // "=", "+=", "-=", "*=", "/=", "%=", "??=", "&&=", "||="
    NodePtr     left;
    NodePtr     right;
};

// ── Class declaration ─────────────────────────────────────────────────────────

struct ClassField {
    std::string name;
    NodePtr     init;       // default value (may be null)
    bool        isPrivate;
    bool        isStatic;
};

struct ClassMethod {
    std::string              name;
    std::vector<std::string> params;
    NodePtr                  body;
    bool                     isStatic;
    bool                     isConstructor;
    bool                     isGetter;
    bool                     isSetter;
    bool                     isAsync;
};

struct ClassDecl {
    std::string              name;
    std::string              superName;       // "" if no extends
    std::vector<std::string> typeParams;      // generic params ("T", "K", "V")
    std::vector<ClassField>  fields;
    std::vector<ClassMethod> methods;
};

struct MapCall    { NodePtr array; NodePtr fn; NodePtr init; };
struct FilterCall { NodePtr array; NodePtr fn; };
struct ReduceCall { NodePtr array; NodePtr fn; NodePtr init; };

struct RouteReg {
    std::string method;
    std::string path;
    NodePtr     handler;
};

struct ServeCall {
    NodePtr app;
    int     port;
    int     streams;
};

struct GpuCall {
    std::string          method;
    std::vector<NodePtr> args;
};

struct ExprStmt { NodePtr expr; };

using NodeData = std::variant<
    ImportDecl, ConstDecl, FuncDecl, CallExpr, MemberExpr,
    OptChainExpr, AwaitExpr, ReturnStmt, Block, BinaryExpr, UnaryExpr,
    TypeofExpr, VoidExpr,
    Literal, Ident, NewExpr, ArrayExpr, ObjectExpr, TemplateLit,
    IfStmt, ForStmt, ForOfStmt, ForInStmt, WhileStmt, DoWhileStmt,
    TryCatchStmt, ThrowStmt, SwitchStmt, BreakStmt, ContinueStmt,
    AssignExpr, ClassDecl,
    MapCall, FilterCall, ReduceCall, RouteReg, ServeCall,
    GpuCall, ExprStmt
>;

struct Node {
    NodeKind kind;
    NodeData data;
    int      line = 0;
};

struct Module {
    std::vector<NodePtr>  imports;
    std::vector<NodePtr>  stmts;
    std::vector<RouteReg> routes;
    int servePort = 8080;
};

// ── Parser ────────────────────────────────────────────────────────────────────

class SymmParser {
public:
    Module parse(const std::string& js, const std::string& filename = "<input>");

    bool hasErrors() const { return !_errors.empty(); }
    const std::vector<std::string>& errors() const { return _errors; }

private:
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
    std::vector<Token>       _tokens;
    size_t                   _tok = 0;
    std::vector<std::string> _errors;
    int _tmpCnt = 0;

    void tokenize();
    Token& cur();
    Token& peek(int offset = 1);
    Token  consume();
    bool   expect(const std::string& val);
    bool   match(const std::string& val);

    Module  parseModule();
    NodePtr parseStmt();
    NodePtr parseImport();
    NodePtr parseConst(bool isMut);
    NodePtr parseFunctionDecl(bool isAsync = false);
    NodePtr parseClass();
    NodePtr parseFor();
    NodePtr parseWhile();
    NodePtr parseDoWhile();
    NodePtr parseTryCatch();
    NodePtr parseSwitch();
    NodePtr parseExpr(int prec = 0);
    NodePtr parseUnary();
    NodePtr parsePostfix(NodePtr left);
    NodePtr parsePrimary();
    NodePtr parseArrow(std::vector<std::string> params, bool isAsync);
    NodePtr parseBlock();
    NodePtr parseCall(NodePtr callee);
    NodePtr parseObject();
    NodePtr parseArray();
    NodePtr parseTemplate();
    std::vector<NodePtr>      parseArgList();
    std::vector<std::string>  parseParamList();
    std::string               parseTypeAnnotation();  // skip TS types

    void extractRoutes(const NodePtr& node, Module& mod);
    void extractServe(const NodePtr& node, Module& mod);

    void error(const std::string& msg, int line = -1);
    static int precedence(const std::string& op);
    static bool isAssignOp(const std::string& op);
};

} // namespace symm
