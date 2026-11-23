#include "editor.h"

#include <stdint.h>

#include "clock.h"
#include "fs.h"
#include "keyboard.h"
#include "kmalloc.h"
#include "libc.h"
#include "screen.h"

// The full-screen text editor behind "write".
//
// The shell's screen is copied out before anything is drawn and put back
// when the editor closes, so "write" leaves the shell exactly as it found
// it. While it runs the screen is repainted from three parts:
//
//   row 0         title bar: "Editing <name> - <bytes> bytes" (the shell's
//                 clock bar is paused meanwhile: this row is ours)
//   rows 1-23     the text itself
//   row 24        status bar: ^S and ^E on the left, the paging markers
//                 "<" and ">" on the right
//
// The text is kept as an array of fixed-width lines, one screen row each.
// A line may be wider than the screen, so the view has a horizontal
// offset (first_col) beside the vertical one (first_line): moving the
// cursor past either edge scrolls the view by one line or column, exactly
// like a terminal. The status bar says whether anything is hidden off
// screen: "<" when the view starts inside a line, ">" when a line runs
// past the right edge.

#define TEXT_TOP 1
#define TEXT_ROWS (SCREEN_ROWS - 2)
#define TEXT_COLS SCREEN_COLS

#define MAX_LINES 200
#define LINE_MAX 255
#define LINE_SIZE (LINE_MAX + 1)

#define COLOR_TEXT 0x0F
#define COLOR_BAR 0x70

static char* lines = 0;

static char path[FS_PATH_MAX];
static char title[FS_NAME_MAX];

static uint32_t line_count = 1;
static uint32_t cursor_line = 0;
static uint32_t cursor_col = 0;
static uint32_t first_line = 0;
static uint32_t first_col = 0;

static char note[24] = "";

static uint16_t saved_cells[SCREEN_CELLS];
static int saved_row = 0;
static int saved_col = 0;

static char* line_at(uint32_t index) { return lines + index * LINE_SIZE; }

static uint32_t line_length(uint32_t index) {
    return (uint32_t)strlen(line_at(index));
}

/* Bytes the file would hold, line breaks included. */
static uint32_t total_bytes(void) {
    uint32_t total = 0;

    for (uint32_t i = 0; i < line_count; i++) {
        total += line_length(i);

        if (i + 1 < line_count) {
            total++;
        }
    }

    return total;
}

/* Write a number in base 10 into out; returns its length. */
static uint32_t number_to_text(uint32_t value, char* out) {
    char digits[10];
    uint32_t count = 0;
    uint32_t length = 0;

    do {
        digits[count] = (char)('0' + (value % 10));
        count++;
        value /= 10;
    } while (value != 0);

    while (count > 0) {
        count--;
        out[length] = digits[count];
        length++;
    }

    out[length] = '\0';

    return length;
}

/* Put "text" on the end of "out"; returns the new length. */
static uint32_t append_text(char* out, uint32_t length, const char* text) {
    for (uint32_t i = 0; text[i] != '\0'; i++) {
        out[length] = text[i];
        length++;
    }

    return length;
}

/* The file's name for the title bar: whatever follows the last slash. */
static void set_title(const char* file) {
    const char* name = file;
    uint32_t length = 0;

    for (const char* p = file; *p != '\0'; p++) {
        if (*p == '/') {
            name = p + 1;
            length = 0;
        } else {
            length++;
        }
    }

    if (length == 0) {
        name = "/";
        length = 1;
    }
    if (length >= FS_NAME_MAX) {
        length = FS_NAME_MAX - 1;
    }

    memcpy(title, name, length);
    title[length] = '\0';
}

/* Draw the title bar: "Editing <name> - <bytes> bytes", black on white. */
static void draw_title(void) {
    char number[10];
    char text[64];
    uint32_t bytes = total_bytes();
    uint32_t length = 0;

    number_to_text(bytes, number);

    length = append_text(text, length, "Editing ");
    length = append_text(text, length, title);
    length = append_text(text, length, " - ");
    length = append_text(text, length, number);
    length = append_text(text, length, (bytes == 1) ? " byte" : " bytes");
    text[length] = '\0';

    screen_fill_row(0, ' ', COLOR_BAR);
    screen_puts_at(0, 0, text, COLOR_BAR);
}

