#include "tetris.h"

#include <stdint.h>

#include "clock.h"
#include "keyboard.h"
#include "libc.h"
#include "screen.h"
#include "timer.h"

/* Board dimensions, in cells; each cell is two screen columns wide. */
#define BOARD_WIDTH 10
#define BOARD_HEIGHT 20

/* Screen layout */
#define PLAYFIELD_TOP 1
#define PLAYFIELD_BOT 22
#define PLAYFIELD_LEFT 22
#define PLAYFIELD_RIGHT 43

#define SIDE_LEFT 45
#define SIDE_RIGHT 58
#define SIDE_TOP 1
#define SIDE_BOT 22

/* Timing. */
#define DEFAULT_DROP_TICKS 50    // ticks per gravity step at the start
#define FLASH_HALF_CYCLE_MS 125  // half of one flash, four flashes per row

/* Colours. */
#define COLOR_BORDER 0x0F
#define COLOR_BAR 0x70
#define COLOR_EMPTY 0x07
#define COLOR_TEXT 0x0F
#define COLOR_LABEL 0x0E
#define COLOR_DIVIDER 0x08
#define COLOR_GAMEOVER 0x4F

/* Box drawing characters (CP437). */
#define BOX_HORIZ ((char)0xCD)
#define BOX_VERT ((char)0xBA)
#define BOX_TOPLEFT ((char)0xC9)
#define BOX_TOPRIGHT ((char)0xBB)
#define BOX_BOTLEFT ((char)0xC8)
#define BOX_BOTRIGHT ((char)0xBC)
#define BOX_DIVIDER ((char)0xC4)

/* One cell of a piece, relative to the piece's top-left corner. */
struct block_offset {
    int r;
    int c;
};

/* The 7 tetrominoes: [piece][rotation][cell]. */
static const struct block_offset piece_shapes[7][4][4] = {
    // I piece: 4x1 bar
    {
        {{1, 0}, {1, 1}, {1, 2}, {1, 3}},
        {{0, 2}, {1, 2}, {2, 2}, {3, 2}},
        {{2, 0}, {2, 1}, {2, 2}, {2, 3}},
        {{0, 1}, {1, 1}, {2, 1}, {3, 1}},
    },
    // O piece: 2x2 square
    {
        {{0, 1}, {0, 2}, {1, 1}, {1, 2}},
        {{0, 1}, {0, 2}, {1, 1}, {1, 2}},
        {{0, 1}, {0, 2}, {1, 1}, {1, 2}},
        {{0, 1}, {0, 2}, {1, 1}, {1, 2}},
    },
    // Z piece
    {
        {{0, 0}, {0, 1}, {1, 1}, {1, 2}},
        {{0, 2}, {1, 1}, {1, 2}, {2, 1}},
        {{1, 0}, {1, 1}, {2, 1}, {2, 2}},
        {{0, 1}, {1, 0}, {1, 1}, {2, 0}},
    },
    // S piece
    {
        {{0, 1}, {0, 2}, {1, 0}, {1, 1}},
        {{0, 1}, {1, 1}, {1, 2}, {2, 2}},
        {{1, 1}, {1, 2}, {2, 0}, {2, 1}},
        {{0, 0}, {1, 0}, {1, 1}, {2, 1}},
    },
    // L piece
    {
        {{0, 0}, {1, 0}, {2, 0}, {2, 1}},
        {{0, 0}, {0, 1}, {0, 2}, {1, 0}},
        {{0, 0}, {0, 1}, {1, 1}, {2, 1}},
        {{0, 2}, {1, 0}, {1, 1}, {1, 2}},
    },
    // J piece
    {
        {{0, 1}, {1, 1}, {2, 0}, {2, 1}},
        {{0, 0}, {1, 0}, {1, 1}, {1, 2}},
        {{0, 0}, {0, 1}, {1, 0}, {2, 0}},
        {{0, 0}, {0, 1}, {0, 2}, {1, 2}},
    },
    // T piece
    {
        {{0, 1}, {1, 0}, {1, 1}, {1, 2}},
        {{0, 1}, {1, 1}, {1, 2}, {2, 1}},
        {{1, 0}, {1, 1}, {1, 2}, {2, 1}},
        {{0, 1}, {1, 0}, {1, 1}, {2, 1}},
    },
};

