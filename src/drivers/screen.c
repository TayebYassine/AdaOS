#include "screen.h"

#include <stdint.h>

#include "io.h"
#include "kmalloc.h"
#include "libc.h"

/* Physical address of the VGA text buffer. */
#define VGA_MEMORY 0xB8000

/* Bright white text on a black background. */
#define DEFAULT_ATTRIBUTE 0x0F

/* CRTC registers holding the cursor's shape. */
#define CRTC_CURSOR_START 0x0A
#define CRTC_CURSOR_END 0x0B

/* Cursor start value that turns the hardware cursor off (bit 5). */
#define CRTC_CURSOR_OFF 0x20

/* CRT controller ports in colour text mode. */
#define CRTC_INDEX_PORT 0x3D4
#define CRTC_DATA_PORT 0x3D5

static int cursor_row = 0;
static int cursor_col = 0;

/* The cell the hardware cursor was last put on; -1 until it is programmed. */
static int cursor_programmed = -1;

/* How many rows at the top print() must not touch. */
static int top_row = 0;

// Nested output captures: each grows its own heap buffer, and an allocation
// failure simply stops that capture where it is.
#define CAPTURE_MAX_DEPTH 8

struct capture_state {
    char* text;
    int length;
    int capacity;
};

static struct capture_state captures[CAPTURE_MAX_DEPTH];
static int capture_depth = 0;

/* Collect one character into the newest capture. */
static void capture_char(char c) {
    struct capture_state* state;

    if (capture_depth <= 0 || capture_depth > CAPTURE_MAX_DEPTH) {
        return;
    }

    state = &captures[capture_depth - 1];

    // A backspace takes back the character before it.
    if (c == '\b') {
        if (state->length > 0) {
            state->length--;
        }
        return;
    }

    if (state->length + 1 >= state->capacity) {
        int new_capacity = state->capacity == 0 ? 128 : state->capacity * 2;
        char* grown = (char*)kmalloc((size_t)new_capacity);

        if (grown == 0) {
            return;
        }
        if (state->text != 0) {
            memcpy(grown, state->text, (size_t)state->length);
            kfree(state->text);
        }
        state->text = grown;
        state->capacity = new_capacity;
    }

    state->text[state->length] = c;
    state->length++;
}

/* Start collecting output instead of drawing it. */
void screen_capture_begin(void) {
    if (capture_depth < CAPTURE_MAX_DEPTH) {
        captures[capture_depth].text = 0;
        captures[capture_depth].length = 0;
        captures[capture_depth].capacity = 0;
    }

    capture_depth++;
}

/* Stop the newest capture and hand its text to the caller. */
char* screen_capture_end(void) {
    struct capture_state* state;
    char* text;

    if (capture_depth <= 0) {
        return 0;
    }

    capture_depth--;

    if (capture_depth >= CAPTURE_MAX_DEPTH) {
        return 0;
    }

    state = &captures[capture_depth];
    text = state->text;

    if (text != 0) {
        if (state->length >= state->capacity) {
            state->length = state->capacity - 1;
        }
        text[state->length] = '\0';
    }

    state->text = 0;
    state->length = 0;
    state->capacity = 0;

    return text;
}

/* Turn a (row, column) pair into an index into the VGA memory array. */
static int cell_index(int row, int col) { return row * SCREEN_COLS + col; }

/* Write one character with a given color into one specific cell. */
static void put_char_at(int row, int col, char c, uint8_t attribute) {
    volatile uint16_t* vga = (volatile uint16_t*)VGA_MEMORY;

    vga[cell_index(row, col)] = ((uint16_t)attribute << 8) | (uint8_t)c;
}

/* Move "count" cells from "src" to "dst", two at a time. */
static void move_cells(volatile uint16_t* dst, volatile uint16_t* src,
                       int count) {
    volatile uint32_t* wide_dst = (volatile uint32_t*)dst;
    volatile uint32_t* wide_src = (volatile uint32_t*)src;
    int pairs = count / 2;

    for (int i = 0; i < pairs; i++) {
        wide_dst[i] = wide_src[i];
    }

    if (count % 2 != 0) {
        dst[count - 1] = src[count - 1];
    }
}

/* Write "value" into "count" cells, two at a time. */
static void fill_cells(volatile uint16_t* dst, int count, uint16_t value) {
    volatile uint32_t* wide_dst = (volatile uint32_t*)dst;
    uint32_t pair = ((uint32_t)value << 16) | value;
    int pairs = count / 2;

    for (int i = 0; i < pairs; i++) {
        wide_dst[i] = pair;
    }

    if (count % 2 != 0) {
        dst[count - 1] = value;
    }
}

/* Fill the whole screen with blank cells (one color, no letters). */
static void clear_cells(void) {
    fill_cells((volatile uint16_t*)VGA_MEMORY, SCREEN_CELLS,
               ((uint16_t)DEFAULT_ATTRIBUTE << 8) | ' ');
}

/* Move everything one line up, so the bottom line becomes empty. */
static void scroll(void) {
    volatile uint16_t* vga = (volatile uint16_t*)VGA_MEMORY;
    int rows = SCREEN_ROWS - top_row - 1;

    move_cells(vga + cell_index(top_row, 0), vga + cell_index(top_row + 1, 0),
               rows * SCREEN_COLS);

    fill_cells(vga + cell_index(SCREEN_ROWS - 1, 0), SCREEN_COLS,
               ((uint16_t)DEFAULT_ATTRIBUTE << 8) | ' ');

    cursor_row = SCREEN_ROWS - 1;
}

