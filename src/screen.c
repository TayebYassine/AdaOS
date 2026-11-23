#include "screen.h"

#include <stdint.h>

#include "io.h"

// VGA text-mode screen driver: writes characters into the text buffer
// at 0xB8000 (80x25 cells, one word each: character + color), tracks
// the cursor, scrolls at the bottom edge, and keeps the hardware
// cursor on screen.

/* Number of character columns (SCREEN_COLS in screen.h) */
#define SCREEN_WIDTH SCREEN_COLS
/* Number of character rows (SCREEN_ROWS in screen.h) */
#define SCREEN_HEIGHT SCREEN_ROWS
/* Physical address of the VGA text buffer */
#define VGA_MEMORY 0xB8000

/* Bright white text on a black background */
#define DEFAULT_ATTRIBUTE 0x0F

/* CRTC registers for the cursor's shape and visibility */
#define CRTC_CURSOR_START 0x0A
#define CRTC_CURSOR_END 0x0B

/* Index port of the CRT controller (0x3D4/0x3D5 on colour text mode) */
#define CRTC_INDEX_PORT 0x3D4
#define CRTC_DATA_PORT 0x3D5

static int cursor_row = 0;
static int cursor_col = 0;

/* How many rows at the top print() must not touch. */
static int top_row = 0;

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

    for (int row = top_row + 1; row < SCREEN_HEIGHT; row++) {
        for (int col = 0; col < SCREEN_WIDTH; col++) {
            vga[cell_index(row - 1, col)] = vga[cell_index(row, col)];
        }
    }

    for (int col = 0; col < SCREEN_WIDTH; col++) {
        put_char_at(SCREEN_HEIGHT - 1, col, ' ', DEFAULT_ATTRIBUTE);
    }

    cursor_row = SCREEN_HEIGHT - 1;
}

/* Go to the start of the next line, scrolling if already at the bottom. */
static void new_line(void) {
    cursor_col = 0;
    cursor_row++;

    if (cursor_row >= SCREEN_HEIGHT) {
        scroll();
    }
}

/* Tell the VGA hardware where to draw the cursor. */
static void update_cursor(void) {
    uint16_t position = (uint16_t)cell_index(cursor_row, cursor_col);

    outb(CRTC_INDEX_PORT, 14);
    outb(CRTC_DATA_PORT, (uint8_t)(position >> 8));
    outb(CRTC_INDEX_PORT, 15);
    outb(CRTC_DATA_PORT, (uint8_t)(position & 0xFF));
}

/* Set the hardware cursor's shape: scanlines 14-15 of the character cell, i.e.
 * a blinking underscore. */
void screen_init(void) {
    outb(CRTC_INDEX_PORT, CRTC_CURSOR_START);
    outb(CRTC_DATA_PORT, 0x0E);
    outb(CRTC_INDEX_PORT, CRTC_CURSOR_END);
    outb(CRTC_DATA_PORT, 0x0F);
}

/* Move one cell left; returns 0 at the top-left corner. */
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

/* Move one cell right; returns 0 at the bottom-right corner. */
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

/* Blank the whole screen and put the cursor back at the top-left. */
void screen_clear(void) {
    clear_cells();
    cursor_row = top_row;
    cursor_col = 0;
    update_cursor();
}

/* Keep the top rows out of print()'s way. */
void screen_set_top(int rows) {
    if (rows < 0) {
        rows = 0;
    }
    if (rows >= SCREEN_HEIGHT) {
        rows = SCREEN_HEIGHT - 1;
    }

    top_row = rows;

    if (cursor_row < top_row) {
        cursor_row = top_row;
        cursor_col = 0;
        update_cursor();
    }
}

/* Print a string at the cursor. */
void print(const char* s) {
    for (int i = 0; s[i] != '\0'; i++) {
        put_char(s[i]);
    }

    update_cursor();
}

/* Print one character at the cursor. */
void print_char(char c) {
    put_char(c);
    update_cursor();
}

/* Move the cursor one cell left, without erasing anything. */
void cursor_left(void) {
    move_left();
    update_cursor();
}

/* Move the cursor one cell right, without erasing anything. */
void cursor_right(void) {
    move_right();
    update_cursor();
}

/* Draw one character into one cell, leaving the cursor alone. */
void screen_put_at(int row, int col, char c, uint8_t attribute) {
    if (row < 0 || row >= SCREEN_HEIGHT || col < 0 || col >= SCREEN_WIDTH) {
        return;
    }

    put_char_at(row, col, c, attribute);
}

/* Fill a whole row with one character and colour. */
void screen_fill_row(int row, char c, uint8_t attribute) {
    if (row < 0 || row >= SCREEN_HEIGHT) {
        return;
    }

    for (int col = 0; col < SCREEN_WIDTH; col++) {
        put_char_at(row, col, c, attribute);
    }
}

/* Draw a string on one row, clipped at the right edge. */
void screen_puts_at(int row, int col, const char* text, uint8_t attribute) {
    for (int i = 0; text[i] != '\0'; i++) {
        if (col + i >= SCREEN_WIDTH) {
            break;
        }
        if (col + i < 0) {
            continue;
        }

        put_char_at(row, col + i, text[i], attribute);
    }
}

/* Park the hardware cursor on a cell. */
void screen_move_to(int row, int col) {
    if (row < 0) {
        row = 0;
    }
    if (row >= SCREEN_HEIGHT) {
        row = SCREEN_HEIGHT - 1;
    }
    if (col < 0) {
        col = 0;
    }
    if (col >= SCREEN_WIDTH) {
        col = SCREEN_WIDTH - 1;
    }

    cursor_row = row;
    cursor_col = col;
    update_cursor();
}

/* Copy the whole screen and the cursor position out. */
void screen_save(uint16_t* cells, int* row, int* col) {
    volatile uint16_t* vga = (volatile uint16_t*)VGA_MEMORY;

    for (int i = 0; i < SCREEN_CELLS; i++) {
        cells[i] = vga[i];
    }

    if (row != 0) {
        *row = cursor_row;
    }
    if (col != 0) {
        *col = cursor_col;
    }
}

/* Put a saved screen back, cursor included. */
void screen_restore(const uint16_t* cells, int row, int col) {
    volatile uint16_t* vga = (volatile uint16_t*)VGA_MEMORY;

    for (int i = 0; i < SCREEN_CELLS; i++) {
        vga[i] = cells[i];
    }

    screen_move_to(row, col);
}
