#include "codegen.h"
#include "parser.h"
#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* getpid, setenv */

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

#define ASSERT_NOT_NULL(ptr, msg)                   \
    do {                                            \
        if ((ptr) == NULL) {                        \
            FAIL(msg);                              \
            return;                                 \
        }                                           \
    } while (0)

#define ASSERT_STR_CONTAINS(haystack, needle, msg)  \
    do {                                            \
        if (strstr((haystack), (needle)) == NULL) { \
            char buf[512];                          \
            snprintf(buf, sizeof buf,               \
                     "%s: expected to find \"%s\"", \
                     (msg), (needle));              \
            FAIL(buf);                              \
            return;                                 \
        }                                           \
    } while (0)

#define ASSERT_TRUE(cond, msg)                      \
    do {                                            \
        if (!(cond)) {                              \
            FAIL(msg);                              \
            return;                                 \
        }                                           \
    } while (0)

static char *parse_and_codegen(const char *source)
{
    lexer_t   lexer;
    parser_t  parser;
    lexer_init(&lexer, source);
    parser_init(&parser, &lexer, ".");
    program_t *prog = parse_program(&parser);
    parser_free_imports(&parser);
    if (parser.had_error) { ast_destroy_program(prog); return NULL; }
    codegen_result_t cr = codegen_emit(prog);
    ast_destroy_program(prog);
    return cr.code;
}

/* --- General-purpose mode tests --- */

static void test_gen_empty(void)
{
    TEST("gen: empty program");
    char *out = parse_and_codegen("");
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "#include <stdio.h>", "stdio include");
    ASSERT_STR_CONTAINS(out, "int main(void)", "main signature");
    ASSERT_STR_CONTAINS(out, "return 0;", "return");
    PASS();
    free(out);
}

static void test_gen_fn_print(void)
{
    TEST("gen: fn + print");
    const char *src =
        "fn add(a, b):\n"
        "    return a + b\n"
        "x = 10\n"
        "y = 20\n"
        "result = add(x, y)\n"
        "print result\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "int add(", "fn as int add(");
    ASSERT_STR_CONTAINS(out, "int x = 10;", "int x");
    ASSERT_STR_CONTAINS(out, "int y = 20;", "int y");
    ASSERT_STR_CONTAINS(out, "add(x, y)", "call expr");
    ASSERT_STR_CONTAINS(out, "printf(\"%d", "printf with format");
    PASS();
    free(out);
}

static void test_gen_print_string(void)
{
    TEST("gen: print string");
    char *out = parse_and_codegen("print \"hello world\"\n");
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "printf(\"%s\\n\", \"hello world\")", "string print");
    PASS();
    free(out);
}

static void test_gen_print_multi(void)
{
    TEST("gen: print multi");
    char *out = parse_and_codegen("print \"Hello, \", name\n");
    ASSERT_NOT_NULL(out, "output non-null");
    /* name is undeclared -> infer_type defaults to TYPE_INT -> %d */
    ASSERT_STR_CONTAINS(out, "printf(\"%s%d\\n\", \"Hello, \", name)", "multi-arg print");
    PASS();
    free(out);
}

static void test_gen_print_multi_with_decl(void)
{
    TEST("gen: print multi with decl");
    char *out = parse_and_codegen(
        "name = \"Kamal\"\n"
        "print \"Hello, \", name\n");
    ASSERT_NOT_NULL(out, "output non-null");
    /* name is declared as string -> infer_type returns TYPE_STRING -> %s */
    ASSERT_STR_CONTAINS(out, "printf(\"%s%s\\n\", \"Hello, \", name)", "multi-arg with decl");
    PASS();
    free(out);
}

static void test_gen_var_types(void)
{
    TEST("gen: var types");
    const char *src =
        "name = \"Kamal\"\n"
        "x = 10\n"
        "y = 3.14\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "const char* name", "string var");
    ASSERT_STR_CONTAINS(out, "int x", "int var");
    ASSERT_STR_CONTAINS(out, "double y", "double var");
    PASS();
    free(out);
}

static void test_gen_if_else(void)
{
    TEST("gen: if/else");
    const char *src =
        "if x > 5:\n"
        "    print \"big\"\n"
        "else:\n"
        "    print \"small\"\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "if ((x > 5))", "if condition");
    ASSERT_STR_CONTAINS(out, "} else {", "else block");
    PASS();
    free(out);
}

/* --- Game-mode tests (backward compat) --- */

static void test_game_window(void)
{
    TEST("game: window");
    char *out = parse_and_codegen("window \"My Game\", 800, 600\n");
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "#include \"raylib.h\"", "raylib include");
    ASSERT_STR_CONTAINS(out, "InitWindow(800, 600", "InitWindow");
    ASSERT_STR_CONTAINS(out, "SetTargetFPS(60)", "SetTargetFPS");
    PASS();
    free(out);
}

