#include "parser.h"
#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>  /* dup, dup2, close, STDERR_FILENO */

static int tests_run  = 0;
static int tests_pass = 0;
static int tests_fail = 0;

#define TEST(name)                                  \
    do {                                            \
        ++tests_run;                                \
        printf("  TEST: %-40s ", name);             \
        fflush(stdout);                             \
    } while (0)

#define PASS()                                      \
    do {                                            \
        ++tests_pass;                               \
        printf("PASS\n");                           \
    } while (0)

#define FAIL(msg)                                   \
    do {                                            \
        ++tests_fail;                               \
        printf("FAIL -- %s\n", msg);                \
    } while (0)

#define ASSERT_INT_EQ(actual, expected, field)          \
    do {                                                \
        if ((actual) != (expected)) {                   \
            char buf[256];                              \
            snprintf(buf, sizeof buf,                   \
                     "%s: got %d, expected %d",         \
                     (field), (int)(actual), (int)(expected)); \
            FAIL(buf);                                  \
            return;                                     \
        }                                               \
    } while (0)

#define ASSERT_SIZE_EQ(actual, expected, field)         \
    do {                                                \
        if ((actual) != (expected)) {                   \
            char buf[256];                              \
            snprintf(buf, sizeof buf,                   \
                     "%s: got %zu, expected %zu",       \
                     (field), (size_t)(actual), (size_t)(expected)); \
            FAIL(buf);                                  \
            return;                                     \
        }                                               \
    } while (0)

#define ASSERT_STR_EQ(s1, len, s2, field)               \
    do {                                                \
        size_t _len = (len);                            \
        if (_len != strlen(s2) ||                       \
            strncmp((s1), (s2), _len) != 0) {           \
            char buf[256];                              \
            snprintf(buf, sizeof buf,                   \
                     "%s: got \"%.*s\", expected \"%s\"", \
                     (field), (int)_len, (s1), (s2));   \
            FAIL(buf);                                  \
            return;                                     \
        }                                               \
    } while (0)

#define ASSERT_TRUE(cond, msg)                          \
    do {                                                \
        if (!(cond)) {                                  \
            FAIL(msg);                                  \
            return;                                     \
        }                                               \
    } while (0)

static program_t *parse_string(const char *source)
{
    lexer_t  lexer;
    parser_t parser;
    lexer_init(&lexer, source);
    parser_init(&parser, &lexer, ".");
    program_t *prog = parse_program(&parser);
    parser_free_imports(&parser);
    return prog;
}

static void test_empty_program(void)
{
    TEST("empty program");
    program_t *prog = parse_string("");
    ASSERT_SIZE_EQ(prog->count, 0UL, "stmt count");
    ast_destroy_program(prog);
    PASS();
}

static void test_window_stmt(void)
{
    TEST("window statement");
    program_t *prog = parse_string("window \"My Game\", 800, 600\n");
    ASSERT_SIZE_EQ(prog->count, 1UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_WINDOW, "type");
    stmt_t *w = prog->stmts[0];
    ASSERT_INT_EQ(w->as.window.title->type, EXPR_STRING, "title type");
    ASSERT_STR_EQ(w->as.window.title->as.string.start,
                  w->as.window.title->as.string.length,
                  "My Game", "title");
    ASSERT_INT_EQ(w->as.window.width->type, EXPR_NUMBER, "width type");
    ASSERT_TRUE(fabs(w->as.window.width->as.number - 800.0) < 0.001, "width");
    ast_destroy_program(prog);
    PASS();
}

static void test_fn_decl(void)
{
    TEST("fn declaration");
    program_t *prog = parse_string("fn add(a, b):\n    return a + b\n");
    ASSERT_SIZE_EQ(prog->count, 1UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_FN, "type");
    stmt_t *fn = prog->stmts[0];
    ASSERT_STR_EQ(fn->as.fn.name_start, fn->as.fn.name_length, "add", "name");
    ASSERT_SIZE_EQ(fn->as.fn.param_count, 2UL, "param count");
    ASSERT_STR_EQ(fn->as.fn.params[0].name_start, fn->as.fn.params[0].name_length, "a", "param[0]");
    ASSERT_STR_EQ(fn->as.fn.params[1].name_start, fn->as.fn.params[1].name_length, "b", "param[1]");
    ASSERT_SIZE_EQ(fn->as.fn.body_count, 1UL, "body count");
    ASSERT_INT_EQ(fn->as.fn.body[0]->type, STMT_RETURN, "body type");
    ast_destroy_program(prog);
    PASS();
}

