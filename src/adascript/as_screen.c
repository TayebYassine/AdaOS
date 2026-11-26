#include "as_screen.h"

#include <stdint.h>

#include "as_error.h"
#include "as_parser.h"
#include "as_value.h"
#include "clock.h"
#include "kmalloc.h"
#include "libc.h"
#include "screen.h"

/* Most arguments one screen statement can hold. */
#define AS_SCREEN_ARGS 12

/* The 16 text-mode colours, by the names a script may write. An index is the
 * VGA attribute byte: black is 0, white is 15. */
static const char* const as_color_names[16] = {
    "black",    "blue",         "green",      "cyan",
    "red",      "magenta",      "brown",      "lightgray",
    "darkgray", "lightblue",    "lightgreen", "lightcyan",
    "lightred", "lightmagenta", "yellow",     "white",
};

/* How many runs are holding the screen. The clock bar is paused while any run
 * holds it, so a script started by another script cannot steal it back. */
static int as_screen_holders = 0;

/* Compare two words, ignoring case. */
static int as_same_word(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

/* Split a line into arguments in place. A quoted argument keeps its quotes so
 * the expression parser can read the string literal, spaces included, and
 * brackets keep an expression with spaces in it together. Returns the argument
 * count, or -1 when there are more than max. */
static int as_split_args(char* line, char** args, int max) {
    int count = 0;
    char* p = line;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (count >= max) {
            return -1;
        }

        if (*p == '"') {
            args[count] = p;
            count++;
            p++;
            while (*p != '\0' && *p != '"') {
                if (*p == '\\' && p[1] != '\0') {
                    p++;
                }
                p++;
            }
            if (*p == '"') {
                p++;
            }
        } else {
            int depth = 0;

            args[count] = p;
            count++;
            while (*p != '\0') {
                if (*p == '(' || *p == '[' || *p == '{') {
                    depth++;
                } else if (*p == ')' || *p == ']' || *p == '}') {
                    if (depth > 0) {
                        depth--;
                    }
                } else if (depth == 0 && (*p == ' ' || *p == '\t')) {
                    break;
                }
                p++;
            }
        }

        if (*p != '\0') {
            *p = '\0';
            p++;
        }
    }

    return count;
}

/* Evaluate one argument as a number. */
static int as_arg_number(struct as_env* env, const char* expr, int* out) {
    struct as_value val = as_eval_expression(env, expr);
    int ok = !env->error;

    if (ok) {
        *out = as_value_to_i32(&val);
    }
    as_value_free(&val);

    return ok;
}

/* Join arguments first..last with single spaces, so an expression split over
 * several arguments reads back as the script wrote it. */
static void as_join_args(char* out, int max, char** args, int first, int last) {
    int pos = 0;

    out[0] = '\0';

    for (int i = first; i <= last; i++) {
        if (i > first && pos < max - 1) {
            out[pos++] = ' ';
        }
        for (int j = 0; args[i][j] != '\0' && pos < max - 1; j++) {
            out[pos++] = args[i][j];
        }
    }

    out[pos] = '\0';
}

/* Look up a colour name and hand back its attribute byte. */
static int as_color_attribute(const char* name, uint8_t* attribute) {
    for (int i = 0; i < 16; i++) {
        if (as_same_word(name, as_color_names[i])) {
            *attribute = (uint8_t)i;
            return 1;
        }
    }

    // The two spellings British English prefers.
    if (as_same_word(name, "lightgrey")) {
        *attribute = 7;
        return 1;
    }
    if (as_same_word(name, "darkgrey")) {
        *attribute = 8;
        return 1;
    }

    return 0;
}

/* Give the screen back and end the run's hold on it. */
static void as_leave_screen(struct as_env* env) {
    screen_set_cursor_visible(1);
    screen_restore(env->saved_cells, env->saved_row, env->saved_col);
    screen_set_top(env->saved_top);

    kfree(env->saved_cells);
    env->saved_cells = 0;
    env->screen_mode = 0;

    if (as_screen_holders > 0) {
        as_screen_holders--;
    }
    if (as_screen_holders == 0) {
        clock_resume();
    }
}

/* Parse a "mode <name>" statement. Returns 1 and sets *mode for a known mode,
 * 0 when the line is not a mode statement, and -1 when the name is unknown.
 * When name is not null it points at the name text. */
int as_parse_mode_line(const char* stmt, enum as_script_mode* mode,
                       const char** name) {
    const char* p = stmt;

    if (strncmp(p, "mode", 4) != 0) {
        return 0;
    }
    if (p[4] != '\0' && p[4] != ' ' && p[4] != '\t') {
        return 0;
    }

    p += 4;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (name != 0) {
        *name = p;
    }

    if (as_same_word(p, "screen")) {
        *mode = AS_MODE_SCREEN;
        return 1;
    }
    if (as_same_word(p, "cli")) {
        *mode = AS_MODE_CLI;
        return 1;
    }
    return -1;
}