/* Draw the visible lines from first_col on, blank where the file ends. */
static void draw_text(void) {
    for (int row = 0; row < TEXT_ROWS; row++) {
        uint32_t index = first_line + (uint32_t)row;
        int screen_row = TEXT_TOP + row;

        screen_fill_row(screen_row, ' ', COLOR_TEXT);

        if (index < line_count) {
            const char* line = line_at(index);
            uint32_t length = line_length(index);

            for (int col = 0; col < TEXT_COLS; col++) {
                uint32_t source = first_col + (uint32_t)col;

                if (source >= length) {
                    break;
                }

                screen_put_at(screen_row, col, line[source], COLOR_TEXT);
            }
        }
    }
}

/* Does any line run past the right edge of the view? */
static int hidden_on_right(void) {
    for (uint32_t i = 0; i < line_count; i++) {
        if (line_length(i) > first_col + TEXT_COLS) {
            return 1;
        }
    }

    return 0;
}

/* Draw the status bar: shortcuts on the left, paging markers on the right. */
static void draw_status(void) {
    screen_fill_row(SCREEN_ROWS - 1, ' ', COLOR_BAR);
    screen_puts_at(SCREEN_ROWS - 1, 0, "^S write   ^E exit", COLOR_BAR);

    if (first_col > 0) {
        screen_put_at(SCREEN_ROWS - 1, SCREEN_COLS - 2, '<', COLOR_BAR);
    }
    if (hidden_on_right()) {
        screen_put_at(SCREEN_ROWS - 1, SCREEN_COLS - 1, '>', COLOR_BAR);
    }

    if (note[0] != '\0') {
        screen_puts_at(SCREEN_ROWS - 1, 24, note, COLOR_BAR);
    }
}

/* Keep the cursor on screen by paging the view one line or column at a time. */
static void keep_cursor_visible(void) {
    if (cursor_line < first_line) {
        first_line = cursor_line;
    }
    if (cursor_line >= first_line + TEXT_ROWS) {
        first_line = cursor_line - TEXT_ROWS + 1;
    }

    if (cursor_col < first_col) {
        first_col = cursor_col;
    }
    if (cursor_col >= first_col + TEXT_COLS) {
        first_col = cursor_col - TEXT_COLS + 1;
    }
}

/* Repaint everything and park the hardware cursor at the insertion point. */
static void render(void) {
    keep_cursor_visible();

    draw_title();
    draw_text();
    draw_status();

    screen_move_to(TEXT_TOP + (int)(cursor_line - first_line),
                   (int)(cursor_col - first_col));
}

/* Split the file into lines; a final newline starts an empty last line. */
static void load_file(const struct fs_node* node) {
    line_count = 1;
    line_at(0)[0] = '\0';

    if (node == 0 || node->data == 0) {
        return;
    }

    for (uint32_t i = 0; i < node->size; i++) {
        char c = node->data[i];
        char* line;
        uint32_t length;

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            if (line_count == MAX_LINES) {
                break;
            }

            line_count++;
            line_at(line_count - 1)[0] = '\0';
            continue;
        }

        line = line_at(line_count - 1);
        length = (uint32_t)strlen(line);

        if (length < LINE_MAX) {
            line[length] = c;
            line[length + 1] = '\0';
        }
    }
}

/* Copy the buffer into the file. Returns 1 on success. */
static int save_file(void) {
    uint32_t size = total_bytes();
    char* buffer = (char*)kmalloc(size + 1);
    uint32_t pos = 0;

    if (buffer == 0) {
        memcpy(note, "out of memory", 14);
        return 0;
    }

    for (uint32_t i = 0; i < line_count; i++) {
        uint32_t length = line_length(i);

        memcpy(buffer + pos, line_at(i), length);
        pos += length;

        if (i + 1 < line_count) {
            buffer[pos] = '\n';
            pos++;
        }
    }
    buffer[pos] = '\0';

    int written = fs_write(path, buffer, size) != 0;

    kfree(buffer);

    if (written) {
        memcpy(note, "written", 8);
    } else {
        memcpy(note, "write failed", 13);
    }

    return written;
}

/* Move the cursor to the end of its line if the line got shorter. */
static void clamp_column(void) {
    uint32_t length = line_length(cursor_line);

    if (cursor_col > length) {
        cursor_col = length;
    }
}

