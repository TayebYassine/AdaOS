#include "snake.h"

#include <stdint.h>

#include "clock.h"
#include "keyboard.h"
#include "libc.h"
#include "screen.h"
#include "timer.h"

// Snake game for AdaOS.
//
// The screen is split into three parts:
//   row 0         top bar: "Score: <score>" on the left, time in MM:SS on the
//   right rows 1-23     the playing field (with wrap-around mirror borders) row
//   24        bottom bar: "^E exit   ^R restart"
//
// The snake moves forward every 0.25 seconds in the current direction
// (controlled by arrow keys). It starts at length 3 moving east. Eating an
// apple (red) gives 5 score and grows the snake by 1; the next apple spawns
// at a random position 3 seconds after the previous one was eaten.
//
// Reaching 100 score displays a blue "You Win!" box. Running into the snake's
// body displays a blue "You Lost!" box.

// Playing field bounds
#define FIELD_ROW_MIN 1
#define FIELD_ROW_MAX (SCREEN_ROWS - 2)
#define FIELD_ROWS (FIELD_ROW_MAX - FIELD_ROW_MIN + 1)
#define FIELD_COLS SCREEN_COLS

// Timing in PIT ticks (100 ticks = 1 second)
#define STEP_TICKS 25
#define APPLE_DELAY_TICKS 300

/* VGA colors */
#define COLOR_BAR 0x70
#define COLOR_HEAD 0x02
#define COLOR_BODY 0x0A
#define COLOR_APPLE 0x0C
#define COLOR_EMPTY 0x07
#define COLOR_DIALOG 0x1F
#define CHAR_BLOCK ((char)0xDB)

#define MAX_SNAKE_LENGTH 2000

struct point {
    int row;
    int col;
};

static struct point snake[MAX_SNAKE_LENGTH];
static int snake_len = 3;

static int dir_r = 0;
static int dir_c = 1;
static int pending_dir_r = 0;
static int pending_dir_c = 1;

static int score = 0;
static int game_state = 0;  // 0 = playing, 1 = lost, 2 = won

static int apple_r = 0;
static int apple_c = 0;
static int apple_exists = 0;
static uint32_t apple_spawn_tick = 0;

static uint32_t start_ticks = 0;
static int final_elapsed = 0;

static uint32_t rng_state = 0x12345678;

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

/* Format an elapsed duration into MM:SS. */
static void format_time(int total_seconds, char* out) {
    int mm = total_seconds / 60;
    int ss = total_seconds % 60;

    out[0] = (char)('0' + ((mm / 10) % 10));
    out[1] = (char)('0' + (mm % 10));
    out[2] = ':';
    out[3] = (char)('0' + ((ss / 10) % 10));
    out[4] = (char)('0' + (ss % 10));
    out[5] = '\0';
}

/* Format the current score into "Score: <N>". */
static void format_score(int s, char* out) {
    strcpy(out, "Score: ");
    int len = 7;
    char digits[10];
    int count = 0;

    if (s == 0) {
        digits[count++] = '0';
    } else {
        int temp = s;
        while (temp > 0) {
            digits[count++] = (char)('0' + (temp % 10));
            temp /= 10;
        }
    }

    while (count > 0) {
        out[len++] = digits[--count];
    }
    out[len] = '\0';
}

/* Returns the total seconds elapsed in the current game. */
static int get_elapsed_seconds(void) {
    if (game_state != 0) {
        return final_elapsed;
    }

    uint32_t now = timer_ticks();
    if (now >= start_ticks) {
        return (int)((now - start_ticks) / 100);
    }
    return 0;
}

/* Draw the top bar with Score on left and time in MM:SS on right. */
static void draw_top_bar(void) {
    char score_buf[24];
    char time_buf[16];

    format_score(score, score_buf);
    format_time(get_elapsed_seconds(), time_buf);

    screen_fill_row(0, ' ', COLOR_BAR);
    screen_puts_at(0, 0, score_buf, COLOR_BAR);
    screen_puts_at(0, SCREEN_COLS - (int)strlen(time_buf), time_buf, COLOR_BAR);
}

/* Draw the bottom bar showing control shortcuts. */
static void draw_bottom_bar(void) {
    screen_fill_row(SCREEN_ROWS - 1, ' ', COLOR_BAR);
    screen_puts_at(SCREEN_ROWS - 1, 0, "^E exit   ^R restart", COLOR_BAR);
}

/* Draw a centered 3-row blue dialog rectangle with message. */
static void draw_dialog(const char* message) {
    int dialog_width = 20;
    int dialog_rows = 3;
    int start_row = (SCREEN_ROWS - dialog_rows) / 2;
    int start_col = (SCREEN_COLS - dialog_width) / 2;
    int msg_len = (int)strlen(message);
    int msg_col = start_col + (dialog_width - msg_len) / 2;

    for (int r = 0; r < dialog_rows; r++) {
        for (int c = 0; c < dialog_width; c++) {
            screen_put_at(start_row + r, start_col + c, ' ', COLOR_DIALOG);
        }
    }

    screen_puts_at(start_row + 1, msg_col, message, COLOR_DIALOG);
}

/* Check whether any segment of the snake is at (r, c). */
static int is_snake_at(int r, int c) {
    for (int i = 0; i < snake_len; i++) {
        if (snake[i].row == r && snake[i].col == c) {
            return 1;
        }
    }
    return 0;
}

