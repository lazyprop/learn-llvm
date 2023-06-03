#include <utility>
#include <string>
#include <vector>
#include <memory>
#include <iostream>


///////////
/* LEXER */
///////////

enum Token {
    tok_eof = -1,

    tok_def = -2,
    tok_extern = -3,

    tok_ident = -4,
    tok_num = -5,
};

static void print_curtok();

static std::string IdentStr;
static double NumVal;

static int gettok() {
    static char last_char = ' ';

    while (isspace(last_char)) {
        last_char = getchar();
    }

    if (isalpha(last_char)) {
        IdentStr = last_char;
        while (isalnum(last_char = getchar())) {
            IdentStr += last_char;
        }

        if (IdentStr == "def") {
            return tok_def;
        }
        if (IdentStr == "extern") {
            return tok_extern;
        }
        return tok_ident;
    }

    if (isdigit(last_char) || last_char == '.') {
        std::string numstr;
        numstr += last_char;

        last_char = getchar();
        while (isdigit(last_char) || last_char == '.') {
            numstr += last_char;
            last_char = getchar();
        }
        NumVal = strtod(numstr.c_str(), nullptr);
        return tok_num;
    }

    // TODO: handle comments

    if (last_char == EOF) {
        return tok_eof;
    }

    char this_char = last_char;
    last_char = getchar();
    return this_char;
}

bool is_op(char op) {
    return (op == '+') || (op == '-') || (op == '*') ? true : false;
}


/////////
/* AST */
/////////

class Expr {
public:
    virtual ~Expr() = default;
};

class NumExpr : public Expr {
    double val;

public:
    NumExpr(double val) : val(val) {}
};

class VarExpr : public Expr {
    std::string name;

public:
    VarExpr(std::string &name) : name(name) {}
};

