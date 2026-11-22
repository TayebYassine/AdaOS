#ifndef ADAOS_SCREEN_H
#define ADAOS_SCREEN_H

/* Clear the entire screen with blank spaces and move the cursor back to the top-left corner. */
void screen_clear(void);

/* Print a string at the current cursor position. */
void print(const char *s);

#endif /* ADAOS_SCREEN_H */
