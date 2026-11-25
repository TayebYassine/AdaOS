#include "adascript.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "command.h"
#include "fs.h"
#include "kmalloc.h"
#include "kprintf.h"
#include "libc.h"
#include "screen.h"
#include "shell.h"
#include "timer.h"

#define AS_NAME_MAX 32
#define AS_LINE_MAX 256
#define AS_ARRAY_MAX 256
#define AS_MAX_RUN_DEPTH 8

static int as_run_depth = 0;

/* AdaScript data types. */
enum as_type {
    AS_TYPE_VOID,
    AS_TYPE_INT8,
    AS_TYPE_UINT8,
    AS_TYPE_INT16,
    AS_TYPE_UINT16,
    AS_TYPE_INT32,
    AS_TYPE_UINT32,
    AS_TYPE_STRING,
    AS_TYPE_BOOL,
    AS_TYPE_ARRAY,
};

/* Value container supporting primitives, strings, and arrays. */
struct as_value {
    enum as_type type;
    enum as_type elem_type;  // Base type when type is AS_TYPE_ARRAY
    int is_null;             // 1 when the value is null
    union {
        int32_t i32;   // signed types
        uint32_t u32;  // unsigned types
        char* str;     // AS_TYPE_STRING, heap owned
        int boolean;   // AS_TYPE_BOOL
        struct {
            struct as_value* items;  // heap array of values
            int count;               // items in use
            int capacity;            // items allocated
        } arr;
    } u;
};

/* One variable in RAM. */
struct as_var {
    char name[AS_NAME_MAX];
    struct as_value val;
    struct as_var* next;  // next variable in the environment
};

/* Execution environment and symbol table. */
struct as_env {
    struct as_var* vars;  // symbol table, newest first
    int error;
    int exit_flag;
    int break_flag;
    int continue_flag;
    int loop_depth;
    int error_line;  // where the error happened
    int error_col;
    char error_text[AS_LINE_MAX];  // and how to explain it
    int cur_line;                  // the statement being run, kept up
    int cur_col;                   // to date by as_exec_block()
};

/* One parsed line in the script. */
struct as_line {
    char* text;     // the line, leading spaces removed
    int orig_line;  // line number in the original file
    int orig_col;   // column the text starts at
    int indent;     // leading spaces, used to build blocks
};

/* Forward declarations. */
static void as_value_free(struct as_value* val);
static void as_value_copy(struct as_value* dst, const struct as_value* src);
static void as_value_copy_raw(struct as_value* dst, const struct as_value* src);
static void as_value_format(const struct as_value* val, char* buf, int max,
                            int quote_strings);
static void as_copy_text(char* buf, const char* src, int max);
static int as_value_to_i32(const struct as_value* val);
static uint32_t as_value_to_u32(const struct as_value* val);
static int as_value_is_truthy(const struct as_value* val);
static int as_value_to_bool(const struct as_value* val);
static void as_value_cast_assign(struct as_value* dst,
                                 const struct as_value* src);
static struct as_value as_eval_expression(struct as_env* env, const char* str);
static int as_exec_block(struct as_env* env, struct as_line* lines, int start,
                         int end);

