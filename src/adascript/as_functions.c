#include "as_functions.h"

#include <stdint.h>

#include "as_error.h"
#include "as_value.h"
#include "kmalloc.h"
#include "libc.h"

/* The functions the parser accepts, with the number of arguments each one
 * reads after its name. */
static const struct as_builtin {
    const char* name;
    int arity;
} as_builtin_table[] = {
    {"lengthof", 1},      {"sizeof", 1},          {"find", 2},
    {"startswith", 2},    {"endswith", 2},        {"includes", 2},
    {"toupper", 1},       {"tolower", 1},         {"tostring", 1},
    {"toint", 1},         {"tobool", 1},          {"isnumeric", 1},
    {"isalpha", 1},       {"isalphanumeric", 1},  {"replace", 3},
};

#define AS_BUILTIN_COUNT \
    (sizeof(as_builtin_table) / sizeof(as_builtin_table[0]))

/* What a value is, for the comparisons and conversions that care about the
 * kind rather than the exact width. */
enum as_kind {
    AS_KIND_NUMBER,
    AS_KIND_TEXT,
    AS_KIND_BOOL,
    AS_KIND_OTHER,
};

static enum as_kind as_kind_of(enum as_type type) {
    switch (type) {
        case AS_TYPE_INT8:
        case AS_TYPE_UINT8:
        case AS_TYPE_INT16:
        case AS_TYPE_UINT16:
        case AS_TYPE_INT32:
        case AS_TYPE_UINT32:
            return AS_KIND_NUMBER;
        case AS_TYPE_STRING:
            return AS_KIND_TEXT;
        case AS_TYPE_BOOL:
            return AS_KIND_BOOL;
        default:
            return AS_KIND_OTHER;
    }
}

static const char* as_type_label(enum as_type type) {
    switch (type) {
        case AS_TYPE_INT8:
            return "int8";
        case AS_TYPE_UINT8:
            return "uint8";
        case AS_TYPE_INT16:
            return "int16";
        case AS_TYPE_UINT16:
            return "uint16";
        case AS_TYPE_INT32:
            return "int32";
        case AS_TYPE_UINT32:
            return "uint32";
        case AS_TYPE_STRING:
            return "string";
        case AS_TYPE_BOOL:
            return "bool";
        case AS_TYPE_ARRAY:
            return "array";
        default:
            return "null";
    }
}

static struct as_value as_int_value(int32_t number) {
    struct as_value val;

    as_value_init(&val, AS_TYPE_INT32, AS_TYPE_VOID);
    val.u.i32 = number;

    return val;
}

static struct as_value as_bool_value(int truth) {
    struct as_value val;

    as_value_init(&val, AS_TYPE_BOOL, AS_TYPE_VOID);
    val.u.boolean = truth ? 1 : 0;

    return val;
}

static struct as_value as_text_value(char* text) {
    struct as_value val;

    as_value_init(&val, AS_TYPE_STRING, AS_TYPE_VOID);
    val.is_null = (text == 0);
    val.u.str = text;

    return val;
}

/* The value a function hands back after setting the error. */
static struct as_value as_failed_value(void) {
    struct as_value val;

    as_value_init(&val, AS_TYPE_INT32, AS_TYPE_VOID);
    val.is_null = 1;

    return val;
}

/* The text one argument holds, or 0 with the error set. */
static const char* as_text_arg(struct as_env* env, const struct as_value* arg,
                               const char* name) {
    if (arg->type != AS_TYPE_STRING || arg->is_null || arg->u.str == 0) {
        as_errorf(env, "%s needs text for its arguments.", name);
        return 0;
    }

    return arg->u.str;
}

static int as_text_equals_ci(const char* a, const char* b) {
    int i = 0;

    while (a[i] != '\0' && b[i] != '\0') {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
        i++;
    }

    return a[i] == b[i];
}

/* Where needle first appears in text, or -1. */
static int as_find_text(const char* text, const char* needle) {
    int needle_len = (int)strlen(needle);

    if (needle_len == 0) {
        return 0;
    }

    for (int i = 0; text[i] != '\0'; i++) {
        if (strncmp(text + i, needle, (size_t)needle_len) == 0) {
            return i;
        }
    }

    return -1;
}

