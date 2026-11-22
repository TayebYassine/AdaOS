#ifndef ADAOS_SCREEN_H
#define ADAOS_SCREEN_H

/* Give the hardware cursor its shape and make it visible. */
void screen_init(void);

/* Clear the entire screen and move the cursor to the top-left corner. */
void screen_clear(void);

/* Print a string at the current cursor position. */
void print(const char* s);

/* Print a single character at the current cursor position. */
void print_char(char c);

/* Move the cursor one cell left, without erasing anything. */
void cursor_left(void);

/* Move the cursor one cell right, without erasing anything. */
void cursor_right(void);

#endif /* ADAOS_SCREEN_H */