static void test_print_stmt(void)
{
    TEST("print statement");
    program_t *prog = parse_string("print \"hello\"\nprint result\n");
    ASSERT_SIZE_EQ(prog->count, 2UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_PRINT, "stmt[0] type");
    ASSERT_SIZE_EQ(prog->stmts[0]->as.print.value_count, 1UL, "stmt[0] arg count");
    ASSERT_INT_EQ(prog->stmts[0]->as.print.values[0]->type, EXPR_STRING, "string print");
    ASSERT_INT_EQ(prog->stmts[1]->type, STMT_PRINT, "stmt[1] type");
    ASSERT_SIZE_EQ(prog->stmts[1]->as.print.value_count, 1UL, "stmt[1] arg count");
    ASSERT_INT_EQ(prog->stmts[1]->as.print.values[0]->type, EXPR_IDENTIFIER, "ident print");
    ast_destroy_program(prog);
    PASS();
}

static void test_print_multi_arg(void)
{
    TEST("print multi-arg");
    program_t *prog = parse_string("print \"Hello, \", name\n");
    ASSERT_SIZE_EQ(prog->count, 1UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_PRINT, "type");
    ASSERT_SIZE_EQ(prog->stmts[0]->as.print.value_count, 2UL, "two args");
    ASSERT_INT_EQ(prog->stmts[0]->as.print.values[0]->type, EXPR_STRING, "arg[0] string");
    ASSERT_INT_EQ(prog->stmts[0]->as.print.values[1]->type, EXPR_IDENTIFIER, "arg[1] ident");
    ast_destroy_program(prog);
    PASS();
}

static void test_var_assign(void)
{
    TEST("variable assignment");
    program_t *prog = parse_string("x = 400\nname = \"Kamal\"\n");
    ASSERT_SIZE_EQ(prog->count, 2UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_ASSIGN, "stmt[0] type");
    ASSERT_STR_EQ(prog->stmts[0]->as.assign.name_start, prog->stmts[0]->as.assign.name_length, "x", "var name");
    ASSERT_INT_EQ(prog->stmts[0]->as.assign.value->type, EXPR_NUMBER, "value type");
    ASSERT_INT_EQ(prog->stmts[1]->type, STMT_ASSIGN, "stmt[1] type");
    ASSERT_INT_EQ(prog->stmts[1]->as.assign.value->type, EXPR_STRING, "string value");
    ast_destroy_program(prog);
    PASS();
}

static void test_if_else(void)
{
    TEST("if/else");
    const char *src =
        "if result > 25:\n"
        "    print \"big\"\n"
        "else:\n"
        "    print \"small\"\n";
    program_t *prog = parse_string(src);
    ASSERT_SIZE_EQ(prog->count, 1UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_IF, "type");
    stmt_t *s = prog->stmts[0];
    ASSERT_INT_EQ(s->as.if_stmt.condition->type, EXPR_COMPARISON, "condition type");
    ASSERT_SIZE_EQ(s->as.if_stmt.then_count, 1UL, "then count");
    ASSERT_INT_EQ(s->as.if_stmt.then_body[0]->type, STMT_PRINT, "then body type");
    ASSERT_TRUE(s->as.if_stmt.else_branch != NULL, "has else");
    ASSERT_INT_EQ(s->as.if_stmt.else_branch->as.if_stmt.then_body[0]->type, STMT_PRINT, "else body type");
    ast_destroy_program(prog);
    PASS();
}

static void test_while_loop(void)
{
    TEST("while loop");
    program_t *prog = parse_string("while x < 10:\n    x = x + 1\n");
    ASSERT_SIZE_EQ(prog->count, 1UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_WHILE, "type");
    stmt_t *w = prog->stmts[0];
    ASSERT_INT_EQ(w->as.while_stmt.condition->type, EXPR_COMPARISON, "condition");
    ASSERT_SIZE_EQ(w->as.while_stmt.body_count, 1UL, "body count");
    ASSERT_INT_EQ(w->as.while_stmt.body[0]->type, STMT_ASSIGN, "body stmt type");
    ast_destroy_program(prog);
    PASS();
}

