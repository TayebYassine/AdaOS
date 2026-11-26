#include "as_interp.h"

#include "adascript.h"
#include "as_command.h"
#include "as_env.h"
#include "as_error.h"
#include "as_input.h"
#include "as_parser.h"
#include "as_screen.h"
#include "as_value.h"
#include "libc.h"
#include "screen.h"

/* Is p the whole keyword kw? Keeps `if dot > 0 do` from matching "do". */
static int as_word_is(const char* p, const char* kw) {
    size_t len = strlen(kw);
    char after;

    if (strncmp(p, kw, len) != 0) {
        return 0;
    }

    after = p[len];
    return after == '\0' || after == ' ' || after == '\t' || after == ':' ||
           after == ';';
}

static int as_is_block_start(const char* t) {
    return strncmp(t, "for ", 4) == 0 || strncmp(t, "while ", 6) == 0 ||
           strncmp(t, "if ", 3) == 0;
}

/* Does this line close a block? "done" closes any, the rest name theirs. */
static int as_is_block_end(const char* t) {
    return strcmp(t, "done") == 0 || strcmp(t, "done;") == 0 ||
           strcmp(t, "endfor") == 0 || strcmp(t, "endfor;") == 0 ||
           strcmp(t, "endwhile") == 0 || strcmp(t, "endwhile;") == 0 ||
           strcmp(t, "endif") == 0 || strcmp(t, "endif;") == 0 ||
           strcmp(t, "fi") == 0 || strcmp(t, "fi;") == 0;
}

static int as_stmt_is(const char* stmt, const char* kw) {
    size_t len = strlen(kw);

    if (strncmp(stmt, kw, len) != 0) {
        return 0;
    }

    return stmt[len] == '\0' || stmt[len] == ' ' || stmt[len] == '\t' ||
           stmt[len] == ';';
}

/* Return the statement that the script's mode does not allow, or 0 when it is
 * allowed. "mode cli" works in both modes: a screen script may use it to give
 * the screen back early. */
static const char* as_mode_conflict(enum as_script_mode mode,
                                    const char* stmt) {
    if (mode == AS_MODE_SCREEN) {
        if (as_stmt_is(stmt, "print")) {
            return "print";
        }
        if (as_stmt_is(stmt, "input")) {
            return "input";
        }
    } else if (mode == AS_MODE_CLI) {
        if (as_stmt_is(stmt, "writecell")) {
            return "writecell";
        }
        if (as_stmt_is(stmt, "erasecell")) {
            return "erasecell";
        }
        if (as_stmt_is(stmt, "clearscreen")) {
            return "clearscreen";
        }
        if (as_stmt_is(stmt, "keepalive")) {
            return "keepalive";
        }
        if (as_stmt_is(stmt, "mode")) {
            enum as_script_mode declared;

            if (as_parse_mode_line(stmt, &declared, 0) == 1 &&
                declared == AS_MODE_SCREEN) {
                return "mode screen";
            }
        }
    }

    return 0;
}

static void as_mode_error(struct as_env* env, const char* statement) {
    as_errorf(env, "'%s' is not available in mode %s.", statement,
              env->script_mode == AS_MODE_SCREEN ? "screen" : "cli");
}

/* Check every line against the declared mode before the script runs, so a
 * stray statement is reported before the script can change the screen. */
int as_check_script_mode(struct as_env* env, struct as_line* lines, int total) {
    for (int i = 0; i < total; i++) {
        const char* stmt = lines[i].text;
        const char* conflict;

        while (*stmt == ' ') {
            stmt++;
        }

        conflict = as_mode_conflict(env->script_mode, stmt);
        if (conflict != 0) {
            env->cur_line = lines[i].orig_line;
            env->cur_col = lines[i].orig_col;
            as_mode_error(env, conflict);
            return -1;
        }
    }

    return 0;
}