static void test_game_draw_circle(void)
{
    TEST("game: draw_circle");
    char *out = parse_and_codegen("draw_circle 400, 300, 50, \"red\"\n");
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "DrawCircle(400, 300, 50, RED)", "DrawCircle");
    PASS();
    free(out);
}

static void test_game_circle_alias(void)
{
    TEST("game: circle alias");
    char *out = parse_and_codegen("circle 200, 100, 25, \"blue\"\n");
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "DrawCircle(200, 100, 25, BLUE)", "circle alias");
    PASS();
    free(out);
}

static void test_game_event(void)
{
    TEST("game: event block");
    const char *src =
        "window \"Test\", 640, 480\n"
        "on update:\n"
        "    draw_circle 100, 200, 25, \"blue\"\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "InitWindow(640, 480, \"Test\")", "InitWindow");
    ASSERT_STR_CONTAINS(out, "while (!WindowShouldClose())", "game loop");
    ASSERT_STR_CONTAINS(out, "DrawCircle(100, 200, 25, BLUE)", "draw in loop");
    ASSERT_STR_CONTAINS(out, "BeginDrawing()", "BeginDrawing");
    ASSERT_STR_CONTAINS(out, "EndDrawing()", "EndDrawing");
    ASSERT_STR_CONTAINS(out, "CloseWindow()", "CloseWindow");
    PASS();
    free(out);
}

static void test_game_vars_and_keys(void)
{
    TEST("game: vars and keys");
    const char *src =
        "window \"G\", 800, 600\n"
        "x = 400\n"
        "y = 300\n"
        "on update:\n"
        "    if key_right:\n"
        "        x = x + 5\n"
        "    circle x, y, 30, \"red\"\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "int x = 400;", "int x");
    ASSERT_STR_CONTAINS(out, "if (IsKeyDown(KEY_RIGHT))", "key check");
    ASSERT_STR_CONTAINS(out, "DrawCircle(x, y, 30, RED)", "draw with vars");
    PASS();
    free(out);
}

/* --- New feature tests (arrays, input, write_file) --- */

static void test_game_sprite(void)
{
    TEST("game: sprite");
    const char *src =
        "window \"Sprite Demo\", 800, 600\n"
        "sprite player = \"player.png\"\n"
        "x = 400\n"
        "y = 300\n"
        "if key_right:\n"
        "    x = x + 5\n"
        "draw_sprite player, x, y\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "#include \"raylib.h\"", "raylib include");
    ASSERT_STR_CONTAINS(out, "Texture2D player = LoadTexture(\"player.png\");",
                        "texture declaration");
    ASSERT_STR_CONTAINS(out, "DrawTexture(player, x, y, WHITE);", "DrawTexture");
    ASSERT_STR_CONTAINS(out, "UnloadTexture(player);", "UnloadTexture");
    ASSERT_STR_CONTAINS(out, "while (!WindowShouldClose())", "game loop");
    PASS();
    free(out);
}