/* Execute a "mode screen" or "mode cli" statement. */
void as_exec_mode(struct as_env* env, const char* stmt) {
    enum as_script_mode mode;
    const char* name = 0;
    int parsed = as_parse_mode_line(stmt, &mode, &name);

    if (parsed < 0) {
        if (name != 0 && name[0] != '\0') {
            as_errorf(env, "unknown mode '%s'; use 'screen' or 'cli'.", name);
        } else {
            as_errorf(env, "mode needs a name: 'screen' or 'cli'.");
        }
        return;
    }
    if (parsed == 0) {
        return;
    }

    if (mode == AS_MODE_SCREEN) {
        uint16_t* cells;

        if (env->screen_mode) {
            return;
        }

        cells = (uint16_t*)kmalloc(SCREEN_CELLS * sizeof(uint16_t));
        if (cells == 0) {
            as_errorf(env, "not enough memory to save the screen.");
            return;
        }

        // The clock bar must stop before the cells are copied, or a tick could
        // paint the old time into the saved screen.
        if (as_screen_holders == 0) {
            clock_pause();
        }
        as_screen_holders++;

        env->saved_cells = cells;
        env->screen_mode = 1;
        env->saved_top = screen_get_top();
        screen_save(cells, &env->saved_row, &env->saved_col);

        // The script owns every row now, so print() may scroll all the way up.
        screen_set_top(0);
        screen_clear();
        screen_set_cursor_visible(0);
        return;
    }

    if (env->screen_mode) {
        as_leave_screen(env);
    }
}

/* Execute a "writecell <row> <column> <text> [color <name>]" statement. */
void as_exec_writecell(struct as_env* env, const char* stmt) {
    char line[AS_LINE_MAX];
    char text[AS_LINE_MAX];
    char* args[AS_SCREEN_ARGS];
    struct as_value val;
    char shown[AS_LINE_MAX];
    const char* draw;
    uint8_t attribute = 0x0F;
    int count;
    int row;
    int col;
    int last;

    as_copy_text(line, stmt, sizeof(line));
    count = as_split_args(line, args, AS_SCREEN_ARGS);

    if (count < 0) {
        as_errorf(env, "writecell has too many arguments.");
        return;
    }
    if (count < 4) {
        as_errorf(env, "writecell needs a row, a column and text.");
        return;
    }

    last = count - 1;
    if (last >= 4 && as_same_word(args[last - 1], "color")) {
        if (!as_color_attribute(args[last], &attribute)) {
            as_errorf(env, "unknown colour '%s'.", args[last]);
            return;
        }
        last -= 2;
    } else if (as_same_word(args[last], "color")) {
        as_errorf(env, "writecell needs a colour name after 'color'.");
        return;
    }

    if (!as_arg_number(env, args[1], &row) ||
        !as_arg_number(env, args[2], &col)) {
        return;
    }

    if (row < 1 || row > SCREEN_ROWS) {
        as_errorf(env, "row %d is outside 1..%d.", row, SCREEN_ROWS);
        return;
    }
    if (col < 1 || col > SCREEN_COLS) {
        as_errorf(env, "column %d is outside 1..%d.", col, SCREEN_COLS);
        return;
    }

    as_join_args(text, sizeof(text), args, 3, last);

    val = as_eval_expression(env, text);
    if (env->error) {
        as_value_free(&val);
        return;
    }

    if (val.type == AS_TYPE_STRING && val.u.str != 0) {
        draw = val.u.str;
    } else {
        as_value_format(&val, shown, sizeof(shown), 0);
        draw = shown;
    }

    screen_puts_at(row - 1, col - 1, draw, attribute);
    as_value_free(&val);
}

/* Execute an "erasecell <row> <column> [<row> <column>]" statement. */
void as_exec_erasecell(struct as_env* env, const char* stmt) {
    char line[AS_LINE_MAX];
    char* args[AS_SCREEN_ARGS];
    int count;
    int row1;
    int col1;
    int row2;
    int col2;

    as_copy_text(line, stmt, sizeof(line));
    count = as_split_args(line, args, AS_SCREEN_ARGS);

    if (count < 0) {
        as_errorf(env, "erasecell has too many arguments.");
        return;
    }
    if (count != 3 && count != 5) {
        as_errorf(env,
                  "erasecell needs 'row column' or 'row column row column'.");
        return;
    }

    if (!as_arg_number(env, args[1], &row1) ||
        !as_arg_number(env, args[2], &col1)) {
        return;
    }

    row2 = row1;
    col2 = col1;
    if (count == 5) {
        if (!as_arg_number(env, args[3], &row2) ||
            !as_arg_number(env, args[4], &col2)) {
            return;
        }
    }

    if (row1 < 1 || row1 > SCREEN_ROWS || row2 < 1 || row2 > SCREEN_ROWS) {
        as_errorf(env, "rows are outside 1..%d.", SCREEN_ROWS);
        return;
    }
    if (col1 < 1 || col1 > SCREEN_COLS || col2 < 1 || col2 > SCREEN_COLS) {
        as_errorf(env, "columns are outside 1..%d.", SCREEN_COLS);
        return;
    }

    screen_fill_rect(row1 - 1, col1 - 1, row2 - 1, col2 - 1, ' ', 0x0F);
}

/* Put the shell's screen back if the run still owns it. */
void as_screen_finish(struct as_env* env) {
    char number[12];

    if (!env->screen_mode) {
        return;
    }

    as_leave_screen(env);

    // The error was printed on the canvas that has just been taken away, so it
    // is shown again on the screen the user is looking at.
    if (env->error) {
        print("Error: An unexpected error at line ");
        itoa(env->error_line, number);
        print(number);
        print(", column ");
        itoa(env->error_col, number);
        print(number);
        print_char('\n');
        print("Exception: ");
        print(env->error_text);
        print_char('\n');
    }
}
