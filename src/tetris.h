#ifndef ADAOS_TETRIS_H
#define ADAOS_TETRIS_H

// The Tetris game: a full-screen program with a bordered playfield and a side
// panel, driving the falling pieces from the timer tick and the keyboard. It
// owns the screen while it runs and hands it back to the shell on exit.

/* Start the Tetris game. */
void tetris_run(void);

#endif /* ADAOS_TETRIS_H */
