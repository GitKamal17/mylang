#include "codegen.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * Dynamic string builder                                             *
 * ------------------------------------------------------------------ */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} sb_t;

/* Forward declaration */
static void oom_func(void);

static bool sb_init(sb_t *b)
{
    b->cap = 512;
    b->len = 0;
    b->data = (char *)malloc(b->cap);
    if (!b->data) return false;
    b->data[0] = '\0';
    return true;
}

static bool sb_grow(sb_t *b, size_t needed)
{
    while (b->cap < needed) {
        size_t new_cap = b->cap ? b->cap * 2 : 512;
        char  *tmp     = (char *)realloc(b->data, new_cap);
        if (!tmp) return false;
        b->data = tmp;
        b->cap  = new_cap;
    }
    return true;
}

static void sb_append(sb_t *b, const char *s)
{
    size_t slen = strlen(s);
    if (!sb_grow(b, b->len + slen + 1)) { oom_func(); }
    memcpy(b->data + b->len, s, slen + 1);
    b->len += slen;
}

static void sb_append_n(sb_t *b, const char *s, size_t n)
{
    if (!sb_grow(b, b->len + n + 1)) { oom_func(); }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void sb_appendf(sb_t *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) { fprintf(stderr, "fmt error\n"); exit(EXIT_FAILURE); }

    if (!sb_grow(b, b->len + (size_t)needed + 1)) { oom_func(); }

    va_start(ap, fmt);
    vsnprintf(b->data + b->len, (size_t)needed + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)needed;
}

/* Explain mode: when enabled, emit_stmt adds // [filename:line] comments */
static int          g_explain_mode    = 0;
static const char  *g_source_filename = NULL;

static void oom_func(void)
{
    fprintf(stderr, "out of memory\n");
    exit(EXIT_FAILURE);
}

/* Forward declarations for built-in helper functions */
static int handle_builtin_call(sb_t *b, const expr_t *e);
static int expr_has_call(const expr_t *e, const char *name, size_t namelen);
static int stmt_has_call(const stmt_t *s, const char *name, size_t namelen);

/* Struct type table: tracks struct definitions so codegen can emit
   compound literal initialization and field access. */
typedef struct {
    const char *name_start;
    size_t      name_length;
    size_t      field_count;
} struct_type_entry_t;

static struct_type_entry_t *struct_types   = NULL;
static size_t              struct_types_cnt = 0;
static size_t              struct_types_cap = 0;

/* ------------------------------------------------------------------ *
 * Helpers                                                            *
 * ------------------------------------------------------------------ */

typedef enum { TYPE_INT, TYPE_DOUBLE, TYPE_STRING, TYPE_DICT, TYPE_UNKNOWN } type_kind_t;

/* Variable type table: tracks type of each variable from its first
   top-level assignment so infer_type can return the correct type
   for identifiers (e.g. string variables get %%s in printf). */
typedef struct {
    const char *start;
    size_t      length;
    type_kind_t type;
} var_type_entry_t;

static var_type_entry_t *var_types      = NULL;
static size_t           var_types_count = 0;
static size_t           var_types_cap   = 0;

/* Forward declarations needed by build_var_types */
static void emit_expr(sb_t *b, const expr_t *e);
static type_kind_t infer_type(const expr_t *e);

static void clear_var_types(void)
{
    free(var_types);
    var_types       = NULL;
    var_types_count = 0;
    var_types_cap   = 0;
}

/* ------------------------------------------------------------------ *
 * Struct type table                                                  *
 * ------------------------------------------------------------------ */

static void build_struct_types(const program_t *prog)
{
    free(struct_types);
    struct_types = NULL; struct_types_cnt = 0; struct_types_cap = 0;

    for (size_t i = 0; i < prog->count; ++i) {
        const stmt_t *s = prog->stmts[i];
        if (s->type != STMT_STRUCT) continue;

        if (struct_types_cnt >= struct_types_cap) {
            size_t nc = struct_types_cap ? struct_types_cap * 2 : 8;
            struct_type_entry_t *t = (struct_type_entry_t *)realloc(
                struct_types, nc * sizeof(struct_type_entry_t));
            if (!t) { fprintf(stderr, "OOM\n"); exit(EXIT_FAILURE); }
            struct_types = t; struct_types_cap = nc;
        }
        struct_types[struct_types_cnt].name_start  = s->as.struct_def.name_start;
        struct_types[struct_types_cnt].name_length = s->as.struct_def.name_length;
        struct_types[struct_types_cnt].field_count = s->as.struct_def.field_count;
        struct_types_cnt++;
    }
}

static int is_struct_type(const char *start, size_t length)
{
    for (size_t i = 0; i < struct_types_cnt; ++i) {
        if (struct_types[i].name_length == length &&
            strncmp(struct_types[i].name_start, start, length) == 0)
            return 1;
    }
    return 0;
}

/* Helper: add a single assignment to the var type table */
static void record_var_type(const stmt_t *s)
{
    if (s->type != STMT_ASSIGN || !s->as.assign.value) return;

    /* Skip duplicates */
    for (size_t j = 0; j < var_types_count; ++j) {
        if (var_types[j].length == s->as.assign.name_length &&
            strncmp(var_types[j].start, s->as.assign.name_start,
                    s->as.assign.name_length) == 0)
            return;
    }

    if (var_types_count >= var_types_cap) {
        size_t nc = var_types_cap ? var_types_cap * 2 : 16;
        var_type_entry_t *t = (var_type_entry_t *)realloc(
            var_types, nc * sizeof(var_type_entry_t));
        if (!t) { fprintf(stderr, "OOM\n"); exit(EXIT_FAILURE); }
        var_types = t; var_types_cap = nc;
    }
    var_types[var_types_count].start  = s->as.assign.name_start;
    var_types[var_types_count].length = s->as.assign.name_length;
    var_types[var_types_count].type   = infer_type(s->as.assign.value);
    var_types_count++;
}

/* Recursively scan a block of statements for variable assignments */
static void scan_block_var_types(const stmt_t *const *stmts, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const stmt_t *s = stmts[i];
        if (s->type == STMT_ASSIGN)
            record_var_type(s);
        else if (s->type == STMT_IF) {
            scan_block_var_types(
                (const stmt_t *const *)s->as.if_stmt.then_body,
                s->as.if_stmt.then_count);
            /* Recurse into else chains */
            const stmt_t *ec = s->as.if_stmt.else_branch;
            while (ec) {
                scan_block_var_types(
                    (const stmt_t *const *)ec->as.if_stmt.then_body,
                    ec->as.if_stmt.then_count);
                if (ec->type == STMT_IF && ec->as.if_stmt.condition)
                    ec = ec->as.if_stmt.else_branch;
                else
                    ec = NULL;
            }
        } else if (s->type == STMT_WHILE) {
            scan_block_var_types(
                (const stmt_t *const *)s->as.while_stmt.body,
                s->as.while_stmt.body_count);
        } else if (s->type == STMT_FOR) {
            scan_block_var_types(
                (const stmt_t *const *)s->as.for_stmt.body,
                s->as.for_stmt.body_count);
        } else if (s->type == STMT_FN) {
            scan_block_var_types(
                (const stmt_t *const *)s->as.fn.body,
                s->as.fn.body_count);
        }
    }
}

static void build_var_types(const program_t *prog)
{
    clear_var_types();
    /* Scan top-level assignments */
    scan_block_var_types((const stmt_t *const *)prog->stmts, prog->count);
}

/* Recursively check if an expression subtree contains EXPR_DICT */
static int expr_has_dict(const expr_t *e)
{
    if (!e) return 0;
    if (e->type == EXPR_DICT) return 1;
    if (e->type == EXPR_BINARY)
        return expr_has_dict(e->as.binary.left) || expr_has_dict(e->as.binary.right);
    if (e->type == EXPR_UNARY)
        return expr_has_dict(e->as.unary.operand);
    if (e->type == EXPR_CALL) {
        for (size_t i = 0; i < e->as.call.arg_count; ++i)
            if (expr_has_dict(e->as.call.args[i])) return 1;
        return 0;
    }
    if (e->type == EXPR_COMPARISON)
        return expr_has_dict(e->as.comparison.left) || expr_has_dict(e->as.comparison.right);
    if (e->type == EXPR_INDEX)
        return expr_has_dict(e->as.index.object) || expr_has_dict(e->as.index.index);
    if (e->type == EXPR_ARRAY) {
        for (size_t i = 0; i < e->as.array.count; ++i)
            if (expr_has_dict(e->as.array.elements[i])) return 1;
        return 0;
    }
    if (e->type == EXPR_METHOD) {
        for (size_t i = 0; i < e->as.method.arg_count; ++i)
            if (expr_has_dict(e->as.method.args[i])) return 1;
        return expr_has_dict(e->as.method.object);
    }
    return 0;
}

/* Recursively check if an expression subtree contains EXPR_METHOD or
   EXPR_FIELD (field access like .len which needs string helpers) */
static int expr_has_method(const expr_t *e)
{
    if (!e) return 0;
    if (e->type == EXPR_METHOD) return 1;
    if (e->type == EXPR_FIELD && e->as.field.field_name_length == 3 &&
        strncmp(e->as.field.field_name_start, "len", 3) == 0) return 1;
    if (e->type == EXPR_BINARY)
        return expr_has_method(e->as.binary.left) || expr_has_method(e->as.binary.right);
    if (e->type == EXPR_UNARY)
        return expr_has_method(e->as.unary.operand);
    if (e->type == EXPR_CALL) {
        for (size_t i = 0; i < e->as.call.arg_count; ++i)
            if (expr_has_method(e->as.call.args[i])) return 1;
        return 0;
    }
    if (e->type == EXPR_COMPARISON)
        return expr_has_method(e->as.comparison.left) || expr_has_method(e->as.comparison.right);
    if (e->type == EXPR_INDEX)
        return expr_has_method(e->as.index.object) || expr_has_method(e->as.index.index);
    if (e->type == EXPR_ARRAY) {
        for (size_t i = 0; i < e->as.array.count; ++i)
            if (expr_has_method(e->as.array.elements[i])) return 1;
        return 0;
    }
    if (e->type == EXPR_DICT) {
        for (size_t i = 0; i < e->as.dict.count; ++i)
            if (expr_has_method(e->as.dict.keys[i]) || expr_has_method(e->as.dict.values[i]))
                return 1;
        return 0;
    }
    return 0;
}

/* Combined check: does this expression need string helpers?
   (methods OR string concatenation) */
static int expr_needs_helpers(const expr_t *e)
{
    if (expr_has_method(e)) return 1;
    if (e && e->type == EXPR_BINARY && e->as.binary.op == OP_PLUS &&
        (infer_type(e->as.binary.left) == TYPE_STRING ||
         infer_type(e->as.binary.right) == TYPE_STRING))
        return 1;
    return 0;
}

/* Recursively check if expression tree contains a call to a named function */
/* Check if a statement contains a call to a named function */
static int stmt_has_call(const stmt_t *s, const char *name, size_t namelen)
{
    if (!s) return 0;
    switch (s->type) {
    case STMT_ASSIGN:
        return s->as.assign.value && expr_has_call(s->as.assign.value, name, namelen);
    case STMT_PRINT:
        for (size_t i = 0; i < s->as.print.value_count; ++i)
            if (expr_has_call(s->as.print.values[i], name, namelen)) return 1;
        return 0;
    case STMT_RETURN:
        return s->as.ret.value && expr_has_call(s->as.ret.value, name, namelen);
    case STMT_IF:
        return (s->as.if_stmt.condition && expr_has_call(s->as.if_stmt.condition, name, namelen));
    case STMT_WHILE:
        return s->as.while_stmt.condition && expr_has_call(s->as.while_stmt.condition, name, namelen);
    case STMT_FOR:
        return (s->as.for_stmt.condition && expr_has_call(s->as.for_stmt.condition, name, namelen));
    case STMT_EXPR:
        return s->as.expr_stmt.expr && expr_has_call(s->as.expr_stmt.expr, name, namelen);
    case STMT_CALL:
        /* Legacy space-separated calls carry the name directly */
        return s->as.call.name_length == namelen &&
               strncmp(s->as.call.name_start, name, namelen) == 0;
    case STMT_FN:
        for (size_t i = 0; i < s->as.fn.body_count; ++i)
            if (stmt_has_call(s->as.fn.body[i], name, namelen)) return 1;
        return 0;
    default:
        return 0;
    }
}