static void test_for_loop(void)
{
    TEST("for loop");
    program_t *prog = parse_string("for (i = 0; i < 10; i = i + 1):\n    print i\n");
    ASSERT_SIZE_EQ(prog->count, 1UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_FOR, "type");
    stmt_t *f = prog->stmts[0];
    ASSERT_TRUE(f->as.for_stmt.init != NULL, "has init");
    ASSERT_INT_EQ(f->as.for_stmt.init->type, STMT_ASSIGN, "init type");
    ASSERT_TRUE(f->as.for_stmt.condition != NULL, "has condition");
    ASSERT_INT_EQ(f->as.for_stmt.condition->type, EXPR_COMPARISON, "condition type");
    ASSERT_TRUE(f->as.for_stmt.inc != NULL, "has inc");
    ASSERT_SIZE_EQ(f->as.for_stmt.body_count, 1UL, "body count");
    ast_destroy_program(prog);
    PASS();
}

static void test_fn_call_expr(void)
{
    TEST("fn call expression");
    program_t *prog = parse_string("result = add(x, y)\nprint result\n");
    ASSERT_SIZE_EQ(prog->count, 2UL, "stmt count");
    stmt_t *a = prog->stmts[0];
    ASSERT_INT_EQ(a->type, STMT_ASSIGN, "assign type");
    ASSERT_INT_EQ(a->as.assign.value->type, EXPR_CALL, "value is call expr");
    ASSERT_STR_EQ(a->as.assign.value->as.call.name_start,
                  a->as.assign.value->as.call.name_length,
                  "add", "call name");
    ASSERT_SIZE_EQ(a->as.assign.value->as.call.arg_count, 2UL, "arg count");
    ast_destroy_program(prog);
    PASS();
}

static void test_legacy_call(void)
{
    TEST("legacy space-separated call");
    program_t *prog = parse_string("circle 400, 300, 50, \"red\"\n");
    ASSERT_SIZE_EQ(prog->count, 1UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_CALL, "legacy call type");
    ast_destroy_program(prog);
    PASS();
}

static void test_event_block(void)
{
    TEST("event block");
    program_t *prog = parse_string("on update:\n    draw_circle 400, 300, 50, \"red\"\n");
    ASSERT_SIZE_EQ(prog->count, 1UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_EVENT, "type");
    ASSERT_SIZE_EQ(prog->stmts[0]->as.event.body_count, 1UL, "body count");
    ASSERT_INT_EQ(prog->stmts[0]->as.event.body[0]->type, STMT_CALL, "body call type");
    ast_destroy_program(prog);
    PASS();
}

static void test_sprite_stmt(void)
{
    TEST("sprite statement");
    program_t *prog = parse_string("sprite player = \"player.png\"\n");
    ASSERT_SIZE_EQ(prog->count, 1UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_SPRITE, "type");
    stmt_t *sp = prog->stmts[0];
    ASSERT_STR_EQ(sp->as.sprite.name_start, sp->as.sprite.name_length,
                  "player", "name");
    ASSERT_TRUE(sp->as.sprite.path != NULL, "has path");
    ASSERT_INT_EQ(sp->as.sprite.path->type, EXPR_STRING, "path type");
    ASSERT_STR_EQ(sp->as.sprite.path->as.string.start,
                  sp->as.sprite.path->as.string.length,
                  "player.png", "path");
    ast_destroy_program(prog);
    PASS();
}

static void test_if_key_condition(void)
{
    TEST("if with key identifier");
    /* A bare key name used as a condition: if key_right: ... */
    program_t *prog = parse_string("if key_right:\n    x = x + 1\n");
    ASSERT_SIZE_EQ(prog->count, 1UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_IF, "type");
    stmt_t *s = prog->stmts[0];
    ASSERT_INT_EQ(s->as.if_stmt.condition->type, EXPR_IDENTIFIER, "key as ident");
    ASSERT_STR_EQ(s->as.if_stmt.condition->as.identifier.start,
                  s->as.if_stmt.condition->as.identifier.length,
                  "key_right", "key name");
    ast_destroy_program(prog);
    PASS();
}