static void test_game_sound(void)
{
    TEST("game: sound");
    const char *src =
        "window \"Sound Demo\", 800, 600\n"
        "jump = load_sound(\"jump.wav\")\n"
        "play_music \"bgm.ogg\"\n"
        "x = 400\n"
        "if key_right:\n"
        "    x = x + 5\n"
        "play_sound jump\n"
        "draw_circle x, 300, 30, \"red\"\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "#include \"raylib.h\"", "raylib include");
    ASSERT_STR_CONTAINS(out, "InitAudioDevice();", "audio device init");
    ASSERT_STR_CONTAINS(out, "Sound jump = LoadSound(\"jump.wav\");", "load sound");
    ASSERT_STR_CONTAINS(out, "Music bgm = LoadMusicStream(\"bgm.ogg\");", "load music");
    ASSERT_STR_CONTAINS(out, "PlayMusicStream(bgm);", "play music");
    ASSERT_STR_CONTAINS(out, "PlaySound(jump);", "play sound in loop");
    ASSERT_STR_CONTAINS(out, "UpdateMusicStream(bgm);", "update music in loop");
    ASSERT_STR_CONTAINS(out, "UnloadSound(jump);", "unload sound");
    ASSERT_STR_CONTAINS(out, "UnloadMusicStream(bgm);", "unload music");
    ASSERT_STR_CONTAINS(out, "CloseAudioDevice();", "close audio device");
    /* play_sound must come before BeginDrawing() */
    size_t play_pos = 0, begin_pos = 0;
    char *play = strstr(out, "PlaySound(jump);");
    char *begin = strstr(out, "BeginDrawing();");
    if (play) play_pos = (size_t)(play - out);
    if (begin) begin_pos = (size_t)(begin - out);
    ASSERT_TRUE(play_pos > 0 && begin_pos > 0 && play_pos < begin_pos,
                "PlaySound emitted before BeginDrawing");
    PASS();
    free(out);
}

static void test_game_sound_parens(void)
{
    TEST("game: sound (parens)");
    /* The prompt's literal syntax: parenthesized calls. */
    const char *src =
        "window \"Sound Demo\", 800, 600\n"
        "jump = load_sound(\"jump.wav\")\n"
        "play_music(\"bgm.ogg\")\n"
        "x = 400\n"
        "play_sound(jump)\n"
        "draw_circle x, 300, 30, \"red\"\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "InitAudioDevice();", "audio device init");
    ASSERT_STR_CONTAINS(out, "Sound jump = LoadSound(\"jump.wav\");", "load sound");
    ASSERT_STR_CONTAINS(out, "Music bgm = LoadMusicStream(\"bgm.ogg\");", "load music");
    ASSERT_STR_CONTAINS(out, "PlaySound(jump);", "play sound in loop");
    ASSERT_STR_CONTAINS(out, "UpdateMusicStream(bgm);", "update music in loop");
    /* parenthesized play_sound must still be before BeginDrawing() */
    size_t play_pos = 0, begin_pos = 0;
    char *play = strstr(out, "PlaySound(jump);");
    char *begin = strstr(out, "BeginDrawing();");
    if (play) play_pos = (size_t)(play - out);
    if (begin) begin_pos = (size_t)(begin - out);
    ASSERT_TRUE(play_pos > 0 && begin_pos > 0 && play_pos < begin_pos,
                "PlaySound emitted before BeginDrawing");
    PASS();
    free(out);
}

static void test_game_collision(void)
{
    TEST("game: collision");
    const char *src =
        "window \"Collision Demo\", 800, 600\n"
        "x = 400\n"
        "y = 300\n"
        "if collide_circle(x, y, 30, 200, 200, 30):\n"
        "    x = 100\n"
        "if collide_rect(0, 0, 50, 50, 60, 60, 40, 40):\n"
        "    y = 100\n"
        "if collide_point(x, y, 100, 100, 200, 200):\n"
        "    x = 50\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "#include \"raylib.h\"", "raylib include");
    ASSERT_STR_CONTAINS(out,
        "if (CheckCollisionCircles((Vector2){x, y}, 30, (Vector2){200, 200}, 30))",
        "circle collision");
    ASSERT_STR_CONTAINS(out,
        "if (CheckCollisionRecs((Rectangle){0, 0, 50, 50}, (Rectangle){60, 60, 40, 40}))",
        "rect collision");
    ASSERT_STR_CONTAINS(out,
        "if (CheckCollisionPointRec((Vector2){x, y}, (Rectangle){100, 100, 200, 200}))",
        "point collision");
    /* Collision checks run in the update section, before drawing */
    size_t col_pos = 0, begin_pos = 0;
    char *col = strstr(out, "CheckCollisionCircles");
    char *begin = strstr(out, "BeginDrawing();");
    if (col) col_pos = (size_t)(col - out);
    if (begin) begin_pos = (size_t)(begin - out);
    ASSERT_TRUE(col_pos > 0 && begin_pos > 0 && col_pos < begin_pos,
                "collision check emitted before BeginDrawing");
    PASS();
    free(out);
}