static int as_find_matching_done(struct as_line* lines, int total, int start) {
    int depth = 1;
    for (int i = start + 1; i < total; i++) {
        const char* t = lines[i].text;
        if (as_is_block_start(t)) {
            depth++;
        } else if (as_is_block_end(t)) {
            depth--;
            if (depth == 0) {
                return i;
            }
        }
    }
    return -1;
}

static void as_exec_define(struct as_env* env, const char* stmt) {
    const char* p = stmt + 6;  // skip "define"
    while (*p == ' ') p++;

    char name[AS_NAME_MAX];
    int n_len = 0;
    int name_too_long = 0;
    while (*p != '\0' && *p != ':' && *p != ' ' && *p != '=') {
        if (n_len < (int)sizeof(name) - 1) {
            name[n_len++] = *p;
        } else {
            name_too_long = 1;
        }
        p++;
    }
    name[n_len] = '\0';

    if (name[0] == '\0') {
        as_errorf(env, "define needs a name.");
        return;
    }
    if (name_too_long) {
        as_errorf(env, "a variable name is longer than %d characters.",
                  AS_NAME_MAX - 1);
        return;
    }

    while (*p == ' ') p++;
    if (*p == ':') p++;
    while (*p == ' ') p++;

    char type_str[32];
    int t_len = 0;
    while (*p != '\0' && *p != '=' && *p != ' ' && *p != ';') {
        if (t_len < (int)sizeof(type_str) - 1) {
            type_str[t_len++] = *p;
        }
        p++;
    }
    type_str[t_len] = '\0';

    enum as_type type;
    enum as_type elem_type;
    if (!as_parse_type(type_str, &type, &elem_type)) {
        as_errorf(env, "unknown type '%s'.", type_str);
        return;
    }

    struct as_var* var = as_env_define(env, name, type, elem_type);
    if (var == 0) {
        as_errorf(env, "cannot define variable '%s'.", name);
        return;
    }

    while (*p == ' ') p++;
    if (*p == '=') {
        p++;
        while (*p == ' ') p++;

        char expr_buf[AS_LINE_MAX];
        strncpy(expr_buf, p, sizeof(expr_buf) - 1);
        expr_buf[sizeof(expr_buf) - 1] = '\0';
        int e_len = (int)strlen(expr_buf);
        while (e_len > 0 &&
               (expr_buf[e_len - 1] == ' ' || expr_buf[e_len - 1] == ';')) {
            expr_buf[--e_len] = '\0';
        }

        struct as_value val = as_eval_expression(env, expr_buf);
        if (env->error) {
            as_value_free(&val);
            return;
        }
        if (var->val.type == AS_TYPE_ARRAY && val.type != AS_TYPE_ARRAY) {
            as_errorf(env, "'%s' is an array; give it { ... }.", name);
        } else {
            as_value_cast_assign(&var->val, &val);
        }
        as_value_free(&val);
    }
}

static void as_exec_delete(struct as_env* env, const char* stmt) {
    const char* p = stmt + 6;
    while (*p == ' ') p++;

    char name[AS_NAME_MAX];
    int n_len = 0;
    while (*p != '\0' && *p != ' ' && *p != ';') {
        if (n_len < (int)sizeof(name) - 1) {
            name[n_len++] = *p;
        }
        p++;
    }
    name[n_len] = '\0';

    if (!as_env_delete(env, name)) {
        as_errorf(env, "variable '%s' not found.", name);
    }
}