static int expr_has_call(const expr_t *e, const char *name, size_t namelen)
{
    if (!e) return 0;
    if (e->type == EXPR_CALL && e->as.call.name_length == namelen &&
        strncmp(e->as.call.name_start, name, namelen) == 0)
        return 1;
    if (e->type == EXPR_BINARY)
        return expr_has_call(e->as.binary.left, name, namelen) ||
               expr_has_call(e->as.binary.right, name, namelen);
    if (e->type == EXPR_UNARY)
        return expr_has_call(e->as.unary.operand, name, namelen);
    if (e->type == EXPR_CALL) {
        for (size_t i = 0; i < e->as.call.arg_count; ++i)
            if (expr_has_call(e->as.call.args[i], name, namelen)) return 1;
        return 0;
    }
    if (e->type == EXPR_COMPARISON)
        return expr_has_call(e->as.comparison.left, name, namelen) ||
               expr_has_call(e->as.comparison.right, name, namelen);
    if (e->type == EXPR_INDEX)
        return expr_has_call(e->as.index.object, name, namelen) ||
               expr_has_call(e->as.index.index, name, namelen);
    if (e->type == EXPR_METHOD)
        return expr_has_call(e->as.method.object, name, namelen);
    return 0;
}

/* Recursively check if an expression tree contains a named identifier */
static int expr_has_ident(const expr_t *e, const char *name, size_t namelen)
{
    if (!e) return 0;
    if (e->type == EXPR_IDENTIFIER && e->as.identifier.length == namelen &&
        strncmp(e->as.identifier.start, name, namelen) == 0)
        return 1;
    if (e->type == EXPR_BINARY)
        return expr_has_ident(e->as.binary.left, name, namelen) ||
               expr_has_ident(e->as.binary.right, name, namelen);
    if (e->type == EXPR_UNARY)
        return expr_has_ident(e->as.unary.operand, name, namelen);
    if (e->type == EXPR_CALL) {
        for (size_t i = 0; i < e->as.call.arg_count; ++i)
            if (expr_has_ident(e->as.call.args[i], name, namelen)) return 1;
        return 0;
    }
    if (e->type == EXPR_COMPARISON)
        return expr_has_ident(e->as.comparison.left, name, namelen) ||
               expr_has_ident(e->as.comparison.right, name, namelen);
    if (e->type == EXPR_INDEX)
        return expr_has_ident(e->as.index.object, name, namelen) ||
               expr_has_ident(e->as.index.index, name, namelen);
    if (e->type == EXPR_METHOD)
        return expr_has_ident(e->as.method.object, name, namelen);
    return 0;
}

/* Check if a statement references a named identifier (anywhere in it) */
static int stmt_has_ident(const stmt_t *s, const char *name, size_t namelen)
{
    if (!s) return 0;
    switch (s->type) {
    case STMT_ASSIGN:
        return s->as.assign.value && expr_has_ident(s->as.assign.value, name, namelen);
    case STMT_PRINT:
        for (size_t i = 0; i < s->as.print.value_count; ++i)
            if (expr_has_ident(s->as.print.values[i], name, namelen)) return 1;
        return 0;
    case STMT_RETURN:
        return s->as.ret.value && expr_has_ident(s->as.ret.value, name, namelen);
    case STMT_IF:
        return (s->as.if_stmt.condition && expr_has_ident(s->as.if_stmt.condition, name, namelen));
    case STMT_WHILE:
        return s->as.while_stmt.condition && expr_has_ident(s->as.while_stmt.condition, name, namelen);
    case STMT_FOR:
        return (s->as.for_stmt.condition && expr_has_ident(s->as.for_stmt.condition, name, namelen));
    case STMT_EXPR:
        return s->as.expr_stmt.expr && expr_has_ident(s->as.expr_stmt.expr, name, namelen);
    case STMT_CALL:
        for (size_t i = 0; i < s->as.call.arg_count; ++i)
            if (expr_has_ident(s->as.call.args[i], name, namelen)) return 1;
        return 0;
    case STMT_FN:
        for (size_t i = 0; i < s->as.fn.body_count; ++i)
            if (stmt_has_ident(s->as.fn.body[i], name, namelen)) return 1;
        return 0;
    default:
        return 0;
    }
}

static type_kind_t lookup_var_type(const char *start, size_t length)
{
    for (size_t i = 0; i < var_types_count; ++i) {
        if (var_types[i].length == length &&
            strncmp(var_types[i].start, start, length) == 0)
            return var_types[i].type;
    }
    return TYPE_UNKNOWN;
}

static void emit_number(sb_t *b, double val)
{
    if (val == (double)(int)val)
        sb_appendf(b, "%d", (int)val);
    else
        sb_appendf(b, "%g", val);
}

/* ------------------------------------------------------------------ *
 * Expression emission                                                *
 * ------------------------------------------------------------------ */

static void emit_expr(sb_t *b, const expr_t *e)
{
    if (!e) { sb_append(b, "0"); return; }

    switch (e->type) {
    case EXPR_NUMBER:
        emit_number(b, e->as.number);
        return;

    case EXPR_STRING:
        sb_append(b, "\"");
        sb_append_n(b, e->as.string.start, e->as.string.length);
        sb_append(b, "\"");
        return;

    case EXPR_IDENTIFIER:
        /* mouse_x / mouse_y are special: they read live mouse state */
        if (e->as.identifier.length == 7 &&
            strncmp(e->as.identifier.start, "mouse_x", 7) == 0) {
            sb_append(b, "GetMouseX()");
        } else if (e->as.identifier.length == 7 &&
                   strncmp(e->as.identifier.start, "mouse_y", 7) == 0) {
            sb_append(b, "GetMouseY()");
        } else {
            sb_append_n(b, e->as.identifier.start, e->as.identifier.length);
        }
        return;

    case EXPR_BINARY:
        /* String concatenation with + */
        if (e->as.binary.op == OP_PLUS &&
            (infer_type(e->as.binary.left) == TYPE_STRING ||
             infer_type(e->as.binary.right) == TYPE_STRING)) {
            sb_append(b, "string_concat(");
            emit_expr(b, e->as.binary.left);
            sb_append(b, ", ");
            emit_expr(b, e->as.binary.right);
            sb_append(b, ")");
        } else {
            sb_append(b, "(");
            emit_expr(b, e->as.binary.left);
            switch (e->as.binary.op) {
            case OP_PLUS:  sb_append(b, " + "); break;
            case OP_MINUS: sb_append(b, " - "); break;
            case OP_STAR:  sb_append(b, " * "); break;
            case OP_SLASH: sb_append(b, " / "); break;
            }
            emit_expr(b, e->as.binary.right);
            sb_append(b, ")");
        }
        return;

    case EXPR_UNARY:
        switch (e->as.unary.op) {
        case UNOP_BANG:  sb_append(b, "!"); break;
        case UNOP_MINUS: sb_append(b, "-"); break;
        }
        emit_expr(b, e->as.unary.operand);
        return;

    case EXPR_CALL:
        /* ---- Built-in functions / struct instantiation ---- */
        if (handle_builtin_call(b, e)) return;

        /* Struct instantiation: FuncName(args) -> (StructName){args} */
        if (is_struct_type(e->as.call.name_start, e->as.call.name_length)) {
            sb_append(b, "(");
            sb_append_n(b, e->as.call.name_start, e->as.call.name_length);
            sb_append(b, "){");
            for (size_t i = 0; i < e->as.call.arg_count; ++i) {
                if (i > 0) sb_append(b, ", ");
                emit_expr(b, e->as.call.args[i]);
            }
            sb_append(b, "}");
            return;
        }

        sb_append_n(b, e->as.call.name_start, e->as.call.name_length);
        sb_append(b, "(");
        for (size_t i = 0; i < e->as.call.arg_count; ++i) {
            if (i > 0) sb_append(b, ", ");
            emit_expr(b, e->as.call.args[i]);
        }
        sb_append(b, ")");
        return;

    case EXPR_COMPARISON:
        sb_append(b, "(");
        emit_expr(b, e->as.comparison.left);
        switch (e->as.comparison.op) {
        case CMP_GT: sb_append(b, " > "); break;
        case CMP_LT: sb_append(b, " < "); break;
        case CMP_GE: sb_append(b, " >= "); break;
        case CMP_LE: sb_append(b, " <= "); break;
        case CMP_EQ: sb_append(b, " == "); break;
        case CMP_NE: sb_append(b, " != "); break;
        }
        emit_expr(b, e->as.comparison.right);
        sb_append(b, ")");
        return;

    case EXPR_ARRAY:
        sb_append(b, "{");
        for (size_t i = 0; i < e->as.array.count; ++i) {
            if (i > 0) sb_append(b, ", ");
            emit_expr(b, e->as.array.elements[i]);
        }
        sb_append(b, "}");
        return;

    case EXPR_INDEX: {
        /* Check if object is a dict -> emit dict_get() instead of [] */
        type_kind_t ot = infer_type(e->as.index.object);
        if (ot == TYPE_DICT) {
            sb_append(b, "dict_get(");
            emit_expr(b, e->as.index.object);
            sb_append(b, ", ");
            emit_expr(b, e->as.index.index);
            sb_append(b, ")");
        } else {
            emit_expr(b, e->as.index.object);
            sb_append(b, "[");
            emit_expr(b, e->as.index.index);
            sb_append(b, "]");
        }
        return;
    }

    case EXPR_DICT:
        /* Standalone dict literal — emit as comment placeholder */
        sb_append(b, "0 /* dict literal */");
        return;

    case EXPR_METHOD: {
        /* Method call: obj.method(args) -> emit C equivalent */
        const char *mn = e->as.method.method_name_start;
        size_t ml = e->as.method.method_name_length;

        /* str.len */
        if (ml == 3 && strncmp(mn, "len", 3) == 0) {
            sb_append(b, "strlen(");
            emit_expr(b, e->as.method.object);
            sb_append(b, ")");
            return;
        }

        /* str.contains(sub) */
        if (ml == 8 && strncmp(mn, "contains", 8) == 0) {
            sb_append(b, "(strstr(");
            emit_expr(b, e->as.method.object);
            sb_append(b, ", ");
            if (e->as.method.arg_count > 0)
                emit_expr(b, e->as.method.args[0]);
            sb_append(b, ") != NULL)");
            return;
        }

        /* str.upper() */
        if (ml == 5 && strncmp(mn, "upper", 5) == 0) {
            sb_append(b, "str_upper(");
            emit_expr(b, e->as.method.object);
            sb_append(b, ")");
            return;
        }

        /* str.lower() */
        if (ml == 5 && strncmp(mn, "lower", 5) == 0) {
            sb_append(b, "str_lower(");
            emit_expr(b, e->as.method.object);
            sb_append(b, ")");
            return;
        }

        /* str.trim() */
        if (ml == 4 && strncmp(mn, "trim", 4) == 0) {
            sb_append(b, "str_trim(");
            emit_expr(b, e->as.method.object);
            sb_append(b, ")");
            return;
        }

        /* str.split(delim) */
        if (ml == 5 && strncmp(mn, "split", 5) == 0) {
            sb_append(b, "str_split(");
            emit_expr(b, e->as.method.object);
            sb_append(b, ", ");
            if (e->as.method.arg_count > 0)
                emit_expr(b, e->as.method.args[0]);
            sb_append(b, ")");
            return;
        }

        /* str.replace(old, new) */
        if (ml == 7 && strncmp(mn, "replace", 7) == 0) {
            sb_append(b, "str_replace(");
            emit_expr(b, e->as.method.object);
            sb_append(b, ", ");
            if (e->as.method.arg_count > 0)
                emit_expr(b, e->as.method.args[0]);
            sb_append(b, ", ");
            if (e->as.method.arg_count > 1)
                emit_expr(b, e->as.method.args[1]);
            sb_append(b, ")");
            return;
        }

        /* Fallback: emit object.method(args) */
        emit_expr(b, e->as.method.object);
        sb_append(b, ".");
        sb_append_n(b, mn, ml);
        sb_append(b, "(");
        for (size_t i = 0; i < e->as.method.arg_count; ++i) {
            if (i > 0) sb_append(b, ", ");
            emit_expr(b, e->as.method.args[i]);
        }
        sb_append(b, ")");
        return;
    }

    case EXPR_FIELD: {
        /* Field/property access: obj.name */
        size_t fl = e->as.field.field_name_length;
        const char *fn = e->as.field.field_name_start;
        /* Built-in: str.len -> strlen(str) */
        if (fl == 3 && strncmp(fn, "len", 3) == 0) {
            sb_append(b, "strlen(");
            emit_expr(b, e->as.field.object);
            sb_append(b, ")");
            return;
        }
        /* Default: obj.field */
        emit_expr(b, e->as.field.object);
        sb_append(b, ".");
        sb_append_n(b, fn, fl);
        return;
    }
    }
}

