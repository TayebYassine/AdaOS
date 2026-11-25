#ifndef ADAOS_ADASCRIPT_COMMON_H
#define ADAOS_ADASCRIPT_COMMON_H

// Limits shared by the AdaScript interpreter: the longest variable name and
// the longest line of source or text a value may hold.

#define AS_NAME_MAX 32
#define AS_LINE_MAX 256

// Most items one array value can hold; as_array_append() refuses past this.
#define AS_ARRAY_MAX 256

#endif /* ADAOS_ADASCRIPT_COMMON_H */
