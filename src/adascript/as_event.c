#include "as_event.h"

#include <stdint.h>

#include "adascript.h"
#include "as_env.h"
#include "as_error.h"
#include "as_interp.h"
#include "as_value.h"
#include "interrupt.h"
#include "keyboard.h"
#include "libc.h"
#include "screen.h"

#define AS_KEY_NAME_MAX 32

static const char* const as_event_names[AS_EVENT_COUNT] = {
    "keypress",
    "cursormove",
    "exit",
};

static void as_dispatch(struct as_env* env, struct as_listener* listener,
                        const char* key_name, int32_t first, int32_t second);

/* Is p the whole word kw, followed by a space, a tab, a ';' or the end? */
static int as_word_is(const char* p, const char* kw) {
    size_t len = strlen(kw);
    char after;

    if (strncmp(p, kw, len) != 0) {
        return 0;
    }

    after = p[len];
    return after == '\0' || after == ' ' || after == '\t' || after == ';';
}

/* The event an "on" line names. Returns its kind, -1 for a line that is not a
 * listener, and -2 for an "on" line with an unknown event name. */
static int as_event_kind(const char* stmt, const char** after) {
    const char* p;
    int kind;

    if (strncmp(stmt, "on", 2) != 0 || (stmt[2] != ' ' && stmt[2] != '\t')) {
        return -1;
    }

    p = stmt + 2;
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    for (kind = 0; kind < AS_EVENT_COUNT; kind++) {
        if (as_word_is(p, as_event_names[kind])) {
            if (after != 0) {
                *after = p + strlen(as_event_names[kind]);
            }
            return kind;
        }
    }

    if (after != 0) {
        *after = p;
    }
    return -2;
}

const char* as_event_cli_conflict(const char* stmt) {
    int kind = as_event_kind(stmt, 0);

    if (kind == AS_EVENT_KEYPRESS) {
        return "on keypress";
    }
    if (kind == AS_EVENT_CURSORMOVE) {
        return "on cursormove";
    }

    return 0;
}

/* Read one variable name from *pp. Returns 0 when the word is not a usable
 * name; either way *pp is left after it. */
static int as_read_name(const char** pp, char* out, int max) {
    const char* p = *pp;
    int len = 0;
    int ok = 1;

    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_')) {
        ok = 0;
    }

    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != ';') {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_')) {
            ok = 0;
        }
        if (len < max - 1) {
            out[len++] = *p;
        } else {
            ok = 0;
        }
        p++;
    }
    out[len] = '\0';
    *pp = p;

    return ok && len > 0;
}

void as_event_register(struct as_env* env, const char* stmt, int body_start,
                       int body_end) {
    const char* p = 0;
    int kind = as_event_kind(stmt, &p);
    struct as_listener listener;

    if (kind < 0) {
        char name[AS_NAME_MAX];
        int len = 0;

        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != ';') {
            if (len < (int)sizeof(name) - 1) {
                name[len++] = *p;
            }
            p++;
        }
        name[len] = '\0';

        if (kind == -2 && name[0] != '\0') {
            as_errorf(env,
                      "unknown event '%s'; use keypress, cursormove or exit.",
                      name);
        } else {
            as_errorf(env, "'on' needs an event: keypress, cursormove or exit.");
        }
        return;
    }

    if (env->script_mode == AS_MODE_CLI &&
        (kind == AS_EVENT_KEYPRESS || kind == AS_EVENT_CURSORMOVE)) {
        as_errorf(env, "'on %s' is not available in mode cli.",
                  as_event_names[kind]);
        return;
    }

    listener.defined = 1;
    listener.kind = kind;
    listener.running = 0;
    listener.param_count = 0;
    listener.body_start = body_start;
    listener.body_end = body_end;
    listener.line = env->cur_line;
    listener.col = env->cur_col;

    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (as_word_is(p, "get")) {
        p += 3;

        for (;;) {
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p == '\0' || *p == ';' || as_word_is(p, "do")) {
                break;
            }
            if (listener.param_count >= AS_EVENT_PARAMS) {
                as_errorf(env, "'on %s' takes at most %d variables.",
                          as_event_names[kind], AS_EVENT_PARAMS);
                return;
            }

            if (!as_read_name(&p, listener.params[listener.param_count],
                              AS_NAME_MAX)) {
                as_errorf(env, "'%s' is not a usable variable name.",
                          listener.params[listener.param_count]);
                return;
            }
            listener.param_count++;
        }
    }

    if (as_word_is(p, "do")) {
        p += 2;
    }
    while (*p == ' ' || *p == '\t' || *p == ';') {
        p++;
    }
    if (*p != '\0') {
        as_errorf(env, "unexpected text after the 'on %s' line.",
                  as_event_names[kind]);
        return;
    }

    if (kind == AS_EVENT_EXIT && listener.param_count != 0) {
        as_errorf(env, "'on exit' takes no variables.");
        return;
    }
    if (kind == AS_EVENT_KEYPRESS && listener.param_count > 1) {
        as_errorf(env, "'on keypress' takes one variable: the key.");
        return;
    }
    if (kind == AS_EVENT_CURSORMOVE && listener.param_count != 0 &&
        listener.param_count != 2) {
        as_errorf(env, "'on cursormove' takes two variables: row and column.");
        return;
    }
    if (listener.param_count == 2 &&
        strcmp(listener.params[0], listener.params[1]) == 0) {
        as_errorf(env, "'%s' cannot be given twice.", listener.params[0]);
        return;
    }

    env->listeners[kind] = listener;
}