/* Would the two values be the same item, for find? */
static int as_values_equal(const struct as_value* a,
                           const struct as_value* b) {
    if (a->is_null || b->is_null) {
        return a->is_null && b->is_null;
    }

    switch (as_kind_of(a->type)) {
        case AS_KIND_TEXT:
            if (as_kind_of(b->type) != AS_KIND_TEXT) {
                return 0;
            }
            return strcmp(a->u.str ? a->u.str : "",
                          b->u.str ? b->u.str : "") == 0;
        case AS_KIND_BOOL:
            return as_kind_of(b->type) == AS_KIND_BOOL &&
                   a->u.boolean == b->u.boolean;
        case AS_KIND_NUMBER:
            return as_kind_of(b->type) == AS_KIND_NUMBER &&
                   as_value_to_i32(a) == as_value_to_i32(b);
        default:
            return 0;
    }
}

/* Read a whole decimal integer, refusing anything else. */
static int as_parse_integer(const char* text, int32_t* out) {
    uint32_t value = 0;
    uint32_t limit;
    const char* p = text;
    int negative = 0;
    int digits = 0;

    if (*p == '-' || *p == '+') {
        negative = (*p == '-');
        p++;
    }

    limit = negative ? 2147483648u : 2147483647u;

    for (; *p != '\0'; p++) {
        uint32_t digit;

        if (*p < '0' || *p > '9') {
            return 0;
        }
        digit = (uint32_t)(*p - '0');
        if (value > (limit - digit) / 10u) {
            return 0;
        }
        value = value * 10u + digit;
        digits++;
    }

    if (digits == 0) {
        return 0;
    }

    *out = negative ? (int32_t)(0u - value) : (int32_t)value;
    return 1;
}

static struct as_value as_call_lengthof(struct as_env* env,
                                        struct as_value* args) {
    if (args[0].type == AS_TYPE_ARRAY) {
        as_errorf(env,
                  "lengthof counts the characters of a string; use sizeof for "
                  "arrays.");
        return as_failed_value();
    }
    if (args[0].type != AS_TYPE_STRING || args[0].u.str == 0) {
        as_errorf(env, "lengthof needs a string.");
        return as_failed_value();
    }

    return as_int_value((int32_t)strlen(args[0].u.str));
}

static struct as_value as_call_sizeof(struct as_env* env,
                                      struct as_value* args) {
    if (args[0].type == AS_TYPE_STRING) {
        as_errorf(env,
                  "sizeof counts the items of an array; use lengthof for "
                  "strings.");
        return as_failed_value();
    }
    if (args[0].type != AS_TYPE_ARRAY) {
        as_errorf(env, "sizeof needs an array.");
        return as_failed_value();
    }

    return as_int_value(args[0].u.arr.count);
}

static struct as_value as_call_find(struct as_env* env,
                                    struct as_value* args) {
    if (args[0].type == AS_TYPE_ARRAY) {
        enum as_type elem = args[0].elem_type;

        if (as_kind_of(elem) != as_kind_of(args[1].type)) {
            as_errorf(env, "find cannot look for %s in an array of %s.",
                      as_type_label(args[1].type), as_type_label(elem));
            return as_failed_value();
        }

        for (int i = 0; i < args[0].u.arr.count; i++) {
            if (as_values_equal(&args[0].u.arr.items[i], &args[1])) {
                return as_int_value(i);
            }
        }

        return as_int_value(-1);
    }

    if (args[0].type == AS_TYPE_STRING && args[0].u.str != 0) {
        const char* needle = as_text_arg(env, &args[1], "find");

        if (needle == 0) {
            return as_failed_value();
        }

        return as_int_value(as_find_text(args[0].u.str, needle));
    }

    as_errorf(env, "find needs an array or a string to search.");
    return as_failed_value();
}

static struct as_value as_call_startswith(struct as_env* env,
                                          struct as_value* args) {
    const char* text = as_text_arg(env, &args[0], "startswith");
    const char* prefix;
    size_t prefix_len;

    if (text == 0) {
        return as_failed_value();
    }
    prefix = as_text_arg(env, &args[1], "startswith");
    if (prefix == 0) {
        return as_failed_value();
    }

    prefix_len = strlen(prefix);
    return as_bool_value(strncmp(text, prefix, prefix_len) == 0);
}

