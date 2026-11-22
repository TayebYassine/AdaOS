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
 * "outb" = OUT a Byte: send one byte to an I/O port.
 *
 * CPUs have a second address space separate from memory, used to talk to
 * hardware - "I/O ports". The VGA chip listens on ports 0x3D4/0x3D5.
 */
static void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
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

/* Print one character at the cursor, then advance the cursor. */
static void put_char(char c) {
    if (c == '\n') {
        new_line();
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