static void test_dict_literal(void)
{
    TEST("dict literal");
    program_t *prog = parse_string("scores = {\"alice\": 95, \"bob\": 87}\n");
    ASSERT_SIZE_EQ(prog->count, 1UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_ASSIGN, "type");
    stmt_t *a = prog->stmts[0];
    ASSERT_STR_EQ(a->as.assign.name_start, a->as.assign.name_length, "scores", "var name");
    ASSERT_INT_EQ(a->as.assign.value->type, EXPR_DICT, "value type");
    ASSERT_SIZE_EQ(a->as.assign.value->as.dict.count, 2UL, "dict entry count");
    /* First key: "alice" */
    ASSERT_INT_EQ(a->as.assign.value->as.dict.keys[0]->type, EXPR_STRING, "key[0] type");
    ASSERT_STR_EQ(a->as.assign.value->as.dict.keys[0]->as.string.start,
                  a->as.assign.value->as.dict.keys[0]->as.string.length,
                  "alice", "key[0]");
    /* First value: 95 */
    ASSERT_INT_EQ(a->as.assign.value->as.dict.values[0]->type, EXPR_NUMBER, "val[0] type");
    /* Second key: "bob" */
    ASSERT_INT_EQ(a->as.assign.value->as.dict.keys[1]->type, EXPR_STRING, "key[1] type");
    ASSERT_STR_EQ(a->as.assign.value->as.dict.keys[1]->as.string.start,
                  a->as.assign.value->as.dict.keys[1]->as.string.length,
                  "bob", "key[1]");
    ast_destroy_program(prog);
    PASS();
}

static void test_return_stmt(void)
{
    TEST("return statement");
    program_t *prog = parse_string("return 42\nreturn\n");
    ASSERT_SIZE_EQ(prog->count, 2UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_RETURN, "stmt[0]");
    ASSERT_TRUE(prog->stmts[0]->as.ret.value != NULL, "has value");
    ASSERT_INT_EQ(prog->stmts[1]->type, STMT_RETURN, "stmt[1]");
    ASSERT_TRUE(prog->stmts[1]->as.ret.value == NULL, "no value");
    ast_destroy_program(prog);
    PASS();
}

static void test_struct_def(void)
{
    TEST("struct definition");
    program_t *prog = parse_string("struct Point:\n    x\n    y\n");
    ASSERT_SIZE_EQ(prog->count, 1UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_STRUCT, "type");
    stmt_t *s = prog->stmts[0];
    ASSERT_STR_EQ(s->as.struct_def.name_start, s->as.struct_def.name_length, "Point", "name");
    ASSERT_SIZE_EQ(s->as.struct_def.field_count, 2UL, "field count");
    ASSERT_STR_EQ(s->as.struct_def.fields[0].name_start, s->as.struct_def.fields[0].name_length, "x", "field[0]");
    ASSERT_STR_EQ(s->as.struct_def.fields[1].name_start, s->as.struct_def.fields[1].name_length, "y", "field[1]");
    ast_destroy_program(prog);
    PASS();
}

static void test_dot_method(void)
{
    TEST("dot method");
    program_t *prog = parse_string("print name.len\nprint name.upper()\n");
    ASSERT_SIZE_EQ(prog->count, 2UL, "stmt count");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_PRINT, "stmt[0] type");
    expr_t *e0 = prog->stmts[0]->as.print.values[0];
    ASSERT_INT_EQ(e0->type, EXPR_FIELD, "e0 type");
    ASSERT_INT_EQ(e0->as.field.object->type, EXPR_IDENTIFIER, "e0 object");
    ASSERT_STR_EQ(e0->as.field.object->as.identifier.start,
                  e0->as.field.object->as.identifier.length,
                  "name", "e0 obj name");
    ASSERT_STR_EQ(e0->as.field.field_name_start, e0->as.field.field_name_length,
                  "len", "e0 field");
    expr_t *e1 = prog->stmts[1]->as.print.values[0];
    ASSERT_INT_EQ(e1->type, EXPR_METHOD, "e1 type");
    ASSERT_STR_EQ(e1->as.method.method_name_start, e1->as.method.method_name_length,
                  "upper", "e1 method");
    ast_destroy_program(prog);
    PASS();
}

/* Error recovery: parse a file with 5 errors and verify every one is
   reported in a single pass (not just the first), and that parsing
   resumes after the errors.  stderr is captured so we can count the
   "Error [line:col]" reports. */
