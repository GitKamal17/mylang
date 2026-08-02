#ifndef MYLANG_AST_H
#define MYLANG_AST_H

#include <stddef.h> /* size_t */

/* ------------------------------------------------------------------ *
 * Forward declarations                                               *
 * ------------------------------------------------------------------ */
typedef struct expr  expr_t;
typedef struct stmt  stmt_t;
typedef struct program program_t;

/* ------------------------------------------------------------------ *
 * Expression types                                                   *
 * ------------------------------------------------------------------ */
typedef enum {
    EXPR_NUMBER,
    EXPR_STRING,
    EXPR_IDENTIFIER,
    EXPR_BINARY,       /* e.g. x + 5, "a" + b                         */
    EXPR_UNARY,        /* e.g. !flag                                   */
    EXPR_CALL,         /* function call: name(args)                    */
    EXPR_COMPARISON,   /* e.g. a > b, x == 5                          */
    EXPR_ARRAY,        /* array literal: [1, 2, 3]                     */
    EXPR_INDEX,        /* index access: arr[i]                         */
    EXPR_DICT,         /* dict literal: {"key": value, ...}              */
    EXPR_METHOD,       /* method call: expr.method(args)                 */
    EXPR_FIELD,        /* field access: expr.field                        */
} expr_type_t;

/* Binary operator kinds. */
typedef enum {
    OP_PLUS,
    OP_MINUS,
    OP_STAR,
    OP_SLASH,
} binary_op_t;

/* Unary operator kinds. */
typedef enum {
    UNOP_BANG,
    UNOP_MINUS,
} unary_op_t;

/* Comparison operator kinds. */
typedef enum {
    CMP_GT,
    CMP_LT,
    CMP_GE,
    CMP_LE,
    CMP_EQ,
    CMP_NE,
} cmp_op_t;

struct expr {
    expr_type_t type;
    int         line;
    int         column;
    union {
        double number;                       /* EXPR_NUMBER            */
        struct {                             /* EXPR_STRING            */
            const char *start;
            size_t      length;
        } string;
        struct {                             /* EXPR_IDENTIFIER        */
            const char *start;
            size_t      length;
        } identifier;
        struct {                             /* EXPR_BINARY            */
            binary_op_t op;
            expr_t     *left;
            expr_t     *right;
        } binary;
        struct {                             /* EXPR_UNARY             */
            unary_op_t op;
            expr_t    *operand;
        } unary;
        struct {                             /* EXPR_CALL              */
            const char *name_start;
            size_t      name_length;
            expr_t    **args;
            size_t      arg_count;
        } call;
        struct {                             /* EXPR_COMPARISON        */
            cmp_op_t  op;
            expr_t   *left;
            expr_t   *right;
        } comparison;
        struct {                             /* EXPR_ARRAY             */
            expr_t  **elements;
            size_t    count;
        } array;
        struct {                             /* EXPR_INDEX             */
            expr_t    *object;
            expr_t    *index;
        } index;
        struct {                             /* EXPR_DICT              */
            expr_t  **keys;
            expr_t  **values;
            size_t    count;
        } dict;
        struct {                             /* EXPR_METHOD            */
            expr_t     *object;
            const char *method_name_start;
            size_t      method_name_length;
            expr_t    **args;
            size_t      arg_count;
        } method;
        struct {                             /* EXPR_FIELD             */
            expr_t     *object;
            const char *field_name_start;
            size_t      field_name_length;
        } field;
    } as;
};

/* ------------------------------------------------------------------ *
 * Statement types                                                    *
 * ------------------------------------------------------------------ */
typedef enum {
    STMT_WINDOW,   /* window <title>, <width>, <height>               */
    STMT_EVENT,    /* on <name> { <body> }                            */
    STMT_CALL,     /* legacy: <keyword|identifier> <args...>           */
    STMT_ASSIGN,   /* <identifier> = <expr>                           */
    STMT_IF,       /* if <expr> { <body> } [else { <body> }]          */
    STMT_WHILE,    /* while <expr> { <body> }                         */
    STMT_FOR,      /* for <init>; <cond>; <inc> { <body> }           */
    STMT_PRINT,    /* print <expr>                                    */
    STMT_RETURN,   /* return [<expr>]                                 */
    STMT_FN,       /* fn name(params) { <body> }                      */
    STMT_EXPR,     /* expression used as statement (call, etc.)        */
    STMT_BREAK,    /* break                                           */
    STMT_CONTINUE, /* continue                                        */
    STMT_STRUCT,   /* struct definition: struct Name { fields }        */
    STMT_SPRITE,   /* sprite <name> = "path" (game mode)                */
} stmt_type_t;

/* Function parameter. */
typedef struct {
    const char *name_start;
    size_t      name_length;
} param_t;

struct stmt {
    stmt_type_t type;
    int         line;
    int         column;
    union {
        struct {                             /* STMT_WINDOW            */
            expr_t *title;
            expr_t *width;
            expr_t *height;
        } window;
        struct {                             /* STMT_EVENT             */
            expr_t  *name;
            stmt_t **body;
            size_t   body_count;
        } event;
        struct {                             /* STMT_CALL (legacy)     */
            const char *name_start;
            size_t      name_length;
            expr_t    **args;
            size_t      arg_count;
        } call;
        struct {                             /* STMT_ASSIGN            */
            const char *name_start;
            size_t      name_length;
            expr_t     *value;
        } assign;
        struct {                             /* STMT_IF                */
            expr_t    *condition;
            stmt_t   **then_body;
            size_t     then_count;
            stmt_t    *else_branch;          /* STMT_IF or NULL        */
        } if_stmt;
        struct {                             /* STMT_WHILE             */
            expr_t    *condition;
            stmt_t   **body;
            size_t     body_count;
        } while_stmt;
        struct {                             /* STMT_FOR               */
            stmt_t    *init;                 /* STMT_ASSIGN or NULL    */
            expr_t    *condition;
            stmt_t    *inc;                  /* STMT_ASSIGN or NULL    */
            stmt_t   **body;
            size_t     body_count;
        } for_stmt;
        struct {                             /* STMT_PRINT             */
            expr_t   **values;
            size_t     value_count;
        } print;
        struct {                             /* STMT_RETURN            */
            expr_t    *value;                /* NULL means void return */
        } ret;
        struct {                             /* STMT_FN                */
            const char *name_start;
            size_t      name_length;
            param_t    *params;              /* array of parameters    */
            size_t      param_count;
            stmt_t    **body;
            size_t      body_count;
        } fn;
        struct {                             /* STMT_EXPR              */
            expr_t    *expr;
        } expr_stmt;
        struct {                             /* STMT_STRUCT            */
            const char *name_start;
            size_t      name_length;
            param_t    *fields;              /* array of field names    */
            size_t      field_count;
        } struct_def;
        struct {                             /* STMT_SPRITE            */
            const char *name_start;          /* sprite variable name    */
            size_t      name_length;
            expr_t     *path;                /* image file path string  */
        } sprite;
    } as;
};

/* ------------------------------------------------------------------ *
 * Program: a sequence of top-level statements.                        *
 * ------------------------------------------------------------------ */
struct program {
    stmt_t **stmts;
    size_t   count;
    size_t   capacity;
    char   **sources;       /* owned heap-allocated source buffers       */
    size_t   source_count;
    size_t   source_cap;
};

/* ------------------------------------------------------------------ *
 * AST memory management                                              *
 * ------------------------------------------------------------------ */

void ast_destroy_program(program_t *prog);

#endif /* MYLANG_AST_H */
