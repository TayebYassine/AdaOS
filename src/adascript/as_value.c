#include "as_value.h"

#include <stdint.h>

#include "kmalloc.h"
#include "libc.h"

// AS_ARRAY_MAX, the array limit, lives in as_common.h with the other limits.

char* as_strdup(const char* s) {
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
void as_value_init(struct as_value* val, enum as_type type,
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
void as_value_free(struct as_value* val) {
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
void as_value_copy(struct as_value* dst, const struct as_value* src) {
    as_value_free(dst);
    as_value_copy_raw(dst, src);
}

/* Append an item to an array. */
int as_array_append(struct as_value* arr, const struct as_value* item) {
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
int32_t as_value_to_i32(const struct as_value* val) {
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
int as_value_is_truthy(const struct as_value* val) {
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
void as_copy_text(char* buf, const char* src, int max) {
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

/* Format a value into a human-readable text string. */
void as_value_format(const struct as_value* val, char* buf, int max,
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
void as_value_cast_assign(struct as_value* dst,
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