/* Colour of each piece, in the order above. */
static const uint8_t piece_colors[7] = {
    0x05,  // I: purple
    0x0E,  // O: yellow
    0x06,  // Z: orange
    0x0A,  // S: lime
    0x0D,  // L: pink
    0x09,  // J: light blue
    0x0C,  // T: light red
};

static uint8_t board[BOARD_HEIGHT][BOARD_WIDTH];

static int cur_piece;
static int cur_rot;
static int cur_r;
static int cur_c;

static int next_piece;
static int score;
static int lines_cleared;
static int game_over;
static int drop_interval;

static uint32_t rng_state = 0x54321;

static uint16_t saved_cells[SCREEN_CELLS];
static int saved_row = 0;
static int saved_col = 0;

/* Simple pseudo-random number generator (xorshift32). */
static uint32_t rand_next(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* Format and draw a number right-aligned within a fixed width. */
static void draw_number_right(int row, int col, int width, int val,
                              uint8_t attr) {
    char buf[16];
    char digits[10];
    int count = 0;
    int v = val;

    if (v == 0) {
        digits[count++] = '0';
    } else {
        while (v > 0) {
            digits[count++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }

    int start = width - count;
    for (int i = 0; i < start; i++) {
        buf[i] = ' ';
    }
    for (int i = 0; i < count; i++) {
        buf[start + i] = digits[count - 1 - i];
    }
    buf[width] = '\0';

    screen_puts_at(row, col, buf, attr);
}

/* Draw the white borders for both playfield and side panel. */
static void draw_borders(void) {
    screen_put_at(PLAYFIELD_TOP, PLAYFIELD_LEFT, BOX_TOPLEFT, COLOR_BORDER);
    screen_put_at(PLAYFIELD_TOP, PLAYFIELD_RIGHT, BOX_TOPRIGHT, COLOR_BORDER);
    screen_put_at(PLAYFIELD_BOT, PLAYFIELD_LEFT, BOX_BOTLEFT, COLOR_BORDER);
    screen_put_at(PLAYFIELD_BOT, PLAYFIELD_RIGHT, BOX_BOTRIGHT, COLOR_BORDER);

    for (int c = PLAYFIELD_LEFT + 1; c < PLAYFIELD_RIGHT; c++) {
        screen_put_at(PLAYFIELD_TOP, c, BOX_HORIZ, COLOR_BORDER);
        screen_put_at(PLAYFIELD_BOT, c, BOX_HORIZ, COLOR_BORDER);
    }
    for (int r = PLAYFIELD_TOP + 1; r < PLAYFIELD_BOT; r++) {
        screen_put_at(r, PLAYFIELD_LEFT, BOX_VERT, COLOR_BORDER);
        screen_put_at(r, PLAYFIELD_RIGHT, BOX_VERT, COLOR_BORDER);
    }

    screen_put_at(SIDE_TOP, SIDE_LEFT, BOX_TOPLEFT, COLOR_BORDER);
    screen_put_at(SIDE_TOP, SIDE_RIGHT, BOX_TOPRIGHT, COLOR_BORDER);
    screen_put_at(SIDE_BOT, SIDE_LEFT, BOX_BOTLEFT, COLOR_BORDER);
    screen_put_at(SIDE_BOT, SIDE_RIGHT, BOX_BOTRIGHT, COLOR_BORDER);

    for (int c = SIDE_LEFT + 1; c < SIDE_RIGHT; c++) {
        screen_put_at(SIDE_TOP, c, BOX_HORIZ, COLOR_BORDER);
        screen_put_at(SIDE_BOT, c, BOX_HORIZ, COLOR_BORDER);
    }
    for (int r = SIDE_TOP + 1; r < SIDE_BOT; r++) {
        screen_put_at(r, SIDE_LEFT, BOX_VERT, COLOR_BORDER);
        screen_put_at(r, SIDE_RIGHT, BOX_VERT, COLOR_BORDER);
    }
}

/* Draw the top and bottom bars. */
static void draw_bars(void) {
    screen_fill_row(0, ' ', COLOR_BAR);
    screen_puts_at(0, 0, "AdaOS Tetris", COLOR_BAR);

    screen_fill_row(SCREEN_ROWS - 1, ' ', COLOR_BAR);
    screen_puts_at(SCREEN_ROWS - 1, 0, "^E exit   ^R restart", COLOR_BAR);
}

/* Draw next piece preview in the side panel. */
static void draw_next_preview(void) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            screen_put_at(12 + r, 48 + c, ' ', COLOR_EMPTY);
        }
    }

    uint8_t color = piece_colors[next_piece];
    for (int i = 0; i < 4; i++) {
        int r = piece_shapes[next_piece][0][i].r;
        int c = piece_shapes[next_piece][0][i].c;
        int sc = 48 + c * 2;
        screen_put_at(12 + r, sc, '[', color);
        screen_put_at(12 + r, sc + 1, ']', color);
    }
}