/* ------------------------------------------------------------------ *
 * Type inference                                                      *
 * ------------------------------------------------------------------ */

static type_kind_t infer_type(const expr_t *e)
{
    if (!e) return TYPE_INT;
    if (e->type == EXPR_STRING) return TYPE_STRING;
    if (e->type == EXPR_NUMBER) {
        if (e->as.number == (double)(int)e->as.number)
            return TYPE_INT;
        return TYPE_DOUBLE;
    }
    if (e->type == EXPR_IDENTIFIER) {
        /* Look up variable type from the top-level type table */
        type_kind_t t = lookup_var_type(e->as.identifier.start,
                                        e->as.identifier.length);
        if (t != TYPE_UNKNOWN) return t;
        return TYPE_INT;
    }
    if (e->type == EXPR_BINARY) {
        type_kind_t lt = infer_type(e->as.binary.left);
        type_kind_t rt = infer_type(e->as.binary.right);
        if (lt == TYPE_STRING || rt == TYPE_STRING) return TYPE_STRING;
        if (lt == TYPE_DOUBLE || rt == TYPE_DOUBLE) return TYPE_DOUBLE;
        return TYPE_INT;
    }
    if (e->type == EXPR_ARRAY) return TYPE_INT;
    if (e->type == EXPR_INDEX) {
        /* Index into a dict returns the value type (int for now) */
        type_kind_t ot = infer_type(e->as.index.object);
        if (ot == TYPE_DICT) return TYPE_INT;
        return TYPE_INT;
    }
    if (e->type == EXPR_METHOD) {
        size_t ml = e->as.method.method_name_length;
        const char *mn = e->as.method.method_name_start;
        if (ml == 3 && strncmp(mn, "len", 3) == 0) return TYPE_INT;
        if (ml == 8 && strncmp(mn, "contains", 8) == 0) return TYPE_INT;
        return TYPE_STRING; /* upper, lower, trim, split, replace */
    }
    if (e->type == EXPR_DICT) return TYPE_DICT;
    if (e->type == EXPR_CALL) {
        /* Built-in input() / read_file() return strings */
        size_t ilen = 5, rflen = 9, felen = 11;
        if (e->as.call.name_length == ilen &&
            strncmp(e->as.call.name_start, "input", ilen) == 0)
            return TYPE_STRING;
        if (e->as.call.name_length == rflen &&
            strncmp(e->as.call.name_start, "read_file", rflen) == 0)
            return TYPE_STRING;
        if (e->as.call.name_length == felen &&
            strncmp(e->as.call.name_start, "file_exists", felen) == 0)
            return TYPE_INT;
        return TYPE_INT;
    }
    return TYPE_INT;
}

static const char *type_cname(type_kind_t t)
{
    switch (t) {
    case TYPE_INT:    return "int";
    case TYPE_DOUBLE: return "double";
    case TYPE_STRING: return "const char*";
    case TYPE_DICT:   return "dict_t";
    default:          return "int";
    }
}

/* ------------------------------------------------------------------ *
 * Color mapping (Raylib)                                             *
 * ------------------------------------------------------------------ */
static const char *map_color(const char *name, size_t len)
{
    struct { const char *name; const char *raylib; } colors[] = {
        { "red", "RED" }, { "green", "GREEN" }, { "blue", "BLUE" },
        { "white", "WHITE" }, { "black", "BLACK" }, { "yellow", "YELLOW" },
        { "orange", "ORANGE" }, { "purple", "PURPLE" }, { "pink", "PINK" },
        { "brown", "BROWN" }, { "gray", "GRAY" }, { "grey", "GRAY" },
        { "lightgray", "LIGHTGRAY" }, { "darkgray", "DARKGRAY" },
        { "skyblue", "SKYBLUE" }, { "maroon", "MAROON" }, { "violet", "VIOLET" },
        { "darkgreen", "DARKGREEN" }, { "lime", "LIME" }, { "gold", "GOLD" },
        { "darkbrown", "DARKBROWN" }, { "beige", "BEIGE" }, { "magenta", "MAGENTA" },
        { "turquoise", "TURQUOISE" }, { NULL, NULL },
    };

    for (int i = 0; colors[i].name; ++i) {
        size_t cLen = strlen(colors[i].name);
        if (len == cLen && strncmp(name, colors[i].name, len) == 0)
            return colors[i].raylib;
    }
    return NULL;
}

/* ------------------------------------------------------------------ *
 * Key mapping (Raylib)                                               *
 * ------------------------------------------------------------------ */
static void emit_key_check(sb_t *b, const char *name, size_t len)
{
    struct { const char *name; size_t len; const char *raylib; } keys[] = {
        { "key_right",   9, "IsKeyDown(KEY_RIGHT)" },
        { "key_left",    8, "IsKeyDown(KEY_LEFT)"  },
        { "key_up",      6, "IsKeyDown(KEY_UP)"    },
        { "key_down",    8, "IsKeyDown(KEY_DOWN)"  },
        { "key_space",   9, "IsKeyDown(KEY_SPACE)" },
        { "key_enter",   9, "IsKeyDown(KEY_ENTER)" },
        { "key_w",       5, "IsKeyDown(KEY_W)"     },
        { "key_a",       5, "IsKeyDown(KEY_A)"     },
        { "key_s",       5, "IsKeyDown(KEY_S)"     },
        { "key_d",       5, "IsKeyDown(KEY_D)"     },
        { "mouse_down", 10, "IsMouseButtonDown(MOUSE_LEFT_BUTTON)" },
        { "mouse_click", 11, "IsMouseButtonPressed(MOUSE_LEFT_BUTTON)" },
    };

    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; ++i) {
        if (len == keys[i].len && strncmp(name, keys[i].name, len) == 0) {
            sb_appendf(b, "if (%s)", keys[i].raylib);
            return;
        }
    }
    sb_append_n(b, name, len);
    sb_append(b, " /* unknown key */");
}

/* ------------------------------------------------------------------ *
 * Built-in function dispatch (math, random, type conversion, sleep)  *
 * Returns 1 if the call was handled, 0 for normal function calls.    *
 * ------------------------------------------------------------------ */
static int handle_builtin_call(sb_t *b, const expr_t *e)
{
    size_t nl = e->as.call.name_length;
    const char *nm = e->as.call.name_start;

    /* ---- Math functions (all require <math.h>) ---- */
    if (nl == 4 && strncmp(nm, "sqrt", 4) == 0) {
        sb_append(b, "sqrt(");
        emit_expr(b, e->as.call.args[0]); sb_append(b, ")"); return 1; }
    if (nl == 3 && strncmp(nm, "sin", 3) == 0) {
        sb_append(b, "sin(");
        emit_expr(b, e->as.call.args[0]); sb_append(b, ")"); return 1; }
    if (nl == 3 && strncmp(nm, "cos", 3) == 0) {
        sb_append(b, "cos(");
        emit_expr(b, e->as.call.args[0]); sb_append(b, ")"); return 1; }
    if (nl == 3 && strncmp(nm, "tan", 3) == 0) {
        sb_append(b, "tan(");
        emit_expr(b, e->as.call.args[0]); sb_append(b, ")"); return 1; }
    if (nl == 5 && strncmp(nm, "floor", 5) == 0) {
        sb_append(b, "floor(");
        emit_expr(b, e->as.call.args[0]); sb_append(b, ")"); return 1; }
    if (nl == 4 && strncmp(nm, "ceil", 4) == 0) {
        sb_append(b, "ceil(");
        emit_expr(b, e->as.call.args[0]); sb_append(b, ")"); return 1; }
    if (nl == 5 && strncmp(nm, "round", 5) == 0) {
        sb_append(b, "round(");
        emit_expr(b, e->as.call.args[0]); sb_append(b, ")"); return 1; }

    /* abs(x) -> fabs(x) for doubles */
    if (nl == 3 && strncmp(nm, "abs", 3) == 0) {
        sb_append(b, "fabs(");
        emit_expr(b, e->as.call.args[0]); sb_append(b, ")"); return 1; }

    /* min(a,b), max(a,b) -> ternary */
    if (nl == 3 && strncmp(nm, "min", 3) == 0) {
        sb_append(b, "("); emit_expr(b, e->as.call.args[0]);
        sb_append(b, " < "); emit_expr(b, e->as.call.args[1]);
        sb_append(b, " ? "); emit_expr(b, e->as.call.args[0]);
        sb_append(b, " : "); emit_expr(b, e->as.call.args[1]);
        sb_append(b, ")"); return 1; }
    if (nl == 3 && strncmp(nm, "max", 3) == 0) {
        sb_append(b, "("); emit_expr(b, e->as.call.args[0]);
        sb_append(b, " > "); emit_expr(b, e->as.call.args[1]);
        sb_append(b, " ? "); emit_expr(b, e->as.call.args[0]);
        sb_append(b, " : "); emit_expr(b, e->as.call.args[1]);
        sb_append(b, ")"); return 1; }

    /* clamp(val, lo, hi) */
    if (nl == 5 && strncmp(nm, "clamp", 5) == 0) {
        sb_append(b, "("); emit_expr(b, e->as.call.args[0]);
        sb_append(b, " < "); emit_expr(b, e->as.call.args[1]);
        sb_append(b, " ? "); emit_expr(b, e->as.call.args[1]);
        sb_append(b, " : ("); emit_expr(b, e->as.call.args[0]);
        sb_append(b, " > "); emit_expr(b, e->as.call.args[2]);
        sb_append(b, " ? "); emit_expr(b, e->as.call.args[2]);
        sb_append(b, " : "); emit_expr(b, e->as.call.args[0]);
        sb_append(b, "))"); return 1; }

    /* ---- Random ---- */
    /* random() -> (double)rand() / RAND_MAX */
    if (nl == 6 && strncmp(nm, "random", 6) == 0 && e->as.call.arg_count == 0) {
        sb_append(b, "((double)rand() / (double)RAND_MAX)"); return 1; }
    /* random_int(lo, hi) -> lo + rand() % (hi - lo + 1) */
    if (nl == 10 && strncmp(nm, "random_int", 10) == 0) {
        sb_append(b, "("); emit_expr(b, e->as.call.args[0]);
        sb_append(b, " + rand() % (("); emit_expr(b, e->as.call.args[1]);
        sb_append(b, " - "); emit_expr(b, e->as.call.args[0]);
        sb_append(b, ") + 1))"); return 1; }

    /* file_exists("path") */
    if (nl == 11 && strncmp(nm, "file_exists", 11) == 0) {
        sb_append(b, "file_exists(");
        if (e->as.call.arg_count > 0) emit_expr(b, e->as.call.args[0]);
        sb_append(b, ")"); return 1; }

    /* to_int(x), to_double(x) -> cast */
    if (nl == 6 && strncmp(nm, "to_int", 6) == 0) {
        sb_append(b, "(int)(");
        emit_expr(b, e->as.call.args[0]); sb_append(b, ")"); return 1; }
    if (nl == 9 && strncmp(nm, "to_double", 9) == 0) {
        sb_append(b, "(double)(");
        emit_expr(b, e->as.call.args[0]); sb_append(b, ")"); return 1; }

    /* to_string(x) -> helper call */
    if (nl == 9 && strncmp(nm, "to_string", 9) == 0) {
        sb_append(b, "to_string(");
        emit_expr(b, e->as.call.args[0]); sb_append(b, ")"); return 1; }

    /* sleep(ms) -> usleep */
    if (nl == 5 && strncmp(nm, "sleep", 5) == 0) {
        sb_append(b, "usleep((");
        emit_expr(b, e->as.call.args[0]);
        sb_append(b, ") * 1000)"); return 1; }

    /* time() -> clock() / CLOCKS_PER_SEC */
    if (nl == 4 && strncmp(nm, "time", 4) == 0) {
        sb_append(b, "((double)clock() / CLOCKS_PER_SEC)"); return 1; }

    /* ---- Collision detection (raylib, returns int/bool) ---- */
    /* collide_circle(x1, y1, r1, x2, y2, r2) */
    if (nl == 14 && strncmp(nm, "collide_circle", 14) == 0) {
        sb_append(b, "CheckCollisionCircles((Vector2){");
        emit_expr(b, e->as.call.args[0]); sb_append(b, ", ");
        emit_expr(b, e->as.call.args[1]); sb_append(b, "}, ");
        emit_expr(b, e->as.call.args[2]);
        sb_append(b, ", (Vector2){");
        emit_expr(b, e->as.call.args[3]); sb_append(b, ", ");
        emit_expr(b, e->as.call.args[4]); sb_append(b, "}, ");
        emit_expr(b, e->as.call.args[5]);
        sb_append(b, ")"); return 1; }

    /* collide_rect(x1, y1, w1, h1, x2, y2, w2, h2) */
    if (nl == 12 && strncmp(nm, "collide_rect", 12) == 0) {
        sb_append(b, "CheckCollisionRecs((Rectangle){");
        emit_expr(b, e->as.call.args[0]); sb_append(b, ", ");
        emit_expr(b, e->as.call.args[1]); sb_append(b, ", ");
        emit_expr(b, e->as.call.args[2]); sb_append(b, ", ");
        emit_expr(b, e->as.call.args[3]);
        sb_append(b, "}, (Rectangle){");
        emit_expr(b, e->as.call.args[4]); sb_append(b, ", ");
        emit_expr(b, e->as.call.args[5]); sb_append(b, ", ");
        emit_expr(b, e->as.call.args[6]); sb_append(b, ", ");
        emit_expr(b, e->as.call.args[7]);
        sb_append(b, "})"); return 1; }

    /* collide_point(x, y, rx, ry, rw, rh) */
    if (nl == 13 && strncmp(nm, "collide_point", 13) == 0) {
        sb_append(b, "CheckCollisionPointRec((Vector2){");
        emit_expr(b, e->as.call.args[0]); sb_append(b, ", ");
        emit_expr(b, e->as.call.args[1]);
        sb_append(b, "}, (Rectangle){");
        emit_expr(b, e->as.call.args[2]); sb_append(b, ", ");
        emit_expr(b, e->as.call.args[3]); sb_append(b, ", ");
        emit_expr(b, e->as.call.args[4]); sb_append(b, ", ");
        emit_expr(b, e->as.call.args[5]);
        sb_append(b, "})"); return 1; }

    return 0;
}

