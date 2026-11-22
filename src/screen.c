#include "io.h"
#include "screen.h"

#include <stdint.h>

#define SCREEN_WIDTH 80    /* Number of character columns */
#define SCREEN_HEIGHT 25   /* Number of character rows */
#define VGA_MEMORY 0xB8000 /* Physical address of the VGA text buffer */

#define DEFAULT_ATTRIBUTE 0x0F /* Bright white text on a black background */

static int cursor_row = 0;
static int cursor_col = 0;

/* Turn a (row, column) pair into an index into the VGA memory array. */
static int cell_index(int row, int col) { return row * SCREEN_WIDTH + col; }

/* Write one character with a given color into one specific cell. */
static void put_char_at(int row, int col, char c, uint8_t attribute) {
    volatile uint16_t* vga = (volatile uint16_t*)VGA_MEMORY;

    vga[cell_index(row, col)] = ((uint16_t)attribute << 8) | (uint8_t)c;
}

/* Fill the whole screen with blank cells (one color, no letters). */
static void clear_cells(void) {
    for (int row = 0; row < SCREEN_HEIGHT; row++) {
        for (int col = 0; col < SCREEN_WIDTH; col++) {
            put_char_at(row, col, ' ', DEFAULT_ATTRIBUTE);
        }
    }
}

/* Move everything one line up, so the bottom line becomes empty. */
static void scroll(void) {
    volatile uint16_t* vga = (volatile uint16_t*)VGA_MEMORY;

    // Copy row 1 -> row 0, row 2 -> row 1, ... row 24 -> row 23
    for (int row = 1; row < SCREEN_HEIGHT; row++) {
        for (int col = 0; col < SCREEN_WIDTH; col++) {
            vga[cell_index(row - 1, col)] = vga[cell_index(row, col)];
        }
    }

    // Blank out the now-empty last row.
    for (int col = 0; col < SCREEN_WIDTH; col++) {
        put_char_at(SCREEN_HEIGHT - 1, col, ' ', DEFAULT_ATTRIBUTE);
    }

    cursor_row = SCREEN_HEIGHT - 1;
}

/*
 * Go to the start of the next line. If we were already on the last line,
 * the screen scrolls instead of moving further down.
 */
static void new_line(void) {
    cursor_col = 0;
    cursor_row++;

    if (cursor_row >= SCREEN_HEIGHT) {
        scroll();
    }
}

/*
 * Tell the VGA hardware where to draw the blinking cursor.
 * The hardware uses two registers (14 = high byte, 15 = low byte of the
 * cell number) selected via the command port 0x3D4, written through the
 * data port 0x3D5.
 */
static void update_cursor(void) {
    uint16_t position = (uint16_t)cell_index(cursor_row, cursor_col);

    outb(0x3D4, 14);                          // select "cursor location high"
    outb(0x3D5, (uint8_t)(position >> 8));    // write its high byte
    outb(0x3D4, 15);                          // select "cursor location low"
    outb(0x3D5, (uint8_t)(position & 0xFF));  // write its low byte
}

/* Move the cursor one cell left (wrapping to the previous row).
 * Returns 0 if it is already at the top-left corner. */
static int move_left(void) {
    if (cursor_col > 0) {
        cursor_col--;
    } else if (cursor_row > 0) {
        cursor_row--;
        cursor_col = SCREEN_WIDTH - 1;
    } else {
        return 0;
    }

    return 1;
}

/* Move the cursor one cell right (wrapping to the next row).
 * Returns 0 if it is already at the bottom-right corner. */
static int move_right(void) {
    if (cursor_col < SCREEN_WIDTH - 1) {
        cursor_col++;
    } else if (cursor_row < SCREEN_HEIGHT - 1) {
        cursor_row++;
        cursor_col = 0;
    } else {
        return 0;
    }

    return 1;
}

/* Print one character at the cursor, then advance the cursor. */
static void put_char(char c) {
    if (c == '\n') {
        new_line();
        return;
    }

    // Backspace: move one cell left, erase it, and stop.
    if (c == '\b') {
        if (move_left()) {
            put_char_at(cursor_row, cursor_col, ' ', DEFAULT_ATTRIBUTE);
        }
        return;
    }

    put_char_at(cursor_row, cursor_col, c, DEFAULT_ATTRIBUTE);
    cursor_col++;

    if (cursor_col >= SCREEN_WIDTH) {
        new_line();
    }
}

void screen_clear(void) {
    clear_cells();
    cursor_row = 0;
    cursor_col = 0;
    update_cursor();
}

void print(const char* s) {
    for (int i = 0; s[i] != '\0'; i++) {
        put_char(s[i]);
    }

    update_cursor();
}

void print_char(char c) {
    put_char(c);
    update_cursor();
}

void cursor_left(void) {
    move_left();
    update_cursor();
}

void cursor_right(void) {
    move_right();
    update_cursor();
}