/* Redraw the static contents and dynamic values in the side panel. */
static void update_side_panel(void) {
    screen_puts_at(3, 46, "  TETRIS  ", COLOR_LABEL);

    for (int c = 46; c <= 57; c++) {
        screen_put_at(4, c, BOX_DIVIDER, COLOR_DIVIDER);
    }

    screen_puts_at(5, 46, "  SCORE   ", COLOR_TEXT);
    draw_number_right(6, 46, 12, score, COLOR_TEXT);

    screen_puts_at(8, 46, "  LINES   ", COLOR_TEXT);
    draw_number_right(9, 46, 12, lines_cleared, COLOR_TEXT);

    screen_puts_at(11, 46, "   NEXT   ", COLOR_LABEL);
    draw_next_preview();

    for (int c = 46; c <= 57; c++) {
        screen_put_at(17, c, BOX_DIVIDER, COLOR_DIVIDER);
    }

    screen_puts_at(18, 46, " ^E Exit  ", COLOR_EMPTY);
    screen_puts_at(19, 46, " ^R Reset ", COLOR_EMPTY);
    screen_puts_at(20, 46, " Up Rot   ", COLOR_EMPTY);
    screen_puts_at(21, 46, " Dn Drop  ", COLOR_EMPTY);

    if (game_over) {
        screen_puts_at(15, 47, "GAME  OVER", COLOR_GAMEOVER);
    } else {
        screen_puts_at(15, 47, "          ", COLOR_EMPTY);
    }
}

/* Redraw all locked blocks in the playfield. */
static void draw_full_board(void) {
    for (int r = 0; r < BOARD_HEIGHT; r++) {
        for (int c = 0; c < BOARD_WIDTH; c++) {
            int sc = PLAYFIELD_LEFT + 1 + c * 2;
            int sr = PLAYFIELD_TOP + 1 + r;
            uint8_t color = board[r][c];

            if (color != 0) {
                screen_put_at(sr, sc, '[', color);
                screen_put_at(sr, sc + 1, ']', color);
            } else {
                screen_put_at(sr, sc, ' ', COLOR_EMPTY);
                screen_put_at(sr, sc + 1, ' ', COLOR_EMPTY);
            }
        }
    }
}

/* Check if the piece collides with walls, floor, or locked blocks. */
static int check_collision(int piece, int rot, int pr, int pc) {
    for (int i = 0; i < 4; i++) {
        int r = pr + piece_shapes[piece][rot][i].r;
        int c = pc + piece_shapes[piece][rot][i].c;

        if (c < 0 || c >= BOARD_WIDTH || r >= BOARD_HEIGHT) {
            return 1;
        }
        if (r >= 0 && board[r][c] != 0) {
            return 1;
        }
    }
    return 0;
}

/* Draw the active falling piece using ASCII '[' and ']' characters. */
static void draw_piece(void) {
    uint8_t color = piece_colors[cur_piece];
    for (int i = 0; i < 4; i++) {
        int r = cur_r + piece_shapes[cur_piece][cur_rot][i].r;
        int c = cur_c + piece_shapes[cur_piece][cur_rot][i].c;

        if (r >= 0 && r < BOARD_HEIGHT && c >= 0 && c < BOARD_WIDTH) {
            int sc = PLAYFIELD_LEFT + 1 + c * 2;
            int sr = PLAYFIELD_TOP + 1 + r;
            screen_put_at(sr, sc, '[', color);
            screen_put_at(sr, sc + 1, ']', color);
        }
    }
}