/* ------------------------------------------------------------------ *
 * Legacy: emit call (space-separated game syntax)                    *
 * ------------------------------------------------------------------ */
static void emit_legacy_call(sb_t *b, const stmt_t *s, const char *indent)
{
    if (!s || s->type != STMT_CALL) return;

    const char *cmd    = s->as.call.name_start;
    size_t      cmdLen = s->as.call.name_length;

    sb_append(b, indent);

    if ((cmdLen == 11 && strncmp(cmd, "draw_circle", 11) == 0) ||
        (cmdLen == 6  && strncmp(cmd, "circle", 6) == 0))
    {
        sb_append(b, "DrawCircle(");
        for (size_t i = 0; i < s->as.call.arg_count; ++i) {
            if (i > 0) sb_append(b, ", ");
            if (i == 3 && s->as.call.arg_count == 4) {
                const expr_t *ce = s->as.call.args[i];
                if (ce->type == EXPR_STRING) {
                    const char *clr = map_color(ce->as.string.start,
                                                ce->as.string.length);
                    if (clr) { sb_append(b, clr); continue; }
                }
            }
            emit_expr(b, s->as.call.args[i]);
        }
        sb_append(b, ");\n");
        return;
    }

    /* draw_rect <x>, <y>, <w>, <h>, "color" -> DrawRectangle(x, y, w, h, COLOR) */
    if (cmdLen == 9 && strncmp(cmd, "draw_rect", 9) == 0) {
        sb_append(b, "DrawRectangle(");
        for (size_t i = 0; i < s->as.call.arg_count; ++i) {
            if (i > 0) sb_append(b, ", ");
            if (i == 4 && s->as.call.arg_count == 5) {
                const expr_t *ce = s->as.call.args[i];
                if (ce->type == EXPR_STRING) {
                    const char *clr = map_color(ce->as.string.start,
                                                ce->as.string.length);
                    if (clr) { sb_append(b, clr); continue; }
                }
            }
            emit_expr(b, s->as.call.args[i]);
        }
        sb_append(b, ");\n");
        return;
    }

    /* draw_sprite <name>, <x>, <y> -> DrawTexture(name, x, y, WHITE) */
    if (cmdLen == 11 && strncmp(cmd, "draw_sprite", 11) == 0) {
        sb_append(b, "DrawTexture(");
        for (size_t i = 0; i < s->as.call.arg_count; ++i) {
            if (i > 0) sb_append(b, ", ");
            emit_expr(b, s->as.call.args[i]);
        }
        sb_append(b, ", WHITE);\n");
        return;
    }

    sb_append_n(b, cmd, cmdLen);
    sb_append(b, "(");
    for (size_t i = 0; i < s->as.call.arg_count; ++i) {
        if (i > 0) sb_append(b, ", ");
        emit_expr(b, s->as.call.args[i]);
    }
    sb_append(b, ");\n");
}

/* ------------------------------------------------------------------ *
 * Statement emission helpers                                         *
 * ------------------------------------------------------------------ */

static void emit_assign(sb_t *b, const stmt_t *s, const char *indent)
{
    if (!s) return;

    const expr_t *val = s->as.assign.value;

    /* Handle input() inside blocks */
    if (val && val->type == EXPR_CALL) {
        size_t ilen = 5;
        if (val->as.call.name_length == ilen &&
            strncmp(val->as.call.name_start, "input", ilen) == 0) {
            sb_append(b, indent);
            sb_append(b, "printf(");
            if (val->as.call.arg_count > 0)
                emit_expr(b, val->as.call.args[0]);
            sb_append(b, ");\n");
            sb_append(b, indent);
            sb_append(b, "char ");
            sb_append_n(b, s->as.assign.name_start, s->as.assign.name_length);
            sb_append(b, "[256];\n");
            sb_append(b, indent);
            sb_append(b, "fgets(");
            sb_append_n(b, s->as.assign.name_start, s->as.assign.name_length);
            sb_append(b, ", sizeof(");
            sb_append_n(b, s->as.assign.name_start, s->as.assign.name_length);
            sb_append(b, "), stdin);\n");
            sb_append(b, indent);
            sb_append_n(b, s->as.assign.name_start, s->as.assign.name_length);
            sb_append(b, "[strcspn(");
            sb_append_n(b, s->as.assign.name_start, s->as.assign.name_length);
            sb_append(b, ", \"\\n\")] = '\\0';\n");
            return;
        }
        size_t rflen = 9;
        if (val->as.call.name_length == rflen &&
            strncmp(val->as.call.name_start, "read_file", rflen) == 0) {
            sb_append(b, indent);
            sb_append(b, "char ");
            sb_append_n(b, s->as.assign.name_start, s->as.assign.name_length);
            sb_append(b, "[4096];\n");
            sb_append(b, indent);
            sb_append(b, "{ FILE *_f = fopen(");
            if (val->as.call.arg_count > 0)
                emit_expr(b, val->as.call.args[0]);
            sb_append(b, ", \"r\"); ");
            sb_append(b, "if (_f) { size_t _n = fread(");
            sb_append_n(b, s->as.assign.name_start, s->as.assign.name_length);
            sb_append(b, ", 1, sizeof(");
            sb_append_n(b, s->as.assign.name_start, s->as.assign.name_length);
            sb_append(b, ") - 1, _f); ");
            sb_append_n(b, s->as.assign.name_start, s->as.assign.name_length);
            sb_append(b, "[_n] = '\\0'; fclose(_f); } }\n");
            return;
        }
    }

    sb_append(b, indent);
    sb_append_n(b, s->as.assign.name_start, s->as.assign.name_length);
    sb_append(b, " = ");
    emit_expr(b, s->as.assign.value);
    sb_append(b, ";\n");
}

static void emit_stmt(sb_t *b, const stmt_t *s, const char *indent);
static void emit_block(sb_t *b, const stmt_t *const *stmts, size_t count,
                       const char *indent)
{
    for (size_t i = 0; i < count; ++i)
        emit_stmt(b, stmts[i], indent);
}