/* Insert one character at the cursor. */
static void insert_char(char c) {
    char* line = line_at(cursor_line);
    uint32_t length = (uint32_t)strlen(line);

    if (length >= LINE_MAX) {
        return;
    }

    for (uint32_t i = length + 1; i > cursor_col; i--) {
        line[i] = line[i - 1];
    }

    line[cursor_col] = c;
    cursor_col++;
}

/* Enter: break the line at the cursor and start a new one. */
static void split_line(void) {
    char* line;
    char* next;
    uint32_t tail;

    if (line_count >= MAX_LINES) {
        return;
    }

    line = line_at(cursor_line);

    for (uint32_t i = line_count; i > cursor_line + 1; i--) {
        memcpy(line_at(i), line_at(i - 1), LINE_SIZE);
    }

    line_count++;

    next = line_at(cursor_line + 1);
    tail = (uint32_t)strlen(line + cursor_col);
    memcpy(next, line + cursor_col, tail + 1);
    line[cursor_col] = '\0';

    cursor_line++;
    cursor_col = 0;
}

/* Backspace: delete the character before the cursor, joining lines. */
static void delete_char(void) {
    char* line = line_at(cursor_line);

    if (cursor_col > 0) {
        for (uint32_t i = cursor_col - 1; line[i] != '\0'; i++) {
            line[i] = line[i + 1];
        }

        cursor_col--;
        return;
    }

    if (cursor_line == 0) {
        return;
    }

    char* previous = line_at(cursor_line - 1);
    uint32_t previous_length = (uint32_t)strlen(previous);
    uint32_t length = (uint32_t)strlen(line);

    if (previous_length + length > LINE_MAX) {
        return;
    }

    memcpy(previous + previous_length, line, length + 1);

    for (uint32_t i = cursor_line; i + 1 < line_count; i++) {
        memcpy(line_at(i), line_at(i + 1), LINE_SIZE);
    }

    line_count--;
    cursor_line--;
    cursor_col = previous_length;
}

/* Up/Down move a line; Left/Right cross line ends like a normal editor. */
static void move_up(void) {
    if (cursor_line == 0) {
        return;
    }

    cursor_line--;
    clamp_column();
}

static void move_down(void) {
    if (cursor_line + 1 >= line_count) {
        return;
    }

    cursor_line++;
    clamp_column();
}

static void move_left(void) {
    if (cursor_col > 0) {
        cursor_col--;
        return;
    }

    if (cursor_line > 0) {
        cursor_line--;
        cursor_col = line_length(cursor_line);
    }
}

static void move_right(void) {
    if (cursor_col < line_length(cursor_line)) {
        cursor_col++;
        return;
    }

    if (cursor_line + 1 < line_count) {
        cursor_line++;
        cursor_col = 0;
    }
}

/* Run the editor on one file until Ctrl+E. */
void editor_open(const char* file) {
    struct fs_node* node;
    uint32_t length = (uint32_t)strlen(file);

    if (length == 0 || length >= FS_PATH_MAX) {
        print("Error: that file name is not usable.\n");
        return;
    }

    node = fs_lookup(file);

    if (node != 0) {
        if (node->kind == FS_DIR) {
            print("Error: that path is a directory.\n");
            return;
        }
    } else if (!fs_can_write(file)) {
        return;
    }

    lines = (char*)kmalloc(MAX_LINES * LINE_SIZE);
    if (lines == 0) {
        print("Error: out of memory.\n");
        return;
    }

    memcpy(path, file, length + 1);
    set_title(file);
    note[0] = '\0';

    cursor_line = 0;
    cursor_col = 0;
    first_line = 0;
    first_col = 0;
    load_file(node);

    screen_save(saved_cells, &saved_row, &saved_col);

    clock_pause();

    for (;;) {
        render();

        int key = keyboard_getchar();

        note[0] = '\0';

        if (key == KEY_CTRL_E) {
            break;
        }
        if (key == KEY_CTRL_S) {
            save_file();
        } else if (key == '\n') {
            split_line();
        } else if (key == '\b') {
            delete_char();
        } else if (key == KEY_UP) {
            move_up();
        } else if (key == KEY_DOWN) {
            move_down();
        } else if (key == KEY_LEFT) {
            move_left();
        } else if (key == KEY_RIGHT) {
            move_right();
        } else if (key >= ' ' && key < 128) {
            insert_char((char)key);
        }
    }

    screen_restore(saved_cells, saved_row, saved_col);

    clock_resume();

    kfree(lines);
    lines = 0;
}