static void as_exec_print(struct as_env* env, const char* stmt) {
    const char* p = stmt + 5;
    char body[AS_LINE_MAX];
    int body_len;
    int newline = 0;

    while (*p == ' ') p++;
    as_copy_text(body, p, sizeof(body));

    // A trailing "endl" is the newline instruction, not a value.
    body_len = (int)strlen(body);
    while (body_len > 0 &&
           (body[body_len - 1] == ' ' || body[body_len - 1] == '\t' ||
            body[body_len - 1] == ';')) {
        body[--body_len] = '\0';
    }
    if (body_len >= 4 && strcmp(body + body_len - 4, "endl") == 0 &&
        (body_len == 4 || body[body_len - 5] == ' ' ||
         body[body_len - 5] == '\t' || body[body_len - 5] == ',')) {
        newline = 1;
        body_len -= 4;
        while (body_len > 0 &&
               (body[body_len - 1] == ' ' || body[body_len - 1] == '\t' ||
                body[body_len - 1] == ',')) {
            body[--body_len] = '\0';
        }

        // "text + endl" means the same as "text endl": the '+' was joining
        // the newline word, which is no value. Left in place it would join the
        // missing right operand, which reads as 0, so every such line ended
        // with a stray 0.
        if (body_len > 0 && body[body_len - 1] == '+') {
            body[--body_len] = '\0';
            while (body_len > 0 &&
                   (body[body_len - 1] == ' ' || body[body_len - 1] == '\t')) {
                body[--body_len] = '\0';
            }
        }
    }

    p = body;

    if (*p == '\0' || *p == ';') {
        print_char('\n');
        return;
    }

    int first = 1;

    while (*p != '\0' && *p != ';') {
        char arg_buf[AS_LINE_MAX];
        int a_len = 0;
        int in_quote = 0;
        int escaped = 0;
        int paren_depth = 0;

        while (*p != '\0') {
            char c = *p;

            if (escaped) {
                escaped = 0;
            } else if (in_quote && c == '\\') {
                escaped = 1;
            } else if (c == '"') {
                in_quote = !in_quote;
            } else if (!in_quote) {
                if (c == '(' || c == '{' || c == '[') {
                    paren_depth++;
                } else if (c == ')' || c == '}' || c == ']') {
                    paren_depth--;
                } else if (paren_depth == 0 && (c == ',' || c == ';')) {
                    if (c == ',') {
                        p++;
                    }
                    break;
                }
            }

            if (a_len < (int)sizeof(arg_buf) - 1) {
                arg_buf[a_len++] = c;
            }
            p++;
        }
        arg_buf[a_len] = '\0';

        char* a_start = arg_buf;
        while (*a_start == ' ') a_start++;
        int a_end = (int)strlen(a_start);
        while (a_end > 0 &&
               (a_start[a_end - 1] == ' ' || a_start[a_end - 1] == ';')) {
            a_start[--a_end] = '\0';
        }

        // "endl" ends the line instead of printing a value.
        if (strcmp(a_start, "endl") == 0) {
            newline = 1;
            while (*p == ' ') p++;
            continue;
        }

        if (!first) {
            print_char(' ');
        }
        first = 0;

        struct as_value val = as_eval_expression(env, a_start);
        char out_buf[AS_LINE_MAX];
        if (env->error) {
            as_value_free(&val);
            return;
        }
        if (val.type == AS_TYPE_STRING && val.u.str != 0) {
            print(val.u.str);
        } else {
            as_value_format(&val, out_buf, sizeof(out_buf), 0);
            print(out_buf);
        }
        as_value_free(&val);

        while (*p == ' ') p++;
    }

    if (newline) {
        print_char('\n');
    }
}