/* Erase the active falling piece from the screen. */
static void erase_piece(void) {
    for (int i = 0; i < 4; i++) {
        int r = cur_r + piece_shapes[cur_piece][cur_rot][i].r;
        int c = cur_c + piece_shapes[cur_piece][cur_rot][i].c;

        if (r >= 0 && r < BOARD_HEIGHT && c >= 0 && c < BOARD_WIDTH) {
            int sc = PLAYFIELD_LEFT + 1 + c * 2;
            int sr = PLAYFIELD_TOP + 1 + r;
            screen_put_at(sr, sc, ' ', COLOR_EMPTY);
            screen_put_at(sr, sc + 1, ' ', COLOR_EMPTY);
        }
    }
}

/* Try rotating the active piece with wall kicks. */
static void try_rotate(void) {
    int next_rot = (cur_rot + 1) % 4;

    if (!check_collision(cur_piece, next_rot, cur_r, cur_c)) {
        erase_piece();
        cur_rot = next_rot;
        draw_piece();
        return;
    }

    static const int kicks[] = {-1, 1, -2, 2};
    for (int i = 0; i < 4; i++) {
        int kc = cur_c + kicks[i];
        if (!check_collision(cur_piece, next_rot, cur_r, kc)) {
            erase_piece();
            cur_c = kc;
            cur_rot = next_rot;
            draw_piece();
            return;
        }
    }
}

/* Try shifting the active piece horizontally. */
static void try_move(int dc) {
    if (!check_collision(cur_piece, cur_rot, cur_r, cur_c + dc)) {
        erase_piece();
        cur_c += dc;
        draw_piece();
    }
}

/* Drop the active piece down by 1 row. Returns 1 on success, 0 if blocked. */
static int try_drop_step(void) {
    if (!check_collision(cur_piece, cur_rot, cur_r + 1, cur_c)) {
        erase_piece();
        cur_r++;
        draw_piece();
        return 1;
    }
    return 0;
}

/* Animate full rows: appear and disappear 4 times for 0.25s (1s total). */
static void animate_line_clear(const int is_full_row[BOARD_HEIGHT]) {
    for (int step = 0; step < 4; step++) {
        for (int r = 0; r < BOARD_HEIGHT; r++) {
            if (is_full_row[r]) {
                for (int c = 0; c < BOARD_WIDTH; c++) {
                    int sc = PLAYFIELD_LEFT + 1 + c * 2;
                    screen_put_at(PLAYFIELD_TOP + 1 + r, sc, ' ', COLOR_EMPTY);
                    screen_put_at(PLAYFIELD_TOP + 1 + r, sc + 1, ' ',
                                  COLOR_EMPTY);
                }
            }
        }
        timer_sleep(FLASH_HALF_CYCLE_MS);

        for (int r = 0; r < BOARD_HEIGHT; r++) {
            if (is_full_row[r]) {
                for (int c = 0; c < BOARD_WIDTH; c++) {
                    int sc = PLAYFIELD_LEFT + 1 + c * 2;
                    uint8_t color = board[r][c];
                    screen_put_at(PLAYFIELD_TOP + 1 + r, sc, '[', color);
                    screen_put_at(PLAYFIELD_TOP + 1 + r, sc + 1, ']', color);
                }
            }
        }
        timer_sleep(FLASH_HALF_CYCLE_MS);
    }
}

