#include "adascript.h"

#include <stdint.h>

#include "as_common.h"
#include "as_env.h"
#include "as_interp.h"
#include "as_value.h"
#include "fs.h"
#include "kmalloc.h"
#include "libc.h"
#include "screen.h"

/* How deeply scripts may run other scripts. */
#define AS_MAX_RUN_DEPTH 8

static int as_run_depth = 0;

/* Run an AdaScript string. */
int adascript_run_string(const char* script) {
    if (script == 0 || script[0] == '\0') {
        return 0;
    }

    if (as_run_depth >= AS_MAX_RUN_DEPTH) {
        print("Error: scripts are nested too deeply (does one run itself?).\n");
        return -1;
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
    env.error_line = 0;
    env.error_col = 0;
    env.error_text[0] = '\0';
    env.cur_line = 0;
    env.cur_col = 0;

    int result = as_exec_block(&env, lines, 0, total - 1);

    as_env_cleanup(&env);
    kfree(script_copy);
    kfree(lines);
    as_run_depth--;

    return result == 0 && env.error == 0 ? 0 : -1;
}

/* Run an AdaScript file at the given filesystem path. */
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
