#include "as_error.h"

#include <stdarg.h>

#include "kprintf.h"
#include "libc.h"

/* A small formatter for error messages: %s, %d, %u, %c, %x and %%. */
static void as_format_text(char* out, int max, const char* fmt, va_list args) {
    int pos = 0;

    if (max <= 0) {
        return;
    }

    for (int i = 0; fmt[i] != '\0'; i++) {
        char tmp[32];
        const char* text = 0;

        if (fmt[i] != '%') {
            if (pos < max - 1) {
                out[pos++] = fmt[i];
            }
            continue;
        }

        i++;
        if (fmt[i] == '\0') {
            break;
        }

        switch (fmt[i]) {
            case 's':
                text = va_arg(args, const char*);
                if (text == 0) {
                    text = "(null)";
                }
                break;
            case 'd':
                itoa(va_arg(args, int), tmp);
                text = tmp;
                break;
            case 'u':
                utoa(va_arg(args, unsigned int), tmp);
                text = tmp;
                break;
            case 'c':
                tmp[0] = (char)va_arg(args, int);
                tmp[1] = '\0';
                text = tmp;
                break;
            case 'x': {
                unsigned int value = va_arg(args, unsigned int);
                int digits = 0;

                do {
                    tmp[digits++] = "0123456789abcdef"[value & 0xF];
                    value >>= 4;
                } while (value != 0 && digits < (int)sizeof(tmp));

                for (int j = 0; j < digits / 2; j++) {
                    char swap = tmp[j];
                    tmp[j] = tmp[digits - 1 - j];
                    tmp[digits - 1 - j] = swap;
                }
                tmp[digits] = '\0';
                text = tmp;
                break;
            }
            case '%':
                tmp[0] = '%';
                tmp[1] = '\0';
                text = tmp;
                break;
            default:
                tmp[0] = '%';
                tmp[1] = fmt[i];
                tmp[2] = '\0';
                text = tmp;
                break;
        }

        for (int j = 0; text[j] != '\0' && pos < max - 1; j++) {
            out[pos++] = text[j];
        }
    }

    out[pos] = '\0';
}

/* Stop the script on an error, showing where it happened. */
void as_errorf(struct as_env* env, const char* fmt, ...) {
    va_list args;

    if (env->error) {
        return;
    }

    va_start(args, fmt);
    as_format_text(env->error_text, sizeof(env->error_text), fmt, args);
    va_end(args);

    env->error = 1;
    env->error_line = env->cur_line > 0 ? env->cur_line : 1;
    env->error_col = env->cur_col > 0 ? env->cur_col : 1;

    kprintf("Error: An unexpected error at line %d, column %d\n",
            env->error_line, env->error_col);
    kprintf("Exception: %s\n", env->error_text);
}

/* Turn a command's captured "Error: ..." line into a script error. */
void as_error_from_command(struct as_env* env, const char* output) {
    char message[AS_LINE_MAX];
    const char* p = output != 0 ? output : "";
    int n = 0;

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }

    if (strncmp(p, "Error:", 6) == 0) {
        p += 6;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
    }

    while (*p != '\0' && *p != '\n' && *p != '\r' &&
           n < (int)sizeof(message) - 1) {
        message[n++] = *p++;
    }

    while (n > 0 && (message[n - 1] == ' ' || message[n - 1] == '\t')) {
        n--;
    }
    message[n] = '\0';

    if (n == 0) {
        as_errorf(env, "command failed.");
    } else {
        as_errorf(env, "%s", message);
    }
}