static void test_game_mouse_keys(void)
{
    TEST("game: mouse keys");
    const char *src =
        "window \"Mouse Demo\", 800, 600\n"
        "x = 400\n"
        "y = 300\n"
        "if key_space:\n"
        "    x = 100\n"
        "if key_enter:\n"
        "    y = 100\n"
        "if key_w:\n"
        "    y = y - 5\n"
        "if key_a:\n"
        "    x = x - 5\n"
        "if key_s:\n"
        "    y = y + 5\n"
        "if key_d:\n"
        "    x = x + 5\n"
        "if mouse_down:\n"
        "    x = mouse_x\n"
        "if mouse_click:\n"
        "    y = mouse_y\n"
        "circle mouse_x, mouse_y, 30, \"red\"\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "if (IsKeyDown(KEY_SPACE))", "key_space");
    ASSERT_STR_CONTAINS(out, "if (IsKeyDown(KEY_ENTER))", "key_enter");
    ASSERT_STR_CONTAINS(out, "if (IsKeyDown(KEY_W))", "key_w");
    ASSERT_STR_CONTAINS(out, "if (IsKeyDown(KEY_A))", "key_a");
    ASSERT_STR_CONTAINS(out, "if (IsKeyDown(KEY_S))", "key_s");
    ASSERT_STR_CONTAINS(out, "if (IsKeyDown(KEY_D))", "key_d");
    ASSERT_STR_CONTAINS(out,
        "if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))", "mouse_down");
    ASSERT_STR_CONTAINS(out,
        "if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))", "mouse_click");
    ASSERT_STR_CONTAINS(out, "DrawCircle(GetMouseX(), GetMouseY(), 30, RED)",
        "mouse coords in draw");
    PASS();
    free(out);
}

static void test_game_draw_rect(void)
{
    TEST("game: draw_rect");
    const char *src =
        "window \"Rect Demo\", 800, 600\n"
        "draw_rect 20, 30, 100, 50, \"red\"\n"
        "draw_rect pad_x, pad_y, 60, 40, \"blue\"\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "DrawRectangle(20, 30, 100, 50, RED)", "draw_rect literals");
    ASSERT_STR_CONTAINS(out, "DrawRectangle(pad_x, pad_y, 60, 40, BLUE)", "draw_rect vars");
    PASS();
    free(out);
}

static void test_gen_array_literal(void)
{
    TEST("gen: array literal");
    char *out = parse_and_codegen("numbers = [10, 20, 30]\n");
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "int numbers[] = {10, 20, 30};", "array decl");
    PASS();
    free(out);
}

static void test_gen_array_index(void)
{
    TEST("gen: array index");
    const char *src =
        "numbers = [10, 20, 30]\n"
        "print numbers[0]\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "int numbers[] = {10, 20, 30};", "array decl");
    ASSERT_STR_CONTAINS(out, "printf(\"%d\\n\", numbers[0])", "index access");
    PASS();
    free(out);
}

static void test_gen_input_builtin(void)
{
    TEST("gen: input");
    char *out = parse_and_codegen("name = input(\"Enter name: \")\n");
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "printf(\"Enter name: \");", "input prompt");
    ASSERT_STR_CONTAINS(out, "char name[256];", "input buffer");
    ASSERT_STR_CONTAINS(out, "fgets(name, sizeof(name), stdin);", "fgets");
    ASSERT_STR_CONTAINS(out, "strcspn(name, \"\\n\")", "newline strip");
    PASS();
    free(out);
}

static void test_gen_write_file(void)
{
    TEST("gen: write_file");
    char *out = parse_and_codegen(
        "write_file(\"output.txt\", \"hello\")\n");
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "FILE *_f = fopen(\"output.txt\", \"w\")", "fopen");
    ASSERT_STR_CONTAINS(out, "fputs(\"hello\", _f)", "fputs");
    ASSERT_STR_CONTAINS(out, "fclose(_f)", "fclose");
    PASS();
    free(out);
}

/* --- Dict tests --- */