static struct as_value as_call_endswith(struct as_env* env,
                                        struct as_value* args) {
    const char* text = as_text_arg(env, &args[0], "endswith");
    const char* suffix;
    size_t text_len;
    size_t suffix_len;

    if (text == 0) {
        return as_failed_value();
    }
    suffix = as_text_arg(env, &args[1], "endswith");
    if (suffix == 0) {
        return as_failed_value();
    }

    text_len = strlen(text);
    suffix_len = strlen(suffix);
    if (suffix_len > text_len) {
        return as_bool_value(0);
    }

    return as_bool_value(strcmp(text + text_len - suffix_len, suffix) == 0);
}

static struct as_value as_call_includes(struct as_env* env,
                                        struct as_value* args) {
    const char* text = as_text_arg(env, &args[0], "includes");
    const char* needle;

    if (text == 0) {
        return as_failed_value();
    }
    needle = as_text_arg(env, &args[1], "includes");
    if (needle == 0) {
        return as_failed_value();
    }

    return as_bool_value(as_find_text(text, needle) >= 0);
}

static struct as_value as_change_case(struct as_env* env,
                                      struct as_value* args, int upper) {
    const char* name = upper ? "toupper" : "tolower";
    const char* text = as_text_arg(env, &args[0], name);
    size_t len;
    char* out;

    if (text == 0) {
        return as_failed_value();
    }

    len = strlen(text);
    out = (char*)kmalloc(len + 1);
    if (out == 0) {
        as_errorf(env, "not enough memory for %s.", name);
        return as_failed_value();
    }

    for (size_t i = 0; i < len; i++) {
        out[i] = (char)(upper ? toupper((unsigned char)text[i])
                              : tolower((unsigned char)text[i]));
    }
    out[len] = '\0';

    return as_text_value(out);
}

static struct as_value as_call_tostring(struct as_env* env,
                                        struct as_value* args) {
    char buf[AS_LINE_MAX];

    as_value_format(&args[0], buf, sizeof(buf), 0);
    (void)env;

    return as_text_value(as_strdup(buf));
}

static struct as_value as_call_toint(struct as_env* env,
                                     struct as_value* args) {
    const char* text;
    int32_t number;

    if (args[0].is_null) {
        as_errorf(env, "toint cannot convert null to an integer.");
        return as_failed_value();
    }
    if (as_kind_of(args[0].type) != AS_KIND_TEXT) {
        return as_int_value(as_value_to_i32(&args[0]));
    }

    text = as_text_arg(env, &args[0], "toint");
    if (text == 0) {
        return as_failed_value();
    }
    if (!as_parse_integer(text, &number)) {
        as_errorf(env, "toint cannot convert '%s' to an integer.", text);
        return as_failed_value();
    }

    return as_int_value(number);
}

static struct as_value as_call_tobool(struct as_env* env,
                                      struct as_value* args) {
    if (args[0].is_null) {
        return as_bool_value(0);
    }
    if (as_kind_of(args[0].type) == AS_KIND_BOOL) {
        return as_bool_value(args[0].u.boolean);
    }
    if (as_kind_of(args[0].type) == AS_KIND_NUMBER) {
        return as_bool_value(as_value_to_i32(&args[0]) != 0);
    }
    if (args[0].type == AS_TYPE_STRING && args[0].u.str != 0) {
        const char* text = args[0].u.str;
        int32_t number;

        if (as_text_equals_ci(text, "true")) {
            return as_bool_value(1);
        }
        if (as_text_equals_ci(text, "false")) {
            return as_bool_value(0);
        }
        if (as_parse_integer(text, &number)) {
            return as_bool_value(number != 0);
        }
        as_errorf(env, "tobool cannot convert '%s' to a bool.", text);
        return as_failed_value();
    }

    as_errorf(env, "tobool needs a bool, a number or text.");
    return as_failed_value();
}

static struct as_value as_call_isnumeric(struct as_env* env,
                                         struct as_value* args) {
    const char* text = as_text_arg(env, &args[0], "isnumeric");
    int i = 0;

    if (text == 0) {
        return as_failed_value();
    }

    while (text[i] >= '0' && text[i] <= '9') {
        i++;
    }

    return as_bool_value(i > 0 && text[i] == '\0');
}

static struct as_value as_call_isalpha(struct as_env* env,
                                       struct as_value* args) {
    const char* text = as_text_arg(env, &args[0], "isalpha");
    int i = 0;

    if (text == 0) {
        return as_failed_value();
    }

    while (text[i] != '\0' && (text[i] < '0' || text[i] > '9')) {
        i++;
    }

    return as_bool_value(i > 0 && text[i] == '\0');
}