/* Give a handler variable a number, replacing whatever it held before. */
static void as_bind_number(struct as_env* env, const char* name,
                           int32_t number) {
    struct as_var* var = as_env_define(env, name, AS_TYPE_INT32, AS_TYPE_VOID);

    if (var == 0) {
        as_errorf(env, "cannot define '%s'.", name);
        return;
    }

    var->val.is_null = 0;
    var->val.u.i32 = number;
}

/* Give a handler variable text, replacing whatever it held before. */
static void as_bind_text(struct as_env* env, const char* name,
                         const char* text) {
    struct as_var* var = as_env_define(env, name, AS_TYPE_STRING, AS_TYPE_VOID);

    if (var == 0) {
        as_errorf(env, "cannot define '%s'.", name);
        return;
    }

    var->val.u.str = as_strdup(text);
    var->val.is_null = (var->val.u.str == 0);
}

/* Run one handler body with its variables bound. The variables are local: a
 * name the script already had is put back afterwards, and one the event made
 * is removed. */
static void as_dispatch(struct as_env* env, struct as_listener* listener,
                        const char* key_name, int32_t first, int32_t second) {
    struct as_value saved[AS_EVENT_PARAMS];
    int had[AS_EVENT_PARAMS];
    int line;
    int col;
    int saved_exit;

    if (listener->running) {
        return;
    }
    listener->running = 1;

    line = env->cur_line;
    col = env->cur_col;

    // An exit handler is dispatched after Ctrl+E has already asked the script
    // to stop; the flag is put back afterwards so it still does.
    saved_exit = env->exit_flag;
    env->exit_flag = 0;
    env->break_flag = 0;
    env->continue_flag = 0;
    env->cancel_flag = 0;
    env->in_handler++;

    for (int i = 0; i < listener->param_count; i++) {
        struct as_var* var = as_env_lookup(env, listener->params[i]);

        had[i] = var != 0;
        as_value_init(&saved[i], AS_TYPE_VOID, AS_TYPE_VOID);
        if (had[i]) {
            as_value_copy(&saved[i], &var->val);
        }
    }

    for (int i = 0; i < listener->param_count; i++) {
        if (listener->kind == AS_EVENT_KEYPRESS) {
            as_bind_text(env, listener->params[i], key_name);
        } else {
            as_bind_number(env, listener->params[i], i == 0 ? first : second);
        }
    }

    env->cur_line = listener->line;
    env->cur_col = listener->col;
    as_exec_block(env, env->lines, listener->body_start, listener->body_end);
    env->cur_line = line;
    env->cur_col = col;

    for (int i = 0; i < listener->param_count; i++) {
        if (had[i]) {
            struct as_var* var = as_env_lookup(env, listener->params[i]);

            if (var != 0) {
                as_value_copy(&var->val, &saved[i]);
            }
        } else {
            as_env_delete(env, listener->params[i]);
        }
        as_value_free(&saved[i]);
    }

    listener->running = 0;
    env->in_handler--;

    env->exit_flag = env->exit_flag || saved_exit;
}

int as_event_has_waiters(const struct as_env* env) {
    return env->listeners[AS_EVENT_KEYPRESS].defined ||
           env->listeners[AS_EVENT_CURSORMOVE].defined;
}

