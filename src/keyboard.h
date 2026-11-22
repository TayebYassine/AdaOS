#ifndef ADAOS_KEYBOARD_H
#define ADAOS_KEYBOARD_H

/*
 * Block until the user presses a key, then return the ASCII character it
 * stands for ('\n' for Enter, '\b' for Backspace). Keys that do not
 * produce a character return one of the KEY_* values below. Handles
 * Shift and Caps Lock. Unknown keys are ignored.
 */

/* Arrow keys (returned for the0xE0-prefixed scancodes). */
#define KEY_UP 0x100
#define KEY_DOWN 0x101
#define KEY_LEFT 0x102
#define KEY_RIGHT 0x103

int keyboard_getchar(void);

#endif /* ADAOS_KEYBOARD_H */