/* Spawn an apple at a random unoccupied cell on the playing field. */
static void spawn_apple(void) {
    int r, c;
    int attempts = 0;

    do {
        r = FIELD_ROW_MIN + (int)(rand_next() % FIELD_ROWS);
        c = (int)(rand_next() % FIELD_COLS);
        attempts++;
    } while (is_snake_at(r, c) && attempts < 2000);

    apple_r = r;
    apple_c = c;
    apple_exists = 1;

    screen_put_at(apple_r, apple_c, CHAR_BLOCK, COLOR_APPLE);
}

/* Reset the game state to initial values and repaint the screen. */
static void game_restart(void) {
    score = 0;
    game_state = 0;
    final_elapsed = 0;
    start_ticks = timer_ticks();

    for (int r = FIELD_ROW_MIN; r <= FIELD_ROW_MAX; r++) {
        screen_fill_row(r, ' ', COLOR_EMPTY);
    }

    // Default snake: length 3, centered, moving east
    snake_len = 3;
    snake[0].row = 12;
    snake[0].col = 40;
    snake[1].row = 12;
    snake[1].col = 39;
    snake[2].row = 12;
    snake[2].col = 38;

    dir_r = 0;
    dir_c = 1;
    pending_dir_r = 0;
    pending_dir_c = 1;

    screen_put_at(snake[0].row, snake[0].col, CHAR_BLOCK, COLOR_HEAD);
    screen_put_at(snake[1].row, snake[1].col, CHAR_BLOCK, COLOR_BODY);
    screen_put_at(snake[2].row, snake[2].col, CHAR_BLOCK, COLOR_BODY);

    spawn_apple();

    draw_top_bar();
    draw_bottom_bar();
}

/* Advance the snake by one step in the current direction. */
static void step_snake(void) {
    int next_r = snake[0].row + dir_r;
    int next_c = snake[0].col + dir_c;

    if (next_c < 0) {
        next_c = FIELD_COLS - 1;
    } else if (next_c >= FIELD_COLS) {
        next_c = 0;
    }

    if (next_r < FIELD_ROW_MIN) {
        next_r = FIELD_ROW_MAX;
    } else if (next_r > FIELD_ROW_MAX) {
        next_r = FIELD_ROW_MIN;
    }

    int eating = (apple_exists && next_r == apple_r && next_c == apple_c);

    // Check collision with snake body. If not eating, the tail vacates.
    int check_limit = eating ? snake_len : (snake_len - 1);
    for (int i = 1; i < check_limit; i++) {
        if (snake[i].row == next_r && snake[i].col == next_c) {
            game_state = 1;
            final_elapsed = get_elapsed_seconds();
            draw_dialog("You Lost!");
            return;
        }
    }

    if (eating) {
        score += 5;
        apple_exists = 0;
        apple_spawn_tick = timer_ticks() + APPLE_DELAY_TICKS;

        if (snake_len < MAX_SNAKE_LENGTH) {
            for (int i = snake_len; i > 0; i--) {
                snake[i] = snake[i - 1];
            }
            snake_len++;
        }

        snake[0].row = next_r;
        snake[0].col = next_c;

        screen_put_at(snake[1].row, snake[1].col, CHAR_BLOCK, COLOR_BODY);
        screen_put_at(snake[0].row, snake[0].col, CHAR_BLOCK, COLOR_HEAD);

        draw_top_bar();

        if (score >= 100) {
            game_state = 2;
            final_elapsed = get_elapsed_seconds();
            draw_dialog("You Win!");
            return;
        }
    } else {
        screen_put_at(snake[snake_len - 1].row, snake[snake_len - 1].col, ' ',
                      COLOR_EMPTY);

        for (int i = snake_len - 1; i > 0; i--) {
            snake[i] = snake[i - 1];
        }

        snake[0].row = next_r;
        snake[0].col = next_c;

        // Old head becomes lime body, new head becomes dark green
        screen_put_at(snake[1].row, snake[1].col, CHAR_BLOCK, COLOR_BODY);
        screen_put_at(snake[0].row, snake[0].col, CHAR_BLOCK, COLOR_HEAD);
    }
}

void snake_run(void) {
    screen_save(saved_cells, &saved_row, &saved_col);
    clock_pause();

    rng_state = timer_ticks() ^ 0xACE1;
    if (rng_state == 0) {
        rng_state = 0x12345678;
    }

    game_restart();

    uint32_t next_step_tick = timer_ticks() + STEP_TICKS;

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
                next_step_tick = timer_ticks() + STEP_TICKS;
                break;
            }

            if (game_state == 0) {
                if ((key == KEY_UP || key == 'w' || key == 'W') && dir_r != 1) {
                    pending_dir_r = -1;
                    pending_dir_c = 0;
                } else if ((key == KEY_DOWN || key == 's' || key == 'S') &&
                           dir_r != -1) {
                    pending_dir_r = 1;
                    pending_dir_c = 0;
                } else if ((key == KEY_LEFT || key == 'a' || key == 'A') &&
                           dir_c != 1) {
                    pending_dir_r = 0;
                    pending_dir_c = -1;
                } else if ((key == KEY_RIGHT || key == 'd' || key == 'D') &&
                           dir_c != -1) {
                    pending_dir_r = 0;
                    pending_dir_c = 1;
                }
            }
        }

        uint32_t now = timer_ticks();

        if (!apple_exists && game_state == 0 && now >= apple_spawn_tick) {
            spawn_apple();
        }

        draw_top_bar();

        if (game_state == 0 && now >= next_step_tick) {
            dir_r = pending_dir_r;
            dir_c = pending_dir_c;

            step_snake();

            next_step_tick = now + STEP_TICKS;
        }

        timer_sleep(10);
    }

cleanup:
    screen_restore(saved_cells, saved_row, saved_col);
    clock_resume();
}