static void as_exec_assignment(struct as_env* env, const char* stmt) {
    const char* eq = strchr(stmt, '=');
    if (eq == 0) return;

    char lhs[AS_LINE_MAX];
    int l_len = (int)(eq - stmt);
    if (l_len >= (int)sizeof(lhs)) l_len = (int)sizeof(lhs) - 1;
    memcpy(lhs, stmt, l_len);
    lhs[l_len] = '\0';

    char* l_start = lhs;
    while (*l_start == ' ') l_start++;
    int l_end = (int)strlen(l_start);
    while (l_end > 0 && l_start[l_end - 1] == ' ') {
        l_start[--l_end] = '\0';
    }

    const char* rhs = eq + 1;
    while (*rhs == ' ') rhs++;

    char rhs_buf[AS_LINE_MAX];
    strncpy(rhs_buf, rhs, sizeof(rhs_buf) - 1);
    rhs_buf[sizeof(rhs_buf) - 1] = '\0';
    int r_len = (int)strlen(rhs_buf);
    while (r_len > 0 &&
           (rhs_buf[r_len - 1] == ' ' || rhs_buf[r_len - 1] == ';')) {
        rhs_buf[--r_len] = '\0';
    }

    struct as_value val = as_eval_expression(env, rhs_buf);
    if (env->error) {
        as_value_free(&val);
        return;
    }

    char* lbrack = strchr(l_start, '[');
    if (lbrack != 0) {
        *lbrack = '\0';
        char* rbrack = strchr(lbrack + 1, ']');
        if (rbrack != 0) *rbrack = '\0';

        if ((int)strlen(l_start) >= AS_NAME_MAX) {
            as_errorf(env, "variable name '%s' is too long.", l_start);
            as_value_free(&val);
            return;
        }

        struct as_var* var = as_env_lookup(env, l_start);
        if (var == 0) {
            as_errorf(env, "variable '%s' not found.", l_start);
            as_value_free(&val);
            return;
        }
        if (var->val.type != AS_TYPE_ARRAY) {
            as_errorf(env, "'%s' is not an array.", l_start);
            as_value_free(&val);
            return;
        }

        struct as_value idx_val = as_eval_expression(env, lbrack + 1);
        int idx = as_value_to_i32(&idx_val);
        as_value_free(&idx_val);

        if (idx < 0 || idx >= var->val.u.arr.count) {
            as_errorf(env, "array index %d out of bounds for '%s'.", idx,
                      l_start);
            as_value_free(&val);
            return;
        }

        as_value_cast_assign(&var->val.u.arr.items[idx], &val);
    } else {
        if ((int)strlen(l_start) >= AS_NAME_MAX) {
            as_errorf(env, "variable name '%s' is too long.", l_start);
            as_value_free(&val);
            return;
        }

        struct as_var* var = as_env_lookup(env, l_start);
        if (var == 0) {
            var = as_env_define(env, l_start, val.type, val.elem_type);
        }
        if (var != 0) {
            if (var->val.type == AS_TYPE_ARRAY && val.type != AS_TYPE_ARRAY) {
                as_errorf(env, "'%s' is an array; assign { ... } instead.",
                          l_start);
            } else {
                as_value_cast_assign(&var->val, &val);
            }
        }
    }

    as_value_free(&val);
}

// Every level of block nesting costs a whole as_exec_block() frame, and a
// script may open blocks as deep as it likes. Eight levels leaves room for the
// statement being run and the expression parser above them inside the task's
// 16 KiB stack, and stops a bad script with an error instead of a crash.
#define AS_MAX_BLOCK_DEPTH 8