static void test_multiple_errors_reported(void)
{
    TEST("multiple errors reported");
    const char *src =
        "x = 5 +\n"           /* error 1: missing right operand          */
        "y =\n"               /* error 2: missing value                  */        "fn broken(:\n"      /* error 3: missing parameter name           */
        "    print\n"         /* error 4: print with no value                  */
        "z = * 3\n"           /* error 5: invalid expression start       */
        "w = 7\n";            /* valid statement after the errors        */

    /* Capture stderr so the test run output stays clean. */
    fflush(stderr);
    int saved = dup(STDERR_FILENO);
    FILE *cap = tmpfile();
    dup2(fileno(cap), STDERR_FILENO);

    lexer_t  lexer;
    parser_t parser;
    lexer_init(&lexer, src);
    parser_init(&parser, &lexer, ".");
    program_t *prog = parse_program(&parser);

    /* Restore stderr and read the captured error output. */
    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    rewind(cap);
    char buf[16384];
    size_t n = fread(buf, 1, sizeof(buf) - 1, cap);
    buf[n] = '\0';
    fclose(cap);

    /* Count how many errors were reported. */
    int count = 0;
    for (const char *q = buf; (q = strstr(q, "Error [")) != NULL; q += 7)
        count++;

    ASSERT_TRUE(parser.had_error, "had_error flag set");
    ASSERT_TRUE(count >= 5, "all errors reported in one pass (not just the first)");
    /* The 5 errors are on lines 1,2,3,4,5 of the inline source. */
    ASSERT_TRUE(strstr(buf, "Error [1:") != NULL, "error reported on line 1");
    ASSERT_TRUE(strstr(buf, "Error [2:") != NULL, "error reported on line 2");
    ASSERT_TRUE(strstr(buf, "Error [3:") != NULL, "error reported on line 3");
    ASSERT_TRUE(strstr(buf, "Error [4:") != NULL, "error reported on line 4");
    ASSERT_TRUE(strstr(buf, "Error [5:") != NULL, "error reported on line 5");

    /* Recovery: the valid statement after all errors must still parse. */
    int found_w = 0;
    for (size_t i = 0; i < prog->count; ++i) {
        if (prog->stmts[i]->type == STMT_ASSIGN &&
            prog->stmts[i]->as.assign.name_length == 1 &&
            strncmp(prog->stmts[i]->as.assign.name_start, "w", 1) == 0)
            found_w = 1;
    }
    ASSERT_TRUE(found_w, "parser recovered and parsed 'w = 7' after errors");

    parser_free_imports(&parser);
    ast_destroy_program(prog);
    PASS();
}

/* Simulate a package installed in ~/.mylang/packages, parse an import,
   and verify the imported functions are merged into the program. */
static void test_import_from_packages(void)
{
    TEST("import from packages");
    const char *old_home = getenv("HOME");
    char tmp[256], cmd[512], path[512];
    snprintf(tmp, sizeof tmp, "/tmp/mylang_pkg_%ld", (long)getpid());
    snprintf(cmd, sizeof cmd, "mkdir -p %s/.mylang/packages/std", tmp);
    system(cmd);

    snprintf(path, sizeof path, "%s/.mylang/packages/std/math.vibes", tmp);
    FILE *fp = fopen(path, "w");
    if (fp) {
        fputs("fn distance(x1, y1, x2, y2):\n"
              "    return sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1))\n"
              "fn lerp(a, b, t):\n"
              "    return a + (b - a) * t\n", fp);
        fclose(fp);
    }

    setenv("HOME", tmp, 1);
    program_t *prog = parse_string("import \"std/math\"\n"
                                   "print distance(1, 2, 4, 6)\n");
    if (old_home) setenv("HOME", old_home, 1);
    else unsetenv("HOME");

    /* 2 imported fn stmts + 1 print = 3 */
    ASSERT_TRUE(prog != NULL, "prog non-null");
    ASSERT_SIZE_EQ(prog->count, 3UL, "stmt count (2 fns + print)");
    ASSERT_INT_EQ(prog->stmts[0]->type, STMT_FN, "first stmt is fn");
    ast_destroy_program(prog);

    snprintf(cmd, sizeof cmd, "rm -rf %s", tmp);
    system(cmd);
    PASS();
}

int main(void)
{
    printf("=== MyLang Parser Test Suite ===\n\n");
    test_empty_program();
    test_window_stmt();
    test_fn_decl();
    test_print_stmt();
    test_print_multi_arg();
    test_var_assign();
    test_if_else();
    test_while_loop();
    test_for_loop();
    test_fn_call_expr();
    test_legacy_call();
    test_event_block();
    test_sprite_stmt();
    test_if_key_condition();
    test_return_stmt();
    test_dict_literal();
    test_dot_method();
    test_struct_def();
    test_multiple_errors_reported();
    test_import_from_packages();
    printf("\n--- Results: %d/%d passed, %d failed ---\n",
           tests_pass, tests_run, tests_fail);
    return tests_fail > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
