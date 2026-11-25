#ifndef ADAOS_ADASCRIPT_VALUE_H
#define ADAOS_ADASCRIPT_VALUE_H

#include <stdint.h>

#include "as_common.h"

// AdaScript values: the tagged union every expression evaluates to. A value is
// a number of one of the fixed-width types, a boolean, a heap string, or an
// array of other values. The helpers below create, copy, free, convert and
// format them; strings and arrays own heap memory.

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

/* Duplicate a string using kernel heap memory. */
char* as_strdup(const char* s);

/* Initialize a value with defaults. */
void as_value_init(struct as_value* val, enum as_type type,
                   enum as_type elem_type);

/* Free any dynamic memory associated with a value. */
void as_value_free(struct as_value* val);

/* Deep-copy a value from src to dst, releasing whatever dst held before. */
void as_value_copy(struct as_value* dst, const struct as_value* src);

/* Append an item to an array. */
int as_array_append(struct as_value* arr, const struct as_value* item);

/* Convert value to signed 32-bit integer. */
int32_t as_value_to_i32(const struct as_value* val);

/* Check if a value evaluates to boolean true. */
int as_value_is_truthy(const struct as_value* val);

/* Copy at most max-1 bytes of src into buf and always terminate it. */
void as_copy_text(char* buf, const char* src, int max);

/* Format a value into a human-readable text string. */
void as_value_format(const struct as_value* val, char* buf, int max,
                     int quote_strings);

/* Cast and assign src value into dst variable according to dst's type. */
void as_value_cast_assign(struct as_value* dst, const struct as_value* src);

#endif /* ADAOS_ADASCRIPT_VALUE_H */
