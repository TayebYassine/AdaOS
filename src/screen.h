#ifndef ADAOS_SCREEN_H
#define ADAOS_SCREEN_H

/* Clear the entire screen with blank spaces and move the cursor back to the top-left corner. */
void screen_clear(void);

/* Print a string at the current cursor position. */
void print(const char *s);

/* Print a single character at the current cursor position. */
void print_char(char c);

#endif /* ADAOS_SCREEN_H */
