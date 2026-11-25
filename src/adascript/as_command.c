#include "as_command.h"

#include "as_error.h"
#include "as_parser.h"
#include "as_value.h"
#include "command.h"
#include "kmalloc.h"
#include "libc.h"
#include "screen.h"

/* Command results are strings, so they should not end in a newline. */
static void as_trim_command_output(char* text) {
    int len;

    if (text == 0) {
        return;
    }

    len = (int)strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        text[--len] = '\0';
    }
}

/* Run an output command and hand back its text as a string value. */
int as_run_command_value(struct as_env* env, const char* command,
                         struct as_value* out) {
    char* output = 0;

    if (!command_run_capture(command, &output)) {
        as_error_from_command(env, output);
        kfree(output);
        return 0;
    }

    as_trim_command_output(output);

    as_value_init(out, AS_TYPE_STRING, AS_TYPE_VOID);
    out->u.str = output != 0 ? output : as_strdup("");
    out->is_null = out->u.str == 0;
    return 1;
}

/* Check if a command is disallowed in AdaScript. */
static int as_is_disallowed_command(const char* cmd) {
    return (strcmp(cmd, "snake") == 0 || strcmp(cmd, "tetris") == 0 ||
            strcmp(cmd, "write") == 0 || strcmp(cmd, "reboot") == 0 ||
            strcmp(cmd, "shutdown") == 0 || strcmp(cmd, "settime") == 0 ||
            strcmp(cmd, "setdate") == 0);
}

/* Commands that take one path or name, and so can take an expression too. */
static int as_command_takes_path(const char* cmd) {
    return strcmp(cmd, "cd") == 0 || strcmp(cmd, "cat") == 0 ||
           strcmp(cmd, "ls") == 0 || strcmp(cmd, "tree") == 0 ||
           strcmp(cmd, "mkdir") == 0 || strcmp(cmd, "rm") == 0 ||
           strcmp(cmd, "find") == 0 || strcmp(cmd, "run") == 0;
}

/* Is the argument an expression? Yes when it starts with an output command,
 * as in  cd pwd + "/x" . */
static int as_arg_is_expression(const char* rest) {
    char word[32];
    int len = 0;

    while (*rest == ' ' || *rest == '\t') {
        rest++;
    }

    if (*rest == '(') {
        return 1;
    }
    if (*rest == '"' || *rest == '\0') {
        return 0;
    }

    while (*rest != '\0' && *rest != ' ' && *rest != '\t' && *rest != '+' &&
           *rest != '-' && *rest != '*' && *rest != '/' && *rest != ';' &&
           len < (int)sizeof(word) - 1) {
        word[len++] = *rest++;
    }
    word[len] = '\0';

    return command_returns_output(word);
}

/* Build  command "value"  so the shell splitter keeps the value together. */
static void as_make_command_value(char* out, int max, const char* cmd,
                                  const char* value) {
    int pos = 0;

    for (int i = 0; cmd[i] != '\0' && pos < max - 1; i++) {
        out[pos++] = cmd[i];
    }
    if (pos < max - 1) {
        out[pos++] = ' ';
    }
    if (pos < max - 1) {
        out[pos++] = '"';
    }
    for (int i = 0; value[i] != '\0' && pos < max - 1; i++) {
        out[pos++] = value[i] == '"' ? ' ' : value[i];
    }
    if (pos < max - 1) {
        out[pos++] = '"';
    }
    out[pos] = '\0';
}

/* Execute a built-in OS command from AdaScript with variable interpolation. */
void as_exec_system_cmd(struct as_env* env, const char* line) {
    char expanded[AS_LINE_MAX];
    int exp_pos = 0;
    int i = 0;

    while (line[i] == ' ') i++;
    char cmd[32];
    int cmd_len = 0;
    while (line[i] != '\0' && line[i] != ' ' && line[i] != ';') {
        if (cmd_len < (int)sizeof(cmd) - 1) {
            cmd[cmd_len++] = line[i];
        }
        i++;
    }
    cmd[cmd_len] = '\0';

    if (as_is_disallowed_command(cmd)) {
        as_errorf(env, "command '%s' cannot be run in AdaScript.", cmd);
        return;
    }

    const char* rest = line + i;
    while (*rest == ' ' || *rest == '\t') rest++;

    if (as_command_takes_path(cmd) && as_arg_is_expression(rest)) {
        struct as_value val = as_eval_expression(env, rest);
        char value[AS_LINE_MAX];

        if (env->error) {
            as_value_free(&val);
            return;
        }

        as_value_format(&val, value, sizeof(value), 0);
        as_value_free(&val);
        as_make_command_value(expanded, sizeof(expanded), cmd, value);
    } else {
        memcpy(expanded, cmd, cmd_len);
        exp_pos = cmd_len;

        while (line[i] != '\0' && line[i] != ';') {
            if (line[i] == ' ') {
                if (exp_pos < (int)sizeof(expanded) - 1) {
                    expanded[exp_pos++] = ' ';
                }
                i++;
                continue;
            }

            if (line[i] == '"') {
                i++;
                while (line[i] != '\0' && line[i] != '"') {
                    if (exp_pos < (int)sizeof(expanded) - 1) {
                        expanded[exp_pos++] = line[i];
                    }
                    i++;
                }
                if (line[i] == '"') i++;
                continue;
            }

            char word[AS_LINE_MAX];
            int w_len = 0;
            int has_dollar = (line[i] == '$');
            if (has_dollar) i++;

            while (line[i] != '\0' && line[i] != ' ' && line[i] != ';') {
                if (w_len < (int)sizeof(word) - 1) {
                    word[w_len++] = line[i];
                }
                i++;
            }
            word[w_len] = '\0';

            struct as_var* var = has_dollar ? as_env_lookup(env, word) : 0;
            if (var != 0) {
                char val_buf[AS_LINE_MAX];
                as_value_format(&var->val, val_buf, sizeof(val_buf), 0);
                int v_len = (int)strlen(val_buf);
                if (exp_pos + v_len < (int)sizeof(expanded) - 1) {
                    memcpy(expanded + exp_pos, val_buf, v_len);
                    exp_pos += v_len;
                }
            } else {
                if (has_dollar && exp_pos < (int)sizeof(expanded) - 1) {
                    expanded[exp_pos++] = '$';
                }
                if (exp_pos + w_len < (int)sizeof(expanded) - 1) {
                    memcpy(expanded + exp_pos, word, w_len);
                    exp_pos += w_len;
                }
            }
        }

        expanded[exp_pos] = '\0';
    }

    char* output = 0;
    int ok = command_run_capture(expanded, &output);

    if (!ok) {
        as_error_from_command(env, output);
        kfree(output);
        return;
    }

    if (output != 0 && output[0] != '\0') {
        int len = (int)strlen(output);

        print(output);
        if (len == 0 || output[len - 1] != '\n') {
            print_char('\n');
        }
    }

    kfree(output);
}