static void test_gen_dict_literal(void)
{
    TEST("gen: dict literal");
    char *out = parse_and_codegen(
        "scores = {\"alice\": 95, \"bob\": 87}\n");
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "dict_t scores = {0};", "dict decl");
    ASSERT_STR_CONTAINS(out, "dict_set(&scores, \"alice\", 95);", "dict_set alice");
    ASSERT_STR_CONTAINS(out, "dict_set(&scores, \"bob\", 87);", "dict_set bob");
    ASSERT_STR_CONTAINS(out, "dict_t", "dict type emitted");
    ASSERT_STR_CONTAINS(out, "dict_get(dict_t d, const char *key)", "dict_get");
    PASS();
    free(out);
}

static void test_gen_dict_index(void)
{
    TEST("gen: dict index");
    const char *src =
        "scores = {\"alice\": 95}\n"
        "print scores[\"alice\"]\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "dict_get(scores, \"alice\")", "dict_get in print");
    PASS();
    free(out);
}

/* --- Break/continue tests --- */

static void test_break_in_while(void)
{
    TEST("break in while");
    const char *src =
        "x = 0\n"
        "while x < 10:\n"
        "    if x > 5:\n"
        "        break\n"
        "    x = x + 1\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "    break;", "break in while");
    ASSERT_STR_CONTAINS(out, "while ((x < 10))", "while condition");
    PASS();
    free(out);
}

static void test_continue_in_for(void)
{
    TEST("continue in for");
    const char *src =
        "for (i = 0; i < 10; i = i + 1):\n"
        "    if i == 5:\n"
        "        continue\n"
        "    print i\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "    continue;", "continue in for");
    ASSERT_STR_CONTAINS(out, "for (i = 0; (i < 10); i = (i + 1))", "for header");
    PASS();
    free(out);
}

static void test_break_outside_loop_error(void)
{
    TEST("break outside loop -> error");
    lexer_t  lexer;
    parser_t parser;
    lexer_init(&lexer, "break\n");
    parser_init(&parser, &lexer, ".");
    program_t *prog = parse_program(&parser);
    ASSERT_TRUE(parser.had_error, "should error: break outside loop");
    ast_destroy_program(prog);
    PASS();
}

static void test_continue_outside_loop_error(void)
{
    TEST("continue outside loop -> error");
    lexer_t  lexer;
    parser_t parser;
    lexer_init(&lexer, "continue\n");
    parser_init(&parser, &lexer, ".");
    program_t *prog = parse_program(&parser);
    ASSERT_TRUE(parser.had_error, "should error: continue outside loop");
    ast_destroy_program(prog);
    PASS();
}

static void test_break_continue_in_nested_loop(void)
{
    TEST("break/continue in nested loop");
    const char *src =
        "for (i = 0; i < 10; i = i + 1):\n"
        "    while 1:\n"
        "        break\n"
        "        continue\n";
    char *out = parse_and_codegen(src);
    ASSERT_NOT_NULL(out, "output non-null");
    ASSERT_STR_CONTAINS(out, "    break;", "break in nested");
    ASSERT_STR_CONTAINS(out, "    continue;", "continue in nested");
    ASSERT_STR_CONTAINS(out, "while (1)", "inner while");
    PASS();
    free(out);
}

static void test_str_len(void)
{
    TEST("str.len");
    const char *src = "name = \"Hello\"\nprint name.len\n";
    char *gen = parse_and_codegen(src);
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "strlen(name)", "str.len");
    ASSERT_STR_CONTAINS(gen, "printf(\"%d", "printf format");
    free(gen);
    PASS();
}

static void test_str_upper(void)
{
    TEST("str.upper");
    const char *src = "name = \"hello\"\nprint name.upper()\n";
    char *gen = parse_and_codegen(src);
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "str_upper(name)", "str.upper");
    ASSERT_STR_CONTAINS(gen, "printf(\"%s", "printf %%s format");
    free(gen);
    PASS();
}

static void test_str_lower(void)
{
    TEST("str.lower");
    const char *src = "name = \"HELLO\"\nprint name.lower()\n";
    char *gen = parse_and_codegen(src);
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "str_lower(name)", "str.lower");
    free(gen);
    PASS();
}

static void test_str_contains(void)
{
    TEST("str.contains");
    const char *src = "name = \"Hello\"\nprint name.contains(\"ell\")\n";
    char *gen = parse_and_codegen(src);
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "strstr(name, \"ell\") != NULL", "str.contains");
    free(gen);
    PASS();
}