/* Go to the start of the next line, scrolling if already at the bottom. */
static void new_line(void) {
    cursor_col = 0;
    cursor_row++;

    if (cursor_row >= SCREEN_ROWS) {
        scroll();
    }
}

/* Tell the VGA hardware where to draw the cursor. */
static void update_cursor(void) {
    uint16_t position = (uint16_t)cell_index(cursor_row, cursor_col);

    // Programming the cursor costs four port writes, so skip it when the
    // cursor is already where it belongs.
    if ((int)position == cursor_programmed) {
        return;
    }

    cursor_programmed = (int)position;

    outb(CRTC_INDEX_PORT, 14);
    outb(CRTC_DATA_PORT, (uint8_t)(position >> 8));
    outb(CRTC_INDEX_PORT, 15);
    outb(CRTC_DATA_PORT, (uint8_t)(position & 0xFF));
}

/* Show or hide the blinking hardware cursor. */
void screen_set_cursor_visible(int visible) {
    outb(CRTC_INDEX_PORT, CRTC_CURSOR_START);
    outb(CRTC_DATA_PORT, visible ? 0x0E : CRTC_CURSOR_OFF);
}

/* Give the hardware cursor its shape: a blinking underscore. */
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
        cursor_col = SCREEN_COLS - 1;
    } else {
        return 0;
    }

    return 1;
}

/* Move one cell right; returns 0 at the bottom-right corner. */
static int move_right(void) {
    if (cursor_col < SCREEN_COLS - 1) {
        cursor_col++;
    } else if (cursor_row < SCREEN_ROWS - 1) {
        cursor_row++;
        cursor_col = 0;
    } else {
        return 0;
    }

    return 1;
}

/* Print one character at the cursor, then advance the cursor. */
static void put_char(char c) {
    if (capture_depth > 0) {
        capture_char(c);
        return;
    }

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

    if (cursor_col >= SCREEN_COLS) {
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
    if (rows >= SCREEN_ROWS) {
        rows = SCREEN_ROWS - 1;
    }

    top_row = rows;

    if (cursor_row < top_row) {
        cursor_row = top_row;
        cursor_col = 0;
        update_cursor();
    }
}

/* How many top rows print() currently leaves alone. */
int screen_get_top(void) { return top_row; }

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

/* Read one cell back, so a caller can skip rewriting what is already there. */
uint16_t screen_cell(int row, int col) {
    volatile uint16_t* vga = (volatile uint16_t*)VGA_MEMORY;

    if (row < 0 || row >= SCREEN_ROWS || col < 0 || col >= SCREEN_COLS) {
        return 0;
    }

    return vga[cell_index(row, col)];
}

/* Draw one character into one cell, leaving the cursor alone. */
void screen_put_at(int row, int col, char c, uint8_t attribute) {
    if (row < 0 || row >= SCREEN_ROWS || col < 0 || col >= SCREEN_COLS) {
        return;
    }

    put_char_at(row, col, c, attribute);
}

/* Fill a whole row with one character and colour. */
void screen_fill_row(int row, char c, uint8_t attribute) {
    if (row < 0 || row >= SCREEN_ROWS) {
        return;
    }

    fill_cells((volatile uint16_t*)VGA_MEMORY + cell_index(row, 0), SCREEN_COLS,
               ((uint16_t)attribute << 8) | (uint8_t)c);
}

/* Fill a rectangle of cells, corners included. */
void screen_fill_rect(int row1, int col1, int row2, int col2, char c,
                      uint8_t attribute) {
    uint16_t value = ((uint16_t)attribute << 8) | (uint8_t)c;

    if (row1 > row2) {
        int swap = row1;
        row1 = row2;
        row2 = swap;
    }
    if (col1 > col2) {
        int swap = col1;
        col1 = col2;
        col2 = swap;
    }

    if (row1 < 0) {
        row1 = 0;
    }
    if (col1 < 0) {
        col1 = 0;
    }
    if (row2 >= SCREEN_ROWS) {
        row2 = SCREEN_ROWS - 1;
    }
    if (col2 >= SCREEN_COLS) {
        col2 = SCREEN_COLS - 1;
    }
    if (row1 > row2 || col1 > col2) {
        return;
    }

    for (int row = row1; row <= row2; row++) {
        fill_cells((volatile uint16_t*)VGA_MEMORY + cell_index(row, col1),
                   col2 - col1 + 1, value);
    }
}

/* Draw a string on one row, clipped at the right edge. */
void screen_puts_at(int row, int col, const char* text, uint8_t attribute) {
    // A row outside the screen would address memory past the text buffer.
    if (row < 0 || row >= SCREEN_ROWS) {
        return;
    }

    for (int i = 0; text[i] != '\0'; i++) {
        if (col + i >= SCREEN_COLS) {
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
    if (row >= SCREEN_ROWS) {
        row = SCREEN_ROWS - 1;
    }
    if (col < 0) {
        col = 0;
    }
    if (col >= SCREEN_COLS) {
        col = SCREEN_COLS - 1;
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
