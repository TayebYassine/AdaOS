#ifndef ADAOS_ADASCRIPT_H
#define ADAOS_ADASCRIPT_H

/* Run an AdaScript file (.as) at the given filesystem path.
 * Returns 0 on success, or non-zero on error. */
int adascript_run_file(const char* path);

/* Run an AdaScript passed as a null-terminated text string.
 * Returns 0 on success, or non-zero on error. */
int adascript_run_string(const char* script);

#endif /* ADAOS_ADASCRIPT_H */