/* Lock the active piece, check full rows, and spawn next piece. */
static void lock_and_spawn(void) {
    uint8_t color = piece_colors[cur_piece];
    for (int i = 0; i < 4; i++) {
        int r = cur_r + piece_shapes[cur_piece][cur_rot][i].r;
        int c = cur_c + piece_shapes[cur_piece][cur_rot][i].c;
        if (r >= 0 && r < BOARD_HEIGHT && c >= 0 && c < BOARD_WIDTH) {
            board[r][c] = color;
        }
    }

    int full_count = 0;
    int is_full_row[BOARD_HEIGHT] = {0};
    for (int r = 0; r < BOARD_HEIGHT; r++) {
        int full = 1;
        for (int c = 0; c < BOARD_WIDTH; c++) {
            if (board[r][c] == 0) {
                full = 0;
                break;
            }
        }
        if (full) {
            is_full_row[r] = 1;
            full_count++;
        }
    }

    if (full_count > 0) {
        animate_line_clear(is_full_row);

        uint8_t new_board[BOARD_HEIGHT][BOARD_WIDTH] = {0};
        int nr = BOARD_HEIGHT - 1;
        for (int r = BOARD_HEIGHT - 1; r >= 0; r--) {
            if (!is_full_row[r]) {
                for (int c = 0; c < BOARD_WIDTH; c++) {
                    new_board[nr][c] = board[r][c];
                }
                nr--;
            }
        }
        memcpy(board, new_board, sizeof(board));

        static const int line_scores[] = {0, 100, 300, 500, 800};
        score += line_scores[full_count];
        lines_cleared += full_count;

        drop_interval = DEFAULT_DROP_TICKS - (lines_cleared / 5) * 3;
        if (drop_interval < 15) {
            drop_interval = 15;
        }

        draw_full_board();
        update_side_panel();
    }

    cur_piece = next_piece;
    next_piece = (int)(rand_next() % 7);
    cur_r = 0;
    cur_c = 3;
    cur_rot = 0;

    draw_next_preview();

    if (check_collision(cur_piece, cur_rot, cur_r, cur_c)) {
        game_over = 1;
        draw_piece();
        update_side_panel();
        return;
    }

    draw_piece();
}

/* Reset game state to initial values. */
static void game_restart(void) {
    memset(board, 0, sizeof(board));
    score = 0;
    lines_cleared = 0;
    game_over = 0;
    drop_interval = DEFAULT_DROP_TICKS;

    cur_piece = (int)(rand_next() % 7);
    next_piece = (int)(rand_next() % 7);
    cur_r = 0;
    cur_c = 3;
    cur_rot = 0;

    draw_full_board();
    draw_piece();
    update_side_panel();
}

/* Play until Ctrl+E, then give the screen back to the shell. */
void tetris_run(void) {
    screen_save(saved_cells, &saved_row, &saved_col);
    clock_pause();

    rng_state = timer_ticks() ^ 0xBEEF;
    if (rng_state == 0) {
        rng_state = 0x54321;
    }

    screen_clear();
    draw_bars();
    draw_borders();

    game_restart();

    uint32_t next_drop_tick = timer_ticks() + drop_interval;

    for (;;) {
        while (1) {
            int key = keyboard_poll();
            if (key == 0) {
                break;
            }

            if (key == KEY_CTRL_E) {
                goto cleanup;
            }

            if (key == KEY_CTRL_R) {
                game_restart();
                next_drop_tick = timer_ticks() + drop_interval;
                break;
            }

            if (!game_over) {
                if (key == KEY_UP || key == 'w' || key == 'W') {
                    try_rotate();
                } else if (key == KEY_LEFT || key == 'a' || key == 'A') {
                    try_move(-1);
                } else if (key == KEY_RIGHT || key == 'd' || key == 'D') {
                    try_move(1);
                } else if (key == KEY_DOWN || key == 's' || key == 'S') {
                    if (try_drop_step()) {
                        next_drop_tick = timer_ticks() + drop_interval;
                    } else {
                        lock_and_spawn();
                        next_drop_tick = timer_ticks() + drop_interval;
                    }
                } else if (key == ' ') {
                    while (try_drop_step()) {
                    }
                    lock_and_spawn();
                    next_drop_tick = timer_ticks() + drop_interval;
                }
            }
        }

        uint32_t now = timer_ticks();

        if (!game_over && now >= next_drop_tick) {
            if (!try_drop_step()) {
                lock_and_spawn();
            }
            next_drop_tick = now + drop_interval;
        }

        timer_sleep(10);
    }

cleanup:
    screen_restore(saved_cells, saved_row, saved_col);
    clock_resume();
}