static void emit_stmt(sb_t *b, const stmt_t *s, const char *indent)
{
    if (!s) return;

    /* Explain mode: annotate each statement with its source line */
    if (g_explain_mode && s->line > 0) {
        if (g_source_filename) {
            sb_appendf(b, "// [%s:%d]\n", g_source_filename, s->line);
        } else {
            sb_appendf(b, "// [line %d]\n", s->line);
        }
    }

    switch (s->type) {
    case STMT_EXPR: {
        const expr_t *ex = s->as.expr_stmt.expr;
        /* Detect write_file / append_file built-in */
        if (ex && ex->type == EXPR_CALL) {
            size_t wflen = 10, aflen = 11;
            int is_write = (ex->as.call.name_length == wflen &&
                strncmp(ex->as.call.name_start, "write_file", wflen) == 0);
            int is_append = (ex->as.call.name_length == aflen &&
                strncmp(ex->as.call.name_start, "append_file", aflen) == 0);
            if (is_write || is_append) {
                sb_append(b, indent);
                sb_append(b, "{ FILE *_f = fopen(");
                if (ex->as.call.arg_count > 0)
                    emit_expr(b, ex->as.call.args[0]);
                sb_append(b, is_append ? ", \"a\"); " : ", \"w\"); ");
                sb_append(b, "fputs(");
                if (ex->as.call.arg_count > 1)
                    emit_expr(b, ex->as.call.args[1]);
                sb_append(b, ", _f); fclose(_f); }\n");
                break;
            }
        }
        sb_append(b, indent);
        emit_expr(b, ex);
        sb_append(b, ";\n");
        break;
    }

    case STMT_ASSIGN:
        emit_assign(b, s, indent);
        break;

    case STMT_PRINT: {
        sb_append(b, indent);
        size_t n = s->as.print.value_count;
        if (n == 0) { sb_append(b, "printf(\"\\n\");\n"); break; }

        /* printf("%s%d...\n", arg1, arg2, ...) */
        sb_append(b, "printf(\"");
        for (size_t i = 0; i < n; ++i) {
            type_kind_t t = infer_type(s->as.print.values[i]);
            switch (t) {
            case TYPE_STRING: sb_append(b, "%s"); break;
            case TYPE_DOUBLE: sb_append(b, "%g"); break;
            default:          sb_append(b, "%d"); break;
            }
        }
        sb_append(b, "\\n\", ");
        for (size_t i = 0; i < n; ++i) {
            if (i > 0) sb_append(b, ", ");
            emit_expr(b, s->as.print.values[i]);
        }
        sb_append(b, ");\n");
        break;
    }

    case STMT_RETURN:
        sb_append(b, indent);
        sb_append(b, "return");
        if (s->as.ret.value) {
            sb_append(b, " ");
            emit_expr(b, s->as.ret.value);
        }
        sb_append(b, ";\n");
        break;

    case STMT_IF: {
        sb_append(b, indent);
        sb_append(b, "if (");
        if (s->as.if_stmt.condition)
            emit_expr(b, s->as.if_stmt.condition);
        sb_append(b, ") {\n");
        emit_block(b, (const stmt_t *const *)s->as.if_stmt.then_body,
                   s->as.if_stmt.then_count, indent);

        /* Walk else chain */
        const stmt_t *ec = s->as.if_stmt.else_branch;
        while (ec) {
            if (ec->type == STMT_IF && ec->as.if_stmt.condition) {
                sb_append(b, indent);
                sb_append(b, "} else if (");
                emit_expr(b, ec->as.if_stmt.condition);
                sb_append(b, ") {\n");
                emit_block(b, (const stmt_t *const *)ec->as.if_stmt.then_body,
                           ec->as.if_stmt.then_count, indent);
                ec = ec->as.if_stmt.else_branch;
            } else {
                sb_append(b, indent);
                sb_append(b, "} else {\n");
                emit_block(b, (const stmt_t *const *)ec->as.if_stmt.then_body,
                           ec->as.if_stmt.then_count, indent);
                ec = NULL;
            }
        }
        sb_append(b, indent);
        sb_append(b, "}\n");
        break;
    }

    case STMT_WHILE:
        sb_append(b, indent);
        sb_append(b, "while (");
        if (s->as.while_stmt.condition)
            emit_expr(b, s->as.while_stmt.condition);
        sb_append(b, ") {\n");
        emit_block(b, (const stmt_t *const *)s->as.while_stmt.body,
                   s->as.while_stmt.body_count, indent);
        sb_append(b, indent);
        sb_append(b, "}\n");
        break;

    case STMT_FOR:
        sb_append(b, indent);
        sb_append(b, "for (");
        if (s->as.for_stmt.init) {
            sb_append_n(b, s->as.for_stmt.init->as.assign.name_start,
                        s->as.for_stmt.init->as.assign.name_length);
            sb_append(b, " = ");
            emit_expr(b, s->as.for_stmt.init->as.assign.value);
        }
        sb_append(b, "; ");
        if (s->as.for_stmt.condition)
            emit_expr(b, s->as.for_stmt.condition);
        sb_append(b, "; ");
        if (s->as.for_stmt.inc) {
            sb_append_n(b, s->as.for_stmt.inc->as.assign.name_start,
                        s->as.for_stmt.inc->as.assign.name_length);
            sb_append(b, " = ");
            emit_expr(b, s->as.for_stmt.inc->as.assign.value);
        }
        sb_append(b, ") {\n");
        emit_block(b, (const stmt_t *const *)s->as.for_stmt.body,
                   s->as.for_stmt.body_count, indent);
        sb_append(b, indent);
        sb_append(b, "}\n");
        break;

    case STMT_CALL:
        emit_legacy_call(b, s, indent);
        break;

    case STMT_SPRITE:
        /* Texture2D declarations are emitted in main() before the game
           loop (they need InitWindow to have run); nothing to emit here. */
        break;

    case STMT_BREAK:
        sb_append(b, indent);
        sb_append(b, "break;\n");
        break;

    case STMT_CONTINUE:
        sb_append(b, indent);
        sb_append(b, "continue;\n");
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ *
 * Audio (sound/music) built-ins                                      *
 * ------------------------------------------------------------------ */

/* Direct check: is e a call to the given built-in name? */
static int expr_is_call(const expr_t *e, const char *name, size_t len)
{
    return e && e->type == EXPR_CALL && e->as.call.name_length == len &&
           strncmp(e->as.call.name_start, name, len) == 0;
}

/* Is this expression a play_sound() or play_music() call? */
static int call_is_play_audio(const expr_t *e)
{
    return expr_is_call(e, "play_sound", 10) ||
           expr_is_call(e, "play_music", 10);
}

/* Derive a C variable name from a music file path: "music/bgm.ogg" ->
   "bgm".  Falls back to "music" for non-literal or empty paths. */
static void derive_name_from_path(const expr_t *path_expr, char *out,
                                  size_t out_size)
{
    const char *fallback = "music";
    if (out_size == 0) return;
    out[0] = '\0';

    if (!path_expr || path_expr->type != EXPR_STRING) {
        snprintf(out, out_size, "%s", fallback);
        return;
    }

    const char *p  = path_expr->as.string.start;
    size_t      pl = path_expr->as.string.length;

    /* Basename: skip everything up to the last '/' */
    const char *base = p;
    for (size_t i = 0; i < pl; ++i)
        if (p[i] == '/') base = p + i + 1;

    /* Strip the extension: cut at the first '.' */
    const char *end = p + pl;
    for (const char *q = base; q < p + pl; ++q)
        if (*q == '.') { end = q; break; }

    size_t blen = (size_t)(end - base);
    if (blen == 0) { snprintf(out, out_size, "%s", fallback); return; }
    if (blen >= out_size) blen = out_size - 1;
    memcpy(out, base, blen);
    out[blen] = '\0';
}

/* Emit "Sound <name> = LoadSound(<path>);" — name from expr or text. */
static void emit_sound_decl(sb_t *b, const expr_t *name_expr,
                            const char *name_start, size_t name_len,
                            const expr_t *path_expr)
{
    sb_append(b, "    Sound ");
    if (name_expr)      emit_expr(b, name_expr);
    else if (name_start) sb_append_n(b, name_start, name_len);
    else                sb_append(b, "sound");
    sb_append(b, " = LoadSound(");
    if (path_expr) emit_expr(b, path_expr);
    sb_append(b, ");\n");
}

static void emit_sound_unload(sb_t *b, const expr_t *name_expr,
                              const char *name_start, size_t name_len)
{
    sb_append(b, "    UnloadSound(");
    if (name_expr)      emit_expr(b, name_expr);
    else if (name_start) sb_append_n(b, name_start, name_len);
    else                sb_append(b, "sound");
    sb_append(b, ");\n");
}

/* Emit "Music <name> = LoadMusicStream(<path>); PlayMusicStream(<name>);" */
static void emit_music_decl(sb_t *b, const expr_t *path_expr)
{
    char mname[128];
    derive_name_from_path(path_expr, mname, sizeof mname);
    sb_append(b, "    Music ");
    sb_append(b, mname);
    sb_append(b, " = LoadMusicStream(");
    if (path_expr) emit_expr(b, path_expr);
    sb_append(b, ");\n");
    sb_append(b, "    PlayMusicStream(");
    sb_append(b, mname);
    sb_append(b, ");\n");
}

static void emit_music_unload(sb_t *b, const expr_t *path_expr)
{
    char mname[128];
    derive_name_from_path(path_expr, mname, sizeof mname);
    sb_append(b, "    UnloadMusicStream(");
    sb_append(b, mname);
    sb_append(b, ");\n");
}

/* Emit a play_sound() / play_music() call in the update section.
   play_music emits the per-frame UpdateMusicStream() call. */
static void emit_play_audio(sb_t *b, const char *cmd, size_t cmdLen,
                            expr_t **args, size_t arg_count,
                            const char *indent)
{
    if (cmdLen == 10 && strncmp(cmd, "play_sound", 10) == 0) {
        sb_append(b, indent);
        sb_append(b, "PlaySound(");
        if (arg_count > 0) emit_expr(b, args[0]);
        sb_append(b, ");\n");
        return;
    }
    /* play_music */
    char mname[128];
    derive_name_from_path(arg_count > 0 ? args[0] : NULL,
                          mname, sizeof mname);
    sb_append(b, indent);
    sb_append(b, "UpdateMusicStream(");
    sb_append(b, mname);
    sb_append(b, ");\n");
}

/* Emit Sound/Music declarations for a statement, recursing into
   event bodies so audio declared inside 'on' blocks still works. */
static void emit_audio_decls(sb_t *b, const stmt_t *s)
{
    if (!s) return;
    if (s->type == STMT_ASSIGN && s->as.assign.value &&
        expr_is_call(s->as.assign.value, "load_sound", 10)) {
        emit_sound_decl(b, NULL, s->as.assign.name_start,
                        s->as.assign.name_length,
                        s->as.assign.value->as.call.arg_count > 0
                            ? s->as.assign.value->as.call.args[0] : NULL);
    } else if (s->type == STMT_CALL &&
               s->as.call.name_length == 10 &&
               strncmp(s->as.call.name_start, "load_sound", 10) == 0 &&
               s->as.call.arg_count >= 2) {
        emit_sound_decl(b, s->as.call.args[0], NULL, 0,
                        s->as.call.args[1]);
    } else if (s->type == STMT_CALL &&
               s->as.call.name_length == 10 &&
               strncmp(s->as.call.name_start, "play_music", 10) == 0) {
        emit_music_decl(b, s->as.call.arg_count > 0
                               ? s->as.call.args[0] : NULL);
    } else if (s->type == STMT_EXPR && s->as.expr_stmt.expr &&
               expr_is_call(s->as.expr_stmt.expr, "play_music", 10)) {
        emit_music_decl(b, s->as.expr_stmt.expr->as.call.arg_count > 0
                               ? s->as.expr_stmt.expr->as.call.args[0] : NULL);
    } else if (s->type == STMT_EVENT) {
        for (size_t i = 0; i < s->as.event.body_count; ++i)
            emit_audio_decls(b, s->as.event.body[i]);
    }
}

static void emit_audio_unloads(sb_t *b, const stmt_t *s)
{
    if (!s) return;
    if (s->type == STMT_ASSIGN && s->as.assign.value &&
        expr_is_call(s->as.assign.value, "load_sound", 10)) {
        emit_sound_unload(b, NULL, s->as.assign.name_start,
                          s->as.assign.name_length);
    } else if (s->type == STMT_CALL &&
               s->as.call.name_length == 10 &&
               strncmp(s->as.call.name_start, "load_sound", 10) == 0 &&
               s->as.call.arg_count >= 1) {
        emit_sound_unload(b, s->as.call.args[0], NULL, 0);
    } else if (s->type == STMT_CALL &&
               s->as.call.name_length == 10 &&
               strncmp(s->as.call.name_start, "play_music", 10) == 0) {
        emit_music_unload(b, s->as.call.arg_count > 0
                                 ? s->as.call.args[0] : NULL);
    } else if (s->type == STMT_EXPR && s->as.expr_stmt.expr &&
               expr_is_call(s->as.expr_stmt.expr, "play_music", 10)) {
        emit_music_unload(b, s->as.expr_stmt.expr->as.call.arg_count > 0
                                 ? s->as.expr_stmt.expr->as.call.args[0] : NULL);
    } else if (s->type == STMT_EVENT) {
        for (size_t i = 0; i < s->as.event.body_count; ++i)
            emit_audio_unloads(b, s->as.event.body[i]);
    }
}

/* ------------------------------------------------------------------ *
 * Public API                                                         *
 * ------------------------------------------------------------------ */

codegen_result_t codegen_emit(const program_t *prog)
{
    codegen_result_t result = { NULL, 0 };

    if (!prog) return result;

    sb_t b;
    if (!sb_init(&b)) return result;

    /* Build variable type table from top-level assignments */
    build_var_types(prog);
    build_struct_types(prog);

    /* Detect game mode */
    int has_window = 0;
    int uses_sprite = 0;
    int uses_audio  = 0;
    int uses_collision = 0;
    int uses_mouse = 0;
    for (size_t i = 0; i < prog->count; ++i) {
        const stmt_t *s = prog->stmts[i];
        if (s->type == STMT_WINDOW) {
            has_window = 1;
        } else if (s->type == STMT_SPRITE) {
            uses_sprite = 1;
        } else if (s->type == STMT_CALL &&
                   s->as.call.name_length == 11 &&
                   strncmp(s->as.call.name_start, "draw_sprite", 11) == 0) {
            uses_sprite = 1;
        }
        /* Sound/music built-ins imply raylib audio */
        uses_audio = uses_audio ||
                     stmt_has_call(s, "load_sound", 10) ||
                     stmt_has_call(s, "play_sound", 10) ||
                     stmt_has_call(s, "play_music", 10);
        /* Collision built-ins imply raylib */
        uses_collision = uses_collision ||
                         stmt_has_call(s, "collide_circle", 14) ||
                         stmt_has_call(s, "collide_rect", 12) ||
                         stmt_has_call(s, "collide_point", 13);
        /* Mouse built-ins imply raylib input */
        uses_mouse = uses_mouse ||
                     stmt_has_ident(s, "mouse_x", 7) ||
                     stmt_has_ident(s, "mouse_y", 7) ||
                     stmt_has_ident(s, "mouse_down", 10) ||
                     stmt_has_ident(s, "mouse_click", 11);
    }

    result.needs_raylib = has_window || uses_sprite || uses_audio || uses_collision || uses_mouse;

    /* Detect which headers are needed */
    int needs_math_h   = 0;
    int needs_time_h   = 0;
    int needs_unistd_h = 0;
    for (size_t i = 0; i < prog->count && !(needs_math_h && needs_time_h && needs_unistd_h); ++i) {
        const stmt_t *s = prog->stmts[i];
        needs_math_h = needs_math_h || stmt_has_call(prog->stmts[i],
            "sqrt", 4) || stmt_has_call(prog->stmts[i], "sin", 3)
            || stmt_has_call(prog->stmts[i], "cos", 3)
            || stmt_has_call(prog->stmts[i], "tan", 3)
            || stmt_has_call(prog->stmts[i], "floor", 5)
            || stmt_has_call(prog->stmts[i], "ceil", 4)
            || stmt_has_call(prog->stmts[i], "round", 5)
            || stmt_has_call(prog->stmts[i], "abs", 3);
        needs_time_h   = needs_time_h   || stmt_has_call(prog->stmts[i], "time", 4);
        needs_unistd_h = needs_unistd_h || stmt_has_call(prog->stmts[i], "sleep", 5);
        if (!s) continue;
    }

    /* Emit includes */
    if (has_window || uses_sprite || uses_audio || uses_collision || uses_mouse) {
        sb_append(&b, "#include \"raylib.h\"\n");
    }
    sb_append(&b, "#include <stdio.h>\n");
    sb_append(&b, "#include <stdlib.h>\n");
    sb_append(&b, "#include <string.h>\n");
    if (needs_math_h)   sb_append(&b, "#include <math.h>\n");
    if (needs_time_h)   sb_append(&b, "#include <time.h>\n");
    if (needs_unistd_h) sb_append(&b, "#include <unistd.h>\n");
    sb_append(&b, "\n");

    /* Check if dict helpers are needed (scan all expressions) */
    int needs_dict = 0;
    for (size_t i = 0; i < prog->count && !needs_dict; ++i) {
        const stmt_t *s = prog->stmts[i];
        switch (s->type) {
        case STMT_ASSIGN:
            if (s->as.assign.value)
                needs_dict = expr_has_dict(s->as.assign.value);
            break;
        case STMT_PRINT:
            for (size_t k = 0; k < s->as.print.value_count && !needs_dict; ++k)
                needs_dict = expr_has_dict(s->as.print.values[k]);
            break;
        case STMT_RETURN:
            if (s->as.ret.value)
                needs_dict = expr_has_dict(s->as.ret.value);
            break;
        case STMT_IF:
            if (s->as.if_stmt.condition)
                needs_dict = expr_has_dict(s->as.if_stmt.condition);
            break;
        case STMT_WHILE:
            if (s->as.while_stmt.condition)
                needs_dict = expr_has_dict(s->as.while_stmt.condition);
            break;
        case STMT_FOR:
            if (s->as.for_stmt.condition)
                needs_dict = expr_has_dict(s->as.for_stmt.condition);
            break;
        case STMT_CALL:
        case STMT_EXPR:
        case STMT_WINDOW:
        default:
            break;
        }
    }

    /* Emit dict helper types/functions if needed */
    if (needs_dict) {
        sb_append(&b, "#define DICT_CAP 64\n");
        sb_append(&b, "typedef struct {\n");
        sb_append(&b, "    const char *keys[DICT_CAP];\n");
        sb_append(&b, "    int values[DICT_CAP];\n");
        sb_append(&b, "    size_t size;\n");
        sb_append(&b, "} dict_t;\n");
        sb_append(&b, "\n");
        sb_append(&b, "static void dict_set(dict_t *d, const char *key, int val) {\n");
        sb_append(&b, "    for (size_t i = 0; i < d->size; ++i) {\n");
        sb_append(&b, "        if (strcmp(d->keys[i], key) == 0) {\n");
        sb_append(&b, "            d->values[i] = val; return;\n");
        sb_append(&b, "        }\n");
        sb_append(&b, "    }\n");
        sb_append(&b, "    d->keys[d->size] = strdup(key);\n");
        sb_append(&b, "    d->values[d->size] = val;\n");
        sb_append(&b, "    d->size++;\n");
        sb_append(&b, "}\n");
        sb_append(&b, "\n");
        sb_append(&b, "static int dict_get(dict_t d, const char *key) {\n");
        sb_append(&b, "    for (size_t i = 0; i < d.size; ++i) {\n");
        sb_append(&b, "        if (strcmp(d.keys[i], key) == 0)\n");
        sb_append(&b, "            return d.values[i];\n");
        sb_append(&b, "    }\n");
        sb_append(&b, "    return 0;\n");
        sb_append(&b, "}\n");
        sb_append(&b, "\n");
        sb_append(&b, "static int dict_contains(dict_t d, const char *key) {\n");
        sb_append(&b, "    for (size_t i = 0; i < d.size; ++i) {\n");
        sb_append(&b, "        if (strcmp(d.keys[i], key) == 0)\n");
        sb_append(&b, "            return 1;\n");
        sb_append(&b, "    }\n");
        sb_append(&b, "    return 0;\n");
        sb_append(&b, "}\n");
        sb_append(&b, "\n");
    }

    /* Check if file_exists helper is needed */
    int needs_file_exists = 0;
    for (size_t i = 0; i < prog->count && !needs_file_exists; ++i) {
        const stmt_t *s = prog->stmts[i];
        if (s->type == STMT_FN) {
            for (size_t j = 0; j < s->as.fn.body_count && !needs_file_exists; ++j)
                if (s->as.fn.body[j]->type == STMT_RETURN && s->as.fn.body[j]->as.ret.value)
                    needs_file_exists = expr_has_call(
                        s->as.fn.body[j]->as.ret.value, "file_exists", 11);
        }
    }
    if (!needs_file_exists) {
        for (size_t i = 0; i < prog->count && !needs_file_exists; ++i) {
            const stmt_t *s = prog->stmts[i];
            switch (s->type) {
            case STMT_ASSIGN:
                if (s->as.assign.value)
                    needs_file_exists = expr_has_call(
                        s->as.assign.value, "file_exists", 11);
                break;
            case STMT_PRINT:
                for (size_t k = 0; k < s->as.print.value_count && !needs_file_exists; ++k)
                    needs_file_exists = expr_has_call(
                        s->as.print.values[k], "file_exists", 11);
                break;
            case STMT_IF:
                if (s->as.if_stmt.condition)
                    needs_file_exists = expr_has_call(
                        s->as.if_stmt.condition, "file_exists", 11);
                break;
            default: break;
            }
        }
    }

    /* Emit file_exists helper if needed */
    if (needs_file_exists) {
        sb_append(&b, "static int file_exists(const char *path) {\n");
        sb_append(&b, "    FILE *f = fopen(path, \"r\");\n");
        sb_append(&b, "    if (!f) return 0;\n");
        sb_append(&b, "    fclose(f);\n");
        sb_append(&b, "    return 1;\n");
        sb_append(&b, "}\n");
        sb_append(&b, "\n");
    }

    /* Emit to_string helper if needed */
    int needs_to_string = 0;
    for (size_t i = 0; i < prog->count && !needs_to_string; ++i)
        needs_to_string = stmt_has_call(prog->stmts[i], "to_string", 9);

    if (needs_to_string) {
        sb_append(&b, "static const char* to_string(int val) {\n");
        sb_append(&b, "    static char buf[64];\n");
        sb_append(&b, "    snprintf(buf, sizeof(buf), \"%d\", val);\n");
        sb_append(&b, "    return buf;\n");
        sb_append(&b, "}\n");
        sb_append(&b, "\n");
    }

    /* Emit string helper functions if needed.
       Check if any statement or fn body uses string methods or concat. */
    int needs_string_helpers = 0;
    for (size_t i = 0; i < prog->count && !needs_string_helpers; ++i) {
        const stmt_t *s = prog->stmts[i];
        if (s->type == STMT_FN) {
            for (size_t j = 0; j < s->as.fn.body_count && !needs_string_helpers; ++j) {
                const stmt_t *bs = s->as.fn.body[j];
                if (bs->type == STMT_RETURN && bs->as.ret.value)
                    needs_string_helpers = expr_needs_helpers(bs->as.ret.value);
            }
        }
    }
    if (!needs_string_helpers) {
        for (size_t i = 0; i < prog->count && !needs_string_helpers; ++i) {
            const stmt_t *s = prog->stmts[i];
            switch (s->type) {
            case STMT_ASSIGN:
                if (s->as.assign.value)
                    needs_string_helpers = expr_needs_helpers(s->as.assign.value);
                break;
            case STMT_PRINT:
                for (size_t k = 0; k < s->as.print.value_count && !needs_string_helpers; ++k)
                    needs_string_helpers = expr_needs_helpers(s->as.print.values[k]);
                break;
            case STMT_RETURN:
                if (s->as.ret.value)
                    needs_string_helpers = expr_needs_helpers(s->as.ret.value);
                break;
            case STMT_IF:
                if (s->as.if_stmt.condition)
                    needs_string_helpers = expr_needs_helpers(s->as.if_stmt.condition);
                break;
            default: break;
            }
        }
    }

    if (needs_string_helpers) {
        sb_append(&b, "static const char* string_concat(const char *a, const char *b) {\n");
        sb_append(&b, "    static char buf[2048];\n");
        sb_append(&b, "    snprintf(buf, sizeof(buf), \"%s%s\", a, b);\n");
        sb_append(&b, "    return buf;\n");
        sb_append(&b, "}\n");
        sb_append(&b, "\n");
        sb_append(&b, "static const char* str_upper(const char *s) {\n");
        sb_append(&b, "    static char buf[2048];\n");
        sb_append(&b, "    size_t i;\n");
        sb_append(&b, "    for (i = 0; s[i] && i < sizeof(buf)-1; ++i)\n");
        sb_append(&b, "        buf[i] = (char)toupper((unsigned char)s[i]);\n");
        sb_append(&b, "    buf[i] = '\\0';\n");
        sb_append(&b, "    return buf;\n");
        sb_append(&b, "}\n");
        sb_append(&b, "\n");
        sb_append(&b, "static const char* str_lower(const char *s) {\n");
        sb_append(&b, "    static char buf[2048];\n");
        sb_append(&b, "    size_t i;\n");
        sb_append(&b, "    for (i = 0; s[i] && i < sizeof(buf)-1; ++i)\n");
        sb_append(&b, "        buf[i] = (char)tolower((unsigned char)s[i]);\n");
        sb_append(&b, "    buf[i] = '\\0';\n");
        sb_append(&b, "    return buf;\n");
        sb_append(&b, "}\n");
        sb_append(&b, "\n");
        sb_append(&b, "static const char* str_trim(const char *s) {\n");
        sb_append(&b, "    const char *start = s;\n");
        sb_append(&b, "    while (*start == ' ' || *start == '\\t' || *start == '\\n' || *start == '\\r')\n");
        sb_append(&b, "        start++;\n");
        sb_append(&b, "    if (*start == '\\0') return \"\";\n");
        sb_append(&b, "    const char *end = start + strlen(start) - 1;\n");
        sb_append(&b, "    while (end > start && (*end == ' ' || *end == '\\t' || *end == '\\n' || *end == '\\r'))\n");
        sb_append(&b, "        end--;\n");
        sb_append(&b, "    static char buf[2048];\n");
        sb_append(&b, "    size_t len = (size_t)(end - start + 1);\n");
        sb_append(&b, "    memcpy(buf, start, len); buf[len] = '\\0';\n");
        sb_append(&b, "    return buf;\n");
        sb_append(&b, "}\n");
        sb_append(&b, "\n");
        sb_append(&b, "static const char* str_split(const char *s, const char *delim) {\n");
        sb_append(&b, "    static char buf[2048];\n");
        sb_append(&b, "    static char copy[2048];\n");
        sb_append(&b, "    strncpy(copy, s, sizeof(copy)-1); copy[sizeof(copy)-1] = '\\0';\n");
        sb_append(&b, "    char *token = strtok(copy, delim);\n");
        sb_append(&b, "    buf[0] = '\\0';\n");
        sb_append(&b, "    while (token) {\n");
        sb_append(&b, "        if (buf[0] != '\\0') strncat(buf, \",\", sizeof(buf)-strlen(buf)-1);\n");
        sb_append(&b, "        strncat(buf, token, sizeof(buf)-strlen(buf)-1);\n");
        sb_append(&b, "        token = strtok(NULL, delim);\n");
        sb_append(&b, "    }\n");
        sb_append(&b, "    return buf;\n");
        sb_append(&b, "}\n");
        sb_append(&b, "\n");
        sb_append(&b, "static const char* str_replace(const char *s, const char *old, const char *new_str) {\n");
        sb_append(&b, "    static char buf[4096];\n");
        sb_append(&b, "    const char *p = s;\n");
        sb_append(&b, "    char *d = buf;\n");
        sb_append(&b, "    size_t old_len = strlen(old);\n");
        sb_append(&b, "    size_t new_len = strlen(new_str);\n");
        sb_append(&b, "    char *end = buf + sizeof(buf) - 1;\n");
        sb_append(&b, "    while (*p && d < end) {\n");
        sb_append(&b, "        const char *found = strstr(p, old);\n");
        sb_append(&b, "        if (!found || found > end) {\n");
        sb_append(&b, "            while (*p && d < end) *d++ = *p++;\n");
        sb_append(&b, "            break;\n");
        sb_append(&b, "        }\n");
        sb_append(&b, "        while (p < found && d < end) *d++ = *p++;\n");
        sb_append(&b, "        { size_t r = new_len; const char *src = new_str; while (r-- > 0 && d < end) *d++ = *src++; }\n");
        sb_append(&b, "        p += old_len;\n");
        sb_append(&b, "    }\n");
        sb_append(&b, "    *d = '\\0';\n");
        sb_append(&b, "    return buf;\n");
        sb_append(&b, "}\n");
        sb_append(&b, "\n");
        sb_append(&b, "#include <ctype.h>\n");
    }

    /* Emit struct type definitions */
    for (size_t i = 0; i < prog->count; ++i) {
        const stmt_t *s = prog->stmts[i];
        if (s->type != STMT_STRUCT) continue;

        sb_append(&b, "typedef struct {\n");
        for (size_t j = 0; j < s->as.struct_def.field_count; ++j) {
            sb_append(&b, "    int ");
            sb_append_n(&b, s->as.struct_def.fields[j].name_start,
                        s->as.struct_def.fields[j].name_length);
            sb_append(&b, ";\n");
        }
        sb_append(&b, "} ");
        sb_append_n(&b, s->as.struct_def.name_start, s->as.struct_def.name_length);
        sb_append(&b, ";\n\n");
    }

    /* Separate fn defs from body stmts */
    size_t   fn_cap = 0, fn_count = 0;
    stmt_t **fns     = NULL;
    size_t   body_cap = 0, body_count = 0;
    stmt_t **body_stmts = NULL;

    for (size_t i = 0; i < prog->count; ++i) {
        const stmt_t *s = prog->stmts[i];
        if (s->type == STMT_FN) {
            if (fn_count >= fn_cap) {
                size_t nc = fn_cap ? fn_cap * 2 : 8;
                stmt_t **t = (stmt_t **)realloc(fns, nc * sizeof(stmt_t *));
                if (!t) { free(b.data); free(fns); free(body_stmts); return result; }
                fns = t; fn_cap = nc;
            }
            fns[fn_count++] = (stmt_t *)s;
        } else if (s->type == STMT_WINDOW) {
            /* skip, handled in game mode */
        } else {
            if (body_count >= body_cap) {
                size_t nc = body_cap ? body_cap * 2 : 8;
                stmt_t **t = (stmt_t **)realloc(body_stmts, nc * sizeof(stmt_t *));
                if (!t) { free(b.data); free(fns); free(body_stmts); return result; }
                body_stmts = t; body_cap = nc;
            }
            body_stmts[body_count++] = (stmt_t *)s;
        }
    }

    /* ---- Emit function definitions (with deduplication) ---- */
    /* Track emitted function signatures to avoid duplicates from imports */
    size_t   emitted_cap = 0, emitted_count = 0;
    struct { const char *s; size_t l; size_t pc; } *emitted = NULL;

    for (size_t i = 0; i < fn_count; ++i) {
        const stmt_t *fn = fns[i];

        /* Check if this function was already emitted */
        int already = 0;
        for (size_t e = 0; e < emitted_count; ++e) {
            if (emitted[e].l == fn->as.fn.name_length &&
                emitted[e].pc == fn->as.fn.param_count &&
                strncmp(emitted[e].s, fn->as.fn.name_start, emitted[e].l) == 0) {
                already = 1;
                break;
            }
        }
        if (already) continue;

        /* Record as emitted */
        if (emitted_count >= emitted_cap) {
            size_t nc = emitted_cap ? emitted_cap * 2 : 8;
            void *t = realloc(emitted, nc * sizeof(*emitted));
            if (!t) { free(b.data); free(fns); free(body_stmts); free(emitted); return result; }
            emitted = t; emitted_cap = nc;
        }
        emitted[emitted_count].s  = fn->as.fn.name_start;
        emitted[emitted_count].l  = fn->as.fn.name_length;
        emitted[emitted_count].pc = fn->as.fn.param_count;
        emitted_count++;

        type_kind_t ret_type = TYPE_INT;
        for (size_t j = 0; j < fn->as.fn.body_count; ++j) {
            if (fn->as.fn.body[j]->type == STMT_RETURN &&
                fn->as.fn.body[j]->as.ret.value) {
                ret_type = infer_type(fn->as.fn.body[j]->as.ret.value);
                break;
            }
        }
        sb_append(&b, type_cname(ret_type));
        sb_append(&b, " ");
        sb_append_n(&b, fn->as.fn.name_start, fn->as.fn.name_length);
        sb_append(&b, "(");
        for (size_t j = 0; j < fn->as.fn.param_count; ++j) {
            if (j > 0) sb_append(&b, ", ");
            sb_append(&b, "int ");
            sb_append_n(&b, fn->as.fn.params[j].name_start,
                        fn->as.fn.params[j].name_length);
        }
        sb_append(&b, ") {\n");
        emit_block(&b, (const stmt_t *const *)fn->as.fn.body,
                   fn->as.fn.body_count, "    ");
        sb_append(&b, "}\n\n");
    }
    free(emitted);

    /* ---- Emit main() ---- */
    sb_append(&b, "int main(void) {\n");

    if (has_window) {
        /* ---- Game mode ---- */
        const stmt_t *window_stmt = NULL;
        for (size_t i = 0; i < prog->count; ++i) {
            if (prog->stmts[i]->type == STMT_WINDOW) {
                window_stmt = prog->stmts[i];
                break;
            }
        }

        size_t up_cap = 0, up_cnt = 0;
        stmt_t **up_stmts = NULL;
        size_t dr_cap = 0, dr_cnt = 0;
        stmt_t **dr_stmts = NULL;

        /* Collect update/draw */
        for (size_t i = 0; i < body_count; ++i) {
            const stmt_t *s = body_stmts[i];
            switch (s->type) {
            case STMT_IF:
                if (up_cnt >= up_cap) {
                    size_t nc = up_cap ? up_cap * 2 : 16;
                    stmt_t **t = (stmt_t **)realloc(up_stmts, nc * sizeof(stmt_t *));
                    if (!t) { free(b.data); free(fns); free(body_stmts); free(up_stmts); free(dr_stmts); return result; }
                    up_stmts = t; up_cap = nc;
                }
                up_stmts[up_cnt++] = (stmt_t *)s;
                break;
            case STMT_CALL:
                /* Audio calls play before drawing (update section) */
                if (s->as.call.name_length == 10 &&
                    (strncmp(s->as.call.name_start, "play_sound", 10) == 0 ||
                     strncmp(s->as.call.name_start, "play_music", 10) == 0)) {
                    if (up_cnt >= up_cap) {
                        size_t nc = up_cap ? up_cap * 2 : 16;
                        stmt_t **t = (stmt_t **)realloc(up_stmts, nc * sizeof(stmt_t *));
                        if (!t) { free(b.data); free(fns); free(body_stmts); free(up_stmts); free(dr_stmts); return result; }
                        up_stmts = t; up_cap = nc;
                    }
                    up_stmts[up_cnt++] = (stmt_t *)s;
                } else {
                    if (dr_cnt >= dr_cap) {
                        size_t nc = dr_cap ? dr_cap * 2 : 16;
                        stmt_t **t = (stmt_t **)realloc(dr_stmts, nc * sizeof(stmt_t *));
                        if (!t) { free(b.data); free(fns); free(body_stmts); free(up_stmts); free(dr_stmts); return result; }
                        dr_stmts = t; dr_cap = nc;
                    }
                    dr_stmts[dr_cnt++] = (stmt_t *)s;
                }
                break;
            case STMT_EXPR: {
                /* Parenthesized audio calls (play_sound(x)) */
                const expr_t *ex = s->as.expr_stmt.expr;
                if (ex && call_is_play_audio(ex)) {
                    if (up_cnt >= up_cap) {
                        size_t nc = up_cap ? up_cap * 2 : 16;
                        stmt_t **t = (stmt_t **)realloc(up_stmts, nc * sizeof(stmt_t *));
                        if (!t) { free(b.data); free(fns); free(body_stmts); free(up_stmts); free(dr_stmts); return result; }
                        up_stmts = t; up_cap = nc;
                    }
                    up_stmts[up_cnt++] = (stmt_t *)s;
                }
                break;
            }
            case STMT_EVENT:
                for (size_t j = 0; j < s->as.event.body_count; ++j) {
                    const stmt_t *body = s->as.event.body[j];
                    if (!body) continue;
                    if (body->type == STMT_IF || body->type == STMT_ASSIGN) {
                        if (up_cnt >= up_cap) {
                            size_t nc = up_cap ? up_cap * 2 : 16;
                            stmt_t **t = (stmt_t **)realloc(up_stmts, nc * sizeof(stmt_t *));
                            if (!t) { free(b.data); free(fns); free(body_stmts); free(up_stmts); free(dr_stmts); return result; }
                            up_stmts = t; up_cap = nc;
                        }
                        up_stmts[up_cnt++] = (stmt_t *)body;
                    } else if (body->type == STMT_CALL &&
                               body->as.call.name_length == 10 &&
                               (strncmp(body->as.call.name_start, "play_sound", 10) == 0 ||
                                strncmp(body->as.call.name_start, "play_music", 10) == 0)) {
                        if (up_cnt >= up_cap) {
                            size_t nc = up_cap ? up_cap * 2 : 16;
                            stmt_t **t = (stmt_t **)realloc(up_stmts, nc * sizeof(stmt_t *));
                            if (!t) { free(b.data); free(fns); free(body_stmts); free(up_stmts); free(dr_stmts); return result; }
                            up_stmts = t; up_cap = nc;
                        }
                        up_stmts[up_cnt++] = (stmt_t *)body;
                    } else if (body->type == STMT_CALL) {
                        if (dr_cnt >= dr_cap) {
                            size_t nc = dr_cap ? dr_cap * 2 : 16;
                            stmt_t **t = (stmt_t **)realloc(dr_stmts, nc * sizeof(stmt_t *));
                            if (!t) { free(b.data); free(fns); free(body_stmts); free(up_stmts); free(dr_stmts); return result; }
                            dr_stmts = t; dr_cap = nc;
                        }
                        dr_stmts[dr_cnt++] = (stmt_t *)body;
                    }
                }
                break;
            default: break;
            }
        }

        /* Window setup */
        sb_append(&b, "    InitWindow(");
        if (window_stmt) {
            emit_expr(&b, window_stmt->as.window.width);
            sb_append(&b, ", ");
            emit_expr(&b, window_stmt->as.window.height);
            sb_append(&b, ", ");
            emit_expr(&b, window_stmt->as.window.title);
        } else {
            sb_append(&b, "800, 600, \"MyLang App\"");
        }
        sb_append(&b, ");\n    SetTargetFPS(60);\n");
        if (uses_audio)
            sb_append(&b, "    InitAudioDevice();\n");
        sb_append(&b, "\n");

        /* Variable declarations from top-level assignments */
        size_t dc = 0, dl_cap = 0;
        struct { const char *s; size_t l; } *decls = NULL;
        for (size_t i = 0; i < body_count; ++i) {
            const stmt_t *s = body_stmts[i];
            if (s->type != STMT_ASSIGN) continue;
            /* load_sound() assignments become Sound declarations below */
            if (s->as.assign.value && expr_is_call(s->as.assign.value, "load_sound", 10))
                continue;
            bool seen = false;
            for (size_t j = 0; j < dc; ++j) {
                if (decls[j].l == s->as.assign.name_length &&
                    strncmp(decls[j].s, s->as.assign.name_start, decls[j].l) == 0) {
                    seen = true; break;
                }
            }
            if (seen) continue;
            if (dc >= dl_cap) {
                size_t nc = dl_cap ? dl_cap * 2 : 8;
                void *t = realloc(decls, nc * sizeof(*decls));
                if (!t) { free(b.data); free(fns); free(body_stmts); free(up_stmts); free(dr_stmts); return result; }
                decls = t; dl_cap = nc;
            }
            decls[dc].s = s->as.assign.name_start;
            decls[dc].l = s->as.assign.name_length;
            dc++;
            sb_append(&b, "    int ");
            sb_append_n(&b, s->as.assign.name_start, s->as.assign.name_length);
            sb_append(&b, " = ");
            emit_expr(&b, s->as.assign.value);
            sb_append(&b, ";\n");
        }
        sb_append(&b, "\n");

        /* Sprite/texture declarations (must come after InitWindow) */
        for (size_t i = 0; i < body_count; ++i) {
            const stmt_t *s = body_stmts[i];
            if (s->type != STMT_SPRITE) continue;
            sb_append(&b, "    Texture2D ");
            sb_append_n(&b, s->as.sprite.name_start, s->as.sprite.name_length);
            sb_append(&b, " = LoadTexture(");
            emit_expr(&b, s->as.sprite.path);
            sb_append(&b, ");\n");
        }

        /* Sound + music declarations (must come after InitAudioDevice) */
        for (size_t i = 0; i < body_count; ++i)
            emit_audio_decls(&b, body_stmts[i]);

        /* Game loop */
        sb_append(&b, "    while (!WindowShouldClose()) {\n");
        for (size_t i = 0; i < up_cnt; ++i) {
            const stmt_t *u = up_stmts[i];
            if (u->type == STMT_IF) {
                if (u->as.if_stmt.condition &&
                    u->as.if_stmt.condition->type == EXPR_IDENTIFIER) {
                    /* Old-style key check: emit_key_check produces full if() */
                    sb_append(&b, "        ");
                    emit_key_check(&b,
                        u->as.if_stmt.condition->as.identifier.start,
                        u->as.if_stmt.condition->as.identifier.length);
                    sb_append(&b, " {\n");
                    emit_block(&b, (const stmt_t *const *)u->as.if_stmt.then_body,
                               u->as.if_stmt.then_count, "            ");
                    sb_append(&b, "        }\n");
                } else {
                    /* New-style expression condition */
                    sb_append(&b, "        if (");
                    emit_expr(&b, u->as.if_stmt.condition);
                    sb_append(&b, ") {\n");
                    emit_block(&b, (const stmt_t *const *)u->as.if_stmt.then_body,
                               u->as.if_stmt.then_count, "            ");
                    sb_append(&b, "        }\n");
                }
            } else if (u->type == STMT_ASSIGN) {
                emit_assign(&b, u, "        ");
            } else if (u->type == STMT_CALL &&
                       u->as.call.name_length == 10 &&
                       (strncmp(u->as.call.name_start, "play_sound", 10) == 0 ||
                        strncmp(u->as.call.name_start, "play_music", 10) == 0)) {
                emit_play_audio(&b, u->as.call.name_start, u->as.call.name_length,
                                u->as.call.args, u->as.call.arg_count, "        ");
            } else if (u->type == STMT_EXPR && u->as.expr_stmt.expr &&
                       call_is_play_audio(u->as.expr_stmt.expr)) {
                const expr_t *ex = u->as.expr_stmt.expr;
                emit_play_audio(&b, ex->as.call.name_start, ex->as.call.name_length,
                                ex->as.call.args, ex->as.call.arg_count, "        ");
            }
        }
        if (up_cnt > 0) sb_append(&b, "\n");

        sb_append(&b, "        BeginDrawing();\n");
        sb_append(&b, "        ClearBackground(BLACK);\n");
        for (size_t i = 0; i < dr_cnt; ++i)
            emit_legacy_call(&b, dr_stmts[i], "        ");
        sb_append(&b, "        EndDrawing();\n");
        sb_append(&b, "    }\n\n");

        /* Unload textures before closing the window */
        for (size_t i = 0; i < body_count; ++i) {
            const stmt_t *s = body_stmts[i];
            if (s->type != STMT_SPRITE) continue;
            sb_append(&b, "    UnloadTexture(");
            sb_append_n(&b, s->as.sprite.name_start, s->as.sprite.name_length);
            sb_append(&b, ");\n");
        }

        /* Unload audio resources before closing the audio device */
        for (size_t i = 0; i < body_count; ++i)
            emit_audio_unloads(&b, body_stmts[i]);
        if (uses_audio)
            sb_append(&b, "    CloseAudioDevice();\n");

        sb_append(&b, "    CloseWindow();\n");
        sb_append(&b, "    return 0;\n");

        free(decls);
        free(up_stmts);
        free(dr_stmts);
    } else {
        /* ---- General-purpose mode ---- */
        /* Emit variable declarations with type inference */
        for (size_t i = 0; i < body_count; ++i) {
            const stmt_t *s = body_stmts[i];
            if (s->type != STMT_ASSIGN) continue;

            /* Check for duplicates */
            int seen = 0;
            for (size_t j = 0; j < i; ++j) {
                if (body_stmts[j]->type == STMT_ASSIGN &&
                    body_stmts[j]->as.assign.name_length == s->as.assign.name_length &&
                    strncmp(body_stmts[j]->as.assign.name_start,
                            s->as.assign.name_start,
                            s->as.assign.name_length) == 0) {
                    seen = 1; break;
                }
            }
            if (seen) continue;

            const expr_t *val = s->as.assign.value;

            /* Built-in: name = input("prompt") -> printf, char[], fgets */
            if (val && val->type == EXPR_CALL) {
                size_t ilen = 5;
                if (val->as.call.name_length == ilen &&
                    strncmp(val->as.call.name_start, "input", ilen) == 0) {
                    sb_append(&b, "    printf(");
                    if (val->as.call.arg_count > 0)
                        emit_expr(&b, val->as.call.args[0]);
                    sb_append(&b, ");\n");
                    sb_append(&b, "    char ");
                    sb_append_n(&b, s->as.assign.name_start,
                                s->as.assign.name_length);
                    sb_append(&b, "[256];\n");
                    sb_append(&b, "    fgets(");
                    sb_append_n(&b, s->as.assign.name_start,
                                s->as.assign.name_length);
                    sb_append(&b, ", sizeof(");
                    sb_append_n(&b, s->as.assign.name_start,
                                s->as.assign.name_length);
                    sb_append(&b, "), stdin);\n");
                    sb_append(&b, "    ");
                    sb_append_n(&b, s->as.assign.name_start,
                                s->as.assign.name_length);
                    sb_append(&b, "[strcspn(");
                    sb_append_n(&b, s->as.assign.name_start,
                                s->as.assign.name_length);
                    sb_append(&b, ", \"\\n\")] = '\\0';\n");
                    continue;
                }
            }

            /* Built-in: name = read_file("path") -> FILE *, fread, char[] */
            if (val && val->type == EXPR_CALL) {
                size_t rflen = 9;
                if (val->as.call.name_length == rflen &&
                    strncmp(val->as.call.name_start, "read_file", rflen) == 0) {
                    sb_append(&b, "    char ");
                    sb_append_n(&b, s->as.assign.name_start,
                                s->as.assign.name_length);
                    sb_append(&b, "[4096];\n");
                    sb_append(&b, "    { FILE *_f = fopen(");
                    if (val->as.call.arg_count > 0)
                        emit_expr(&b, val->as.call.args[0]);
                    sb_append(&b, ", \"r\"); ");
                    sb_append(&b, "if (_f) { size_t _n = fread(");
                    sb_append_n(&b, s->as.assign.name_start,
                                s->as.assign.name_length);
                    sb_append(&b, ", 1, sizeof(");
                    sb_append_n(&b, s->as.assign.name_start,
                                s->as.assign.name_length);
                    sb_append(&b, ") - 1, _f); ");
                    sb_append_n(&b, s->as.assign.name_start,
                                s->as.assign.name_length);
                    sb_append(&b, "[_n] = '\\0'; fclose(_f); } }\n");
                    continue;
                }
            }

            /* Struct instantiation: emit StructType var = (Type){...} */
            if (val && val->type == EXPR_CALL &&
                is_struct_type(val->as.call.name_start, val->as.call.name_length)) {
                sb_append(&b, "    ");
                sb_append_n(&b, val->as.call.name_start, val->as.call.name_length);
                sb_append(&b, " ");
                sb_append_n(&b, s->as.assign.name_start, s->as.assign.name_length);
                sb_append(&b, " = ");
                emit_expr(&b, val);
                sb_append(&b, ";\n");
                continue;
            }

            /* Dict literal: emit dict_t var = {0}; dict_set(...) */
            if (val && val->type == EXPR_DICT) {
                sb_append(&b, "    dict_t ");
                sb_append_n(&b, s->as.assign.name_start,
                            s->as.assign.name_length);
                sb_append(&b, " = {0};\n");
                for (size_t k = 0; k < val->as.dict.count; ++k) {
                    sb_append(&b, "    dict_set(&");
                    sb_append_n(&b, s->as.assign.name_start,
                                s->as.assign.name_length);
                    sb_append(&b, ", ");
                    emit_expr(&b, val->as.dict.keys[k]);
                    sb_append(&b, ", ");
                    emit_expr(&b, val->as.dict.values[k]);
                    sb_append(&b, ");\n");
                }
                continue;
            }

            type_kind_t t = infer_type(val);
            sb_append(&b, "    ");
            sb_append(&b, type_cname(t));
            sb_append(&b, " ");
            sb_append_n(&b, s->as.assign.name_start, s->as.assign.name_length);
            /* Array literal: insert [] so it reads 'int name[] = {...}' */
            if (val && val->type == EXPR_ARRAY)
                sb_append(&b, "[]");
            sb_append(&b, " = ");
            emit_expr(&b, val);
            sb_append(&b, ";\n");
        }
        if (body_count > 0) sb_append(&b, "\n");

        /* Emit body statements that are NOT assignments
         * (assignments were already emitted as declarations above). */
        for (size_t i = 0; i < body_count; ++i) {
            const stmt_t *s = body_stmts[i];
            if (s->type == STMT_ASSIGN) continue; /* skip, already declared */
            emit_stmt(&b, s, "    ");
        }
        sb_append(&b, "    return 0;\n");
    }

    sb_append(&b, "}\n");

    result.code = b.data;
    free(fns);
    free(body_stmts);
    clear_var_types();
    free(struct_types);
    struct_types = NULL; struct_types_cnt = 0; struct_types_cap = 0;
    return result;
}

/* ------------------------------------------------------------------ *
 * Explain-mode codegen (adds line-number annotations)                *
 * ------------------------------------------------------------------ */

codegen_result_t codegen_emit_explain(const program_t *prog,
                                       const char *filename)
{
    g_explain_mode    = 1;
    g_source_filename = filename;
    codegen_result_t r = codegen_emit(prog);
    g_explain_mode    = 0;
    g_source_filename = NULL;
    return r;
}
