#include "adascript.h"

#include <stdint.h>

#include "as_common.h"
#include "as_env.h"
#include "as_error.h"
#include "as_interp.h"
#include "as_screen.h"
#include "as_value.h"
#include "fs.h"
#include "keyboard.h"
#include "kmalloc.h"
#include "libc.h"
#include "screen.h"

#define AS_MAX_RUN_DEPTH 8

static int as_run_depth = 0;

int adascript_is_running(void) { return as_run_depth > 0; }

int adascript_interrupt_requested(void) {
    return as_run_depth > 0 && keyboard_ctrl_e_seen();
}

int adascript_run_string(const char* script) {
    if (script == 0) {
        return 0;
    }

    if (as_run_depth >= AS_MAX_RUN_DEPTH) {
        print("Error: scripts are nested too deeply (does one run itself?).\n");
        return -1;
    }

    if (as_run_depth == 0) {
        keyboard_clear_ctrl_e();
    }
    as_run_depth++;

    int line_count = 0;
    for (int i = 0; script[i] != '\0'; i++) {
        if (script[i] == '\n') {
            line_count++;
        }
    }
    line_count += 2;

    struct as_line* lines =
        (struct as_line*)kmalloc(line_count * sizeof(struct as_line));
    if (lines == 0) {
        print("Error: out of memory.\n");
        as_run_depth--;
        return -1;
    }

    char* script_copy = as_strdup(script);
    if (script_copy == 0) {
        kfree(lines);
        print("Error: out of memory.\n");
        as_run_depth--;
        return -1;
    }

    int total = 0;
    int curr_orig_line = 1;
    char* cur = script_copy;

    while (*cur != '\0') {
        char* raw_start = cur;
        char* line_start = cur;
        while (*cur != '\0' && *cur != '\n') {
            cur++;
        }
        if (*cur == '\n') {
            *cur = '\0';
            cur++;
        }

        int in_q = 0;
        int escaped = 0;
        for (int j = 0; line_start[j] != '\0'; j++) {
            if (escaped) {
                escaped = 0;
                continue;
            }
            if (in_q && line_start[j] == '\\') {
                escaped = 1;
                continue;
            }
            if (line_start[j] == '"') {
                in_q = !in_q;
            } else if (!in_q) {
                int word_start = (j == 0 || line_start[j - 1] == ' ' ||
                                  line_start[j - 1] == '\t');
                if (word_start &&
                    (line_start[j] == '#' ||
                     (line_start[j] == '/' && line_start[j + 1] == '/') ||
                     (line_start[j] == '-' && line_start[j + 1] == '-'))) {
                    line_start[j] = '\0';
                    break;
                }
            }
        }

        int indent = 0;
        while (*line_start == ' ' || *line_start == '\t') {
            if (*line_start == ' ')
                indent++;
            else
                indent += 4;
            line_start++;
        }

        int len = (int)strlen(line_start);
        while (len > 0 &&
               (line_start[len - 1] == ' ' || line_start[len - 1] == '\r' ||
                line_start[len - 1] == '\t')) {
            line_start[--len] = '\0';
        }

        if (len > 0 && line_start[len - 1] == ';') {
            line_start[--len] = '\0';
            while (len > 0 && (line_start[len - 1] == ' ' ||
                               line_start[len - 1] == '\t')) {
                line_start[--len] = '\0';
            }
        }

        if (len > 0) {
            lines[total].text = line_start;
            lines[total].orig_line = curr_orig_line;
            lines[total].orig_col = (int)(line_start - raw_start) + 1;
            lines[total].indent = indent;
            total++;
        }

        curr_orig_line++;
    }

    struct as_env env;
    env.vars = 0;
    env.error = 0;
    env.exit_flag = 0;
    env.break_flag = 0;
    env.continue_flag = 0;
    env.loop_depth = 0;
    env.block_depth = 0;
    env.error_line = 0;
    env.error_col = 0;
    env.error_text[0] = '\0';
    env.cur_line = 0;
    env.cur_col = 0;
    env.script_mode = AS_MODE_NONE;
    env.screen_mode = 0;
    env.saved_cells = 0;
    env.saved_row = 0;
    env.saved_col = 0;
    env.saved_top = 0;

    int result = 0;
    enum as_script_mode declared = AS_MODE_NONE;
    const char* mode_name = 0;
    int mode_line =
        total > 0 ? as_parse_mode_line(lines[0].text, &declared, &mode_name)
                  : 0;

    if (mode_line == 0) {
        env.cur_line = total > 0 ? lines[0].orig_line : 1;
        env.cur_col = total > 0 ? lines[0].orig_col : 1;
        as_errorf(&env,
                  "a script must declare 'mode screen' or 'mode cli' on its "
                  "first line.");
        result = -1;
    } else if (mode_line < 0) {
        env.cur_line = lines[0].orig_line;
        env.cur_col = lines[0].orig_col;
        if (mode_name != 0 && mode_name[0] != '\0') {
            as_errorf(&env, "unknown mode '%s'; use 'screen' or 'cli'.",
                      mode_name);
        } else {
            as_errorf(&env, "mode needs a name: 'screen' or 'cli'.");
        }
        result = -1;
    } else {
        env.script_mode = declared;

        if (as_check_script_mode(&env, lines, total) == 0) {
            result = as_exec_block(&env, lines, 0, total - 1);
            as_screen_finish(&env);
        } else {
            result = -1;
        }
    }

    as_env_cleanup(&env);
    kfree(script_copy);
    kfree(lines);
    as_run_depth--;

    if (as_run_depth == 0 && env.script_mode == AS_MODE_CLI &&
        keyboard_ctrl_e_seen()) {
        print(" ^E (Abort)\n");
        keyboard_clear_ctrl_e();
    }

    return result == 0 && env.error == 0 ? 0 : -1;
}

int adascript_run_file(const char* path) {
    struct fs_node* node = fs_resolve(path);
    if (node == 0) {
        return -1;
    }
    if (node->kind == FS_DIR) {
        print("Error: that path is a directory.\n");
        return -1;
    }

    return adascript_run_string(node->data);
}