static int as_exec_block_inner(struct as_env* env, struct as_line* lines,
                               int start, int end) {
    int i = start;
    while (i <= end) {
        if (env->error) {
            return -1;
        }
        // Ctrl+E stops the whole script, in either mode, between statements.
        if (adascript_interrupt_requested()) {
            env->exit_flag = 1;
            return 0;
        }
        if (env->exit_flag || env->break_flag || env->continue_flag) {
            return 0;
        }

        const char* stmt = lines[i].text;
        while (*stmt == ' ') stmt++;

        env->cur_line = lines[i].orig_line;
        env->cur_col = lines[i].orig_col;

        if (*stmt == '\0') {
            i++;
            continue;
        }

        // A statement from the other mode must never run, even if the
        // up-front scan somehow missed it.
        {
            const char* conflict = as_mode_conflict(env->script_mode, stmt);
            if (conflict != 0) {
                as_mode_error(env, conflict);
                i++;
                continue;
            }
        }

        // Loop and script control, which only set a flag for the caller.
        if (strcmp(stmt, "break") == 0 || strcmp(stmt, "break;") == 0) {
            if (env->loop_depth == 0) {
                as_errorf(env, "'break' outside a loop.");
                i++;
                continue;
            }
            env->break_flag = 1;
            return 0;
        }
        if (strcmp(stmt, "continue") == 0 || strcmp(stmt, "continue;") == 0) {
            if (env->loop_depth == 0) {
                as_errorf(env, "'continue' outside a loop.");
                i++;
                continue;
            }
            env->continue_flag = 1;
            return 0;
        }
        if (strcmp(stmt, "exit") == 0 || strcmp(stmt, "exit;") == 0 ||
            strcmp(stmt, "return") == 0 || strcmp(stmt, "return;") == 0) {
            env->exit_flag = 1;
            return 0;
        }

        // The simple statements: one per line, no block around them.
        if (strncmp(stmt, "define ", 7) == 0) {
            as_exec_define(env, stmt);
            i++;
            continue;
        }

        if (strncmp(stmt, "delete ", 7) == 0) {
            as_exec_delete(env, stmt);
            i++;
            continue;
        }

        if (strncmp(stmt, "print", 5) == 0 &&
            (stmt[5] == ' ' || stmt[5] == ';' || stmt[5] == '\0')) {
            as_exec_print(env, stmt);
            i++;
            continue;
        }

        if (strncmp(stmt, "input", 5) == 0 &&
            (stmt[5] == ' ' || stmt[5] == '\t' || stmt[5] == ';' ||
             stmt[5] == '\0')) {
            as_exec_input(env, stmt);
            i++;
            continue;
        }

        if (strncmp(stmt, "mode", 4) == 0 &&
            (stmt[4] == ' ' || stmt[4] == '\0')) {
            as_exec_mode(env, stmt);
            i++;
            continue;
        }

        if (strncmp(stmt, "writecell", 9) == 0 &&
            (stmt[9] == ' ' || stmt[9] == '\0')) {
            as_exec_writecell(env, stmt);
            i++;
            continue;
        }

        if (strncmp(stmt, "erasecell", 9) == 0 &&
            (stmt[9] == ' ' || stmt[9] == '\0')) {
            as_exec_erasecell(env, stmt);
            i++;
            continue;
        }

        if (strncmp(stmt, "clearscreen", 11) == 0 &&
            (stmt[11] == ' ' || stmt[11] == '\t' || stmt[11] == ';' ||
             stmt[11] == '\0')) {
            as_exec_clearscreen(env, stmt);
            i++;
            continue;
        }

        if (strncmp(stmt, "keepalive", 9) == 0 &&
            (stmt[9] == ' ' || stmt[9] == '\t' || stmt[9] == ';' ||
             stmt[9] == '\0')) {
            as_exec_keepalive(env, stmt);
            i++;
            continue;
        }

        // for over an array: `for x in arr do`.
        if (strncmp(stmt, "for ", 4) == 0) {
            int done_line = as_find_matching_done(lines, end + 1, i);
            if (done_line < 0) {
                as_errorf(env, "'for' without matching 'endfor' (or 'done').");
                return -1;
            }

            const char* fp = stmt + 4;
            while (*fp == ' ') fp++;

            char var_name[AS_NAME_MAX];
            int vn_len = 0;
            while (*fp != '\0' && *fp != ' ' && *fp != ':') {
                if (vn_len < (int)sizeof(var_name) - 1) {
                    var_name[vn_len++] = *fp;
                }
                fp++;
            }
            var_name[vn_len] = '\0';
            while (*fp == ' ') fp++;
            if (*fp == ':') {
                fp++;
                while (*fp == ' ') fp++;
            }

            if (as_word_is(fp, "in")) {
                fp += 2;
                while (*fp == ' ' || *fp == ':') fp++;

                char arr_name[AS_NAME_MAX];
                int an_len = 0;
                while (*fp != '\0' && *fp != ' ' && *fp != ':' && *fp != ';') {
                    if (an_len < (int)sizeof(arr_name) - 1) {
                        arr_name[an_len++] = *fp;
                    }
                    fp++;
                }
                arr_name[an_len] = '\0';

                struct as_var* arr_var;
                struct as_var* iter_var;
                int total_elems;

                // "for x in x" would hand the array's own storage back to
                // as_value_copy, which frees the old value first: the loop
                // would then walk memory it had already given away.
                if (strcmp(var_name, arr_name) == 0) {
                    as_errorf(env,
                              "'%s' cannot be both the loop variable and the "
                              "array it walks.",
                              var_name);
                    i = done_line + 1;
                    continue;
                }

                arr_var = as_env_lookup(env, arr_name);
                if (arr_var == 0 || arr_var->val.type != AS_TYPE_ARRAY) {
                    as_errorf(env, "'%s' is not an array.", arr_name);
                    i = done_line + 1;
                    continue;
                }

                iter_var = as_env_lookup(env, var_name);
                if (iter_var == 0) {
                    iter_var = as_env_define(
                        env, var_name, arr_var->val.elem_type, AS_TYPE_VOID);
                }
                if (iter_var == 0) {
                    as_errorf(env, "cannot define '%s'.", var_name);
                    i = done_line + 1;
                    continue;
                }

                total_elems = arr_var->val.u.arr.count;
                env->loop_depth++;
                for (int k = 0; k < total_elems; k++) {
                    // Look both names up again every pass: the body may have
                    // deleted or redefined either of them, and the cached
                    // pointers would then point at freed memory.
                    struct as_var* live_arr = as_env_lookup(env, arr_name);
                    struct as_var* live_iter = as_env_lookup(env, var_name);

                    if (live_arr == 0 || live_arr->val.type != AS_TYPE_ARRAY ||
                        live_arr->val.u.arr.items == 0 ||
                        k >= live_arr->val.u.arr.count) {
                        break;
                    }
                    if (live_iter == 0) {
                        as_errorf(env, "'%s' went away during the loop.",
                                  var_name);
                        break;
                    }

                    as_value_copy(&live_iter->val,
                                  &live_arr->val.u.arr.items[k]);
                    as_exec_block(env, lines, i + 1, done_line - 1);

                    if (env->error) break;
                    if (env->exit_flag) break;
                    if (env->break_flag) {
                        env->break_flag = 0;
                        break;
                    }
                    env->continue_flag = 0;
                }
                env->loop_depth--;

                i = done_line + 1;
                continue;
            } else {
                // Counting loop: `for i from A to B step S do`, where the
                // step defaults to 1 and may be negative.
                struct as_var* iter_var = as_env_lookup(env, var_name);
                if (iter_var == 0) {
                    iter_var = as_env_define(env, var_name, AS_TYPE_INT32,
                                             AS_TYPE_VOID);
                }
                if (iter_var == 0) {
                    as_errorf(env, "cannot define counter '%s'.", var_name);
                    i = done_line + 1;
                    continue;
                }

                char start_expr[AS_LINE_MAX];
                char end_expr[AS_LINE_MAX];
                char step_expr[AS_LINE_MAX];
                start_expr[0] = '\0';
                end_expr[0] = '\0';
                step_expr[0] = '\0';

                if (as_word_is(fp, "from")) {
                    fp += 4;
                    while (*fp == ' ' || *fp == ':') fp++;
                    int s_len = 0;
                    while (*fp != '\0' && !as_word_is(fp, "to")) {
                        if (s_len < (int)sizeof(start_expr) - 1) {
                            start_expr[s_len++] = *fp;
                        }
                        fp++;
                    }
                    start_expr[s_len] = '\0';
                    struct as_value s_val = as_eval_expression(env, start_expr);
                    as_value_cast_assign(&iter_var->val, &s_val);
                    as_value_free(&s_val);
                }

                if (as_word_is(fp, "to")) {
                    fp += 2;
                    while (*fp == ' ' || *fp == ':') fp++;
                }

                int e_len = 0;
                while (*fp != '\0' && !as_word_is(fp, "step") &&
                       !as_word_is(fp, "do")) {
                    if (e_len < (int)sizeof(end_expr) - 1) {
                        end_expr[e_len++] = *fp;
                    }
                    fp++;
                }
                end_expr[e_len] = '\0';

                int step = 1;
                if (as_word_is(fp, "step")) {
                    fp += 4;
                    while (*fp == ' ' || *fp == ':') fp++;
                    int st_len = 0;
                    while (*fp != '\0' && !as_word_is(fp, "do")) {
                        if (st_len < (int)sizeof(step_expr) - 1) {
                            step_expr[st_len++] = *fp;
                        }
                        fp++;
                    }
                    step_expr[st_len] = '\0';
                    struct as_value st_val = as_eval_expression(env, step_expr);
                    step = as_value_to_i32(&st_val);
                    as_value_free(&st_val);
                }

                if (end_expr[0] == '\0' || step == 0) {
                    as_errorf(env, "'for' needs a limit and a non-zero step.");
                    i = done_line + 1;
                    continue;
                }

                struct as_value end_val = as_eval_expression(env, end_expr);
                int32_t target_end = as_value_to_i32(&end_val);
                as_value_free(&end_val);

                env->loop_depth++;
                while (1) {
                    int32_t cur = as_value_to_i32(&iter_var->val);
                    if (step > 0 && cur > target_end) break;
                    if (step < 0 && cur < target_end) break;

                    as_exec_block(env, lines, i + 1, done_line - 1);

                    if (env->error) break;
                    if (env->exit_flag) break;
                    if (env->break_flag) {
                        env->break_flag = 0;
                        break;
                    }
                    env->continue_flag = 0;

                    struct as_value next_val;
                    as_value_init(&next_val, AS_TYPE_INT32, AS_TYPE_VOID);
                    next_val.u.i32 = cur + step;
                    as_value_cast_assign(&iter_var->val, &next_val);

                    int32_t next = as_value_to_i32(&iter_var->val);
                    if ((step > 0 && next <= cur) ||
                        (step < 0 && next >= cur)) {
                        as_errorf(env, "counter '%s' wraps at %d with step %d.",
                                  var_name, cur, step);
                        break;
                    }
                }
                env->loop_depth--;

                i = done_line + 1;
                continue;
            }
        }

        // while: re-evaluate the condition before every pass.
        if (strncmp(stmt, "while ", 6) == 0) {
            int done_line = as_find_matching_done(lines, end + 1, i);
            if (done_line < 0) {
                as_errorf(env,
                          "'while' without matching 'endwhile' (or 'done').");
                return -1;
            }

            const char* wp = stmt + 6;
            while (*wp == ' ') wp++;

            char cond_expr[AS_LINE_MAX];
            int c_len = 0;
            while (*wp != '\0' && !as_word_is(wp, "do")) {
                if (c_len < (int)sizeof(cond_expr) - 1) {
                    cond_expr[c_len++] = *wp;
                }
                wp++;
            }
            cond_expr[c_len] = '\0';

            env->loop_depth++;
            while (1) {
                struct as_value cond = as_eval_expression(env, cond_expr);
                int truthy = as_value_is_truthy(&cond);
                as_value_free(&cond);
                if (!truthy) break;

                as_exec_block(env, lines, i + 1, done_line - 1);

                if (env->error) break;
                if (env->exit_flag) break;
                if (env->break_flag) {
                    env->break_flag = 0;
                    break;
                }
                env->continue_flag = 0;
            }
            env->loop_depth--;

            i = done_line + 1;
            continue;
        }

        // if / else if / else: walk the branches until one runs.
        if (strncmp(stmt, "if ", 3) == 0) {
            int done_line = as_find_matching_done(lines, end + 1, i);
            if (done_line < 0) {
                as_errorf(env, "'if' without matching 'endif' (or 'done').");
                return -1;
            }

            int executed = 0;
            int branch_start = i;
            int depth = 1;

            for (int k = i + 1; k <= done_line; k++) {
                if (k < done_line) {
                    const char* kt = lines[k].text;
                    while (*kt == ' ') kt++;
                    if (as_is_block_start(kt)) {
                        depth++;
                    } else if (as_is_block_end(kt)) {
                        depth--;
                    }
                }

                if (depth == 1 && (k == done_line ||
                                   strncmp(lines[k].text, "else if ", 8) == 0 ||
                                   strncmp(lines[k].text, "elif ", 5) == 0 ||
                                   strcmp(lines[k].text, "else") == 0 ||
                                   strcmp(lines[k].text, "else do") == 0 ||
                                   strcmp(lines[k].text, "else:") == 0)) {
                    if (!executed) {
                        const char* bt = lines[branch_start].text;
                        while (*bt == ' ') bt++;
                        int run_branch = 0;

                        if (strncmp(bt, "if ", 3) == 0 ||
                            strncmp(bt, "else if ", 8) == 0 ||
                            strncmp(bt, "elif ", 5) == 0) {
                            const char* cond_ptr =
                                bt +
                                (bt[0] == 'i' ? 3 : (bt[2] == 's' ? 8 : 5));
                            while (*cond_ptr == ' ') cond_ptr++;
                            char cond_buf[AS_LINE_MAX];
                            int cb_len = 0;
                            while (*cond_ptr != '\0' &&
                                   !as_word_is(cond_ptr, "do") &&
                                   !as_word_is(cond_ptr, "then")) {
                                if (cb_len < (int)sizeof(cond_buf) - 1) {
                                    cond_buf[cb_len++] = *cond_ptr;
                                }
                                cond_ptr++;
                            }
                            cond_buf[cb_len] = '\0';
                            struct as_value c_val =
                                as_eval_expression(env, cond_buf);
                            run_branch = as_value_is_truthy(&c_val);
                            as_value_free(&c_val);
                        } else {
                            run_branch = 1;
                        }

                        if (run_branch) {
                            as_exec_block(env, lines, branch_start + 1, k - 1);
                            executed = 1;
                            if (env->error) {
                                return -1;
                            }
                        }
                    }

                    branch_start = k;
                }
            }

            i = done_line + 1;
            continue;
        }

        // A bare `name = ...` is an assignment; anything else is left to the
        // command dispatcher.
        const char* eq_ptr = strchr(stmt, '=');
        if (eq_ptr != 0 && eq_ptr != stmt && *(eq_ptr - 1) != '=' &&
            *(eq_ptr - 1) != '!' && *(eq_ptr - 1) != '<' &&
            *(eq_ptr - 1) != '>' && *(eq_ptr + 1) != '=') {
            as_exec_assignment(env, stmt);
            i++;
            continue;
        }

        as_exec_system_cmd(env, stmt);
        i++;
    }

    return 0;
}

int as_exec_block(struct as_env* env, struct as_line* lines, int start,
                  int end) {
    int result;

    // Checked here as well as between statements so an empty loop body still
    // notices Ctrl+E on every turn.
    if (adascript_interrupt_requested()) {
        env->exit_flag = 1;
        return 0;
    }

    if (env->block_depth >= AS_MAX_BLOCK_DEPTH) {
        as_errorf(env, "blocks nest deeper than %d levels.",
                  AS_MAX_BLOCK_DEPTH);
        return -1;
    }

    env->block_depth++;
    result = as_exec_block_inner(env, lines, start, end);
    env->block_depth--;

    return result;
}