static void test_str_concat(void)
{
    TEST("string concat");
    const char *src = "print \"Hello, \" + \"World\"\n";
    char *gen = parse_and_codegen(src);
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "string_concat(\"Hello, \", \"World\")", "concat");
    free(gen);
    PASS();
}

static void test_read_file(void)
{
    TEST("read_file");
    const char *src = "contents = read_file(\"file.txt\")\n";
    char *gen = parse_and_codegen(src);
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "char contents[4096]", "char buffer decl");
    ASSERT_STR_CONTAINS(gen, "fopen(\"file.txt\", \"r\")", "fopen read");
    ASSERT_STR_CONTAINS(gen, "fread(contents", "fread");
    ASSERT_STR_CONTAINS(gen, "fclose(_f)", "fclose");
    free(gen);
    PASS();
}

static void test_file_exists(void)
{
    TEST("file_exists");
    const char *src = "if file_exists(\"file.txt\"):\n    print \"exists\"\n";
    char *gen = parse_and_codegen(src);
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "file_exists(\"file.txt\")", "file_exists call");
    ASSERT_STR_CONTAINS(gen, "static int file_exists", "file_exists helper");
    free(gen);
    PASS();
}

static void test_append_file(void)
{
    TEST("append_file");
    const char *src = "append_file(\"log.txt\", \"data\")\n";
    char *gen = parse_and_codegen(src);
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "fopen(\"log.txt\", \"a\")", "fopen append");
    ASSERT_STR_CONTAINS(gen, "fputs(\"data\", _f)", "fputs");
    ASSERT_STR_CONTAINS(gen, "fclose(_f)", "fclose");
    free(gen);
    PASS();
}

/* ---- Standard Library built-in tests ---- */

static void test_builtin_sqrt(void)
{
    TEST("builtin: sqrt");
    char *gen = parse_and_codegen("print sqrt(144)\n");
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "#include <math.h>", "math.h include");
    ASSERT_STR_CONTAINS(gen, "sqrt(144)", "sqrt call");
    free(gen); PASS();
}

static void test_builtin_sin_cos(void)
{
    TEST("builtin: sin/cos");
    char *gen = parse_and_codegen("print sin(0.5) + cos(0.5)\n");
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "sin(0.5)", "sin");
    ASSERT_STR_CONTAINS(gen, "cos(0.5)", "cos");
    free(gen); PASS();
}

static void test_builtin_min_max(void)
{
    TEST("builtin: min/max");
    char *gen = parse_and_codegen("print min(10, 20)\nprint max(10, 20)\n");
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "(10 < 20 ? 10 : 20)", "min ternary");
    ASSERT_STR_CONTAINS(gen, "(10 > 20 ? 10 : 20)", "max ternary");
    free(gen); PASS();
}

static void test_builtin_clamp(void)
{
    TEST("builtin: clamp");
    char *gen = parse_and_codegen("print clamp(5, 0, 10)\n");
    ASSERT_NOT_NULL(gen, "output non-null");
    /* clamp is inlined as a ternary chain */
    ASSERT_STR_CONTAINS(gen, "(5 < 0 ? 0 : (5 > 10 ? 10 : 5))", "clamp ternary");
    free(gen); PASS();
}

static void test_builtin_random(void)
{
    TEST("builtin: random");
    char *gen = parse_and_codegen("print random()\n");
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "RAND_MAX", "random uses RAND_MAX");
    free(gen); PASS();
}

static void test_builtin_random_int(void)
{
    TEST("builtin: random_int");
    char *gen = parse_and_codegen("print random_int(1, 6)\n");
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "rand()", "random_int uses rand");
    free(gen); PASS();
}

static void test_builtin_to_int_double(void)
{
    TEST("builtin: to_int/to_double");
    char *gen = parse_and_codegen("x = to_int(3.14)\ny = to_double(42)\n");
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "(int)(3.14)", "to_int");
    ASSERT_STR_CONTAINS(gen, "(double)(42)", "to_double");
    free(gen); PASS();
}

static void test_builtin_to_string(void)
{
    TEST("builtin: to_string");
    char *gen = parse_and_codegen("print to_string(42)\n");
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "to_string(42)", "to_string call");
    ASSERT_STR_CONTAINS(gen, "static const char* to_string", "to_string helper");
    free(gen); PASS();
}

