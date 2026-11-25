#ifndef ADAOS_SCREEN_H
#define ADAOS_SCREEN_H

#include <stdint.h>

// The VGA text-mode driver: 80 columns by 25 rows of 16-bit cells (character
// plus colour byte) at physical 0xB8000. It keeps the cursor position, wraps at
// the right edge, scrolls at the bottom, programs the hardware cursor, and can
// divert its output into a heap buffer so a command's text can be captured
// instead of drawn.

/* The text mode the VGA buffer holds: 80 columns by 25 rows. */
#define SCREEN_COLS 80
#define SCREEN_ROWS 25
/* Cells in one whole screen. */
#define SCREEN_CELLS (SCREEN_COLS * SCREEN_ROWS)

/* Give the hardware cursor its shape and make it visible. */
void screen_init(void);

/* Clear the entire screen and move the cursor to the top-left corner. */
void screen_clear(void);

/* Print a string at the current cursor position. */
void print(const char* s);

/* Print a single character at the current cursor position. */
void print_char(char c);

/* Start collecting everything print()/print_char() would show; captures may be
 * nested, and each begin needs its own end. */
void screen_capture_begin(void);

/* Stop the newest capture and return its heap text, or 0 when there was
 * nothing to capture. The caller owns the string. */
char* screen_capture_end(void);

/* Move the cursor one cell left, without erasing anything. */
void cursor_left(void);

/* Move the cursor one cell right, without erasing anything. */
void cursor_right(void);

/* Draw one character into one cell; attribute is a VGA colour byte. */
void screen_put_at(int row, int col, char c, uint8_t attribute);

/* The cell at (row, col) as the VGA hardware holds it: character in the low
 * byte, colour in the high one. A cell outside the screen reads as 0. */
uint16_t screen_cell(int row, int col);

/* Fill a whole row with one character and colour. */
void screen_fill_row(int row, char c, uint8_t attribute);

/* Draw a string on one row, stopping at the right edge of the screen. */
void screen_puts_at(int row, int col, const char* text, uint8_t attribute);

/* Put the hardware cursor on a cell, clamped to the screen. */
void screen_move_to(int row, int col);

/* Keep the top "rows" rows out of print()'s way: the cursor goes below them and
 * scrolling stops there instead of at the top edge. */
void screen_set_top(int rows);

/* Copy the text buffer into "cells" and remember the cursor. */
void screen_save(uint16_t* cells, int* row, int* col);

/* Put a saved screen and cursor back, overwriting everything. */
void screen_restore(const uint16_t* cells, int row, int col);

#endif /* ADAOS_SCREEN_H */