/* Duplicate a string using kernel heap memory. */
static char* as_strdup(const char* s) {
    if (s == 0) {
        return 0;
    }
    size_t len = strlen(s);
    char* copy = (char*)kmalloc(len + 1);
    if (copy != 0) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

/* Initialize a value with defaults. */
static void as_value_init(struct as_value* val, enum as_type type,
                          enum as_type elem_type) {
    val->type = type;
    val->elem_type = elem_type;
    val->is_null = 0;

    switch (type) {
        case AS_TYPE_INT8:
        case AS_TYPE_INT16:
        case AS_TYPE_INT32:
            val->u.i32 = 0;
            break;
        case AS_TYPE_UINT8:
        case AS_TYPE_UINT16:
        case AS_TYPE_UINT32:
            val->u.u32 = 0;
            break;
        case AS_TYPE_BOOL:
            val->u.boolean = 0;
            break;
        case AS_TYPE_STRING:
            val->is_null = 1;
            val->u.str = 0;
            break;
        case AS_TYPE_ARRAY:
            val->u.arr.items = 0;
            val->u.arr.count = 0;
            val->u.arr.capacity = 0;
            break;
        default:
            val->u.i32 = 0;
            break;
    }
}

/* Free any dynamic memory associated with a value. */
static void as_value_free(struct as_value* val) {
    if (val->type == AS_TYPE_STRING) {
        if (val->u.str != 0) {
            kfree(val->u.str);
            val->u.str = 0;
        }
    } else if (val->type == AS_TYPE_ARRAY) {
        if (val->u.arr.items != 0) {
            for (int i = 0; i < val->u.arr.count; i++) {
                as_value_free(&val->u.arr.items[i]);
            }
            kfree(val->u.arr.items);
            val->u.arr.items = 0;
        }
        val->u.arr.count = 0;
        val->u.arr.capacity = 0;
    }
}

/* Deep-copy a value into storage that owns nothing yet.
 * Never frees dst: the caller guarantees dst holds no live memory. */
static void as_value_copy_raw(struct as_value* dst,
                              const struct as_value* src) {
    dst->type = src->type;
    dst->elem_type = src->elem_type;
    dst->is_null = src->is_null;

    if (src->type == AS_TYPE_STRING) {
        dst->u.str = src->u.str ? as_strdup(src->u.str) : 0;
        if (src->u.str != 0 && dst->u.str == 0) {
            dst->is_null = 1;
        }
    } else if (src->type == AS_TYPE_ARRAY) {
        dst->u.arr.items = 0;
        dst->u.arr.count = 0;
        dst->u.arr.capacity = 0;
        if (src->u.arr.count > 0) {
            struct as_value* items = (struct as_value*)kmalloc(
                src->u.arr.count * sizeof(struct as_value));
            if (items == 0) {
                return;
            }
            for (int i = 0; i < src->u.arr.count; i++) {
                as_value_copy_raw(&items[i], &src->u.arr.items[i]);
            }
            dst->u.arr.items = items;
            dst->u.arr.count = src->u.arr.count;
            dst->u.arr.capacity = src->u.arr.count;
        }
    } else {
        dst->u = src->u;
    }
}

/* Deep-copy a value from src to dst, releasing whatever dst held before. */
static void as_value_copy(struct as_value* dst, const struct as_value* src) {
    as_value_free(dst);
    as_value_copy_raw(dst, src);
}

/* Append an item to an array. */
static int as_array_append(struct as_value* arr, const struct as_value* item) {
    if (arr->type != AS_TYPE_ARRAY) {
        return 0;
    }

    if (arr->u.arr.count >= arr->u.arr.capacity) {
        int new_cap = (arr->u.arr.capacity == 0) ? 4 : arr->u.arr.capacity * 2;
        if (new_cap > AS_ARRAY_MAX) {
            new_cap = AS_ARRAY_MAX;
        }
        if (arr->u.arr.count >= new_cap) {
            return 0;
        }

        struct as_value* new_items =
            (struct as_value*)kmalloc(new_cap * sizeof(struct as_value));
        if (new_items == 0) {
            return 0;
        }

        for (int i = 0; i < arr->u.arr.count; i++) {
            new_items[i] = arr->u.arr.items[i];
        }

        if (arr->u.arr.items != 0) {
            kfree(arr->u.arr.items);
        }

        arr->u.arr.items = new_items;
        arr->u.arr.capacity = new_cap;
    }

    if (arr->elem_type == AS_TYPE_VOID ||
        (arr->elem_type == AS_TYPE_INT32 && arr->u.arr.count == 0)) {
        arr->elem_type = item->type;
    }

    as_value_init(&arr->u.arr.items[arr->u.arr.count], arr->elem_type,
                  AS_TYPE_VOID);
    as_value_cast_assign(&arr->u.arr.items[arr->u.arr.count], item);
    arr->u.arr.count++;
    return 1;
}

/* Convert value to signed 32-bit integer. */
static int32_t as_value_to_i32(const struct as_value* val) {
    if (val->is_null) {
        return 0;
    }

    switch (val->type) {
        case AS_TYPE_INT8:
        case AS_TYPE_INT16:
        case AS_TYPE_INT32:
            return val->u.i32;
        case AS_TYPE_UINT8:
        case AS_TYPE_UINT16:
        case AS_TYPE_UINT32:
            return (int32_t)val->u.u32;
        case AS_TYPE_BOOL:
            return val->u.boolean ? 1 : 0;
        case AS_TYPE_STRING:
            return val->u.str ? atoi(val->u.str) : 0;
        default:
            return 0;
    }
}

/* Convert value to unsigned 32-bit integer. */
static uint32_t as_value_to_u32(const struct as_value* val) {
    if (val->is_null) {
        return 0;
    }

    switch (val->type) {
        case AS_TYPE_UINT8:
        case AS_TYPE_UINT16:
        case AS_TYPE_UINT32:
            return val->u.u32;
        case AS_TYPE_INT8:
        case AS_TYPE_INT16:
        case AS_TYPE_INT32:
            return (uint32_t)val->u.i32;
        case AS_TYPE_BOOL:
            return val->u.boolean ? 1 : 0;
        case AS_TYPE_STRING:
            return val->u.str ? (uint32_t)atoi(val->u.str) : 0;
        default:
            return 0;
    }
}

/* Check if a value evaluates to boolean true. */
static int as_value_is_truthy(const struct as_value* val) {
    if (val->is_null) {
        return 0;
    }

    switch (val->type) {
        case AS_TYPE_BOOL:
            return val->u.boolean != 0;
        case AS_TYPE_INT8:
        case AS_TYPE_INT16:
        case AS_TYPE_INT32:
            return val->u.i32 != 0;
        case AS_TYPE_UINT8:
        case AS_TYPE_UINT16:
        case AS_TYPE_UINT32:
            return val->u.u32 != 0;
        case AS_TYPE_STRING:
            return val->u.str != 0 && val->u.str[0] != '\0';
        case AS_TYPE_ARRAY:
            return val->u.arr.count > 0;
        default:
            return 0;
    }
}

/* Case-insensitive string comparison, for the words a bool accepts. */
static int as_str_equals_ci(const char* a, const char* b) {
    int i = 0;

    while (a[i] != '\0' && b[i] != '\0') {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
        i++;
    }

    return a[i] == b[i];
}

/* Convert a value to a boolean. Text is understood, so
 * `define f: bool = "false"` does what it reads like. */
static int as_value_to_bool(const struct as_value* val) {
    if (val->type == AS_TYPE_STRING && val->u.str != 0) {
        const char* s = val->u.str;
        size_t len = strlen(s);

        if (len == 0) {
            return 0;
        }
        if (as_str_equals_ci(s, "false") || as_str_equals_ci(s, "no") ||
            strcmp(s, "0") == 0) {
            return 0;
        }
        if (as_str_equals_ci(s, "true") || as_str_equals_ci(s, "yes") ||
            strcmp(s, "1") == 0) {
            return 1;
        }
    }

    return as_value_is_truthy(val);
}

/* Copy at most max-1 bytes of src into buf and always terminate it. */
static void as_copy_text(char* buf, const char* src, int max) {
    int i = 0;

    if (max <= 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i < max - 1) {
        buf[i] = src[i];
        i++;
    }
    buf[i] = '\0';
}

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
static void as_errorf(struct as_env* env, const char* fmt, ...) {
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
static void as_error_from_command(struct as_env* env, const char* output) {
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
static int as_run_command_value(struct as_env* env, const char* command,
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

/* Format a value into a human-readable text string. */
static void as_value_format(const struct as_value* val, char* buf, int max,
                            int quote_strings) {
    if (max <= 0) {
        return;
    }

    if (val->is_null) {
        as_copy_text(buf, "null", max);
        return;
    }

    switch (val->type) {
        case AS_TYPE_INT8:
        case AS_TYPE_INT16:
        case AS_TYPE_INT32:
            itoa(val->u.i32, buf);
            break;
        case AS_TYPE_UINT8:
        case AS_TYPE_UINT16:
        case AS_TYPE_UINT32:
            utoa(val->u.u32, buf);
            break;
        case AS_TYPE_BOOL:
            as_copy_text(buf, val->u.boolean ? "true" : "false", max);
            break;
        case AS_TYPE_STRING: {
            if (val->u.str == 0) {
                as_copy_text(buf, "null", max);
            } else if (quote_strings) {
                int len = (int)strlen(val->u.str);
                if (len + 3 < max) {
                    buf[0] = '"';
                    memcpy(buf + 1, val->u.str, len);
                    buf[len + 1] = '"';
                    buf[len + 2] = '\0';
                } else {
                    as_copy_text(buf, val->u.str, max);
                }
            } else {
                as_copy_text(buf, val->u.str, max);
            }
            break;
        }
        case AS_TYPE_ARRAY: {
            int pos = 0;
            if (pos < max - 1) {
                buf[pos++] = '[';
            }
            if (pos < max - 1) {
                buf[pos++] = ' ';
            }
            for (int i = 0; i < val->u.arr.count; i++) {
                if (i > 0) {
                    if (pos < max - 2) {
                        buf[pos++] = ',';
                        buf[pos++] = ' ';
                    }
                }
                char item_buf[64];
                as_value_format(&val->u.arr.items[i], item_buf,
                                sizeof(item_buf), 1);
                int item_len = (int)strlen(item_buf);
                if (pos + item_len < max - 2) {
                    memcpy(buf + pos, item_buf, item_len);
                    pos += item_len;
                }
            }
            if (pos < max - 2) {
                buf[pos++] = ' ';
                buf[pos++] = ']';
            }
            buf[pos] = '\0';
            break;
        }
        default:
            buf[0] = '\0';
            break;
    }
}

/* Cast and assign src value into dst variable according to dst's type. */
static void as_value_cast_assign(struct as_value* dst,
                                 const struct as_value* src) {
    if (src->is_null) {
        as_value_free(dst);
        dst->is_null = 1;
        if (dst->type == AS_TYPE_STRING) {
            dst->u.str = 0;
        }
        return;
    }

    dst->is_null = 0;

    switch (dst->type) {
        case AS_TYPE_INT8:
            dst->u.i32 = (int8_t)as_value_to_i32(src);
            break;
        case AS_TYPE_UINT8:
            dst->u.u32 = (uint8_t)as_value_to_u32(src);
            break;
        case AS_TYPE_INT16:
            dst->u.i32 = (int16_t)as_value_to_i32(src);
            break;
        case AS_TYPE_UINT16:
            dst->u.u32 = (uint16_t)as_value_to_u32(src);
            break;
        case AS_TYPE_INT32:
            dst->u.i32 = as_value_to_i32(src);
            break;
        case AS_TYPE_UINT32:
            dst->u.u32 = as_value_to_u32(src);
            break;
        case AS_TYPE_BOOL:
            dst->u.boolean = as_value_to_bool(src);
            break;
        case AS_TYPE_STRING: {
            as_value_free(dst);
            if (src->type == AS_TYPE_STRING) {
                dst->u.str = src->u.str ? as_strdup(src->u.str) : 0;
                dst->is_null = (src->u.str == 0);
            } else {
                char buf[AS_LINE_MAX];
                as_value_format(src, buf, sizeof(buf), 0);
                dst->u.str = as_strdup(buf);
            }
            break;
        }
        case AS_TYPE_ARRAY: {
            if (src->type != AS_TYPE_ARRAY) {
                break;
            }

            enum as_type target_elem = (dst->elem_type != AS_TYPE_VOID)
                                           ? dst->elem_type
                                           : src->elem_type;
            struct as_value* items = 0;

            if (src->u.arr.count > 0) {
                items = (struct as_value*)kmalloc(src->u.arr.count *
                                                  sizeof(struct as_value));
                if (items == 0) {
                    break;
                }
                for (int i = 0; i < src->u.arr.count; i++) {
                    as_value_init(&items[i], target_elem, AS_TYPE_VOID);
                    as_value_cast_assign(&items[i], &src->u.arr.items[i]);
                }
            }

            as_value_free(dst);
            dst->type = AS_TYPE_ARRAY;
            dst->elem_type = target_elem;
            dst->is_null = 0;
            dst->u.arr.items = items;
            dst->u.arr.count = src->u.arr.count;
            dst->u.arr.capacity = src->u.arr.count;
            break;
        }
        default:
            break;
    }
}

/* Find a variable by name in the environment. */
static struct as_var* as_env_lookup(struct as_env* env, const char* name) {
    for (struct as_var* v = env->vars; v != 0; v = v->next) {
        if (strcmp(v->name, name) == 0) {
            return v;
        }
    }
    return 0;
}

/* Register a variable; an existing one is re-initialised, so a `define`
 * inside a loop or an `if` body does not fail on its second pass. */
static struct as_var* as_env_define(struct as_env* env, const char* name,
                                    enum as_type type, enum as_type elem_type) {
    if (name[0] == '\0') {
        return 0;
    }

    struct as_var* existing = as_env_lookup(env, name);
    if (existing != 0) {
        as_value_free(&existing->val);
        as_value_init(&existing->val, type, elem_type);
        return existing;
    }

    struct as_var* var = (struct as_var*)kmalloc(sizeof(struct as_var));
    if (var == 0) {
        return 0;
    }

    strncpy(var->name, name, AS_NAME_MAX - 1);
    var->name[AS_NAME_MAX - 1] = '\0';
    as_value_init(&var->val, type, elem_type);

    var->next = env->vars;
    env->vars = var;
    return var;
}

/* Unregister a variable from RAM and free its memory. */
static int as_env_delete(struct as_env* env, const char* name) {
    struct as_var* prev = 0;
    for (struct as_var* v = env->vars; v != 0; prev = v, v = v->next) {
        if (strcmp(v->name, name) == 0) {
            if (prev != 0) {
                prev->next = v->next;
            } else {
                env->vars = v->next;
            }
            as_value_free(&v->val);
            kfree(v);
            return 1;
        }
    }
    return 0;
}

/* Free all variables remaining in RAM. */
static void as_env_cleanup(struct as_env* env) {
    struct as_var* v = env->vars;
    while (v != 0) {
        struct as_var* next = v->next;
        as_value_free(&v->val);
        kfree(v);
        v = next;
    }
    env->vars = 0;
}

/* Parse type name string like "int8", "string", "uint32[]". */
static int as_parse_type(const char* s, enum as_type* out_type,
                         enum as_type* out_elem) {
    int len = (int)strlen(s);
    int is_arr = 0;
    char base[32];

    if (len > 2 && s[len - 2] == '[' && s[len - 1] == ']') {
        is_arr = 1;
        if (len - 2 >= (int)sizeof(base)) {
            return 0;
        }
        memcpy(base, s, len - 2);
        base[len - 2] = '\0';
    } else {
        if (len >= (int)sizeof(base)) {
            return 0;
        }
        strcpy(base, s);
    }

    enum as_type t;
    if (strcmp(base, "int8") == 0) {
        t = AS_TYPE_INT8;
    } else if (strcmp(base, "uint8") == 0) {
        t = AS_TYPE_UINT8;
    } else if (strcmp(base, "int16") == 0) {
        t = AS_TYPE_INT16;
    } else if (strcmp(base, "uint16") == 0) {
        t = AS_TYPE_UINT16;
    } else if (strcmp(base, "int32") == 0 || strcmp(base, "int") == 0) {
        t = AS_TYPE_INT32;
    } else if (strcmp(base, "uint32") == 0 || strcmp(base, "uint") == 0) {
        t = AS_TYPE_UINT32;
    } else if (strcmp(base, "string") == 0) {
        t = AS_TYPE_STRING;
    } else if (strcmp(base, "bool") == 0) {
        t = AS_TYPE_BOOL;
    } else {
        return 0;
    }

    if (is_arr) {
        *out_type = AS_TYPE_ARRAY;
        *out_elem = t;
    } else {
        *out_type = t;
        *out_elem = AS_TYPE_VOID;
    }

    return 1;
}

/* Tokenizer for expressions. */
enum as_tok_kind {
    TOK_EOF = 0,
    TOK_INT,
    TOK_STRING,
    TOK_IDENT,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NULL,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_LTE,
    TOK_GT,
    TOK_GTE,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
    TOK_LEN,
};

struct as_token {
    enum as_tok_kind kind;
    int32_t int_val;
    char text[AS_LINE_MAX];
};

struct as_lexer {
    const char* src;
    int pos;
    struct as_token cur;
};

/* Advance lexer to next token. */
static void as_lexer_next(struct as_lexer* lex) {
    while (lex->src[lex->pos] == ' ' || lex->src[lex->pos] == '\t' ||
           lex->src[lex->pos] == '\r' || lex->src[lex->pos] == '\n') {
        lex->pos++;
    }

    char c = lex->src[lex->pos];
    if (c == '\0') {
        lex->cur.kind = TOK_EOF;
        lex->cur.text[0] = '\0';
        return;
    }

    // Integer literal; decimal or hex
    if ((c >= '0' && c <= '9') ||
        (c == '0' &&
         (lex->src[lex->pos + 1] == 'x' || lex->src[lex->pos + 1] == 'X'))) {
        int hex = 0;
        int32_t val = 0;

        if (c == '0' &&
            (lex->src[lex->pos + 1] == 'x' || lex->src[lex->pos + 1] == 'X')) {
            hex = 1;
            lex->pos += 2;
        }

        while (1) {
            char ch = lex->src[lex->pos];
            if (ch >= '0' && ch <= '9') {
                val = hex ? (val * 16 + (ch - '0')) : (val * 10 + (ch - '0'));
                lex->pos++;
            } else if (hex && ch >= 'a' && ch <= 'f') {
                val = val * 16 + (ch - 'a' + 10);
                lex->pos++;
            } else if (hex && ch >= 'A' && ch <= 'F') {
                val = val * 16 + (ch - 'A' + 10);
                lex->pos++;
            } else {
                break;
            }
        }

        lex->cur.kind = TOK_INT;
        lex->cur.int_val = val;
        itoa(val, lex->cur.text);
        return;
    }

    // String literal
    if (c == '"') {
        lex->pos++;
        int idx = 0;
        while (lex->src[lex->pos] != '\0' && lex->src[lex->pos] != '"') {
            char ch = lex->src[lex->pos++];
            if (ch == '\\') {
                if (lex->src[lex->pos] == '\0') {
                    if (idx < (int)sizeof(lex->cur.text) - 1) {
                        lex->cur.text[idx++] = ch;
                    }
                    break;
                }
                char esc = lex->src[lex->pos++];
                if (esc == 'n')
                    ch = '\n';
                else if (esc == 't')
                    ch = '\t';
                else if (esc == '"')
                    ch = '"';
                else if (esc == '\\')
                    ch = '\\';
                else
                    ch = esc;
            }
            if (idx < (int)sizeof(lex->cur.text) - 1) {
                lex->cur.text[idx++] = ch;
            }
        }
        if (lex->src[lex->pos] == '"') {
            lex->pos++;
        }
        lex->cur.text[idx] = '\0';
        lex->cur.kind = TOK_STRING;
        return;
    }

    // Identifier or keyword
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        int idx = 0;
        while ((lex->src[lex->pos] >= 'a' && lex->src[lex->pos] <= 'z') ||
               (lex->src[lex->pos] >= 'A' && lex->src[lex->pos] <= 'Z') ||
               (lex->src[lex->pos] >= '0' && lex->src[lex->pos] <= '9') ||
               lex->src[lex->pos] == '_') {
            if (idx < (int)sizeof(lex->cur.text) - 1) {
                lex->cur.text[idx++] = lex->src[lex->pos];
            }
            lex->pos++;
        }
        lex->cur.text[idx] = '\0';

        if (strcmp(lex->cur.text, "true") == 0) {
            lex->cur.kind = TOK_TRUE;
        } else if (strcmp(lex->cur.text, "false") == 0) {
            lex->cur.kind = TOK_FALSE;
        } else if (strcmp(lex->cur.text, "null") == 0) {
            lex->cur.kind = TOK_NULL;
        } else if (strcmp(lex->cur.text, "and") == 0) {
            lex->cur.kind = TOK_AND;
        } else if (strcmp(lex->cur.text, "or") == 0) {
            lex->cur.kind = TOK_OR;
        } else if (strcmp(lex->cur.text, "not") == 0) {
            lex->cur.kind = TOK_NOT;
        } else if (strcmp(lex->cur.text, "len") == 0) {
            lex->cur.kind = TOK_LEN;
        } else {
            lex->cur.kind = TOK_IDENT;
        }
        return;
    }

    // Multi-character operators
    if (c == '=' && lex->src[lex->pos + 1] == '=') {
        lex->pos += 2;
        lex->cur.kind = TOK_EQ;
        return;
    }
    if (c == '!' && lex->src[lex->pos + 1] == '=') {
        lex->pos += 2;
        lex->cur.kind = TOK_NEQ;
        return;
    }
    if (c == '<' && lex->src[lex->pos + 1] == '=') {
        lex->pos += 2;
        lex->cur.kind = TOK_LTE;
        return;
    }
    if (c == '>' && lex->src[lex->pos + 1] == '=') {
        lex->pos += 2;
        lex->cur.kind = TOK_GTE;
        return;
    }
    if (c == '&' && lex->src[lex->pos + 1] == '&') {
        lex->pos += 2;
        lex->cur.kind = TOK_AND;
        return;
    }
    if (c == '|' && lex->src[lex->pos + 1] == '|') {
        lex->pos += 2;
        lex->cur.kind = TOK_OR;
        return;
    }

    // Single-character operators and symbols
    lex->pos++;
    switch (c) {
        case '+':
            lex->cur.kind = TOK_PLUS;
            break;
        case '-':
            lex->cur.kind = TOK_MINUS;
            break;
        case '*':
            lex->cur.kind = TOK_STAR;
            break;
        case '/':
            lex->cur.kind = TOK_SLASH;
            break;
        case '%':
            lex->cur.kind = TOK_PERCENT;
            break;
        case '<':
            lex->cur.kind = TOK_LT;
            break;
        case '>':
            lex->cur.kind = TOK_GT;
            break;
        case '!':
            lex->cur.kind = TOK_NOT;
            break;
        case '(':
            lex->cur.kind = TOK_LPAREN;
            break;
        case ')':
            lex->cur.kind = TOK_RPAREN;
            break;
        case '[':
            lex->cur.kind = TOK_LBRACKET;
            break;
        case ']':
            lex->cur.kind = TOK_RBRACKET;
            break;
        case '{':
            lex->cur.kind = TOK_LBRACE;
            break;
        case '}':
            lex->cur.kind = TOK_RBRACE;
            break;
        case ',':
            lex->cur.kind = TOK_COMMA;
            break;
        default:
            lex->cur.kind = TOK_EOF;
            break;
    }
}

static struct as_value as_parse_or(struct as_env* env, struct as_lexer* lex);
static struct as_value as_parse_and(struct as_env* env, struct as_lexer* lex);
static struct as_value as_parse_equality(struct as_env* env,
                                         struct as_lexer* lex);
static struct as_value as_parse_relational(struct as_env* env,
                                           struct as_lexer* lex);
static struct as_value as_parse_add(struct as_env* env, struct as_lexer* lex);
static struct as_value as_parse_mul(struct as_env* env, struct as_lexer* lex);
static struct as_value as_parse_unary(struct as_env* env, struct as_lexer* lex);
static struct as_value as_parse_primary(struct as_env* env,
                                        struct as_lexer* lex);

/* Lowest precedence: || and or. */
static struct as_value as_parse_or(struct as_env* env, struct as_lexer* lex) {
    struct as_value left = as_parse_and(env, lex);
    while (lex->cur.kind == TOK_OR) {
        as_lexer_next(lex);
        struct as_value right = as_parse_and(env, lex);
        int res = as_value_is_truthy(&left) || as_value_is_truthy(&right);
        as_value_free(&left);
        as_value_free(&right);
        left.type = AS_TYPE_BOOL;
        left.is_null = 0;
        left.u.boolean = res;
    }
    return left;
}

/* && and and, one step tighter than ||. */
static struct as_value as_parse_and(struct as_env* env, struct as_lexer* lex) {
    struct as_value left = as_parse_equality(env, lex);
    while (lex->cur.kind == TOK_AND) {
        as_lexer_next(lex);
        struct as_value right = as_parse_equality(env, lex);
        int res = as_value_is_truthy(&left) && as_value_is_truthy(&right);
        as_value_free(&left);
        as_value_free(&right);
        left.type = AS_TYPE_BOOL;
        left.is_null = 0;
        left.u.boolean = res;
    }
    return left;
}

static struct as_value as_parse_equality(struct as_env* env,
                                         struct as_lexer* lex) {
    struct as_value left = as_parse_relational(env, lex);
    while (lex->cur.kind == TOK_EQ || lex->cur.kind == TOK_NEQ) {
        enum as_tok_kind op = lex->cur.kind;
        as_lexer_next(lex);
        struct as_value right = as_parse_relational(env, lex);
        int eq = 0;

        if (left.is_null || right.is_null) {
            eq = (left.is_null && right.is_null);
        } else if (left.type == AS_TYPE_STRING &&
                   right.type == AS_TYPE_STRING) {
            eq = (left.u.str != 0 && right.u.str != 0 &&
                  strcmp(left.u.str, right.u.str) == 0);
        } else if (left.type == AS_TYPE_BOOL && right.type == AS_TYPE_BOOL) {
            eq = (left.u.boolean == right.u.boolean);
        } else {
            eq = (as_value_to_i32(&left) == as_value_to_i32(&right));
        }

        int res = (op == TOK_EQ) ? eq : !eq;
        as_value_free(&left);
        as_value_free(&right);
        left.type = AS_TYPE_BOOL;
        left.is_null = 0;
        left.u.boolean = res;
    }
    return left;
}

static struct as_value as_parse_relational(struct as_env* env,
                                           struct as_lexer* lex) {
    struct as_value left = as_parse_add(env, lex);
    while (lex->cur.kind == TOK_LT || lex->cur.kind == TOK_LTE ||
           lex->cur.kind == TOK_GT || lex->cur.kind == TOK_GTE) {
        enum as_tok_kind op = lex->cur.kind;
        as_lexer_next(lex);
        struct as_value right = as_parse_add(env, lex);
        int cmp = 0;

        if (left.type == AS_TYPE_STRING && right.type == AS_TYPE_STRING) {
            cmp = strcmp(left.u.str ? left.u.str : "",
                         right.u.str ? right.u.str : "");
        } else {
            int32_t a = as_value_to_i32(&left);
            int32_t b = as_value_to_i32(&right);
            cmp = (a < b) ? -1 : (a > b ? 1 : 0);
        }

        int res = 0;
        if (op == TOK_LT)
            res = (cmp < 0);
        else if (op == TOK_LTE)
            res = (cmp <= 0);
        else if (op == TOK_GT)
            res = (cmp > 0);
        else if (op == TOK_GTE)
            res = (cmp >= 0);

        as_value_free(&left);
        as_value_free(&right);
        left.type = AS_TYPE_BOOL;
        left.is_null = 0;
        left.u.boolean = res;
    }
    return left;
}

/* Addition and subtraction, which also joins strings. */
static struct as_value as_parse_add(struct as_env* env, struct as_lexer* lex) {
    struct as_value left = as_parse_mul(env, lex);
    while (lex->cur.kind == TOK_PLUS || lex->cur.kind == TOK_MINUS) {
        enum as_tok_kind op = lex->cur.kind;
        as_lexer_next(lex);
        struct as_value right = as_parse_mul(env, lex);

        if (op == TOK_PLUS) {
            if (left.type == AS_TYPE_STRING || right.type == AS_TYPE_STRING) {
                char b1[AS_LINE_MAX];
                char b2[AS_LINE_MAX];
                const char* s1;
                const char* s2;

                if (left.type == AS_TYPE_STRING && left.u.str != 0) {
                    s1 = left.u.str;
                } else {
                    as_value_format(&left, b1, sizeof(b1), 0);
                    s1 = b1;
                }
                if (right.type == AS_TYPE_STRING && right.u.str != 0) {
                    s2 = right.u.str;
                } else {
                    as_value_format(&right, b2, sizeof(b2), 0);
                    s2 = b2;
                }

                int l1 = (int)strlen(s1);
                int l2 = (int)strlen(s2);
                char* joined = (char*)kmalloc(l1 + l2 + 1);
                if (joined != 0) {
                    memcpy(joined, s1, l1);
                    memcpy(joined + l1, s2, l2 + 1);
                }
                as_value_free(&left);
                as_value_free(&right);
                left.type = AS_TYPE_STRING;
                left.is_null = (joined == 0);
                left.u.str = joined;
            } else {
                int32_t sum = as_value_to_i32(&left) + as_value_to_i32(&right);
                as_value_free(&left);
                as_value_free(&right);
                left.type = AS_TYPE_INT32;
                left.is_null = 0;
                left.u.i32 = sum;
            }
        } else {
            int32_t diff = as_value_to_i32(&left) - as_value_to_i32(&right);
            as_value_free(&left);
            as_value_free(&right);
            left.type = AS_TYPE_INT32;
            left.is_null = 0;
            left.u.i32 = diff;
        }
    }
    return left;
}

/* Multiplication, division and remainder. */
static struct as_value as_parse_mul(struct as_env* env, struct as_lexer* lex) {
    struct as_value left = as_parse_unary(env, lex);
    while (lex->cur.kind == TOK_STAR || lex->cur.kind == TOK_SLASH ||
           lex->cur.kind == TOK_PERCENT) {
        enum as_tok_kind op = lex->cur.kind;
        as_lexer_next(lex);
        struct as_value right = as_parse_unary(env, lex);
        int32_t a = as_value_to_i32(&left);
        int32_t b = as_value_to_i32(&right);
        int32_t res = 0;

        if (op == TOK_STAR) {
            res = a * b;
        } else if (op == TOK_SLASH) {
            if (b == 0) {
                as_errorf(env, "division by zero.");
            } else {
                res = a / b;
            }
        } else if (op == TOK_PERCENT) {
            if (b == 0) {
                as_errorf(env, "modulo by zero.");
            } else {
                res = a % b;
            }
        }

        as_value_free(&left);
        as_value_free(&right);
        left.type = AS_TYPE_INT32;
        left.is_null = 0;
        left.u.i32 = res;
    }
    return left;
}

static struct as_value as_parse_unary(struct as_env* env,
                                      struct as_lexer* lex) {
    if (lex->cur.kind == TOK_MINUS) {
        as_lexer_next(lex);
        struct as_value val = as_parse_unary(env, lex);
        int32_t num = -as_value_to_i32(&val);
        as_value_free(&val);
        struct as_value res;
        as_value_init(&res, AS_TYPE_INT32, AS_TYPE_VOID);
        res.u.i32 = num;
        return res;
    }
    if (lex->cur.kind == TOK_NOT) {
        as_lexer_next(lex);
        struct as_value val = as_parse_unary(env, lex);
        int b = !as_value_is_truthy(&val);
        as_value_free(&val);
        struct as_value res;
        as_value_init(&res, AS_TYPE_BOOL, AS_TYPE_VOID);
        res.u.boolean = b;
        return res;
    }
    return as_parse_primary(env, lex);
}

static struct as_value as_parse_primary(struct as_env* env,
                                        struct as_lexer* lex) {
    struct as_value val;
    as_value_init(&val, AS_TYPE_INT32, AS_TYPE_VOID);

    if (lex->cur.kind == TOK_INT) {
        val.type = AS_TYPE_INT32;
        val.is_null = 0;
        val.u.i32 = lex->cur.int_val;
        as_lexer_next(lex);
        return val;
    }

    if (lex->cur.kind == TOK_STRING) {
        val.type = AS_TYPE_STRING;
        val.is_null = 0;
        val.u.str = as_strdup(lex->cur.text);
        as_lexer_next(lex);
        return val;
    }

    if (lex->cur.kind == TOK_TRUE) {
        val.type = AS_TYPE_BOOL;
        val.is_null = 0;
        val.u.boolean = 1;
        as_lexer_next(lex);
        return val;
    }

    if (lex->cur.kind == TOK_FALSE) {
        val.type = AS_TYPE_BOOL;
        val.is_null = 0;
        val.u.boolean = 0;
        as_lexer_next(lex);
        return val;
    }

    if (lex->cur.kind == TOK_NULL) {
        val.type = AS_TYPE_STRING;
        val.is_null = 1;
        val.u.str = 0;
        as_lexer_next(lex);
        return val;
    }

    if (lex->cur.kind == TOK_LPAREN) {
        as_lexer_next(lex);
        val = as_parse_or(env, lex);
        if (lex->cur.kind == TOK_RPAREN) {
            as_lexer_next(lex);
        }
        return val;
    }

    if (lex->cur.kind == TOK_LEN) {
        as_lexer_next(lex);
        if (lex->cur.kind == TOK_LPAREN) {
            as_lexer_next(lex);
        }
        struct as_value target = as_parse_or(env, lex);
        if (lex->cur.kind == TOK_RPAREN) {
            as_lexer_next(lex);
        }
        int len_val = 0;
        if (target.type == AS_TYPE_STRING && target.u.str != 0) {
            len_val = (int)strlen(target.u.str);
        } else if (target.type == AS_TYPE_ARRAY) {
            len_val = target.u.arr.count;
        }
        as_value_free(&target);
        val.type = AS_TYPE_INT32;
        val.is_null = 0;
        val.u.i32 = len_val;
        return val;
    }

    // Array literal; { 1, 2, 3 }
    if (lex->cur.kind == TOK_LBRACE) {
        as_lexer_next(lex);
        val.type = AS_TYPE_ARRAY;
        val.elem_type = AS_TYPE_INT32;
        val.is_null = 0;
        val.u.arr.items = 0;
        val.u.arr.count = 0;
        val.u.arr.capacity = 0;

        while (lex->cur.kind != TOK_RBRACE && lex->cur.kind != TOK_EOF) {
            struct as_value item = as_parse_or(env, lex);
            if (val.u.arr.count == 0) {
                val.elem_type = item.type;
            }
            as_array_append(&val, &item);
            as_value_free(&item);
            if (lex->cur.kind == TOK_COMMA) {
                as_lexer_next(lex);
            }
        }
        if (lex->cur.kind == TOK_RBRACE) {
            as_lexer_next(lex);
        }
        return val;
    }

    // Identifier and variable access
    if (lex->cur.kind == TOK_IDENT) {
        char name[AS_NAME_MAX];
        strncpy(name, lex->cur.text, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        as_lexer_next(lex);

        struct as_var* var = as_env_lookup(env, name);
        if (var == 0) {
            // A command that prints text acts as a string value here, so
            // "pwd + \"/programs\"" reads the directory the command shows.
            if (command_returns_output(name)) {
                as_run_command_value(env, name, &val);
            } else {
                as_errorf(env, "variable '%s' not found.", name);
            }
            return val;
        }

        // Array indexing: arr[index]
        if (lex->cur.kind == TOK_LBRACKET) {
            as_lexer_next(lex);
            struct as_value idx_val = as_parse_or(env, lex);
            if (lex->cur.kind == TOK_RBRACKET) {
                as_lexer_next(lex);
            }
            int idx = as_value_to_i32(&idx_val);
            as_value_free(&idx_val);

            if (var->val.type != AS_TYPE_ARRAY) {
                as_errorf(env, "'%s' is not an array.", name);
                return val;
            }
            if (idx < 0 || idx >= var->val.u.arr.count) {
                as_errorf(env, "array index %d out of bounds for '%s'.", idx,
                          name);
                return val;
            }
            as_value_copy(&val, &var->val.u.arr.items[idx]);
            return val;
        }

        as_value_copy(&val, &var->val);
        return val;
    }

    as_lexer_next(lex);
    return val;
}

/* Evaluate an expression string. */
static struct as_value as_eval_expression(struct as_env* env, const char* str) {
    struct as_lexer lex;
    lex.src = str;
    lex.pos = 0;
    as_lexer_next(&lex);
    return as_parse_or(env, &lex);
}

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

/* Does this line open a block? */
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

/* Find the matching ending for a block. */
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
static void as_exec_system_cmd(struct as_env* env, const char* line) {
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

/* Execute a "define" statement. */
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

/* Execute a "delete" statement. */
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

/* Execute a "print" statement. */
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

/* The natural range of a numeric type. */
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

/* Parse text that must be a whole number, with an optional sign. */
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

/* Execute an "input" statement: read a line and validate it for a variable. */
static void as_exec_input(struct as_env* env, const char* stmt) {
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
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        if (v_len < (int)sizeof(var_name) - 1) {
            var_name[v_len++] = *p;
        }
        p++;
    }
    var_name[v_len] = '\0';

    if (var_name[0] == '\0') {
        as_errorf(env, "input needs a variable name.");
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

/* Execute an assignment: <var> = <expr> or <var>[<index>] = <expr>. */
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

/* Execute a block of lines between start and end (inclusive). */
static int as_exec_block(struct as_env* env, struct as_line* lines, int start,
                         int end) {
    int i = start;
    while (i <= end) {
        if (env->error) {
            return -1;
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

                struct as_var* arr_var = as_env_lookup(env, arr_name);
                if (arr_var == 0 || arr_var->val.type != AS_TYPE_ARRAY) {
                    as_errorf(env, "'%s' is not an array.", arr_name);
                    i = done_line + 1;
                    continue;
                }

                struct as_var* iter_var = as_env_lookup(env, var_name);
                if (iter_var == 0) {
                    iter_var = as_env_define(
                        env, var_name, arr_var->val.elem_type, AS_TYPE_VOID);
                }

                int total_elems = arr_var->val.u.arr.count;
                env->loop_depth++;
                for (int k = 0; k < total_elems; k++) {
                    if (arr_var->val.type != AS_TYPE_ARRAY ||
                        arr_var->val.u.arr.items == 0 ||
                        k >= arr_var->val.u.arr.count) {
                        break;
                    }

                    as_value_copy(&iter_var->val, &arr_var->val.u.arr.items[k]);
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