static void test_builtin_sleep_time(void)
{
    TEST("builtin: sleep/time");
    char *gen = parse_and_codegen("sleep(100)\nprint time()\n");
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "#include <time.h>", "time.h include");
    ASSERT_STR_CONTAINS(gen, "#include <unistd.h>", "unistd.h include");
    ASSERT_STR_CONTAINS(gen, "usleep((100) * 1000)", "sleep calls usleep");
    ASSERT_STR_CONTAINS(gen, "CLOCKS_PER_SEC", "time uses CLOCKS_PER_SEC");
    free(gen); PASS();
}

static void test_builtin_file_exists(void)
{
    TEST("builtin: file_exists");
    char *gen = parse_and_codegen("if file_exists(\"x.txt\"):\n    print \"y\"\n");
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "file_exists(\"x.txt\")", "file_exists call");
    ASSERT_STR_CONTAINS(gen, "static int file_exists", "file_exists helper");
    free(gen); PASS();
}

static void test_struct_def_and_use(void)
{
    TEST("struct def and use");
    const char *src =
        "struct Point:\n"
        "    x\n"
        "    y\n"
        "p = Point(10, 20)\n"
        "print p.x\n";
    char *gen = parse_and_codegen(src);
    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "typedef struct", "struct typedef");
    ASSERT_STR_CONTAINS(gen, "} Point;", "struct name");
    ASSERT_STR_CONTAINS(gen, "Point p = (Point){10, 20}", "struct instantiation");
    ASSERT_STR_CONTAINS(gen, "p.x", "field access");
    free(gen); PASS();
}

/* Simulate a package installed in ~/.mylang/packages, import it, and
   verify the emitted C contains the deduplicated std functions. */
static void test_import_std_math(void)
{
    TEST("import std/math");
    const char *old_home = getenv("HOME");
    char tmp[256], cmd[512], path[512];
    snprintf(tmp, sizeof tmp, "/tmp/mylang_cg_%ld", (long)getpid());
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
    char *gen = parse_and_codegen(
        "import \"std/math\"\n"
        "print distance(0, 0, 3, 4)\n");
    if (old_home) setenv("HOME", old_home, 1);
    else unsetenv("HOME");

    ASSERT_NOT_NULL(gen, "output non-null");
    ASSERT_STR_CONTAINS(gen, "int distance(", "distance fn emitted");
    ASSERT_STR_CONTAINS(gen, "int lerp(", "lerp fn emitted");
    ASSERT_STR_CONTAINS(gen, "#include <math.h>", "math.h include");
    ASSERT_STR_CONTAINS(gen, "distance(0, 0, 3, 4)", "call emitted");
    free(gen);

    snprintf(cmd, sizeof cmd, "rm -rf %s", tmp);
    system(cmd);
    PASS();
}

int main(void)
{
    printf("=== MyLang Codegen Test Suite ===\n\n");
    test_gen_empty();
    test_gen_fn_print();
    test_gen_print_string();
    test_gen_print_multi();
    test_gen_print_multi_with_decl();
    test_gen_var_types();
    test_gen_if_else();
    test_game_window();
    test_game_draw_circle();
    test_game_circle_alias();
    test_game_event();
    test_game_vars_and_keys();
    test_game_sprite();
    test_game_sound();
    test_game_sound_parens();
    test_game_collision();
    test_game_mouse_keys();
    test_game_draw_rect();
    test_gen_array_literal();
    test_gen_array_index();
    test_gen_input_builtin();
    test_gen_write_file();
    test_gen_dict_literal();
    test_gen_dict_index();
    test_break_in_while();
    test_continue_in_for();
    test_break_outside_loop_error();
    test_continue_outside_loop_error();
    test_break_continue_in_nested_loop();
    test_str_len();
    test_str_upper();
    test_str_lower();
    test_str_contains();
    test_str_concat();
    test_read_file();
    test_file_exists();
    test_append_file();
    test_builtin_sqrt();
    test_builtin_sin_cos();
    test_builtin_min_max();
    test_builtin_clamp();
    test_builtin_random();
    test_builtin_random_int();
    test_builtin_to_int_double();
    test_builtin_to_string();
    test_builtin_sleep_time();
    test_builtin_file_exists();
    test_struct_def_and_use();
    test_import_std_math();
    printf("\n--- Results: %d/%d passed, %d failed ---\n",
           tests_pass, tests_run, tests_fail);
    return tests_fail > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