static struct as_value as_call_isalphanumeric(struct as_env* env,
                                              struct as_value* args) {
    const char* text = as_text_arg(env, &args[0], "isalphanumeric");
    int digits = 0;
    int others = 0;

    if (text == 0) {
        return as_failed_value();
    }

    for (int i = 0; text[i] != '\0'; i++) {
        if (text[i] >= '0' && text[i] <= '9') {
            digits++;
        } else {
            others++;
        }
    }

    return as_bool_value(digits > 0 && others > 0);
}

static struct as_value as_call_replace(struct as_env* env,
                                       struct as_value* args) {
    const char* text = as_text_arg(env, &args[0], "replace");
    const char* from;
    const char* to;
    int from_len;
    int to_len;
    int found = 0;
    int out_len;
    int pos = 0;
    char* out;

    if (text == 0) {
        return as_failed_value();
    }
    from = as_text_arg(env, &args[1], "replace");
    if (from == 0) {
        return as_failed_value();
    }
    to = as_text_arg(env, &args[2], "replace");
    if (to == 0) {
        return as_failed_value();
    }

    from_len = (int)strlen(from);
    to_len = (int)strlen(to);

    if (from_len == 0) {
        return as_text_value(as_strdup(text));
    }

    for (int i = 0; text[i] != '\0';) {
        if (strncmp(text + i, from, (size_t)from_len) == 0) {
            found++;
            i += from_len;
        } else {
            i++;
        }
    }

    out_len = (int)strlen(text) + found * (to_len - from_len);
    if (out_len >= AS_LINE_MAX) {
        as_errorf(env, "replace would make a string longer than %d characters.",
                  AS_LINE_MAX - 1);
        return as_failed_value();
    }

    out = (char*)kmalloc((size_t)out_len + 1);
    if (out == 0) {
        as_errorf(env, "not enough memory for replace.");
        return as_failed_value();
    }

    for (int i = 0; text[i] != '\0';) {
        if (strncmp(text + i, from, (size_t)from_len) == 0) {
            if (to_len > 0) {
                memcpy(out + pos, to, (size_t)to_len);
            }
            pos += to_len;
            i += from_len;
        } else {
            out[pos++] = text[i++];
        }
    }
    out[pos] = '\0';

    return as_text_value(out);
}

int as_function_arity(const char* name) {
    for (size_t i = 0; i < AS_BUILTIN_COUNT; i++) {
        if (strcmp(as_builtin_table[i].name, name) == 0) {
            return as_builtin_table[i].arity;
        }
    }

    return -1;
}

struct as_value as_function_call(struct as_env* env, const char* name,
                                 struct as_value* args, int argc) {
    (void)argc;

    if (strcmp(name, "lengthof") == 0) {
        return as_call_lengthof(env, args);
    }
    if (strcmp(name, "sizeof") == 0) {
        return as_call_sizeof(env, args);
    }
    if (strcmp(name, "find") == 0) {
        return as_call_find(env, args);
    }
    if (strcmp(name, "startswith") == 0) {
        return as_call_startswith(env, args);
    }
    if (strcmp(name, "endswith") == 0) {
        return as_call_endswith(env, args);
    }
    if (strcmp(name, "includes") == 0) {
        return as_call_includes(env, args);
    }
    if (strcmp(name, "toupper") == 0) {
        return as_change_case(env, args, 1);
    }
    if (strcmp(name, "tolower") == 0) {
        return as_change_case(env, args, 0);
    }
    if (strcmp(name, "tostring") == 0) {
        return as_call_tostring(env, args);
    }
    if (strcmp(name, "toint") == 0) {
        return as_call_toint(env, args);
    }
    if (strcmp(name, "tobool") == 0) {
        return as_call_tobool(env, args);
    }
    if (strcmp(name, "isnumeric") == 0) {
        return as_call_isnumeric(env, args);
    }
    if (strcmp(name, "isalpha") == 0) {
        return as_call_isalpha(env, args);
    }
    if (strcmp(name, "isalphanumeric") == 0) {
        return as_call_isalphanumeric(env, args);
    }
    if (strcmp(name, "replace") == 0) {
        return as_call_replace(env, args);
    }

    as_errorf(env, "unknown function '%s'.", name);
    return as_failed_value();
}