/* The name a key is reported as: "A", "1", "&", "Space", "Backspace",
 * "Escape", "Up", "Ctrl+S"... */
static void as_key_text(int key, char* out, int max) {
    if (out == 0 || max < 2) {
        return;
    }

    if (key == ' ') {
        as_copy_text(out, "Space", max);
        return;
    }
    if (key == '\b') {
        as_copy_text(out, "Backspace", max);
        return;
    }
    if (key == '\n') {
        as_copy_text(out, "Enter", max);
        return;
    }
    if (key == '\t') {
        as_copy_text(out, "Tab", max);
        return;
    }
    if (key == KEY_ESC) {
        as_copy_text(out, "Escape", max);
        return;
    }
    if (key == KEY_UP) {
        as_copy_text(out, "Up", max);
        return;
    }
    if (key == KEY_DOWN) {
        as_copy_text(out, "Down", max);
        return;
    }
    if (key == KEY_LEFT) {
        as_copy_text(out, "Left", max);
        return;
    }
    if (key == KEY_RIGHT) {
        as_copy_text(out, "Right", max);
        return;
    }

    // Control codes are Ctrl plus a letter; Ctrl+E is the exit event instead.
    if (key >= 1 && key <= 26) {
        int len;

        as_copy_text(out, "Ctrl+", max);
        len = (int)strlen(out);
        if (len + 1 < max) {
            out[len] = (char)('A' + key - 1);
            out[len + 1] = '\0';
        }
        return;
    }

    // Letters are reported in uppercase whatever case they were typed in.
    if (key >= 'a' && key <= 'z') {
        key -= 32;
    }
    if (key > 32 && key < 127) {
        out[0] = (char)key;
        out[1] = '\0';
        return;
    }

    as_copy_text(out, "Unknown", max);
}

/* Where an arrow key would take the cursor. Returns 0 for any other key and
 * when the cursor already sits on that edge of the screen. */
static int as_arrow_target(int key, int row, int col, int* out_row,
                           int* out_col) {
    switch (key) {
        case KEY_UP:
            if (row <= 0) {
                return 0;
            }
            row--;
            break;
        case KEY_DOWN:
            if (row >= SCREEN_ROWS - 1) {
                return 0;
            }
            row++;
            break;
        case KEY_LEFT:
            if (col <= 0) {
                return 0;
            }
            col--;
            break;
        case KEY_RIGHT:
            if (col >= SCREEN_COLS - 1) {
                return 0;
            }
            col++;
            break;
        default:
            return 0;
    }

    *out_row = row;
    *out_col = col;
    return 1;
}

int as_event_loop(struct as_env* env) {
    struct as_listener* keys = &env->listeners[AS_EVENT_KEYPRESS];

    for (;;) {
        int key;

        if (env->error) {
            return -1;
        }
        if (env->exit_flag) {
            return 0;
        }
        if (adascript_interrupt_requested()) {
            env->exit_flag = 1;
            return 0;
        }

        key = keyboard_poll();
        if (key == KEY_CTRL_E) {
            env->exit_flag = 1;
            return 0;
        }

        if (key != 0) {
            int row;
            int col;
            int next_row;
            int next_col;

            screen_get_cursor(&row, &col);

            // An arrow key asks to move the cursor. The handler hears about
            // it before the cursor is shown in the new cell, and "cancel" in
            // the handler keeps it where it is.
            if (!env->cursor_fixed && env->screen_mode &&
                as_arrow_target(key, row, col, &next_row, &next_col)) {
                struct as_listener* mover =
                    &env->listeners[AS_EVENT_CURSORMOVE];

                env->cancel_flag = 0;
                if (mover->defined) {
                    as_dispatch(env, mover, 0, next_row + 1, next_col + 1);
                }
                if (!env->cancel_flag) {
                    screen_move_to(next_row, next_col);
                }
                env->cancel_flag = 0;
            }

            if (keys->defined) {
                char name[AS_KEY_NAME_MAX];

                as_key_text(key, name, sizeof(name));
                as_dispatch(env, keys, name, 0, 0);
            }
        }

        // Nothing else to do until the keyboard or the timer interrupts.
        interrupt_wait();
    }
}

void as_event_dispatch_exit(struct as_env* env) {
    struct as_listener* listener = &env->listeners[AS_EVENT_EXIT];

    if (listener->defined) {
        as_dispatch(env, listener, 0, 0, 0);
    }
}
