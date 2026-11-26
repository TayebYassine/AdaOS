#include "as_input.h"

#include "adascript.h"
#include "as_env.h"
#include "as_error.h"
#include "as_value.h"
#include "kprintf.h"
#include "libc.h"
#include "screen.h"
#include "shell.h"

static void as_type_number_bounds(enum as_type t, int32_t* lo, uint32_t* hi) {
    switch (t) {
        case AS_TYPE_INT8:
            *lo = -128;
            *hi = 127;
            break;
        case AS_TYPE_UINT8:
            *lo = 0;
            *hi = 255;
            break;
        case AS_TYPE_INT16:
            *lo = -32768;
            *hi = 32767;
            break;
        case AS_TYPE_UINT16:
            *lo = 0;
            *hi = 65535;
            break;
        case AS_TYPE_INT32:
            *lo = -2147483647 - 1;
            *hi = 2147483647u;
            break;
        case AS_TYPE_UINT32:
            *lo = 0;
            *hi = 0xFFFFFFFFu;
            break;
        case AS_TYPE_BOOL:
            *lo = 0;
            *hi = 1;
            break;
        default:
            *lo = 0;
            *hi = 0;
            break;
    }
}

static int as_parse_whole_number(const char* text, int* negative,
                                 uint32_t* magnitude) {
    const char* s = text;
    uint32_t value = 0;
    int neg = 0;

    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == '+' || *s == '-') {
        neg = (*s == '-');
        s++;
    }
    if (*s < '0' || *s > '9') {
        return 0;
    }
    while (*s >= '0' && *s <= '9') {
        uint32_t digit = (uint32_t)(*s - '0');
        if (value > (0xFFFFFFFFu - digit) / 10u) {
            return 0;
        }
        value = value * 10u + digit;
        s++;
    }
    while (*s == ' ' || *s == '\t' || *s == '\r') {
        s++;
    }
    if (*s != '\0') {
        return 0;
    }

    *negative = neg && value != 0;
    *magnitude = value;
    return 1;
}

/*
 * Explain what input expects, then let the user try again. The message ends
 * without a newline so the next attempt is typed on this same line.
 */
static void as_input_expect(enum as_type type, int32_t lo, uint32_t hi,
                            uint32_t min_len, uint32_t max_len) {
    char low[16];
    char high[16];

    if (type == AS_TYPE_STRING) {
        char min_buf[16];
        char max_buf[16];
        utoa(min_len, min_buf);
        utoa(max_len, max_buf);

        if (min_len == 0) {
            kprintf(
                "Error: Invalid input. Use at most %s characters. Try again. ",
                max_buf);
        } else if (min_len == max_len) {
            kprintf(
                "Error: Invalid input. Use exactly %s characters. Try again. ",
                min_buf);
        } else {
            kprintf(
                "Error: Invalid input. Use %s to %s characters. Try again. ",
                min_buf, max_buf);
        }
        return;
    }

    itoa(lo, low);
    utoa(hi, high);
    kprintf("Error: Invalid input. Enter a number from %s to %s. Try again. ",
            low, high);
}