class BinaryExpr : public Expr {
    char op;
    std::unique_ptr<Expr> lhs, rhs;

public:
    BinaryExpr(char op, std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs)
        : op(op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
};

class CallExpr : public Expr {
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;

public:
    CallExpr(std::string callee, std::vector<std::unique_ptr<Expr>> args)
        : callee(callee), args(std::move(args)) {}
};


class FuncPrototype {
    std::string name;
    std::vector<std::unique_ptr<Expr>> args;

public:
    FuncPrototype(std::string name, std::vector<std::unique_ptr<Expr>> args)
        : name(name), args(std::move(args)) {}
};

class FunctionExpr : public Expr {
    std::unique_ptr<FuncPrototype> proto;
    std::unique_ptr<Expr> body;

public:
    FunctionExpr(std::unique_ptr<FuncPrototype> proto,
                 std::unique_ptr<Expr> body)
        : proto(std::move(proto)), body(std::move(body)) {}
};

static int CurTok;
static int get_next_token() {
    return CurTok = gettok();
}

std::unique_ptr<Expr> log_error(const char* str) {
    fprintf(stderr, "Error: %s\n", str);
    print_curtok();
    return nullptr;
}

std::unique_ptr<FuncPrototype> log_error_p(const char* str) {
    log_error(str);
    return nullptr;
}


static std::unique_ptr<Expr> parse_expr();

static std::unique_ptr<Expr> parse_ident() {
    std::string name = IdentStr;
    get_next_token();

    // if the name is followed by parentheses then it is a function call
    if (CurTok != '(') {
        return std::make_unique<VarExpr>(name);
    }

    std::vector<std::unique_ptr<Expr>> args;
    get_next_token(); // eat '('

    if (CurTok != ')') {
        while (true) {
            if (auto arg = parse_expr()) {
                args.push_back(std::move(arg));
            } else {
                return log_error("failed to parse argument");
            }

            if (CurTok == ')') {
                break;
            }
            if (CurTok != ',') {
                return log_error("expected ',' or ')' in argument list");
            }

            get_next_token();
        }
        get_next_token(); // eat the ')'
    }

    return std::make_unique<CallExpr>(name, std::move(args));
}

static std::unique_ptr<NumExpr> parse_number() {
    auto result = std::make_unique<NumExpr>(NumVal);
    get_next_token(); // eat the number
    return result;
}

static std::unique_ptr<Expr> parse_primary() {
    switch (CurTok) {
        case tok_ident:
            return parse_ident();
        case tok_num:
            return parse_number();
        default:
            return log_error("unknown token type");
    }
}

// of the form
//      '+' primary
static std::unique_ptr<Expr> parse_binop_rhs(std::unique_ptr<Expr> lhs) {
    std::unique_ptr<Expr> cur = std::move(lhs);
    while (true) {
        int op = CurTok;
        if (!is_op(op)) {
            return cur;
        }

        get_next_token(); // eat the op (only if it is a valid op)

        auto rhs = parse_primary();
        if (!rhs) {
            return log_error(
                    "could not parse right hand side of binary expression");
        }

        // assuming all ops be left associative
        cur = std::make_unique<BinaryExpr>(op, std::move(cur), std::move(rhs));
    }
}

static std::unique_ptr<Expr> parse_expr() {
    auto lhs = parse_primary();
    if (!lhs) {
        return nullptr;
    }

    return parse_binop_rhs(std::move(lhs));
}

static std::unique_ptr<FuncPrototype> parse_prototype() {
    if (CurTok != tok_ident) {
        return log_error_p("expected function name");
    }

    std::string name = IdentStr;
    get_next_token(); // eat name

    if (CurTok != '(') {
        return log_error_p("expected (");
    }

    // TODO: check whether all these args are idents
    std::vector<std::unique_ptr<Expr>> argnames;
    get_next_token(); // eat '('

    if (CurTok != ')') {
        while (true) {
            if (auto aname = parse_ident()) {
                argnames.push_back(std::move(aname));
            } else {
                char buffer[100];
                std::sprintf(buffer,
                        "in argument list in function prototype\n"
                        "expected identifier. found %c",
                        CurTok);
                return log_error_p(buffer);
            }

            if (CurTok == ')') {
                break;
            }
            if (CurTok != ',') {
                return log_error_p("expected ',' or ')' in argument list");
            }

            get_next_token();
        }
        get_next_token(); // eat the ')'
    }

    return std::make_unique<FuncPrototype>(name, std::move(argnames));
}

// definition ::=
//      'def' proto expr
static std::unique_ptr<Expr> parse_definition() {
    get_next_token(); // eat the 'def'
    auto proto = parse_prototype();
    if (!proto) {
        return nullptr;
    }

    auto body = parse_expr();
    if (!body) {
        return log_error("expected function body");
    }
    return std::make_unique<FunctionExpr>(std::move(proto), std::move(body));
}

static std::unique_ptr<Expr> parse_toplevel_expr() {
    if (auto e = parse_expr()) {
        auto proto = std::make_unique<FuncPrototype>(
                std::string("__anon_expr"),
                std::vector<std::unique_ptr<Expr>>()
        );
        return std::make_unique<FunctionExpr>(std::move(proto), std::move(e));
    }
    return nullptr;
}

static std::unique_ptr<FuncPrototype> parse_extern() {
    get_next_token(); // eat the 'extern'
    return parse_prototype();
}


////////////////////
// top level parsing
////////////////////

static void handle_definition() {
    if (parse_definition()) {
    } else {
        get_next_token();
    }
}

static void handle_toplevel_expr() {
    if (parse_toplevel_expr()) {
        fprintf(stderr, "parsed top level expression\n");
    } else {
        get_next_token();
    }
}

static void handle_extern() {
    if (parse_extern()) {
        fprintf(stderr, "parsed extern\n");
    } else {
        get_next_token();
    }
}


static void main_loop() {
    while (true) {
        printf("ready> ");
        get_next_token();
        switch (CurTok) {
            case tok_eof:
                return;

            case ';':
                //get_next_token(); // eat the ';'
                break;

            case tok_def:
                handle_definition();
                break;

            case tok_extern:
                handle_extern();
                break;

            default:
                handle_toplevel_expr();
                break;
        }
    }
}

static void print_curtok() {
    switch (CurTok) {
        case tok_def:
            printf("token type: def\n");
            return;
        case tok_extern:
            printf("token type: extern\n");
            return;
        case tok_ident:
            printf("token type: ident. %s\n", IdentStr.c_str());
            return;
        case tok_num:
            printf("token type: number. %d\n", NumVal);
            return;
        case tok_eof:
            printf("token type: eof\n");
            return;
        default:
            printf("unknown token type: %c\n", CurTok);
            return;
    }
}

int main() {

    /*
    while (CurTok != tok_eof) {
        print_curtok();
        get_next_token();
    }
    */

    main_loop();

    return 0;
}