void as_exec_input(struct as_env* env, const char* stmt) {
    const char* p = stmt + 5;
    while (*p == ' ' || *p == '\t') p++;

    char prompt[64] = "";
    if (*p == '"') {
        p++;
        int pr_len = 0;
        while (*p != '\0' && *p != '"') {
            if (pr_len < (int)sizeof(prompt) - 1) {
                prompt[pr_len++] = *p;
            }
            p++;
        }
        prompt[pr_len] = '\0';
        if (*p == '"') p++;
        while (*p == ' ' || *p == ',') p++;
    }

    char var_name[AS_NAME_MAX];
    int v_len = 0;
    int name_too_long = 0;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        if (v_len < (int)sizeof(var_name) - 1) {
            var_name[v_len++] = *p;
        } else {
            name_too_long = 1;
        }
        p++;
    }
    var_name[v_len] = '\0';

    if (var_name[0] == '\0') {
        as_errorf(env, "input needs a variable name.");
        return;
    }
    // A name cut short here would define a different variable than the one
    // the script wrote, so refuse it the way "define" does.
    if (name_too_long) {
        as_errorf(env, "a variable name is longer than %d characters.",
                  AS_NAME_MAX - 1);
        return;
    }

    struct as_var* var = as_env_lookup(env, var_name);
    enum as_type type = AS_TYPE_STRING;

    if (var != 0) {
        if (var->val.type == AS_TYPE_ARRAY) {
            as_errorf(env, "input cannot fill the array '%s'.", var_name);
            return;
        }
        if (var->val.type != AS_TYPE_VOID) {
            type = var->val.type;
        }
    }

    int typeof_given = 0;
    int has_min = 0;
    int min_negative = 0;
    uint32_t min_magnitude = 0;
    int has_max = 0;
    int max_negative = 0;
    uint32_t max_magnitude = 0;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') {
            break;
        }

        const char* word = p;
        int word_len = 0;
        while (p[word_len] != '\0' && p[word_len] != ' ' &&
               p[word_len] != '\t') {
            word_len++;
        }
        p += word_len;

        if (word_len == 6 && strncmp(word, "typeof", 6) == 0) {
            while (*p == ' ' || *p == '\t') p++;

            char type_str[32];
            int t_len = 0;
            while (*p != '\0' && *p != ' ' && *p != '\t') {
                if (t_len < (int)sizeof(type_str) - 1) {
                    type_str[t_len++] = *p;
                }
                p++;
            }
            type_str[t_len] = '\0';

            enum as_type parsed_type;
            enum as_type parsed_elem;
            if (!as_parse_type(type_str, &parsed_type, &parsed_elem) ||
                parsed_type == AS_TYPE_ARRAY) {
                as_errorf(env, "unknown input type '%s'.", type_str);
                return;
            }
            type = parsed_type;
            typeof_given = 1;
            continue;
        }

        if (word_len == 5 && strncmp(word, "range", 5) == 0) {
            int count = 0;

            for (;;) {
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '\0') {
                    break;
                }

                const char* num_start = p;
                int num_len = 0;
                while (p[num_len] != '\0' && p[num_len] != ' ' &&
                       p[num_len] != '\t') {
                    num_len++;
                }

                if (num_len == 6 && strncmp(num_start, "typeof", 6) == 0) {
                    break;
                }
                if (num_len == 5 && strncmp(num_start, "range", 5) == 0) {
                    break;
                }

                p += num_len;
                if (count >= 2) {
                    as_errorf(env, "input range takes at most two numbers.");
                    return;
                }

                char num_buf[32];
                if (num_len >= (int)sizeof(num_buf)) {
                    as_errorf(env, "input range value is too long.");
                    return;
                }
                memcpy(num_buf, num_start, num_len);
                num_buf[num_len] = '\0';

                int negative;
                uint32_t magnitude;
                if (!as_parse_whole_number(num_buf, &negative, &magnitude)) {
                    as_errorf(env, "input range '%s' is not a number.",
                              num_buf);
                    return;
                }

                if (count == 0) {
                    has_min = 1;
                    min_negative = negative;
                    min_magnitude = magnitude;
                } else {
                    has_max = 1;
                    max_negative = negative;
                    max_magnitude = magnitude;
                }
                count++;
            }
            continue;
        }

        char shown[32];
        int shown_len = word_len < (int)sizeof(shown) - 1
                            ? word_len
                            : (int)sizeof(shown) - 1;
        memcpy(shown, word, shown_len);
        shown[shown_len] = '\0';
        as_errorf(env, "unexpected word '%s' after input.", shown);
        return;
    }

    int32_t lo = 0;
    uint32_t hi = 0;
    uint32_t min_len = 0;
    uint32_t max_len = SHELL_LINE_MAX - 1;

    if (type == AS_TYPE_STRING) {
        if (has_min) {
            if (min_negative) {
                as_errorf(env, "input range for '%s' is invalid.", var_name);
                return;
            }
            min_len = min_magnitude;
        }
        if (has_max) {
            if (max_negative) {
                as_errorf(env, "input range for '%s' is invalid.", var_name);
                return;
            }
            max_len = max_magnitude;
        }
        if (max_len > (uint32_t)(SHELL_LINE_MAX - 1)) {
            max_len = SHELL_LINE_MAX - 1;
        }
        if (min_len > max_len) {
            as_errorf(env, "input range for '%s' is invalid.", var_name);
            return;
        }
    } else {
        as_type_number_bounds(type, &lo, &hi);

        if (has_min) {
            int32_t candidate;
            if (min_negative) {
                if (min_magnitude > 2147483648u) {
                    as_errorf(env, "input range for '%s' is invalid.",
                              var_name);
                    return;
                }
                candidate = min_magnitude == 2147483648u
                                ? (-2147483647 - 1)
                                : -(int32_t)min_magnitude;
            } else {
                if (min_magnitude > 2147483647u) {
                    as_errorf(env, "input range for '%s' is invalid.",
                              var_name);
                    return;
                }
                candidate = (int32_t)min_magnitude;
            }
            if (candidate > lo) {
                lo = candidate;
            }
        }

        if (has_max) {
            if (max_negative) {
                as_errorf(env, "input range for '%s' is invalid.", var_name);
                return;
            }
            if (max_magnitude < hi) {
                hi = max_magnitude;
            }
        }

        if (lo >= 0 && (uint32_t)lo > hi) {
            as_errorf(env, "input range for '%s' is invalid.", var_name);
            return;
        }
    }

    // typeof is the type the line is read as, so the variable has to hold
    // that same type once input stores the value.
    if (typeof_given && (var == 0 || var->val.type != type)) {
        var = as_env_define(env, var_name, type, AS_TYPE_VOID);
        if (var == 0) {
            as_errorf(env, "cannot define variable '%s'.", var_name);
            return;
        }
    }

    char input_line[SHELL_LINE_MAX];
    int negative = 0;
    uint32_t magnitude = 0;

    if (prompt[0] != '\0') {
        print(prompt);
    }

    for (;;) {
        shell_read_line(input_line);

        if (adascript_interrupt_requested()) {
            return;
        }

        if (type == AS_TYPE_STRING) {
            int len = (int)strlen(input_line);
            if (len >= (int)min_len && (uint32_t)len <= max_len) {
                break;
            }
            as_input_expect(type, lo, hi, min_len, max_len);
            continue;
        }

        if (!as_parse_whole_number(input_line, &negative, &magnitude)) {
            as_input_expect(type, lo, hi, min_len, max_len);
            continue;
        }

        if (negative) {
            if (lo >= 0) {
                as_input_expect(type, lo, hi, min_len, max_len);
                continue;
            }
            uint32_t limit = (uint32_t)(-(lo + 1)) + 1u;
            if (magnitude > limit) {
                as_input_expect(type, lo, hi, min_len, max_len);
                continue;
            }
        } else if (magnitude > hi) {
            as_input_expect(type, lo, hi, min_len, max_len);
            continue;
        }
        break;
    }

    struct as_value v;
    as_value_init(&v, type, AS_TYPE_VOID);
    v.is_null = 0;

    if (type == AS_TYPE_STRING) {
        v.u.str = as_strdup(input_line);
        v.is_null = v.u.str == 0;
    } else if (type == AS_TYPE_BOOL) {
        v.u.boolean = magnitude != 0;
    } else if (type == AS_TYPE_UINT8 || type == AS_TYPE_UINT16 ||
               type == AS_TYPE_UINT32) {
        v.u.u32 = magnitude;
    } else if (negative) {
        v.u.i32 =
            magnitude == 2147483648u ? (-2147483647 - 1) : -(int32_t)magnitude;
    } else {
        v.u.i32 = (int32_t)magnitude;
    }

    if (var == 0) {
        var = as_env_define(env, var_name, type, AS_TYPE_VOID);
        if (var == 0) {
            as_value_free(&v);
            as_errorf(env, "cannot define variable '%s'.", var_name);
            return;
        }
    }

    as_value_cast_assign(&var->val, &v);
    as_value_free(&v);
}
